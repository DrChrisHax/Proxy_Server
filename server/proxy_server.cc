#include "proxy_server.h"

#include "protocol/detector.h"
#include "protocol/protocol.h"
#include "protocol/socks5.h"
#include "protocol/socks4.h"
#include "utils/string_utils.h"

#include <cstring>
#include <vector>
#include <iostream>

namespace server {

    ProxyServer::ProxyServer(const Config& cfg) : config_(cfg), logger_{cfg.log_file_name} {}

    void ProxyServer::Run() {
        (void)core::InitializeNetwork();

        socket_ = core::CreateListeningSocket(config_.bind_host, config_.port, config_.backlog);

        if (socket_ < 0) {
            logger_.critical(("Failed to create listening socket on port ") + std::to_string(config_.port));
            return; // We can't do anything without a listening socket
        }

        if (!core::SetSocketNonBlocking(socket_)) {
            logger_.error("[fd:" + std::to_string(socket_) + "] Failed to set listening socket non-blocking");
            CleanUpResources();
            return;
        }

        poller_ = core::CreateEventPoller();
        if (poller_ < 0) {
            logger_.critical("[fd:" + std::to_string(socket_) + "] Failed to create epoll instance");
            CleanUpResources();
            return;
        }

        if (!core::RegisterReadEvent(poller_, socket_)) {
            logger_.critical("[fd:" + std::to_string(socket_) + "] Failed to register listening socket with epoll");
            CleanUpResources();
            return;
        }


        core::PollEvent events[static_cast<std::size_t>(config_.max_events)];
        bool running = true;

        while (running) {
            const int TIMEOUT = -1; // Never timeout
            int n = core::WaitForEvents(poller_, events, config_.max_events, TIMEOUT);

            if (n < 0) {
                logger_.error("[fd:" + std::to_string(socket_) + "] epoll_wait failed");
                running = false;
                break;
            } else if (n == 0) {
                // Not expected with timeout = -1
                continue;
            }

            for (int i = 0; i < n; i++) {

                const auto fd = events[i].fd;
                const auto mask = events[i].mask;

                // ----------------------------
                // Event Error Handling
                // ----------------------------
                if (mask & (core::kEventError | core::kEventHangup | core::kEventOther)) {
                    if (fd == socket_) {
                        logger_.error("[fd:" + std::to_string(fd) + "] Listening socket fatal error/hangup");
                        running = false;
                        break;
                    }

                    auto it = connections_.find(fd);
                    if (it != connections_.end()) {
                        connections_.erase(it);
                    } else {
                        core::CloseSocket(fd);
                    }
                    continue;
                }

                // ----------------------------
                // 1. Handle New Connections
                // ----------------------------
                if (fd == socket_) {
                    std::vector<core::SocketIdentifier> accepted;
                    (void)AcceptNewClientConnection(socket_, poller_, logger_, connections_, &accepted);

                    for (auto a : accepted) {
                        core::ConnectionInfo info{};
                        if (core::GetSocketInfo(a, info)) {
                            std::string detail_log = "Connection details [fd:" + std::to_string(a) + "] " +
                                                    info.source_ip + ":" + std::to_string(info.source_port) +
                                                    " -> " + info.local_ip + ":" + std::to_string(info.local_port) +
                                                    " | Protocol: " + (info.is_ipv6 ? "IPv6" : "IPv4") +
                                                    " | SND_BUF: " + std::to_string(info.send_buffer_size) +
                                                    " | RCV_BUF: " + std::to_string(info.recv_buffer_size);         

                            logger_.info(detail_log);
                        } else {
                            std::string ip;
                            uint16_t port = 0;
                            if (core::SocketToAddress(a, ip, port)) {
                                logger_.info("New connection [fd:" + std::to_string(a) + "] from " + 
                                           ip + ":" + std::to_string(port) + 
                                           " (detailed metrics unavailable)");
                            } else {
                                logger_.info("New connection [fd:" + std::to_string(a) + "] " +
                                           "(address info unavailable)");
                            }
                        }
                    }
                    continue;
                }

                // Get the connection
                auto it = connections_.find(fd);
                if (it == connections_.end()) continue;

                core::Connection* connection = &it->second;

                // Also find the peer if it exists
                core::Connection* peer_connection = nullptr;
                if (connection->peer_socket_id_ != -1) {
                    // The peer should exist
                    auto pit = connections_.find(connection->peer_socket_id_);
                    if (pit != connections_.end()) {
                        peer_connection = &pit->second;
                    } else {
                        // Peer no longer exists; clear the link
                        connection->peer_socket_id_ = -1;
                    }
                }

                // ----------------------------------
                // 2. READABLE: read and stage response when ready
                // ----------------------------------
                if (mask & core::kEventReadable) {
                    if (connection->Read() != core::ConnectionResult::OK) {
                        // The read failed
                        if (connection->closed_) {
                            connections_.erase(it);
                        }
                        continue;
                    }

                    // Uncomment below to see user requests outputed to the console
                    //std::cout << core::utils::bytesToReadableString(connection->receive_buffer_) << std::endl;
                    //std::cout << core::utils::bytesToHex(connection->receive_buffer_) << std::endl;

                    if (connection->role_ == core::ConnectionRole::Client) {
                        if (!connection->protocol_) {
                            bool known = connection->SetProtocol();

                            if (!known || !connection->protocol_) {
                                logger_.debug("Unknown connection protocol; sending health response");
                                HealthResponse(*connection);
                                (void)core::UpdateEventInterest(
                                    poller_, 
                                    fd, 
                                    /*readable=*/true, 
                                    /*writable=*/true);
                                continue;
                            }
                        }

                        // If the connection has a protocol set, we can do work with it
                        if (connection->protocol_) {

                            // Peer connection can be null if no upstream connection yet
                            connection->protocol_->OnReadable(connection, peer_connection, poller_);

                            // Figure out which protocol it is so we know what to do
                            switch (connection->protocol_->type()) {
                                case core::protocol::ProtocolType::kSocks4: {
                                    auto* socks4 = dynamic_cast<core::protocol::Socks4*>(connection->protocol_.get());
                                    if (connection->peer_socket_id_ == -1 &&
                                        socks4->state() == core::protocol::Socks4::State::Established) {
                                            core::CreateUpstreamTCPConnection (
                                                poller_,
                                                logger_,
                                                connections_,
                                                *connection,
                                                socks4->destination_ip(),
                                                socks4->destination_port()
                                            );
                                        }
                                    break;
                                }
                                case core::protocol::ProtocolType::kSocks4a: {
                                    [[fallthrough]];
                                }
                                case core::protocol::ProtocolType::kSocks5: {
                                    [[fallthrough]];
                                }
                                case core::protocol::ProtocolType::kHttp: {
                                    [[fallthrough]];
                                }
                                default: {
                                    logger_.debug("Protocol unknown or unimplemented; sending health response");
                                    HealthResponse(*connection);
                                    connection->Write();
                                    (void)core::UpdateEventInterest(
                                        poller_, 
                                        fd, 
                                        /*readable=*/true, 
                                        /*writable=*/true);
                                    continue;
                                }
                            }
                        }
                    } else if (connection->role_ == core::ConnectionRole::Upstream) {
                        // The client's connection protocol can handle forwarding upstream
                        if (peer_connection && peer_connection->protocol_) {
                            peer_connection->protocol_->OnReadable(connection, peer_connection, poller_);
                        }
                    }

                    // Update this connection's event interests
                    (void)core::UpdateEventInterest(
                        poller_,
                        fd,
                        /*readable=*/true, // Replace with some rate limiting check
                        /*writable=*/connection->want_write_
                    );

                    // Update the peer's event interests, if we just changed them
                    if (peer_connection) {
                        (void)core::UpdateEventInterest(
                            poller_,
                            peer_connection->id_,
                            /*readable=*/true, // Replace with some rate limiting check
                            /*writable=*/peer_connection->want_write_
                        );
                    }
                }

                // ----------------------------------
                // 3. WRITABLE: drain send_buffer
                // ----------------------------------
                if (mask & core::kEventWritable) {

                    if (connection->protocol_) {
                        connection->protocol_->OnWritable(connection, peer_connection, poller_);
                    }

                    (void)connection->Write();

                    (void)core::UpdateEventInterest(
                        poller_,
                        fd,
                        /*readable=*/true, // Add some sort of rate limiting later
                        /*writable=*/connection->want_write_
                    );
                }
            }        
        }    
        CleanUpResources(); 
    }

    
    void ProxyServer::CleanUpResources() {
        // Remove all connections
        for (auto it = connections_.begin(); it != connections_.end();) {
            it = connections_.erase(it);
        }

        // Close poller
        if (poller_ > 0) {
            core::CloseSocket(poller_);
            poller_ = 0;
        }

        // Close listening socket
        if (socket_ > 0) {
            core::CloseSocket(socket_);
            socket_ = 0;
        }

        core::ShutDownNetwork();
    }

    void ProxyServer::HealthResponse(core::Connection& connection) {
        static const char kResponse[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 30\r\n"
            "\r\n"
            "Hello from your proxy server!\n";

        const std::size_t kLen = sizeof(kResponse) - 1;
        const std::size_t old_size = connection.send_buffer_.size();

        connection.send_buffer_.resize(old_size + kLen);
        std::memcpy(connection.send_buffer_.data() + old_size, kResponse, kLen);
        connection.want_write_ = true;
    }

} // namespace server

