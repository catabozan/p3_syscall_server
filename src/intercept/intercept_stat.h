/*
 * stat() syscall interceptor
 */

#ifndef __INTERCEPT_STAT_
#define __INTERCEPT_STAT_

#include <sys/stat.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>

/* Thread-local reentry guard */
static __thread int in_stat_intercept = 0;

/*
 * Intercepted stat() function
 */
int stat(const char *pathname, struct stat *statbuf) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_stat_intercept || is_rpc_in_progress()) {
        return syscall(SYS_stat, pathname, statbuf);
    }

    /* Set guard */
    in_stat_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted stat(\"%s\", buf)\n",
                          pathname);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        stat_request req;
        req.path = (char *)pathname;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        stat_response *res = syscall_stat_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            if (result >= 0) {
                /* Success: populate statbuf with response data */
                memset(statbuf, 0, sizeof(struct stat));
                statbuf->st_dev = res->dev;
                statbuf->st_ino = res->ino;
                statbuf->st_mode = res->mode;
                statbuf->st_nlink = res->nlink;
                statbuf->st_uid = res->uid;
                statbuf->st_gid = res->gid;
                statbuf->st_rdev = res->rdev;
                statbuf->st_size = res->size;
                statbuf->st_blksize = res->blksize;
                statbuf->st_blocks = res->blocks;
                statbuf->st_atime = res->atime;
                statbuf->st_mtime = res->mtime;
                statbuf->st_ctime = res->ctime;

                msg_len = snprintf(debug_msg, sizeof(debug_msg),
                                  "[Client] stat() RPC result: %d, errno=%d\n",
                                  result, errno);
                syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
            } else {
                /* RPC returned error */
                msg_len = snprintf(debug_msg, sizeof(debug_msg),
                                  "[Client] stat() RPC returned error: %d, errno=%d\n",
                                  result, errno);
                syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
            }
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] stat() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_stat, pathname, statbuf);
    }

    /* Clear guard */
    in_stat_intercept = 0;

    return result;
}

#endif
