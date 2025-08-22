#cmake_minimum_required(VERSION 3.15)
#project(network_binding)
#
#set(CMAKE_CXX_STANDARD 17)
#
#find_package(pybind11 CONFIG REQUIRED)
#include_directories(../)
#pybind11_add_module(network_module
#                    src/network.cpp
#                    ../tcp_protocol.cpp)
#
#install(TARGETS network_module DESTINATION .)