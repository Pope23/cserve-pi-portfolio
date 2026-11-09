#ifndef CSERVE_SERVER_H
#define CSERVE_SERVER_H

#include <stdint.h>

#define SERVER_DEFAULT_HOST "127.0.0.1"
#define SERVER_LAN_HOST "0.0.0.0"
enum { SERVER_DEFAULT_PORT = 8080, SERVER_EXIT_FAILURE = 1, SERVER_EXIT_USAGE = 2 };

struct ServerConfig {
    const char *host;
    uint16_t port;
};

/* Config and its IPv4 host string are validated by the caller and borrowed
 * for this blocking call. Owns listener, clients and an in-memory task context.
 * Handles requests sequentially until an unrecoverable accept error; returns
 * SERVER_EXIT_FAILURE on startup/accept failure. Keeps the existing SIGPIPE
 * ignore policy and does not introduce a graceful-shutdown interface. */
int server_run(const struct ServerConfig *config);

#endif
