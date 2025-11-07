# ONC RPC & XDR Primer

This document explains ONC RPC (Open Network Computing Remote Procedure Call) and XDR (External Data Representation) for developers unfamiliar with these technologies.

## What is RPC?

**Remote Procedure Call (RPC)** makes calling a function on another machine look like calling a local function.

### Without RPC (Manual Network Programming)
```c
// Client side - lots of boilerplate!
int sock = socket(AF_INET, SOCK_STREAM, 0);
connect(sock, &server_addr, sizeof(server_addr));

// Manually serialize arguments
char buffer[1024];
int offset = 0;
memcpy(buffer + offset, &arg1, sizeof(arg1)); offset += sizeof(arg1);
memcpy(buffer + offset, &arg2, sizeof(arg2)); offset += sizeof(arg2);
memcpy(buffer + offset, str, strlen(str));     offset += strlen(str);

send(sock, buffer, offset, 0);

// Wait for response
recv(sock, buffer, sizeof(buffer), 0);

// Manually deserialize result
int result;
memcpy(&result, buffer, sizeof(result));

close(sock);
return result;
```

### With RPC (Simple!)
```c
// Client side - just call the function!
result = remote_function(arg1, arg2, str);
// RPC handles everything: connection, serialization, transmission, deserialization
```

## What is ONC RPC?

**ONC RPC** (also called Sun RPC) is a specific RPC standard developed by Sun Microsystems, now an open standard (RFC 5531).

### Key Features

1. **Language-Independent Protocol**
   - Define interface once, use from C, Python, Java, etc.

2. **Automatic Code Generation**
   - `rpcgen` tool generates client/server stubs from protocol definition

3. **Transport-Agnostic**
   - Works over TCP, UDP, UNIX sockets, or any byte stream

4. **Built-in Serialization**
   - XDR handles data marshalling automatically

5. **Service Discovery**
   - Portmapper/rpcbind for finding services (optional)

## What is XDR?

**XDR (External Data Representation)** is the serialization format used by ONC RPC.

### The Problem XDR Solves

Different computers represent data differently:

```
Integer 0x12345678 on different systems:

Big-Endian (Network):    12 34 56 78
Little-Endian (x86):     78 56 34 12

String "Hello" with different representations:
With length prefix:      05 48 65 6C 6C 6F
Null-terminated:         48 65 6C 6C 6F 00
```

**XDR provides**: A standard, portable way to represent data over the network.

### XDR Type System

| XDR Type | C Type | Description |
|----------|--------|-------------|
| `int` | `int32_t` | 32-bit signed integer |
| `unsigned int` | `uint32_t` | 32-bit unsigned integer |
| `bool` | `bool_t` | Boolean (TRUE/FALSE) |
| `string<N>` | `char*` | Variable-length string (max N bytes) |
| `opaque[N]` | `char[N]` | Fixed-length byte array |
| `opaque<N>` | `struct { u_int len; char* data; }` | Variable-length byte array |
| `struct` | `struct` | Composite type |

## How ONC RPC Works in Our Project

### Step 1: Define Protocol (protocol.x)

```c
/* Our protocol definition */
struct open_request {
    string path<4096>;     /* File path */
    int flags;             /* Open flags */
    unsigned int mode;     /* Permissions */
};

struct open_response {
    int fd;                /* File descriptor */
    int result;            /* Return value */
    int err;               /* errno */
};

program SYSCALL_PROG {
    version SYSCALL_VERS {
        open_response SYSCALL_OPEN(open_request) = 1;
    } = 1;
} = 0x20000001;
```

**Explanation:**
- `struct open_request`: Defines request parameters
- `struct open_response`: Defines return values
- `program SYSCALL_PROG`: Declares an RPC program (like a namespace)
- `version SYSCALL_VERS`: Program version (for compatibility)
- `SYSCALL_OPEN(open_request) = 1`: Procedure 1 takes open_request, returns open_response
- `= 0x20000001`: Program number (must be unique)

### Step 2: Generate Code with rpcgen

```bash
rpcgen -C protocol.x
```

**Generates:**

1. **protocol.h** - Data structures and function declarations
2. **protocol_xdr.c** - XDR serialization functions
3. **protocol_clnt.c** - Client-side stub functions
4. **protocol_svc.c** - Server-side dispatcher

### Step 3: Generated Client Stub (Simplified)

```c
// Generated in protocol_clnt.c
open_response* syscall_open_1(open_request *argp, CLIENT *clnt) {
    static open_response res;

    // 1. Serialize request with XDR
    if (!xdr_open_request(xdrs_out, argp)) {
        return NULL;
    }

    // 2. Send to server
    clnt_call(clnt, SYSCALL_OPEN, ...);

    // 3. Receive response
    // 4. Deserialize response with XDR
    if (!xdr_open_response(xdrs_in, &res)) {
        return NULL;
    }

    return &res;
}
```

**We just call:**
```c
CLIENT *client = get_rpc_client();
open_request req = { .path = "/tmp/file.txt", .flags = O_RDONLY };
open_response *res = syscall_open_1(&req, client);
if (res) {
    int fd = res->fd;
    int err = res->err;
}
```

### Step 4: Implement Server Procedure

We implement the actual server-side function:

```c
// In rpc_server.c
open_response* syscall_open_1_svc(open_request *req, struct svc_req *rqstp) {
    static open_response res;

    // Execute the actual syscall
    int server_fd = open(req->path, req->flags, req->mode);

    // Map FDs
    res.fd = add_fd_mapping(server_fd);
    res.result = (server_fd >= 0) ? res.fd : -1;
    res.err = errno;

    return &res;  // RPC automatically serializes and sends back
}
```

**The server dispatcher (generated) handles:**
- Receiving request
- Deserializing arguments
- Calling our `syscall_open_1_svc()`
- Serializing result
- Sending response

## XDR Serialization in Detail

### Example: Serializing open_request

Given:
```c
open_request req = {
    .path = "/tmp/test.txt",
    .flags = 577,      // O_CREAT | O_WRONLY | O_TRUNC
    .mode = 0644
};
```

**XDR Wire Format:**
```
Offset | Bytes                    | Description
-------|--------------------------|-------------
0x00   | 00 00 00 0E              | String length (14)
0x04   | 2F 74 6D 70 2F 74 65 73  | "/tmp/tes"
0x0C   | 74 2E 74 78 74 00 00 00  | "t.txt" + padding
0x14   | 00 00 02 41              | flags (577)
0x18   | 00 00 01 A4              | mode (0644)
```

**Notes:**
- All integers are 32-bit, network byte order (big-endian)
- Strings are length-prefixed
- Data padded to 4-byte boundaries
- XDR functions handle this automatically!

### XDR Functions Generated for Our Types

```c
// In protocol_xdr.c (generated)

bool_t xdr_open_request(XDR *xdrs, open_request *objp) {
    if (!xdr_string(xdrs, &objp->path, 4096))  // Serialize path
        return FALSE;
    if (!xdr_int(xdrs, &objp->flags))          // Serialize flags
        return FALSE;
    if (!xdr_u_int(xdrs, &objp->mode))         // Serialize mode
        return FALSE;
    return TRUE;
}

bool_t xdr_open_response(XDR *xdrs, open_response *objp) {
    if (!xdr_int(xdrs, &objp->fd))             // Serialize fd
        return FALSE;
    if (!xdr_int(xdrs, &objp->result))         // Serialize result
        return FALSE;
    if (!xdr_int(xdrs, &objp->err))            // Serialize errno
        return FALSE;
    return TRUE;
}
```

## Variable-Length Arrays with opaque<>

One of the most useful XDR features for our project:

```c
// In protocol.x
struct read_response {
    opaque data<1048576>;  /* Up to 1MB of data */
    int result;
    int err;
};
```

**Generated C Structure:**
```c
struct read_response {
    struct {
        u_int data_len;    /* Actual length */
        char *data_val;    /* Pointer to data */
    } data;
    int result;
    int err;
};
```

**Usage on Server:**
```c
read_response* syscall_read_1_svc(read_request *req, struct svc_req *rqstp) {
    static read_response res;
    static char buffer[1048576];

    ssize_t bytes_read = read(server_fd, buffer, req->count);

    res.data.data_val = buffer;         // Point to buffer
    res.data.data_len = bytes_read;     // Set actual length
    res.result = bytes_read;

    return &res;  // XDR serializes only data_len bytes!
}
```

**Wire Format (reading 10 bytes):**
```
Offset | Bytes      | Description
-------|------------|-------------
0x00   | 00 00 00 0A| data_len = 10
0x04   | XX XX ...  | 10 bytes of actual data
0x0E   | 00 00      | Padding to 4-byte boundary
0x10   | 00 00 00 0A| result = 10
0x14   | 00 00 00 00| err = 0
```

Efficient! Only 10 bytes transferred, not the full 1MB buffer.

## Transport Layer

ONC RPC is transport-agnostic. Our project supports:

### UNIX Domain Sockets
```c
// Client side
int sock = socket(AF_UNIX, SOCK_STREAM, 0);
connect(sock, &server_addr, sizeof(server_addr));
CLIENT *client = clnt_vc_create(sock, &svcaddr, PROG, VERS, 0, 0);
```

### TCP Sockets
```c
// Client side (automatic connection)
CLIENT *client = clnt_create("localhost", PROG, VERS, "tcp");
```

**The RPC layer handles:**
- Connection management
- Request/response matching
- Retries (for UDP)
- Timeouts

## Service Registration with Portmapper

For TCP (not needed for UNIX sockets):

```c
// Server side
SVCXPRT *transp = svctcp_create(RPC_ANYSOCK, 0, 0);
svc_register(transp, SYSCALL_PROG, SYSCALL_VERS, syscall_prog_1, IPPROTO_TCP);
```

**Portmapper** (rpcbind service):
- Maps program numbers to ports
- Clients query: "What port is SYSCALL_PROG on?"
- Portmapper responds: "Port 43210"
- Client connects to port 43210

## RPC Call Flow

```
Client                          Network                      Server
======                          =======                      ======

1. Call Function
   syscall_open_1(&req, client)

2. Client Stub
   ├─ Serialize req with XDR
   │  (xdr_open_request)
   │
   ├─ Add RPC header ──────────────────────────────────────→
   │  (program, version, proc)
   │                                                             3. Server Dispatcher
   │                                                                ├─ Validate header
   │                                                                ├─ Deserialize args
   │                                                                │  (xdr_open_request)
   │                                                                │
   │                                                                4. Call Server Func
   │                                                                   syscall_open_1_svc()
   │                                                                   ├─ Execute open()
   │                                                                   └─ Return result
   │
   │                                                                5. Serialize Result
   │  ←─────────────────────────────────────────────────────       (xdr_open_response)
   │
6. Deserialize Response
   (xdr_open_response)

7. Return to Caller
   return &res;
```

## Practical Tips for Understanding RPC Code

### 1. Recognize Generated vs. Hand-Written Code

**Generated files** (don't edit these!):
- `protocol.h` - Data structures
- `protocol_xdr.c` - Serialization functions
- `protocol_clnt.c` - Client stubs
- `protocol_svc.c` - Server dispatcher

**Hand-written files**:
- `protocol.x` - Protocol definition
- `rpc_server.c` - Server implementation (`*_svc` functions)
- `rpc_client.c` - Client setup and connection management
- `intercept/*.h` - Interceptors calling RPC

### 2. Function Naming Convention

```
syscall_open_1          // Client stub (generated)
syscall_open_1_svc      // Server implementation (you write)
xdr_open_request        // XDR serialization (generated)
```

Pattern: `<procedure>_<version>` and `<procedure>_<version>_svc`

### 3. Understanding Static Variables

```c
open_response* syscall_open_1_svc(...) {
    static open_response res;  // ← Why static?
    //...
    return &res;
}
```

**Reason**: RPC serializes before the function returns. The pointer must remain valid after return for serialization. Static storage persists.

### 4. Reading XDR Protocol Files

```c
struct my_struct {
    int field1;
    string field2<100>;
    opaque field3<>;
};
```

Think of it as:
```c
typedef struct {
    int32_t field1;
    char *field2;                    // max 100 chars
    struct {
        u_int len;
        char *data;
    } field3;                        // any length
} my_struct;
```

## Common Pitfalls

### ❌ Wrong: Modifying Generated Code
```c
// In protocol_clnt.c (generated)
open_response* syscall_open_1(...) {
    // Don't edit this! It will be overwritten by rpcgen
}
```
**Fix**: Edit `protocol.x` and regenerate.

### ❌ Wrong: Returning Local Variables
```c
open_response* syscall_open_1_svc(...) {
    open_response res;  // ← Stack variable!
    return &res;        // ← Dangling pointer after return!
}
```
**Fix**: Use `static` or allocated memory.

### ❌ Wrong: Forgetting to Set data_len
```c
read_response* syscall_read_1_svc(...) {
    static char buffer[1024];
    res.data.data_val = buffer;
    // Forgot: res.data.data_len = bytes_read;
    return &res;  // XDR will use garbage length!
}
```
**Fix**: Always set both `data_val` and `data_len`.

## Further Reading

### RPC Specifications
- [RFC 5531 - RPC Protocol Specification](https://www.rfc-editor.org/rfc/rfc5531.html)
- [RFC 4506 - XDR Specification](https://www.rfc-editor.org/rfc/rfc4506.html)

### Books
- "UNIX Network Programming, Vol 2" by W. Richard Stevens (Chapter on RPC)
- "Understanding Linux Network Internals" (RPC/XDR sections)

### Online Resources
- `man rpcgen` - rpcgen compiler documentation
- `man xdr` - XDR library functions
- libtirpc source code

---

**Next:** [04_CODE_STRUCTURE.md](./04_CODE_STRUCTURE.md) - Understanding the codebase structure

**Prev:** [02_ARCHITECTURE.md](./02_ARCHITECTURE.md) - System architecture overview
