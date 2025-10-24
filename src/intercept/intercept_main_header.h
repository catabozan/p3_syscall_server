#ifndef __INTERCEPT_MAIN_HEADER_
#define __INTERCEPT_MAIN_HEADER_
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
#include "../protocol/protocol_main_header.h"


int connect_to_sock_and_send_msg(ClientMsg *msg);

#endif