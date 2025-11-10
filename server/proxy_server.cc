#include "proxy_server.h"
#include "network.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h> // for ignoring SIGPIPE

#include <arpa/inet.h>
#include <netdb.h>

#include <iostream> // Potentially remove later
#include "config.h"
#include <atomic>

namespace {
    std::atomic<bool> g_running{true};  
    void HandleSignal(int) {
        g_running = false;
    }
}

namespace server {

    void ProxyServer::Run() {
       signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE to avoid crashes on closed connections
        signal(SIGINT, HandleSignal); // graceful ctrl + c
        signal(SIGTERM, HandleSignal); // system stop
        (void)core::InitializeNetwork();
        // modified
        const auto& cfg = core::GetConfig();
socket_ = core::CreateListeningSocket(cfg.bind_host, cfg.port, cfg.backlog);


        if (socket_ < 0) {
            // TODO: Log failed to create a listening socket on port_
            return; // We can't do anything without a listening socket
        }

        (void)core::SetSocketNonBlocking(socket_); // This can fail but we will handle the logging and consequences later

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
        // modified
        epoll_event events[cfg.max_events];

        while (/*true*/g_running) {
            int n = core::WaitForEvents(poller_, events, cfg.max_events, -1); 
            if (n < 0) {
                continue; // skip instead of breaking if EINTR
            } else if (n == 0) {
                // Not expected with timout = -1
                // Keep for future use but we will probably
                // never need this branch
            }

            for (int i = 0; i < n; ++i) {
                if (events[i].data.fd == socket_) {
                    // TODO: Accept connection and handle data

                    /*------*/
                    //TODO: Remove all this
                    sockaddr_storage client_address{};
                    socklen_t address_length = sizeof(client_address);

                    core::SocketIdentifier client_fd = 
                        ::accept(socket_, reinterpret_cast<sockaddr*>(&client_address), &address_length);

                    if (client_fd < 0) {
                        std::cerr << "[Warn] accept() failed, errno=" << errno << std::endl;
                        continue;
                    }

                    char host[NI_MAXHOST], service[NI_MAXSERV];
                    if (getnameinfo(reinterpret_cast<sockaddr*>(&client_address), address_length,
                                    host, sizeof(host), service, sizeof(service),
                                    NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
                        std::cout << "[Connection] New client: " << host
                                  << ":" << service << std::endl;
                    } else {
                        std::cout << "[Connection] New client (unresolved)" << std::endl;
                    }

                    // graceful disconnect
                    ::shutdown(client_fd, SHUT_WR); // send EOF to client

                    ::close(client_fd);
                    std::cout << "[Connection] Closed client connection\n";
                    /*------*/

                }
            }

        }

        CleanUpResources();
    }


    void ProxyServer::CleanUpResources() {
        if (poller_ > 0) {
            // Modification: since poller_ is an epoll descriptor, not a socet, it should be used directly
            ::close(poller_);
            poller_ = 0;
        }

        if (socket_ > 0) {
            core::CloseSocket(socket_);
            socket_ = 0;
        }

        core::ShutDownNetwork();
        std::cout << "[Shutdown] Cleaning up resources..." << std::endl;
    }


} //namespace server
