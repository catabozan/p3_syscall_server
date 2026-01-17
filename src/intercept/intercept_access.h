/*
 * access() syscall interceptor
 */

#ifndef __INTERCEPT_ACCESS_
#define __INTERCEPT_ACCESS_

#include <unistd.h>
#include <fcntl.h>

/* Thread-local reentry guard */
static __thread int in_access_intercept = 0;

/*
 * Intercepted access() function
 */
int access(const char *pathname, int mode) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_access_intercept || is_rpc_in_progress()) {
        return syscall(SYS_access, pathname, mode);
    }

    /* Set guard */
    in_access_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted access(\"%s\", %d)\n",
                          pathname, mode);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        access_request req;
        req.path = (char *)pathname;
        req.mode = mode;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        access_response *res = syscall_access_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] access() RPC result: %d, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] access() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_access, pathname, mode);
    }

    /* Clear guard */
    in_access_intercept = 0;

    return result;
}

/*
 * faccessat() wrapper - simplified implementation
 * Only handles AT_FDCWD with flags=0, falls back to direct syscall otherwise
 */
int faccessat(int dirfd, const char *pathname, int mode, int flags) {
    if (dirfd == AT_FDCWD && flags == 0) {
        /* Can use access() */
        return access(pathname, mode);
    }
    /* Full faccessat needs separate RPC - use direct syscall */
    return syscall(SYS_faccessat, dirfd, pathname, mode, flags);
}

#endif
