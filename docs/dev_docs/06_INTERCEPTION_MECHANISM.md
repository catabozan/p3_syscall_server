# Interception Mechanism

This document explains how LD_PRELOAD works and how we use it to transparently intercept syscalls.

## What is LD_PRELOAD?

**LD_PRELOAD** is a Linux dynamic linker feature that allows you to load shared libraries before any other libraries (including libc) when a program starts.

### Dynamic Linking Basics

When you run a program:

```
Program Executable (./program)
        ↓
Dynamic Linker (/lib64/ld-linux-x86-64.so.2)
        ↓
Load shared libraries in order:
    1. Libraries specified in LD_PRELOAD
    2. Libraries listed in DT_NEEDED (from binary)
    3. System libraries (libc, libpthread, etc.)
        ↓
Resolve symbols (function names → addresses)
    ↓
Jump to program's main()
```

**Symbol Resolution Rule**: First definition wins!

If two libraries define `open()`:
1. `intercept.so` defines `open()` ✓ (loaded first via LD_PRELOAD)
2. `libc.so` defines `open()` ✗ (ignored, symbol already resolved)

Result: Our `open()` is called instead of libc's.

### Example

```bash
# Without LD_PRELOAD
./program  # Calls libc's open()

# With LD_PRELOAD
LD_PRELOAD=./intercept.so ./program  # Calls our open() in intercept.so
```

### Advantages

✅ **No source code modification** - Works with any binary
✅ **No recompilation** - Preload library at runtime
✅ **Selective interception** - Only override specific functions
✅ **Transparent** - Program doesn't know it's being intercepted
✅ **Reversible** - Remove LD_PRELOAD to disable interception

### Limitations

❌ **Only libc wrappers** - Can't intercept direct syscalls (`syscall(SYS_open, ...)`)
❌ **Statically linked binaries** - No dynamic linking, no interception
❌ **SUID/SGID programs** - Security restriction, LD_PRELOAD ignored
❌ **Kernel bypasses** - vDSO (virtual dynamic shared object) may bypass libc

## How We Use LD_PRELOAD

### Our Shared Library: intercept.so

Built from `src/intercept_client.c`:

```c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

// Include RPC protocol
#include "protocol/protocol.h"

// Include RPC client
#include "rpc_client.c"

// Include all interceptors
#include "intercept/intercept_open.h"
#include "intercept/intercept_close.h"
#include "intercept/intercept_read.h"
#include "intercept/intercept_write.h"
```

**Compilation**:
```bash
gcc -shared -fPIC -o build/intercept.so src/intercept_client.c \
    src/protocol/protocol_xdr.c src/protocol/protocol_clnt.c \
    $(pkg-config --cflags --libs libtirpc)
```

**Flags**:
- `-shared`: Build shared library (.so)
- `-fPIC`: Position-independent code (required for shared libraries)

### Interceptor Pattern

Each syscall follows this pattern (using `open()` as example):

```c
#ifndef INTERCEPT_OPEN_H
#define INTERCEPT_OPEN_H

#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <errno.h>

// ════════════════════════════════════════════════════════════════
// STEP 1: Thread-local reentry guard
// ════════════════════════════════════════════════════════════════
static __thread int in_open_intercept = 0;

// ════════════════════════════════════════════════════════════════
// STEP 2: Override libc function
// ════════════════════════════════════════════════════════════════
int open(const char *pathname, int flags, ...) {
    // ────────────────────────────────────────────────────────────
    // STEP 3: Extract variadic arguments
    // ────────────────────────────────────────────────────────────
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    // ────────────────────────────────────────────────────────────
    // STEP 4: Check reentry guards (CRITICAL!)
    // ────────────────────────────────────────────────────────────
    if (in_open_intercept || is_rpc_in_progress()) {
        // We're inside interceptor or RPC is active
        // → Use direct syscall to avoid infinite recursion
        return syscall(SYS_open, pathname, flags, mode);
    }

    // ────────────────────────────────────────────────────────────
    // STEP 5: Set reentry guard
    // ────────────────────────────────────────────────────────────
    in_open_intercept = 1;

    // ────────────────────────────────────────────────────────────
    // STEP 6: Get RPC client connection
    // ────────────────────────────────────────────────────────────
    CLIENT *client = get_rpc_client();

    int result = -1;

    if (client != NULL) {
        // ────────────────────────────────────────────────────────
        // STEP 7: Package arguments into RPC request
        // ────────────────────────────────────────────────────────
        open_request req = {
            .path = (char*)pathname,  // Cast away const (XDR limitation)
            .flags = flags,
            .mode = mode
        };

        // ────────────────────────────────────────────────────────
        // STEP 8: Make RPC call (with global guard)
        // ────────────────────────────────────────────────────────
        rpc_in_progress = 1;  // Disable ALL interceptors
        open_response *res = syscall_open_1(&req, client);
        rpc_in_progress = 0;  // Re-enable interceptors

        // ────────────────────────────────────────────────────────
        // STEP 9: Extract result and errno
        // ────────────────────────────────────────────────────────
        if (res != NULL) {
            result = res->result;
            errno = res->err;  // Propagate server's errno
        } else {
            // RPC call failed (network error, server crash, etc.)
            result = -1;
            errno = EIO;  // Generic I/O error
        }
    } else {
        // ────────────────────────────────────────────────────────
        // STEP 10: Fallback to direct syscall if RPC unavailable
        // ────────────────────────────────────────────────────────
        result = syscall(SYS_open, pathname, flags, mode);
    }

    // ────────────────────────────────────────────────────────────
    // STEP 11: Clear reentry guard
    // ────────────────────────────────────────────────────────────
    in_open_intercept = 0;

    // ────────────────────────────────────────────────────────────
    // STEP 12: Return result to caller
    // ────────────────────────────────────────────────────────────
    return result;
}

// ════════════════════════════════════════════════════════════════
// Also intercept open64() (large file support)
// ════════════════════════════════════════════════════════════════
int open64(const char *pathname, int flags, ...) {
    // Same implementation as open()
    // ...
}

#endif // INTERCEPT_OPEN_H
```

### Key Design Elements

#### 1. Thread-Local Reentry Guards

```c
static __thread int in_open_intercept = 0;
```

**Purpose**: Prevent infinite recursion within same interceptor

**Why `__thread`?**
- Each thread gets its own copy of the variable
- Thread A can be inside `open()` while Thread B is not
- No need for locking

**Recursion Scenario**:
```
User program calls open()
    → Our open() interceptor
        → Some code calls printf() for debugging
            → printf() internally calls write()
                → Our write() interceptor
                    → write() needs to log to a file
                        → Calls open() ← RECURSION!
```

**Without guard**: Infinite loop, stack overflow
**With guard**: Second `open()` call uses `syscall(SYS_open, ...)` directly

#### 2. Global RPC Progress Flag

```c
static __thread int rpc_in_progress = 0;

void rpc_in_progress = int value) {
    rpc_in_progress = value;
}

int is_rpc_in_progress(void) {
    return rpc_in_progress;
}
```

**Purpose**: Prevent ALL interceptors from activating during RPC operations

**Why Needed?**

RPC library internally calls syscalls:
```
get_rpc_client() creates connection
    → socket()        (OK, not intercepted)
    → connect()       (OK, not intercepted)
    → open("/etc/resolv.conf")  ← INTERCEPTED! (for hostname resolution)
        → Our open() interceptor
            → get_rpc_client()  ← RECURSION!
```

**Solution**: Set `rpc_in_progress = 1` during RPC initialization and calls

```c
CLIENT *get_rpc_client(void) {
    if (rpc_client != NULL) {
        return rpc_client;
    }

    rpc_in_progress = 1;  // Disable interceptors

    // Create connection (may internally call open/close/read/write)
    rpc_client = clnt_vc_create(...);

    rpc_in_progress = 0;  // Re-enable interceptors

    return rpc_client;
}
```

All interceptors check this flag:
```c
if (in_open_intercept || is_rpc_in_progress()) {
    return syscall(SYS_open, pathname, flags, mode);
}
```

#### 3. Direct Syscall Fallback

```c
#include <sys/syscall.h>

syscall(SYS_open, pathname, flags, mode);
```

**What is `syscall()`?**
- libc function that makes raw syscalls
- Bypasses all wrappers
- Takes syscall number + arguments

**When Used**:
1. Inside interceptors (reentry protection)
2. When RPC client is NULL (server unavailable)
3. When `rpc_in_progress` is set

**Syscall Numbers**:
```c
#define SYS_open   2    // x86_64
#define SYS_close  3
#define SYS_read   0
#define SYS_write  1
```

Defined in `<sys/syscall.h>` and `<asm/unistd.h>`.

#### 4. Variadic Function Handling

```c
int open(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }
    // ...
}
```

**Why Variadic?**

`open()` signature:
```c
int open(const char *pathname, int flags, ...);
```

The `mode` parameter is only required when creating a file (`O_CREAT`).

**Without O_CREAT**:
```c
open("/tmp/file.txt", O_RDONLY);  // No mode
```

**With O_CREAT**:
```c
open("/tmp/file.txt", O_CREAT | O_WRONLY, 0644);  // mode = 0644
```

Our interceptor must handle both cases.

## Thread Safety

### Challenge

Multiple threads calling intercepted syscalls simultaneously:

```
Thread 1: open("file1.txt", ...)
Thread 2: read(fd, ...)
Thread 3: write(fd, ...)
```

### Solution: Thread-Local Storage

All state is thread-local:

```c
// In interceptors
static __thread int in_open_intercept = 0;
static __thread int in_close_intercept = 0;
static __thread int in_read_intercept = 0;
static __thread int in_write_intercept = 0;

// In RPC client
static __thread CLIENT *rpc_client = NULL;
static __thread int in_rpc_init = 0;
static __thread int rpc_in_progress = 0;
```

**Result**: Each thread has:
- Independent reentry guards
- Separate RPC connection
- Own initialization state

**No locking needed!**

### Concurrency Example

```
Thread 1                        Thread 2
════════                        ════════

open("file1.txt", ...)
├─ in_open_intercept[T1] = 1
├─ get_rpc_client()
│  └─ rpc_client[T1] = ...
├─ RPC call                    read(fd, ...)
│                              ├─ in_read_intercept[T2] = 1
│                              ├─ get_rpc_client()
│                              │  └─ rpc_client[T2] = ...
│                              ├─ RPC call (independent!)
│                              │  └─ Uses rpc_client[T2]
│  └─ Uses rpc_client[T1]     │
├─ Get response                ├─ Get response
└─ in_open_intercept[T1] = 0  └─ in_read_intercept[T2] = 0
```

Threads never interfere because they use independent state.

## Recursion Prevention Deep Dive

### Problem Scenarios

#### Scenario 1: Same Interceptor Recursion

```
User program
    → open("file.txt", O_RDONLY)
        → [Interceptor] open()
            → (some buggy code calls open() internally)
                → [Interceptor] open() ← RECURSION
```

**Solution**: Per-interceptor guard (`in_open_intercept`)

#### Scenario 2: Cross-Interceptor Recursion

```
User program
    → open("file.txt", O_RDONLY)
        → [Interceptor] open()
            → get_rpc_client()
                → RPC library calls open("/etc/nsswitch.conf")
                    → [Interceptor] open() ← RECURSION
```

**Solution**: Global `rpc_in_progress` flag

#### Scenario 3: Multi-Level Recursion

```
User program
    → write(1, "data", 4)  // stdout
        → [Interceptor] write()
            → printf("[Debug] write() called\n")
                → write(1, "[Debug]...", ...)
                    → [Interceptor] write() ← RECURSION
```

**Solution**: Per-interceptor guard (`in_write_intercept`)

### Guard Combination Logic

```c
if (in_open_intercept || is_rpc_in_progress()) {
    return syscall(SYS_open, pathname, flags, mode);
}
```

**Truth table**:

| in_open_intercept | rpc_in_progress | Action |
|-------------------|-----------------|--------|
| 0 | 0 | Proceed with interception |
| 1 | 0 | Direct syscall (same interceptor recursion) |
| 0 | 1 | Direct syscall (RPC active) |
| 1 | 1 | Direct syscall (both) |

**Result**: Only the outermost call gets intercepted, all nested calls use direct syscalls.

## Debugging Interception

### Check if Interception is Active

```bash
# Run with environment variable tracing
LD_DEBUG=libs LD_PRELOAD=./build/intercept.so ./build/program 2>&1 | grep intercept

# Expected output:
#      [0];  needed by ./build/program [0]
#      [0];  generating link map
#   ...
#   calling init: ./build/intercept.so
```

If you don't see `intercept.so`, LD_PRELOAD is not working.

### Verify Symbol Override

```bash
# Check which open() is being used
LD_PRELOAD=./build/intercept.so ldd ./build/program | grep intercept

# Check symbol resolution
LD_DEBUG=symbols,bindings LD_PRELOAD=./build/intercept.so ./build/program 2>&1 | grep "open"

# Expected output includes:
#   binding file ./build/program [0] to ./build/intercept.so [0]: normal symbol `open'
```

### Trace Interceptor Calls

Add debug prints to interceptors:

```c
int open(const char *pathname, int flags, ...) {
    fprintf(stderr, "[Intercept] open(%s, 0x%x)\n", pathname, flags);

    // ... rest of interceptor
}
```

**Output**:
```
[Intercept] open(/tmp/p3_tb_test.txt, 0x241)
[Client] open() RPC result: fd=3
[Intercept] write(3, 0x7ffd..., 56)
[Client] write() RPC result: 56 bytes
...
```

### Common Issues

#### Issue 1: Interceptor Not Called

**Symptom**: Program behaves as if LD_PRELOAD is not set

**Causes**:
- Forgot to set LD_PRELOAD environment variable
- Path to intercept.so is wrong
- Program is statically linked
- Program is SUID/SGID

**Diagnosis**:
```bash
# Check if dynamic
file ./build/program
# Should say "dynamically linked"

# Check if SUID
ls -l ./build/program
# Should NOT have 's' in permissions
```

#### Issue 2: Infinite Recursion / Stack Overflow

**Symptom**: Program crashes with segmentation fault

**Causes**:
- Missing reentry guard
- Guard not checked at start of interceptor
- RPC library calling intercepted functions without `rpc_in_progress` set

**Diagnosis**:
```bash
# Get stack trace
gdb ./build/program
(gdb) set environment LD_PRELOAD ./build/intercept.so
(gdb) run
# ... crash
(gdb) bt
# Check for repeating pattern in backtrace
```

**Example bad backtrace**:
```
#0  open() at intercept_open.h:15
#1  get_rpc_client() at rpc_client.c:45
#2  open() at intercept_open.h:30
#3  get_rpc_client() at rpc_client.c:45
#4  open() at intercept_open.h:30
... (repeats)
```

#### Issue 3: RPC Calls Failing During Initialization

**Symptom**: "Failed to connect to RPC server" but server is running

**Cause**: `rpc_in_progress` not set during `get_rpc_client()`

**Fix**: Ensure `rpc_in_progress = 1` is called before connection setup:
```c
CLIENT *get_rpc_client(void) {
    // ...
    rpc_in_progress = 1;  // MUST be here!
    rpc_client = clnt_vc_create(...);
    rpc_in_progress = 0;
    // ...
}
```

## Advanced Topics

### Intercepting More Syscalls

To add a new syscall (e.g., `lseek()`):

1. **Update protocol.x**:
```c
struct lseek_request {
    int fd;
    int offset;
    int whence;
};

struct lseek_response {
    int result;  // New offset or -1
    int err;
};

program SYSCALL_PROG {
    version SYSCALL_VERS {
        // ... existing
        lseek_response SYSCALL_LSEEK(lseek_request) = 5;
    } = 1;
} = 0x20000001;
```

2. **Regenerate protocol files**:
```bash
make rpc_gen
```

3. **Implement server handler** (rpc_server.c):
```c
lseek_response* syscall_lseek_1_svc(lseek_request *req, struct svc_req *rqstp) {
    static lseek_response res;

    int server_fd = get_server_fd(req->fd);
    if (server_fd < 0) {
        res.result = -1;
        res.err = EBADF;
        return &res;
    }

    off_t new_offset = lseek(server_fd, req->offset, req->whence);
    res.result = new_offset;
    res.err = (new_offset < 0) ? errno : 0;

    return &res;
}
```

4. **Create interceptor** (src/intercept/intercept_lseek.h):
```c
#ifndef INTERCEPT_LSEEK_H
#define INTERCEPT_LSEEK_H

static __thread int in_lseek_intercept = 0;

off_t lseek(int fd, off_t offset, int whence) {
    if (in_lseek_intercept || is_rpc_in_progress()) {
        return syscall(SYS_lseek, fd, offset, whence);
    }

    in_lseek_intercept = 1;

    CLIENT *client = get_rpc_client();
    off_t result = -1;

    if (client != NULL) {
        lseek_request req = { .fd = fd, .offset = offset, .whence = whence };

        rpc_in_progress = 1;
        lseek_response *res = syscall_lseek_1(&req, client);
        rpc_in_progress = 0;

        if (res != NULL) {
            result = res->result;
            errno = res->err;
        }
    } else {
        result = syscall(SYS_lseek, fd, offset, whence);
    }

    in_lseek_intercept = 0;
    return result;
}

#endif // INTERCEPT_LSEEK_H
```

5. **Include in intercept_client.c**:
```c
#include "intercept/intercept_lseek.h"
```

6. **Rebuild**:
```bash
make clean && make all
```

### Selective Interception

Only intercept specific paths:

```c
int open(const char *pathname, int flags, ...) {
    // ... extract mode

    // Only intercept files in /tmp/
    if (strncmp(pathname, "/tmp/", 5) != 0) {
        return syscall(SYS_open, pathname, flags, mode);
    }

    // ... proceed with RPC interception
}
```

### Performance Optimization

Reduce overhead by caching RPC client:

```c
// Already done in our implementation!
static __thread CLIENT *rpc_client = NULL;

CLIENT *get_rpc_client(void) {
    if (rpc_client != NULL) {
        return rpc_client;  // Fast path: return cached client
    }

    // Slow path: create new connection
    // ...
}
```

**Overhead comparison**:
- Creating connection per call: ~50 μs
- Using cached connection: ~5 μs
- Direct syscall: ~0.2 μs

## Security Considerations

### LD_PRELOAD is Disabled For

1. **SUID/SGID programs** - Prevents privilege escalation
2. **Programs with capabilities** - Similar security reasons
3. **Secure execution mode** (AT_SECURE) - Kernel-enforced restriction

**Example**:
```bash
# Make program SUID
sudo chown root ./build/program
sudo chmod u+s ./build/program

# LD_PRELOAD is ignored
LD_PRELOAD=./build/intercept.so ./build/program  # No interception!
```

### Risks

⚠️ **Code injection**: LD_PRELOAD can run arbitrary code
⚠️ **Data exfiltration**: Interceptors can log sensitive data
⚠️ **Credential theft**: Can capture passwords in arguments

**Mitigation**: Only use with trusted shared libraries.

---

**Next**: [07_FD_MAPPING.md](./07_FD_MAPPING.md) - File descriptor translation

**Prev**: [05_TRANSPORT_LAYER.md](./05_TRANSPORT_LAYER.md) - Transport layer details
