/* CServe Pi: CLI configuration for the sequential Linux/POSIX server. */
#include "server.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PORT_MIN = 1, PORT_MAX = 65535, PORT_DECIMAL_BASE = 10 };
static const char HOST_OPTION[] = "--host";
static const char PORT_OPTION[] = "--port";
static const char USAGE_FORMAT[] = "Usage: %s [--host %s|%s] [--port %d..%d]\n";
static const char HOST_ERROR[] = "Only loopback or all interfaces are supported.\n";

static bool valid_port(const char *text, uint16_t *port) {
    errno = 0;
    char *end = NULL;
    long n = strtol(text, &end, PORT_DECIMAL_BASE);
    if (errno || *end || n < PORT_MIN || n > PORT_MAX) return false;
    *port = (uint16_t)n;
    return true;
}

static bool parse_arguments(int argc, char **argv, struct ServerConfig *config) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], HOST_OPTION) == 0 && i + 1 < argc) config->host = argv[++i];
        else if (strcmp(argv[i], PORT_OPTION) == 0 && i + 1 < argc
                 && valid_port(argv[++i], &config->port)) {}
        else return false;
    }
    return true;
}

int main(int argc, char **argv) {
    struct ServerConfig config = {.host = SERVER_DEFAULT_HOST, .port = SERVER_DEFAULT_PORT};
    if (!parse_arguments(argc, argv, &config)) {
        fprintf(stderr, USAGE_FORMAT, argv[0], SERVER_DEFAULT_HOST, SERVER_LAN_HOST, PORT_MIN, PORT_MAX);
        return SERVER_EXIT_USAGE;
    }
    if (strcmp(config.host, SERVER_DEFAULT_HOST) != 0 && strcmp(config.host, SERVER_LAN_HOST) != 0) {
        fprintf(stderr, "%s", HOST_ERROR);
        return SERVER_EXIT_USAGE;
    }
    return server_run(&config);
}
