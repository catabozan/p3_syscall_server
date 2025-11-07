# Transport Layer

This document explains how the RPC transport layer works, covering both UNIX domain sockets and TCP sockets implementations.

## Overview

The transport layer handles the actual network communication between client and server. ONC RPC is **transport-agnostic**, meaning the same RPC protocol can work over different underlying transports.

Our implementation supports two transports:

| Transport | Path/Address | Speed | Scope | Portmapper? |
|-----------|--------------|-------|-------|-------------|
| UNIX Domain Socket | `/tmp/p3_tb` | Very fast (~2-3 μs) | Same machine only | Not needed |
| TCP Socket | `localhost:9999` | Fast (~50 μs local) | Network-capable | Required |

## Transport Selection

### Configuration

Both client and server read the `RPC_TRANSPORT` environment variable:

```bash
# Use UNIX sockets (default)
unset RPC_TRANSPORT
# OR
export RPC_TRANSPORT=unix

# Use TCP sockets
export RPC_TRANSPORT=tcp
```

### Implementation (transport_config.h)

```c
typedef enum {
    TRANSPORT_UNIX,  // Default
    TRANSPORT_TCP
} transport_type_t;

#define UNIX_SOCKET_PATH "/tmp/p3_tb"
#define TCP_HOST "localhost"
#define TCP_PORT 9999

static inline transport_type_t get_transport_type(void) {
    const char *env = getenv("RPC_TRANSPORT");
    if (env != NULL && strcasecmp(env, "tcp") == 0) {
        return TRANSPORT_TCP;
    }
    return TRANSPORT_UNIX;  // Default
}
```

**Design Rationale**:
- Single source of truth for configuration
- Easy to extend (add new transports)
- Runtime selection (no recompilation needed)

## UNIX Domain Socket Transport

### What Are UNIX Domain Sockets?

UNIX domain sockets are IPC (Inter-Process Communication) endpoints that:
- Use filesystem paths as addresses (e.g., `/tmp/p3_tb`)
- Only work on the same machine
- Are faster than TCP (no network stack overhead)
- Support both stream (SOCK_STREAM) and datagram (SOCK_DGRAM) modes

We use **SOCK_STREAM** for reliable, ordered, bidirectional communication.

### Server Implementation

#### Step 1: Create Listen Socket

```c
// Create socket
int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
if (listen_sock < 0) {
    perror("socket");
    exit(1);
}

// Prepare address structure
struct sockaddr_un addr;
memset(&addr, 0, sizeof(addr));
addr.sun_family = AF_UNIX;
strncpy(addr.sun_path, UNIX_SOCKET_PATH, sizeof(addr.sun_path) - 1);

// Remove stale socket file if exists
unlink(UNIX_SOCKET_PATH);

// Bind socket to path
if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    exit(1);
}

// Listen for connections (backlog = 5)
if (listen(listen_sock, 5) < 0) {
    perror("listen");
    exit(1);
}

printf("[Server] Listening on UNIX socket: %s\n", UNIX_SOCKET_PATH);
```

**Key Points**:
- `unlink()` removes old socket file (left over from previous run)
- Backlog of 5 means up to 5 pending connections
- After `listen()`, server is ready to accept clients

#### Step 2: Accept Connections (Loop)

```c
while (1) {
    printf("[Server] Waiting for client connection...\n");

    // Block until client connects
    int client_sock = accept(listen_sock, NULL, NULL);
    if (client_sock < 0) {
        perror("accept");
        continue;  // Try again
    }

    printf("[Server] Client connected (fd=%d)\n", client_sock);

    // Create RPC service handle for this client
    SVCXPRT *transp = svcfd_create(client_sock, 0, 0);
    if (transp == NULL) {
        fprintf(stderr, "Error: svcfd_create failed\n");
        close(client_sock);
        continue;
    }

    // Register RPC dispatcher
    if (!svc_register(transp, SYSCALL_PROG, SYSCALL_VERS, syscall_prog_1, 0)) {
        fprintf(stderr, "Error: unable to register\n");
        svc_destroy(transp);
        continue;
    }

    // Handle requests from this client (blocks until disconnect)
    svc_run();

    printf("[Server] Client disconnected\n");

    // Cleanup
    svc_unregister(SYSCALL_PROG, SYSCALL_VERS);
    svc_destroy(transp);
}
```

**Flow**:
1. `accept()` blocks until client connects
2. `svcfd_create()` wraps the connected socket in RPC transport
3. `svc_register()` associates program number with dispatcher function
4. `svc_run()` enters event loop, handling RPC requests until client disconnects
5. Cleanup and loop back to accept next client

**Why `svcfd_create()` Instead of `svcunix_create()`?**

`svcunix_create()` creates a listening socket, but we already have one. We need to create an RPC transport for each *accepted* connection, so we use `svcfd_create()` which takes an existing connected socket.

### Client Implementation

```c
// Step 1: Create socket
int sock = socket(AF_UNIX, SOCK_STREAM, 0);
if (sock < 0) {
    perror("socket");
    return NULL;
}

// Step 2: Prepare server address
struct sockaddr_un server_addr;
memset(&server_addr, 0, sizeof(server_addr));
server_addr.sun_family = AF_UNIX;
strncpy(server_addr.sun_path, UNIX_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

// Step 3: Connect to server
if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    perror("connect");
    close(sock);
    return NULL;
}

printf("[Client] Connected to UNIX socket: %s\n", UNIX_SOCKET_PATH);

// Step 4: Create RPC client handle
struct netbuf svcaddr;
svcaddr.len = svcaddr.maxlen = sizeof(server_addr);
svcaddr.buf = (char *)&server_addr;

CLIENT *clnt = clnt_vc_create(sock, &svcaddr, SYSCALL_PROG, SYSCALL_VERS, 0, 0);
if (clnt == NULL) {
    clnt_pcreateerror("clnt_vc_create");
    close(sock);
    return NULL;
}

return clnt;
```

**Flow**:
1. Create socket
2. `connect()` to server's listening socket
3. `clnt_vc_create()` creates RPC client handle from connected socket
4. Ready to make RPC calls

**What is `clnt_vc_create()`?**

- **vc** = "virtual circuit" (connection-oriented, like TCP or UNIX stream)
- Creates CLIENT handle for making RPC calls
- Parameters:
  - `sock`: Connected socket file descriptor
  - `svcaddr`: Server address (needed for reconnection logic)
  - `SYSCALL_PROG`, `SYSCALL_VERS`: Program/version to call
  - `0, 0`: Send/receive buffer sizes (0 = default)

### Connection Lifecycle

```
Server                                   Client
======                                   ======

socket() + bind() + listen()
        ↓
[Waiting for connections]
        ↓
accept() ←───────────────────────── socket() + connect()
        ↓                                   ↓
svcfd_create(client_sock)            clnt_vc_create(sock)
        ↓                                   ↓
svc_register()                       [Ready to call]
        ↓                                   ↓
svc_run() ←─────── RPC Request ─────── syscall_open_1()
        ↓                                   ↑
[Dispatch to handler]                      │
        ↓                                   │
syscall_open_1_svc()                       │
        ↓                                   │
[Execute syscall]                          │
        ↓                                   │
[Return response] ────── RPC Response ─────┘
        ↓                                   ↓
[Continue loop]                        [Use result]
        ↓                                   ↓
        ⋮                                   ⋮
        ↓                                   ↓
[Client disconnects] ←──────────────── clnt_destroy() or exit
        ↓
svc_run() returns
        ↓
Cleanup
        ↓
[Back to accept()]
```

### Wire Protocol (UNIX Sockets)

Data sent over UNIX sockets:

```
┌──────────────────────────────────────────────────┐
│             RPC Message Header                    │
│  - Transaction ID (xid)                          │
│  - Message type (CALL or REPLY)                  │
│  - RPC version (2)                               │
│  - Program number (SYSCALL_PROG)                 │
│  - Version number (SYSCALL_VERS)                 │
│  - Procedure number (SYSCALL_OPEN, etc.)         │
│  - Credentials                                   │
│  - Verifier                                      │
├──────────────────────────────────────────────────┤
│             XDR-Encoded Arguments                 │
│  (Serialized by xdr_open_request, etc.)          │
│  - Path string (length-prefixed)                 │
│  - Flags (32-bit int)                            │
│  - Mode (32-bit int)                             │
└──────────────────────────────────────────────────┘
```

Response format is similar.

**Important**: The RPC library handles all framing, headers, and message assembly. We only deal with the high-level procedure calls.

## TCP Socket Transport

### What Are TCP Sockets?

TCP sockets provide:
- Network communication (can span machines)
- Reliable, ordered, byte-stream delivery
- Connection-oriented (like UNIX stream sockets)
- Address format: `host:port` (e.g., `localhost:9999`)

### Why TCP Requires Portmapper

**Problem**: How does a client find the server?

With UNIX sockets, the path is fixed (`/tmp/p3_tb`). With TCP, the server can bind to any available port. The **portmapper** (also called **rpcbind**) solves this:

1. Portmapper runs on well-known port 111
2. Server registers: "I'm SYSCALL_PROG v1 on port X"
3. Client queries portmapper: "Where is SYSCALL_PROG v1?"
4. Portmapper responds: "Port X"
5. Client connects to port X

### Server Implementation

```c
// Step 1: Create TCP transport
SVCXPRT *transp = svctcp_create(RPC_ANYSOCK, 0, 0);
if (transp == NULL) {
    fprintf(stderr, "Error: cannot create TCP service\n");
    exit(1);
}

// Step 2: Register with portmapper
if (!svc_register(transp, SYSCALL_PROG, SYSCALL_VERS,
                 syscall_prog_1, IPPROTO_TCP)) {
    fprintf(stderr, "Error: unable to register (SYSCALL_PROG, SYSCALL_VERS, tcp)\n");
    fprintf(stderr, "Make sure rpcbind is running!\n");
    exit(1);
}

printf("[Server] RPC server registered with portmapper\n");
printf("[Server] Listening on TCP (port assigned by system)...\n");

// Step 3: Enter event loop (never returns)
svc_run();
```

**Key Differences from UNIX**:
- `svctcp_create()` handles socket creation, binding, and listening automatically
- `RPC_ANYSOCK` means "let kernel assign a port"
- `svc_register()` with `IPPROTO_TCP` registers with portmapper
- No manual accept loop (RPC library handles it)

### Client Implementation

```c
// Connect to server via portmapper
CLIENT *clnt = clnt_create(TCP_HOST, SYSCALL_PROG, SYSCALL_VERS, "tcp");
if (clnt == NULL) {
    clnt_pcreateerror("clnt_create");
    return NULL;
}

printf("[Client] Connected to RPC server via TCP\n");
return clnt;
```

**What `clnt_create()` Does**:
1. Queries portmapper on `TCP_HOST` port 111
2. Asks: "What port is SYSCALL_PROG v1 on?"
3. Portmapper responds with port number
4. Connects to that port
5. Returns CLIENT handle

**Much simpler than UNIX!** But requires rpcbind running.

### Connection Lifecycle

```
Portmapper (port 111)              Server                     Client
======================             ======                     ======

[Running on port 111]
        ↑
        │ Register
        ├──────────────────── svctcp_create()
        │                     svc_register(IPPROTO_TCP)
        │                            ↓
        │                     [Listening on port X]
        │                                                       ↓
        │ Query: "SYSCALL_PROG?" ←───────────────────── clnt_create()
        │                                                       ↑
        └─ Response: "Port X" ───────────────────────────────→ │
                                                                │
                               ←─────── connect to port X ──────┘
                                        ↓
                                   [Accept]
                                        ↓
                               ←─────── RPC calls ──────→
```

### Portmapper Interaction Example

```bash
# Start portmapper (usually runs as system service)
sudo rpcbind

# Check portmapper is running
rpcinfo -p

# Output:
#    program vers proto   port  service
#     100000    4   tcp    111  portmapper
#     100000    3   tcp    111  portmapper
#     100000    2   tcp    111  portmapper
#     100000    4   udp    111  portmapper
#     100000    3   udp    111  portmapper
#     100000    2   udp    111  portmapper

# Start our server (registers with portmapper)
RPC_TRANSPORT=tcp ./build/syscall_server

# Check our service is registered
rpcinfo -p

# Output now includes:
#    536870913    1   tcp  40123  (our SYSCALL_PROG = 0x20000001)

# Client connects
RPC_TRANSPORT=tcp LD_PRELOAD=./build/intercept.so ./build/program
```

## Transport Comparison

### Performance

| Operation | UNIX Socket | TCP Socket (localhost) |
|-----------|-------------|------------------------|
| Connection setup | ~5 μs | ~50 μs (portmapper query) |
| RPC call (small payload) | ~2-3 μs | ~50 μs |
| RPC call (1 MB payload) | ~500 μs | ~2 ms |
| Throughput | ~2 GB/s | ~1 GB/s |

**Benchmark conditions**: localhost, Linux 5.x, no network congestion

### Use Cases

**UNIX Sockets** (default):
- ✅ Single machine (development, testing)
- ✅ Maximum performance
- ✅ No portmapper dependency
- ✅ Simpler security model (filesystem permissions)

**TCP Sockets**:
- ✅ Remote file access across network
- ✅ Server and client on different machines
- ✅ Portmapper already running (common in NFS environments)
- ⚠️ Requires firewall configuration
- ⚠️ Slightly slower
- ⚠️ Security considerations (network exposure)

## Implementation Details

### Buffer Sizes

```c
// In clnt_vc_create() and svcfd_create()
// Last two parameters are send/receive buffer sizes
clnt_vc_create(sock, &svcaddr, SYSCALL_PROG, SYSCALL_VERS, 0, 0);
                                                              ↑  ↑
                                                       sendsz  recvsz
                                                       (0 = default)
```

Default buffer sizes (libtirpc):
- Send buffer: 8192 bytes
- Receive buffer: 8192 bytes

**For large payloads** (e.g., 1 MB read/write):
- RPC library handles fragmentation automatically
- Multiple send()/recv() calls under the hood
- Application sees single RPC call

### Socket Options

The RPC library sets these socket options automatically:

```c
// For TCP sockets
int opt = 1;
setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));  // Disable Nagle
setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));  // Keep-alive

// For UNIX sockets
// (No special options needed)
```

**TCP_NODELAY**: Disables Nagle's algorithm, reducing latency for small messages (RPC requests/responses)

**SO_KEEPALIVE**: Detects dead connections

### Error Handling

#### Client-Side Connection Errors

```c
CLIENT *client = get_rpc_client();
if (client == NULL) {
    // Connection failed - fallback to direct syscall
    return syscall(SYS_open, pathname, flags, mode);
}
```

**Common causes**:
- Server not running
- Wrong socket path / host / port
- Portmapper not running (TCP)
- Network issues (TCP)
- Permission denied (UNIX socket)

#### Server-Side Connection Errors

```c
int client_sock = accept(listen_sock, NULL, NULL);
if (client_sock < 0) {
    perror("accept");
    continue;  // Try to accept next client
}
```

**Common causes**:
- System resource exhaustion (too many open files)
- Signal interruption (EINTR)

### Thread Safety

**Client-Side**:
- ✅ Each thread has its own RPC client handle (`__thread CLIENT *rpc_client`)
- ✅ Independent connections prevent contention
- ✅ No locking needed

**Server-Side** (UNIX sockets):
- ⚠️ Single-threaded (one client at a time)
- To support multiple clients concurrently:
  - Option 1: Fork child process per client
  - Option 2: Thread pool
  - (Not implemented in current version)

**Server-Side** (TCP sockets):
- RPC library handles multiple clients automatically (via `select()` or `poll()`)
- Our implementation is still single-threaded per request

## Debugging Transport Issues

### UNIX Socket Issues

**Problem**: "Connection refused"

**Diagnosis**:
```bash
# Check if socket file exists
ls -l /tmp/p3_tb

# Check if server is running
ps aux | grep syscall_server

# Try connecting manually
nc -U /tmp/p3_tb
```

**Solutions**:
- Start server first
- Check socket path matches in both client/server
- Remove stale socket file: `rm /tmp/p3_tb`

**Problem**: "Permission denied"

**Diagnosis**:
```bash
ls -l /tmp/p3_tb
# Output: srwxr-xr-x 1 user1 group1 0 Jan 1 12:00 /tmp/p3_tb
```

**Solutions**:
- Run client as same user
- Change socket permissions (server side):
  ```c
  chmod("/tmp/p3_tb", 0777);  // After bind()
  ```

### TCP Socket Issues

**Problem**: "unable to register (SYSCALL_PROG, SYSCALL_VERS, tcp)"

**Diagnosis**:
```bash
# Check if rpcbind is running
systemctl status rpcbind
# OR
ps aux | grep rpcbind

# Check if portmapper is accessible
rpcinfo -p
```

**Solutions**:
- Start rpcbind: `sudo systemctl start rpcbind`
- Check firewall rules (port 111)

**Problem**: Client can't connect

**Diagnosis**:
```bash
# Check if service is registered
rpcinfo -p | grep 536870913  # SYSCALL_PROG in decimal

# Check server logs
```

**Solutions**:
- Ensure server registered successfully
- Check `TCP_HOST` matches server hostname
- Verify network connectivity: `ping localhost`

### Packet Capture

To see actual RPC messages:

```bash
# UNIX sockets (limited visibility)
strace -e trace=sendto,recvfrom -s 1000 ./build/program

# TCP sockets (full packet capture)
sudo tcpdump -i lo -X port 111  # Portmapper queries
sudo tcpdump -i lo -X port 40123  # RPC traffic (replace with actual port)
```

## Advanced Topics

### Custom Timeouts

```c
struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };  // 5 seconds
clnt_control(client, CLSET_TIMEOUT, (char *)&timeout);
```

Default timeout: 25 seconds (TIMEOUT macro in generated code)

### Retries (UDP only)

For UDP transport (not used in our project):
```c
struct timeval retry = { .tv_sec = 1, .tv_usec = 0 };  // 1 second
clnt_control(client, CLSET_RETRY_TIMEOUT, (char *)&retry);
```

TCP/UNIX don't need retries (reliable transport).

### Connection Pooling

Current implementation: One connection per thread, persistent.

Alternative (not implemented):
- Connection pool with multiple CLIENT handles
- Round-robin assignment to RPC calls
- Benefit: Higher concurrency

### Authentication

ONC RPC supports authentication mechanisms:
- AUTH_NONE (default, no authentication)
- AUTH_UNIX (UID/GID-based)
- AUTH_DES (DES encryption)
- AUTH_KERB (Kerberos)

Our implementation uses AUTH_NONE (generated code defaults to this).

To add authentication:
```c
client->cl_auth = authunix_create_default();  // UNIX auth
```

---

**Next**: [06_INTERCEPTION_MECHANISM.md](./06_INTERCEPTION_MECHANISM.md) - LD_PRELOAD details

**Prev**: [04_CODE_STRUCTURE.md](./04_CODE_STRUCTURE.md) - Code organization
