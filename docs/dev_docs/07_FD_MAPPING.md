# File Descriptor Mapping

This document explains why FD mapping is necessary and how it's implemented in the server.

## The FD Mapping Problem

### Background: What Are File Descriptors?

A **file descriptor** (FD) is a small non-negative integer that identifies an open file within a process. Each process has its own independent FD table:

```
Process A FD Table          Process B FD Table
┌────┬──────────────┐      ┌────┬──────────────┐
│ 0  │ stdin        │      │ 0  │ stdin        │
│ 1  │ stdout       │      │ 1  │ stdout       │
│ 2  │ stderr       │      │ 2  │ stderr       │
│ 3  │ /tmp/a.txt   │      │ 3  │ /tmp/b.txt   │  ← Different files!
│ 4  │ /tmp/c.txt   │      │ 4  │ /tmp/d.txt   │
└────┴──────────────┘      └────┴──────────────┘
```

FD 3 in Process A is **completely independent** from FD 3 in Process B.

### The Problem in Our System

```
Client Process                    Server Process
══════════════                    ══════════════

open("/tmp/test.txt", ...)
    → Server executes: open(...)
    → Server gets FD 5 ──────────→ FD Table:
                                       3: (some file)
                                       4: (socket to client)
                                       5: /tmp/test.txt ← Real FD

Client expects: FD 3 ←─────────── What to return?

Later:
read(3, buffer, 100)
    → Client sends "fd=3"
    → Server receives "fd=3"
    → Server needs to know:        FD 3 in client
         "fd=3" means FD 5 ────→   = FD 5 in server
```

**Key Insight**: Client and server have independent FD spaces. We need bidirectional mapping:

```
Client FD ←→ Server FD
    3     ←→     5
    4     ←→     7
    5     ←→     9
```

## Why Not Use Server FDs Directly?

### Option 1: Return Server FD to Client (Bad!)

```c
// Server
int server_fd = open(req->path, req->flags, req->mode);
return server_fd;  // Return 5

// Client
int fd = syscall_open_1(...);  // Gets 5
// Client thinks it has FD 5

// Later, client opens another file locally
int local_fd = open("/etc/passwd", O_RDONLY);  // Kernel assigns FD 5!

// Now client has TWO "FD 5"s:
//   - Remote FD 5 (on server) = /tmp/test.txt
//   - Local FD 5 (on client) = /etc/passwd

// Chaos!
read(5, buffer, 100);  // Which FD 5???
```

**Problem**: FD collision between remote and local files.

### Option 2: Use FD Mapping (Good!)

```c
// Server
int server_fd = open(req->path, req->flags, req->mode);  // Gets 5
int client_fd = add_fd_mapping(server_fd);               // Creates mapping 3→5
return client_fd;  // Return 3

// Client
int fd = syscall_open_1(...);  // Gets 3
// Client thinks it has FD 3 (consistent with local FD allocation)

// Later:
read(3, buffer, 100);
// Client sends "fd=3" to server
// Server looks up: 3 → 5
// Server calls: read(5, buffer, 100)  ← Correct!
```

**Benefits**:
- Client FDs don't collide with local FDs
- Client-side code doesn't need to know about mapping
- Server maintains bidirectional translation

## Implementation

### Data Structure (src/rpc_server.c)

```c
#define MAX_FDS 1024

// Global FD mapping table
// Index: client_fd
// Value: server_fd (-1 means unmapped)
static int fd_mapping[MAX_FDS];

// Next client FD to assign (starts at 3)
static int next_client_fd = 3;
```

**Why start at 3?**
- FD 0, 1, 2 are reserved for stdin, stdout, stderr
- Starting at 3 mimics normal file descriptor allocation

**Size limit**: 1024 FDs (typical system limit is similar: `ulimit -n`)

### Initialization

```c
void init_fd_mapping(void) {
    for (int i = 0; i < MAX_FDS; i++) {
        fd_mapping[i] = -1;  // -1 means "not mapped"
    }
    next_client_fd = 3;  // Start after stdin/stdout/stderr
}
```

Called in `main()` before starting server.

### Adding Mappings (open)

```c
int add_fd_mapping(int server_fd) {
    if (next_client_fd >= MAX_FDS) {
        fprintf(stderr, "[Server] Error: FD mapping table full\n");
        return -1;
    }

    int client_fd = next_client_fd++;
    fd_mapping[client_fd] = server_fd;

    printf("[Server] FD Mapping: client_fd=%d → server_fd=%d\n",
           client_fd, server_fd);

    return client_fd;
}
```

**Usage** (in `syscall_open_1_svc`):
```c
int server_fd = open(req->path, req->flags, req->mode);
if (server_fd >= 0) {
    int client_fd = add_fd_mapping(server_fd);
    res.fd = client_fd;  // Return client FD
    res.result = client_fd;
}
```

**Output example**:
```
[Server] open('/tmp/test.txt') -> server_fd=5
[Server] FD Mapping: client_fd=3 → server_fd=5
```

### Translating FDs (read/write/close)

```c
int get_server_fd(int client_fd) {
    if (client_fd < 0 || client_fd >= MAX_FDS) {
        fprintf(stderr, "[Server] Invalid client_fd: %d\n", client_fd);
        return -1;
    }

    int server_fd = fd_mapping[client_fd];

    if (server_fd < 0) {
        fprintf(stderr, "[Server] Unmapped client_fd: %d\n", client_fd);
        return -1;
    }

    return server_fd;
}
```

**Usage** (in `syscall_read_1_svc`):
```c
int server_fd = get_server_fd(req->fd);
if (server_fd < 0) {
    res.result = -1;
    res.err = EBADF;  // Bad file descriptor
    return &res;
}

ssize_t bytes_read = read(server_fd, buffer, req->count);
```

**Error handling**: If client sends invalid FD (not mapped), return EBADF error.

### Removing Mappings (close)

```c
void remove_fd_mapping(int client_fd) {
    if (client_fd < 0 || client_fd >= MAX_FDS) {
        return;
    }

    int server_fd = fd_mapping[client_fd];
    if (server_fd >= 0) {
        printf("[Server] Removing FD Mapping: client_fd=%d → server_fd=%d\n",
               client_fd, server_fd);
    }

    fd_mapping[client_fd] = -1;  // Mark as unmapped
}
```

**Usage** (in `syscall_close_1_svc`):
```c
int server_fd = get_server_fd(req->fd);
if (server_fd < 0) {
    res.result = -1;
    res.err = EBADF;
    return &res;
}

int result = close(server_fd);  // Close actual file

if (result >= 0) {
    remove_fd_mapping(req->fd);  // Remove mapping
}

res.result = result;
res.err = (result < 0) ? errno : 0;
```

**Note**: Only remove mapping if close() succeeds. If close() fails, mapping remains (FD still open).

## Complete Lifecycle Example

### Scenario: Client opens, writes, and closes a file

```
Client                          Server
======                          ======

1. open("/tmp/test.txt", O_WRONLY | O_CREAT, 0644)
    ↓
   [Interceptor packages request]
    ↓
   [RPC call] ──────────────────→ syscall_open_1_svc(req)
                                       ↓
                                   int server_fd = open(...);
                                   // Kernel assigns FD 5
                                       ↓
                                   int client_fd = add_fd_mapping(5);
                                   // next_client_fd = 3
                                   // fd_mapping[3] = 5
                                   // next_client_fd = 4
                                   // Returns 3
    ↓                                  ↓
   [RPC response] ←─────────────── return client_fd=3
    ↓
   int fd = 3;  // Client sees FD 3


2. write(3, "Hello", 5)
    ↓
   [Interceptor packages request: fd=3]
    ↓
   [RPC call] ──────────────────→ syscall_write_1_svc(req)
                                       ↓
                                   int server_fd = get_server_fd(3);
                                   // Lookup: fd_mapping[3] = 5
                                   // Returns 5
                                       ↓
                                   write(5, "Hello", 5);
                                   // Writes to actual file
                                       ↓
    ↓                              return result=5
   [RPC response] ←───────────────
    ↓
   ssize_t n = 5;  // Client sees 5 bytes written


3. close(3)
    ↓
   [Interceptor packages request: fd=3]
    ↓
   [RPC call] ──────────────────→ syscall_close_1_svc(req)
                                       ↓
                                   int server_fd = get_server_fd(3);
                                   // Lookup: fd_mapping[3] = 5
                                       ↓
                                   close(5);
                                   // Closes actual file
                                       ↓
                                   remove_fd_mapping(3);
                                   // fd_mapping[3] = -1
                                       ↓
    ↓                              return result=0
   [RPC response] ←───────────────
    ↓
   int ret = 0;  // Client sees success
```

### FD Mapping Table Evolution

```
After open():
  fd_mapping[3] = 5
  next_client_fd = 4

After close():
  fd_mapping[3] = -1  (unmapped)
  next_client_fd = 4  (unchanged)
```

**Note**: `next_client_fd` is never decremented. Closed FDs are not reused in current implementation.

## Edge Cases and Error Handling

### Case 1: Double Close

```c
// Client
int fd = open("/tmp/test.txt", O_RDONLY);  // fd=3
close(fd);  // OK
close(fd);  // Double close!
```

**Server behavior**:
```c
// First close(3)
int server_fd = get_server_fd(3);  // Returns 5
close(5);  // OK
remove_fd_mapping(3);  // fd_mapping[3] = -1

// Second close(3)
int server_fd = get_server_fd(3);  // Returns -1 (unmapped)
// Return EBADF error to client
```

**Result**: Client gets EBADF error (correct behavior, matches normal close()).

### Case 2: Invalid FD

```c
// Client
close(999);  // Never opened
```

**Server behavior**:
```c
int server_fd = get_server_fd(999);  // Returns -1 (unmapped or out of range)
// Return EBADF error
```

**Result**: Client gets EBADF error (correct).

### Case 3: FD Table Full

```c
// Client opens 1021 files (3 to 1023)
for (int i = 0; i < 1021; i++) {
    open("/tmp/test.txt", O_RDONLY);
}
// Tries to open one more
int fd = open("/tmp/test.txt", O_RDONLY);  // fd = ?
```

**Server behavior**:
```c
int server_fd = open(...);  // Succeeds, gets FD X

int client_fd = add_fd_mapping(server_fd);
// next_client_fd = 1024 (>= MAX_FDS)
// Returns -1

// Must close the server_fd we just opened!
close(server_fd);

// Return EMFILE error (too many open files)
```

**Current implementation**: Does NOT handle this correctly (potential FD leak).

**Fix needed**:
```c
int add_fd_mapping(int server_fd) {
    if (next_client_fd >= MAX_FDS) {
        fprintf(stderr, "[Server] Error: FD mapping table full\n");
        close(server_fd);  // Close immediately!
        errno = EMFILE;
        return -1;
    }
    // ... rest of function
}

// In syscall_open_1_svc:
int client_fd = add_fd_mapping(server_fd);
if (client_fd < 0) {
    res.fd = -1;
    res.result = -1;
    res.err = errno;  // EMFILE
    return &res;
}
```

### Case 4: Server Restart

**Problem**: If server restarts, all FD mappings are lost.

**Client behavior**:
```c
int fd = open("/tmp/test.txt", O_RDONLY);  // Before restart: fd=3, server_fd=5
// Server restarts
read(fd, buffer, 100);  // After restart: client sends fd=3
```

**Server behavior**:
```c
// After restart, fd_mapping is reinitialized
int server_fd = get_server_fd(3);  // Returns -1 (unmapped)
// Returns EBADF error
```

**Result**: Client gets errors after server restart. This is expected (connection lost).

**Solution**: Client should detect connection loss and handle gracefully (reconnect, reopen files).

## Design Alternatives

### Alternative 1: Hash Map

**Current**: Array with index = client_fd

**Alternative**: Hash map with key = client_fd, value = server_fd

**Pros**:
- Sparse FD allocation (reuse closed FDs)
- No MAX_FDS limit

**Cons**:
- More complex implementation
- Slightly slower lookup

**Verdict**: Array is simpler and fast enough for our use case.

### Alternative 2: FD Passing via SCM_RIGHTS

**Concept**: Use `sendmsg()` with `SCM_RIGHTS` to pass actual file descriptors over UNIX sockets.

**How it works**:
```c
// Server opens file
int server_fd = open("/tmp/test.txt", O_RDONLY);

// Server sends FD to client via UNIX socket
send_fd_over_socket(socket_fd, server_fd);

// Client receives FD (kernel duplicates it into client's FD table)
int client_fd = recv_fd_over_socket(socket_fd);

// Client can now use client_fd directly (refers to same open file)
read(client_fd, buffer, 100);  // No RPC needed!
```

**Pros**:
- No FD mapping needed
- No RPC for read/write/close (just for open)
- Lower latency

**Cons**:
- Only works with UNIX sockets (no TCP support)
- Complex implementation
- Kernel-specific behavior
- Doesn't work across machines

**Verdict**: FD mapping is more portable and simpler.

### Alternative 3: UUIDs Instead of FDs

**Concept**: Return UUID strings instead of integer FDs.

```c
// Server
int server_fd = open(...);
char *uuid = generate_uuid();  // "550e8400-e29b-41d4-a716-446655440000"
map_uuid_to_fd[uuid] = server_fd;
return uuid;

// Client
char *uuid = syscall_open_1(...);  // Gets UUID

// Later
read(uuid, buffer, 100);  // Client sends UUID
```

**Pros**:
- No FD number collision possible
- Can support multiple servers

**Cons**:
- Incompatible with standard syscall interface (expects int, not string)
- More complex
- Performance overhead (string comparison)

**Verdict**: Integer FDs are standard and expected by programs.

## Performance Considerations

### Lookup Time Complexity

**Array-based**:
- Add: O(1)
- Lookup: O(1)
- Remove: O(1)

**Hash map**:
- Add: O(1) average, O(n) worst case
- Lookup: O(1) average, O(n) worst case
- Remove: O(1) average, O(n) worst case

**Conclusion**: Array is optimal for this use case.

### Memory Usage

```c
static int fd_mapping[MAX_FDS];  // 1024 * 4 bytes = 4 KB
```

**4 KB is negligible** for modern systems.

If we wanted to support 1 million FDs:
```c
static int fd_mapping[1000000];  // 4 MB
```

Still acceptable, but hash map might be better (only allocates for used FDs).

### Cache Locality

Array access has excellent cache locality:
```c
int server_fd = fd_mapping[client_fd];  // Single memory access, likely in cache
```

Hash maps have worse cache locality (pointer chasing, hash collisions).

## Thread Safety

### Current Implementation: Single-Threaded Server

FD mapping table is **global** (not thread-local):
```c
static int fd_mapping[MAX_FDS];  // Shared across all server threads
```

**Thread safety**: NOT thread-safe (no locking).

**Why it's OK**: Server handles one client at a time (single-threaded).

### Multi-Threaded Server (Future)

If we add multi-threading, we need synchronization:

**Option 1: Global Lock**
```c
static pthread_mutex_t fd_mapping_lock = PTHREAD_MUTEX_INITIALIZER;

int add_fd_mapping(int server_fd) {
    pthread_mutex_lock(&fd_mapping_lock);
    // ... add mapping
    pthread_mutex_unlock(&fd_mapping_lock);
}
```

**Option 2: Per-Client Mapping**
```c
typedef struct {
    int fd_mapping[MAX_FDS];
    int next_client_fd;
} client_state_t;

// Each client connection has its own state
client_state_t *client_state = malloc(sizeof(client_state_t));
```

**Verdict**: Per-client mapping is cleaner (no locking needed).

## Debugging FD Mapping

### Logging

Current implementation includes debug prints:
```c
printf("[Server] FD Mapping: client_fd=%d → server_fd=%d\n", client_fd, server_fd);
printf("[Server] Removing FD Mapping: client_fd=%d → server_fd=%d\n", client_fd, server_fd);
```

**Example output**:
```
[Server] open('/tmp/test.txt') -> server_fd=5
[Server] FD Mapping: client_fd=3 → server_fd=5
[Server] read(client_fd=3, server_fd=5, count=1024)
[Server] Removing FD Mapping: client_fd=3 → server_fd=5
```

### Inspecting FD Table

Add debug function:
```c
void dump_fd_mapping(void) {
    printf("[Server] FD Mapping Table:\n");
    for (int i = 0; i < MAX_FDS; i++) {
        if (fd_mapping[i] >= 0) {
            printf("  client_fd=%d → server_fd=%d\n", i, fd_mapping[i]);
        }
    }
}
```

Call from signal handler (e.g., SIGUSR1):
```c
void handle_sigusr1(int sig) {
    dump_fd_mapping();
}

signal(SIGUSR1, handle_sigusr1);
```

Usage:
```bash
# Send SIGUSR1 to server
kill -SIGUSR1 $(pidof syscall_server)

# Output:
# [Server] FD Mapping Table:
#   client_fd=3 → server_fd=5
#   client_fd=4 → server_fd=7
```

### Detecting FD Leaks

Track open FDs:
```c
static int open_fd_count = 0;

int add_fd_mapping(int server_fd) {
    // ...
    open_fd_count++;
    return client_fd;
}

void remove_fd_mapping(int client_fd) {
    // ...
    if (server_fd >= 0) {
        open_fd_count--;
    }
    // ...
}

// Periodically log
printf("[Server] Open FDs: %d\n", open_fd_count);
```

If `open_fd_count` keeps growing without bound, there's a leak (close() not called).

---

**Next**: [08_REQUEST_FLOW.md](./08_REQUEST_FLOW.md) - Complete request lifecycle with PlantUML diagrams

**Prev**: [06_INTERCEPTION_MECHANISM.md](./06_INTERCEPTION_MECHANISM.md) - LD_PRELOAD interception
