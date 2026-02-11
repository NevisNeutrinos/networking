#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include <iostream>
#include <asio.hpp>
#include <deque>
#include <chrono>
#include <optional>
#include <condition_variable>
#include <thread>
#include "tcp_protocol.h"

using asio::ip::tcp;

class TCPConnection : public std::enable_shared_from_this<TCPConnection>, public Command {
public:
    TCPConnection(asio::io_context& io_context, const std::string& ip_address,
        uint16_t port, bool is_server, bool use_heartbeat, bool monitor_link);
    ~TCPConnection();

    std::deque<Command> send_command_buffer_;
    std::deque<Command> recv_command_buffer_;

    // Interface to send/receive commands and data
    void Start();
    void WriteSendBuffer(uint16_t cmd, std::vector<uint32_t> &vec);
    void WriteSendBuffer(const Command& cmd_struct);
    void WriteRecvBuffer(const Command& cmd_struct);
    bool DataInSendBuffer();
    bool DataInRecvBuffer();

    Command ReadRecvBuffer();
    std::vector<Command> ReadRecvBuffer(size_t num_cmds);

    bool getSocketIsOpen() const { return socket_.is_open(); }
    void setStopCmdRead(const bool stop_read) {
        stop_server_.store(true);
        stop_cmd_read_ = stop_read;
        cmd_available_.notify_all();
    }

    // Functions to help manage the async IO context from python
    std::thread python_io_context_thread_;
    std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> python_work_guard_;
    void PythonRun(asio::io_context &ctx) {
        Start();
        python_work_guard_ = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(ctx.get_executor());
        python_io_context_thread_ = std::thread([&ctx]() {
            try {
                ctx.run();
            } catch (const std::exception &e) {
                std::cerr << "io_context thread exception: " << e.what() << std::endl;
            }
        });
    }
    void PythonStop(asio::io_context &ctx) {
        setStopCmdRead(true);
        ctx.stop();
        if (python_io_context_thread_.joinable()) python_io_context_thread_.join();
        python_work_guard_.reset();
    }

    // Function to provide access to the decoder without the sockets
    Command DecodeRawPacket(std::vector<uint8_t>& raw_buff);

private:

    TCPProtocol tcp_protocol_;

    std::optional<tcp::acceptor> acceptor_;
    std::optional<tcp::socket> accept_socket_;
    tcp::endpoint endpoint_;
    tcp::socket socket_;
    std::array<uint8_t, TCPProtocol::RECVBUFFSIZE> buffer_{};
    uint16_t port_;
    bool client_connected_;
    asio::steady_timer timeout_;
    asio::steady_timer send_timeout_;
    std::atomic_bool stop_cmd_read_;
    std::atomic_bool stop_cmd_write_;
    std::atomic_bool stop_server_;
    std::atomic_bool use_heartbeat_;
    std::atomic_bool is_server_;
    bool monitor_link_;  // set true if a monitor link, else assumed to be command link
    std::thread read_data_thread_;
    std::thread write_data_thread_;

    bool debug_flag_;
    std::atomic_bool restart_client_;
    std::mutex send_mutex_;
    std::mutex recv_mutex_;
    std::mutex sock_mutex_;

    asio::error_code read_error_;
    size_t requested_bytes_;
    size_t received_bytes_;
    std::chrono::high_resolution_clock::time_point chrono_read_start_;
    asio::steady_timer timer_;
    bool read_in_progress_{false};
    std::atomic_bool packet_read_{false};
    std::atomic_bool reset_read_timer_{false};
    std::chrono::time_point<std::chrono::steady_clock> start_;

    void StartClient();
    void ClearSocketBuffer();
    void StartServer();
    void ReadHandler(const asio::error_code& ec, std::size_t bytes_transferred);
    void SendHandler(const asio::error_code& ec, const std::size_t &bytes_sent);
    void ReadData();
    void EchoData();
    void SendData();
    void SendHeartbeat();

    // Creates a blocking wait for commands to
    // become available in the read buffer.
    std::condition_variable cmd_available_;
    std::condition_variable send_cmd_available_;
    Command recv_command_;

};

#endif  // TCP_SERVER_H_
