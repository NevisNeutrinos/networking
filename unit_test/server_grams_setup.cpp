//
// Created by Jon Sensenig on 9/15/25.
//

#include "../tcp_connection.h"
#include <iostream>

std::atomic<bool> keepRunning(true);
size_t fakeMetricsRecvCount = 0;

void PrintState() {
    std::cout << "Select a command:\n";
    std::cout << "  [0] Send Cmd\n";
    std::cout << "  [1] Read Cmd\n";
    std::cout << "  [2] Read All Cmd\n";
    std::cout << "  [-1] Exit\n";
    std::cout << "Enter choice: ";
}

int GetUserInput() {
    int choice;
    std::cin >> choice;

    // Handle invalid input (non-numeric)
    if (std::cin.fail()) {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Invalid input. Please enter a number.\n";
        return GetUserInput();  // Retry
    }
    return choice;
}

std::vector<int32_t> GetUserInputList() {
    std::vector<int32_t> numbers;
    std::string line;
    std::cout << "Enter numbers in one line: ";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::getline(std::cin, line);

    std::stringstream ss(line);
    int num;
    while (ss >> num) {
        numbers.push_back(num);
    }

    return numbers;
}

void SendHeartbeat(TCPConnection &server) {
    auto start = std::chrono::steady_clock::now();
    while (keepRunning.load()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed > 1000) {
            server.WriteSendBuffer(Command{TCPProtocol::kHeartBeat, 0});
            start = now;
        }
    }
}

void MonitorServer(TCPConnection &cmd_server, TCPConnection &monitor_server) {
    while (keepRunning.load()) {
        if (!monitor_server.recv_command_buffer_.empty()) {
            // If monitoring data is received, read the command and print it
            Command cmd = monitor_server.ReadRecvBuffer();
            std::cout << "************** Server Monitor Link ****************" << std::endl;
            std::cout << std::hex << cmd.command << ": ";
            std::cout << "[";
            for (auto &arg : cmd.arguments) { std::cout << arg << ", "; }
            std::cout << "]" << std::dec << std::endl;
            std::cout << "******************************" << std::endl;

            // Echo back the number of bytes received
            int32_t num_bytes = cmd.arguments.size() * sizeof(int32_t);
            Command reply(0x5, 1);
            reply.arguments = {num_bytes};
            monitor_server.WriteSendBuffer(reply);
            if (cmd.command == 0xB0) {
                std::cout << "\033[35m Received Fake Metrics Count=" << fakeMetricsRecvCount << "\033[0m" << std::endl;
                fakeMetricsRecvCount++;
            }
        }

        if (!cmd_server.recv_command_buffer_.empty()) {
            // If monitoring data is received, read the command and print it
            Command cmd = cmd_server.ReadRecvBuffer();
            std::cout << "************** Server Command Link ****************" << std::endl;
            std::cout << std::hex << cmd.command << ": ";
            std::cout << "[";
            for (auto &arg : cmd.arguments) { std::cout << arg << ", "; }
            std::cout << "]" << std::dec << std::endl;
            std::cout << "******************************" << std::endl;
            // No reply sent for command link
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(int argc, char* argv[]) {

    if (argc < 4) {
        std::cerr << "\033[31mPlease include IP address and port!" << std::endl;
        std::cerr << "  Usage: " << argv[0] << " <IP address> <Cmd port> <Monitor port>  red\033[0m" << std::endl;
        return 1;
    }

    std::string ip_address = argv[1];
    uint16_t cmd_port = std::stoi(argv[2]);
    uint16_t monitor_port = std::stoi(argv[3]);


    asio::io_context io_context;
    std::cout << "Starting pGRAMS server..." << std::endl;
    TCPConnection cmd_server(io_context, ip_address, cmd_port, true, false, false);
    TCPConnection monitor_server(io_context, ip_address, monitor_port, true, false, true);
    std::cout << "Starting IO Context..." << std::endl;

    // Guard to keep IO contex from completely before we want to quit
    asio::executor_work_guard<asio::io_context::executor_type> work_guard(io_context.get_executor());
    std::thread io_thread([&]() { io_context.run(); });
    std::thread heartbeat_thread( [&]{SendHeartbeat(cmd_server); } );
    std::thread monitor_thread( [&]{ MonitorServer(cmd_server, monitor_server); } );

    while (keepRunning.load()) {
        PrintState();
        int input = GetUserInput();

        if (input == -1) {
            std::cout << "Exiting Server...\n";
            keepRunning.store(false);
            break;
        }

        std::cout << "Enter Cmd:  ";
        auto cmd = static_cast<uint16_t>(GetUserInput());
        std::cout << "Enter Arg: \n";
        std::vector<int> args = GetUserInputList();
        std::cout << "Sending command" << std::endl;
        cmd_server.WriteSendBuffer(cmd, args);
    }
    heartbeat_thread.join();
    monitor_thread.join();
    io_context.stop();
    io_thread.join();
}