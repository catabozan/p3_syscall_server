/*
 * RPC Server for Syscall Interception
 *
 * This server receives syscall requests via RPC from intercepted clients,
 * executes them, and returns results with errno propagation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "protocol/protocol.h"
#include "transport_config.h"

/* Maximum number of file descriptors to track */
#define MAX_FDS 1024

/* File descriptor mapping: client_fd -> server_fd */
static int fd_mapping[MAX_FDS];

/* Next available client FD to assign */
static int next_client_fd = 3;  /* Start after stdin/stdout/stderr */

/* Initialize FD mapping table */
static void init_fd_mapping(void) {
    for (int i = 0; i < MAX_FDS; i++) {
        fd_mapping[i] = -1;
    }
}

/* Add FD mapping: returns client_fd to use */
static int add_fd_mapping(int server_fd) {
    if (next_client_fd >= MAX_FDS) {
        fprintf(stderr, "Error: FD mapping table full\n");
        return -1;
    }

    int client_fd = next_client_fd++;
    fd_mapping[client_fd] = server_fd;

    fprintf(stderr, "[Server] FD mapping: client_fd=%d -> server_fd=%d\n",
            client_fd, server_fd);

    return client_fd;
}

/* Add FD mapping with minimum FD requirement (for F_DUPFD) */
static int add_fd_mapping_from(int server_fd, int min_fd) {
    /* Find first available client FD >= min_fd */
    int client_fd = (min_fd > next_client_fd) ? min_fd : next_client_fd;

    /* Search for available slot */
    while (client_fd < MAX_FDS) {
        if (fd_mapping[client_fd] == -1) {
            /* Found available slot */
            fd_mapping[client_fd] = server_fd;

            /* Update next_client_fd if necessary */
            if (client_fd >= next_client_fd) {
                next_client_fd = client_fd + 1;
            }

            fprintf(stderr, "[Server] FD mapping: client_fd=%d -> server_fd=%d (min_fd=%d)\n",
                    client_fd, server_fd, min_fd);

            return client_fd;
        }
        client_fd++;
    }

    /* No available slot found */
    fprintf(stderr, "Error: FD mapping table full (min_fd=%d)\n", min_fd);
    return -1;
}

/* Remove FD mapping */
static void remove_fd_mapping(int client_fd) {
    if (client_fd >= 0 && client_fd < MAX_FDS) {
        fprintf(stderr, "[Server] Removing FD mapping: client_fd=%d -> server_fd=%d\n",
                client_fd, fd_mapping[client_fd]);
        fd_mapping[client_fd] = -1;
    }
}

/* Translate client FD to server FD */
static int translate_fd(int client_fd) {
    if (client_fd < 0 || client_fd >= MAX_FDS) {
        return -1;
    }
    return fd_mapping[client_fd];
}

/*
 * SYSCALL_OPEN implementation
 */
open_response *
syscall_open_1_svc(open_request *req, struct svc_req *rqstp) {
    static open_response res;

    fprintf(stderr, "[Server] OPEN: path=%s, flags=%d, mode=%o\n",
            req->path, req->flags, req->mode);

    /* Execute the actual open syscall */
    int server_fd = open(req->path, req->flags, req->mode);
    int saved_errno = errno;

    if (server_fd >= 0) {
        /* Success: create FD mapping */
        int client_fd = add_fd_mapping(server_fd);
        if (client_fd < 0) {
            /* Mapping failed */
            close(server_fd);
            res.fd = -1;
            res.result = -1;
            res.err = ENFILE;  /* Too many open files */
        } else {
            res.fd = client_fd;
            res.result = client_fd;
            res.err = 0;
        }
    } else {
        /* Failure */
        res.fd = -1;
        res.result = -1;
        res.err = saved_errno;
    }

    fprintf(stderr, "[Server] OPEN result: fd=%d, errno=%d\n", res.result, res.err);

    return &res;
}

/*
 * SYSCALL_OPEN implementation
 */
openat_response *
syscall_openat_1_svc(openat_request *req, struct svc_req *rqstp) {
    static openat_response res;

    fprintf(stderr, "[Server] OPENAT: dirfd=%d path=%s, flags=%d, mode=%o\n",
            req->dirfd, req->path, req->flags, req->mode);

    /* Execute the actual open syscall */
    int server_fd = openat(req->dirfd, req->path, req->flags, req->mode);
    int saved_errno = errno;

    if (server_fd >= 0) {
        /* Success: create FD mapping */
        int client_fd = add_fd_mapping(server_fd);
        if (client_fd < 0) {
            /* Mapping failed */
            close(server_fd);
            res.fd = -1;
            res.result = -1;
            res.err = ENFILE;  /* Too many open files */
        } else {
            res.fd = client_fd;
            res.result = client_fd;
            res.err = 0;
        }
    } else {
        /* Failure */
        res.fd = -1;
        res.result = -1;
        res.err = saved_errno;
    }

    fprintf(stderr, "[Server] OPENAT result: fd=%d, errno=%d\n", res.result, res.err);

    return &res;
}

/*
 * SYSCALL_CLOSE implementation
 */
close_response *
syscall_close_1_svc(close_request *req, struct svc_req *rqstp) {
    static close_response res;

    fprintf(stderr, "[Server] CLOSE: client_fd=%d\n", req->fd);

    /* Translate client FD to server FD */
    int server_fd = translate_fd(req->fd);

    if (server_fd < 0) {
        /* Invalid FD */
        res.result = -1;
        res.err = EBADF;
        fprintf(stderr, "[Server] CLOSE failed: invalid client_fd=%d\n", req->fd);
    } else {
        /* Execute the actual close syscall */
        res.result = close(server_fd);
        res.err = errno;

        /* Remove FD mapping */
        if (res.result == 0) {
            remove_fd_mapping(req->fd);
        }

        fprintf(stderr, "[Server] CLOSE result: %d, errno=%d\n", res.result, res.err);
    }

    return &res;
}

/*
 * SYSCALL_READ implementation
 */
read_response *
syscall_read_1_svc(read_request *req, struct svc_req *rqstp) {
    static read_response res;
    static char buffer[MAX_BUFFER_SIZE];

    fprintf(stderr, "[Server] READ: client_fd=%d, count=%u\n", req->fd, req->count);

    /* Clear previous data */
    res.data.data_val = NULL;
    res.data.data_len = 0;

    /* Translate client FD to server FD */
    int server_fd = translate_fd(req->fd);

    if (server_fd < 0) {
        /* Invalid FD */
        res.result = -1;
        res.err = EBADF;
        fprintf(stderr, "[Server] READ failed: invalid client_fd=%d\n", req->fd);
    } else {
        /* Ensure count doesn't exceed buffer size */
        unsigned int count = req->count;
        if (count > MAX_BUFFER_SIZE) {
            count = MAX_BUFFER_SIZE;
        }

        /* Execute the actual read syscall */
        ssize_t bytes_read = read(server_fd, buffer, count);
        res.err = errno;

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

        fprintf(stderr, "[Server] READ result: %zd bytes, errno=%d\n",
                bytes_read, res.err);
    }

    return &res;
}

/*
 * SYSCALL_PREAD implementation
 */
pread_response *
syscall_pread_1_svc(pread_request *req, struct svc_req *rqstp) {
    static pread_response res;
    static char buffer[MAX_BUFFER_SIZE];

    fprintf(stderr, "[Server] PREAD: client_fd=%d, count=%u, offset=%u\n", req->fd, req->count, req->offset);

    /* Clear previous data */
    res.data.data_val = NULL;
    res.data.data_len = 0;

    /* Translate client FD to server FD */
    int server_fd = translate_fd(req->fd);

    if (server_fd < 0) {
        /* Invalid FD */
        res.result = -1;
        res.err = EBADF;
        fprintf(stderr, "[Server] PREAD failed: invalid client_fd=%d\n", req->fd);
    } else {
        /* Ensure count doesn't exceed buffer size */
        unsigned int count = req->count;
        if (count > MAX_BUFFER_SIZE) {
            count = MAX_BUFFER_SIZE;
        }

        /* Execute the actual read syscall */
        ssize_t bytes_read = pread(server_fd, buffer, count, req->offset);
        res.err = errno;

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

        fprintf(stderr, "[Server] PREAD result: %zd bytes, errno=%d\n",
                bytes_read, res.err);
    }

    return &res;
}

/*
 * SYSCALL_WRITE implementation
 */
write_response *
syscall_write_1_svc(write_request *req, struct svc_req *rqstp) {
    static write_response res;

    fprintf(stderr, "[Server] WRITE: client_fd=%d, count=%u\n",
            req->fd, req->data.data_len);

    /* Translate client FD to server FD */
    int server_fd = translate_fd(req->fd);

    if (server_fd < 0) {
        /* Invalid FD */
        res.result = -1;
        res.err = EBADF;
        fprintf(stderr, "[Server] WRITE failed: invalid client_fd=%d\n", req->fd);
    } else {
        /* Execute the actual write syscall */
        ssize_t bytes_written = write(server_fd, req->data.data_val,
                                      req->data.data_len);
        res.result = bytes_written;
        res.err = errno;

        fprintf(stderr, "[Server] WRITE result: %zd bytes, errno=%d\n",
                bytes_written, res.err);
    }

    return &res;
}

/*
 * SYSCALL_PWRITE implementation
 */
pwrite_response *
syscall_pwrite_1_svc(pwrite_request *req, struct svc_req *rqstp) {
    static pwrite_response res;

    fprintf(stderr, "[Server] PWRITE: client_fd=%d, count=%u, offset=%u\n",
            req->fd, req->data.data_len, req->offset);

    /* Translate client FD to server FD */
    int server_fd = translate_fd(req->fd);

    if (server_fd < 0) {
        /* Invalid FD */
        res.result = -1;
        res.err = EBADF;
        fprintf(stderr, "[Server] WRITE failed: invalid client_fd=%d\n", req->fd);
    } else {
        /* Execute the actual write syscall */
        ssize_t bytes_written = pwrite(server_fd, req->data.data_val,
                                      req->data.data_len, req->offset);
        res.result = bytes_written;
        res.err = errno;

        fprintf(stderr, "[Server] WRITE result: %zd bytes, errno=%d\n",
                bytes_written, res.err);
    }

    return &res;
}

/*
 * SYSCALL_STAT implementation
 */
stat_response *
syscall_stat_1_svc(stat_request *req, struct svc_req *rqstp) {
    static stat_response res;
    struct stat statbuf;

    fprintf(stderr, "[Server] STAT: path=%s\n", req->path);

    /* Execute the actual stat syscall */
    int stat_result = stat(req->path, &statbuf);
    int saved_errno = errno;

    if (stat_result == 0) {
        /* Success: populate response with stat data */
        res.result = 0;
        res.err = 0;
        res.dev = statbuf.st_dev;
        res.ino = statbuf.st_ino;
        res.mode = statbuf.st_mode;
        res.nlink = statbuf.st_nlink;
        res.uid = statbuf.st_uid;
        res.gid = statbuf.st_gid;
        res.rdev = statbuf.st_rdev;
        res.size = statbuf.st_size;
        res.blksize = statbuf.st_blksize;
        res.blocks = statbuf.st_blocks;
        res.atime = statbuf.st_atime;
        res.mtime = statbuf.st_mtime;
        res.ctime = statbuf.st_ctime;

        fprintf(stderr, "[Server] STAT result: mode=%o, size=%u, errno=%d\n",
                res.mode, res.size, res.err);
    } else {
        /* Failure */
        res.result = -1;
        res.err = 0;
        res.dev = 0;
        res.ino = 0;
        res.mode = 0;
        res.nlink = 0;
        res.uid = 0;
        res.gid = 0;
        res.rdev = 0;
        res.size = 0;
        res.blksize = 0;
        res.blocks = 0;
        res.atime = 0;
        res.mtime = 0;
        res.ctime = 0;

        fprintf(stderr, "[Server] STAT failed: errno=%d\n", res.err);
    }

    return &res;
}
/*
 * SYSCALL_NEWFSTATAT implementation
 */
newfstatat_response *
syscall_newfstatat_1_svc(newfstatat_request *req, struct svc_req *rqstp) {
    static newfstatat_response res;
    struct stat statbuf;

    fprintf(stderr, "[Server] NEWFSTATAT: dirfd=%d path=%s flags=%u\n", req->dirfd, req->path, req->flags);

    /* Execute the actual stat syscall */
    int stat_result = stat(req->path, &statbuf);
    int saved_errno = errno;

    if (stat_result == 0) {
        /* Success: populate response with stat data */
        res.result = 0;
        res.err = 0;
        res.dev = statbuf.st_dev;
        res.ino = statbuf.st_ino;
        res.mode = statbuf.st_mode;
        res.nlink = statbuf.st_nlink;
        res.uid = statbuf.st_uid;
        res.gid = statbuf.st_gid;
        res.rdev = statbuf.st_rdev;
        res.size = statbuf.st_size;
        res.blksize = statbuf.st_blksize;
        res.blocks = statbuf.st_blocks;
        res.atime = statbuf.st_atime;
        res.mtime = statbuf.st_mtime;
        res.ctime = statbuf.st_ctime;

        fprintf(stderr, "[Server] NEWFSTATAT result: mode=%o, size=%u, errno=%d\n",
                res.mode, res.size, res.err);
    } else {
        /* Failure */
        res.result = -1;
        res.err = 0;
        res.dev = 0;
        res.ino = 0;
        res.mode = 0;
        res.nlink = 0;
        res.uid = 0;
        res.gid = 0;
        res.rdev = 0;
        res.size = 0;
        res.blksize = 0;
        res.blocks = 0;
        res.atime = 0;
        res.mtime = 0;
        res.ctime = 0;

        fprintf(stderr, "[Server] NEWFSTATAT failed: errno=%d\n", res.err);
    }

    return &res;
}

/*
 * SYSCALL_FSTAT implementation
 */
fstat_response *
syscall_fstat_1_svc(fstat_request *req, struct svc_req *rqstp) {
    static fstat_response res;
    struct stat statbuf;

    fprintf(stderr, "[Server] FSTAT: fd=%d\n", req->fd);

    /* Translate client FD to server FD */
    int server_fd = translate_fd(req->fd);

    /* Execute the actual stat syscall */
    int stat_result = fstat(server_fd, &statbuf);
    int saved_errno = errno;

    if (stat_result == 0) {
        /* Success: populate response with stat data */
        res.result = 0;
        res.err = 0;
        res.dev = statbuf.st_dev;
        res.ino = statbuf.st_ino;
        res.mode = statbuf.st_mode;
        res.nlink = statbuf.st_nlink;
        res.uid = statbuf.st_uid;
        res.gid = statbuf.st_gid;
        res.rdev = statbuf.st_rdev;
        res.size = statbuf.st_size;
        res.blksize = statbuf.st_blksize;
        res.blocks = statbuf.st_blocks;
        res.atime = statbuf.st_atime;
        res.mtime = statbuf.st_mtime;
        res.ctime = statbuf.st_ctime;

        fprintf(stderr, "[Server] FSTAT result: mode=%o, size=%u, errno=%d\n",
                res.mode, res.size, res.err);
    } else {
        /* Failure */
        res.result = -1;
        res.err = 0;
        res.dev = 0;
        res.ino = 0;
        res.mode = 0;
        res.nlink = 0;
        res.uid = 0;
        res.gid = 0;
        res.rdev = 0;
        res.size = 0;
        res.blksize = 0;
        res.blocks = 0;
        res.atime = 0;
        res.mtime = 0;
        res.ctime = 0;

        fprintf(stderr, "[Server] FSTAT failed: errno=%d\n", res.err);
    }

    return &res;
}

/*
 * SYSCALL_FCNTL implementation
 */
fcntl_response *
syscall_fcntl_1_svc(fcntl_request *req, struct svc_req *rqstp) {
    static fcntl_response res;

    fprintf(stderr, "[Server] FCNTL: client_fd=%d, cmd=%d\n", req->fd, req->cmd);

    /* Initialize response */
    memset(&res, 0, sizeof(res));
    res.arg_out.type = FCNTL_ARG_NONE;

    /* Translate client FD to server FD */
    int server_fd = translate_fd(req->fd);

    if (server_fd < 0) {
        /* Invalid FD */
        res.result = -1;
        res.err = EBADF;
        fprintf(stderr, "[Server] FCNTL failed: invalid client_fd=%d\n", req->fd);
        return &res;
    }

    /* Prepare argument based on union type */
    int int_arg = 0;
    struct flock flock_arg;
    void *arg_ptr = NULL;

    switch(req->arg.type) {
        case FCNTL_ARG_NONE:
            arg_ptr = NULL;
            break;

        case FCNTL_ARG_INT:
            int_arg = req->arg.fcntl_arg_u.int_arg;
            arg_ptr = (void *)(long)int_arg;
            break;

        case FCNTL_ARG_FLOCK:
            /* Convert XDR flock_data to native struct flock */
            memset(&flock_arg, 0, sizeof(flock_arg));
            flock_arg.l_type = req->arg.fcntl_arg_u.flock_arg.l_type;
            flock_arg.l_whence = req->arg.fcntl_arg_u.flock_arg.l_whence;
            flock_arg.l_start = (off_t)req->arg.fcntl_arg_u.flock_arg.l_start;
            flock_arg.l_len = (off_t)req->arg.fcntl_arg_u.flock_arg.l_len;
            flock_arg.l_pid = req->arg.fcntl_arg_u.flock_arg.l_pid;
            arg_ptr = &flock_arg;
            break;
    }

    /* Execute the fcntl syscall */
    int result;
    if (req->arg.type == FCNTL_ARG_NONE) {
        result = fcntl(server_fd, req->cmd);
    } else if (req->arg.type == FCNTL_ARG_INT) {
        result = fcntl(server_fd, req->cmd, int_arg);
    } else {
        result = fcntl(server_fd, req->cmd, arg_ptr);
    }

    int saved_errno = errno;

    /* Handle success */
    if (result >= 0) {
        /* Special handling for F_DUPFD and F_DUPFD_CLOEXEC */
        if (req->cmd == F_DUPFD || req->cmd == F_DUPFD_CLOEXEC) {
            /* result is a new server FD, we need to create a client FD mapping */
            /* Use the minimum FD from the int_arg */
            int new_client_fd = add_fd_mapping_from(result, int_arg);
            if (new_client_fd < 0) {
                /* Mapping failed */
                close(result);
                res.result = -1;
                res.err = ENFILE;
                fprintf(stderr, "[Server] FCNTL F_DUPFD failed: FD mapping table full\n");
                return &res;
            }
            res.result = new_client_fd;
            res.err = 0;
        } else {
            res.result = result;
            res.err = 0;

            /* Special handling for F_GETLK - copy modified flock back */
            if (req->cmd == F_GETLK && req->arg.type == FCNTL_ARG_FLOCK) {
                res.arg_out.type = FCNTL_ARG_FLOCK;
                res.arg_out.fcntl_arg_u.flock_arg.l_type = flock_arg.l_type;
                res.arg_out.fcntl_arg_u.flock_arg.l_whence = flock_arg.l_whence;
                res.arg_out.fcntl_arg_u.flock_arg.l_start = flock_arg.l_start;
                res.arg_out.fcntl_arg_u.flock_arg.l_len = flock_arg.l_len;
                res.arg_out.fcntl_arg_u.flock_arg.l_pid = flock_arg.l_pid;
            }
        }
    } else {
        /* Error */
        res.result = -1;
        res.err = saved_errno;
    }

    fprintf(stderr, "[Server] FCNTL result: %d, errno=%d\n", res.result, res.err);

    return &res;
}

/*
 * Free result function (required by RPC infrastructure)
 */
int
syscall_prog_1_freeresult(SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result) {
    xdr_free(xdr_result, result);
    return 1;
}

/*
 * Main server function
 */
int main(int argc, char *argv[]) {
    SVCXPRT *transp = NULL;
    transport_type_t transport;
    extern void syscall_prog_1(struct svc_req *, register SVCXPRT *);

    fprintf(stderr, "[Server] Starting RPC server...\n");

    /* Initialize FD mapping */
    init_fd_mapping();

    /* Get transport configuration */
    transport = get_transport_type();
    fprintf(stderr, "[Server] Using %s transport\n", get_transport_name(transport));

    /* Remove old registration */
    pmap_unset(SYSCALL_PROG, SYSCALL_VERS);

    if (transport == TRANSPORT_UNIX) {
        /* For UNIX sockets, manually accept and create transports */
        int listen_sock, conn_sock;
        struct sockaddr_un addr;
        socklen_t addr_len;

        listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_sock < 0) {
            perror("socket");
            exit(1);
        }

        /* Remove old socket if it exists */
        unlink(UNIX_SOCKET_PATH);

        /* Bind to socket path */
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, UNIX_SOCKET_PATH, sizeof(addr.sun_path) - 1);

        if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("bind");
            close(listen_sock);
            exit(1);
        }

        if (listen(listen_sock, 5) < 0) {
            perror("listen");
            close(listen_sock);
            exit(1);
        }

        fprintf(stderr, "[Server] RPC server ready at %s\n", UNIX_SOCKET_PATH);
        fprintf(stderr, "[Server] Waiting for connections...\n");

        /* Accept first connection */
        addr_len = sizeof(addr);
        conn_sock = accept(listen_sock, (struct sockaddr *)&addr, &addr_len);
        if (conn_sock < 0) {
            perror("accept");
            close(listen_sock);
            exit(1);
        }

        fprintf(stderr, "[Server] Accepted connection\n");

        /* Create RPC transport for this connection */
        transp = svcfd_create(conn_sock, 0, 0);
        if (transp == NULL) {
            fprintf(stderr, "Error: svcfd_create failed\n");
            close(conn_sock);
            close(listen_sock);
            exit(1);
        }

        /* Register service */
        if (!svc_register(transp, SYSCALL_PROG, SYSCALL_VERS, syscall_prog_1, 0)) {
            fprintf(stderr, "Error: unable to register service\n");
            SVC_DESTROY(transp);
            close(listen_sock);
            exit(1);
        }

        close(listen_sock);  /* Don't accept more connections for now */

    } else {  /* TRANSPORT_TCP */
        /* Create TCP transport */
        transp = svctcp_create(RPC_ANYSOCK, 0, 0);
        if (transp == NULL) {
            fprintf(stderr, "Error: cannot create TCP service\n");
            exit(1);
        }

        /* Register service with portmapper */
        if (!svc_register(transp, SYSCALL_PROG, SYSCALL_VERS, syscall_prog_1, IPPROTO_TCP)) {
            fprintf(stderr, "Error: unable to register (SYSCALL_PROG, SYSCALL_VERS, tcp)\n");
            exit(1);
        }

        fprintf(stderr, "[Server] RPC server ready on TCP port %d\n", transp->xp_port);
    }

    fprintf(stderr, "[Server] Waiting for requests...\n");

    /* Enter service loop */
    svc_run();

    fprintf(stderr, "[Server] svc_run returned (should never happen)\n");
    exit(1);
}

/*
 * SYSCALL_FDATASYNC implementation
 */
fdatasync_response *
syscall_fdatasync_1_svc(fdatasync_request *req, struct svc_req *rqstp) {
    static fdatasync_response res;

    fprintf(stderr, "[Server] FDATASYNC: client_fd=%d\n",
            req->fd);

    /* Translate client FD to server FD */
    int server_fd = translate_fd(req->fd);

    if (server_fd < 0) {
        /* Invalid FD */
        res.result = -1;
        res.err = EBADF;
        fprintf(stderr, "[Server] WRITE FDATASYNC: invalid client_fd=%d\n", req->fd);
    } else {
        /* Execute the actual write syscall */
        ssize_t bytes_written = fdatasync(server_fd);
        res.result = bytes_written;
        res.err = errno;

        fprintf(stderr, "[Server] FDATASYNC result: %zd, errno=%d\n",
                res.result, res.err);
    }

    return &res;
}