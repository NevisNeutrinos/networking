#include "tcp_connection.h"

TCPConnection::TCPConnection(asio::io_context& io_context, const std::string& ip_address,
    const short port, const bool is_server)
    : acceptor_(io_context, tcp::endpoint(asio::ip::make_address(ip_address), port)),
      accept_socket_(io_context),
      socket_(io_context),
      port_(port) {

    std::cout << "Server started on port " << port << "..." << std::endl;
    read_state_ = kHeader;
    is_server ? StartServer() : StartClient();
}

void TCPConnection::StartServer() {
    acceptor_.async_accept(accept_socket_, [this](std::error_code ec) {
      if (!ec) {
        std::cout << "New client connected!" << std::endl;
          socket_ = std::move(accept_socket_);
          std::thread(&TCPConnection::ReadWriteHandler, this).detach();
      }
      StartServer();  // Accept the next client
    });
}

void TCPConnection::StartClient() {

    tcp::endpoint endpoint(tcp::endpoint(tcp::v4(), port_));
    socket_.async_connect(endpoint,
    [this](const asio::error_code& ec) {
        if (!ec) {
            std::cout << "Connected to server!" << std::endl;
            std::thread(&TCPConnection::ReadWriteHandler, this).detach();
        } else {
            std::cerr << "Connection failed: " << ec.message() << std::endl;
        }
    });
}

void TCPConnection::ReadWriteHandler() {
    while (socket_.is_open()) {
        if (socket_.available()) ReadData();
        if (!send_command_buffer_.empty()) SendData();
        usleep(50000);
    }
}

size_t TCPConnection::ReadHandler(size_t read_bytes) {
    size_t num_bytes = read(socket_, asio::buffer(buffer_, read_bytes));
    std::cout << "nBytes" << num_bytes << std::endl;
    return num_bytes;
}

void TCPConnection::ReadData() {
    std::cout << "Read Data!" << std::endl;

    bool debug = true;
    uint16_t command_code = 0;
    uint16_t arg_count = 0;
    uint16_t crc = 0;
    size_t num_bytes = 0;
    bool complete_packet = false;

    while (!complete_packet) {
        switch (read_state_) {
            case kHeader: {
                complete_packet = false;
                num_bytes = 0;
                std::cout << "CState" << read_state_ << std::endl;
                num_bytes += ReadHandler(TCPProtocol::getHeaderSize());
                auto *header = reinterpret_cast<TCPProtocol::Header *>(&buffer_);
                if (!TCPProtocol::GoodStartCode(header->start_code1, header->start_code2)) {
                    std::cerr << "Bad start code!" << std::endl;
                }
                arg_count = header->arg_count; 
                recv_command_buffer_.emplace_back(header->cmd_code, header->arg_count);
                std::cout << "NArgs: " << arg_count << std::endl;
                read_state_ = kArgs;
                break;
            }
            case kArgs: {
                std::cout << "CState" << read_state_ << std::endl;
                num_bytes += ReadHandler(arg_count * sizeof(int32_t));
                auto *buf_ptr_32 = reinterpret_cast<int32_t *>(&buffer_);
                for (int i = 0; i < arg_count; i++) {
                    recv_command_buffer_.back().arguments[i] = buf_ptr_32[i];
                }
                read_state_ = kFooter;
                break;
            }
            case kFooter: {
                std::cout << "CState" << read_state_ << std::endl;
                ReadHandler(TCPProtocol::getFooterSize());
                auto *footer = reinterpret_cast<TCPProtocol::Footer *>(&buffer_);
                crc = footer->crc;
                if(!TCPProtocol::GoodEndCode(footer->end_code1, footer->end_code2)) {
                    std::cerr << "Bad end code!" << std::endl;
                }
                std::cout << "Received all data!" << std::endl;
                read_state_ = debug ? kEndRecv : kHeader;
                complete_packet = !debug;
                break;
            }
            case kEndRecv: {
                std::cout << "Cmd: " << recv_command_buffer_.back().command << std::endl;
                std::cout << "Num Args: " << recv_command_buffer_.back().arguments.size() << std::endl;
                std::cout << "Args: ";
                for (auto &arg : recv_command_buffer_.back().arguments) {
                    std::cout << arg << " ";
                }
                std::cout << " " << std::endl;
                EchoData();
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

void TCPConnection::EchoData() {
    std::cout << "EchoData!" << std::endl;

    while (!recv_command_buffer_.empty()) {
        send_command_buffer_.emplace_back(recv_command_buffer_.front());
        recv_command_buffer_.pop_front();
    }
}

void TCPConnection::SendData() {
    size_t num_bytes = 0;
    while (!send_command_buffer_.empty()) {
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
