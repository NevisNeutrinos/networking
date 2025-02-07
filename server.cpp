#include "../networking/tcp_connection.h"
#include <iostream>

int main() {
    try {
        asio::io_context io_context;
        TCPConnection server(io_context, 12345, true);
        io_context.run();  // Run the IO context loop
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}