# API Reference

This document provides a complete reference for all RPC procedures, data structures, and constants in the P3_TB system.

## RPC Program Definition

```c
program SYSCALL_PROG {
    version SYSCALL_VERS {
        open_response SYSCALL_OPEN(open_request) = 1;
        close_response SYSCALL_CLOSE(close_request) = 2;
        read_response SYSCALL_READ(read_request) = 3;
        write_response SYSCALL_WRITE(write_request) = 4;
    } = 1;
} = 0x20000001;
```

### Program Identifier

```c
#define SYSCALL_PROG 0x20000001  // Program number
#define SYSCALL_VERS 1           // Version number
```

**Program number range**: 0x20000000 - 0x3fffffff (user-defined programs)

### Procedure Numbers

```c
#define SYSCALL_OPEN  1  // open() syscall
#define SYSCALL_CLOSE 2  // close() syscall
#define SYSCALL_READ  3  // read() syscall
#define SYSCALL_WRITE 4  // write() syscall
```

## Constants

### Buffer Size

```c
const MAX_BUFFER_SIZE = 1048576;  /* 1 MB */
```

Maximum size for read/write operations.

**Rationale**: 1 MB is large enough for most I/O operations while keeping RPC messages reasonable.

## Data Structures

### open_request

Request structure for `SYSCALL_OPEN` procedure.

```c
struct open_request {
    string path<4096>;      /* File path */
    int flags;              /* open() flags */
    unsigned int mode;      /* File permissions */
};
```

**Fields**:

| Field | Type | Description | Range/Values |
|-------|------|-------------|--------------|
| `path` | `string<4096>` | File path to open | Max 4096 bytes (typical PATH_MAX) |
| `flags` | `int` | open() flags | O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, etc. |
| `mode` | `unsigned int` | Permissions (if O_CREAT) | 0000-0777 (octal) |

**Common flags** (from `<fcntl.h>`):
- `O_RDONLY` (0): Open for reading only
- `O_WRONLY` (1): Open for writing only
- `O_RDWR` (2): Open for reading and writing
- `O_CREAT` (0100): Create file if it doesn't exist
- `O_TRUNC` (01000): Truncate to zero length
- `O_APPEND` (02000): Append mode

**Example**:
```c
open_request req = {
    .path = "/tmp/test.txt",
    .flags = O_CREAT | O_WRONLY | O_TRUNC,  // 0x241
    .mode = 0644  // rw-r--r--
};
```

**XDR encoding** (example):
```
Offset | Bytes                    | Description
-------|--------------------------|-------------
0x00   | 00 00 00 0E              | path length (14)
0x04   | 2F 74 6D 70 2F 74 65 73  | "/tmp/tes"
0x0C   | 74 2E 74 78 74 00 00 00  | "t.txt" + padding
0x14   | 00 00 02 41              | flags (0x241)
0x18   | 00 00 01 A4              | mode (0644 octal = 0x1A4)
```

---

### open_response

Response structure for `SYSCALL_OPEN` procedure.

```c
struct open_response {
    int fd;       /* File descriptor (client-side) */
    int result;   /* Return value */
    int err;      /* errno value */
};
```

**Fields**:

| Field | Type | Description | Values |
|-------|------|-------------|--------|
| `fd` | `int` | File descriptor assigned by server | ≥3 on success, -1 on error |
| `result` | `int` | Return value (same as fd) | ≥3 on success, -1 on error |
| `err` | `int` | errno if result < 0 | ENOENT, EACCES, etc. (see errno(3)) |

**Success example**:
```c
open_response res = {
    .fd = 3,        // Client FD
    .result = 3,
    .err = 0
};
```

**Failure example**:
```c
open_response res = {
    .fd = -1,
    .result = -1,
    .err = ENOENT  // No such file or directory
};
```

**Common errno values**:
- `ENOENT` (2): No such file or directory
- `EACCES` (13): Permission denied
- `EEXIST` (17): File exists (O_CREAT | O_EXCL)
- `EISDIR` (21): Is a directory
- `EMFILE` (24): Too many open files
- `EROFS` (30): Read-only filesystem

---

### close_request

Request structure for `SYSCALL_CLOSE` procedure.

```c
struct close_request {
    int fd;  /* File descriptor to close */
};
```

**Fields**:

| Field | Type | Description | Values |
|-------|------|-------------|--------|
| `fd` | `int` | File descriptor | Valid FD returned by open() |

**Example**:
```c
close_request req = {
    .fd = 3
};
```

---

### close_response

Response structure for `SYSCALL_CLOSE` procedure.

```c
struct close_response {
    int result;  /* Return value */
    int err;     /* errno value */
};
```

**Fields**:

| Field | Type | Description | Values |
|-------|------|-------------|--------|
| `result` | `int` | Return value | 0 on success, -1 on error |
| `err` | `int` | errno if result < 0 | EBADF, EIO, etc. |

**Success example**:
```c
close_response res = {
    .result = 0,
    .err = 0
};
```

**Failure example**:
```c
close_response res = {
    .result = -1,
    .err = EBADF  // Bad file descriptor
};
```

**Common errno values**:
- `EBADF` (9): Bad file descriptor (not open or invalid)
- `EIO` (5): I/O error
- `EINTR` (4): Interrupted by signal

---

### read_request

Request structure for `SYSCALL_READ` procedure.

```c
struct read_request {
    int fd;       /* File descriptor to read from */
    int count;    /* Number of bytes to read */
};
```

**Fields**:

| Field | Type | Description | Values |
|-------|------|-------------|--------|
| `fd` | `int` | File descriptor | Valid FD returned by open() |
| `count` | `int` | Bytes to read | 1 to MAX_BUFFER_SIZE |

**Note**: If `count > MAX_BUFFER_SIZE`, server will clamp to MAX_BUFFER_SIZE.

**Example**:
```c
read_request req = {
    .fd = 3,
    .count = 1024
};
```

---

### read_response

Response structure for `SYSCALL_READ` procedure.

```c
struct read_response {
    opaque data<MAX_BUFFER_SIZE>;  /* Data read */
    int result;                    /* Bytes read or -1 */
    int err;                       /* errno value */
};
```

**Fields**:

| Field | Type | Description | Values |
|-------|------|-------------|--------|
| `data` | `opaque<MAX_BUFFER_SIZE>` | Data read from file | Variable length |
| `result` | `int` | Bytes read | ≥0 on success, -1 on error, 0 on EOF |
| `err` | `int` | errno if result < 0 | EBADF, EIO, etc. |

**XDR opaque<> representation**:
```c
struct {
    u_int data_len;  // Actual length
    char *data_val;  // Pointer to data
} data;
```

**Success example** (14 bytes read):
```c
read_response res = {
    .data = {
        .data_len = 14,
        .data_val = "Hello, World!\n"
    },
    .result = 14,
    .err = 0
};
```

**EOF example**:
```c
read_response res = {
    .data = {
        .data_len = 0,
        .data_val = NULL
    },
    .result = 0,  // 0 = EOF
    .err = 0
};
```

**Failure example**:
```c
read_response res = {
    .data = {
        .data_len = 0,
        .data_val = NULL
    },
    .result = -1,
    .err = EBADF  // Bad file descriptor
};
```

**Common errno values**:
- `EBADF` (9): Bad file descriptor
- `EIO` (5): I/O error
- `EISDIR` (21): Is a directory (can't read directory as file)
- `EAGAIN` (11): Resource temporarily unavailable (non-blocking I/O)

---

### write_request

Request structure for `SYSCALL_WRITE` procedure.

```c
struct write_request {
    int fd;                         /* File descriptor to write to */
    opaque data<MAX_BUFFER_SIZE>;   /* Data to write */
    int count;                      /* Number of bytes to write */
};
```

**Fields**:

| Field | Type | Description | Values |
|-------|------|-------------|--------|
| `fd` | `int` | File descriptor | Valid FD returned by open() |
| `data` | `opaque<MAX_BUFFER_SIZE>` | Data to write | Variable length |
| `count` | `int` | Bytes to write | 1 to MAX_BUFFER_SIZE |

**XDR opaque<> representation**:
```c
struct {
    u_int data_len;  // Length of data
    char *data_val;  // Pointer to data
} data;
```

**Note**: `count` should equal `data.data_len`. Server uses `data.data_len`.

**Example**:
```c
write_request req = {
    .fd = 3,
    .data = {
        .data_len = 5,
        .data_val = "Hello"
    },
    .count = 5
};
```

---

### write_response

Response structure for `SYSCALL_WRITE` procedure.

```c
struct write_response {
    int result;  /* Bytes written or -1 */
    int err;     /* errno value */
};
```

**Fields**:

| Field | Type | Description | Values |
|-------|------|-------------|--------|
| `result` | `int` | Bytes written | ≥0 on success, -1 on error |
| `err` | `int` | errno if result < 0 | EBADF, ENOSPC, etc. |

**Success example**:
```c
write_response res = {
    .result = 5,  // 5 bytes written
    .err = 0
};
```

**Partial write example** (rare, but possible):
```c
write_response res = {
    .result = 3,  // Only 3 of 5 bytes written
    .err = 0      // Not an error
};
```

**Failure example**:
```c
write_response res = {
    .result = -1,
    .err = ENOSPC  // No space left on device
};
```

**Common errno values**:
- `EBADF` (9): Bad file descriptor (not open for writing)
- `ENOSPC` (28): No space left on device
- `EDQUOT` (122): Disk quota exceeded
- `EIO` (5): I/O error
- `EPIPE` (32): Broken pipe (other end closed)

---

## RPC Procedures

### SYSCALL_OPEN (Procedure 1)

Opens a file on the server and returns a file descriptor.

**Signature**:
```c
open_response SYSCALL_OPEN(open_request) = 1;
```

**Client stub**:
```c
open_response *syscall_open_1(open_request *argp, CLIENT *clnt);
```

**Server implementation**:
```c
open_response *syscall_open_1_svc(open_request *argp, struct svc_req *rqstp);
```

**Behavior**:
1. Server executes `open(argp->path, argp->flags, argp->mode)`
2. If successful:
   - Creates FD mapping (client_fd → server_fd)
   - Returns client_fd in response
3. If failure:
   - Returns -1 with errno

**Example usage** (client-side):
```c
CLIENT *client = get_rpc_client();

open_request req = {
    .path = "/tmp/test.txt",
    .flags = O_CREAT | O_WRONLY | O_TRUNC,
    .mode = 0644
};

open_response *res = syscall_open_1(&req, client);
if (res != NULL) {
    if (res->result >= 0) {
        int fd = res->result;  // Use this FD
        printf("Opened: fd=%d\n", fd);
    } else {
        printf("Error: %s\n", strerror(res->err));
    }
} else {
    printf("RPC call failed\n");
}
```

---

### SYSCALL_CLOSE (Procedure 2)

Closes a file descriptor on the server.

**Signature**:
```c
close_response SYSCALL_CLOSE(close_request) = 2;
```

**Client stub**:
```c
close_response *syscall_close_1(close_request *argp, CLIENT *clnt);
```

**Server implementation**:
```c
close_response *syscall_close_1_svc(close_request *argp, struct svc_req *rqstp);
```

**Behavior**:
1. Server looks up server_fd from client_fd
2. Executes `close(server_fd)`
3. If successful, removes FD mapping
4. Returns result

**Example usage**:
```c
close_request req = { .fd = fd };

close_response *res = syscall_close_1(&req, client);
if (res != NULL) {
    if (res->result == 0) {
        printf("Closed successfully\n");
    } else {
        printf("Error: %s\n", strerror(res->err));
    }
}
```

---

### SYSCALL_READ (Procedure 3)

Reads data from a file descriptor on the server.

**Signature**:
```c
read_response SYSCALL_READ(read_request) = 3;
```

**Client stub**:
```c
read_response *syscall_read_1(read_request *argp, CLIENT *clnt);
```

**Server implementation**:
```c
read_response *syscall_read_1_svc(read_request *argp, struct svc_req *rqstp);
```

**Behavior**:
1. Server looks up server_fd from client_fd
2. Executes `read(server_fd, buffer, count)`
3. Returns data and byte count

**Example usage**:
```c
read_request req = {
    .fd = fd,
    .count = 1024
};

read_response *res = syscall_read_1(&req, client);
if (res != NULL) {
    if (res->result > 0) {
        // Copy data from response to user buffer
        memcpy(user_buffer, res->data.data_val, res->data.data_len);
        printf("Read %d bytes\n", res->result);
    } else if (res->result == 0) {
        printf("EOF\n");
    } else {
        printf("Error: %s\n", strerror(res->err));
    }
}
```

---

### SYSCALL_WRITE (Procedure 4)

Writes data to a file descriptor on the server.

**Signature**:
```c
write_response SYSCALL_WRITE(write_request) = 4;
```

**Client stub**:
```c
write_response *syscall_write_1(write_request *argp, CLIENT *clnt);
```

**Server implementation**:
```c
write_response *syscall_write_1_svc(write_request *argp, struct svc_req *rqstp);
```

**Behavior**:
1. Server looks up server_fd from client_fd
2. Executes `write(server_fd, data, data_len)`
3. Returns byte count written

**Example usage**:
```c
const char *data = "Hello, World!\n";

write_request req = {
    .fd = fd,
    .data = {
        .data_len = strlen(data),
        .data_val = (char *)data
    },
    .count = strlen(data)
};

write_response *res = syscall_write_1(&req, client);
if (res != NULL) {
    if (res->result >= 0) {
        printf("Wrote %d bytes\n", res->result);
    } else {
        printf("Error: %s\n", strerror(res->err));
    }
}
```

---

## RPC Client API

### clnt_create()

Creates an RPC client handle (TCP transport).

```c
CLIENT *clnt_create(
    const char *host,        // Hostname or IP
    unsigned long prog,      // Program number
    unsigned long vers,      // Version number
    const char *proto        // "tcp", "udp", or "unix"
);
```

**Example**:
```c
CLIENT *client = clnt_create("localhost", SYSCALL_PROG, SYSCALL_VERS, "tcp");
if (client == NULL) {
    clnt_pcreateerror("clnt_create");
    exit(1);
}
```

### clnt_vc_create()

Creates an RPC client handle from an existing connection (UNIX sockets).

```c
CLIENT *clnt_vc_create(
    int sock,                 // Connected socket FD
    const struct netbuf *svcaddr,  // Server address
    unsigned long prog,       // Program number
    unsigned long vers,       // Version number
    unsigned int sendsz,      // Send buffer size (0 = default)
    unsigned int recvsz       // Receive buffer size (0 = default)
);
```

**Example**:
```c
int sock = socket(AF_UNIX, SOCK_STREAM, 0);
struct sockaddr_un addr = { .sun_family = AF_UNIX };
strncpy(addr.sun_path, "/tmp/p3_tb", sizeof(addr.sun_path) - 1);
connect(sock, (struct sockaddr *)&addr, sizeof(addr));

struct netbuf svcaddr = {
    .len = sizeof(addr),
    .maxlen = sizeof(addr),
    .buf = (char *)&addr
};

CLIENT *client = clnt_vc_create(sock, &svcaddr, SYSCALL_PROG, SYSCALL_VERS, 0, 0);
```

### clnt_destroy()

Destroys an RPC client handle and frees resources.

```c
void clnt_destroy(CLIENT *clnt);
```

**Example**:
```c
clnt_destroy(client);
```

---

## RPC Server API

### svc_register()

Registers an RPC service.

```c
bool_t svc_register(
    SVCXPRT *xprt,           // Service transport
    unsigned long prog,      // Program number
    unsigned long vers,      // Version number
    void (*dispatch)(struct svc_req *, SVCXPRT *),  // Dispatcher function
    unsigned long protocol   // IPPROTO_TCP, IPPROTO_UDP, or 0
);
```

**Example**:
```c
if (!svc_register(transp, SYSCALL_PROG, SYSCALL_VERS, syscall_prog_1, IPPROTO_TCP)) {
    fprintf(stderr, "Unable to register\n");
    exit(1);
}
```

### svc_run()

Enters the RPC service loop (never returns).

```c
void svc_run(void);
```

**Example**:
```c
printf("Server listening...\n");
svc_run();  // Blocks forever
```

### svcfd_create()

Creates a service transport from an existing connection.

```c
SVCXPRT *svcfd_create(
    int fd,            // Connected socket FD
    unsigned int sendsz,   // Send buffer size (0 = default)
    unsigned int recvsz    // Receive buffer size (0 = default)
);
```

**Example**:
```c
int client_sock = accept(listen_sock, NULL, NULL);
SVCXPRT *transp = svcfd_create(client_sock, 0, 0);
```

### svctcp_create()

Creates a TCP service transport (automatic listen).

```c
SVCXPRT *svctcp_create(
    int sock,          // RPC_ANYSOCK for auto-creation
    unsigned int sendsz,
    unsigned int recvsz
);
```

**Example**:
```c
SVCXPRT *transp = svctcp_create(RPC_ANYSOCK, 0, 0);
```

---

## Error Codes

All errno values are standard POSIX error codes. See `man errno` for complete list.

### Common Error Codes

| Code | Name | Value | Description |
|------|------|-------|-------------|
| `ENOENT` | No such file or directory | 2 | File doesn't exist |
| `EIO` | I/O error | 5 | Hardware error |
| `EBADF` | Bad file descriptor | 9 | Invalid or closed FD |
| `EACCES` | Permission denied | 13 | Insufficient permissions |
| `EEXIST` | File exists | 17 | File already exists (O_CREAT\|O_EXCL) |
| `EISDIR` | Is a directory | 21 | Can't open directory as file |
| `EMFILE` | Too many open files | 24 | Process FD limit reached |
| `ENOSPC` | No space left on device | 28 | Disk full |
| `EROFS` | Read-only file system | 30 | Can't write to read-only FS |

---

**Next**: [10_DEVELOPMENT_GUIDE.md](./10_DEVELOPMENT_GUIDE.md) - Development workflow

**Prev**: [08_REQUEST_FLOW.md](./08_REQUEST_FLOW.md) - Request flow diagrams
