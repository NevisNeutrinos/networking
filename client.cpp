//
// Created by Jon Sensenig on 2/10/25.
//

#include "tcp_connection.h"
#include <iostream>


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

int main(int argc, char* argv[]) {

    if (argc < 3) {
        std::cerr << "Please include IP address and port!" << std::endl;
        std::cerr << "Usage: " << argv[0] << " <IP address> <port>\n";
        return 1;
    }

    std::string ip_address = argv[1];
    uint16_t port = std::stoi(argv[2]);

    try {
        asio::io_context io_context;
        std::cout << "Starting client..." << std::endl;
        TCPConnection client(io_context, ip_address, port, false, false, false);
        std::cout << "Starting IO Context..." << std::endl;

        // Guard to keep IO contex from completely before we want to quit
        asio::executor_work_guard<asio::io_context::executor_type> work_guard(io_context.get_executor());
        std::thread io_thread([&]() { io_context.run(); });

        while (client.getSocketIsOpen()) {
            PrintState();
            int input = GetUserInput();

            if (input == -1) {
                std::cout << "Exiting...\n";
                break;
            }

            switch (input) {
                case 0: {
                    std::cout << "Enter Cmd:  ";
                    auto cmd = static_cast<uint16_t>(GetUserInput());
                    std::cout << "Enter Arg: \n";
                    std::vector<int> args = GetUserInputList();
                    std::cout << "Sending command" << std::endl;
                    client.WriteSendBuffer(cmd, args);
                    break;
                }
                case 1: {
                    std::cout << "Receiving command" << std::endl;
                    Command cmd = client.ReadRecvBuffer();
                    std::cout << "******************************" << std::endl;
                    std::cout << "Command: " << cmd.command << std::endl;
                    for (auto &arg : cmd.arguments) {std::cout << arg << std::endl;}
                    std::cout << "******************************" << std::endl;
                    break;
                }
                case 2: {
                    std::cout << "Receiving All command" << std::endl;
                    std::vector<Command> cmd_vec = client.ReadRecvBuffer(10000);
                    std::cout << "******************************" << std::endl;
                    for (auto &cmd : cmd_vec) {
                        std::cout << " -- Command: " << cmd.command << std::endl;
                        for (auto &arg : cmd.arguments) {std::cout << arg << std::endl;}
                    }
                    std::cout << "******************************" << std::endl;
                    break;
                }
                default:
                  std::cerr << "Invalid input." << std::endl;
            }
        }
        io_context.stop();
        io_thread.join();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}
