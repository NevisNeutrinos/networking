//
// Created by Jon Sensenig on 2/6/25.
//

#include "tcp_protocol.h"

TCPProtocol::TCPProtocol(const uint16_t cmd, const size_t vec_size) :
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

void TCPProtocol::PrintPacket(const Command &cmd) {
    std::cout << "Cmd: " << cmd.command << std::endl;
    std::cout << "Num Args: " << cmd.arguments.size() << std::endl;
    std::cout << "Args: ";
    for (const auto &arg : cmd.arguments) std::cout << arg << " ";
    std::cout << std::endl;
}

size_t TCPProtocol::DecodePackets(std::array<uint8_t, RECVBUFFSIZE> &pbuffer, std::deque<Command> &cmd_buffer) {
    bool debug = false;
    std::cout << "State: " << decode_state_ << std::endl;
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
            std::cout << "Recv Cmd: " << ntohs(header->cmd_code) << std::endl;
            std::cout << "StartCode: " << ntohs(header->start_code1) << " / " << ntohs(header->start_code2) << std::endl;
            std::cout << "Arg Count: " << decoder_arg_count_ << std::endl;
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
            std::cout << "CRC: " << ntohs(footer->crc) << std::endl;
            std::cout << "EndCode: " << ntohs(footer->end_code1) << " / " << ntohs(footer->end_code2) << std::endl;
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

void TCPProtocol::print() {
    std::cout << "********************************" << std::endl;
    std::cout << std::hex;
    std::cout   << "Start Code 1: 0x" << start_code1 << " \n"
                << "Start Code 2: 0x" << start_code2 << " \n";
    std::cout << std::dec;
    std::cout << "Command:      " << command << " \n"
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