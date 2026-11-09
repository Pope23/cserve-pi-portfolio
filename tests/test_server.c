/* Fault injection at POSIX boundaries; the real server and storage run here. */
#include "server.h"
#include "http_parser.h"
#include "http_response.h"
#include "router.h"
#include "task_storage.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>

enum { LISTENER_FD = 7, CLIENT_FD = 8, EXPECTED_CLIENTS = 2 };
enum FailureMode { SOCKET_FAILURE, BIND_FAILURE, LISTEN_FAILURE,
                   ACCEPT_INTERRUPTED, PARSER_FAILURE, OPTION_FAILURE };
static enum FailureMode mode;
static bool listener_open, client_open;
static unsigned listener_closes, client_closes, accepted, accept_calls, routed, errors;
static unsigned option_calls;

int __wrap_socket(int domain, int type, int protocol);
int __wrap_setsockopt(int fd, int level, int option, const void *value, socklen_t length);
int __wrap_bind(int fd, const struct sockaddr *addr, socklen_t length);
int __wrap_listen(int fd, int backlog);
int __wrap_accept(int fd, struct sockaddr *addr, socklen_t *length);
int __wrap_close(int fd);

int __wrap_socket(int domain, int type, int protocol) {
    assert(domain == AF_INET && type == SOCK_STREAM && protocol == 0);
    if (mode == SOCKET_FAILURE) { errno = EMFILE; return -1; }
    listener_open = true;
    return LISTENER_FD;
}

int __wrap_setsockopt(int fd, int level, int option, const void *value, socklen_t length) {
    assert(level == SOL_SOCKET);
    option_calls++;
    if (fd == LISTENER_FD) {
        assert(option == SO_REUSEADDR && length == sizeof(int));
        assert(*(const int *)value == 1);
    } else {
        assert(fd == CLIENT_FD && client_open);
        assert(option == SO_RCVTIMEO || option == SO_SNDTIMEO);
        assert(length == sizeof(struct timeval));
        const struct timeval *timeout = value;
        assert(timeout->tv_sec == 5 && timeout->tv_usec == 0);
    }
    if (mode == OPTION_FAILURE) { errno = ENOPROTOOPT; return -1; }
    return 0;
}

int __wrap_bind(int fd, const struct sockaddr *addr, socklen_t length) {
    assert(fd == LISTENER_FD && listener_open);
    assert(length == sizeof(struct sockaddr_in));
    const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)addr;
    assert(ipv4->sin_family == AF_INET && ntohs(ipv4->sin_port) == 8080);
    assert(ntohl(ipv4->sin_addr.s_addr) == INADDR_LOOPBACK);
    if (mode == BIND_FAILURE) { errno = EADDRINUSE; return -1; }
    return 0;
}

int __wrap_listen(int fd, int backlog) {
    assert(fd == LISTENER_FD && listener_open && backlog == 16);
    if (mode == LISTEN_FAILURE) { errno = EIO; return -1; }
    return 0;
}

int __wrap_accept(int fd, struct sockaddr *addr, socklen_t *length) {
    assert(fd == LISTENER_FD && listener_open && !client_open);
    assert(addr == NULL && length == NULL);
    struct sigaction action;
    assert(sigaction(SIGPIPE, NULL, &action) == 0 && action.sa_handler == SIG_IGN);
    accept_calls++;
    if (mode == ACCEPT_INTERRUPTED && accept_calls == 1) { errno = EINTR; return -1; }
    if (accepted == EXPECTED_CLIENTS) { errno = EIO; return -1; }
    accepted++;
    client_open = true;
    return CLIENT_FD;
}

int __wrap_close(int fd) {
    if (fd == LISTENER_FD) {
        assert(listener_open && !client_open);
        listener_open = false;
        listener_closes++;
    } else {
        assert(fd == CLIENT_FD && client_open);
        client_open = false;
        client_closes++;
    }
    return 0;
}

int http_read_request(int fd, struct Request *request) {
    assert(fd == CLIENT_FD && client_open);
    assert(request->content_length == 0 && request->body[0] == '\0');
    return mode == PARSER_FAILURE ? HTTP_BAD_REQUEST : 0;
}

void http_error_reply(int fd, int status) {
    assert(fd == CLIENT_FD && client_open && status == HTTP_BAD_REQUEST);
    errors++;
}

void router_handle(int fd, const struct Request *request, struct TaskStorage *storage) {
    assert(fd == CLIENT_FD && client_open && request != NULL);
    if (routed == 0) {
        const struct Task *created = NULL;
        assert(task_storage_at(storage, 0) == NULL);
        assert(task_storage_create(storage, "context survives clients", &created) == TASK_CREATE_OK);
        assert(created->id == 1);
    } else {
        assert(task_storage_at(storage, 0)->id == 1);
    }
    routed++;
}

static void test_run(enum FailureMode failure, unsigned expected_clients) {
    mode = failure;
    listener_open = client_open = false;
    listener_closes = client_closes = accepted = accept_calls = routed = errors = option_calls = 0;
    const struct ServerConfig config = {.host = "127.0.0.1", .port = 8080};
    assert(server_run(&config) == 1);
    assert(!listener_open && !client_open);
    assert(listener_closes == (failure == SOCKET_FAILURE ? 0U : 1U));
    assert(client_closes == expected_clients && accepted == expected_clients);
    assert(routed == (failure == PARSER_FAILURE ? 0U : expected_clients));
    assert(errors == (failure == PARSER_FAILURE ? expected_clients : 0U));
    if (expected_clients > 0) assert(option_calls == 1U + expected_clients * 2U);
}

int main(void) {
    test_run(SOCKET_FAILURE, 0);
    test_run(BIND_FAILURE, 0);
    test_run(LISTEN_FAILURE, 0);
    test_run(ACCEPT_INTERRUPTED, EXPECTED_CLIENTS);
    test_run(PARSER_FAILURE, EXPECTED_CLIENTS);
    test_run(OPTION_FAILURE, EXPECTED_CLIENTS);
    puts("Server lifecycle: 6 cases OK (injected errors are expected)");
    return 0;
}
