#include "tcp_connection.h"

TCPConnection::TCPConnection(asio::io_context& io_context, const std::string& ip_address,
    const short port, const bool is_server)
    : tcp_protocol_(0,0),
      endpoint_(asio::ip::make_address(ip_address), port),
      socket_(io_context),
      port_(port),
      client_connected_(false),
      timeout_(io_context),
      stop_cmd_read_(false),
      stop_server_(false),
      received_bytes_(0),
      timer_(io_context),
      io_context_(io_context) {

    // Make sure the decoder is ready for the first packet
    requested_bytes_ = sizeof(TCPProtocol::Header);
    tcp_protocol_.RestartDecoder();

    if (is_server) {
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
        std::cout << "Starting Client on Address [" << ip_address << "] Port [" << port<< "]" << std::endl;
        StartClient();
    }
}

TCPConnection::~TCPConnection() {
    // Stop the server from accepting new connections
    stop_server_.store(true);

    std::cout << "Clearing TCP buffers and closing connections ..." << std::endl;
    send_command_buffer_.clear();
    recv_command_buffer_.clear();

    if (socket_.is_open()) {
        socket_.cancel(); // cancel all pending async operations
        socket_.close();
        std::cout << "Closed server socket" << std::endl;
    }
    std::cout << "Destructed TCP connection" << std::endl;
}

void TCPConnection::StartServer() {
    if (stop_server_.load()) return;
    acceptor_->async_accept(*accept_socket_, [this](std::error_code ec) {
      if (!ec) {
        std::cout << "New client connected!" << std::endl;
          socket_ = std::move(*accept_socket_);
          std::thread(&TCPConnection::ReadData, this).detach();
      } else {
          std::cerr << "Client connection failed with error: " << ec.message() << std::endl;
      }
      if (!stop_server_.load()) StartServer();  // Accept the next client
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
            // Start a thread for reading and writing, these handle ASIO async operations
            // so they dont use a CPU unless data is being processed
            std::thread(&TCPConnection::ReadData, this).detach();
            client_connected_ = true;
        } else {
            std::cerr << "Connection failed: " << ec.message() << std::endl;
        }
    });
}

void TCPConnection::ReadData() {
    // Set timeout to 5 seconds
    constexpr auto read_timeout = std::chrono::seconds(5);

    asio::async_read( socket_,// Or async_read if you need exactly N bytes
    asio::buffer(buffer_, requested_bytes_), // Read up to buffer size
    std::bind(&TCPConnection::ReadHandler, this, std::placeholders::_1, std::placeholders::_2)
    );

    // Timeout in case something happens in the middle of the packet send
    if (packet_read_) {
        timer_.expires_after(read_timeout);
        timer_.async_wait([this](const asio::error_code& ec) {
            if (ec) {
                return; // Timer cancelled, read completed
            }
            packet_read_ = false;
            std::cerr << "Read timeout. Emptying buffer.. \n";
            // Empty the socket buffer if there's any data in it
            asio::error_code ignored_ec;
            size_t bytes_available = socket_.available(ignored_ec);
            if(bytes_available > 0) {
                std::vector<uint8_t> temp_buffer(bytes_available);
                socket_.read_some(asio::buffer(temp_buffer), ignored_ec);
                temp_buffer.clear();
            }
            std::cerr << "Emptied buffer of " << bytes_available << "B" << std::endl;
            tcp_protocol_.RestartDecoder();
        });
    }
}

void TCPConnection::ReadHandler(const asio::error_code& ec, std::size_t bytes_transferred) {

    if (!ec) {
        std::cout << "Read " << bytes_transferred << " bytes successfully.\n";
        // If the requested data was read from the socket we can decode it
        if (bytes_transferred == requested_bytes_) {
            packet_read_ = true;
            // The DecodePackets uses an internal state machine to iterate through the packet eg, Header, Payload, Footer
            // Returns 0 when the end of the packet frame is reached
            { // scope the mutex to just apply to the decode call since it fills the receiver buffer
                std::lock_guard<std::mutex> lock(mutex_);
                requested_bytes_ = tcp_protocol_.DecodePackets(buffer_, recv_command_buffer_);
            }
            if (requested_bytes_ == 0) {
                timer_.cancel();
                packet_read_ = false;
                requested_bytes_ = sizeof(TCPProtocol::Header);
                chrono_read_start_= std::chrono::high_resolution_clock::now();
                cmd_available_.notify_all();
            }
            // 2. Initiate the *next* read operation
            ReadData(); // Loop back to wait for more data
        } else {
            std::cerr << "Requested " << requested_bytes_ << "B  but received " << bytes_transferred << "B" << std::endl;
        }
    } else if (ec == asio::error::eof) {
        std::cerr << "Connection closed by peer (EOF).\n";
        // Handle clean closure
        // close_connection();
    } else {
        std::cerr << "Read Error: " << ec.message() << "\n";
        // Handle error, maybe close socket
        // close_connection();
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

void TCPConnection::WriteSendBuffer(const uint16_t cmd, std::vector<int32_t>& vec) {
    Command cmd_packet(cmd, vec.size());
    cmd_packet.arguments = std::move(vec);
    WriteSendBuffer(cmd_packet);
}

void TCPConnection::WriteSendBuffer(const Command& cmd_struct) {
    std::lock_guard<std::mutex> lock(mutex_);
    send_command_buffer_.emplace_back(cmd_struct);
    // This will call the io_context to execute the sending
    asio::post(io_context_, [this]() { this->SendData(); });
}

void TCPConnection::SendData() {
    // If there is no more data in send buffer return, ie end the current send and wait for
    // new data in the send buffer
    if (!DataInSendBuffer()) return;

    // Construct the TCP packet which will be deserialized and sent.
    TCPProtocol packet(send_command_buffer_.front().command,
                       send_command_buffer_.front().arguments.size()); // cmd, vec.size
    packet.arguments = std::move(send_command_buffer_.front().arguments);
    std::vector<uint8_t> buffer = packet.Serialize();
    std::cout << "Sending Bytes: " << buffer.size() << std::endl;

    async_write(socket_, asio::buffer(buffer), [this](const asio::error_code &ec,
                                                                        const std::size_t &bytes_sent) {
        if (!ec) {
            std::cout << "Sent: " << bytes_sent << "B" << std::endl;
            send_command_buffer_.pop_front();
            SendData();
        } else {
            std::cerr << "Send error: " << ec.message() << "\n";
        }
    });
}
