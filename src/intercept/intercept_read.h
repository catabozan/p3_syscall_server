#ifndef __INTERCEPT_READ_
#define __INTERCEPT_READ_

#include "intercept_main_header.h"

static __thread int in_read_intercept = 0; /* thread-local reentry guard */

/* Exported symbol that intercepts calls to 'read' from the program */
ssize_t read(int fd, void *buf, size_t count) {
    /* If we're already inside the intercept (reentry), call kernel read directly */
    if (in_read_intercept) {
        return syscall(SYS_read, fd, buf, count);
    }

    in_read_intercept = 1; /* enter guarded region */

    /* Prepare a small log message. Use snprintf (safe) then write to stderr. */
    char payload[256];
    int mlen = snprintf(payload, sizeof(payload), "[intercept] read called: fd=%d count=%zu\n", fd, count);

    ClientMsg msg;
    msg.client_id = 0;
    msg.version = CURRENT_VERSION;
    msg.payload_size = mlen;
    strncpy(msg.payload, payload, mlen);

    connect_to_sock_and_send_msg(&msg);

    /* Use write() system call to avoid stdio buffering issues (write is not intercepted). */
    if (mlen > 0) {
        /* write to file descriptor 2 (stderr) */
        ssize_t w = write(STDERR_FILENO, payload, (size_t)mlen);
        (void)w; /* ignore write errors for this debug logging */
    }

    /* Perform the actual read using syscall to avoid invoking libc wrappers that could call read again. */
    ssize_t ret = syscall(SYS_read, fd, buf, count);

    in_read_intercept = 0; /* leave guarded region */

    /* Return whatever the kernel returned (so program sees expected behavior) */
    return ret;
}

#endif
