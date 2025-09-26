//
// Created by Jon Sensenig on 9/15/25.
//
#include "../tcp_connection.h"
#include <iostream>
#include <csignal>

std::atomic<bool> keepRunning(true);
size_t fakeMetricsSentCount = 0;

void signalHandler(int signum) {
    if (keepRunning.load()) {
        std::cout << "\nInterrupt signal (" << signum << ") received. Stopping gracefully...\n";
        std::cout << "\033[36m If client doesn't shutdown now, press Ctrl-C again.. \033[0m" << std::endl;
        keepRunning.store(false);
    } else {
        std::cout << "\nInterrupt signal (" << signum << ") received. Didn't work the 1st time, terminating...\n";
        // Resorting to normal Ctrl-C operation, default SIGINT handler
        std::signal(SIGINT, SIG_DFL);
        // Re-raise the signal so the OS handles it normally
        raise(SIGINT);
    }
}

int main(int argc, char* argv[]) {

    if (argc < 4) {
        std::cerr << "\033[31mPlease include IP address and port!" << std::endl;
        std::cerr << "  Usage: " << argv[0] << " <IP address> <Cmd port> <Monitor port>  red\033[0m" << std::endl;
        return 1;
    }

    std::vector<int32_t> stat_words = {0xFACE, 0xA, 0xBAD, 0xCAFE, 0xDEAD, 0xBEEF, 0xDAD, 0xDEED, 0xFAD, 0xDEAF, 0xBAD};
    int send_period = 1000;
    if (argc > 4) {
        stat_words.clear();
        auto num_status_words = std::stoi(argv[4]);
        std::cout << "\033[31m Included status words, (num words = " << num_status_words << ") \033[0m" << std::endl;
        for (int32_t i = 0; i < num_status_words; ++i) stat_words.push_back(i++);
        // Set the status send period in milliseconds
        send_period = std::stio(argv[5]);
    }

    std::string ip_address = argv[1];
    uint16_t cmd_port = std::stoi(argv[2]);
    uint16_t monitor_port = std::stoi(argv[3]);

    // Register the signal handler
    std::signal(SIGINT, signalHandler);
    std::cout << "Press Ctrl-C once to stop or twice to force stop.. \n";

    auto start = std::chrono::steady_clock::now();

    asio::io_context io_context;
    std::cout << "Starting pGRAMS client..." << std::endl;
    TCPConnection cmd_client(io_context, ip_address, cmd_port, false, true, false);
    TCPConnection monitor_client(io_context, ip_address, monitor_port, false, false, true);
    std::cout << "Starting IO Context..." << std::endl;

    // Guard to keep IO contex from completely before we want to quit
    asio::executor_work_guard<asio::io_context::executor_type> work_guard(io_context.get_executor());
    std::thread io_thread([&]() { io_context.run(); });

    while (keepRunning.load()) {
        // Get the current time, we want to check so we can send fake monitoring data at 1Hz
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

        // If a command is received
        if (!cmd_client.recv_command_buffer_.empty()) {
            // Read the command and print it
            Command cmd = cmd_client.ReadRecvBuffer();
            std::cout << "************** Client Command Link ****************" << std::endl;
            std::cout << std::hex << cmd.command << ": ";
            std::cout << "[";
            for (auto &arg : cmd.arguments) { std::cout << arg << ", "; }
            std::cout << "]" << std::dec << std::endl;
            std::cout << "******************************" << std::endl;

            // Echo back the command
            cmd_client.WriteSendBuffer(cmd);
        }

        if (elapsed > send_period) {
            std::cout << "\033[35m Sending fake monitor data.. (" << fakeMetricsSentCount << ") \033[0m" << std::endl;
            Command cmd(0xB0, 2);
            cmd.arguments = stat_words;
            monitor_client.WriteSendBuffer(cmd);
            start = now; // update the time
            fakeMetricsSentCount++;
        }

        if (!monitor_client.recv_command_buffer_.empty()) {
            // Read the command and print it
            Command cmd = monitor_client.ReadRecvBuffer();
            std::cout << "************ Client Monitor Link ***************" << std::endl;
            std::cout << std::hex << cmd.command << ": ";
            std::cout << "[";
            for (auto &arg : cmd.arguments) { std::cout << arg << ", "; }
            std::cout << "]" << std::dec << std::endl;
            std::cout << "******************************" << std::endl;
        }
    }
    std::cout << "Stopping client!" << std::endl;
    io_context.stop();
    io_thread.join();
}
