#ifndef __SERIALIZATION__ 
#define __SERIALIZATION__
#include "protocol_main_header.h"
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>

int serialize_client_msg(ClientMsg *msg) {
    char buff[sizeof(ClientMsg)];

    size_t offset = 0;
    buff[offset++] = msg->version;
    buff[offset++] = msg->client_id;

    unsigned int net_payload_size = htonl(msg->payload_size);
    memcpy(buff + offset, &net_payload_size, sizeof(net_payload_size));

    // TODO: serialize more
}

#endif