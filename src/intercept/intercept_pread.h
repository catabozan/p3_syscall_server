/*
 * pread() syscall interceptor
 */

#ifndef __INTERCEPT_PREAD_
#define __INTERCEPT_PREAD_

/* Thread-local reentry guard */
static __thread int in_pread_intercept = 0;

/*
 * Intercepted pread() function
 */
ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_pread_intercept || is_rpc_in_progress()) {
        return syscall(SYS_preadv, fd, buf, count, offset);
    }

    /* Set guard */
    in_pread_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted pread(%d, %p, %zu, %zu)\n",
                          fd, buf, count, offset);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    ssize_t result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        pread_request req;
        req.fd = fd;
        req.count = count;
        req.offset = offset;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        pread_response *res = syscall_pread_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            /* Copy data from response to user buffer */
            if (result > 0 && res->data.data_len > 0) {
                size_t bytes_to_copy = res->data.data_len;
                if (bytes_to_copy > count) {
                    bytes_to_copy = count;
                }
                memcpy(buf, res->data.data_val, bytes_to_copy);
            }

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] pread() RPC result: %zd bytes, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] pread() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_preadv, fd, buf, count, offset);
    }

    /* Clear guard */
    in_pread_intercept = 0;

    return result;
}

ssize_t pread64(int fd, void *buf, size_t count, off_t offset) {
    return pread(fd, buf, count, offset);
}

#endif
