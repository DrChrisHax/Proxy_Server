#ifndef PROXY_SERVER_CORE_CONFIG_H_
#define PROXY_SERVER_CORE_CONFIG_H_

#include <string>
#include <optional>

namespace core {

struct Config {
    int port = 8080;
    int backlog = 128;
    int max_events = 16;
    bool verbose = false;
    std::string bind_host;

    // Helper: load from argc/argv
    static Config FromArgs(int argc, char* argv[]);
};

// Provides global access (read-only) to program configuration
const Config& GetConfig();
void InitializeConfig(const Config& cfg);

}  // namespace core

#endif  // PROXY_SERVER_CORE_CONFIG_H_

