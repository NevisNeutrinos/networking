#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include <iostream>
#include <asio.hpp>
#include <deque>
#include <optional>
#include <condition_variable>
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

    TCPProtocol tcp_protocol_;

    std::optional<tcp::acceptor> acceptor_;
    std::optional<tcp::socket> accept_socket_;
    tcp::endpoint endpoint_;
    tcp::socket socket_;
    // uint8_t buffer_[1000]{};
    std::array<uint8_t, 10000> buffer_{};
    short port_;
    bool client_connected_;
    asio::steady_timer timeout_;
    std::atomic_bool stop_cmd_read_;
    std::atomic_bool stop_server_;

    std::mutex mutex_;

    asio::error_code read_error_;
    size_t requested_bytes_;
    size_t received_bytes_;
    std::chrono::time_point<std::chrono::steady_clock> chrono_read_start_;

    void StartClient();
    void StartServer();
    void ReadHandler();
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
    // become available in the read buffer.
    std::condition_variable cmd_available_;

};

#endif  // TCP_SERVER_H_
