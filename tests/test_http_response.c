/* Deterministic transport/formatting characterization; no production hooks. */
#include "http_response.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

enum { CAPTURE_CAPACITY = 4096, SHORT_SEND_BYTES = 3, TEST_FD = 7 };
enum SendMode { SEND_COMPLETE, SEND_INTERRUPTED_PARTIAL, SEND_ZERO, SEND_ERROR };
enum FormatMode { FORMAT_NORMAL, FORMAT_ERROR, FORMAT_TRUNCATED };
static enum SendMode send_mode;
static enum FormatMode format_mode;
static char captured[CAPTURE_CAPACITY];
static size_t captured_length;
static size_t send_calls;

ssize_t __wrap_send(int fd, const void *data, size_t length, int flags);
int __wrap_snprintf(char *out, size_t capacity, const char *format, ...);

ssize_t __wrap_send(int fd, const void *data, size_t length, int flags) {
    assert(fd == TEST_FD && flags == 0);
    send_calls++;
    if (send_mode == SEND_ZERO) return 0;
    if (send_mode == SEND_ERROR) { errno = EPIPE; return -1; }
    if (send_mode == SEND_INTERRUPTED_PARTIAL && send_calls == 1) {
        errno = EINTR;
        return -1;
    }
    if (send_mode == SEND_INTERRUPTED_PARTIAL && length > SHORT_SEND_BYTES)
        length = SHORT_SEND_BYTES;
    assert(length <= sizeof(captured) - captured_length);
    memcpy(captured + captured_length, data, length);
    captured_length += length;
    return (ssize_t)length;
}

int __wrap_snprintf(char *out, size_t capacity, const char *format, ...) {
    if (format_mode == FORMAT_ERROR) return -1;
    if (format_mode == FORMAT_TRUNCATED) return (int)capacity;
    va_list args;
    va_start(args, format);
    int result = vsnprintf(out, capacity, format, args);
    va_end(args);
    return result;
}

static void reset_capture(enum SendMode mode, enum FormatMode formatting) {
    send_mode = mode;
    format_mode = formatting;
    captured_length = 0;
    send_calls = 0;
}

static void assert_wire(const char *expected) {
    assert(captured_length == strlen(expected));
    assert(memcmp(captured, expected, captured_length) == 0);
}

static void test_partial_send_and_eintr(void) {
    reset_capture(SEND_INTERRUPTED_PARTIAL, FORMAT_NORMAL);
    http_json_reply(TEST_FD, 200, "OK", "{\"status\":\"ok\"}");
    assert_wire("HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                "Content-Length: 15\r\nConnection: close\r\n"
                "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n"
                "{\"status\":\"ok\"}");
}

static void test_empty_body(void) {
    reset_capture(SEND_COMPLETE, FORMAT_NORMAL);
    http_respond(TEST_FD, 204, "No Content", "application/json", "", 0);
    assert_wire("HTTP/1.1 204 No Content\r\nContent-Type: application/json\r\n"
                "Content-Length: 0\r\nConnection: close\r\n"
                "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n");
}

static void test_format_failure(enum FormatMode mode) {
    reset_capture(SEND_COMPLETE, mode);
    http_json_reply(TEST_FD, 200, "OK", "{}");
    assert(captured_length == 0 && send_calls == 0);
}

static void test_header_overflow(void) {
    char oversized_mime[CAPTURE_CAPACITY];
    memset(oversized_mime, 'x', sizeof(oversized_mime) - 1U);
    oversized_mime[sizeof(oversized_mime) - 1U] = '\0';
    reset_capture(SEND_COMPLETE, FORMAT_NORMAL);
    http_respond(TEST_FD, 200, "OK", oversized_mime, "body", strlen("body"));
    assert(captured_length == 0 && send_calls == 0);
}

static void test_send_failure(enum SendMode mode) {
    reset_capture(mode, FORMAT_NORMAL);
    http_json_reply(TEST_FD, 200, "OK", "{}");
    assert(captured_length == 0);
    /* Existing void API attempts header and body independently, then returns. */
    assert(send_calls == 2);
}

static void test_error_replies(void) {
    const struct { int code; const char *status; const char *body; } cases[] = {
        {400, "400 Bad Request", "{\"error\":\"invalid request\"}"},
        {404, "404 Not Found", "{\"error\":\"not found\"}"},
        {405, "405 Method Not Allowed", "{\"error\":\"method not allowed\"}"},
        {411, "411 Length Required", "{\"error\":\"content-length required\"}"},
        {413, "413 Content Too Large", "{\"error\":\"request too large\"}"},
        {500, "500 Internal Server Error", "{\"error\":\"server error\"}"},
        {999, "500 Internal Server Error", "{\"error\":\"server error\"}"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset_capture(SEND_COMPLETE, FORMAT_NORMAL);
        http_error_reply(TEST_FD, cases[i].code);
        assert(captured_length < sizeof(captured));
        captured[captured_length] = '\0';
        assert(strstr(captured, cases[i].status) == captured + strlen("HTTP/1.1 "));
        assert(strcmp(strstr(captured, "\r\n\r\n") + strlen("\r\n\r\n"), cases[i].body) == 0);
    }
}

int main(void) {
    test_partial_send_and_eintr();
    test_empty_body();
    test_format_failure(FORMAT_ERROR);
    test_format_failure(FORMAT_TRUNCATED);
    test_header_overflow();
    test_send_failure(SEND_ZERO);
    test_send_failure(SEND_ERROR);
    test_error_replies();
    puts("HTTP response characterization: 8 cases OK");
    return 0;
}
