#include "connection.h"
#include "network_constants.h"

#include "protocol/detector.h"
#include "protocol/protocol.h"
#include "protocol/socks5.h"
#include "protocol/socks4.h"
#include "protocol/socks4a.h"

#include <cerrno>
#include <cstring>

namespace core {

    // Constructors and Destructor

    Connection::Connection() noexcept :
        id_{-1},
        role_{},
        peer_socket_id_{-1},
        receive_buffer_{},
        send_buffer_{},
        want_write_{false},
        closed_{false},
        protocol_{nullptr} 
        {}
    
    Connection::Connection(core::SocketIdentifier id, ConnectionRole role) noexcept :
        id_{id},
        role_{role},
        peer_socket_id_{-1},
        receive_buffer_{},
        send_buffer_{},
        want_write_{false},
        closed_{false},
        protocol_{nullptr} 
        {}

    Connection::~Connection() {
        Close();
    }

    Connection::Connection(Connection&& other) noexcept :
        id_{other.id_},
        role_{other.role_},
        peer_socket_id_{other.peer_socket_id_},
        receive_buffer_{std::move(other.receive_buffer_)},
        send_buffer_{std::move(other.send_buffer_)},
        want_write_{other.want_write_},
        closed_{other.closed_},
        protocol_{std::move(other.protocol_)}
    {
        // Invalidate the moved-from object so its destructor is harmless
        other.id_ = -1;
        other.peer_socket_id_ = -1;
        other.want_write_ = false;
        other.closed_ = true;     // treat as closed; no FD to close
    }

    Connection& Connection::operator=(Connection&& other) noexcept {
        if (this != &other) {
            // Release current resources
            Close();

            // Move-transfer trivial & owning members
            id_ = other.id_;
            role_ = other.role_;
            peer_socket_id_ = other.peer_socket_id_;
            receive_buffer_ = std::move(other.receive_buffer_);
            send_buffer_ = std::move(other.send_buffer_);
            want_write_ = other.want_write_;
            closed_ = other.closed_;
            protocol_ = std::move(other.protocol_);

            // Invalidate the moved-from object
            other.id_ = -1;
            other.peer_socket_id_ = -1;
            other.want_write_ = false;
            other.closed_ = true;
        }
        return *this;
    }


    // Class API

    ConnectionResult Connection::Read() {
        std::byte buffer[core::k_16KB];

        while (true) {
            std::ptrdiff_t n = core::Receive(id_, buffer, sizeof(buffer));
            if (n > 0) {
                const std::size_t old = receive_buffer_.size();
                receive_buffer_.resize(old + static_cast<std::size_t>(n));
                std::memcpy(receive_buffer_.data() + old, buffer, static_cast<std::size_t>(n));
                continue; // drain until we would block
            }

            if (n == 0) {
                // TODO: Log peer closed connection gracefully
                closed_ = true;
                break;
            }

            // n < 0
            // Receive will already log error
            break;
        }

        if (closed_) {
            // TODO: Log client closed gracefully
            Close();
            return ConnectionResult::PeerClosed;
        }

        return ConnectionResult::OK;
    }

    ConnectionResult Connection::Write() {
        // Drain as many bytes as the kernel will accept
        // We are currently using a single contiguous vector
        // However we may want to try something different like a ring buffer in the future
        while (!send_buffer_.empty()) {
            const void* data = send_buffer_.data();
            const std::size_t data_length = send_buffer_.size();

            std::ptrdiff_t sent = core::Send(id_, data, data_length);
            if (sent > 0) {
                send_buffer_.erase(send_buffer_.begin(),
                                   send_buffer_.begin() + static_cast<std::size_t>(sent));
                continue; // Try sending more during this event
            }
            // sent <= 0: would-block or other error, can stop trying to send
            break;
        }

        want_write_ = !send_buffer_.empty();

        return ConnectionResult::OK;
    }

    void Connection::Close() {
        if (id_ >= 0) {
            core::CloseSocket(id_);
            id_ = -1;
        }
        closed_ = true;
    }

    bool Connection::SetProtocol() {
        if (protocol_) return true; // Already set

        core::protocol::Detector detector;
        core::protocol::ProtocolType type{};
        if (!detector.ProbeProtocol(receive_buffer_, &type)) {
            return false; // not enough bytes yet
        }

        switch (type) {
            case core::protocol::ProtocolType::kHttp:
                // TODO: protocol_ = std::make_unique<Http>();
                return false; // not implemented yet
            case core::protocol::ProtocolType::kSocks5:
                // protocol_ = std::make_unique<core::protocol::Socks5>();
                return false; 
            case core::protocol::ProtocolType::kSocks4:
                protocol_ = std::make_unique<core::protocol::Socks4>();
                return true;
            case core::protocol::ProtocolType::kSocks4a:
                protocol_ = std::make_unique<core::protocol::Socks4a>();
                return true;
            case core::protocol::ProtocolType::kUnsupported:
            case core::protocol::ProtocolType::kUnknown:
            default:
                return false;
        }
    }

    ConnectionResult AcceptNewClientConnection(core::SocketIdentifier socket, core::EventPollerIdentifier poller, 
                                               ConnectionMap& connections,
                                               std::vector<core::SocketIdentifier>* accepted_out) {
        
        while (true) {
            core::SocketIdentifier client_socket = core::AcceptConnection(socket);

            if (client_socket < 0) {
                // TODO: Log info - no more queued connections, or accept() failed
                return ConnectionResult::OK; 
            }

            if (!core::SetSocketNonBlocking(client_socket)) {
                // TODO: Log that client socket could not be set non-blocking
                // Continuing could block the event loop so we have to close
                core::CloseSocket(client_socket);
                continue; // We do not need to fail the whole loop
            }

            if (!core::RegisterReadEvent(poller, client_socket)) {
                // epoll registration failed
                core::CloseSocket(client_socket);
                return ConnectionResult::EpollRegisterFailed;
            }

            Connection c;
            c.id_ = client_socket;
            c.role_ = ConnectionRole::Client;
            c.receive_buffer_.reserve(core::k_8KB);
            connections.emplace(client_socket, std::move(c));

            if (accepted_out != nullptr) accepted_out->push_back(client_socket);

            // TODO: Log that the client is accepted and registered with epoll
        }
    }

    ConnectionResult CreateUpstreamTCPConnection(core::EventPollerIdentifier poller,
                                                    ConnectionMap& connections,
                                                    Connection& client,
                                                    const std::byte dest_ip[4],
                                                    uint16_t dest_port) {
        if (client.peer_socket_id_ != -1) {
            return ConnectionResult::OK; // Already paired
        }
        
        uint32_t ip =
            (static_cast<uint32_t>(std::to_integer<uint8_t>(dest_ip[0])) << 24) |
            (static_cast<uint32_t>(std::to_integer<uint8_t>(dest_ip[1])) << 16) |
            (static_cast<uint32_t>(std::to_integer<uint8_t>(dest_ip[2])) << 8)  |
             static_cast<uint32_t>(std::to_integer<uint8_t>(dest_ip[3]));

        core::SocketIdentifier upstream_socket = core::ConnectTCPIPv4(ip, dest_port);

        if (upstream_socket < 0) {
            return ConnectionResult::UnknownError;
        }

        if (!core::SetSocketNonBlocking(upstream_socket)) {
            core::CloseSocket(upstream_socket);
            return ConnectionResult::UnknownError;
        }

        if (!core::RegisterReadEvent(poller, upstream_socket)) {
            core::CloseSocket(upstream_socket);
            return ConnectionResult::EpollRegisterFailed;
        }

        if (!core::UpdateEventInterest(
                poller,
                upstream_socket,
                /*readable=*/true,
                /*writable=*/true)) {
            core::CloseSocket(upstream_socket);
            return ConnectionResult::EpollRegisterFailed;
        }

        Connection upstream;
        upstream.id_ = upstream_socket;
        upstream.role_ = ConnectionRole::Upstream;
        upstream.peer_socket_id_ = client.id_;
        upstream.receive_buffer_.reserve(core::k_8KB);

        connections.emplace(upstream_socket, std::move(upstream));

        // Link client to its upstream socket
        client.peer_socket_id_ = upstream_socket;

        return ConnectionResult::OK;
    }

} // namespace core