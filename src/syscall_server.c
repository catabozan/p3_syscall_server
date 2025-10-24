/*
 * step1_server.c
 *
 * Minimal UNIX-domain socket server.
 *
 * - Creates a socket file at SOCKET_PATH
 * - Binds and listens for a single connection
 * - Accepts a connection, receives bytes, prints them
 *
 * Comments explain each section for readers comfortable with C but new to Linux sockets.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>         // close(), unlink()
#include <sys/socket.h>     // socket(), bind(), listen(), accept()
#include <sys/un.h>         // struct sockaddr_un
#include "protocol/protocol_main_header.h"

#define SOCKET_PATH "/tmp/p3_tb"
#define BUF_SIZE sizeof(ClientMsg)

int main(void) {
    int listen_fd = -1, client_fd = -1;
    struct sockaddr_un addr;
    ClientMsg *buf;
    buf = malloc(BUF_SIZE);

    /* Remove old socket file if it exists (cleanup from previous runs) */
    unlink(SOCKET_PATH);

    /* Create a UNIX-domain stream socket (like TCP but local to this machine). */
    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(1);
    }

    /* Clear and fill the address struct: family + path on filesystem */
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    /*
     * Bind the socket to the path. This creates a special file in the filesystem
     * that other processes can connect to.
     */
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(1);
    }

    /* Make the socket a passive listening socket (queue size 1). */
    if (listen(listen_fd, 1) < 0) {
        perror("listen");
        close(listen_fd);
        unlink(SOCKET_PATH);
        exit(1);
    }

    printf("Server: listening on %s\n", SOCKET_PATH);

    /* Wait for a single client to connect. accept() blocks until a connection arrives. */
    client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(listen_fd);
        unlink(SOCKET_PATH);
        exit(1);
    }

    /* Read data sent by the client and print it. recv returns number of bytes read. */
    ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
    if (n < 0) {
        perror("recv");
    } else {
        printf("Server: received: \"%s\"\n", buf->payload);
    }

    /* Close sockets and unlink the socket file */
    close(client_fd);
    close(listen_fd);
    unlink(SOCKET_PATH);

    free(buf);
    return 0;
}
