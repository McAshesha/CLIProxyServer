cmake_minimum_required(VERSION 3.29)
project(CLIProxyServer C)

set(CMAKE_C_STANDARD 11)

add_executable(CLIProxyServer src/main.c
        src/socks5.c
        src/include/socks5.h)
