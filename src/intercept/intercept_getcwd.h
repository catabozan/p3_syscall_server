/*
 * getcwd() syscall interceptor
 */

#ifndef __INTERCEPT_GETCWD_
#define __INTERCEPT_GETCWD_

#include <unistd.h>
#include <stdlib.h>

/* Thread-local reentry guard */
static __thread int in_getcwd_intercept = 0;

/*
 * Intercepted getcwd() function
 */
char *getcwd(char *buf, size_t size) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_getcwd_intercept || is_rpc_in_progress()) {
        return (char *)syscall(SYS_getcwd, buf, size);
    }

    /* Set guard */
    in_getcwd_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted getcwd(%p, %zu)\n",
                          buf, size);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Handle NULL buf case (GNU extension) - allocate buffer */
    int allocated = 0;
    if (buf == NULL) {
        size = (size == 0) ? PATH_MAX : size;
        buf = malloc(size);
        if (buf == NULL) {
            in_getcwd_intercept = 0;
            errno = ENOMEM;
            return NULL;
        }
        allocated = 1;
    }

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    char *result = NULL;

    if (client != NULL) {
        /* Prepare RPC request */
        getcwd_request req;
        req.size = size;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        getcwd_response *res = syscall_getcwd_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL && res->result == 0) {
            /* RPC call succeeded */
            errno = res->err;

            /* Copy path to buffer */
            size_t path_len = strlen(res->path);
            if (path_len < size) {
                strcpy(buf, res->path);
                result = buf;

                msg_len = snprintf(debug_msg, sizeof(debug_msg),
                                  "[Client] getcwd() RPC result: \"%s\", errno=%d\n",
                                  buf, errno);
                syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
            } else {
                /* Buffer too small */
                errno = ERANGE;
                if (allocated) {
                    free(buf);
                }
            }
        } else {
            /* RPC call failed */
            if (res != NULL) {
                errno = res->err;
            } else {
                clnt_perror(client, "[Client] getcwd() RPC failed");
                errno = EIO;
            }
            if (allocated) {
                free(buf);
            }
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));

        char *syscall_result = (char *)syscall(SYS_getcwd, buf, size);
        if (syscall_result == NULL && allocated) {
            free(buf);
        } else {
            result = syscall_result;
        }
    }

    /* Clear guard */
    in_getcwd_intercept = 0;

    return result;
}

#endif
