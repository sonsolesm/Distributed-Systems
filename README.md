# 🌐 Distributed Systems – P2P File Sharing Platform

This repository contains the final project for the Distributed Systems course at Universidad Carlos III de Madrid (UC3M), developed by **Sonsoles Molina Abad** and **Lorenzo Largacha Sanz**.

We designed and implemented a peer-to-peer (P2P) file sharing system using C and Python. The system supports distributed file publishing, searching, and transferring between clients via sockets, without relying on a central file repository. The project also includes a timestamping web service and a partially implemented RPC logging server.

The system consists of:
- A concurrent multithreaded server in C that coordinates client operations.
- A client in Python that interacts with the server and other peers.
- A web service that provides a timestamp for every client request.
- An RPC logging server (optional), designed to receive and display user operations.

Each client runs a listener thread and acts as both a user and a P2P node, capable of publishing files and serving downloads to other clients.

### Features
- Register / unregister users
- Connect / disconnect to the system
- Publish and delete file references
- List connected users and their shared files
- Peer-to-peer file transfers between clients
- Web service for real-time timestamping
- RPC logging of user operations (*partially implemented*)

### Technologies Used
- Languages: C, Python
- Concepts: TCP sockets, multithreading, client-server architecture, P2P systems, ONC-RPC, web services
- Libraries: `socket`, `threading`, `spyne`, `zeep`, `rpcgen`
- Environment: Linux (tested on UC3M lab machines)

### Setup & Execution

One-time setup:
```bash
chmod +x setup.sh
./setup.sh     # installs Python dependencies (spyne, zeep)
make           # compiles all C files (server and optional RPC)
```

To run the full system, open **three terminal windows** and execute:

1. Start the server:
```bash
./server -p <port>
```

2. Start the web service:
```bash
python3 web_services.py
```

3. Start the client:
```bash
python3 client.py -s <server_ip> -p <port>
```

You can then interact with the system using supported commands:
- `REGISTER <username>`
- `UNREGISTER <username>`
- `CONNECT <username>`
- `PUBLISH <file> <description>`
- `DELETE <file>`
- `LIST_USERS`
- `LIST_CONTENT <username>`
- `DISCONNECT <username>`
- `GET_FILE <user> <remote_file> <local_file>`
- `QUIT`

### Testing

We implemented a full set of functional tests for:
- Registration and connection (including error handling)
- File publishing and deletion
- User and content listing
- Peer-to-peer file download
- Client disconnection
- Integration of web service and RPC logging (partially)

These tests are documented in the final report [`Memoria Practica Final.pdf`](./Memoria%20Practica%20Final.pdf), which includes command-by-command results.

### File Structure
```
distributed-systems/
├── client.py                # Python client interface
├── server.c                 # C server
├── lines.c / lines.h        # Socket utility functions
├── web_services.py          # Timestamp web service
├── operations.x             # ONC-RPC interface definition
├── server_operations.c      # RPC server logic (partial)
├── Makefile                 # Compilation instructions
├── setup.sh                 # Python env setup
├── Memoria Practica Final.pdf
└── README.md
```

### Notes
- The RPC server is partially implemented (`server_operations.c`).
- The system runs fully without RPC. Web service is required.
- Designed to run across multiple machines or terminals.

### Authors
- **Sonsoles Molina Abad**
- **Lorenzo Largacha Sanz**
