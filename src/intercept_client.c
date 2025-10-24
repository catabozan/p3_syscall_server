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
#include "protocol/protocol_main_header.h"

#define SOCKET_PATH "/tmp/p3_tb"

int connect_to_sock_and_send_msg(ClientMsg *msg) {
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
    if (send(sock, msg, sizeof(msg), 0) < 0) {
        perror("send");
    } else {
        printf("Client: sent \"%s\"\n", msg);
    }

    close(sock);
    return 0;
}

#include "./intercept/intercept_read.h"