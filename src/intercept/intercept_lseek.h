/*
 * lseek() syscall interceptor
 */

#ifndef __INTERCEPT_LSEEK_
#define __INTERCEPT_LSEEK_

/* Thread-local reentry guard */
static __thread int in_lseek_intercept = 0;

/*
 * Intercepted lseek() function
 */
off_t lseek(int fd, off_t offset, int whence) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_lseek_intercept || is_rpc_in_progress()) {
        return syscall(SYS_lseek, fd, offset, whence);
    }

    /* Set guard */
    in_lseek_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted lseek(%d, %ld, %d)\n",
                          fd, offset, whence);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    off_t result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        lseek_request req;
        req.fd = fd;
        req.offset = offset;
        req.whence = whence;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        lseek_response *res = syscall_lseek_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] lseek() RPC result: %ld, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] lseek() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_lseek, fd, offset, whence);
    }

    /* Clear guard */
    in_lseek_intercept = 0;

    return result;
}

/*
 * lseek64 is just an alias for lseek on x86_64
 */
off_t lseek64(int fd, off_t offset, int whence) {
    return lseek(fd, offset, whence);
}

#endif
