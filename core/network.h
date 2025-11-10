#ifndef PROXY_SERVER_CORE_NETWORK_H_
#define PROXY_SERVER_CORE_NETWORK_H_

#include <string>

namespace core {

using SocketIdentifier = int;
using EventPollerIdentifier = int;

bool InitializeNetwork();
void ShutDownNetwork();

SocketIdentifier CreateListeningSocket(const std::string& bind_host, int port, int max_connection_count);
bool SetSocketNonBlocking(SocketIdentifier id);

EventPollerIdentifier CreateEventPoller();
bool RegisterReadEvent(EventPollerIdentifier poller, SocketIdentifier socket);
int WaitForEvents(EventPollerIdentifier poller, void* events, int max_events, int timeout_ms);
void CloseSocket(SocketIdentifier socket);

}  // namespace core

#endif  // PROXY_SERVER_CORE_NETWORK_H_

