/*
 * stat() syscall interceptor
 */

#ifndef __INTERCEPT_NEWFSTATAT_
#define __INTERCEPT_NEWFSTATAT_

#include <sys/stat.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>

/* Thread-local reentry guard */
static __thread int in_newfstatat_intercept = 0;

/*
 * Intercepted newfstatat() function
 */
int newfstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_newfstatat_intercept || is_rpc_in_progress()) {
        return syscall(SYS_newfstatat, pathname, statbuf);
    }

    /* Set guard */
    in_newfstatat_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted newfstatat(%u, \"%s\", buf, %u)\n", dirfd,
                          pathname, flags);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        newfstatat_request req;
        req.dirfd = dirfd;
        req.path = (char *)pathname;
        req.flags = flags;

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        newfstatat_response *res = syscall_newfstatat_1(&req, client);

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
                                  "[Client] newfstatat() RPC result: %d, errno=%d\n",
                                  result, errno);
                syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
            } else {
                /* RPC returned error */
                msg_len = snprintf(debug_msg, sizeof(debug_msg),
                                  "[Client] newfstatat() RPC returned error: %d, errno=%d\n",
                                  result, errno);
                syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
            }
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] newfstatat() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        result = syscall(SYS_newfstatat, pathname, statbuf);
    }

    /* Clear guard */
    in_newfstatat_intercept = 0;

    return result;
}

int fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    return newfstatat(dirfd, pathname, statbuf, flags);
}

#endif
