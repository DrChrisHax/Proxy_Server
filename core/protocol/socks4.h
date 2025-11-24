#ifndef PROXY_SERVER_CORE_PROTOCOL_SOCKS4_H_
#define PROXY_SERVER_CORE_PROTOCOL_SOCKS4_H_

#include "protocol.h"

#include <cstdint>
#include <cstddef>
#include <string>

namespace core::protocol {


    class Socks4 : public Protocol {
    public:

        enum class Command : uint8_t {
            StreamConnection = 0x01,
            PortBinding = 0x02
        };

        enum class State : uint8_t {
            Init = 0,
            Established,
            Failed
        };

        ProtocolType type() const noexcept override { return ProtocolType::kSocks4; }

        void OnReadable(Connection* self, Connection* peer, EventPollerIdentifier poller) override;
        void OnWritable(Connection* self, Connection* peer, EventPollerIdentifier poller) override;

        State state() const noexcept { return state_; }
        uint16_t destination_port() const noexcept { return destination_port_; }
        const std::byte* destination_ip() const noexcept { return destination_ip_; }

    protected:
        State state_{State::Init};

        uint16_t destination_port_;
        std::byte destination_ip_[4];
        std::string user_id_;
    };

} // namespace core::protocol

#endif // PROXY_SERVER_CORE_PROTOCOL_SOCKS4_H_