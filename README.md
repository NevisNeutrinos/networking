# networking

To build client and server,
```
mkdir build && cmake -B build && cd build && make
```

For now commands and arguements can be sent between the client and server.
The IP address and port are hardcoded in the server.cpp and client.cpp

Client:
Connects to a server running at given IP and port.
```c++
TCPConnection server(io_context, "127.0.0.1", 12345, false);
```

Server: 
Connects to the specified IP address and port.
```c++
TCPConnection server(io_context, "127.0.0.1", 12345, true);
```

After building the server `GramsReadoutConnect` can be started, 
followed by the client `GramsReadoutClient`. Both client and server have a 
menu where you can either send or read commands.
```
Select a command:
  [0] Send Cmd
  [1] Read Cmd
  [2] Read All Cmd
  [-1] Exit
Enter choice:
```

If option `[0]` is selected it is followed by the prompt to enter a command, 
followed by a prompt for a space separated list of arguements.
```
Enter Cmd:  4
Enter Arg: 
Enter numbers in one line: 942 832 95
```

Option `[1]` reads out a single command from the bufffer with its associated
arguements while `[2]` reads out all commands in the buffer.
```
******************************
 -- Command: 4
942
832
95
******************************
```

