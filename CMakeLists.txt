cmake_minimum_required(VERSION 3.29)
project(CLIProxyServer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include directories
include_directories(
        ${PROJECT_SOURCE_DIR}/src/include
)

# Source files
set(SOURCES
        src/main.c
        src/buffer.c
        src/logger.c
        src/server.c
        src/tunnel.c
        src/sock.c
        src/protocol.c
        src/protocol_parser.c
        src/terminal.c
)


# Define the executable
add_executable(CLIProxyServer ${SOURCES})

# Link pthread for threading support
find_package(Threads REQUIRED)
target_link_libraries(CLIProxyServer PRIVATE Threads::Threads)

find_package(Curses REQUIRED)
target_link_libraries(CLIProxyServer PRIVATE ${CURSES_LIBRARIES})
target_include_directories(CLIProxyServer PRIVATE ${CURSES_INCLUDE_DIR})
