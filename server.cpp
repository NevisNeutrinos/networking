#include "tcp_connection.h"
#include <iostream>


int main() {
    try {
        asio::io_context io_context;
        std::cout << "Starting server..." << std::endl;
        TCPConnection server(io_context, "127.0.0.1", 12345, true);
        std::cout << "Starting IO Context..." << std::endl;

        std::thread io_thread([&]() { io_context.run(); });

        // while (true) {
        //     std::string message;
        //     std::cout << "Enter integer: ";
        //     std::getline(std::cin, message);
        //
        //     TCPProtocol packet(42, 5);
        //     Command cmd(45, 3);
        //     cmd.arguments = {20,30,40,std::stoi(message)};
        //     server.send_command_buffer_.emplace_back(cmd);
        //     usleep(10000);
        // }

        io_thread.join();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}
