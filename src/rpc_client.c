/*
 * RPC Client for Syscall Interception
 *
 * This client library is loaded via LD_PRELOAD and intercepts syscalls,
 * forwarding them to the RPC server via persistent connection.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <rpc/rpc.h>
#include "protocol/protocol.h"
#include "transport_config.h"

/* Thread-local RPC client handle for persistent connection */
static __thread CLIENT *rpc_client = NULL;

/* Thread-local flag to prevent initialization loops */
static __thread int in_rpc_init = 0;

/* Global flag to disable ALL interception during RPC operations */
static __thread int rpc_in_progress = 0;

/*
 * Check if we're inside an RPC operation (for interceptor guards)
 */
static inline int is_rpc_in_progress(void) {
    return rpc_in_progress || in_rpc_init;
}

/*
 * Initialize RPC client connection (lazy initialization)
 * Returns CLIENT* on success, NULL on failure
 */
CLIENT *get_rpc_client(void) {
    transport_type_t transport;

    /* If already initialized, return existing client */
    if (rpc_client != NULL) {
        return rpc_client;
    }

    /* Prevent recursive initialization */
    if (in_rpc_init) {
        return NULL;
    }

    in_rpc_init = 1;
    rpc_in_progress = 1;  /* Disable all interception during init */

    /* Get transport configuration */
    transport = get_transport_type();

    if (transport == TRANSPORT_UNIX) {
        /* Create UNIX domain socket manually */
        int sock;
        struct sockaddr_un server_addr;

        sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) {
            const char *msg = "[Client] Failed to create UNIX socket\n";
            syscall(SYS_write, STDERR_FILENO, msg, strlen(msg));
            goto error;
        }

        /* Connect to server */
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sun_family = AF_UNIX;
        strncpy(server_addr.sun_path, UNIX_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            const char *msg = "[Client] Failed to connect to UNIX socket\n";
            syscall(SYS_write, STDERR_FILENO, msg, strlen(msg));
            close(sock);
            goto error;
        }

        /* Create RPC client from connected socket using clnt_vc_create */
        struct netbuf svcaddr;
        svcaddr.len = svcaddr.maxlen = sizeof(server_addr);
        svcaddr.buf = (char *)&server_addr;

        rpc_client = clnt_vc_create(sock, &svcaddr, SYSCALL_PROG, SYSCALL_VERS, 0, 0);
        if (rpc_client == NULL) {
            const char *msg = "[Client] Failed to create RPC client from UNIX socket\n";
            syscall(SYS_write, STDERR_FILENO, msg, strlen(msg));
            close(sock);
            goto error;
        }

    } else {  /* TRANSPORT_TCP */
        /* Create TCP RPC client */
        rpc_client = clnt_create(TCP_HOST, SYSCALL_PROG, SYSCALL_VERS, "tcp");
        if (rpc_client == NULL) {
            const char *msg = "[Client] Failed to connect via TCP\n";
            syscall(SYS_write, STDERR_FILENO, msg, strlen(msg));
            goto error;
        }
    }

error:
    in_rpc_init = 0;
    rpc_in_progress = 0;  /* Re-enable interception */
    return rpc_client;
}

/*
 * Cleanup function called when thread exits
 */
static void cleanup_rpc_client(void) __attribute__((destructor));

static void cleanup_rpc_client(void) {
    if (rpc_client != NULL) {
        clnt_destroy(rpc_client);
        rpc_client = NULL;
    }
}

/*
 * Include all interceptor implementations
 */
#include "intercept/intercept_open.h"
#include "intercept/intercept_close.h"
#include "intercept/intercept_read.h"
#include "intercept/intercept_write.h"
#include "intercept/intercept_stat.h"
