#include "socks4a.h"
#include "connection.h"

#include <cstring>
#include <iostream>
#include <iterator>

namespace core::protocol {

    void Socks4a::OnReadable(Connection* self, Connection* peer, EventPollerIdentifier poller) {

        // For non-init states, just use SOCKS4 behavior
        if (state() != State::Init) {
            Socks4::OnReadable(self, peer, poller);
            return;
        }

        auto& buf = self->receive_buffer_;

        // Minimum SOCKS4/4a request:
        // VN(1) CD(1) DSTPORT(2) DSTIP(4) USERID(>=1, at least one 0x00 byte)
        if (buf.size() < 9) {
            return; // Not enough data yet
        }

        const uint8_t version = std::to_integer<uint8_t>(buf[0]);
        const uint8_t command = std::to_integer<uint8_t>(buf[1]);

        destination_port_ = (std::to_integer<uint16_t>(buf[2]) << 8) | std::to_integer<uint16_t>(buf[3]);

        for (int i = 0; i < 4; ++i) {
            destination_ip_[i] = buf[4 + i];
        }

        std::size_t idx = 8;
        while (idx < buf.size() && buf[idx] != std::byte{0}) {
            ++idx;
        }

        if (idx == buf.size()) {
                    return; // No NULL terminator so message may be incomplete
        }

        user_id_.assign(
                    reinterpret_cast<const char*>(buf.data() + 8),
                    reinterpret_cast<const char*>(buf.data() + idx)
        );

        std::size_t domain_start = idx + 1;
        if (domain_start >= buf.size()) {
            return; // Domain not present yet
        }

        std::size_t d = domain_start;
        while (d < buf.size() && buf[d] != std::byte{0}) {
            ++d;
        }

        if (d == buf.size()) {
            return; // Domain string incomplete
        }

        destination_domain_.assign(
            reinterpret_cast<const char*>(buf.data() + domain_start),
            reinterpret_cast<const char*>(buf.data() + d)
        );
        has_domain_ = true;

        // Consume the entire request including domain null terminator
        buf.erase(buf.begin(), buf.begin() + d + 1);

        constexpr uint8_t kSocksVersion = 0x04;
        constexpr uint8_t kCommandConnect = 0x01;
        constexpr uint8_t kRequestGranted = 0x5A;
        constexpr uint8_t kRequestFailed = 0x5B;

        // SOCKS4/4a reply:
        // VN = 0x00
        // CD = 0x5A (granted) or 0x5B..0x5D (errors)
        uint8_t status = kRequestGranted;
        if (version != kSocksVersion || command != kCommandConnect) {
            status = kRequestFailed;
        }

        std::byte reply[8];
        reply[0] = std::byte{0x00};
        reply[1] = std::byte{status};
        reply[2] = std::byte{static_cast<uint8_t>(destination_port_ >> 8)};
        reply[3] = std::byte{static_cast<uint8_t>(destination_port_ & 0xFF)};
        for (int i = 0; i < 4; ++i) {
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

        // NOTE:
        // If has_domain_ is true, this indicates that we need to resolve
        // the destination_domain_ to an IPv4 address later in the server
        // event loop before creating the upstream TCP connection.
        //
        // After resolution, call SetResolvedIP() and then create the
        // upstream tCP connection using destination_ip_ and destination_port_.
    }

    void Socks4a::OnWritable(Connection* self, Connection* peer, EventPollerIdentifier poller) {
        Socks4::OnWritable(self, peer, poller);
    }

    void Socks4a::SetResolvedIP(const std::byte ip[4]) noexcept {
        for (int i = 0; i < 4; ++i) {
            destination_ip_[i] = ip[i];
        }
    }

} // namespace core::protocol