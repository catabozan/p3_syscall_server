/*
 * intercept_print_read.c
 *
 * Build as a shared library and load with LD_PRELOAD:
 *   gcc -shared -fPIC -o libintercept_print_read.so intercept_print_read.c
 *   LD_PRELOAD=./libintercept_print_read.so ./your_program
 *
 * This intercepts read(fd, buf, count), prints a short message to stderr,
 * then performs the actual read via syscall(SYS_read,...).
 *
 * Important safety: we use a thread-local re-entry guard so if any function
 * we call internally triggers read again, we won't recurse forever.
 */

//  gcc -shared -fPIC -o intercept.so intercept.c
// LD_PRELOAD=./intercept.so

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>   // syscall numbers (SYS_read)
#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

static __thread int in_read_intercept = 0; /* thread-local reentry guard */

/* Exported symbol that intercepts calls to 'read' from the program */
ssize_t read(int fd, void *buf, size_t count) {
    /* If we're already inside the intercept (reentry), call kernel read directly */
    if (in_read_intercept) {
        return syscall(SYS_read, fd, buf, count);
    }

    in_read_intercept = 1; /* enter guarded region */

    /* Prepare a small log message. Use snprintf (safe) then write to stderr. */
    char msg[256];
    int mlen = snprintf(msg, sizeof(msg), "[intercept] read called: fd=%d count=%zu\n", fd, count);

    /* Use write() system call to avoid stdio buffering issues (write is not intercepted). */
    if (mlen > 0) {
        /* write to file descriptor 2 (stderr) */
        ssize_t w = write(STDERR_FILENO, msg, (size_t)mlen);
        (void)w; /* ignore write errors for this debug logging */
    }

    /* Perform the actual read using syscall to avoid invoking libc wrappers that could call read again. */
    ssize_t ret = syscall(SYS_read, fd, buf, count);

    in_read_intercept = 0; /* leave guarded region */

    /* Return whatever the kernel returned (so program sees expected behavior) */
    return ret;
}
