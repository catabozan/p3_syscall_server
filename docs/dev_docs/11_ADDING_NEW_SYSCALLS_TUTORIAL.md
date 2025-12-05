# Tutorial: Adding New Syscall Interceptors

This tutorial provides a step-by-step guide for implementing a new syscall interceptor in the P3_TB system. By following this guide, you'll learn how to intercept a syscall on the client side, forward it to the server via RPC, and handle the response.

## Prerequisites

Before starting this tutorial, you should:
- Have the project built and working (`make all` succeeds)
- Be familiar with C programming and syscalls
- Understand the basics of ONC RPC (see [03_ONC_RPC_PRIMER.md](./03_ONC_RPC_PRIMER.md))
- Have read the architecture overview (see [02_ARCHITECTURE.md](./02_ARCHITECTURE.md))

## Overview: The Five-Step Process

Adding a new syscall involves five main steps:

1. **Define the Protocol** - Add request/response structures to the XDR definition
2. **Generate RPC Code** - Use rpcgen to generate client/server stubs
3. **Implement Server Handler** - Write the server-side syscall execution logic
4. **Create Client Interceptor** - Write the LD_PRELOAD function wrapper
5. **Test and Verify** - Build, test, and debug your implementation

## Example: Implementing `stat()` Syscall

Throughout this tutorial, we'll implement the `stat()` syscall as a complete example. The `stat()` syscall retrieves file metadata (size, permissions, timestamps, etc.).

### Syscall Signature

```c
int stat(const char *pathname, struct stat *statbuf);
```

**Parameters**:
- `pathname`: Path to the file
- `statbuf`: Pointer to struct where results will be stored

**Returns**:
- 0 on success
- -1 on error (errno is set)

## Step 1: Define the Protocol

The protocol definition lives in `src/protocol/protocol.x`. This file uses XDR (External Data Representation) language to define data structures and RPC procedures.

### 1.1 Define Request Structure

First, define the structure for the request (data sent from client to server):

```c
/* stat() syscall structures */
struct stat_request {
    string path<MAX_PATH_LEN>;  /* File path */
};
```

**Key Points**:
- Use XDR types: `string`, `int`, `unsigned int`, `opaque`, etc.
- For variable-length data, use angle brackets: `string path<MAX>`
- For fixed-length arrays, use square brackets: `int values[10]`
- Keep it simple - only send what's needed

### 1.2 Define Response Structure

Next, define the response structure (data sent from server to client):

```c
struct stat_response {
    int result;              /* Return value: 0 on success, -1 on error */
    int err;                 /* errno value */

    /* struct stat fields we care about */
    unsigned int st_mode;    /* File type and permissions */
    unsigned int st_size;    /* File size in bytes */
    unsigned int st_atime;   /* Last access time */
    unsigned int st_mtime;   /* Last modification time */
    unsigned int st_ctime;   /* Last status change time */
};
```

**Important Considerations**:
- Always include `result` (return value) and `err` (errno)
- Only include fields you need from complex structures like `struct stat`
- Use XDR-compatible types (avoid pointers, use unsigned int instead of time_t)
- Document each field with comments

### 1.3 Add RPC Procedure

Now add the procedure to the RPC program definition. Find the `program SYSCALL_PROG` block and add your new procedure:

```c
program SYSCALL_PROG {
    version SYSCALL_VERS {
        open_response SYSCALL_OPEN(open_request) = 1;
        close_response SYSCALL_CLOSE(close_request) = 2;
        read_response SYSCALL_READ(read_request) = 3;
        write_response SYSCALL_WRITE(write_request) = 4;
        stat_response SYSCALL_STAT(stat_request) = 5;  /* NEW PROCEDURE */
    } = 1;
} = 0x20000001;
```

**Key Points**:
- Assign the next available procedure number (5 in this case)
- Never reuse or change existing procedure numbers (breaks compatibility)
- Follow the naming convention: `SYSCALL_<NAME>`

### 1.4 Complete Protocol File Changes

Edit `src/protocol/protocol.x` and add all three components:

```c
/* Add near the top, after other syscall structures */

/*
 * stat() syscall structures
 */
struct stat_request {
    string path<MAX_PATH_LEN>;  /* File path to stat */
};

struct stat_response {
    int result;                 /* Return value: 0 on success, -1 on error */
    int err;                    /* errno value */
    unsigned int st_mode;       /* File mode (permissions and type) */
    unsigned int st_size;       /* File size in bytes */
    unsigned int st_atime;      /* Access time (seconds since epoch) */
    unsigned int st_mtime;      /* Modification time */
    unsigned int st_ctime;      /* Status change time */
};

/* Then update the program definition */
program SYSCALL_PROG {
    version SYSCALL_VERS {
        open_response SYSCALL_OPEN(open_request) = 1;
        close_response SYSCALL_CLOSE(close_request) = 2;
        read_response SYSCALL_READ(read_request) = 3;
        write_response SYSCALL_WRITE(write_request) = 4;
        stat_response SYSCALL_STAT(stat_request) = 5;  /* ADD THIS LINE */
    } = 1;
} = 0x20000001;
```

## Step 2: Generate RPC Code

After modifying the protocol definition, regenerate the RPC code using `rpcgen`:

```bash
make rpc_gen
```

This command does the following:
1. Runs `rpcgen -C src/protocol/protocol.x`
2. Generates four files in `src/protocol/`:
   - `protocol.h` - Header with struct definitions and function prototypes
   - `protocol_xdr.c` - XDR serialization/deserialization functions
   - `protocol_clnt.c` - Client stub functions (e.g., `syscall_stat_1()`)
   - `protocol_svc.c` - Server dispatcher code
3. Post-processes generated files (wraps main(), fixes function visibility)

### 2.1 What Gets Generated

After running `make rpc_gen`, you'll have:

**Client Stub** (in `protocol_clnt.c`):
```c
stat_response *
syscall_stat_1(stat_request *argp, CLIENT *clnt)
{
    static stat_response clnt_res;
    // ... RPC call implementation ...
    return (&clnt_res);
}
```

**Server Skeleton** (in `protocol_svc.c`):
- Updated dispatcher that routes SYSCALL_STAT calls to `syscall_stat_1_svc()`

**Data Structures** (in `protocol.h`):
```c
struct stat_request {
    char *path;
};
typedef struct stat_request stat_request;

struct stat_response {
    int result;
    int err;
    u_int st_mode;
    u_int st_size;
    // ... etc ...
};
typedef struct stat_response stat_response;
```

### 2.2 Verify Generation

Check that your structures were generated:

```bash
grep -A 10 "struct stat_request" src/protocol/protocol.h
grep "syscall_stat_1" src/protocol/protocol_clnt.c
```

You should see your new structures and the client stub function.

## Step 3: Implement Server Handler

Now implement the server-side logic that executes the syscall and returns results.

### 3.1 Server Function Signature

All server handlers follow this pattern:

```c
<response_type> *
syscall_<name>_1_svc(<request_type> *req, struct svc_req *rqstp)
```

For our stat example:

```c
stat_response *
syscall_stat_1_svc(stat_request *req, struct svc_req *rqstp)
```

### 3.2 Implementation Template

Here's the complete implementation to add to `src/rpc_server.c`:

```c
/*
 * SYSCALL_STAT implementation
 */
stat_response *
syscall_stat_1_svc(stat_request *req, struct svc_req *rqstp) {
    static stat_response res;
    struct stat statbuf;

    fprintf(stderr, "[Server] STAT: path=%s\n", req->path);

    /* Execute the actual stat syscall */
    int result = stat(req->path, &statbuf);
    int saved_errno = errno;

    if (result == 0) {
        /* Success: populate response with stat data */
        res.result = 0;
        res.err = 0;
        res.st_mode = statbuf.st_mode;
        res.st_size = statbuf.st_size;
        res.st_atime = statbuf.st_atime;
        res.st_mtime = statbuf.st_mtime;
        res.st_ctime = statbuf.st_ctime;

        fprintf(stderr, "[Server] STAT result: size=%u, mode=0%o\n",
                res.st_size, res.st_mode);
    } else {
        /* Failure: set error */
        res.result = -1;
        res.err = saved_errno;

        fprintf(stderr, "[Server] STAT failed: errno=%d (%s)\n",
                saved_errno, strerror(saved_errno));
    }

    return &res;
}
```

### 3.3 Key Implementation Points

**1. Use Static Storage**:
```c
static stat_response res;
```
- RPC infrastructure expects the response to remain valid after return
- Static storage ensures the memory persists
- One static variable per function is safe (single-threaded per request)

**2. Save errno Immediately**:
```c
int result = stat(req->path, &statbuf);
int saved_errno = errno;
```
- Other function calls might overwrite errno
- Save it right after the syscall

**3. Always Set Both result and err**:
```c
res.result = result_value;  // The actual return value
res.err = errno_value;      // The errno if result indicates error
```

**4. Add Debug Logging**:
```c
fprintf(stderr, "[Server] STAT: path=%s\n", req->path);
fprintf(stderr, "[Server] STAT result: ...\n");
```
- Use stderr (not stdout) for server logs
- Prefix with `[Server]` for clarity
- Log inputs and outputs for debugging

**5. Handle Both Success and Failure**:
```c
if (result == 0) {
    // Populate success data
    res.st_mode = statbuf.st_mode;
    // ...
} else {
    // Set error information
    res.err = saved_errno;
}
```

## Step 4: Create Client Interceptor

The interceptor is the LD_PRELOAD function that replaces the original libc function.

### 4.1 Create Interceptor Header

Create a new file `src/intercept/intercept_stat.h`:

```c
/*
 * stat() syscall interceptor
 */

#ifndef __INTERCEPT_STAT_
#define __INTERCEPT_STAT_

#include <sys/stat.h>

/* Thread-local reentry guard */
static __thread int in_stat_intercept = 0;

/*
 * Intercepted stat() function
 */
int stat(const char *pathname, struct stat *statbuf) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_stat_intercept || is_rpc_in_progress()) {
        return syscall(SYS_stat, pathname, statbuf);
    }

    /* Set guard */
    in_stat_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted stat(\"%s\")\n",
                          pathname);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        stat_request req;
        req.path = (char *)pathname;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        stat_response *res = syscall_stat_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            /* Copy stat data to user's buffer on success */
            if (result == 0) {
                memset(statbuf, 0, sizeof(struct stat));
                statbuf->st_mode = res->st_mode;
                statbuf->st_size = res->st_size;
                statbuf->st_atime = res->st_atime;
                statbuf->st_mtime = res->st_mtime;
                statbuf->st_ctime = res->st_ctime;
            }

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] stat() RPC result: %d, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] stat() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_stat, pathname, statbuf);
    }

    /* Clear guard */
    in_stat_intercept = 0;

    return result;
}

#endif
```

### 4.2 Interceptor Anatomy

Let's break down each part:

#### Header Guards
```c
#ifndef __INTERCEPT_STAT_
#define __INTERCEPT_STAT_
// ... code ...
#endif
```
- Prevents multiple inclusion
- Use unique name per interceptor

#### Reentry Guard
```c
static __thread int in_stat_intercept = 0;
```
- **`static`**: Internal to this file
- **`__thread`**: Each thread gets its own copy
- Prevents infinite recursion if the intercepted function calls itself

#### Guard Check
```c
if (in_stat_intercept || is_rpc_in_progress()) {
    return syscall(SYS_stat, pathname, statbuf);
}
```
- If we're already intercepting stat() → use direct syscall
- If any RPC is in progress → use direct syscall
- Prevents recursive interception loops

#### Debug Logging (Safe)
```c
char debug_msg[256];
int msg_len = snprintf(debug_msg, sizeof(debug_msg), "[Client] ...\n", ...);
syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
```
- **Never use printf/fprintf** in interceptors (they may call write, causing recursion)
- Use `syscall(SYS_write)` directly
- Write to `STDERR_FILENO` (2)

#### RPC Call Pattern
```c
/* Get RPC client */
CLIENT *client = get_rpc_client();

if (client != NULL) {
    /* Prepare request */
    stat_request req;
    req.path = (char *)pathname;

    /* Disable interception */
    rpc_in_progress = 1;

    /* Make RPC call */
    stat_response *res = syscall_stat_1(&req, client);

    /* Re-enable interception */
    rpc_in_progress = 0;

    /* Process response */
    if (res != NULL) {
        result = res->result;
        errno = res->err;
        // Copy data if needed
    }
}
```

#### Fallback Path
```c
else {
    /* No RPC connection - fall back to direct syscall */
    result = syscall(SYS_stat, pathname, statbuf);
}
```
- Always provide a fallback
- If RPC fails, program can still run locally

#### Cleanup
```c
in_stat_intercept = 0;
return result;
```
- Always clear the guard before returning
- Return the result to the caller

### 4.3 Common Patterns for Different Syscall Types

**Simple Input, Simple Output** (like `close()`):
```c
close_request req;
req.fd = fd;

close_response *res = syscall_close_1(&req, client);

result = res->result;
errno = res->err;
```

**Input with Data Buffer** (like `write()`):
```c
write_request req;
req.fd = fd;
req.data.data_val = (char *)buf;
req.data.data_len = count;

write_response *res = syscall_write_1(&req, client);
```

**Output with Data Buffer** (like `read()`):
```c
read_request req;
req.fd = fd;
req.count = count;

read_response *res = syscall_read_1(&req, client);

if (res->result > 0) {
    size_t bytes_to_copy = res->data.data_len;
    if (bytes_to_copy > count) bytes_to_copy = count;
    memcpy(buf, res->data.data_val, bytes_to_copy);
}
```

**Variadic Arguments** (like `open()`):
```c
int open(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }
    // ... rest of interceptor
}
```

### 4.4 Include the Interceptor

Edit `src/rpc_client.c` and add your new interceptor at the end:

```c
/*
 * Include all interceptor implementations
 */
#include "intercept/intercept_open.h"
#include "intercept/intercept_close.h"
#include "intercept/intercept_read.h"
#include "intercept/intercept_write.h"
#include "intercept/intercept_stat.h"  /* ADD THIS LINE */
```

**Important**: The order matters if interceptors depend on each other (usually they don't).

## Step 5: Build and Test

### 5.1 Rebuild Everything

```bash
make clean
make all
```

Watch for compilation errors:
- Missing includes? Add them to your interceptor header
- Type mismatches? Check XDR type conversions
- Undefined references? Check function names match generated stubs

### 5.2 Create a Test Program

Create `test_stat.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    printf("=== Testing stat() Interception ===\n\n");

    /* Create a test file first */
    const char *test_file = "/tmp/test_stat.txt";
    int fd = open(test_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    write(fd, "Hello, World!", 13);
    close(fd);

    /* Now stat it */
    struct stat statbuf;
    printf("[Test] Calling stat(\"%s\")...\n", test_file);

    int result = stat(test_file, &statbuf);

    if (result == 0) {
        printf("[Test] SUCCESS\n");
        printf("  File size: %lu bytes\n", (unsigned long)statbuf.st_size);
        printf("  File mode: 0%o\n", statbuf.st_mode & 0777);
        printf("  Is regular file: %s\n", S_ISREG(statbuf.st_mode) ? "yes" : "no");

        /* Verify size matches */
        if (statbuf.st_size == 13) {
            printf("\n=== Test Result: PASS ===\n");
            return 0;
        } else {
            printf("\n=== Test Result: FAIL (size mismatch) ===\n");
            return 1;
        }
    } else {
        printf("[Test] FAILED: %s\n", strerror(errno));
        printf("\n=== Test Result: FAIL ===\n");
        return 1;
    }
}
```

### 5.3 Compile and Run

```bash
# Compile test
gcc -o test_stat test_stat.c

# Terminal 1: Start server
./build/syscall_server

# Terminal 2: Run test with interception
LD_PRELOAD=./build/intercept.so ./test_stat
```

### 5.4 Expected Output

**Server terminal**:
```
[Server] Starting RPC server...
[Server] Using UNIX transport
[Server] RPC server ready at /tmp/p3_tb
[Server] Waiting for requests...
[Server] OPEN: path=/tmp/test_stat.txt, flags=577, mode=644
[Server] OPEN result: fd=3, errno=0
[Server] WRITE: client_fd=3, count=13
[Server] WRITE result: 13 bytes, errno=0
[Server] CLOSE: client_fd=3
[Server] CLOSE result: 0, errno=0
[Server] STAT: path=/tmp/test_stat.txt
[Server] STAT result: size=13, mode=0644
```

**Client terminal**:
```
=== Testing stat() Interception ===

[Client] Intercepted open("/tmp/test_stat.txt")
[Client] open() RPC result: fd=3, errno=0
[Client] Intercepted write(3, 0x...)
[Client] write() RPC result: 13 bytes, errno=0
[Client] Intercepted close(3)
[Client] close() RPC result: 0, errno=0
[Test] Calling stat("/tmp/test_stat.txt")...
[Client] Intercepted stat("/tmp/test_stat.txt")
[Client] stat() RPC result: 0, errno=0
[Test] SUCCESS
  File size: 13 bytes
  File mode: 0644
  Is regular file: yes

=== Test Result: PASS ===
```

## Troubleshooting

### Compilation Errors

**Error: `undefined reference to 'syscall_stat_1'`**

**Cause**: RPC code not regenerated or not linked

**Solution**:
```bash
make rpc_gen
make clean
make all
```

**Error: `'stat_request' undeclared`**

**Cause**: Missing include in interceptor

**Solution**: Add to your interceptor header:
```c
#include "../protocol/protocol.h"
```

Note: This is already included via `rpc_client.c`, but make sure it's visible.

**Error: `conflicting types for 'stat'`**

**Cause**: Missing or wrong includes

**Solution**: Add these includes to your interceptor:
```c
#include <sys/stat.h>
#include <sys/syscall.h>
```

### Runtime Errors

**Error: Client doesn't intercept (uses real syscall)**

**Diagnosis**:
```bash
# Check if intercept.so contains your function
nm -D ./build/intercept.so | grep stat
```

Should show:
```
00000xxx T stat
```

**Solution**: Make sure you included your interceptor in `rpc_client.c`.

**Error: Segmentation fault in interceptor**

**Common causes**:
1. NULL pointer dereference (check RPC response)
2. Infinite recursion (check reentry guards)
3. Buffer overflow (check buffer sizes)

**Debug with GDB**:
```bash
gdb ./build/program
(gdb) set environment LD_PRELOAD ./build/intercept.so
(gdb) break stat
(gdb) run
(gdb) bt  # When it crashes
```

**Error: RPC call fails (res == NULL)**

**Diagnosis**: Check server logs for errors

**Common causes**:
1. Type mismatch in XDR structures
2. Server handler not implemented
3. Network/socket issues

**Solution**: Add debug prints and check both client and server logs.

### Data Corruption

**Problem**: Data received doesn't match data sent

**Diagnosis**: Add detailed logging:

In server:
```c
fprintf(stderr, "[Server] Request: path=%s\n", req->path);
fprintf(stderr, "[Server] Response: mode=0%o, size=%u\n", res.st_mode, res.st_size);
```

In client:
```c
syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
```

**Common causes**:
1. XDR type mismatch (using `int` instead of `unsigned int`)
2. Endianness issues (XDR handles this, but check your types)
3. Copying wrong amount of data

## Best Practices

### 1. Thread Safety

Always use `__thread` for reentry guards:
```c
static __thread int in_myfunction_intercept = 0;
```

This ensures each thread has its own guard variable.

### 2. Error Handling

Always check RPC responses:
```c
response_type *res = syscall_something_1(&req, client);
if (res != NULL) {
    if (res->result >= 0) {
        // Success path
    } else {
        // Error path - use res->err
        errno = res->err;
    }
} else {
    // RPC failure - log it
    clnt_perror(client, "RPC failed");
}
```

### 3. Logging

Use safe logging in interceptors:
```c
// GOOD - direct syscall
char msg[256];
int len = snprintf(msg, sizeof(msg), "...\n");
syscall(SYS_write, STDERR_FILENO, msg, len);

// BAD - may cause recursion
printf("...\n");          // May call write()
fprintf(stderr, "...\n"); // May call write()
```

### 4. Resource Cleanup

If your syscall allocates resources, ensure cleanup on all paths:
```c
if (client != NULL) {
    // ... RPC call ...
} else {
    // Fallback path - also clean up
}

// Always clear guard
in_myfunction_intercept = 0;
return result;
```

### 5. XDR Type Mapping

Common type mappings:

| C Type | XDR Type | Notes |
|--------|----------|-------|
| `int` | `int` | 32-bit signed |
| `unsigned int` | `unsigned int` | 32-bit unsigned |
| `char *` | `string<MAX>` | Variable-length string |
| `void *` | `opaque<MAX>` | Variable-length binary data |
| `uint8_t[]` | `opaque[N]` | Fixed-length binary data |
| `size_t` | `unsigned int` | Size may differ on 32/64-bit |
| `off_t` | `int` or `hyper` | Use `hyper` for 64-bit offsets |

### 6. Testing Checklist

Before committing your new syscall:

- [ ] Compiles without warnings
- [ ] Test program passes
- [ ] Works with UNIX sockets
- [ ] Works with TCP sockets (if applicable)
- [ ] Handles errors correctly (test with invalid inputs)
- [ ] No memory leaks (checked with Valgrind)
- [ ] Thread-safe (uses `__thread` guards)
- [ ] Fallback path works (test without server)
- [ ] Server logs show expected behavior
- [ ] Client logs show expected behavior

## Advanced Topics

### Handling Complex Data Structures

If your syscall uses complex structures (like `struct stat`), you have two options:

**Option 1: Flatten the structure** (recommended)
```c
// In protocol.x
struct stat_response {
    int result;
    int err;
    unsigned int st_mode;
    unsigned int st_size;
    // ... only the fields you need
};
```

**Option 2: Define the full structure in XDR**
```c
// In protocol.x
struct rpc_stat {
    unsigned int st_dev;
    unsigned int st_ino;
    unsigned int st_mode;
    // ... all fields
};
```

Option 1 is usually better because:
- Smaller messages
- Faster serialization
- Only transfer what you need
- Less coupling to system-specific layouts

### Handling Variable-Length Data

For syscalls that read/write variable amounts of data:

**Reading (like `read()`)**:
```c
// Server allocates and populates buffer
static char buffer[MAX_BUFFER_SIZE];
res.data.data_val = buffer;
res.data.data_len = bytes_read;

// Client copies to user buffer
memcpy(buf, res->data.data_val, res->data.data_len);
```

**Writing (like `write()`)**:
```c
// Client points to user buffer
req.data.data_val = (char *)buf;
req.data.data_len = count;

// Server reads directly
write(fd, req->data.data_val, req->data.data_len);
```

### Adding Syscalls with File Descriptors

If your syscall uses file descriptors:

**Client side**: Use the FD as-is
```c
req.fd = fd;  // Use the FD the program gave us
```

**Server side**: Translate to real FD
```c
int server_fd = translate_fd(req->fd);
if (server_fd < 0) {
    res.err = EBADF;
    return &res;
}
// Use server_fd for actual syscall
```

See [07_FD_MAPPING.md](./07_FD_MAPPING.md) for details on FD mapping.

## Summary

To add a new syscall interceptor:

1. **Update `src/protocol/protocol.x`**:
   - Add request/response structures
   - Add procedure to RPC program

2. **Regenerate RPC code**:
   ```bash
   make rpc_gen
   ```

3. **Implement server handler in `src/rpc_server.c`**:
   ```c
   response_type *syscall_name_1_svc(request_type *req, struct svc_req *rqstp)
   ```

4. **Create interceptor in `src/intercept/intercept_name.h`**:
   - Add reentry guard
   - Implement function with same signature as libc
   - Make RPC call
   - Handle response
   - Provide fallback

5. **Include interceptor in `src/rpc_client.c`**:
   ```c
   #include "intercept/intercept_name.h"
   ```

6. **Build and test**:
   ```bash
   make clean && make all
   ./build/syscall_server &
   LD_PRELOAD=./build/intercept.so ./test_program
   ```

## Next Steps

- Read [06_INTERCEPTION_MECHANISM.md](./06_INTERCEPTION_MECHANISM.md) for deep dive on LD_PRELOAD
- Read [07_FD_MAPPING.md](./07_FD_MAPPING.md) to understand FD translation
- Read [08_REQUEST_FLOW.md](./08_REQUEST_FLOW.md) for complete request lifecycle
- See [09_API_REFERENCE.md](./09_API_REFERENCE.md) for existing syscall implementations

## Further Reading

- **XDR Specification**: [RFC 4506](https://www.rfc-editor.org/rfc/rfc4506.html)
- **RPC Specification**: [RFC 5531](https://www.rfc-editor.org/rfc/rfc5531.html)
- **rpcgen Manual**: `man rpcgen`
- **LD_PRELOAD Technique**: `man ld.so`

---

**Prev**: [10_DEVELOPMENT_GUIDE.md](./10_DEVELOPMENT_GUIDE.md) - General development guide

**[Back to Documentation Index](./00_README.md)**
