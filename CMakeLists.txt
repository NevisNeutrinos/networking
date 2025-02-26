cmake_minimum_required(VERSION 3.15)
project(GramsNetworkConnection)

set(CMAKE_CXX_STANDARD 17)

# The networking is built on top of the ASIO header library
# note it does not require BOOST
find_path(ASIO_INCLUDE_DIR asio.hpp
        HINTS /usr/include /usr/local/include $ENV{HOME}/asio*/include)

if (ASIO_INCLUDE_DIR)
    message(STATUS "Found ASIO: ${ASIO_INCLUDE_DIR}")
    include_directories(${ASIO_INCLUDE_DIR})
else()
    message(FATAL_ERROR "ASIO not found")
endif()

# Standalone Client
message(STATUS "Compiling Client")
add_executable(GramsReadoutClient client.cpp
        tcp_connection.cpp
        tcp_connection.h
        tcp_protocol.h
        tcp_protocol.cpp)
target_compile_definitions(GramsReadoutClient PRIVATE ASIO_STANDALONE)
target_include_directories(GramsReadoutClient PRIVATE ${ASIO_INCLUDE_DIR})
target_link_libraries(GramsReadoutClient PRIVATE pthread)


## TCP/IP Connection
message(STATUS "Compiling Server")
add_executable(GramsReadoutConnect server.cpp
        tcp_connection.cpp
        tcp_connection.h
        tcp_protocol.h
        tcp_protocol.cpp)

target_compile_definitions(GramsReadoutConnect PRIVATE ASIO_STANDALONE)
target_include_directories(GramsReadoutConnect PRIVATE ${ASIO_INCLUDE_DIR})
target_link_libraries(GramsReadoutConnect PRIVATE pthread)

set(COMPILE_UNIT_TESTS FALSE)
if(COMPILE_UNIT_TESTS)
    add_subdirectory(unit_test)
endif()
