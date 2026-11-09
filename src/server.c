#include "server.h"
#include "http_parser.h"
#include "http_response.h"
#include "router.h"
#include "task_storage.h"
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

enum { LISTEN_BACKLOG = 16, CLIENT_TIMEOUT_SECONDS = 5 };
static const char SOCKET_ERROR[] = "socket";
static const char BIND_LISTEN_ERROR[] = "bind/listen";
static const char ACCEPT_ERROR[] = "accept";
static const char LISTENING_FORMAT[] = "CServe Pi listening on http://%s:%u\n";

static void configure_client_timeouts(int fd) {
    struct timeval timeout = {.tv_sec = CLIENT_TIMEOUT_SECONDS, .tv_usec = 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

static void handle_client(int fd, struct TaskStorage *storage) {
    configure_client_timeouts(fd);
    struct Request request = {0};
    int status = http_read_request(fd, &request);
    if (status != 0) http_error_reply(fd, status);
    else router_handle(fd, &request, storage);
}

static bool bind_listener(int fd, const struct ServerConfig *config) {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config->port);
    return inet_pton(AF_INET, config->host, &addr.sin_addr) == 1
        && bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0
        && listen(fd, LISTEN_BACKLOG) == 0;
}

static int open_listener(const struct ServerConfig *config) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror(SOCKET_ERROR); return -1; }
    int reuse = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (!bind_listener(fd, config)) {
        perror(BIND_LISTEN_ERROR);
        close(fd);
        return -1;
    }
    return fd;
}

static void accept_clients(int server, struct TaskStorage *storage) {
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror(ACCEPT_ERROR);
            return;
        }
        handle_client(client, storage);
        close(client);
    }
}

int server_run(const struct ServerConfig *config) {
    struct TaskStorage storage;
    task_storage_init(&storage);
    (void)signal(SIGPIPE, SIG_IGN);
    int server = open_listener(config);
    if (server < 0) return SERVER_EXIT_FAILURE;
    fprintf(stderr, LISTENING_FORMAT, config->host, (unsigned)config->port);
    accept_clients(server, &storage);
    close(server);
    return SERVER_EXIT_FAILURE;
}
