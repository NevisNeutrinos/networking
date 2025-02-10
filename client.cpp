//
// Created by Jon Sensenig on 2/10/25.
//

#include "tcp_connection.h"
#include <iostream>

typedef TCPProtocol::CommandCodes Cmd;

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

int main() {
    try {
        asio::io_context io_context;
        std::cout << "Starting client..." << std::endl;
        TCPConnection server(io_context, "127.0.0.1", 12345, false);
        std::cout << "Starting IO Context..." << std::endl;

        std::thread io_thread([&]() { io_context.run(); });

        while (true) {
            PrintState();
            int input = GetUserInput();

            if (input == -1) {
                std::cout << "Exiting...\n";
                break;
            }

            switch (input) {
                case 0: {
                    std::cout << "Enter Cmd:  ";
                    auto cmd = static_cast<Cmd>(GetUserInput());
                    std::cout << "Enter Arg: \n";
                    std::vector<int> args = GetUserInputList();
                    std::cout << "Sending command" << std::endl;
                    server.WriteSendBuffer(cmd, args);
                    break;
                }
                case 1: {
                    std::cout << "Receiving command" << std::endl;
                    Command cmd = server.ReadRecvBuffer();
                    std::cout << "******************************" << std::endl;
                    std::cout << "Command: " << static_cast<uint16_t>(cmd.command) << std::endl;
                    for (auto &arg : cmd.arguments) {std::cout << arg << std::endl;}
                    std::cout << "******************************" << std::endl;
                    break;
                }
                case 2: {
                    std::cout << "Receiving All command" << std::endl;
                    std::vector<Command> cmd_vec = server.ReadRecvBuffer(10000);
                    std::cout << "******************************" << std::endl;
                    for (auto &cmd : cmd_vec) {
                        std::cout << " -- Command: " << static_cast<uint16_t>(cmd.command) << std::endl;
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