//
// Created by Jon Sensenig on 2/6/25.
//

#include "tcp_protocol.h"

TCPProtocol::TCPProtocol(const CommandCodes cmd, const size_t vec_size) : command_code(cmd),
                                                                      arg_count(vec_size),
                                                                      arguments(vec_size) {
    start_code1 = kStartCode1;
    start_code2 = kStartCode2;
    crc = 1;
    end_code1 = kEndCode1;
    end_code2 = kEndCode2;
}

uint16_t TCPProtocol::CalcCRC(std::vector<uint8_t>& pbuffer, size_t num_bytes, uint16_t crc) {
    for (size_t i = 0; i < num_bytes; i++) {
        crc ^= pbuffer.at(i);
        for (int j = 0; j < 8; j++) {
            if (crc & 1) { crc = (crc >> 1) ^ 0x8408; }
            else { crc >>= 1; }
        }
    }
    return crc;
}

uint16_t TCPProtocol::CalcCRC(const uint8_t *pbuffer, size_t num_bytes, uint16_t crc) {
    for (size_t i = 0; i < num_bytes; i++) {
        crc ^= pbuffer[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) { crc = (crc >> 1) ^ 0x8408; }
            else { crc >>= 1; }
        }
    }
    return crc;
}

std::vector<uint8_t> TCPProtocol::Serialize() {
    std::vector<uint8_t> buffer;
    std::cout << "Serialize nArgs: " << arguments.size() << std::endl;
    // Header + arguments + footer
    size_t header_content_bytes = header_size_ + arguments.size() * sizeof(int32_t);
    buffer.resize(header_content_bytes + footer_size_);

    size_t offset = 0;
    auto Append = [&](const void* data, size_t size) {
        std::memcpy(buffer.data() + offset, data, size);
        offset += size;
    };

    Append(&start_code1, sizeof(start_code1));
    Append(&start_code2, sizeof(start_code2));
    Append(&command_code, sizeof(command_code));
    Append(&arg_count, sizeof(arg_count));
    for (int32_t arg : arguments) Append(&arg, sizeof(arg));
    crc = CalcCRC(buffer, header_content_bytes);
    Append(&crc, sizeof(crc));
    Append(&end_code1, sizeof(end_code1));
    Append(&end_code2, sizeof(end_code2));

    std::cout << "buffer.size() " << buffer.size() << std::endl;

    return buffer;
}

void TCPProtocol::print() {
    std::cout << "********************************" << std::endl;
    std::cout << std::hex;
    std::cout   << "Start Code 1: 0x" << start_code1 << " \n"
                << "Start Code 2: 0x" << start_code2 << " \n";
    std::cout << std::dec;
    std::cout << "Command:      " << static_cast<uint16_t>(command_code) << " \n"
                << "NArgs:        " << arg_count << " \n";
    std::cout << "Args: ";
    for (auto &arg : arguments) {
        std::cout << " " << arg;
    }
    std::cout << std::endl;
    std::cout << "CRC:          " << crc << " \n";
    std::cout << std::hex;
    std::cout << "End Code 1:   0x" << end_code1 << " \n"
                << "End Code 2:   0x" << end_code2 << std::endl;
    std::cout << std::dec;
    std::cout << "********************************" << std::endl;
}