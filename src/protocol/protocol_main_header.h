#ifndef __PROTOCOL_MAIN_HEADER__
#define __PROTOCOL_MAIN_HEADER__

#define CURRENT_VERSION 0
// message types
#define MESSAGE_TYPE_READ 0

struct client_msg {
    char version;
    char client_id;
    unsigned int payload_size;
    char payload[1024];
};
typedef struct client_msg ClientMsg;

struct server_msg {
    char version;
    char client_id;
    unsigned int payload_size;
    char payload[1024];
};
typedef struct server_msg ServerMsg;

#endif