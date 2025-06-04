# CLIProxyServer

🚀 **CLIProxyServer** is a next-level, SOCKS5‑powered proxy server built in C for Linux (Windows byebye!). It’s the plug between your client and the target server, catching every TCP/UDP packet and decoding HTTP & WebSocket traffic in real time. With dope “freeze” and “stop” commands you can flex from the terminal, it keeps you fully in control. Powered by slick non‑blocking I/O (epoll), dynamic buffers, and a clean modular setup, this beast delivers scalable, low‑latency performance that’s ready for whatever traffic you throw at it.

## Table of Contents

* [Features](#features)
* [Prerequisites](#prerequisites)
* [Build & Install](#build--install)
* [Usage](#usage)
* [Options](#options)
* [Examples](#examples)
* [Contributing](#contributing)
* [License](#license)

---

## Features

* 🛡️ **SOCKS5 Handshake & Authentication**
  Implements full SOCKS5 protocol: greeting, optional USER/PASS (RFC1929), and CONNECT commands.
* **Dynamic Buffering**
  Uses dynamically expanding FIFO buffers for TCP/UDP (UDP no :) TODO ) payloads—no fixed‑size limits.
* 💬 **HTTP & WebSocket Parsing**
  Parses and logs HTTP headers and WebSocket text frames in real time. Unrecognized traffic is hex‑dumped.
* ⚡ **Non‑Blocking I/O (epoll)**
  Scales to many simultaneous connections with minimal overhead.
* ❄️ **Interactive Terminal Control**
  A separate thread listens for `freeze` (pause forwarding) and `stop` (graceful shutdown) commands.
* 🧩 **Modular Architecture**
  Clear separation of concerns: buffering, socket abstraction, protocol parsing, tunneling, and logging.

---

## Prerequisites

Ensure you have the following installed on your system:

* **CMake** (version >= 3.29)
* **GCC** (or Clang) with C11 support
* **pthread** library (for threading)
* **ncurses** development package (`libncurses-dev` on Debian/Ubuntu, `ncurses-devel` on Fedora) (TODO :) )
* **Make** (or Ninja)

On Debian/Ubuntu, you can install dependencies with:

```bash
sudo apt update
sudo apt install build-essential cmake libncurses-dev
```

On Arch Linux (pacman):

```bash
sudo pacman -Syu base-devel cmake ncurses
```

---

## Build & Install

1. **Clone the repository**:

   ```bash
   git clone https://github.com/<your-username>/CLIProxyServer.git
   cd CLIProxyServer
   ```

2. **Create a build directory and run CMake**:

   ```bash
   mkdir build
   cd build
   cmake ..
   ```

   CMake will locate required packages (Threads, Curses) and generate Makefiles.

3. **Compile**:

   ```bash
   make
   ```

   This produces the `CLIProxyServer` executable in `build/`.

4. *(Optional)* **Install to /usr/local/bin**:

   ```bash
   sudo make install
   ```

   By default, the CMakeLists.txt does not specify an `install` target; you may place `CLIProxyServer` manually or update CMakeLists to add:

   ```cmake
   install(TARGETS CLIProxyServer DESTINATION bin)
   ```

---

## Usage

```bash
./CLIProxyServer -a <bind_address> -p <bind_port> [options]
```

Once running, open a separate terminal window to issue control commands to the proxy:

* Type `freeze` and press Enter to pause all packet forwarding (logging still continues).
* Type `stop` and press Enter to gracefully shut down the proxy server.

### Command Syntax

* **`-a <bind_address>`**
  IPv4 or IPv6 address (or hostname) on which the proxy listens.
* **`-p <bind_port>`**
  TCP port on which the proxy listens.
* **`-u <username>`** *(optional)*
  Username for SOCKS5 USER/PASS authentication.
* **`-k <password>`** *(optional)*
  Password for SOCKS5 USER/PASS authentication.
* **`-o <logfile>`** *(optional)*
  Path to a log file. If omitted, logs are printed to stdout.

**Note**: If `-u` and `-k` are not supplied, the proxy uses “no authentication” mode.

---

## Options

| Option          | Description                                                 |
| --------------- | ----------------------------------------------------------- |
| `-a <address>`  | Listening IP address or hostname (required)                 |
| `-p <port>`     | Listening port number (required)                            |
| `-u <username>` | SOCKS5 username for USER/PASS auth (optional)               |
| `-k <password>` | SOCKS5 password for USER/PASS auth (optional)               |
| `-o <logfile>`  | File path for logging output (optional; defaults to stdout) |

---

## Examples

1. **Run without authentication, logging to console**:

   ```bash
   ./CLIProxyServer -a 0.0.0.0 -p 1080
   ```

   * Listens on all interfaces (IPv4) at port 1080.
   * Uses SOCKS5 “no auth” mode.
   * Logs appear in the terminal.

2. **Run with USER/PASS authentication and log to a file**:

   ```bash
   ./CLIProxyServer -a 127.0.0.1 -p 1080 -u admin -k secret -o /var/log/cli_proxy.log
   ```

   * Binds to `127.0.0.1:1080`.
   * Requires clients to authenticate with username `admin` and password `secret`.
   * Appends all logs to `/var/log/cli_proxy.log`.

3. **Pause forwarding traffic (freeze mode)**:

   * In the main proxy window run:

     ```bash
     freeze
     ```

   * All incoming/outgoing packets are logged but not forwarded until “freeze” is toggled off.

4. **Graceful shutdown**:

   * Type `stop` in the proxy’s stdin or send `SIGINT` (Ctrl+C) to terminate gracefully.
   * Ensures pending buffers are flushed before exit.

---

## Contributing

✨ Low‑key excited that you’re thinking of jumping in! Here’s how to get involved:

1. **Fork** the repository and create a new feature branch:

   ```bash
   git checkout -b feature/my-awesome-addition
   ```
2. Make your changes, ensuring you maintain code clarity and consistent style (C11, meaningful variable names, open braces, etc.).
3. **Test** your modifications thoroughly—epoll behavior, buffer growth, SOCKS5 handshake, and terminal commands.
4. **Submit a Pull Request**. We’ll review and merge if it aligns with our forward‑thinking design.

🔥 We’re always looking for rad improvements, so don’t hesitate to propose new features—maybe UDP ASSOCIATE support or advanced logging filters!

---

## License

CLIProxyServer is distributed under the MIT License. See [LICENSE](LICENSE) for details.
