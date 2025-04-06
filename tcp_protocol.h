//
// Created by Jon Sensenig on 2/5/25.
//

#ifndef TCP_PROTOCOL_H
#define TCP_PROTOCOL_H

#include <cstdint>
#include <cstring>  // memcpy
#include <iostream>
#include <vector>
#include <array>
#include <deque>
#include <netinet/in.h>

class Command;

class TCPProtocol {

public:

    // Constants for start and end codes
    static constexpr uint16_t kStartCode1 = 0xEB90; // BE 0xEB90 LE 0x90EB
    static constexpr uint16_t kStartCode2 = 0x5B6A; // BE 0x5B6A LE 0x6A5B
    static constexpr uint16_t kEndCode1 = 0xC5A4;   // BE 0xC5A4 LE 0xA4C5
    static constexpr uint16_t kEndCode2 = 0xD279;   // BE 0xD279 LE 0x79D2

    static constexpr size_t header_size_ = 8;
    static constexpr size_t footer_size_ = 6;

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

    // Start decoder waiting for a Header, can be called if packet is lost and we need to restart
    inline void RestartDecoder() { decode_state_ = kHeader; }

    constexpr static size_t RECVBUFFSIZE = 10000;
    size_t DecodePackets(std::array<uint8_t, RECVBUFFSIZE> &pbuffer, std::deque<::Command> &cmd_buffer);
    std::vector<uint8_t> Serialize();
    void print();

private:

    template <typename T, typename U>
    T* reinterpret_buffer(U* buffer) {
        static_assert(std::is_pointer_v<T*>, "T must be a pointer type");
        return reinterpret_cast<T*>(buffer);
    }

    void PrintPacket(const Command &cmd);

    size_t num_bytes_;
    uint16_t calc_crc_;
    u_int16_t decoder_arg_count_;

    enum PacketDecoderStates {
        kHeader,
        kArgs,
        kFooter
    };

    PacketDecoderStates decode_state_;

    // FIXME the command buffers should be declared here
//    std::deque<Command> recv_command_buffer_2;

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
