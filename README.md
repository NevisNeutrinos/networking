# networking

To build client and server,
```
mkdir build && cmake -B build && cd build && make
```

As of now, the client sends data to the server which 
echos it back to the client. The IP address and port 
are hardcoded in the server.cpp and client.cpp

Client:
Connects to a server running at given IP and port.
```c++
socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), 12345));
```

Server: 
Connects to the specified IP address and port.
```c++
TCPConnection server(io_context, "127.0.0.1", 12345, true);
```

After building the server `GramsReadoutConnect` can be started, 
followed by the client `GramsReadoutClient`. The client will 
ask for an integer which, after being entered, will be sent 
and echoed back from the server. The last arg should be the
one entered, the command and other args are hardcoded for now.

```
********************************
Start Code 1: 0xeb90 
Start Code 2: 0x5b6a 
Command:      42 
NArgs:        5 
Args:  10 20 30 40 45
CRC:          11828 
End Code 1:   0xc5a4 
End Code 2:   0xd279
********************************
```