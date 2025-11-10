# Proxy Server

A simple TCP proxy server implemented in C++20 for Linux systems.  
This server demonstrates non-blocking network I/O using `epoll` and supports configurable options via command-line arguments.

---

## Features

- Non-blocking TCP server using `epoll`.
- Configurable:
  - Port (`--port`)
  - Maximum backlog (`--backlog`)
  - Maximum events per `epoll_wait` (`--max-events`)
  - Bind host (`--bind-host`)
  - Verbose logging (`--verbose`)
- Graceful client connection handling.
- Simple modular structure:
  - `core/` - Networking and configuration utilities.
  - `server/` - Proxy server implementation.
  - `app/` - Entry point (`main.cc`).

---

## Requirements

- Linux system
- C++20 compatible compiler (tested with `g++`)
- `make` build system

---

## Building

```bash
git clone <repository-url>
cd proxy_server
make clean && make

This will compile the server and place the binary in the ./bins directory:

./bins/proxy_server

Running
./bins/proxy_server [OPTIONS]

Available Options
Option	Description
--port N	TCP port to listen on (default: 8080)
--backlog N	Maximum number of pending connections (default: 128)
--max-events N	Maximum number of events returned by epoll (default: 16)
--bind-host IP	IP address/interface to bind (default: all interfaces)
--verbose	Enable verbose logging
--help	Show usage information
Example
./bins/proxy_server --port 9090 --verbose

Testing the Server

Start the server:

./bins/proxy_server --port 9090 --verbose


In another terminal, test with nc (netcat):

nc 127.0.0.1 9090


The server will print a message like:

[Connection] New client: 127.0.0.1:<port>


The client can be disconnected gracefully using Ctrl+D or by closing the netcat session.

Verify the listening port with:

ss -tuln | grep 9090

Project Structure
proxy_server/
├─ app/
│  └─ main.cc           # Entry point
├─ core/
│  ├─ config.h          # Config struct & parsing
│  ├─ config.cc         # Config implementation
│  ├─ network.h         # Network interface
│  └─ network_linux.cc  # Linux network implementation
├─ server/
│  ├─ proxy_server.h    # ProxyServer class
│  └─ proxy_server.cc   # ProxyServer implementation
├─ bins/                # Compiled binaries (output by make)
└─ Makefile             # Build instructions

Notes

SIGPIPE is ignored to prevent crashes when writing to disconnected clients.

Non-blocking sockets prevent the server from hanging on slow or inactive clients.

Currently, this proxy server accepts connections but does not forward traffic. This can be extended for full proxy functionality.

License

MIT License

Group Members

[Our Names]

