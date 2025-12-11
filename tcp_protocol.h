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
#include <utility>
#include <netinet/in.h>

// class Command;
class Command {
public:

    uint16_t command;
    std::vector<int32_t> arguments;

    Command(const uint16_t cmd, const size_t vec_size) : command(cmd), arguments(vec_size) {}

    // Public setter for the arguemnts of the packet
    void SetArguments(const std::vector<int32_t> &args) { arguments = std::move(args); };

    void print() {
        std::cout << "Command:      " << command << " \n";
        std::cout << "Args: ";
        for (auto &arg : arguments) {
            std::cout << " " << arg;
        }
        std::cout << std::endl;
    }
};

class TCPProtocol : public Command {

public:

    // Constants for start and end codes
    static constexpr uint16_t kStartCode1 = 0xEB90; // BE 0xEB90 LE 0x90EB
    static constexpr uint16_t kStartCode2 = 0x5B6A; // BE 0x5B6A LE 0x6A5B
    static constexpr uint16_t kEndCode1 = 0xC5A4;   // BE 0xC5A4 LE 0xA4C5
    static constexpr uint16_t kEndCode2 = 0xD279;   // BE 0xD279 LE 0x79D2

    static constexpr size_t header_size_ = 8;
    static constexpr size_t footer_size_ = 6;

    // Constructor
    TCPProtocol(const uint16_t cmd, const size_t vec_size) :
    Command(cmd, vec_size),
    arg_count(vec_size),
    num_bytes_(0),
    calc_crc_(0),
    decoder_arg_count_(0),
    decode_state_(kHeader) {

        start_code1 = kStartCode1;
        start_code2 = kStartCode2;
        crc = 1;
        end_code1 = kEndCode1;
        end_code2 = kEndCode2;
    }

    // Structure for a packet
    uint16_t start_code1 = kEndCode1;
    uint16_t start_code2;
    // command, from Command class
    uint16_t arg_count;
    // std::vector<int32_t> arguments, from Command class
    uint16_t crc;
    uint16_t end_code1;
    uint16_t end_code2;

    struct Header {
        uint16_t start_code1;
        uint16_t start_code2;
        // uint16_t cmd_code;
        // uint16_t arg_count;
    };

    struct CommandArg {
        uint16_t cmd_code;
        uint16_t arg_count;
    };

    struct Footer {
        uint16_t crc;
        uint16_t end_code1;
        uint16_t end_code2;
    };

    // Heart beat command
    static constexpr uint16_t kHeartBeat = 0xFFFF;
    static constexpr uint32_t kCorruptData = 0xFFFFFFFF;

    // static uint16_t CalcCRC(std::vector<uint8_t> &pbuffer, size_t num_bytes, uint16_t crc = 0);
    // static uint16_t CalcCRC(const uint8_t *pbuffer, size_t num_bytes, uint16_t crc = 0);

    static uint16_t getHeaderSize() { return header_size_; }
    static uint16_t getFooterSize() { return footer_size_; }
    static bool GoodStartCode(const uint16_t code1, const uint16_t code2) {
        return code1 == kStartCode1 && code2 == kStartCode2;
    }
    static bool GoodEndCode(const uint16_t code1, const uint16_t code2) {
        return code1 == kEndCode1 && code2 == kEndCode2;
    }

    // Start decoder waiting for a Header, can be called if packet is lost and we need to restart
    void RestartDecoder() { decode_state_ = kHeader; }
    uint16_t GetDecoderState() const { return static_cast<uint16_t>(decode_state_); }

    // Make sure the receiver buffer is large enough to hold at least the max size packet 
    // allowed by protocol approx. 2^16 4B words = 262kB (setting to 1MB ~ 3.5 packets)
    constexpr static size_t RECVBUFFSIZE = 1000000;
    size_t DecodePackets(std::array<uint8_t, RECVBUFFSIZE> &pbuffer, Command &recv_cmd);
    // std::vector<uint8_t> Serialize();
    // TCPProtocol Deserialize(std::vector<uint8_t> &data);
    void print();

    uint16_t CalcCRC(std::vector<uint8_t>& pbuffer, size_t num_bytes, uint16_t crc=0) {
        for (size_t i = 0; i < num_bytes; i++) {
            crc ^= pbuffer.at(i);
            for (int j = 0; j < 8; j++) {
                if (crc & 1) { crc = (crc >> 1) ^ 0x8408; }
                else { crc >>= 1; }
            }
        }
        return crc;
    }

    uint16_t CalcCRC(const uint8_t *pbuffer, size_t num_bytes, uint16_t crc=0) {
        for (size_t i = 0; i < num_bytes; i++) {
            crc ^= pbuffer[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 1) { crc = (crc >> 1) ^ 0x8408; }
                else { crc >>= 1; }
            }
        }
        return crc;
    }

    std::vector<uint8_t> Serialize() {
        std::vector<uint8_t> buffer;
        //std::cout << "Serialize nArgs: " << arguments.size() << std::endl;
        // Header + arguments + footer
        size_t header_content_bytes = header_size_ + arguments.size() * sizeof(int32_t);
        buffer.resize(header_content_bytes + footer_size_);

        size_t offset = 0;
        auto Append = [&](const void* data, size_t size) {
            std::memcpy(buffer.data() + offset, data, size);
            offset += size;
        };
        // Header
        uint16_t tmp16 = htons(start_code1);
        Append(&tmp16, sizeof(start_code1));
        tmp16 = htons(start_code2);
        Append(&tmp16, sizeof(start_code2));

        tmp16 = htons(command);
        Append(&tmp16, sizeof(command));
        tmp16 = htons(arg_count);
        Append(&tmp16, sizeof(arg_count));
        uint32_t tmp32;
        for (int32_t arg : arguments) {
            tmp32 = htonl(arg);
            Append(&tmp32, sizeof(arg));
        }
        crc = CalcCRC(buffer, header_content_bytes);
        tmp16 = htons(crc);
        Append(&tmp16, sizeof(crc));
        // Footer
        tmp16 = htons(end_code1);
        Append(&tmp16, sizeof(end_code1));
        tmp16 = htons(end_code2);
        Append(&tmp16, sizeof(end_code2));

        //std::cout << "buffer.size() " << buffer.size() << std::endl;

        return buffer;
    }

    Command Deserialize(std::vector<uint8_t> &data) {

        if (data.size() % 2 != 0) {
            throw std::runtime_error("Invalid data size for deserialization.");
        }

        // The minimum data size is 16b so we are safe to cast from 8b to 16b
        // without worrying about bit alignment. Preallocate the memory with reserve()
        // Get a pointer to the start of the byte data, cast to the new pointer type
        uint16_t* p_start16 = reinterpret_cast<uint16_t*>(data.data());
        size_t num_uint16s = data.size() / 2;

        // Construct the new vector by copying from the reinterpreted memory range
        std::vector<uint16_t> pbuffer(p_start16, p_start16 + num_uint16s);
        for (auto &word : pbuffer) { word = ntohs(word); }

        size_t word_count = 0;
        if ( !GoodStartCode(pbuffer.at(word_count), pbuffer.at(word_count+1)) ) {
            std::cerr << "Bad start code!" << std::endl;
        }
        word_count += 2;

        uint16_t cmd_code = pbuffer.at(word_count++);
        uint16_t arg_count = pbuffer.at(word_count++);

        // We need to know how many args to initialize the packet
        // TCPProtocol packet(cmd_code, arg_count);
        Command packet(cmd_code, arg_count);

        // The args are 32b so cast 16b vector to 32b
        int32_t* p_start32 = reinterpret_cast<int32_t*>(data.data());
        p_start32 += word_count / 2;
        std::vector<int32_t> arg_buffer(p_start32, p_start32 + arg_count);
        for (auto &word : arg_buffer) { word = ntohl(word); }

        packet.SetArguments(arg_buffer);
        word_count += sizeof(uint16_t) * arg_count; // keep track of how many 16b words

        // Check the CRC, it includes everything except the footer
        uint16_t decoded_crc = pbuffer.at(word_count++);
        uint16_t calc_crc = CalcCRC(data, data.size() - sizeof(Footer));
        if (calc_crc != decoded_crc) {
            std::cerr << "Bad CRC! Calc/Decoded=" << calc_crc << "/" << decoded_crc << std::endl;
        }

        if ( !GoodEndCode(pbuffer.at(word_count), pbuffer.at(word_count+1)) ) {
            std::cerr << "Bad end code!" << std::endl;
        }

        return packet;
    }

private:

    template <typename T, typename U>
    T* reinterpret_buffer(U* buffer) {
        // static_assert(std::is_pointer_v<T*>, "T must be a pointer type");
        return reinterpret_cast<T*>(buffer);
    }

    void PrintPacket(const Command &cmd);

    size_t num_bytes_;
    uint16_t calc_crc_;
    u_int16_t decoder_arg_count_;

    enum PacketDecoderStates {
        kHeader,
        kCommandArg,
        kArgs,
        kFooter
    };

    PacketDecoderStates decode_state_;

    // FIXME the command buffers should be declared here
//    std::deque<Command> recv_command_buffer_2;

};

#endif  // TCP_PROTOCOL_H
