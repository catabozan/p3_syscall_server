/*
 * write() syscall interceptor
 */

#ifndef __INTERCEPT_WRITE_
#define __INTERCEPT_WRITE_

/* Thread-local reentry guard */
static __thread int in_write_intercept = 0;

/*
 * Intercepted write() function
 */
ssize_t write(int fd, const void *buf, size_t count) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_write_intercept || is_rpc_in_progress()) {
        return syscall(SYS_write, fd, buf, count);
    }

    /* Set guard */
    in_write_intercept = 1;

    /* Debug message using raw syscall (careful not to cause recursion) */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted write(%d, %p, %zu)\n",
                          fd, buf, count);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    ssize_t result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        write_request req;
        req.fd = fd;
        req.data.data_val = (char *)buf;
        req.data.data_len = count;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        write_response *res = syscall_write_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] write() RPC result: %zd bytes, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] write() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_write, fd, buf, count);
    }

    /* Clear guard */
    in_write_intercept = 0;

    return result;
}

#endif
