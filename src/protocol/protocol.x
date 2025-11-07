/*
 * ONC RPC/XDR Protocol Definition for Syscall Interception
 *
 * This file defines the RPC interface for intercepting and forwarding
 * syscalls (open, close, read, write) from client to server.
 */

/* Maximum path length for open() */
const MAX_PATH_LEN = 4096;

/* Maximum buffer size for read/write operations */
const MAX_BUFFER_SIZE = 1048576;  /* 1MB */

/*
 * Open syscall structures
 */
struct open_request {
    string path<MAX_PATH_LEN>;  /* File path */
    int flags;                   /* Open flags (O_RDONLY, O_WRONLY, etc.) */
    unsigned int mode;           /* Permission mode */
};

struct open_response {
    int fd;          /* File descriptor (client-side mapped FD) */
    int result;      /* Return value: fd on success, -1 on error */
    int err;         /* errno value */
};

/*
 * Close syscall structures
 */
struct close_request {
    int fd;  /* File descriptor to close */
};

struct close_response {
    int result;  /* Return value: 0 on success, -1 on error */
    int err;     /* errno value */
};

/*
 * Read syscall structures
 */
struct read_request {
    int fd;           /* File descriptor to read from */
    unsigned int count;  /* Number of bytes to read */
};

struct read_response {
    opaque data<MAX_BUFFER_SIZE>;  /* Variable-length buffer with data read */
    int result;                     /* Number of bytes read, or -1 on error */
    int err;                        /* errno value */
};

/*
 * Write syscall structures
 */
struct write_request {
    int fd;                         /* File descriptor to write to */
    opaque data<MAX_BUFFER_SIZE>;  /* Variable-length buffer with data to write */
};

struct write_response {
    int result;  /* Number of bytes written, or -1 on error */
    int err;     /* errno value */
};

/*
 * RPC Program Definition
 */
program SYSCALL_PROG {
    version SYSCALL_VERS {
        open_response SYSCALL_OPEN(open_request) = 1;
        close_response SYSCALL_CLOSE(close_request) = 2;
        read_response SYSCALL_READ(read_request) = 3;
        write_response SYSCALL_WRITE(write_request) = 4;
    } = 1;  /* Version 1 */
} = 0x20000001;  /* Program number */
