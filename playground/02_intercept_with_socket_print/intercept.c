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
#include <sys/socket.h>     // socket(), connect(), send()
#include <sys/un.h>         // struct sockaddr_un

#define SOCKET_PATH "/tmp/example_socket_intercept"

int connect_to_sock_and_send_msg(const char *msg) {
    int sock = -1;
    struct sockaddr_un addr;

    /* Create a socket */
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(1);
    }

    /* Prepare server address */
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    /* Connect to server; connect() will fail if server isn't listening */
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        exit(1);
    }

    /* Send the message (send may not send all bytes in other contexts; here it's small). */
    if (send(sock, msg, strlen(msg), 0) < 0) {
        perror("send");
    } else {
        printf("Client: sent \"%s\"\n", msg);
    }

    close(sock);
    return 0;
}

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
    connect_to_sock_and_send_msg(msg);

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
