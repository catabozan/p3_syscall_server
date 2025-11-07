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
