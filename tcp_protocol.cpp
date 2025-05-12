//
// Created by Jon Sensenig on 2/6/25.
//

#include "tcp_protocol.h"

TCPProtocol::TCPProtocol(const uint16_t cmd, const size_t vec_size) : Command(0,0),
                                                                        command_code(cmd),
                                                                      arg_count(vec_size),
                                                                      arguments(vec_size) {
    start_code1 = kStartCode1;
    start_code2 = kStartCode2;
    crc = 1;
    end_code1 = kEndCode1;
    end_code2 = kEndCode2;
}

void TCPProtocol::PrintPacket(const Command &cmd) {
    std::cout << "Cmd: " << cmd.command << std::endl;
    std::cout << "Num Args: " << cmd.arguments.size() << std::endl;
    std::cout << "Args: ";
    for (const auto &arg : cmd.arguments) std::cout << arg << " ";
    std::cout << std::endl;
}

size_t TCPProtocol::DecodePackets(std::array<uint8_t, RECVBUFFSIZE> &pbuffer, std::deque<Command> &cmd_buffer) {
    bool debug = false;

    switch (decode_state_) {
        case kHeader: {
            calc_crc_ = CalcCRC(pbuffer.data(), sizeof(Header));
            const auto *header = reinterpret_buffer<Header>(&pbuffer);
            if (!GoodStartCode(ntohs(header->start_code1), ntohs(header->start_code2))) {
                std::cerr << "Bad Start code! [" << header->start_code1 << "] ["<< header->start_code2 << "]" << std::endl;
            }
            decoder_arg_count_ = ntohs(header->arg_count);
            // Construct a Command packet in the buffer with the command and expected number of args
            cmd_buffer.emplace_back(ntohs(header->cmd_code), decoder_arg_count_);
            decode_state_ = kArgs;
            return sizeof(int32_t) * decoder_arg_count_;
        }
        case kArgs: {
            calc_crc_ = CalcCRC(pbuffer.data(), sizeof(int32_t) * decoder_arg_count_, calc_crc_);
            const auto *buf_ptr_32 = reinterpret_buffer<uint32_t>(&pbuffer);
            // We already have the Command packet with the correct number of args, now just fill it
            for (size_t i = 0; i < decoder_arg_count_; i++) {
                cmd_buffer.back().arguments[i] = ntohl(buf_ptr_32[i]);
            }
            decode_state_ = kFooter;
            return sizeof(Footer);
        }
        case kFooter: {
            const auto *footer = reinterpret_buffer<Footer>(&pbuffer);
            if (ntohs(footer->crc) != calc_crc_) {
                std::cerr << "Bad CRC! Received [" << ntohs(footer->crc) << "] Calculated [" << calc_crc_ << "]"  << std::endl;
            }
            if(!GoodEndCode(ntohs(footer->end_code1), ntohs(footer->end_code2))) {
                std::cerr << "Bad end code! [" << footer->end_code1 << "] ["<< footer->end_code2 << "]" << std::endl;
            }
            if (debug) PrintPacket(cmd_buffer.back());
            decode_state_ = kHeader;
            return SIZE_MAX; // end of packet
        }
        default: {
            std::cerr << "Ended up in an invalid state! How???" << std::endl;
            // Restart the decoder if we end up here
            decode_state_ = kHeader;
            return SIZE_MAX; // end of packet
        }
    } // switch
    return false;
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
    uint16_t tmp16 = htons(start_code1);
    Append(&tmp16, sizeof(start_code1));
    tmp16 = htons(start_code2);
    Append(&tmp16, sizeof(start_code2));
    tmp16 = htons(command_code);
    Append(&tmp16, sizeof(command_code));
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
    tmp16 = htons(end_code1);
    Append(&tmp16, sizeof(end_code1));
    tmp16 = htons(end_code2);
    Append(&tmp16, sizeof(end_code2));

    std::cout << "buffer.size() " << buffer.size() << std::endl;

    return buffer;
}

void TCPProtocol::print() {
    std::cout << "********************************" << std::endl;
    std::cout << std::hex;
    std::cout   << "Start Code 1: 0x" << start_code1 << " \n"
                << "Start Code 2: 0x" << start_code2 << " \n";
    std::cout << std::dec;
    std::cout << "Command:      " << command_code << " \n"
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