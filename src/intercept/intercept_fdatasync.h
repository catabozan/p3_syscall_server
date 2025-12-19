/*
 * fdatasync() syscall interceptor
 */

#ifndef __INTERCEPT_FDATASYNC_
#define __INTERCEPT_FDATASYNC_

#include <sys/stat.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>

/* Thread-local reentry guard */
static __thread int in_fdatasync_intercept = 0;

/*
 * Intercepted fdatasync() function
 */
int fdatasync(int fd) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_fdatasync_intercept || is_rpc_in_progress()) {
        return syscall(SYS_fdatasync, fd);
    }

    /* Set guard */
    in_fdatasync_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted fdatasync(%d, buf)\n",
                          fd);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        fdatasync_request req;
        req.fd = fd;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        fdatasync_response *res = syscall_fdatasync_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            if (result >= 0) {
                
                msg_len = snprintf(debug_msg, sizeof(debug_msg),
                                  "[Client] fdatasync() RPC result: %d, errno=%d\n",
                                  result, errno);
                syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
            } else {
                /* RPC returned error */
                msg_len = snprintf(debug_msg, sizeof(debug_msg),
                                  "[Client] fdatasync() RPC returned error: %d, errno=%d\n",
                                  result, errno);
                syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
            }
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] fdatasync() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_fdatasync, fd);
    }

    /* Clear guard */
    in_fdatasync_intercept = 0;

    return result;
}

#endif
