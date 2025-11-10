#include "network.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

namespace core {

    // ---------------------
    // Linux Network Wrapper
    // ---------------------

    bool InitializeNetwork() {
        // No setup required for Linux
        return true;
    }

    void ShutDownNetwork() {
        // No cleanup required for Linux
    }

    SocketIdentifier CreateListeningSocket(const std::string& bind_host, int port, int max_connection_count) {
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

        addrinfo* result = nullptr;
        const std::string port_str = std::to_string(port);

        int status = getaddrinfo(bind_host.empty() ? nullptr : bind_host.c_str(),
                                 port_str.c_str(), &hints, &result);
        
        if (status) {
            // TODO: Log getaddrinfo error
            // std::cerr << gai_strerror(status);
            // Reason: invalid host, missing DNS, or system resolver issues
            return -1;
        }

        SocketIdentifier listening_socket_id = -1;
        for (addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
            SocketIdentifier socket_id = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (socket_id < 0) {
                // TODO: Log socket() failure
                // Reason: resource limits, permission issues, or unsupported family type on this kernel
                continue;
            }

            int optval = 1;
            ::setsockopt(socket_id, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

            if (ai->ai_family == AF_INET6) {
                int v6only = 0;
                if (::setsockopt(socket_id, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) < 0) {
                    // TODO: Log IPV6_V6ONLY setsockopt failure 
                    // Reason: system policy may enforce v6-only; non-fatal, continue with current socket_id
                }
            }

            if (::bind(socket_id, ai->ai_addr, ai->ai_addrlen) < 0) {
                // TODO: Log bind() failure
                // getnameinfo()
                // Reason: port in use, privileged port, or invalid interface binding
                ::close(socket_id);
                continue; // Try next address info candidate
            }

            if (::listen(socket_id, max_connection_count) < 0) {
                // TODO: Log listen() failure 
                // Include max_connection_count value
                // Reason: resource limits or transient kernel state
                ::close(socket_id);
                continue; // Try next address info candidate
            }

            // Success on the first candidates that binds & listens
            listening_socket_id = socket_id;
            break;
        }

        if (listening_socket_id < 0) {
            // TODO: Log failure to create any listening socket from getaddrinfo results
            // Reason: Every candidate failed; Probably a port issue or IPv4/IPv6 is not enabled
        }

        ::freeaddrinfo(result);
        return listening_socket_id;
    }

    bool SetSocketNonBlocking(SocketIdentifier socket) {
        int flags = ::fcntl(socket, F_GETFL, 0);
        if (flags == -1) {
            // TODO: Log fcntl(GET_FL) error
            // Reason: invalid file descriptor (Socket ID) or kernel issue
            // Coninuing may cause blocking in the loop which will tank performance
            return false;
        }
        if (::fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1) {
            // TODO: Log fcntl(F_SETFL) error
            // Reason: cannot enable non-blocking I/O
            return false;
        }
        return true;
    }

    EventPollerIdentifier CreateEventPoller() {
        EventPollerIdentifier epID = ::epoll_create1(0);
        if (epID < 0) {
            // TODO: Log epoll_create1 failure with errno
            // Reason: File Descriptor limits, kernel issues
        }
        return epID;
    }

    bool RegisterReadEvent(EventPollerIdentifier poller, SocketIdentifier socket) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = socket;
        if (::epoll_ctl(poller, EPOLL_CTL_ADD, socket, &ev) < 0) {
            // TODO: Log epoll_ctl(ADD) failure with errno, poller handle, and socket fd
            // Reason: invalid descriptors, already registered, or resource constraints
            return false;
        }
        return true;
    }

    int WaitForEvents(EventPollerIdentifier poller, void* events, int max_events, int timeout_ms) {
        int n = ::epoll_wait(poller, static_cast<epoll_event*>(events), max_events, timeout_ms);
        if (n < 0 && errno != EINTR) {
            // From the epoll man page
            // EINTR --> The call was interrupted by a signal handler before either
            // any of the requested events occurred or the timeout expired
            // therefore we can continue on if we get this error
            // TODO: Log epoll_wait failure with errno
            // Reason: poller invalidation or system call error
            return -1;
        }
        return n;
    }

    void CloseSocket(SocketIdentifier socket) {
        if (socket >= 0) {
            if (::close(socket) < 0) {
                // TODO: Log close() failure with errno and socket fd
                // Reason: Typically a double free but otherwise a rare error
            }
        }
    }

} // namespace core
