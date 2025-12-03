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
      recv_command_(0,0) {

    // Make sure the decoder is ready for the first packet
    requested_bytes_ = sizeof(TCPProtocol::Header);
    tcp_protocol_.RestartDecoder();
    send_command_buffer_.clear();
    recv_command_buffer_.clear();
    recv_command_.arguments.reserve(100); // reserve a vector size so we don't have to keep allocating more memory
    if (is_server_) {
        std::cout << "Starting Server on Address [" << ip_address << "] Port [" << port<< "]" << std::endl;
        if (debug_flag_) std::cout << "PRE Acceptor/accept socket value = " << acceptor_.has_value() << " / "
                                    << accept_socket_.has_value() << std::endl;
        acceptor_.emplace(tcp::acceptor(io_context, endpoint_));
        if (!acceptor_.has_value()) {
            std::cerr << "Acceptor is not initialized!" << std::endl;
        }
        accept_socket_.emplace(io_context);
        if (!accept_socket_.has_value()) {
            std::cerr << "Accept Socket is not initialized!" << std::endl;
        }
        if (debug_flag_) std::cout << "Acceptor/accept socket value = " << acceptor_.has_value() << " / "
                                    << accept_socket_.has_value() << std::endl;
        StartServer();
    }
    else {
        std::cout << "Starting Receive Client on Address [" << ip_address << "] Port [" << port << "]" << std::endl;
        if (use_heartbeat) reset_read_timer_ = true;
        start_ = std::chrono::steady_clock::now();
        StartClient();
    }
}

TCPConnection::~TCPConnection() {
    // Stop the server from accepting new connections
    send_command_buffer_.clear();
    recv_command_buffer_.clear();

    send_cmd_available_.notify_one();
    stop_server_.store(true);
    stop_cmd_write_.store(true);

    if (write_data_thread_.joinable()) write_data_thread_.join();
    if (debug_flag_) std::cout << "Clearing TCP buffers and closing connections ..." << std::endl;

    if (socket_.is_open()) {
        socket_.cancel(); // cancel all pending async operations
        socket_.close();
        if (debug_flag_) std::cout << "Closed TCP socket" << std::endl;
    }

    std::cout << "Destructed TCP connection [" << port_ << "]" << std::endl;
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
          if (use_heartbeat_) std::thread(&TCPConnection::SendHeartbeat, this).detach();
      } else {
          if (debug_flag_) std::cout << "Client connection failed with error: " << ec.message() << std::endl;
      }
      if (!stop_server_.load()) StartServer();  // Accept the next client
    });
}

void TCPConnection::StartClient() {
    if (stop_server_.load()) return;
    if (socket_.is_open()) {
        if (debug_flag_) std::cout << "Empty and Cancel socket operations" << std::endl;
        std::unique_lock<std::mutex> slock(sock_mutex_);
        socket_.cancel();
        slock.unlock();
        ClearSocketBuffer();
        slock.lock();
        socket_.close();
        slock.unlock();
        if (debug_flag_) std::cout << "Socket cancelled and closed" << std::endl;
    }
    if (write_data_thread_.joinable()) {
        stop_cmd_write_.store(true);
        send_cmd_available_.notify_one();
        if (debug_flag_) std::cout << "Joining write thread before restarting" << std::endl;
        write_data_thread_.join();
        stop_cmd_write_.store(false);
    }
    timer_.cancel();
    client_connected_ = false;
    tcp_protocol_.RestartDecoder();
    requested_bytes_ = sizeof(TCPProtocol::Header);
    send_command_buffer_.clear();
    received_bytes_ = 0;

    if (debug_flag_) std::cout << "--> Async_connect" << std::endl;
    // Receive command socket
    socket_.async_connect(endpoint_, [this](const asio::error_code& ec) {
        if (!ec) {
            std::cout << "Receive socket connected to server! [" << port_ << "]" << " 0FD: " << socket_.native_handle() << std::endl;
            timeout_.cancel();
            // Set send buffer size
            asio::socket_base::send_buffer_size option_send(1 * 1024);
            socket_.set_option(option_send);
            // Start a thread for reading and writing, these handle ASIO async operations
            // so they don't use a CPU unless data is being processed
            tcp_protocol_.RestartDecoder();
            restart_client_ = false;
            requested_bytes_ = monitor_link_ ? 0xFFFF : sizeof(TCPProtocol::Header); // large read for status link
            ReadData(); // move this to an ASIO event driven operation instead of a thread
            write_data_thread_ = std::thread(&TCPConnection::SendData, this);
            client_connected_ = true;
        } else {
            std::cerr << "Receive socket connection failed: " << ec.message() << " [" << port_ << "]" << std::endl;
            if (write_data_thread_.joinable()) {
                stop_cmd_write_.store(true);
                send_cmd_available_.notify_one();
                if (debug_flag_) std::cout << "Joining write thread before restarting" << std::endl;
                write_data_thread_.join();
                stop_cmd_write_.store(false);
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            StartClient();
        }
    });

    // Set up a timeout (e.g., 3 seconds)
    timeout_.expires_after(std::chrono::seconds(5));
    timeout_.async_wait([this](const asio::error_code& ec) {
        if (ec == asio::error::operation_aborted) {
            return; // Timer cancelled, connection completed successfully
        }
        // Timeout expired
        if (debug_flag_) std::cout << "Connection timed out. [" << port_ << "]" << std::endl;
        // Wait for 2s before trying again
        std::this_thread::sleep_for(std::chrono::seconds(2));
        // StartClient();
    });
}

void TCPConnection::ClearSocketBuffer() {
    asio::error_code ignored_ec;
    // FIXME add return if !socket.is_open()
    if (!socket_.is_open()) return;
    std::unique_lock<std::mutex> slock(sock_mutex_);
    const size_t bytes_available = socket_.available(ignored_ec);
    if (bytes_available > 0) {
        std::vector<uint8_t> temp_buffer(bytes_available);
        socket_.read_some(asio::buffer(temp_buffer), ignored_ec);
        temp_buffer.clear();
    }
    slock.unlock();
    if (debug_flag_) std::cout << "Emptied buffer of " << bytes_available << "B" << std::endl;
}

void TCPConnection::ReadData() {
    // Set timeout to 1 seconds, the heartbeat
    if (stop_server_.load()) return;
    if (!is_server_ && restart_client_.load()) StartClient();
    constexpr auto read_timeout = std::chrono::milliseconds(1500); // give 0.5s grace period

    if (debug_flag_) std::cout << "Setting read to " << requested_bytes_ << "B " << std::endl;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
    asio::async_read( socket_,// Or async_read if you need exactly N bytes
                      asio::buffer(buffer_, requested_bytes_), // Read up to buffer size
                std::bind(&TCPConnection::ReadHandler, this, std::placeholders::_1, std::placeholders::_2)
    );

    // Timeout in case something happens in the middle of the packet read
    if (!is_server_ && client_connected_ && (packet_read_ || use_heartbeat_) && !monitor_link_) {
        if (debug_flag_) std::cout << "Resetting read timer [" << reset_read_timer_ << "]" << " (" << elapsed << ")" << std::endl;
        timer_.expires_after(read_timeout);
        start_ = now;
        timer_.async_wait([this](const asio::error_code& ec) {
            // operation_aborted is the timer cancel. If success that means the timer completed
            if (ec == asio::error::operation_aborted) {
                return; // Timer cancelled, read completed successfully
            }
            packet_read_ = false;
            // if (debug_flag_)
            std::cout << "Read timeout or no heartbeat received. Emptying buffer..  EC=" << ec.message() << std::endl;
            // Empty the socket buffer if there's any data in it
            ClearSocketBuffer();
            client_connected_ = false;
            restart_client_.store(true);
            if (debug_flag_) std::cout << "Will try reconnecting...  [" << port_ << "]" << std::endl;
            if (socket_.is_open()) socket_.cancel();
            StartClient();
        });
    }
}

void TCPConnection::ReadHandler(const asio::error_code& ec, std::size_t bytes_transferred) {
    if (debug_flag_) for (size_t i = 0; i < bytes_transferred; i++) std::cout << std::hex << static_cast<int>(buffer_[i]) << ", ";
    if (debug_flag_) std::cout << std::dec << std::endl;

    reset_read_timer_ = false;
    if (!ec) {
        // If the requested data was read from the socket we can decode it
        if (bytes_transferred == requested_bytes_) {
            packet_read_ = true;
            received_bytes_ += bytes_transferred;
            // The DecodePackets uses an internal state machine to iterate through the packet eg, Header, Payload, Footer
            // Returns 0 when the end of the packet frame is reached
            { // scope the mutex to just apply to the decode call since it fills the receiver buffer
                std::lock_guard<std::mutex> lock(recv_mutex_);
                requested_bytes_ = tcp_protocol_.DecodePackets(buffer_, recv_command_);
            }
            // Will keep getting kCorruptData until a good start code is found
            if (requested_bytes_ == TCPProtocol::kCorruptData) { // 0xFFFFFFFF
                timer_.cancel(); // cancel the wait since we are receiving data just corrupted
                requested_bytes_ = sizeof(TCPProtocol::Header);
                tcp_protocol_.RestartDecoder(); // make sure to set the state machine to expect a header
                received_bytes_ = 0;
            } else if (requested_bytes_ == SIZE_MAX) { // end of good packet
                if (debug_flag_) std::cout << "Cancelling timer, expiry: " << std::endl;
                timer_.cancel(); // anything we receive should count as a heartbeat
                if (use_heartbeat_ && recv_command_.command == TCPProtocol::kHeartBeat) {
                    // if (debug_flag_)
                    // std::cout << "Heartbeat received.." << std::endl;
                    // timer_.expires_after(std::chrono::seconds(1));
                    reset_read_timer_ = true;
                    WriteRecvBuffer(recv_command_);
                } else {
                    // Full packet received so place into the queue and notify consumers
                    WriteRecvBuffer(recv_command_);
                    cmd_available_.notify_all();
                }

                packet_read_ = false;
                requested_bytes_ = sizeof(TCPProtocol::Header);
                chrono_read_start_= std::chrono::high_resolution_clock::now();

                // Send an ack back after receiving a message, for client, command link only
                // per specification the ack should be the command + num received bytes
                if (!is_server_ && !monitor_link_ && DataInRecvBuffer()) {
                    std::vector<int32_t> data = { static_cast<int32_t>(received_bytes_) };
                    {
                        std::lock_guard<std::mutex> lock(recv_mutex_);
                        WriteSendBuffer(recv_command_buffer_.back().command, data);
                        if (TCPProtocol::kHeartBeat == recv_command_buffer_.back().command) recv_command_buffer_.pop_back();
                    }
                }
                received_bytes_ = 0;
            }
            // 2. Initiate the *next* read operation
            ReadData(); // Loop back to wait for more data
            // TODO check if the returned bytes is 0 for Status link, indicating a broken link
        // } else if (monitor_link_ && requested_bytes_ == 0) { ;
        } else { // did not receive requested bytes
            if (debug_flag_) std::cout << "Requested " << requested_bytes_ << "B  but received " << bytes_transferred << "B"
                      << " Starting over" << std::endl;
            // Remove the last element if there is an incomplete packet
            if (DataInRecvBuffer()) {
                {
                    std::lock_guard<std::mutex> lock(recv_mutex_);
                    recv_command_buffer_.pop_back();
                }
            }
            received_bytes_ = 0;
            // Never should receive data for status link, should end with 0B read if lost conenction
            if (monitor_link_ && requested_bytes_ == 0) {
                restart_client_.store(true);
                if (socket_.is_open()) socket_.cancel();
                StartClient();
            }
            ClearSocketBuffer();
            tcp_protocol_.RestartDecoder();
            requested_bytes_ = monitor_link_ ? 0xFFFF : sizeof(TCPProtocol::Header);
            ReadData(); // Loop back to wait for more data
        }
    } else if (ec == asio::error::eof) {
        if (debug_flag_) std::cout << "Connection closed by peer (EOF).\n";
        if (client_connected_ && !stop_server_.load() && !stop_cmd_write_.load()) {
            restart_client_.store(true);
            if (socket_.is_open()) socket_.cancel();
            received_bytes_ = 0;
            StartClient();
        }
    } else {
        if (debug_flag_) std::cout << "Read Error: " << ec.message() << " client_connected " << client_connected_ << "\n";
        if (client_connected_ && !stop_server_.load() && !stop_cmd_write_.load()) {
            restart_client_.store(true);
            if (socket_.is_open()) socket_.cancel();
            received_bytes_ = 0;
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
        auto cmd = ReadRecvBuffer();
        // This shouldn't happen but if a heartbeat were to make it here we want to drop it
        if(cmd.command == TCPProtocol::kHeartBeat) continue;
        commands.push_back(cmd);
    }
    return commands;
}

void TCPConnection::EchoData() {
    if (debug_flag_) std::cout << "EchoData!" << std::endl;
    while (DataInRecvBuffer()) {
        WriteSendBuffer(ReadRecvBuffer());
        std::lock_guard<std::mutex> lock(recv_mutex_);
        recv_command_buffer_.pop_front();
    }
}

void TCPConnection::SendHeartbeat() {
    auto heartbeat = Command(TCPProtocol::kHeartBeat, 0);
    while (!stop_server_.load() && !stop_cmd_write_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        WriteSendBuffer(heartbeat);
    }
    if (debug_flag_) std::cout << "Ending hearbeat.." << std::endl;
}

void TCPConnection::WriteSendBuffer(const uint16_t cmd, std::vector<int32_t>& vec) {
    Command cmd_packet(cmd, vec.size());
    if (!vec.empty()) cmd_packet.arguments = std::move(vec);
    WriteSendBuffer(cmd_packet);
}

void TCPConnection::WriteSendBuffer(const Command& cmd_struct) {
    if(!is_server_ && !client_connected_) {
        std::cout << "Client not connected, dropping message" << std::endl;
    } else {
        std::unique_lock<std::mutex> lock(send_mutex_);
        send_command_buffer_.emplace_back(cmd_struct);
        lock.unlock();
        send_cmd_available_.notify_one();
    }
    if (debug_flag_) std::cout << "Send cmd: " << cmd_struct.command << "/" << cmd_struct.arguments.size() << std::endl;
}

void TCPConnection::WriteRecvBuffer(const Command& cmd_struct) {
    std::lock_guard<std::mutex> lock(recv_mutex_);
    recv_command_buffer_.emplace_back(cmd_struct);
}

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
        asio::socket_base::send_buffer_size option_send;
        socket_.get_option(option_send);
        int send_buffer_size = option_send.value();
        async_write(socket_, asio::buffer(buffer), [this](const asio::error_code &ec,
                                                                            const std::size_t &bytes_sent) {
            if (!ec) {
                // if (debug_flag_)
                // std::cout << "Sent: " << bytes_sent << "B" << std::endl;
            } else {
                // if (debug_flag_)
                std::cerr << "Send error: " << ec.message() << "\n";
                send_command_buffer_.clear();
                // if (client_connected_) restart_client_.store(true); //FIXME add something here
            }
        });
        // slock.unlock();
    }
    if (debug_flag_) std::cout << "Exit SendData" << std::endl;
}

Command TCPConnection::DecodeRawPacket(std::vector<uint8_t>& raw_buff) {
    Command cmd_buffer(0,0);
    size_t buf_idx = 0;

    // Header
    requested_bytes_ = TCPProtocol::header_size_;
    for (size_t i = 0; i < requested_bytes_; i++) {
        buffer_.at(buf_idx) = raw_buff.at(buf_idx);
        buf_idx++;
    }
    requested_bytes_ = tcp_protocol_.DecodePackets(buffer_, cmd_buffer);
    // Arguments
    for (size_t i = 0; i < requested_bytes_; i++) {
        buffer_.at(buf_idx) = raw_buff.at(buf_idx);
        buf_idx++;
    }
    requested_bytes_ = tcp_protocol_.DecodePackets(buffer_, cmd_buffer);
    // Footer
    for (size_t i = 0; i < requested_bytes_; i++) {
        buffer_.at(buf_idx) = raw_buff.at(buf_idx);
        buf_idx++;
    }
    requested_bytes_ = tcp_protocol_.DecodePackets(buffer_, cmd_buffer);
    // Reset decoder state machine
    tcp_protocol_.RestartDecoder();
    if (requested_bytes_ != SIZE_MAX) {
        throw std::runtime_error("Invalid requested bytes received");
    }
    return cmd_buffer;
}
