#ifndef PROXY_SERVER_SERVER_PROXY_SERVER_H_
#define PROXY_SERVER_SERVER_PROXY_SERVER_H_

#include "network.h"

namespace server {

class ProxyServer final {
public:
    void Run();

private:
    core::SocketIdentifier socket_{};
    core::EventPollerIdentifier poller_{};

    void CleanUpResources();
};

}  // namespace server

#endif  // PROXY_SERVER_SERVER_PROXY_SERVER_H_

