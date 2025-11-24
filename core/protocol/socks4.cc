#include "socks4.h"
#include "connection.h"

#include <cstring>
#include <iostream>
#include <iterator>

namespace core::protocol {


    void Socks4::OnReadable(Connection* self, Connection* peer, EventPollerIdentifier poller) {
        auto& buf = self->receive_buffer_;

        switch (state_) {
            case State::Init: {

                // Minimum SOCKS4 request:
                // VN(1) CD(1) DSTPORT(2) DSTIP(4) USERID(>=1, at least one 0x00 byte)
                if (buf.size() < 9) {
                    return; // Not enough data
                }

                const uint8_t version = std::to_integer<uint8_t>(buf[0]);
                const uint8_t command = std::to_integer<uint8_t>(buf[1]);

                destination_port_ = std::to_integer<uint16_t>(buf[2]) << 8 | std::to_integer<uint16_t>(buf[3]);

                for(int i = 0; i < 4; i++) {
                    destination_ip_[i] = buf[4 + i];
                }

                // Find the last char of the User ID
                std::size_t idx = 8;
                while (idx < buf.size() && buf[idx] != std::byte{0}) {
                    idx++;
                }

                if (idx == buf.size()) {
                    return; // No NULL terminator so message may be incomplete
                }

                user_id_.assign(
                    reinterpret_cast<const char*>(buf.data() + 8),
                    reinterpret_cast<const char*>(buf.data() + idx)
                );

                // Consume the whole request, including the trailing NULL
                buf.erase(buf.begin(), buf.begin() + idx + 1);

                constexpr uint8_t kSocksVersion = 0x04;
                constexpr uint8_t kCommandConnect = 0x01;
                constexpr uint8_t kRequestGranted = 0x5A;
                constexpr uint8_t kRequestFailed = 0x5B;

                // SOCKS4 reply:
                // VN = 0x00
                // CD = 0x5A (granted) or 0x5B..0x5d (errors)
                uint8_t status = kRequestGranted;

                if (version != kSocksVersion || command != kCommandConnect) {
                    status = kRequestFailed;
                }

                // We can potentially check for specific UserIDs here later

                std::byte reply[8];
                reply[0] = std::byte{0x00};
                reply[1] = std::byte{status};
                reply[2] = std::byte{static_cast<uint8_t>(destination_port_ >> 8)};
                reply[3] = std::byte{static_cast<uint8_t>(destination_port_ & 0xFF)};
                for (int i = 0; i < 4; i++) {
                    reply[4 + i] = destination_ip_[i];
                }

                auto& send = self->send_buffer_;
                send.insert(send.end(), std::begin(reply), std::end(reply));
                self->want_write_ = true;

                (void)core::UpdateEventInterest(
                    poller,
                    self->id_,
                    /*readable=*/true,
                    /*writable=*/true
                );

                if (status == kRequestGranted) {
                    state_ = State::Established;
                } else {
                    state_ = State::Failed;
                    self->closed_ = true;
                }
                break;
            }

            case State::Established: {
                // Data tunneling
                // Whichever side ("self") becomes readable, its bytes are moved
                // into the peer's send buffer
                if (peer == nullptr || self == peer) {
                    return; // Peer not set yet
                }

                if (!buf.empty()) {
                    peer->send_buffer_.insert(peer->send_buffer_.end(), buf.begin(), buf.end());
                    buf.clear();

                    peer->want_write_ = true;

                    (void)core::UpdateEventInterest(
                        poller,
                        peer->id_,
                        /*readable=*/true,
                        /*writable=*/true
                    );
                }
                break;
            }
            case State::Failed: {
                // Nothing to do, server will close connection
                break;
            }
        }
    }

    void Socks4::OnWritable(Connection* self, Connection* peer, EventPollerIdentifier poller) {
        (void)self;
        (void)peer;
        (void)poller;

        // We rely on Connection::Write() plus want_write_ / UpdateEventInterest
        // to handle draining send buffers. No protocol-specific behavior needed.
    }

} // namespace core::protocol