//
// Created by Jon Sensenig on 2/5/25.
//

#ifndef TCP_PROTOCOL_H
#define TCP_PROTOCOL_H

#include <cstdint>
#include <cstring>  // memcpy
#include <iostream>
#include <vector>

class TCPProtocol {

private:

    // Constants
    static constexpr uint16_t kStartCode1 = 0xEB90;
    static constexpr uint16_t kStartCode2 = 0x5B6A;
    static constexpr uint16_t kEndCode1 = 0xC5A4;
    static constexpr uint16_t kEndCode2 = 0xD279;

    static constexpr size_t header_size_ = 8;
    static constexpr size_t footer_size_ = 6;

public:

    // Constructor
    TCPProtocol(uint16_t cmd, size_t vec_size);

    // Structure for a packet
    uint16_t start_code1 = kEndCode1;
    uint16_t start_code2;
    uint16_t command_code;
    uint16_t arg_count;
    std::vector<int32_t> arguments;
    uint16_t crc;
    uint16_t end_code1;
    uint16_t end_code2;

    struct Header {
        uint16_t start_code1;
        uint16_t start_code2;
        uint16_t cmd_code;
        uint16_t arg_count;
    };

    struct Footer {
        uint16_t crc;
        uint16_t end_code1;
        uint16_t end_code2;
    };

    static uint16_t CalcCRC(std::vector<uint8_t> &pbuffer, size_t num_bytes, uint16_t crc = 0);
    static uint16_t CalcCRC(const uint8_t *pbuffer, size_t num_bytes, uint16_t crc = 0);

    static uint16_t getHeaderSize() { return header_size_; }
    static uint16_t getFooterSize() { return footer_size_; }
    static bool GoodStartCode(const uint16_t code1, const uint16_t code2) {
        return code1 == kStartCode1 && code2 == kStartCode2;
    }
    static bool GoodEndCode(const uint16_t code1, const uint16_t code2) {
        return code1 == kEndCode1 && code2 == kEndCode2;
    }

    std::vector<uint8_t> Serialize();
    void print();

};

class Command {
public:

    uint16_t command;
    std::vector<int32_t> arguments;

    Command(const uint16_t cmd, const size_t vec_size) : command(cmd), arguments(vec_size) {}

    void print() {
        std::cout << "Command:      " << command << " \n";
        std::cout << "Args: ";
        for (auto &arg : arguments) {
            std::cout << " " << arg;
        }
        std::cout << std::endl;
    }
};

#endif  // TCP_PROTOCOL_H
