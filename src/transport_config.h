/*
 * Transport Configuration for RPC Communication
 *
 * Configure the transport type (UNIX or TCP) using environment variable:
 *   RPC_TRANSPORT=unix  (default) - Use UNIX domain sockets
 *   RPC_TRANSPORT=tcp              - Use TCP sockets
 */

#ifndef _TRANSPORT_CONFIG_H
#define _TRANSPORT_CONFIG_H

#include <stdlib.h>
#include <string.h>

/* Transport types */
typedef enum {
    TRANSPORT_UNIX,
    TRANSPORT_TCP
} transport_type_t;

/* Configuration */
#define UNIX_SOCKET_PATH "/tmp/p3_tb"
#define TCP_HOST "localhost"
#define TCP_PORT 9999

/*
 * Get configured transport type from environment
 * Returns: TRANSPORT_UNIX (default) or TRANSPORT_TCP
 */
static inline transport_type_t get_transport_type(void) {
    const char *env = getenv("RPC_TRANSPORT");

    if (env == NULL) {
        return TRANSPORT_UNIX;  /* Default to UNIX */
    }

    if (strcasecmp(env, "tcp") == 0) {
        return TRANSPORT_TCP;
    }

    return TRANSPORT_UNIX;
}

/*
 * Get transport type name for logging
 */
static inline const char* get_transport_name(transport_type_t type) {
    return (type == TRANSPORT_TCP) ? "TCP" : "UNIX";
}

#endif /* _TRANSPORT_CONFIG_H */
