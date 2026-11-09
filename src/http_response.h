#ifndef CSERVE_HTTP_RESPONSE_H
#define CSERVE_HTTP_RESPONSE_H

#include <stddef.h>

enum HttpStatus {
    HTTP_OK = 200,
    HTTP_CREATED = 201,
    HTTP_NO_CONTENT = 204,
    HTTP_BAD_REQUEST = 400,
    HTTP_NOT_FOUND = 404,
    HTTP_METHOD_NOT_ALLOWED = 405,
    HTTP_LENGTH_REQUIRED = 411,
    HTTP_CONTENT_TOO_LARGE = 413,
    HTTP_INTERNAL_SERVER_ERROR = 500,
    HTTP_INSUFFICIENT_STORAGE = 507
};

#define HTTP_REASON_OK "OK"
#define HTTP_REASON_CREATED "Created"
#define HTTP_REASON_NO_CONTENT "No Content"
#define HTTP_REASON_INSUFFICIENT_STORAGE "Insufficient Storage"
#define HTTP_MIME_JSON "application/json; charset=utf-8"
#define HTTP_MIME_EMPTY_JSON "application/json"
#define HTTP_MIME_HTML "text/html; charset=utf-8"

/* Trusted metadata must be NUL-terminated. Body contains at least length bytes.
 * These helpers borrow fd and buffers; they neither close fd nor free data.
 * As before, formatting/transport errors stop that operation without a status
 * return. The caller owns connection cleanup and SIGPIPE policy. */
void http_respond(int fd, int status, const char *reason,
                  const char *mime, const char *body, size_t length);
void http_json_reply(int fd, int status, const char *reason, const char *json);
void http_error_reply(int fd, int status);

#endif
