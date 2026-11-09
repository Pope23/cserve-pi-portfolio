#include "http_parser.h"
#include "http_response.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

#define STRINGIFY_VALUE(value) #value
#define STRINGIFY(value) STRINGIFY_VALUE(value)
#define HTTP_VERSION_MAX 15
#define MAX_WIRE (HTTP_MAX_HEADER + HTTP_MAX_BODY + 1)
enum { DECIMAL_BASE = 10 };
static const char HEADER_END[] = "\r\n\r\n";
static const char HEADER_SEPARATORS[] = "\r\n";
static const char HTTP_VERSION[] = "HTTP/1.1";
static const char CONTENT_LENGTH[] = "Content-Length";
static const char TRANSFER_ENCODING[] = "Transfer-Encoding";
static const char POST_METHOD[] = "POST";
static const char REQUEST_LINE_FORMAT[] =
    "%" STRINGIFY(HTTP_METHOD_MAX) "s %" STRINGIFY(HTTP_PATH_MAX)
    "s %" STRINGIFY(HTTP_VERSION_MAX) "s %c";
enum { REQUEST_LINE_FIELDS = 3 };

struct RequestReader {
    char wire[MAX_WIRE + 1];
    size_t used;
    size_t header_size;
    bool headers_done;
};

static bool parse_content_length(const char *value, size_t *length) {
    if (!*value) return false;
    size_t result = 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (!isdigit(*p)) return false;
        unsigned digit = (unsigned)(*p - (unsigned char)'0');
        if (result > (SIZE_MAX - digit) / DECIMAL_BASE) return false;
        result = result * DECIMAL_BASE + digit;
    }
    *length = result;
    return true;
}

static int parse_request_line(const char *line, struct Request *req) {
    if (!line) return HTTP_BAD_REQUEST;
    char version[HTTP_VERSION_MAX + 1];
    char extra;
    if (sscanf(line, REQUEST_LINE_FORMAT, req->method, req->path, version, &extra)
        != REQUEST_LINE_FIELDS || strcmp(version, HTTP_VERSION) != 0) return HTTP_BAD_REQUEST;
    return req->path[0] == '/' ? 0 : HTTP_BAD_REQUEST;
}

static int parse_header(char *line, struct Request *req, bool *length_seen) {
    char *colon = strchr(line, ':');
    if (!colon) return HTTP_BAD_REQUEST;
    *colon++ = '\0';
    while (*colon == ' ' || *colon == '\t') colon++;
    if (strcasecmp(line, TRANSFER_ENCODING) == 0) return HTTP_BAD_REQUEST;
    if (strcasecmp(line, CONTENT_LENGTH) != 0) return 0;
    if (*length_seen) return HTTP_BAD_REQUEST;
    *length_seen = true;
    if (!parse_content_length(colon, &req->content_length)) return HTTP_BAD_REQUEST;
    return req->content_length > HTTP_MAX_BODY ? HTTP_CONTENT_TOO_LARGE : 0;
}

static int parse_headers(char *buf, struct Request *req) {
    char *save = NULL;
    char *line = strtok_r(buf, HEADER_SEPARATORS, &save);
    int status = parse_request_line(line, req);
    if (status != 0) return status;
    bool length_seen = false;
    while ((line = strtok_r(NULL, HEADER_SEPARATORS, &save)) != NULL) {
        status = parse_header(line, req, &length_seen);
        if (status != 0) return status;
    }
    if (strcmp(req->method, POST_METHOD) == 0 && !length_seen) return HTTP_LENGTH_REQUIRED;
    return 0;
}

static int read_headers(struct RequestReader *reader, struct Request *req) {
    char *marker = strstr(reader->wire, HEADER_END);
    if (!marker) return reader->used >= HTTP_MAX_HEADER ? HTTP_CONTENT_TOO_LARGE : 0;
    reader->header_size = (size_t)(marker - reader->wire) + sizeof(HEADER_END) - 1U;
    if (reader->header_size > HTTP_MAX_HEADER) return HTTP_CONTENT_TOO_LARGE;
    *marker = '\0';
    int status = parse_headers(reader->wire, req);
    if (status != 0) return status;
    reader->headers_done = true;
    return 0;
}

static int copy_body(const struct RequestReader *reader, struct Request *req) {
    if (req->content_length > 0) {
        const char *data = reader->wire + reader->header_size;
        if (memchr(data, '\0', req->content_length)) return HTTP_BAD_REQUEST;
        memcpy(req->body, data, req->content_length);
    }
    req->body[req->content_length] = '\0';
    return 0;
}

static int receive_bytes(int fd, struct RequestReader *reader) {
    if (reader->used >= MAX_WIRE) return HTTP_CONTENT_TOO_LARGE;
    ssize_t n = recv(fd, reader->wire + reader->used, MAX_WIRE - reader->used, 0);
    if (n < 0 && errno == EINTR) return 0;
    if (n <= 0) return HTTP_BAD_REQUEST;
    reader->used += (size_t)n;
    reader->wire[reader->used] = '\0';
    return 0;
}

int http_read_request(int fd, struct Request *req) {
    struct RequestReader reader = {0};
    for (;;) {
        if (!reader.headers_done) {
            int status = read_headers(&reader, req);
            if (status != 0) return status;
        }
        if (reader.headers_done && reader.used >= reader.header_size + req->content_length)
            return copy_body(&reader, req);
        int status = receive_bytes(fd, &reader);
        if (status != 0) return status;
    }
}
