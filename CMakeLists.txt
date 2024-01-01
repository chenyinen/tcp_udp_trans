cmake_minimum_required(VERSION 2.8)

set(CMAKE_BUILD_TYPE Debug)
project(network_trans)


add_executable(tcp_server ${CMAKE_CURRENT_SOURCE_DIR}/tcp_server.c ${CMAKE_CURRENT_SOURCE_DIR}/log.c)
add_executable(tcp_client ${CMAKE_CURRENT_SOURCE_DIR}/tcp_client.c ${CMAKE_CURRENT_SOURCE_DIR}/log.c)