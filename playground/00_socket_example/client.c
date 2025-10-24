/*
 * step1_client.c
 *
 * Minimal UNIX-domain socket client.
 *
 * - Creates a socket
 * - Connects to the server socket file at SOCKET_PATH
 * - Sends the string "hello from client"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>         // close()
#include <sys/socket.h>     // socket(), connect(), send()
#include <sys/un.h>         // struct sockaddr_un

#define SOCKET_PATH "/tmp/example_socket_step1"

int main(void) {
    int sock = -1;
    struct sockaddr_un addr;
    const char *msg = "hello from client";

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
