#include "proxy_server.h"

#include "protocol/detector.h"
#include "protocol/protocol.h"
#include "protocol/socks5.h"
#include "protocol/socks4.h"
#include "protocol/socks4a.h"
#include "utils/string_utils.h"

#include <cstring>
#include <iostream> // Remove after Logger exists
#include <chrono>
#include <iomanip>
#include <sstream>

namespace server {

    void ProxyServer::Run() {
        
        (void)core::InitializeNetwork();

        socket_ = core::CreateListeningSocket("", port_, backlog_);

        if (socket_ < 0) {
            // TODO: Log failed to create a listening socket on port_
            return; // We can't do anything without a listening socket
        }

        if (!core::SetSocketNonBlocking(socket_)) {
            // TODO: Log failed to set socket non-blocking
            CleanUpResources();
            return;
        } 

        poller_ = core::CreateEventPoller();
        if (poller_ < 0) {
            // TODO: Log failed to create an epoll instance
            CleanUpResources();
            return;
        }

        if (!core::RegisterReadEvent(poller_, socket_)) {
            // TODO: Log failed to register socket with epoll
            CleanUpResources();
            return;
        }

        core::PollEvent events[static_cast<std::size_t>(max_events_)];

        bool running = true;
        while (running) {
            int n = core::WaitForEvents(poller_, events, max_events_, -1); 
            if (n < 0) {
                // TODO: Log epoll_wait error
                // This is super rare but if it happens we may want to re-create epoll
                // For now just exit
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
                        // TODO: Log fatal listening socket error/hangup
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
                    (void)AcceptNewClientConnection(socket_, poller_, connections_, &accepted);

                    // TODO: Remove the accepted socket vector once the logger is in place
                    // Do not change the AcceptNewConnections signature nor the functionality
                    // of AcceptNewConnections other than adding in the Logger
                    // Remove the printout here but keep the vector
                    // We can revisit this when the Logger is in place

                    // Remove later
                    for (auto a : accepted) {
                        std::string ip;
                        uint16_t port = 0;

                        auto now = std::chrono::system_clock::now();
                        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
                        std::tm tm_buf{};
                    #ifdef _WIN32
                        localtime_s(&tm_buf, &now_c);
                    #else
                        localtime_r(&now_c, &tm_buf);
                    #endif

                        std::ostringstream timestamp;
                        timestamp << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

                        if (core::SocketToAddress(a, ip, port)) {
                            std::cout << "[" << timestamp.str() << "] [+] New connection " << a
                                    << " from " << ip << ":" << port << std::endl;
                        } else {
                            std::cout << "[" << timestamp.str() << "] [+] New connection " << a
                                    << " (address unavailable)" << std::endl;
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


                // ----------------------------
                // 2. READABLE: read and stage response when ready
                // ----------------------------
                if (mask & core::kEventReadable) {
                    if (connection->Read() != core::ConnectionResult::OK) {
                        // The read failed
                        if (connection->closed_) {
                            connections_.erase(it);
                        }
                        continue;
                    }

                    // Uncomment below to see user requests outputed to the console
                    // std::cout << core::utils::bytesToReadableString(connection->receive_buffer_) << std::endl;
                    // std::cout << core::utils::bytesToHex(connection->receive_buffer_) << std::endl;

                    if (connection->role_ == core::ConnectionRole::Client) {

                        if (!connection->protocol_) {
                            (void)connection->SetProtocol();
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
                                                connections_,
                                                *connection,
                                                socks4->destination_ip(),
                                                socks4->destination_port()
                                            );
                                        }
                                        break;
                                    } 
                                    case core::protocol::ProtocolType::kSocks4a: {
                                        auto* socks4a = dynamic_cast<core::protocol::Socks4a*>(connection->protocol_.get());

                                        const std::string& host = socks4a->destination_domain();
                                        (void)host;
                                        //TODO: Resolve host to ip once dns implemented
                                        // 
                                        // Something like
                                        // auto ip_opt = core::ResolveHostnameToIPv4(host);
                                        // if (!ip_opt) {
                                        //     // Resolution failed: close or mark failed (your call)
                                        //     connection->closed_ = true;
                                        //     break;
                                        // }
                                        // socks4a->SetResolvedIP(ip_opt->data());
                                        // if (connection->peer_socket_id_ == -1 && 
                                        //     socks4a->state() == core::protocol::Socks4::State::Established) {
                                        //     core::CreateUpstreamTCPConnection (
                                        //         poller_,
                                        //         connections_,
                                        //         *connection,
                                        //         socks4a->destination_ip(),
                                        //         socks4a->destination_port()
                                        //     );
                                        // }
                                        break;
                                    }
                                    case core::protocol::ProtocolType::kSocks5: {
                                        break;
                                    }
                                    case core::protocol::ProtocolType::kHttp: {
                                        break;
                                    }
                                    default: {
                                        break;
                                    }
                                }             
                            } else {
                                std::cout << "No matching protocol found" << std::endl;
                                std::cout << core::utils::bytesToReadableString(connection->receive_buffer_) << std::endl;
                                HealthResponse(*connection);
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

                // ----------------------------
                // 3. WRITABLE: drain send_buffer
                // ----------------------------
                if (mask & core::kEventWritable) {

      
                    if (connection->protocol_) {
                         connection->protocol_->OnWritable(connection, peer_connection, poller_);
                    }

                    // Attempt to drain as much as possible durring this event from the send_buffer
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

        // Close all connections
        for (auto it = connections_.begin(); it != connections_.end(); ) {
            it = connections_.erase(it); //Returns the next it
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


} //namespace server