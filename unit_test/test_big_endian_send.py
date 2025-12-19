import socket
import struct

# Server details
HOST = "127.0.0.1"  # Change to target IP
PORT = 12345         # Change to target port

# Constants
START_CODE1 = 0xEB90
START_CODE2 = 0x5B6A
END_CODE1 = 0xC5A4
END_CODE2 = 0xD279


# Example values
command_code = 0x3344
arguments = [123456, -654321, 789012]  # Example uint32_t arguments
arg_count = len(arguments)

# Pack the data in big-endian format by using ">"
payload = struct.pack(">4H", START_CODE1, START_CODE2, command_code, arg_count)  # Header
payload += struct.pack(f">{arg_count}i", *arguments)  # Arguments
crc = 0x1 # CRC will fail since it is fake
payload += struct.pack(">3H", crc, END_CODE1, END_CODE2)  # Footer

print(f"Sending data: {payload.hex()}")

# Create and send the TCP packet
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
    sock.connect((HOST, PORT))
    sock.sendall(payload)

    # Optionally receive a response
    response = sock.recv(1024)
    print(f"Received: {response.hex()}")


