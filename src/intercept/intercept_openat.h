/*
 * openat() syscall interceptor
 */

#ifndef __INTERCEPT_OPENAT_
#define __INTERCEPT_OPENAT_

#include <stdarg.h>
#include <fcntl.h>

/* Thread-local reentry guard */
static __thread int in_openat_intercept = 0;

/*
 * Intercepted openat() function
 */
int openat(int dirfd, const char *pathname, int flags, ...) {
    /* Handle optional mode argument */
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_openat_intercept || is_rpc_in_progress()) {
        return syscall(SYS_openat, dirfd, pathname, flags, mode);
    }

    /* Set guard */
    in_openat_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted openat(%d, \"%s\", %d, %o)\n",
                          dirfd, pathname, flags, mode);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        openat_request req;
        req.dirfd = dirfd;
        req.path = (char *)pathname;
        req.flags = flags;
        req.mode = mode;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        openat_response *res = syscall_openat_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] openat() RPC result: fd=%d, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] openat() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_openat, dirfd, pathname, flags, mode);
    }

    /* Clear guard */
    in_openat_intercept = 0;

    return result;
}

#endif
