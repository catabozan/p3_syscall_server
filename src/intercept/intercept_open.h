/*
 * open() syscall interceptor
 */

#include <stdarg.h>
#include <fcntl.h>

/* Thread-local reentry guard */
static __thread int in_open_intercept = 0;

/*
 * Intercepted open() function
 */
int open(const char *pathname, int flags, ...) {
    /* Handle optional mode argument */
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_open_intercept || is_rpc_in_progress()) {
        return syscall(SYS_open, pathname, flags, mode);
    }

    /* Set guard */
    in_open_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted open(\"%s\", %d, %o)\n",
                          pathname, flags, mode);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        open_request req;
        req.path = (char *)pathname;
        req.flags = flags;
        req.mode = mode;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        open_response *res = syscall_open_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] open() RPC result: fd=%d, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] open() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_open, pathname, flags, mode);
    }

    /* Clear guard */
    in_open_intercept = 0;

    return result;
}

/*
 * Intercepted open64() function (for large file support)
 */
int open64(const char *pathname, int flags, ...) {
    /* Handle optional mode argument */
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    /* Just call open() with O_LARGEFILE flag */
    return open(pathname, flags | O_LARGEFILE, mode);
}
