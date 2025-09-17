#include "tcp_connection.h"

TCPConnection::TCPConnection(asio::io_context& io_context, const std::string& ip_address,
    const uint16_t port, const bool is_server, const bool use_heartbeat, const bool monitor_link)
    : Command(0,0),
      tcp_protocol_(0,0),
      endpoint_(asio::ip::make_address(ip_address), port),
      socket_(io_context),
      port_(port),
      client_connected_(false),
      timeout_(io_context),
      send_timeout_(io_context),
      stop_cmd_read_(false),
      stop_cmd_write_(false),
      stop_server_(false),
      use_heartbeat_(use_heartbeat),
      is_server_(is_server),
      monitor_link_(monitor_link),
      debug_flag_(false),
      restart_client_(false),
      received_bytes_(0),
      timer_(io_context),
      recv_command_(0,0),
      io_context_(io_context) {

    // Make sure the decoder is ready for the first packet
    requested_bytes_ = sizeof(TCPProtocol::Header);
    tcp_protocol_.RestartDecoder();
    send_command_buffer_.clear();
    recv_command_buffer_.clear();
    recv_command_.arguments.reserve(100); // reserve a vector size so we don't have to keep allocating more memory
    if (is_server_) {
        std::cout << "Starting Server on Address [" << ip_address << "] Port [" << port<< "]" << std::endl;
        std::cout << "PRE Acceptor/accept socket value = " << acceptor_.has_value() << " / " << accept_socket_.has_value() << std::endl;
        acceptor_.emplace(tcp::acceptor(io_context, endpoint_));
        if (!acceptor_.has_value()) {
            std::cerr << "Acceptor is not initialized!" << std::endl;
        }
        accept_socket_.emplace(io_context);
        if (!accept_socket_.has_value()) {
            std::cerr << "Accept Socket is not initialized!" << std::endl;
        }
        std::cout << "Acceptor/accept socket value = " << acceptor_.has_value() << " / " << accept_socket_.has_value() << std::endl;
        StartServer();
    }
    else {
        std::cout << "Starting Receive Client on Address [" << ip_address << "] Port [" << port << "]" << std::endl;
        StartClient();
    }
}

TCPConnection::~TCPConnection() {
    // Stop the server from accepting new connections
    send_cmd_available_.notify_one();
    stop_server_.store(true);
    stop_cmd_write_.store(true);

    if (write_data_thread_.joinable()) write_data_thread_.join();
    if (socket_.is_open()) {
        socket_.cancel(); // cancel all pending async operations
        socket_.close();
        std::cout << "Closed TCP socket" << std::endl;
    }

    std::cout << "Clearing TCP buffers and closing connections ..." << std::endl;
    send_command_buffer_.clear();
    recv_command_buffer_.clear();
    // if (read_data_thread_.joinable()) read_data_thread_.join();

    std::cout << "Destructed TCP connection" << std::endl;
}

void TCPConnection::StartServer() {
    if (stop_server_.load()) {
        stop_cmd_write_.store(true);
        return;
    }
    acceptor_->async_accept(*accept_socket_, [this](std::error_code ec) {
      if (!ec) {
        std::cout << "New client connected!" << std::endl;
          socket_ = std::move(*accept_socket_);
          tcp_protocol_.RestartDecoder();
          requested_bytes_ = sizeof(TCPProtocol::Header);
          requested_bytes_ = sizeof(TCPProtocol::Header);
          std::thread(&TCPConnection::ReadData, this).detach();
          std::thread(&TCPConnection::SendData, this).detach();
      } else {
          std::cerr << "Client connection failed with error: " << ec.message() << std::endl;
      }
      if (!stop_server_.load()) StartServer();  // Accept the next client
    });
}

void TCPConnection::StartClient() {
    if (stop_server_.load()) return;

    if (socket_.is_open()) {
        std::cout << "Empty and Cancel socket operations" << std::endl;
        ClearSocketBuffer();
        std::unique_lock<std::mutex> slock(sock_mutex_);
        socket_.cancel();
        socket_.close();
        slock.unlock();
        std::cout << "Socket cancelled and closed" << std::endl;
    }
    if (write_data_thread_.joinable()) {
        stop_cmd_write_.store(true);
        send_cmd_available_.notify_one();
        std::cout << "Joining write thread before restarting" << std::endl;
        write_data_thread_.join();
        stop_cmd_write_.store(false);
    }
    timer_.cancel();
    client_connected_ = false;
    tcp_protocol_.RestartDecoder();
    requested_bytes_ = sizeof(TCPProtocol::Header);

    // Set up a timeout (e.g., 3 seconds)
    timeout_.expires_after(std::chrono::seconds(3));
    timeout_.async_wait([this](const asio::error_code& ec) {
        if (!ec) { // Timeout expired
            std::cerr << "Connection timed out. [" << port_ << "]" << std::endl;
            // Wait for 2s before trying again
            std::this_thread::sleep_for(std::chrono::seconds(2));
            StartClient();
        }
    });
    std::cout << "--> Async_connect" << std::endl;
    // Receive command socket
    socket_.async_connect(endpoint_, [this](const asio::error_code& ec) {
        if (!ec) {
            timeout_.cancel();
            std::cout << "Receive socket connected to server! [" << port_ << "]" << std::endl;
            // Start a thread for reading and writing, these handle ASIO async operations
            // so they don't use a CPU unless data is being processed
            tcp_protocol_.RestartDecoder();
            restart_client_ = false;
            requested_bytes_ = sizeof(TCPProtocol::Header);
            ReadData(); // move this to an ASIO event driven operation instead of a thread
            write_data_thread_ = std::thread(&TCPConnection::SendData, this);
            client_connected_ = true;
        } else {
            std::cerr << "Receive socket connection failed: " << ec.message() << " [" << port_ << "]" << std::endl;
            if (write_data_thread_.joinable()) {
                stop_cmd_write_.store(true);
                send_cmd_available_.notify_one();
                std::cout << "Joining write thread before restarting" << std::endl;
                write_data_thread_.join();
                stop_cmd_write_.store(false);
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            StartClient();
        }
    });
}

void TCPConnection::ClearSocketBuffer() {
    asio::error_code ignored_ec;
    std::unique_lock<std::mutex> slock(sock_mutex_);
    const size_t bytes_available = socket_.available(ignored_ec);
    if (bytes_available > 0) {
        std::vector<uint8_t> temp_buffer(bytes_available);
        socket_.read_some(asio::buffer(temp_buffer), ignored_ec);
        temp_buffer.clear();
    }
    slock.unlock();
    std::cerr << "Emptied buffer of " << bytes_available << "B" << std::endl;
}

void TCPConnection::ReadData() {
    // Set timeout to 1 seconds, the heartbeat
    if (stop_server_.load()) return;
    if (!is_server_ && restart_client_.load()) StartClient();
    constexpr auto read_timeout = std::chrono::milliseconds(1500); // give 0.5s grace period

    if (debug_flag_) std::cout << "Setting read to " << requested_bytes_ << "B " << std::endl;
    if (debug_flag_) std::cout << "io_context_.stopped(): " << io_context_.stopped() << std::endl;

    asio::async_read( socket_,// Or async_read if you need exactly N bytes
                      asio::buffer(buffer_, requested_bytes_), // Read up to buffer size
                std::bind(&TCPConnection::ReadHandler, this, std::placeholders::_1, std::placeholders::_2)
    );

    // Timeout in case something happens in the middle of the packet read
    if (!is_server_ && client_connected_ && (packet_read_ || use_heartbeat_) && !monitor_link_) {
        timer_.expires_after(read_timeout);
        timer_.async_wait([this](const asio::error_code& ec) {
            // operation_aborted is the timer cancel. If success that means the timer completed
            if (ec == asio::error::operation_aborted) {
                return; // Timer cancelled, read completed successfully
            }
            packet_read_ = false;
            std::cerr << "Read timeout or no heartbeat received. Emptying buffer..  EC=" << ec.message() << std::endl;
            // Empty the socket buffer if there's any data in it
            ClearSocketBuffer();
            client_connected_ = false;
            restart_client_.store(true);
            std::cout << "Will try reconnecting...  [" << port_ << "]" << std::endl;
            // FIXME handle timeout
            socket_.cancel();
            StartClient();
        });
    }
}

void TCPConnection::ReadHandler(const asio::error_code& ec, std::size_t bytes_transferred) {
    // std::cout << "[" << port_ << "] RecvB: " << requested_bytes_ << "/" << bytes_transferred << std::endl;
    if (debug_flag_) for (size_t i = 0; i < bytes_transferred; i++) std::cout << std::hex << static_cast<int>(buffer_[i]) << ", ";
    if (debug_flag_) std::cout << std::dec << std::endl;
    if (!ec) {
        // If the requested data was read from the socket we can decode it
        if (bytes_transferred == requested_bytes_) {
            packet_read_ = true;
            // The DecodePackets uses an internal state machine to iterate through the packet eg, Header, Payload, Footer
            // Returns 0 when the end of the packet frame is reached
            { // scope the mutex to just apply to the decode call since it fills the receiver buffer
                std::lock_guard<std::mutex> lock(recv_mutex_);
                requested_bytes_ = tcp_protocol_.DecodePackets(buffer_, recv_command_);
            }
            if (requested_bytes_ == SIZE_MAX) { // end of packet

                if (debug_flag_) std::cout << "Cancelling timer, expiry: " << std::endl;

                if (use_heartbeat_ && recv_command_.command == TCPProtocol::kHeartBeat) {
                    timer_.expires_after(std::chrono::seconds(1));
                } else {
                    // Full packet received so place into the queue and notify consumers
                    recv_command_buffer_.emplace_back(recv_command_);
                    cmd_available_.notify_all();
                }

                packet_read_ = false;
                requested_bytes_ = sizeof(TCPProtocol::Header);
                chrono_read_start_= std::chrono::high_resolution_clock::now();

                // Send an ack back after receiving a message, for client, command link only
                if (!is_server_ && !monitor_link_ && !recv_command_buffer_.empty()) {
                    WriteSendBuffer(recv_command_buffer_.back());
                }
            }
            // 2. Initiate the *next* read operation
            ReadData(); // Loop back to wait for more data
        } else {
            std::cerr << "Requested " << requested_bytes_ << "B  but received " << bytes_transferred << "B"
                      << " Starting over" << std::endl;
            // Remove the last element if there is a half-formed command
            if (!recv_command_buffer_.empty()) recv_command_buffer_.pop_back();
            ClearSocketBuffer();
            tcp_protocol_.RestartDecoder();
            requested_bytes_ = sizeof(TCPProtocol::Header);
            ReadData(); // Loop back to wait for more data
        }
    } else if (ec == asio::error::eof) {
        std::cerr << "Connection closed by peer (EOF).\n";
        if (client_connected_) {
            restart_client_.store(true);
            socket_.cancel();
            StartClient();
        }
    } else {
        std::cerr << "Read Error: " << ec.message() << "\n";
        if (client_connected_) {
            restart_client_.store(true);
            socket_.cancel();
            StartClient();
        }
    }
}

bool TCPConnection::DataInSendBuffer() {
    std::lock_guard<std::mutex> lock(send_mutex_);
    return !send_command_buffer_.empty();
}

bool TCPConnection::DataInRecvBuffer() {
    std::lock_guard<std::mutex> lock(recv_mutex_);
    return !recv_command_buffer_.empty();
}

Command TCPConnection::ReadRecvBuffer() {
    std::unique_lock cmd_lock(recv_mutex_);
    cmd_available_.wait(cmd_lock, [this] {
        return !recv_command_buffer_.empty() || stop_cmd_read_;
    });
    if (stop_cmd_read_) return {0, 0};
    Command command = recv_command_buffer_.front();
    recv_command_buffer_.pop_front();
    return command;
}

std::vector<Command> TCPConnection::ReadRecvBuffer(size_t num_cmds) {
    std::unique_lock lock(recv_mutex_);
    size_t buffer_size = recv_command_buffer_.size();
    lock.unlock();

    size_t num_reads = num_cmds;
    if (num_cmds > buffer_size) {
        num_reads = buffer_size;
        if (debug_flag_) std::cout << "Requested commands larger than buffer size, reading entire buffer: "
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
        std::lock_guard<std::mutex> lock(recv_mutex_);
        recv_command_buffer_.pop_front();
    }
}

void TCPConnection::WriteSendBuffer(const uint16_t cmd, std::vector<int32_t>& vec) {
    Command cmd_packet(cmd, vec.size());
    if (!vec.empty()) cmd_packet.arguments = std::move(vec);
    WriteSendBuffer(cmd_packet);
}

void TCPConnection::WriteSendBuffer(const Command& cmd_struct) {
    std::unique_lock<std::mutex> lock(send_mutex_);
    send_command_buffer_.emplace_back(cmd_struct);
    lock.unlock();
    if (debug_flag_) std::cout << "Send cmd: " << cmd_struct.command << "/" << cmd_struct.arguments.size() << std::endl;
    send_cmd_available_.notify_one();
    // This will call the io_context to execute the sending
    // auto self(shared_from_this());
    // asio::post(io_context_, [this]() { this->SendData(); });
}

// void TCPConnection::SendData() {
//     if (debug_flag_) std::cout << stop_cmd_write_.load() << "/" << stop_server_.load()  << std::endl;
//     if (stop_server_.load() || stop_cmd_write_.load()) return;
//     if (restart_client_.load()) StartClient();
//
//     std::unique_lock<std::mutex> lock(send_mutex_);
//     if (send_command_buffer_.empty()) return;
//     // send_cmd_available_.wait(lock, [this] { // FIXME add self
//     // return !send_command_buffer_.empty() || stop_cmd_write_.load() || stop_server_.load();
//     // });
//     if (stop_cmd_write_.load() || stop_server_.load()) return; // just an extra catch
//
//     // Construct the TCP packet which will be deserialized and sent.
//     TCPProtocol packet(send_command_buffer_.front().command,
//                        send_command_buffer_.front().arguments.size()); // cmd, vec.size
//     packet.arguments = std::move(send_command_buffer_.front().arguments);
//     std::vector<uint8_t> buffer = packet.Serialize();
//     send_command_buffer_.pop_front();
//     lock.unlock();
//
//     async_write(socket_, asio::buffer(buffer),
//         std::bind(&TCPConnection::SendHandler, this, std::placeholders::_1, std::placeholders::_2));
//
//     std::cout << "Exit SendData" << std::endl;
// }
//
// void TCPConnection::SendHandler(const asio::error_code& ec, const std::size_t &bytes_sent) {
//     if (!ec) {
//         if (debug_flag_) std::cout << "Sent: " << bytes_sent << "B" << std::endl;
//         SendData();
//     } else if (ec == asio::error::eof) {
//         std::cerr << "Connection closed by peer (EOF).\n";
//         if (client_connected_) restart_client_.store(true);
//         socket_.cancel();
//         StartClient();
//     } else {
//         std::cerr << "Write Error: " << ec.message() << "\n";
//         if (client_connected_) restart_client_.store(true);
//         socket_.cancel();
//         StartClient();
//     }
// }

// Current function 09/16
void TCPConnection::SendData() {
    if (debug_flag_) std::cout << stop_cmd_write_.load() << "/" << stop_server_.load()  << std::endl;
    while (!stop_server_.load() && !stop_cmd_write_.load()) {
        std::unique_lock<std::mutex> lock(send_mutex_);

        send_cmd_available_.wait(lock, [this] { // FIXME add self
        return !send_command_buffer_.empty() || stop_cmd_write_.load() || stop_server_.load();
        });
        if (stop_cmd_write_.load() || stop_server_.load()) break; // just an extra catch

        // Construct the TCP packet which will be deserialized and sent.
        TCPProtocol packet(send_command_buffer_.front().command,
                           send_command_buffer_.front().arguments.size()); // cmd, vec.size
        packet.arguments = std::move(send_command_buffer_.front().arguments);
        std::vector<uint8_t> buffer = packet.Serialize();
        send_command_buffer_.pop_front();
        lock.unlock();

        async_write(socket_, asio::buffer(buffer), [this](const asio::error_code &ec,
                                                                            const std::size_t &bytes_sent) {
            if (!ec) {
                if (debug_flag_) std::cout << "Sent: " << bytes_sent << "B" << std::endl;
            } else {
                std::cerr << "Send error: " << ec.message() << "\n";
                // if (client_connected_) restart_client_.store(true); //FIXME add something here
            }
        });
        // slock.unlock();
    }
    std::cout << "Exit SendData" << std::endl;
}
