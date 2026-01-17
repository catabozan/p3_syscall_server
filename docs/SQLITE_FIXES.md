# SQLite3 Integration Issues and Fixes

## Overview

This document details the complete investigation and resolution of issues encountered when running SQLite3 with our syscall interception framework. The interception system uses LD_PRELOAD to redirect libc function calls to an RPC server that executes them remotely.

## Initial Problem

When attempting to run SQLite3 with syscall interception:

```bash
LD_PRELOAD=./build/intercept.so ./sqlite-src-3510100/sqlite3 test.db
```

SQLite would fail with:
```
Runtime error: disk I/O error (10)
```

This error occurred when attempting to create tables and insert data, suggesting that file I/O operations were failing despite the RPC server executing them successfully.

## Investigation Process

### Step 1: Analyzing Debug Output

The interception library outputs debug messages showing syscall flow:

```
[Client] Intercepted pread(3, 0x7fffb7fb5740, 100, 0)
[Client] pread() RPC result: 0 bytes, errno=2

[Client] Intercepted pread(3, 0x7fffb7fb53f0, 16, 24)
[Client] pread() RPC result: 0 bytes, errno=0
```

**Key Observation**: The first `pread()` returned 0 bytes (correct for an empty file) but had `errno=2` (ENOENT - "No such file or directory"). This is wrong! A successful read of 0 bytes should have `errno=0`.

Server logs confirmed the operations succeeded:
```
[Server] PREAD result: 0 bytes, errno=0
```

This indicated an **errno propagation problem** between server and client.

### Step 2: Understanding SQLite's Error Detection

SQLite is extremely cautious about I/O errors because database corruption is catastrophic. When it sees a successful syscall (return value >= 0) but also finds `errno != 0`, it interprets this as an I/O error and aborts the transaction.

This is actually correct behavior from SQLite's perspective - the syscall succeeded but `errno` indicates something went wrong.

### Step 3: Tracing the Bug

Examining the RPC server code revealed the problem in multiple syscall handlers. Using `pread` as an example:

```c
// BUGGY CODE (before fix)
ssize_t bytes_read = pread(server_fd, buffer, count, req->offset);
res.err = errno;  // ❌ ALWAYS captures errno, even on success!

if (bytes_read >= 0) {
    /* Success: populate response with data */
    res.data.data_val = buffer;
    res.data.data_len = bytes_read;
    res.result = bytes_read;
} else {
    /* Failure */
    res.data.data_val = NULL;
    res.data.data_len = 0;
    res.result = -1;
}
```

**The bug**: `errno` is captured immediately after the syscall, regardless of success or failure. When the syscall succeeds, `errno` contains a stale value from a previous operation.

### Step 4: Understanding errno Semantics

From the POSIX specification:

> The value of errno should only be examined when a function call returns an error indication. When a function call succeeds, the value of errno is unspecified and may be modified.

Key points:
- `errno` is **NOT** cleared on success
- `errno` is **ONLY** set on failure
- On success, `errno` may contain garbage from previous operations

Our bug violated this by always reading `errno`, capturing stale values.

## Issues Found and Fixed

### Issue #1: Errno Propagation Bug in 11 Syscall Handlers

**Affected Handlers**:
1. `read` (src/rpc_server.c:248)
2. `pread` (src/rpc_server.c:300)
3. `write` (src/rpc_server.c:344)
4. `pwrite` (src/rpc_server.c:376)
5. `close` (src/rpc_server.c:204)
6. `lseek` (src/rpc_server.c:839)
7. `fdatasync` (src/rpc_server.c:808)
8. `unlink` (src/rpc_server.c:907-910)
9. `access` (src/rpc_server.c:885)
10. `prlimit64` (src/rpc_server.c:981-984)
11. `ioctl` (src/rpc_server.c:1036, 1064)

**Impact**:
- Successful operations incorrectly returned error codes
- Applications like SQLite interpreted these as I/O failures
- Intermittent failures depending on previous syscall history

**Fix Pattern**:

```c
// CORRECT CODE (after fix)
ssize_t bytes_read = pread(server_fd, buffer, count, req->offset);

if (bytes_read >= 0) {
    /* Success: populate response with data */
    res.data.data_val = buffer;
    res.data.data_len = bytes_read;
    res.result = bytes_read;
    res.err = 0;  // ✅ Set errno to 0 on success
} else {
    /* Failure */
    res.data.data_val = NULL;
    res.data.data_len = 0;
    res.result = -1;
    res.err = errno;  // ✅ Only capture errno on failure
}
```

**Fix Applied to All Handlers**:

1. **read/pread/write/pwrite** (similar pattern):
   - Check if result >= 0 (success)
   - Set `res.err = 0` on success
   - Set `res.err = errno` only on failure

2. **close/fdatasync** (return 0 on success, -1 on failure):
   - Check if result == 0 (success)
   - Set `res.err = 0` on success
   - Set `res.err = errno` on failure

3. **lseek** (returns offset on success, -1 on failure):
   - Check if result != (off_t)-1 (success)
   - Set `res.err = 0` on success
   - Set `res.err = errno` on failure

4. **unlink/access** (used saved_errno pattern):
   - Replaced unconditional `res.err = saved_errno`
   - Added conditional: if success, set 0; else capture errno

5. **prlimit64** (special case with bidirectional data):
   - Check if result == 0 (success)
   - Handle old_limit copying only on success
   - Set `res.err = 0` on success, errno on failure

6. **ioctl** (multiple code paths for different argument types):
   - Fixed INT argument case
   - Fixed WINSIZE argument case
   - Both now set errno=0 on success

### Issue #2: Missing pwrite64 Interception

After fixing errno propagation, SQLite still had issues. Further investigation revealed:

```bash
$ strace ./sqlite3 test.db "CREATE TABLE test (id INTEGER);" 2>&1 | grep pwrite
pwrite64(4, "\0\0\0\0\0\0\0\0...", 512, 0) = 512
pwrite64(4, "\0\0\0\1", 4, 512) = 4
pwrite64(4, "SQLite format 3\0...", 4096, 516) = 4096
```

**Problem**: SQLite uses `pwrite64()` on 64-bit systems, but our interception only provided `pwrite()`.

**Why This Happens**:
- On 64-bit Linux, there are often two versions of I/O syscalls
- `pwrite()` - legacy 32-bit offset version
- `pwrite64()` - 64-bit offset version (supports large files > 2GB)
- Modern applications use the 64-bit versions
- glibc may provide `pwrite()` as a wrapper to `pwrite64()`

**Discovery**:
Checking our existing code showed `pread64` was already present:
```c
// src/intercept/intercept_pread.h (already existed)
ssize_t pread64(int fd, void *buf, size_t count, off_t offset) {
    return pread(fd, buf, count, offset);
}
```

But `pwrite64` was missing from `src/intercept/intercept_pwrite.h`.

**Fix**:

Added to `src/intercept/intercept_pwrite.h`:
```c
/*
 * Intercepted pwrite64() function - alias to pwrite on 64-bit systems
 */
ssize_t pwrite64(int fd, const void *buf, size_t count, off64_t offset) {
    return pwrite(fd, buf, count, (off_t)offset);
}
```

**Why This Works**:
- On x86_64 Linux, `off_t` and `off64_t` are both 64-bit
- The wrapper simply forwards to our existing `pwrite()` implementation
- `pwrite()` internally uses `SYS_pwrite64` syscall anyway (line 17 of intercept_pwrite.h)
- This ensures both libc interfaces are intercepted

## Technical Deep Dive

### Understanding the Errno Flow

1. **Normal syscall path** (without interception):
   ```
   Application → libc function → syscall → kernel
                                        ↓
   Application ← errno set by libc ← return value
   ```

2. **Our interception path**:
   ```
   Application → interceptor → RPC client → RPC server → syscall → kernel
                                                                  ↓
   Application ← errno restored ← RPC client ← RPC response ← return + errno
   ```

3. **The errno gap**: Between RPC call and response, errno may be modified by:
   - RPC library internal operations (network I/O, memory allocation)
   - Other intercepted syscalls during RPC processing
   - Reentrant signal handlers

Therefore, the server **must** capture errno immediately and send it in the RPC response.

### Why the Bug Was Subtle

The bug manifested differently depending on execution history:

**Scenario A** - Bug hidden:
```c
// Previous operation succeeded (errno unmodified, might be 0)
int fd = open("file.txt", O_RDONLY);  // Success, errno unchanged
// errno might be 0 from earlier operations

ssize_t n = pread(fd, buf, 100, 0);   // Success, returns 0
res.err = errno;  // ❌ Captures 0 (coincidentally correct!)
```
Application sees: result=0, errno=0 → **Appears to work**

**Scenario B** - Bug visible:
```c
// Previous operation failed
access("nonexistent", F_OK);  // Fails, errno=2 (ENOENT)

ssize_t n = pread(fd, buf, 100, 0);   // Success, returns 0
res.err = errno;  // ❌ Captures 2 (stale ENOENT!)
```
Application sees: result=0, errno=2 → **ERROR! I/O failure**

SQLite's workload triggered Scenario B frequently because it calls `access()` to check for files before operations.

### SQLite-Specific Considerations

SQLite is particularly sensitive to errno issues because:

1. **Transaction Safety**: SQLite uses transactions for data integrity. Any I/O error must abort the transaction to prevent corruption.

2. **Journal Mode**: SQLite uses a rollback journal (test.db-journal) for crash recovery. Errors during journal operations are critical.

3. **Strict Error Checking**: SQLite checks errno even on successful operations in some cases, following defensive programming practices.

4. **Large File Support**: SQLite supports databases > 2GB, requiring 64-bit I/O operations (pwrite64/pread64).

## Testing and Validation

### Before Fixes

```bash
$ echo "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT);" | \
  LD_PRELOAD=./build/intercept.so ./sqlite3 test.db

Runtime error: disk I/O error (10)
```

**Debug output showed**:
```
[Server] PREAD result: 0 bytes, errno=0   # Server side: correct
[Client] pread() RPC result: 0 bytes, errno=2   # Client side: wrong!
```

### After errno Fixes

```bash
$ echo "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT);" | \
  LD_PRELOAD=./build/intercept.so ./sqlite3 test.db

Runtime error: disk I/O error (10)  # Still failing!
```

**Debug output showed**:
```
[Server] PREAD result: 0 bytes, errno=0   # Correct now
[Client] pread() RPC result: 0 bytes, errno=0   # Fixed!
# But pwrite64 calls are not being intercepted...
```

### After pwrite64 Fix

```bash
$ echo "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT); \
  INSERT INTO users VALUES (1, 'Alice'), (2, 'Bob'); \
  SELECT * FROM users;" | \
  LD_PRELOAD=./build/intercept.so ./sqlite3 test.db

1|Alice
2|Bob
```

**Success!** All operations completed correctly:
- Table creation
- Data insertion
- Query execution
- File synchronization (fdatasync)
- Cleanup (unlink journal file)

## Files Modified

### src/rpc_server.c

**Lines changed**: 11 separate code sections across ~1100 lines

**Modified handlers**:
- `syscall_read_1_svc()` - lines 250-265
- `syscall_pread_1_svc()` - lines 303-318
- `syscall_write_1_svc()` - lines 345-359
- `syscall_pwrite_1_svc()` - lines 382-396
- `syscall_close_1_svc()` - lines 202-213
- `syscall_lseek_1_svc()` - lines 856-869
- `syscall_fdatasync_1_svc()` - lines 820-833
- `syscall_unlink_1_svc()` - lines 905-913
- `syscall_access_1_svc()` - lines 883-891
- `syscall_prlimit64_1_svc()` - lines 982-1004
- `syscall_ioctl_1_svc()` - lines 1059-1073, 1075-1100

### src/intercept/intercept_pwrite.h

**Lines added**: 79-84

```c
/*
 * Intercepted pwrite64() function - alias to pwrite on 64-bit systems
 */
ssize_t pwrite64(int fd, const void *buf, size_t count, off64_t offset) {
    return pwrite(fd, buf, count, (off_t)offset);
}
```

## Lessons Learned

### 1. errno Semantics Are Critical

When building syscall interception layers, errno handling must be precise:
- **Always** set errno=0 explicitly on success
- **Never** read errno after successful operations
- **Immediately** capture errno after failed operations
- **Preserve** errno across RPC boundaries

### 2. 64-bit Variants Matter

Modern applications use 64-bit syscall variants:
- `pread64`, `pwrite64` instead of `pread`, `pwrite`
- `stat64`, `fstat64` instead of `stat`, `fstat`
- `lseek64` instead of `lseek`

Interception layers must handle both 32-bit and 64-bit variants, even on 64-bit systems where they may be equivalent.

### 3. Test with Real-World Applications

Unit tests with simple test programs don't reveal issues that real applications expose:
- SQLite exercises complex I/O patterns
- Error detection is more sophisticated
- Race conditions and timing issues appear
- Legacy syscall variant issues surface

### 4. Debug Output Is Essential

Without detailed debug logging showing:
- Which syscalls are intercepted
- Return values and errno at each stage
- RPC request/response contents
- Server-side execution results

This bug would have been nearly impossible to diagnose.

### 5. System Call Compatibility Layers Are Complex

Building a complete syscall interception layer requires:
- Deep understanding of POSIX semantics
- Knowledge of Linux-specific extensions
- Awareness of glibc implementation details
- Handling of 32/64-bit variants
- Proper error propagation
- Reentrant/thread-safe design
- Signal safety considerations

## Future Considerations

### Potential Issues to Watch

1. **Other 64-bit variants**: May need to add:
   - `ftruncate64` (if sqlite uses ftruncate)
   - `lseek64` (currently only have lseek)
   - `stat64`, `fstat64` (if needed)

2. **File locking**: SQLite uses fcntl() for advisory locks:
   - Currently implemented for F_GETLK, F_SETLK, F_SETLKW
   - Locks are local to RPC server, not client
   - May cause issues with concurrent access

3. **Memory-mapped I/O**: SQLite can use mmap():
   - Not currently intercepted
   - Would require complex shared memory setup
   - SQLite compiled with SQLITE_MAX_MMAP_SIZE=0 to disable

4. **File system synchronization**:
   - `fsync()` vs `fdatasync()` semantics
   - Currently only fdatasync() is intercepted
   - May need to add fsync() as well

### Testing Recommendations

1. **Comprehensive test suite**:
   - Large database operations (> 2GB files)
   - Concurrent access patterns
   - Transaction abort scenarios
   - Power-loss simulation (journal recovery)

2. **Performance benchmarking**:
   - RPC overhead vs direct syscalls
   - Impact of network latency
   - Buffer size optimization

3. **Stress testing**:
   - Long-running operations
   - Memory pressure conditions
   - High concurrency scenarios

## Conclusion

The SQLite integration issue was caused by two distinct bugs:

1. **Errno propagation bug**: Incorrect errno handling in 11 syscall handlers, causing successful operations to appear as failures
2. **Missing pwrite64 interception**: SQLite's use of 64-bit I/O functions wasn't intercepted

Both bugs were subtle and only manifested with real-world applications like SQLite that:
- Have sophisticated error detection
- Use modern 64-bit syscall variants
- Exercise complex I/O patterns
- Have strict correctness requirements

The fixes were straightforward once identified:
- Proper errno handling: set to 0 on success, capture on failure
- Add pwrite64 wrapper function to intercept 64-bit writes

These fixes make the syscall interception framework robust enough for production database applications like SQLite.

## References

- [POSIX errno specification](https://pubs.opengroup.org/onlinepubs/9699919799/functions/errno.html)
- [Linux pwrite64 man page](https://man7.org/linux/man-pages/man2/pwrite.2.html)
- [SQLite Architecture Documentation](https://www.sqlite.org/arch.html)
- [SQLite File Locking And Concurrency](https://www.sqlite.org/lockingv3.html)
