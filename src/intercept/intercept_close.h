/*
 * close() syscall interceptor
 */

/* Thread-local reentry guard */
static __thread int in_close_intercept = 0;

/*
 * Intercepted close() function
 */
int close(int fd) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_close_intercept || is_rpc_in_progress()) {
        return syscall(SYS_close, fd);
    }

    /* Set guard */
    in_close_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted close(%d)\n", fd);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        close_request req;
        req.fd = fd;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        close_response *res = syscall_close_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] close() RPC result: %d, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] close() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_close, fd);
    }

    /* Clear guard */
    in_close_intercept = 0;

    return result;
}
