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
 * Openat syscall structures
 */
struct openat_request {
    int dirfd;                   /* Base directory file descriptor */
    string path<MAX_PATH_LEN>;   /* File path */
    int flags;                   /* Open flags (O_RDONLY, O_WRONLY, etc.) */
    unsigned int mode;           /* Permission mode */
};

struct openat_response {
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
 * PRead syscall structures
 */
struct pread_request {
    int fd;           /* File descriptor to read from */
    long offset;
    unsigned int count;  /* Number of bytes to read */
};

struct pread_response {
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
 * Pwrite syscall structures
 */
struct pwrite_request {
    int fd;                         /* File descriptor to write to */
    long offset;
    opaque data<MAX_BUFFER_SIZE>;  /* Variable-length buffer with data to write */
};

struct pwrite_response {
    int result;  /* Number of bytes written, or -1 on error */
    int err;     /* errno value */
};

/*
 * Stat syscall structures
 */
struct stat_request {
    string path<MAX_PATH_LEN> /* File path */;
};

struct stat_response {
    int result;
    int err;

    /* Device and inode info */
    unsigned long dev;   /* Device ID containing file */
    unsigned long ino;   /* Inode number */

    /* File type, permissions, and link count */
    unsigned int mode;   /* File type and permissions (S_IF*) */
    unsigned int nlink;  /* Number of hard links */

    /* Ownership */
    unsigned int uid;    /* User ID of owner */
    unsigned int gid;    /* Group ID of owner */

    /* Special files (character/block devices) */
    unsigned long rdev;  /* Device ID if special file */

    /* File size and block allocation */
    unsigned hyper size;     /* File size in bytes */
    unsigned long blksize;       /* Block size for I/O */
    unsigned hyper blocks;   /* Number of 512-byte blocks allocated */

    /* Timestamps (seconds since epoch) */
    unsigned long atime; /* Last access time */
    unsigned long mtime; /* Last modification time */
    unsigned long ctime; /* Last status change time */
};

/*
 * Newfstatat syscall structures
 */
struct newfstatat_request {
    int dirfd;                   /* Base directory file descriptor */
    string path<MAX_PATH_LEN>;   /* File path (relative to dirfd or absolute) */
    int flags;                   /* Flags controlling behavior */
                                 /* e.g., 0, AT_SYMLINK_NOFOLLOW, AT_EMPTY_PATH */
};

/* Response structure for newfstatat */
struct newfstatat_response {
    int result; /* 0 = success, -1 = failure */
    int err;    /* errno value if result == -1 */

    /* Device and inode info */
    unsigned long dev;   /* Device ID containing file */
    unsigned long ino;   /* Inode number */

    /* File type, permissions, and link count */
    unsigned int mode;   /* File type and permissions (S_IF*) */
    unsigned int nlink;  /* Number of hard links */

    /* Ownership */
    unsigned int uid;    /* User ID of owner */
    unsigned int gid;    /* Group ID of owner */

    /* Special files (character/block devices) */
    unsigned long rdev;  /* Device ID if special file */

    /* File size and block allocation */
    unsigned hyper size;     /* File size in bytes */
    unsigned long blksize;       /* Block size for I/O */
    unsigned hyper blocks;   /* Number of 512-byte blocks allocated */

    /* Timestamps (seconds since epoch) */
    unsigned long atime; /* Last access time */
    unsigned long mtime; /* Last modification time */
    unsigned long ctime; /* Last status change time */
};

struct fstat_request {
    int fd /* File descriptor */;
};

struct fstat_response {
    int result;
    int err;

    /* Device and inode info */
    unsigned long dev;   /* Device ID containing file */
    unsigned long ino;   /* Inode number */

    /* File type, permissions, and link count */
    unsigned int mode;   /* File type and permissions (S_IF*) */
    unsigned int nlink;  /* Number of hard links */

    /* Ownership */
    unsigned int uid;    /* User ID of owner */
    unsigned int gid;    /* Group ID of owner */

    /* Special files (character/block devices) */
    unsigned long rdev;  /* Device ID if special file */

    /* File size and block allocation */
    unsigned hyper size;     /* File size in bytes */
    unsigned long blksize;       /* Block size for I/O */
    unsigned hyper blocks;   /* Number of 512-byte blocks allocated */

    /* Timestamps (seconds since epoch) */
    unsigned long atime; /* Last access time */
    unsigned long mtime; /* Last modification time */
    unsigned long ctime; /* Last status change time */
};

/*
 * fcntl syscall structures
 */

/* File locking structure */
struct flock_data {
    short l_type;       /* F_RDLCK, F_WRLCK, F_UNLCK */
    short l_whence;     /* SEEK_SET, SEEK_CUR, SEEK_END */
    hyper l_start;      /* Starting offset (64-bit) */
    hyper l_len;        /* Length (64-bit, 0 = EOF) */
    int l_pid;          /* Process ID */
};

/* Argument type discriminator */
enum fcntl_arg_type {
    FCNTL_ARG_NONE = 0,    /* No argument needed */
    FCNTL_ARG_INT = 1,     /* Integer argument */
    FCNTL_ARG_FLOCK = 2    /* struct flock argument */
};

/* Discriminated union for fcntl arguments */
union fcntl_arg switch (fcntl_arg_type type) {
    case FCNTL_ARG_NONE:
        void;
    case FCNTL_ARG_INT:
        int int_arg;
    case FCNTL_ARG_FLOCK:
        flock_data flock_arg;
};

struct fcntl_request {
    int fd;                 /* File descriptor */
    int cmd;                /* fcntl command */
    fcntl_arg arg;          /* Argument (discriminated union) */
};

struct fcntl_response {
    int result;             /* Return value */
    int err;                /* errno value */
    fcntl_arg arg_out;      /* Output argument (for F_GETLK) */
};

struct fdatasync_request {
    int fd;                 /* File descriptor */
};

struct fdatasync_response {
    int result;             /* Return value */
    int err;                /* errno value */
};


/*
 * RPC Program Definition
 */
program SYSCALL_PROG {
    version SYSCALL_VERS {
        open_response SYSCALL_OPEN(open_request) = 1;
        openat_response SYSCALL_OPENAT(openat_request) = 2;
        close_response SYSCALL_CLOSE(close_request) = 3;
        read_response SYSCALL_READ(read_request) = 4;
        pread_response SYSCALL_PREAD(pread_request) = 5;
        write_response SYSCALL_WRITE(write_request) = 6;
        pwrite_response SYSCALL_PWRITE(pwrite_request) = 7;
        stat_response SYSCALL_STAT(stat_request) = 8;
        newfstatat_response SYSCALL_NEWFSTATAT(newfstatat_request) = 9;
        fstat_response SYSCALL_FSTAT(fstat_request) = 10;
        fcntl_response SYSCALL_FCNTL(fcntl_request) = 11;
        fdatasync_response SYSCALL_FDATASYNC(fdatasync_request) = 12;
    } = 1;  /* Version 1 */
} = 0x20000001;  /* Program number */
