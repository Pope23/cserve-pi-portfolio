/* CServe Pi: intentionally small HTTP/1.1 task API for Linux / Raspberry Pi OS.
 * Educational server: sequential connections, in-memory data, no authentication.
 * Do not expose directly to the public Internet. */
#define _POSIX_C_SOURCE 200809L
#include "http_parser.h"
#include "http_response.h"
#include "task_storage.h"
#include "router.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8080

static void handle_client(int fd, struct TaskStorage *storage) {
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct Request request = {0};
    int status = http_read_request(fd, &request);
    if (status != 0) http_error_reply(fd, status);
    else router_handle(fd, &request, storage);
}

static bool valid_port(const char *text, uint16_t *port) {
    errno = 0;
    char *end = NULL;
    long n = strtol(text, &end, 10);
    if (errno || *end || n < 1 || n > 65535) return false;
    *port = (uint16_t)n;
    return true;
}

int main(int argc, char **argv) {
    const char *host = DEFAULT_HOST;
    uint16_t port = DEFAULT_PORT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) host = argv[++i];
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc && valid_port(argv[++i], &port)) {}
        else {
            fprintf(stderr, "Usage: %s [--host 127.0.0.1|0.0.0.0] [--port 1..65535]\n", argv[0]);
            return 2;
        }
    }
    if (strcmp(host, "127.0.0.1") != 0 && strcmp(host, "0.0.0.0") != 0) {
        fprintf(stderr, "Only loopback or all interfaces are supported.\n");
        return 2;
    }
    struct TaskStorage storage;
    task_storage_init(&storage);
    (void)signal(SIGPIPE, SIG_IGN);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }
    int reuse = 1;
    (void)setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1 ||
        bind(server, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(server, 16) != 0) {
        perror("bind/listen");
        close(server);
        return 1;
    }
    fprintf(stderr, "CServe Pi listening on http://%s:%u\n", host, (unsigned)port);
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        handle_client(client, &storage);
        close(client);
    }
    close(server);
    return 1;
}
