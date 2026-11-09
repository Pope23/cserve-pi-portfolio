#ifndef CSERVE_HTTP_PARSER_H
#define CSERVE_HTTP_PARSER_H

#include <stddef.h>

#define HTTP_MAX_HEADER 8192
#define HTTP_MAX_BODY 4096
#define HTTP_METHOD_MAX 15
#define HTTP_PATH_MAX 159

struct Request {
    char method[HTTP_METHOD_MAX + 1];
    char path[HTTP_PATH_MAX + 1];
    char body[HTTP_MAX_BODY + 1];
    size_t content_length;
};

/* Zero-initialize req before calling. Returns 0 on success or an HTTP status
 * on error; the partially parsed output must not be used on error.
 * Borrows fd, without closing it or setting timeouts. Handles one bounded
 * request, copying and NUL-terminating its body; surplus bytes are ignored.
 * This preserves the existing HTTP subset rather than tightening validation. */
int http_read_request(int fd, struct Request *req);

#endif
