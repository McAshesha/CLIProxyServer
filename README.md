# CLIProxyServer

A SOCKS5 proxy server written in C for Linux. Forwards TCP traffic between a
client and the target host with optional USER/PASS authentication and live
HTTP / WebSocket logging.

## Build

```bash
mkdir build && cd build
cmake ..
make
```

This produces the `CLIProxyServer` executable in `build/`.

## Usage

```bash
./CLIProxyServer -a <bind_address> -p <bind_port>
```

Type `freeze` in the terminal to pause forwarding, `stop` to shut down.
