/*
 * fcntl() syscall interceptor
 */

#ifndef __INTERCEPT_FCNTL_
#define __INTERCEPT_FCNTL_

#include <stdarg.h>
#include <fcntl.h>

/* Thread-local reentry guard */
static __thread int in_fcntl_intercept = 0;

/*
 * Helper function: Determine argument type based on fcntl command
 */
static fcntl_arg_type get_fcntl_arg_type(int cmd) {
    switch(cmd) {
        /* Commands with no argument */
        case F_GETFD:
        case F_GETFL:
        case F_GETOWN:
            return FCNTL_ARG_NONE;

        /* Commands with int argument */
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETFL:
        case F_SETOWN:
            return FCNTL_ARG_INT;

        /* Commands with struct flock* argument */
        case F_GETLK:
        case F_SETLK:
        case F_SETLKW:
            return FCNTL_ARG_FLOCK;

        /* Default to no argument for unknown commands */
        default:
            return FCNTL_ARG_NONE;
    }
}

/*
 * Intercepted fcntl() function
 */
int fcntl(int fd, int cmd, ...) {
    /* Determine argument type */
    fcntl_arg_type arg_type = get_fcntl_arg_type(cmd);

    /* Extract variadic argument based on type */
    va_list args;
    va_start(args, cmd);

    int int_arg = 0;
    struct flock *flock_ptr = NULL;
    struct flock flock_copy;

    switch(arg_type) {
        case FCNTL_ARG_INT:
            int_arg = va_arg(args, int);
            break;
        case FCNTL_ARG_FLOCK:
            flock_ptr = va_arg(args, struct flock *);
            if (flock_ptr != NULL) {
                /* Copy flock structure for safety */
                flock_copy = *flock_ptr;
            }
            break;
        case FCNTL_ARG_NONE:
            /* No argument to extract */
            break;
    }

    va_end(args);

    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_fcntl_intercept || is_rpc_in_progress()) {
        /* Use direct syscall based on argument type */
        switch(arg_type) {
            case FCNTL_ARG_NONE:
                return syscall(SYS_fcntl, fd, cmd);
            case FCNTL_ARG_INT:
                return syscall(SYS_fcntl, fd, cmd, int_arg);
            case FCNTL_ARG_FLOCK:
                return syscall(SYS_fcntl, fd, cmd, flock_ptr);
            default:
                return syscall(SYS_fcntl, fd, cmd);
        }
    }

    /* Set guard */
    in_fcntl_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted fcntl(%d, %d)\n", fd, cmd);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Log warning for F_SETLKW */
    if (cmd == F_SETLKW) {
        const char *warning = "[Client] Warning: F_SETLKW may block and cause RPC timeout\n";
        syscall(SYS_write, STDERR_FILENO, warning, strlen(warning));
    }

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        fcntl_request req;
        req.fd = fd;
        req.cmd = cmd;
        req.arg.type = arg_type;

        /* Populate union based on argument type */
        switch(arg_type) {
            case FCNTL_ARG_NONE:
                /* Nothing to populate */
                break;
            case FCNTL_ARG_INT:
                req.arg.fcntl_arg_u.int_arg = int_arg;
                break;
            case FCNTL_ARG_FLOCK:
                if (flock_ptr != NULL) {
                    /* Convert struct flock to flock_data */
                    req.arg.fcntl_arg_u.flock_arg.l_type = flock_copy.l_type;
                    req.arg.fcntl_arg_u.flock_arg.l_whence = flock_copy.l_whence;
                    req.arg.fcntl_arg_u.flock_arg.l_start = flock_copy.l_start;
                    req.arg.fcntl_arg_u.flock_arg.l_len = flock_copy.l_len;
                    req.arg.fcntl_arg_u.flock_arg.l_pid = flock_copy.l_pid;
                }
                break;
        }

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        fcntl_response *res = syscall_fcntl_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            /* Handle output arguments for F_GETLK */
            if (cmd == F_GETLK && flock_ptr != NULL && result >= 0) {
                if (res->arg_out.type == FCNTL_ARG_FLOCK) {
                    /* Copy modified flock back to user's struct */
                    flock_ptr->l_type = res->arg_out.fcntl_arg_u.flock_arg.l_type;
                    flock_ptr->l_whence = res->arg_out.fcntl_arg_u.flock_arg.l_whence;
                    flock_ptr->l_start = res->arg_out.fcntl_arg_u.flock_arg.l_start;
                    flock_ptr->l_len = res->arg_out.fcntl_arg_u.flock_arg.l_len;
                    flock_ptr->l_pid = res->arg_out.fcntl_arg_u.flock_arg.l_pid;
                }
            }

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] fcntl() RPC result: %d, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] fcntl() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection - fall back to direct syscall */
        const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
        syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));

        switch(arg_type) {
            case FCNTL_ARG_NONE:
                result = syscall(SYS_fcntl, fd, cmd);
                break;
            case FCNTL_ARG_INT:
                result = syscall(SYS_fcntl, fd, cmd, int_arg);
                break;
            case FCNTL_ARG_FLOCK:
                result = syscall(SYS_fcntl, fd, cmd, flock_ptr);
                break;
        }
    }

    /* Clear guard */
    in_fcntl_intercept = 0;

    return result;
}

#endif
