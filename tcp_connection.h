#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include <iostream>
#include <asio.hpp>
#include <deque>

#include "tcp_protocol.h"

using asio::ip::tcp;

class TCPConnection {
public:
    TCPConnection(asio::io_context& io_context, const std::string& ip_address,
        short port, bool is_server);
    ~TCPConnection();

    std::deque<Command> send_command_buffer_;
    std::deque<Command> recv_command_buffer_;

    // Interface to send/receive commands and data
    void WriteSendBuffer(uint16_t cmd, std::vector<int32_t>& vec);
    void WriteSendBuffer(const Command& cmd_struct);

    Command ReadRecvBuffer();
    std::vector<Command> ReadRecvBuffer(size_t num_cmds);

    bool getSocketIsOpen() const { return socket_.is_open(); }
    void setStopCmdRead(const bool stop_read) {
        stop_cmd_read_ = stop_read;
        cmd_available_.notify_all();
    }

private:
    std::optional<tcp::acceptor> acceptor_;
    std::optional<tcp::socket> accept_socket_;
    tcp::endpoint endpoint_;
    tcp::socket socket_;
    uint8_t buffer_[1000]{};
    short port_;
    bool client_connected_;
    asio::steady_timer timeout_;
    std::atomic_bool stop_cmd_read_;

    std::mutex mutex_;

    void StartClient();
    void StartServer();
    size_t ReadHandler(size_t read_bytes);
    void ReadWriteHandler();
    void ReadData();
    void EchoData();
    void SendData();
    bool DataInSendBuffer();
    bool DataInRecvBuffer();

    enum ReadStates {
        kHeader,
        kArgs,
        kFooter,
        kEndRecv
    };

    ReadStates read_state_;

    // Creates a blocking wait for commands to
    // become available in the buffer.
    std::condition_variable cmd_available_;

};

#endif  // TCP_SERVER_H_
