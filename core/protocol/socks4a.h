#ifndef PROXY_SERVER_CORE_PROTOCOL_SOCKS4A_H_
#define PROXY_SERVER_CORE_PROTOCOL_SOCKS4A_H_

#include "socks4.h"

#include <string>

namespace core::protocol {


    class Socks4a : public Socks4 {
    public:
        using State = Socks4::State;

        ProtocolType type() const noexcept override { return ProtocolType::kSocks4a; }

        void OnReadable(Connection* self, Connection* peer, EventPollerIdentifier poller) override;
        void OnWritable(Connection* self, Connection* peer, EventPollerIdentifier poller) override;

        bool has_domain() const noexcept { return has_domain_; }
        const std::string& destination_domain() const noexcept { return destination_domain_; }

        void SetResolvedIP(const std::byte ip[4]) noexcept;

    private:
        bool has_domain_{false};
        std::string destination_domain_;
    };

} // namespace core::protocol


#endif // PROXY_SERVER_CORE_PROTOCOL_SOCKS4A_H_