cmake_minimum_required(VERSION 3.15)
project(GramsNetworkConnection)

set(CMAKE_CXX_STANDARD 17)

# Try to catch any potential problems at compile time
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
     add_compile_options(-Wall -Wextra -pedantic)
endif()

# The networking is built on top of the ASIO header library
# note it does not require BOOST
file(GLOB ASIO_SEARCH_CUSTOM_PATH "$ENV{HOME}/asio*/include")
find_path(ASIO_INCLUDE_DIR asio.hpp
    HINTS $ENV{HOME}/asio*/include NO_DEFAULT_PATH)

if (ASIO_INCLUDE_DIR)
    message(STATUS "[networking] Found ASIO: ${ASIO_INCLUDE_DIR}")
    include_directories("${ASIO_INCLUDE_DIR}")
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

########################################
# pGRAMS Connection Configuration
# Client
message(STATUS "Compiling pGRAMS Client")
add_executable(pgrams_client unit_test/client_grams_setup.cpp
        tcp_connection.cpp
        tcp_connection.h
        tcp_protocol.h
        tcp_protocol.cpp)
target_compile_definitions(pgrams_client PRIVATE ASIO_STANDALONE)
target_include_directories(pgrams_client PRIVATE ${ASIO_INCLUDE_DIR})
target_link_libraries(pgrams_client PRIVATE pthread)


## Server
message(STATUS "Compiling pGRAMS Server")
add_executable(pgrams_server unit_test/server_grams_setup.cpp
        tcp_connection.cpp
        tcp_connection.h
        tcp_protocol.h
        tcp_protocol.cpp)

target_compile_definitions(pgrams_server PRIVATE ASIO_STANDALONE)
target_include_directories(pgrams_server PRIVATE ${ASIO_INCLUDE_DIR})
target_link_libraries(pgrams_server PRIVATE pthread)


set(COMPILE_UNIT_TESTS FALSE)
if(COMPILE_UNIT_TESTS)
    add_subdirectory(unit_test)
endif()
