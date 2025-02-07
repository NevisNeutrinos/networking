#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include <iostream>
#include <asio.hpp>
#include <deque>

#include "tcp_protocol.h"

using asio::ip::tcp;

class TCPConnection {
public:
    TCPConnection(asio::io_context& io_context, short port, bool is_server);

    std::deque<Command> send_command_buffer_;
    std::deque<Command> recv_command_buffer_;

private:
    tcp::acceptor acceptor_;
    tcp::socket socket_, accept_socket_;
    uint8_t buffer_[1000];
    short port_;

    void MakeClient();
    void MakeServer();
    size_t ReadHandler(size_t read_bytes);
    void ReadData();
    void EchoData();
    void SendData();

    enum ReadStates {
        kHeader,
        kArgs,
        kFooter,
        kEndRecv
    };

    ReadStates read_state_;

};

#endif  // TCP_SERVER_H_
