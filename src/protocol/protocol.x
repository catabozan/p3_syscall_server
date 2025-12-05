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

struct stat_request {
    string path<MAX_PATH_LEN> /* File path */;
};

struct stat_response {
    int result;
    int err;

    unsigned int mode;       /* File type and permissions */
    unsigned int size;       /* File size in bytes */
    unsigned int atime;      /* Last access time */
    unsigned int mtime;      /* Last modification time */
    unsigned int ctime;      /* Last status change time */
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
        stat_response SYSCALL_STAT(stat_request) = 5;
    } = 1;  /* Version 1 */
} = 0x20000001;  /* Program number */
