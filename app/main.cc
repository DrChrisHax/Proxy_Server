#include "proxy_server.h"
#include "config.h"

int main(int argc, char* argv[]) {
    // Parse args into Config
    core::Config cfg = core::Config::FromArgs(argc, argv);

    // Store globally so other modules can read it 
    core::InitializeConfig(cfg);

    server::ProxyServer proxy;
    proxy.Run();

    return 0;
} // main.cc

