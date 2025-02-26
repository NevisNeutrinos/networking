#include "tcp_connection.h"

TCPConnection::TCPConnection(asio::io_context& io_context, const std::string& ip_address,
    const short port, const bool is_server)
    : endpoint_(asio::ip::make_address(ip_address), port),
      socket_(io_context),
      port_(port),
      client_connected_(false),
      timeout_(io_context),
      stop_cmd_read_(false) {

    read_state_ = kHeader;
    if (is_server) {
        std::cout << "Starting Server on Address [" << ip_address << "] Port [" << port<< "]" << std::endl;
        acceptor_.emplace(tcp::acceptor(io_context, endpoint_));
        accept_socket_.emplace(io_context);
        StartServer();
    }
    else {
        std::cout << "Starting Client on Address [" << ip_address << "] Port [" << port<< "]" << std::endl;
        StartClient();
    }
}

TCPConnection::~TCPConnection() {
    std::cout << "Clearing buffers and closing connections ..." << std::endl;
    send_command_buffer_.clear();
    recv_command_buffer_.clear();

    if (socket_.is_open()) {
        socket_.close();
    }
    if (acceptor_ && acceptor_->is_open()) {
        acceptor_->close();
    }
    if (accept_socket_ && accept_socket_->is_open()) {
        accept_socket_->close();
    }
}

void TCPConnection::StartServer() {
    acceptor_->async_accept(*accept_socket_, [this](std::error_code ec) {
      if (!ec) {
        std::cout << "New client connected!" << std::endl;
          socket_ = std::move(*accept_socket_);
          std::thread(&TCPConnection::ReadWriteHandler, this).detach();
      }
      StartServer();  // Accept the next client
    });
}

void TCPConnection::StartClient() {
    // Set up a timeout (e.g., 3 seconds)
    timeout_.expires_after(std::chrono::seconds(3));
    timeout_.async_wait([this](const asio::error_code& ec) {
        if (!ec) { // Timeout expired
            std::cerr << "Connection timed out.\n";
            socket_.cancel();  // Force async_connect to fail
            socket_.close();
        }
    });

    socket_.async_connect(endpoint_,
    [this](const asio::error_code& ec) {
        if (!ec) {
            timeout_.cancel();
            std::cout << "Connected to server!" << std::endl;
            std::thread(&TCPConnection::ReadWriteHandler, this).detach();
            client_connected_ = true;
        } else {
            std::cerr << "Connection failed: " << ec.message() << std::endl;
        }
    });
}

void TCPConnection::ReadWriteHandler() {
    while (socket_.is_open()) {
        if (socket_.available()) ReadData();
        if (DataInSendBuffer()) SendData();
        usleep(50000);
    }
}

size_t TCPConnection::ReadHandler(size_t read_bytes) {
    size_t num_bytes = read(socket_, asio::buffer(buffer_, read_bytes));
    return num_bytes;
}

void TCPConnection::ReadData() {
    std::cout << "Read Data!" << std::endl;

    bool debug = false;
    std::vector<uint8_t> crc_buffer;
    uint16_t command_code = 0;
    uint16_t arg_count = 0;
    uint16_t calc_crc = 0;
    size_t num_bytes = 0;
    bool complete_packet = false;

    // TODO: add timeout so we don't get stuck waiting for a potentially failed packet.
    while (!complete_packet) {
        switch (read_state_) {
            case kHeader: {
                complete_packet = false;
                num_bytes = ReadHandler(TCPProtocol::getHeaderSize());
                calc_crc = TCPProtocol::CalcCRC(reinterpret_cast<uint8_t *>(&buffer_), num_bytes);
                auto *header = reinterpret_cast<TCPProtocol::Header *>(&buffer_);
                if (!TCPProtocol::GoodStartCode(header->start_code1, header->start_code2)) {
                    std::cerr << "Bad start code!" << std::endl;
                }
                arg_count = header->arg_count;
                std::lock_guard<std::mutex> lock(mutex_);
                recv_command_buffer_.emplace_back(header->cmd_code, header->arg_count);
                std::cout << "NArgs: " << arg_count << std::endl;
                read_state_ = kArgs;
                break;
            }
            case kArgs: {
                num_bytes = ReadHandler(arg_count * sizeof(int32_t));
                calc_crc = TCPProtocol::CalcCRC(reinterpret_cast<uint8_t *>(&buffer_), num_bytes, calc_crc);
                auto *buf_ptr_32 = reinterpret_cast<int32_t *>(&buffer_);
                std::lock_guard<std::mutex> lock(mutex_);
                for (int i = 0; i < arg_count; i++) {
                    recv_command_buffer_.back().arguments[i] = buf_ptr_32[i];
                }
                read_state_ = kFooter;
                break;
            }
            case kFooter: {
                ReadHandler(TCPProtocol::getFooterSize());
                auto *footer = reinterpret_cast<TCPProtocol::Footer *>(&buffer_);
                if (footer->crc != calc_crc) {
                    std::cerr << "Bad CRC! Received [" << footer->crc << "] Calculated [" << calc_crc << "]"  << std::endl;
                }
                if(!TCPProtocol::GoodEndCode(footer->end_code1, footer->end_code2)) {
                    std::cerr << "Bad end code!" << std::endl;
                }
                std::cout << "Received all data!" << std::endl;
                read_state_ = debug ? kEndRecv : kHeader;
                complete_packet = !debug;
                cmd_available_.notify_all();
                break;
            }
            case kEndRecv: {
                // std::lock_guard<std::mutex> lock(mutex_);
                std::cout << "Cmd: " << recv_command_buffer_.back().command << std::endl;
                std::cout << "Num Args: " << recv_command_buffer_.back().arguments.size() << std::endl;
                std::cout << "Args: ";
                for (auto &arg : recv_command_buffer_.back().arguments) {
                    std::cout << arg << " ";
                }
                std::cout << " " << std::endl;
                // EchoData();
                read_state_ = kHeader;
                complete_packet = true;
                break;
            }
            default: {
                throw std::runtime_error("Invalid state");
            }
        }
    }
}

bool TCPConnection::DataInSendBuffer() {
    std::lock_guard<std::mutex> lock(mutex_);
    return !send_command_buffer_.empty();
}

bool TCPConnection::DataInRecvBuffer() {
    std::lock_guard<std::mutex> lock(mutex_);
    return !recv_command_buffer_.empty();
}

Command TCPConnection::ReadRecvBuffer() {
    std::unique_lock cmd_lock(mutex_);
    cmd_available_.wait(cmd_lock, [this] {
        return !recv_command_buffer_.empty() || stop_cmd_read_;
    });
    if (stop_cmd_read_) return {0, 0};
    Command command = recv_command_buffer_.front();
    recv_command_buffer_.pop_front();
    return command;
}

std::vector<Command> TCPConnection::ReadRecvBuffer(size_t num_cmds) {
    std::unique_lock lock(mutex_);
    size_t buffer_size = recv_command_buffer_.size();
    lock.unlock();

    size_t num_reads = num_cmds;
    if (num_cmds > buffer_size) {
        num_reads = buffer_size;
        std::cout << "Requested commands larger than buffer size, reading entire buffer: "
        << num_reads << std::endl;
    }

    std::vector<Command> commands;
    commands.reserve(num_reads);

    for (size_t i = 0; i < num_reads; i++) {
        commands.push_back(ReadRecvBuffer());
    }
    return commands;
}

void TCPConnection::EchoData() {
    std::cout << "EchoData!" << std::endl;
    while (DataInRecvBuffer()) {
        WriteSendBuffer(ReadRecvBuffer());
        std::lock_guard<std::mutex> lock(mutex_);
        recv_command_buffer_.pop_front();
    }
}

void TCPConnection::WriteSendBuffer(uint16_t cmd, std::vector<int32_t>& vec) {
    std::lock_guard<std::mutex> lock(mutex_);
    send_command_buffer_.emplace_back(cmd, vec.size());
    send_command_buffer_.back().arguments = std::move(vec);
}

void TCPConnection::WriteSendBuffer(const Command& cmd_struct) {
    std::lock_guard<std::mutex> lock(mutex_);
    send_command_buffer_.emplace_back(cmd_struct);
}

void TCPConnection::SendData() {
    size_t num_bytes = 0;
    while (DataInSendBuffer()) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Construct the TCP packet which will be deserialized and sent.
        TCPProtocol packet(send_command_buffer_.front().command,
                           send_command_buffer_.front().arguments.size()); // cmd, vec.size
        packet.arguments = std::move(send_command_buffer_.front().arguments);
        std::vector<uint8_t> buffer = packet.Serialize();
        num_bytes += asio::write(socket_, asio::buffer(buffer));
        send_command_buffer_.pop_front();
    }
    std::cout << "Sent Bytes: " << num_bytes << std::endl;
}


// int main() {
//     try {
//         asio::io_context io_context;
//         TCPConnection server(io_context, 12345);
//         io_context.run();  // Run the IO context loop
//     } catch (const std::exception& e) {
//         std::cerr << "Error: " << e.what() << std::endl;
//     }
//     return 0;
// }
