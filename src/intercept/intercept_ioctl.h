/*
 * ioctl() syscall interceptor
 * Limited implementation for common ioctl requests
 */

#ifndef __INTERCEPT_IOCTL_
#define __INTERCEPT_IOCTL_

#include <sys/ioctl.h>
#include <termios.h>
#include <stdarg.h>

/* Thread-local reentry guard */
static __thread int in_ioctl_intercept = 0;

/*
 * Helper function to determine ioctl argument type
 */
static ioctl_arg_type get_ioctl_arg_type(unsigned long request) {
    switch (request) {
        case TIOCGWINSZ:  /* Get window size - returns winsize */
        case TIOCSWINSZ:  /* Set window size - takes winsize */
            return IOCTL_ARG_WINSIZE;

        case FIONREAD:    /* Get bytes available - returns int */
        case FIONBIO:     /* Set non-blocking - takes int */
            return IOCTL_ARG_INT;

        default:
            return IOCTL_ARG_NONE;
    }
}

/*
 * Intercepted ioctl() function
 */
int ioctl(int fd, unsigned long request, ...) {
    /* Check reentry guard - if already inside or RPC in progress, use direct syscall */
    if (in_ioctl_intercept || is_rpc_in_progress()) {
        va_list args;
        va_start(args, request);
        void *argp = va_arg(args, void *);
        va_end(args);
        return syscall(SYS_ioctl, fd, request, argp);
    }

    /* Set guard */
    in_ioctl_intercept = 1;

    /* Debug message using raw syscall */
    char debug_msg[256];
    int msg_len = snprintf(debug_msg, sizeof(debug_msg),
                          "[Client] Intercepted ioctl(%d, 0x%lx)\n",
                          fd, request);
    syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);

    /* Determine argument type */
    ioctl_arg_type arg_type = get_ioctl_arg_type(request);

    /* Extract variadic arguments */
    va_list args;
    va_start(args, request);
    void *argp = NULL;
    int *int_arg_ptr = NULL;
    struct winsize *winsize_arg_ptr = NULL;

    if (arg_type != IOCTL_ARG_NONE) {
        argp = va_arg(args, void *);
        if (arg_type == IOCTL_ARG_INT) {
            int_arg_ptr = (int *)argp;
        } else if (arg_type == IOCTL_ARG_WINSIZE) {
            winsize_arg_ptr = (struct winsize *)argp;
        }
    }
    va_end(args);

    /* Get RPC client */
    CLIENT *client = get_rpc_client();
    int result = -1;

    if (client != NULL) {
        /* Prepare RPC request */
        ioctl_request req;
        req.fd = fd;
        req.request = request;
        req.arg_in.type = arg_type;

        /* Populate argument based on type */
        switch (arg_type) {
            case IOCTL_ARG_INT:
                if (int_arg_ptr != NULL && (request == FIONBIO)) {
                    /* FIONBIO takes int input */
                    req.arg_in.ioctl_arg_u.int_arg = *int_arg_ptr;
                } else {
                    req.arg_in.ioctl_arg_u.int_arg = 0;
                }
                break;

            case IOCTL_ARG_WINSIZE:
                if (winsize_arg_ptr != NULL && request == TIOCSWINSZ) {
                    /* TIOCSWINSZ takes winsize input */
                    req.arg_in.ioctl_arg_u.winsize_arg.ws_row = winsize_arg_ptr->ws_row;
                    req.arg_in.ioctl_arg_u.winsize_arg.ws_col = winsize_arg_ptr->ws_col;
                    req.arg_in.ioctl_arg_u.winsize_arg.ws_xpixel = winsize_arg_ptr->ws_xpixel;
                    req.arg_in.ioctl_arg_u.winsize_arg.ws_ypixel = winsize_arg_ptr->ws_ypixel;
                } else {
                    /* Zero for TIOCGWINSZ */
                    req.arg_in.ioctl_arg_u.winsize_arg.ws_row = 0;
                    req.arg_in.ioctl_arg_u.winsize_arg.ws_col = 0;
                    req.arg_in.ioctl_arg_u.winsize_arg.ws_xpixel = 0;
                    req.arg_in.ioctl_arg_u.winsize_arg.ws_ypixel = 0;
                }
                break;

            default:
                break;
        }

        /* Disable interception during RPC call */
        rpc_in_progress = 1;

        /* Call RPC service */
        ioctl_response *res = syscall_ioctl_1(&req, client);

        /* Re-enable interception */
        rpc_in_progress = 0;

        if (res != NULL) {
            /* RPC call succeeded */
            result = res->result;
            errno = res->err;

            /* Copy output arguments back */
            if (result >= 0 && res->arg_out.type != IOCTL_ARG_NONE) {
                switch (res->arg_out.type) {
                    case IOCTL_ARG_INT:
                        if (int_arg_ptr != NULL) {
                            *int_arg_ptr = res->arg_out.ioctl_arg_u.int_arg;
                        }
                        break;

                    case IOCTL_ARG_WINSIZE:
                        if (winsize_arg_ptr != NULL) {
                            winsize_arg_ptr->ws_row = res->arg_out.ioctl_arg_u.winsize_arg.ws_row;
                            winsize_arg_ptr->ws_col = res->arg_out.ioctl_arg_u.winsize_arg.ws_col;
                            winsize_arg_ptr->ws_xpixel = res->arg_out.ioctl_arg_u.winsize_arg.ws_xpixel;
                            winsize_arg_ptr->ws_ypixel = res->arg_out.ioctl_arg_u.winsize_arg.ws_ypixel;
                        }
                        break;

                    default:
                        break;
                }
            }

            msg_len = snprintf(debug_msg, sizeof(debug_msg),
                              "[Client] ioctl() RPC result: %d, errno=%d\n",
                              result, errno);
            syscall(SYS_write, STDERR_FILENO, debug_msg, msg_len);
        } else {
            /* RPC call failed */
            clnt_perror(client, "[Client] ioctl() RPC failed");
            errno = EIO;
            result = -1;
        }
    } else {
        /* No RPC connection or unsupported ioctl - fall back to direct syscall */
        if (arg_type == IOCTL_ARG_NONE) {
            const char *msg = "[Client] Unsupported ioctl request, using direct syscall\n";
            syscall(SYS_write, STDERR_FILENO, msg, strlen(msg));
        } else {
            const char *fallback_msg = "[Client] No RPC connection, using direct syscall\n";
            syscall(SYS_write, STDERR_FILENO, fallback_msg, strlen(fallback_msg));
        }
        result = syscall(SYS_ioctl, fd, request, argp);
    }

    /* Clear guard */
    in_ioctl_intercept = 0;

    return result;
}

#endif
