cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 17)

message(STATUS "Compiling Unit Tests")
file(GLOB SRC_FILES ${CMAKE_SOURCE_DIR}/tcp*cpp)
add_executable(UnitTests tcp_protocol_test.cpp ${SRC_FILES})

target_link_libraries(UnitTests PRIVATE pthread gtest gtest_main)
