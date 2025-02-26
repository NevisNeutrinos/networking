//
// Created by Jon Sensenig on 2/26/25.
//

#include "gtest/gtest.h"
#include "../tcp_protocol.h"
#include <vector>
#include <cstring>
#include <cstdint>


// Test fixture for TCPProtocol class
class TCPProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed. This runs before each test.
    }

    void TearDown() override {
        // Teardown code if needed. This runs after each test.
    }
};

// Test the constructor
TEST_F(TCPProtocolTest, Constructor) {
    uint16_t cmd = 0x1234;
    size_t vec_size = 3;
    TCPProtocol protocol(cmd, vec_size);

    EXPECT_EQ(protocol.command_code, cmd);
    EXPECT_EQ(protocol.arg_count, vec_size);
    EXPECT_EQ(protocol.arguments.size(), vec_size);
    EXPECT_EQ(protocol.start_code1, TCPProtocol::kStartCode1);
    EXPECT_EQ(protocol.start_code2, TCPProtocol::kStartCode2);
//    EXPECT_EQ(protocol.crc, 1);
    EXPECT_EQ(protocol.end_code1, TCPProtocol::kEndCode1);
    EXPECT_EQ(protocol.end_code2, TCPProtocol::kEndCode2);
}

// Test CalcCRC with std::vector<uint8_t>
TEST_F(TCPProtocolTest, CalcCRCVector) {
    TCPProtocol protocol(0, 0);
    std::vector<uint8_t> buffer = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint16_t initial_crc = 0x0;
    uint16_t expected_crc = 0x8858; // Calculated with online tool.
    uint16_t result_crc = protocol.CalcCRC(buffer, buffer.size(), initial_crc);

    EXPECT_EQ(result_crc, expected_crc);
}

// Test CalcCRC with const uint8_t*
TEST_F(TCPProtocolTest, CalcCRCArray) {
    TCPProtocol protocol(0, 0);
    uint8_t buffer[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint16_t initial_crc = 0x0;
    uint16_t expected_crc = 0x8858; // Calculated with online tool.
    uint16_t result_crc = protocol.CalcCRC(buffer, sizeof(buffer), initial_crc);

    EXPECT_EQ(result_crc, expected_crc);
}

// Test Serialize with no arguments
TEST_F(TCPProtocolTest, SerializeNoArgs) {
    uint16_t cmd = 0x1234;
    TCPProtocol protocol(cmd, 0);

    std::vector<uint8_t> serialized_data = protocol.Serialize();

    EXPECT_GE(serialized_data.size(), protocol.header_size_ + protocol.footer_size_);

    // Verify start codes
    EXPECT_EQ(serialized_data[0], TCPProtocol::kStartCode1);
    EXPECT_EQ(serialized_data[1], TCPProtocol::kStartCode2);

    // Verify command code
    uint16_t received_cmd = (serialized_data[3] << 8) | serialized_data[2];
    EXPECT_EQ(received_cmd, cmd);

    //Verify arg count
    uint16_t argCount = (serialized_data[5] << 8) | serialized_data[4];
    EXPECT_EQ(argCount,0);

    // Verify end codes
    EXPECT_EQ(serialized_data.back() - 1, TCPProtocol::kEndCode1);
    EXPECT_EQ(serialized_data.back(), TCPProtocol::kEndCode2);

    // Calculate the expected CRC manually
    std::vector<uint8_t> bufferForCrc(serialized_data.begin(), serialized_data.end() - protocol.footer_size_);
    uint16_t calculated_crc = protocol.CalcCRC(bufferForCrc,bufferForCrc.size(), 0xFFFF);
    uint16_t received_crc = (serialized_data[serialized_data.size() - 4] << 8) | serialized_data[serialized_data.size() - 3];
    EXPECT_EQ(calculated_crc, received_crc);
}

// Test Serialize with arguments
TEST_F(TCPProtocolTest, SerializeWithArgs) {
    uint16_t cmd = 0xABCD;
    size_t arg_count = 2;
    TCPProtocol protocol(cmd, arg_count);
    protocol.arguments[0] = 0x11223344;
    protocol.arguments[1] = 0x55667788;

    std::vector<uint8_t> serialized_data = protocol.Serialize();

    EXPECT_GE(serialized_data.size(), protocol.header_size_ + arg_count*sizeof(int32_t) + protocol.footer_size_);

    // Verify start codes
    EXPECT_EQ(serialized_data[0], TCPProtocol::kStartCode1);
    EXPECT_EQ(serialized_data[1], TCPProtocol::kStartCode2);

    // Verify command code
    uint16_t received_cmd = (serialized_data[3] << 8) | serialized_data[2];
    EXPECT_EQ(received_cmd, cmd);

     //Verify arg count
    uint16_t received_argCount = (serialized_data[5] << 8) | serialized_data[4];
    EXPECT_EQ(received_argCount,arg_count);

    //Verify arguments
    int32_t arg1 = (serialized_data[9] << 24) | (serialized_data[8] << 16) | (serialized_data[7] << 8) | serialized_data[6];
    int32_t arg2 = (serialized_data[13] << 24) | (serialized_data[12] << 16) | (serialized_data[11] << 8) | serialized_data[10];
    EXPECT_EQ(arg1, protocol.arguments[0]);
    EXPECT_EQ(arg2, protocol.arguments[1]);

    // Verify end codes
    EXPECT_EQ(serialized_data.back() - 1, TCPProtocol::kEndCode1);
    EXPECT_EQ(serialized_data.back(), TCPProtocol::kEndCode2);

    // Calculate the expected CRC manually
    std::vector<uint8_t> bufferForCrc(serialized_data.begin(), serialized_data.end() - protocol.footer_size_);
    uint16_t calculated_crc = protocol.CalcCRC(bufferForCrc,bufferForCrc.size(), 0xFFFF);
    uint16_t received_crc = (serialized_data[serialized_data.size() - 4] << 8) | serialized_data[serialized_data.size() - 3];
    EXPECT_EQ(calculated_crc, received_crc);

}