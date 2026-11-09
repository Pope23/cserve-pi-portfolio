#include "http_parser.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

enum { TEST_FD = 7, READ_CHUNK_BYTES = 3, REQUEST_CAPACITY = 512 };
static const char *incoming;
static size_t offset;
static size_t chunk_size;
static bool interrupt_once;
static bool fail_receive;

ssize_t __wrap_recv(int fd, void *buffer, size_t capacity, int flags);

ssize_t __wrap_recv(int fd, void *buffer, size_t capacity, int flags) {
    assert(fd == TEST_FD && flags == 0);
    if (interrupt_once) { interrupt_once = false; errno = EINTR; return -1; }
    if (fail_receive) { errno = EIO; return -1; }
    size_t count = strlen(incoming) - offset;
    if (count > chunk_size) count = chunk_size;
    if (count > capacity) count = capacity;
    memcpy(buffer, incoming + offset, count);
    offset += count;
    return (ssize_t)count;
}

static int parse(const char *wire, struct Request *request) {
    incoming = wire;
    offset = 0;
    *request = (struct Request){0};
    return http_read_request(TEST_FD, request);
}

static void test_fragmentation_and_eintr(void) {
    struct Request request;
    chunk_size = 1;
    interrupt_once = true;
    assert(parse("POST /api/tasks HTTP/1.1\r\nContent-Length: 4\r\n\r\ndata", &request) == 0);
    assert(strcmp(request.method, "POST") == 0);
    assert(strcmp(request.path, "/api/tasks") == 0);
    assert(request.content_length == 4);
    assert(strcmp(request.body, "data") == 0);
    chunk_size = READ_CHUNK_BYTES;
}

static void test_premature_eof(void) {
    struct Request request;
    assert(parse("POST /api/tasks HTTP/1.1\r\nContent-Length: 4\r\n\r\nda", &request) == 400);
    assert(parse("GET /health HTTP/1.1\r\n", &request) == 400);
    assert(parse("", &request) == 400);
}

static void test_receive_error(void) {
    struct Request request;
    fail_receive = true;
    assert(parse("GET /health HTTP/1.1\r\n\r\n", &request) == 400);
    fail_receive = false;
}

static void test_field_widths(void) {
    struct Request request;
    char path[161];
    char wire[REQUEST_CAPACITY];
    memset(path, 'x', sizeof(path) - 1U);
    path[0] = '/';
    path[159] = '\0';
    int n = snprintf(wire, sizeof(wire), "ABCDEFGHIJKLMNO %s HTTP/1.1\r\n\r\n", path);
    assert(n > 0 && (size_t)n < sizeof(wire));
    assert(parse(wire, &request) == 0);
    assert(strlen(request.method) == 15 && strlen(request.path) == 159);
    assert(parse("ABCDEFGHIJKLMNOP / HTTP/1.1\r\n\r\n", &request) == 400);
    path[159] = 'x'; path[160] = '\0';
    n = snprintf(wire, sizeof(wire), "GET %s HTTP/1.1\r\n\r\n", path);
    assert(n > 0 && (size_t)n < sizeof(wire));
    assert(parse(wire, &request) == 400);
}

static void test_request_line_rejections(void) {
    struct Request request;
    assert(parse("\r\n\r\n", &request) == 400);
    assert(parse("GET relative HTTP/1.1\r\n\r\n", &request) == 400);
    assert(parse("GET / HTTP/1.0\r\n\r\n", &request) == 400);
    assert(parse("GET / HTTP/1.1 extra\r\n\r\n", &request) == 400);
}

static void test_header_error_order(void) {
    struct Request request;
    assert(parse("POST / HTTP/1.1\r\nContent-Length: 4097\r\nContent-Length: 0\r\n\r\n",
                 &request) == 413);
    assert(parse("POST / HTTP/1.1\r\nBroken\r\nContent-Length: 4097\r\n\r\n",
                 &request) == 400);
    assert(parse("POST / HTTP/1.1\r\ncontent-length:\t0\r\n\r\n", &request) == 0);
    assert(request.content_length == 0 && request.body[0] == '\0');
}

int main(void) {
    test_fragmentation_and_eintr();
    test_premature_eof();
    test_receive_error();
    test_field_widths();
    test_request_line_rejections();
    test_header_error_order();
    puts("HTTP parser: 6 cases OK");
    return 0;
}
