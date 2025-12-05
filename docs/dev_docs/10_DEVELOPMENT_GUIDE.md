# Development Guide

This document provides practical guidance for developers working on the P3_TB project: setup, building, testing, debugging, and extending functionality.

## Getting Started

### Prerequisites

**Required**:
- Linux system (Debian/Ubuntu recommended)
- GCC compiler
- GNU Make
- libtirpc development files
- pkg-config
- rpcgen tool

**Optional**:
- GDB (debugging)
- Valgrind (memory leak detection)
- strace (system call tracing)
- tcpdump (network analysis)
- rpcbind (for TCP transport)

### Installation (Debian/Ubuntu)

```bash
# Update package list
sudo apt-get update

# Install required packages
sudo apt-get install -y \
    build-essential \
    libtirpc-dev \
    pkg-config \
    rpcgen

# Install optional tools
sudo apt-get install -y \
    gdb \
    valgrind \
    strace \
    tcpdump \
    rpcbind
```

### Verifying Installation

```bash
# Check GCC
gcc --version
# Should show: gcc (Debian ...) 10.x or newer

# Check libtirpc
pkg-config --modversion libtirpc
# Should show: 1.x.x

# Check rpcgen
rpcgen -h
# Should show usage information

# Check rpcbind (for TCP transport)
systemctl status rpcbind
# Should show: active (running)
```

## Project Setup

### Clone Repository

```bash
# If using git
git clone <repository-url>
cd P3_TB

# Verify structure
ls -la
# Should see: src/, build/, docs/, Makefile, etc.
```

### Build Everything

```bash
# Clean previous builds
make clean

# Build all components
make all

# Verify binaries
ls -la build/
# Should see: syscall_server, intercept.so, program
```

### Quick Test

```bash
# Terminal 1: Start server
./build/syscall_server
# Output: [Server] Listening on UNIX socket: /tmp/p3_tb

# Terminal 2: Run test program
LD_PRELOAD=./build/intercept.so ./build/program
# Output: === Test Result: ALL TESTS PASSED ===
```

If tests pass, your environment is set up correctly!

## Build System

### Makefile Targets

```bash
# Build everything
make all          # Equivalent to: make rpc_gen server client program

# Individual targets
make rpc_gen      # Generate RPC protocol files from protocol.x
make server       # Build syscall_server binary
make client       # Build intercept.so shared library
make program      # Build test program

# Cleanup
make clean        # Remove all generated files

# Run targets
make run_server   # Start the server
make run_program  # Run test program with interception
```

### Build Process Flow

```
protocol.x
    ↓ (rpcgen -C)
protocol.h, protocol_xdr.c, protocol_clnt.c, protocol_svc.c
    ↓
    ├─→ server: rpc_server.c + protocol_svc.c + protocol_xdr.c → syscall_server
    ├─→ client: intercept_client.c + protocol_clnt.c + protocol_xdr.c → intercept.so
    └─→ program: program.c → program
```

### Detailed Build Commands

**RPC Code Generation**:
```bash
cd src/protocol
rpcgen -C protocol.x
cd ../..

# Post-process generated files (automated in Makefile)
# 1. Wrap auto-generated main() in protocol_svc.c
sed -i '/^int$/,/^}$/{/^int$$/i\#ifndef RPC_SVC_FG\n;/^}$$/a\#endif\n}' src/protocol/protocol_svc.c

# 2. Make dispatcher non-static
sed -i 's/^static void syscall_prog_1/void syscall_prog_1/' src/protocol/protocol_svc.c
```

**Server Compilation**:
```bash
gcc -o build/syscall_server \
    src/rpc_server.c \
    src/protocol/protocol_xdr.c \
    src/protocol/protocol_svc.c \
    -DRPC_SVC_FG \
    $(pkg-config --cflags --libs libtirpc)
```

**Client Compilation**:
```bash
gcc -shared -fPIC -o build/intercept.so \
    src/intercept_client.c \
    src/protocol/protocol_xdr.c \
    src/protocol/protocol_clnt.c \
    $(pkg-config --cflags --libs libtirpc)
```

**Test Program Compilation**:
```bash
gcc -o build/program src/program.c
```

### Compilation Flags Explained

| Flag | Purpose |
|------|---------|
| `-shared` | Build shared library (.so) |
| `-fPIC` | Position-independent code (required for shared libs) |
| `-DRPC_SVC_FG` | Disable auto-generated main() in protocol_svc.c |
| `$(pkg-config --cflags libtirpc)` | Include paths for libtirpc |
| `$(pkg-config --libs libtirpc)` | Link against libtirpc |

## Testing

### Running Tests

**Basic Test** (UNIX sockets):
```bash
# Terminal 1: Server
./build/syscall_server

# Terminal 2: Client
LD_PRELOAD=./build/intercept.so ./build/program
```

**TCP Sockets Test**:
```bash
# Ensure rpcbind is running
sudo systemctl start rpcbind

# Terminal 1: Server
RPC_TRANSPORT=tcp ./build/syscall_server

# Terminal 2: Client
RPC_TRANSPORT=tcp LD_PRELOAD=./build/intercept.so ./build/program
```

### Test Program Structure

The test program (`src/program.c`) runs 7 tests:

1. **open() for writing** - Creates/truncates file
2. **write()** - Writes test data
3. **close()** - Closes write FD
4. **open() for reading** - Opens same file for reading
5. **read()** - Reads data back
6. **Data integrity** - Verifies data matches
7. **close()** - Closes read FD

**Expected Output**:
```
=== Starting P3_TB Syscall Interception Tests ===

[Test 1] Opening file for writing...
[Test 1] PASS - fd=3

[Test 2] Writing data...
[Test 2] PASS - wrote 56 bytes

[Test 3] Closing write fd...
[Test 3] PASS

[Test 4] Opening file for reading...
[Test 4] PASS - fd=3

[Test 5] Reading data...
[Test 5] PASS - read 56 bytes

[Test 6] Verifying data integrity...
[Test 6] PASS - data matches

[Test 7] Closing read fd...
[Test 7] PASS

=== Test Result: ALL TESTS PASSED ===
```

### Writing Custom Tests

Create a new test file:

```c
// test_custom.c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main() {
    // Test 1: Multiple files
    int fd1 = open("/tmp/file1.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    int fd2 = open("/tmp/file2.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    assert(fd1 >= 0 && fd2 >= 0);

    write(fd1, "File 1", 6);
    write(fd2, "File 2", 6);

    close(fd1);
    close(fd2);

    printf("Test passed!\n");
    return 0;
}
```

Compile and run:
```bash
gcc -o test_custom test_custom.c
LD_PRELOAD=./build/intercept.so ./test_custom
```

### Integration Testing with Real Programs

Test with standard utilities:

```bash
# Test with cat
echo "Hello" > /tmp/test.txt
LD_PRELOAD=./build/intercept.so cat /tmp/test.txt

# Test with grep
LD_PRELOAD=./build/intercept.so grep "Hello" /tmp/test.txt

# Test with wc
LD_PRELOAD=./build/intercept.so wc -l /tmp/test.txt
```

**Note**: Some programs may not work if they use syscalls we don't intercept (e.g., stat, lseek).

## Debugging

### Debug Logging

Add debug prints to interceptors:

```c
// In src/intercept/intercept_open.h
int open(const char *pathname, int flags, ...) {
    fprintf(stderr, "[DEBUG] open(%s, 0x%x)\n", pathname, flags);

    // ... rest of interceptor

    fprintf(stderr, "[DEBUG] open() returning %d (errno=%d)\n", result, errno);
    return result;
}
```

Rebuild and run:
```bash
make client
LD_PRELOAD=./build/intercept.so ./build/program 2>debug.log
cat debug.log
```

### Using GDB

**Debug Server**:
```bash
gdb ./build/syscall_server
(gdb) break syscall_open_1_svc
(gdb) run
# In another terminal, run client
(gdb) continue
```

**Debug Client** (tricky with LD_PRELOAD):
```bash
gdb ./build/program
(gdb) set environment LD_PRELOAD ./build/intercept.so
(gdb) break open  # Break on our interceptor
(gdb) run
```

### Using strace

Trace syscalls to see what's really happening:

```bash
# Trace server
strace -f -e trace=open,close,read,write ./build/syscall_server

# Trace client (shows both intercepted and direct syscalls)
strace -f -e trace=open,close,read,write \
       env LD_PRELOAD=./build/intercept.so ./build/program
```

### Using Valgrind

Check for memory leaks:

```bash
# Check server
valgrind --leak-check=full ./build/syscall_server

# Check client (more complex with LD_PRELOAD)
valgrind --leak-check=full \
         env LD_PRELOAD=./build/intercept.so ./build/program
```

### Network Debugging

**Capture RPC traffic** (TCP):
```bash
# Start server
RPC_TRANSPORT=tcp ./build/syscall_server

# In another terminal, capture traffic
sudo tcpdump -i lo -X -s 0 port 9999  # Or whatever port server uses

# In another terminal, run client
RPC_TRANSPORT=tcp LD_PRELOAD=./build/intercept.so ./build/program
```

### Common Issues

#### Issue: "Connection refused"

**Symptom**: Client can't connect to server

**Diagnosis**:
```bash
# Check if server is running
ps aux | grep syscall_server

# Check if socket exists (UNIX)
ls -l /tmp/p3_tb

# Check if port is listening (TCP)
netstat -ln | grep 9999
```

**Solutions**:
- Start server first
- Check socket path/port matches
- For TCP, ensure rpcbind is running

#### Issue: "Segmentation fault"

**Symptom**: Crash in interceptor or server

**Diagnosis**:
```bash
# Get backtrace
gdb ./build/syscall_server
(gdb) run
# ... crash
(gdb) bt
```

**Common causes**:
- Dereferencing NULL pointer (check RPC response)
- Stack overflow (infinite recursion - check guards)
- Buffer overflow (check array bounds)

#### Issue: "Symbol not found"

**Symptom**: `LD_PRELOAD` not working

**Diagnosis**:
```bash
# Check if intercept.so exists
file ./build/intercept.so

# Check symbols in .so
nm -D ./build/intercept.so | grep open
```

**Solutions**:
- Rebuild: `make clean && make client`
- Check path to intercept.so is correct

## Adding New Syscalls

**For a detailed, comprehensive tutorial with complete examples, see [11_ADDING_NEW_SYSCALLS_TUTORIAL.md](./11_ADDING_NEW_SYSCALLS_TUTORIAL.md).**

The tutorial includes:
- Complete stat() implementation walkthrough
- Detailed explanation of each component
- Troubleshooting common issues
- Best practices and patterns

Below is a quick reference for experienced developers:

### Step-by-Step Guide: Adding lseek()

#### 1. Update Protocol Definition

Edit `src/protocol/protocol.x`:

```c
/* Add structures */
struct lseek_request {
    int fd;
    int offset;
    int whence;
};

struct lseek_response {
    int result;  /* New offset or -1 */
    int err;
};

/* Add procedure to program */
program SYSCALL_PROG {
    version SYSCALL_VERS {
        open_response SYSCALL_OPEN(open_request) = 1;
        close_response SYSCALL_CLOSE(close_request) = 2;
        read_response SYSCALL_READ(read_request) = 3;
        write_response SYSCALL_WRITE(write_request) = 4;
        lseek_response SYSCALL_LSEEK(lseek_request) = 5;  /* NEW */
    } = 1;
} = 0x20000001;
```

#### 2. Regenerate Protocol Files

```bash
make rpc_gen
```

This regenerates:
- `protocol.h` (with lseek_request/response)
- `protocol_xdr.c` (with xdr_lseek_request/response)
- `protocol_clnt.c` (with syscall_lseek_1 stub)
- `protocol_svc.c` (dispatcher updated)

#### 3. Implement Server Handler

Edit `src/rpc_server.c`, add:

```c
lseek_response* syscall_lseek_1_svc(lseek_request *req, struct svc_req *rqstp) {
    static lseek_response res;

    printf("[Server] Handling lseek(client_fd=%d, offset=%d, whence=%d)\n",
           req->fd, req->offset, req->whence);

    // Map client FD to server FD
    int server_fd = get_server_fd(req->fd);
    if (server_fd < 0) {
        res.result = -1;
        res.err = EBADF;
        return &res;
    }

    // Execute lseek
    off_t new_offset = lseek(server_fd, req->offset, req->whence);

    res.result = new_offset;
    res.err = (new_offset < 0) ? errno : 0;

    printf("[Server] lseek() -> result=%d, errno=%d\n", res.result, res.err);

    return &res;
}
```

#### 4. Create Interceptor

Create `src/intercept/intercept_lseek.h`:

```c
#ifndef INTERCEPT_LSEEK_H
#define INTERCEPT_LSEEK_H

#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>

static __thread int in_lseek_intercept = 0;

off_t lseek(int fd, off_t offset, int whence) {
    // Check guards
    if (in_lseek_intercept || is_rpc_in_progress()) {
        return syscall(SYS_lseek, fd, offset, whence);
    }

    in_lseek_intercept = 1;

    // Get RPC client
    CLIENT *client = get_rpc_client();

    off_t result = -1;

    if (client != NULL) {
        // Package request
        lseek_request req = {
            .fd = fd,
            .offset = offset,
            .whence = whence
        };

        // Make RPC call
        rpc_in_progress = 1;
        lseek_response *res = syscall_lseek_1(&req, client);
        rpc_in_progress = 0;

        // Extract result
        if (res != NULL) {
            result = res->result;
            errno = res->err;
        }
    } else {
        // Fallback
        result = syscall(SYS_lseek, fd, offset, whence);
    }

    in_lseek_intercept = 0;
    return result;
}

#endif // INTERCEPT_LSEEK_H
```

#### 5. Include Interceptor

Edit `src/intercept_client.c`, add:

```c
#include "intercept/intercept_lseek.h"
```

#### 6. Rebuild and Test

```bash
make clean
make all

# Create test
cat > test_lseek.c << 'EOF'
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main() {
    int fd = open("/tmp/test.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    write(fd, "0123456789", 10);

    off_t pos = lseek(fd, 5, SEEK_SET);
    printf("Seeked to position: %ld\n", pos);

    write(fd, "ABCDE", 5);
    close(fd);

    // Verify
    fd = open("/tmp/test.txt", O_RDONLY);
    char buf[11] = {0};
    read(fd, buf, 10);
    printf("File contents: %s\n", buf);  // Should be: 01234ABCDE
    close(fd);

    return 0;
}
EOF

gcc -o test_lseek test_lseek.c
LD_PRELOAD=./build/intercept.so ./test_lseek
```

## Best Practices

### Code Style

- Follow existing code style
- Use descriptive variable names
- Add comments for non-obvious logic
- Keep functions focused and small

### Error Handling

```c
// Always check RPC results
open_response *res = syscall_open_1(&req, client);
if (res != NULL) {
    // Check result
    if (res->result >= 0) {
        // Success
    } else {
        // Error: use res->err
    }
} else {
    // RPC call failed
}
```

### Memory Management

- Use `static` storage for RPC response structures
- Don't forget to free XDR-allocated memory (if using manual XDR)
- Check for memory leaks with Valgrind

### Thread Safety

- Use `__thread` for all per-interceptor guards
- Ensure RPC client connections are thread-local
- Don't share state across threads without locking

### Performance

- Minimize RPC calls (batch operations if possible)
- Use persistent connections (already implemented)
- Profile with `perf` or `gprof` if performance is critical

## Troubleshooting

### Build Errors

**Error**: `libtirpc not found`

**Solution**:
```bash
sudo apt-get install libtirpc-dev
```

**Error**: `rpcgen: command not found`

**Solution**:
```bash
sudo apt-get install rpcgen
```

**Error**: `multiple definition of 'main'`

**Solution**: Makefile should handle this with `-DRPC_SVC_FG`. If not, check Makefile's sed commands.

### Runtime Errors

**Error**: `Error: unable to register (SYSCALL_PROG, SYSCALL_VERS, tcp)`

**Solution**:
```bash
sudo systemctl start rpcbind
```

**Error**: Infinite recursion / stack overflow

**Solution**: Check reentry guards and `rpc_in_progress` flag are set correctly.

## Contributing

### Workflow

1. **Create feature branch**:
```bash
git checkout -b feature/add-stat-syscall
```

2. **Make changes**: Follow guide above for adding syscalls

3. **Test thoroughly**:
```bash
make clean
make all
# Run existing tests
LD_PRELOAD=./build/intercept.so ./build/program
# Run your tests
LD_PRELOAD=./build/intercept.so ./test_myfeature
```

4. **Update documentation**: Add to relevant docs in `docs/dev_docs/`

5. **Commit changes**:
```bash
git add .
git commit -m "Add stat() syscall interception"
```

6. **Push and create pull request**:
```bash
git push origin feature/add-stat-syscall
```

### Code Review Checklist

- [ ] Code compiles without warnings
- [ ] All tests pass
- [ ] New functionality has tests
- [ ] Documentation updated
- [ ] No memory leaks (checked with Valgrind)
- [ ] Error handling implemented
- [ ] Thread-safe (uses `__thread` guards)

## Resources

### Documentation

- [RFC 5531 - RPC Protocol Specification](https://www.rfc-editor.org/rfc/rfc5531.html)
- [RFC 4506 - XDR Specification](https://www.rfc-editor.org/rfc/rfc4506.html)
- Man pages: `man rpcgen`, `man xdr`, `man rpc`

### Tools

- GDB Manual: https://sourceware.org/gdb/current/onlinedocs/gdb/
- Valgrind Manual: https://valgrind.org/docs/manual/manual.html
- strace Tutorial: https://strace.io/

### Project Documentation

Start with [00_README.md](./00_README.md) for documentation roadmap.

---

**Prev**: [09_API_REFERENCE.md](./09_API_REFERENCE.md) - API reference

**[Back to Documentation Index](./00_README.md)**
