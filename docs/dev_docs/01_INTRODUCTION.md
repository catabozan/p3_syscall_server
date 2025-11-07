# Introduction to P3_TB Syscall Interception System

## What is This Project?

The P3_TB system is a **transparent syscall interception and remote execution framework**. It allows you to intercept file I/O operations (open, close, read, write) from any program and execute them on a remote server, without modifying the target program.

### Real-World Analogy

Imagine you're working at a desk (your program) and need to access files from a filing cabinet (filesystem). Normally, you'd walk to the cabinet yourself. With our system:

1. **Interception**: We intercept your request before you get up
2. **Transmission**: We send your request to an assistant (server) via phone (RPC)
3. **Execution**: The assistant accesses the filing cabinet for you
4. **Return**: The assistant sends back the file contents

Your program doesn't know the difference—it just gets the file!

## Key Features

### ✅ Transparent Interception
- Uses `LD_PRELOAD` to intercept libc functions
- No source code modification needed
- Works with any dynamically-linked program

### ✅ RPC-Based Communication
- Industry-standard ONC RPC protocol
- Automatic serialization with XDR
- Choice of UNIX or TCP transport

### ✅ File Descriptor Mapping
- Server maintains client_fd ↔ server_fd mapping
- Handles multiple open files correctly
- Automatic cleanup on close

### ✅ Thread-Safe Design
- Per-thread RPC connections
- Reentry guards prevent recursion
- Supports multi-threaded programs

### ✅ Error Propagation
- Accurate errno values returned
- Client sees exact server-side errors
- Transparent error handling

## Use Cases

### 1. Remote File Access
Access files on a different machine without NFS/SMB:
```bash
# Server on machine A with files
./build/syscall_server

# Client on machine B runs program
RPC_TRANSPORT=tcp LD_PRELOAD=./build/intercept.so ./my_program
```

### 2. Sandboxing
Execute programs with file operations handled by a supervisor:
```bash
# Supervisor process controls file access
./build/syscall_server  # Can log, filter, or modify operations

# Sandboxed program
LD_PRELOAD=./build/intercept.so ./untrusted_program
```

### 3. Testing & Development
Test file operations without touching real files:
```bash
# Test server in memory or mock filesystem
./build/syscall_server  # Can simulate errors, slow I/O, etc.

# Program under test
LD_PRELOAD=./build/intercept.so ./program_to_test
```

### 4. File System Migration
Transparently redirect legacy applications to new storage:
```bash
# New storage backend via server
./build/syscall_server  # Maps to new filesystem

# Legacy app unchanged
LD_PRELOAD=./build/intercept.so ./legacy_app
```

## Architecture at a Glance

```
┌─────────────────────────────────────────────────────────────┐
│                    User Program (Unmodified)                 │
│                  ./my_program or ls or cat                   │
└────────────────────────────┬────────────────────────────────┘
                             │ calls open()/read()/write()/close()
                             ↓
┌─────────────────────────────────────────────────────────────┐
│              Interception Layer (LD_PRELOAD)                 │
│                    intercept.so                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  Interceptors: open() close() read() write()         │  │
│  └──────────────────┬───────────────────────────────────┘  │
└─────────────────────┼──────────────────────────────────────┘
                      │ RPC call
                      ↓
┌─────────────────────────────────────────────────────────────┐
│                    RPC Transport Layer                       │
│           UNIX Domain Socket  OR  TCP Socket                 │
│                    /tmp/p3_tb  OR  localhost:9999           │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ↓
┌─────────────────────────────────────────────────────────────┐
│                      RPC Server                              │
│                  ./syscall_server                            │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  FD Mapper: client_fd → server_fd                    │  │
│  │  Executors: syscall_open_1_svc()                     │  │
│  │             syscall_read_1_svc()                     │  │
│  │             syscall_write_1_svc()                    │  │
│  │             syscall_close_1_svc()                    │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────────────────┬────────────────────────────────┘
                             │ actual syscalls
                             ↓
┌─────────────────────────────────────────────────────────────┐
│                     Operating System                         │
│                   Real Filesystem                            │
└─────────────────────────────────────────────────────────────┘
```

## Quick Start

### Build the Project
```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install libtirpc-dev

# Build everything
make clean && make all
```

### Run a Test
```bash
# Terminal 1: Start the server
./build/syscall_server

# Terminal 2: Run test program with interception
LD_PRELOAD=./build/intercept.so ./build/program
```

### Expected Output
```
[Client] Intercepted open("/tmp/p3_tb_test.txt", 577, 644)
[Client] open() RPC result: fd=3, errno=0
[Client] Intercepted write(3, 0x..., 56)
[Client] write() RPC result: 56 bytes, errno=0
...
=== Test Result: ALL TESTS PASSED ===
```

## Core Components

### 1. Protocol Definition (`src/protocol/protocol.x`)
Defines the RPC interface in XDR language:
```c
program SYSCALL_PROG {
    version SYSCALL_VERS {
        open_response SYSCALL_OPEN(open_request) = 1;
        read_response SYSCALL_READ(read_request) = 3;
        // ...
    } = 1;
} = 0x20000001;
```

### 2. RPC Server (`src/rpc_server.c`)
- Accepts RPC requests
- Maps client FDs to server FDs
- Executes actual syscalls
- Returns results with errno

### 3. RPC Client (`src/rpc_client.c`)
- Maintains persistent RPC connection
- Provides `get_rpc_client()` helper
- Handles transport selection

### 4. Interceptors (`src/intercept/*.h`)
- Override libc functions (open, close, read, write)
- Forward calls to RPC server
- Handle errors and fallback

### 5. Build System (`Makefile`)
- Generates RPC stubs with `rpcgen`
- Compiles server and client library
- Links with libtirpc

## Technologies Used

| Technology | Purpose | Why? |
|------------|---------|------|
| **ONC RPC** | Remote procedure calls | Industry standard, mature, well-documented |
| **XDR** | Data serialization | Built-in to RPC, handles endianness automatically |
| **rpcgen** | Code generation | Auto-generates client/server stubs from protocol |
| **LD_PRELOAD** | Function interception | OS-level mechanism, requires no code changes |
| **libtirpc** | RPC library | Modern implementation, supports UNIX sockets |

## Configuration

The system uses environment variables for configuration:

```bash
# Transport selection
RPC_TRANSPORT=unix    # Use UNIX domain sockets (default)
RPC_TRANSPORT=tcp     # Use TCP sockets

# Server-side only
RPC_TRANSPORT=tcp ./build/syscall_server

# Client-side
RPC_TRANSPORT=tcp LD_PRELOAD=./build/intercept.so ./program
```

Edit `src/transport_config.h` to change defaults:
```c
#define UNIX_SOCKET_PATH "/tmp/p3_tb"
#define TCP_HOST "localhost"
#define TCP_PORT 9999
```

## Limitations & Constraints

### Current Limitations
1. **Single client per server**: Server handles one client at a time (UNIX sockets)
2. **File I/O only**: Only open/close/read/write intercepted
3. **No stat operations**: stat(), fstat(), lstat() not implemented
4. **No directory operations**: opendir(), readdir() not supported
5. **No seeking**: lseek() not implemented

### Performance Considerations
- **RPC Overhead**: Each syscall requires RPC round-trip (~2-3μs for UNIX sockets)
- **Data Copy**: Read/write data copied through RPC buffers
- **Max Buffer Size**: 1MB per read/write operation (configurable)

### Thread Safety
- ✅ **Safe**: Each thread gets its own RPC connection
- ✅ **Safe**: Reentry guards prevent recursion
- ⚠️ **Note**: FD space shared across threads (by design)

## Project Status

### ✅ Fully Implemented
- ONC RPC/XDR protocol
- UNIX socket transport
- TCP socket transport (requires rpcbind)
- open(), close(), read(), write() syscalls
- FD mapping
- errno propagation
- Thread-safe interception
- Comprehensive test suite

### 🚧 Future Enhancements
- Multi-client server support
- Additional syscalls (stat, lseek, fcntl)
- Async I/O support
- Authentication & encryption
- Performance optimizations

## Next Steps

Now that you understand what this project does, proceed to:

→ **[02_ARCHITECTURE.md](./02_ARCHITECTURE.md)** to understand the system design

→ **[03_ONC_RPC_PRIMER.md](./03_ONC_RPC_PRIMER.md)** to learn about RPC/XDR

→ **[10_DEVELOPMENT_GUIDE.md](./10_DEVELOPMENT_GUIDE.md)** to start developing

---

**Questions?** Check the [README](./00_README.md) for navigation help.
