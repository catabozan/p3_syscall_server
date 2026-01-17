/*
 * prlimit() syscall interceptor
 * Note: Uses exact system signature to properly override via LD_PRELOAD
 */

#ifndef __INTERCEPT_PRLIMIT64_
#define __INTERCEPT_PRLIMIT64_

#define _GNU_SOURCE
#include <sys/resource.h>

/* Thread-local reentry guard */
static __thread int in_prlimit_intercept = 0;

/*
 * Intercepted prlimit() function with exact system signature
 */
int prlimit(__pid_t pid, enum __rlimit_resource resource, const struct rlimit *new_limit, struct rlimit *old_limit) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_prlimit_intercept || is_rpc_in_progress()) {
        return syscall(SYS_prlimit64, pid, resource, new_limit, old_limit);
    }

    /* Set guard */
    in_prlimit_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted prlimit64(%d, %d, %p, %p)\n",
                          pid, resource, new_limit, old_limit);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        prlimit64_request req;
        req.pid = pid;
        req.resource = resource;

        /* Check if new_limit is provided */
        if (new_limit != NULL) {
            req.has_new_limit = TRUE;
            req.new_limit.rlim_cur = new_limit->rlim_cur;
            req.new_limit.rlim_max = new_limit->rlim_max;
        } else {
            req.has_new_limit = FALSE;
            req.new_limit.rlim_cur = 0;
            req.new_limit.rlim_max = 0;
        }

        /* Check if old_limit is requested */
        req.request_old_limit = (old_limit != NULL) ? TRUE : FALSE;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        prlimit64_response *res = syscall_prlimit64_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            /* Copy old_limit if requested and available */
            if (old_limit != NULL && res->has_old_limit) {
                old_limit->rlim_cur = res->old_limit.rlim_cur;
                old_limit->rlim_max = res->old_limit.rlim_max;
            }

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] prlimit64() RPC result: %d, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] prlimit64() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_prlimit64, pid, resource, new_limit, old_limit);
    }

    /* Clear guard */
    in_prlimit_intercept = 0;

    return result;
}

#endif
