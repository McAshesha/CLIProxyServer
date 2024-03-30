# CLIProxyServer

A SOCKS5 proxy server written in C for Linux. Forwards TCP traffic between a
client and the target host with optional USER/PASS authentication and live
HTTP / WebSocket logging.

## Features

* **SOCKS5 Handshake & Authentication** — full SOCKS5 protocol: greeting,
  optional USER/PASS (RFC1929), and CONNECT commands.
* **Dynamic Buffering** — FIFO byte buffers that grow geometrically.
* **HTTP & WebSocket Parsing** — logs request lines, headers and WebSocket
  text frames as the traffic flows through. Unknown payloads are hex-dumped.
* **Non-Blocking I/O (epoll)** — scales to many simultaneous connections.
* **Interactive Terminal Control** — a separate thread listens for `freeze`
  (pause forwarding) and `stop` (graceful shutdown).
* **Modular Architecture** — clear separation between buffering, socket
  abstraction, protocol parsing, tunneling and logging.

## Prerequisites

* CMake >= 3.29
* GCC or Clang with C11 support
* pthread
* ncurses development headers (`libncurses-dev` / `ncurses-devel`)

```bash
sudo apt install build-essential cmake libncurses-dev
```

## Build

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

```bash
./CLIProxyServer -a <bind_address> -p <bind_port> [options]
```

### Options

| Option          | Description                                                 |
| --------------- | ----------------------------------------------------------- |
| `-a <address>`  | Listening IP address or hostname (required)                 |
| `-p <port>`     | Listening port number (required)                            |
| `-u <username>` | SOCKS5 username for USER/PASS auth (optional)               |
| `-k <password>` | SOCKS5 password for USER/PASS auth (optional)               |
| `-o <logfile>`  | File path for logging output (optional; defaults to stdout) |

Type `freeze` in the proxy terminal to pause forwarding, `stop` to shut down.
