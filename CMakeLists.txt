cmake_minimum_required(VERSION 3.29)
project(CLIProxyServer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR}/src/include)

add_executable(CLIProxyServer src/main.c)
