# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a syscall interception and remote execution project that intercepts system calls from client programs and forwards them to a server process via UNIX domain sockets. The project explores syscall interception using LD_PRELOAD and implements a binary protocol for client-server communication.

## Architecture

The project consists of three main components:

1. **Syscall Server** (`src/syscall_server.c`): A UNIX domain socket server that listens at `/tmp/p3_tb`, accepts connections from intercepted programs, receives syscall information, and processes requests.

2. **Interception Library** (`src/intercept_client.c`): A shared library loaded via LD_PRELOAD that intercepts libc functions (currently `read()`), packages syscall information into protocol messages, and sends them to the server via socket connection.

3. **Test Program** (`src/program.c`): A simple program used to test the interception mechanism.

### Key Implementation Details

- **Syscall Interception**: Uses LD_PRELOAD to override libc functions. Individual interceptors are in `src/intercept/` (e.g., `intercept_read.h`).
- **Thread-Safe Reentry Protection**: Each interceptor uses `__thread` local guards to prevent infinite recursion when intercepted functions call themselves indirectly.
- **Socket Communication**: Client connects to `/tmp/p3_tb` UNIX domain socket for each syscall, sends structured message, then disconnects.
- **Protocol**: Binary message format defined in `src/protocol/protocol_main_header.h` with `ClientMsg` and `ServerMsg` structs containing version, client_id, payload_size, and payload fields.

### Directory Structure

- `src/`: Core implementation files
  - `protocol/`: Protocol definitions and serialization (work in progress)
  - `intercept/`: Individual syscall interceptor implementations
- `build/`: Compiled binaries and shared libraries
- `playground/`: Experimental code and examples for prototyping
- `gramine/`: Gramine library submodule (referenced for PAL interface study)

## Build Commands

Build all components:
```bash
make all
```

Build individual components:
```bash
make server    # Builds syscall_server
make client    # Builds intercept.so shared library
make program   # Builds test program
```

Clean build artifacts:
```bash
make clean
```

## Running the Project

1. Start the server in one terminal:
```bash
make run_server
# or directly:
./build/syscall_server
```

2. Run the intercepted program in another terminal:
```bash
make run_program
# or directly:
LD_PRELOAD=./build/intercept.so ./build/program
```

## Development Tasks

Current work focuses on (see `todo.md`):
- Implementing complete message serialization/deserialization in `src/protocol/serialization.h`
- Creating a PAL-like interface abstraction layer
- Potentially auto-generating interface definitions from PAL specifications using AST parsing

## Important Notes

- The server must be running before launching intercepted programs, or the client will fail to connect
- Socket path is hardcoded to `/tmp/p3_tb` in both client and server
- Current implementation only intercepts `read()` syscall; adding more interceptors requires creating new headers in `src/intercept/`
- Serialization implementation is incomplete (marked with TODO in `serialization.h`)
- Protocol uses fixed-size 1024-byte payload buffers in messages
