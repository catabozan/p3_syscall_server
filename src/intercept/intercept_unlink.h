/*
 * unlink() syscall interceptor
 */

#ifndef __INTERCEPT_UNLINK_
#define __INTERCEPT_UNLINK_

#include <unistd.h>
#include <fcntl.h>

/* Thread-local reentry guard */
static __thread int in_unlink_intercept = 0;

/*
 * Intercepted unlink() function
 */
int unlink(const char *pathname) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_unlink_intercept || is_rpc_in_progress()) {
        return syscall(SYS_unlink, pathname);
    }

    /* Set guard */
    in_unlink_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted unlink(\"%s\")\n",
                          pathname);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        unlink_request req;
        req.path = (char *)pathname;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        unlink_response *res = syscall_unlink_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] unlink() RPC result: %d, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] unlink() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_unlink, pathname);
    }

    /* Clear guard */
    in_unlink_intercept = 0;

    return result;
}

/*
 * unlinkat() wrapper - simplified implementation
 * Only handles AT_FDCWD with flags=0, falls back to direct syscall otherwise
 */
int unlinkat(int dirfd, const char *pathname, int flags) {
    if (dirfd == AT_FDCWD && flags == 0) {
        /* Can use unlink() */
        return unlink(pathname);
    }
    /* Full unlinkat needs separate RPC - use direct syscall */
    return syscall(SYS_unlinkat, dirfd, pathname, flags);
}

#endif
