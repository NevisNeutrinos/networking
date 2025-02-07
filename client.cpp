#include <iostream>
#include <asio.hpp>
#include "tcp_protocol.h"

using asio::ip::tcp;


// Deserialize Packet from a raw buffer
TCPProtocol Deserialize(const std::vector<uint8_t>& buffer) {

    TCPProtocol packet(1,1);

    size_t offset = 0;
    std::cout << "Entered deserializing" << std::endl;

    auto Extract = [&](void* data, size_t size) {
        std::memcpy(data, buffer.data() + offset, size);
        offset += size;
    };

    Extract(&packet.start_code1, sizeof(packet.start_code1));
    Extract(&packet.start_code2, sizeof(packet.start_code2));
    Extract(&packet.command_code, sizeof(packet.command_code));
    Extract(&packet.arg_count, sizeof(packet.arg_count));

    packet.arguments.resize(packet.arg_count);
    for (int32_t& arg : packet.arguments) Extract(&arg, sizeof(arg));

    Extract(&packet.crc, sizeof(packet.crc));
    Extract(&packet.end_code1, sizeof(packet.end_code1));
    Extract(&packet.end_code2, sizeof(packet.end_code2));
    std::cout << "Returning deserializing" << std::endl;
    return packet;
}

int main() {
    try {
        asio::io_context io_context;
        tcp::socket socket(io_context);
        socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), 12345));

        std::cout << "Connected to server!" << std::endl;
        int32_t arg = 0;

        while (true) {
            std::string message;
            std::cout << "Enter integer: ";
            std::getline(std::cin, message);

            TCPProtocol packet(42, 5);
            packet.arguments = {10,20,30,40,std::stoi(message)};

            std::vector<uint8_t> buffer = packet.Serialize();
            asio::write(socket, asio::buffer(buffer));

            // while (socket.available()) {
            std::vector<uint8_t> reply(buffer.size());
            std::size_t length = read(socket, asio::buffer(reply, buffer.size()));

            TCPProtocol reply_packet = Deserialize(reply);
            std::cout << "Server response nBytes: " << length << std::endl;
            reply_packet.print();
            // }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
