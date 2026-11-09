#include "http_response.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

enum { RESPONSE_HEADER_CAPACITY = 512 };
static const char HEADER_FORMAT[] =
    "HTTP/1.1 %d %s\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %zu\r\n"
    "Connection: close\r\n"
    "Cache-Control: no-store\r\n"
    "X-Content-Type-Options: nosniff\r\n\r\n";

struct ErrorResponse {
    int status;
    const char *reason;
    const char *json;
};

static const struct ErrorResponse ERROR_RESPONSES[] = {
    {HTTP_BAD_REQUEST, "Bad Request", "{\"error\":\"invalid request\"}"},
    {HTTP_NOT_FOUND, "Not Found", "{\"error\":\"not found\"}"},
    {HTTP_METHOD_NOT_ALLOWED, "Method Not Allowed", "{\"error\":\"method not allowed\"}"},
    {HTTP_LENGTH_REQUIRED, "Length Required", "{\"error\":\"content-length required\"}"},
    {HTTP_CONTENT_TOO_LARGE, "Content Too Large", "{\"error\":\"request too large\"}"}
};
static const struct ErrorResponse DEFAULT_ERROR = {
    HTTP_INTERNAL_SERVER_ERROR, "Internal Server Error", "{\"error\":\"server error\"}"
};

static void send_all(int fd, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t result = send(fd, data + sent, length - sent, 0);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return;
        sent += (size_t)result;
    }
}

void http_respond(int fd, int status, const char *reason,
                  const char *mime, const char *body, size_t length) {
    char header[RESPONSE_HEADER_CAPACITY];
    int n = snprintf(header, sizeof(header), HEADER_FORMAT, status, reason, mime, length);
    if (n < 0 || (size_t)n >= sizeof(header)) return;
    send_all(fd, header, (size_t)n);
    if (length > 0) send_all(fd, body, length);
}

void http_json_reply(int fd, int status, const char *reason, const char *json) {
    http_respond(fd, status, reason, HTTP_MIME_JSON, json, strlen(json));
}

static const struct ErrorResponse *find_error_response(int status) {
    for (size_t i = 0; i < sizeof(ERROR_RESPONSES) / sizeof(ERROR_RESPONSES[0]); i++) {
        if (ERROR_RESPONSES[i].status == status) return &ERROR_RESPONSES[i];
    }
    return &DEFAULT_ERROR;
}

void http_error_reply(int fd, int status) {
    const struct ErrorResponse *response = find_error_response(status);
    http_json_reply(fd, response->status, response->reason, response->json);
}
