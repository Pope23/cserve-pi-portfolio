/* CServe Pi: intentionally small HTTP/1.1 task API for Linux / Raspberry Pi OS.
 * Educational server: sequential connections, in-memory data, no authentication.
 * Do not expose directly to the public Internet. */
#define _POSIX_C_SOURCE 200809L
#include "http_response.h"
#include "task_storage.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8080
#define MAX_HEADER 8192
#define MAX_BODY 4096
#define MAX_WIRE (MAX_HEADER + MAX_BODY + 1)
#define MAX_JSON 65536
#define MAX_PAGE 32768

struct Request {
    char method[16];
    char path[160];
    char body[MAX_BODY + 1];
    size_t content_length;
};

static char *find_header_end(char *buf) {
    return strstr(buf, "\r\n\r\n");
}

static bool parse_content_length(const char *value, size_t *length) {
    if (!*value) return false;
    size_t result = 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (!isdigit(*p)) return false;
        unsigned digit = (unsigned)(*p - (unsigned char)'0');
        if (result > (SIZE_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    *length = result;
    return true;
}

/* Returns 0 on success, or an HTTP status to report to the client. */
static int parse_headers(char *buf, struct Request *req, bool *length_seen) {
    char *save = NULL;
    char *line = strtok_r(buf, "\r\n", &save);
    if (!line) return 400;
    char version[16];
    char extra;
    if (sscanf(line, "%15s %159s %15s %c", req->method, req->path,
               version, &extra) != 3 || strcmp(version, "HTTP/1.1") != 0) return 400;
    if (req->path[0] != '/') return 400;
    while ((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
        char *colon = strchr(line, ':');
        if (!colon) return 400;
        *colon++ = '\0';
        while (*colon == ' ' || *colon == '\t') colon++;
        if (strcasecmp(line, "Transfer-Encoding") == 0) return 400;
        if (strcasecmp(line, "Content-Length") != 0) continue;
        if (*length_seen) return 400;
        *length_seen = true;
        if (!parse_content_length(colon, &req->content_length)) return 400;
        if (req->content_length > MAX_BODY) return 413;
    }
    if (strcmp(req->method, "POST") == 0 && !*length_seen) return 411;
    return 0;
}

static int read_request(int fd, struct Request *req) {
    char wire[MAX_WIRE + 1] = {0};
    size_t used = 0;
    size_t header_size = 0;
    bool headers_done = false;
    bool length_seen = false;
    for (;;) {
        if (!headers_done) {
            char *marker = find_header_end(wire);
            if (marker) {
                header_size = (size_t)(marker - wire) + 4U;
                if (header_size > MAX_HEADER) return 413;
                *marker = '\0';
                int status = parse_headers(wire, req, &length_seen);
                if (status != 0) return status;
                headers_done = true;
            } else if (used >= MAX_HEADER) {
                return 413;
            }
        }
        if (headers_done && used >= header_size + req->content_length) {
            if (req->content_length > 0) {
                const char *data = wire + header_size;
                if (memchr(data, '\0', req->content_length)) return 400;
                memcpy(req->body, data, req->content_length);
            }
            req->body[req->content_length] = '\0';
            return 0;
        }
        if (used >= MAX_WIRE) return 413;
        ssize_t n = recv(fd, wire + used, MAX_WIRE - used, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 400;
        used += (size_t)n;
        wire[used] = '\0';
    }
}

static const char *skip_space(const char *p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    return p;
}

/* Narrow, bounded JSON string decoder, including \uXXXX escapes. */
static bool hex4(const char **cursor, uint32_t *value) {
    uint32_t result = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char ch = (unsigned char)**cursor;
        if (ch == 0U) return false;
        (*cursor)++;
        unsigned digit;
        if (ch >= '0' && ch <= '9') digit = (unsigned)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') digit = (unsigned)(ch - 'a') + 10U;
        else if (ch >= 'A' && ch <= 'F') digit = (unsigned)(ch - 'A') + 10U;
        else return false;
        result = (result << 4U) | digit;
    }
    *value = result;
    return true;
}

static bool put_utf8(char *dest, size_t cap, size_t *used, uint32_t cp) {
    unsigned char bytes[4];
    size_t count;
    if (cp <= 0x7FU) { bytes[0] = (unsigned char)cp; count = 1; }
    else if (cp <= 0x7FFU) {
        bytes[0] = (unsigned char)(0xC0U | (cp >> 6U));
        bytes[1] = (unsigned char)(0x80U | (cp & 0x3FU));
        count = 2;
    } else if (cp <= 0xFFFFU) {
        bytes[0] = (unsigned char)(0xE0U | (cp >> 12U));
        bytes[1] = (unsigned char)(0x80U | ((cp >> 6U) & 0x3FU));
        bytes[2] = (unsigned char)(0x80U | (cp & 0x3FU));
        count = 3;
    } else if (cp <= 0x10FFFFU) {
        bytes[0] = (unsigned char)(0xF0U | (cp >> 18U));
        bytes[1] = (unsigned char)(0x80U | ((cp >> 12U) & 0x3FU));
        bytes[2] = (unsigned char)(0x80U | ((cp >> 6U) & 0x3FU));
        bytes[3] = (unsigned char)(0x80U | (cp & 0x3FU));
        count = 4;
    } else return false;
    if (*used + count >= cap) return false;
    for (size_t i = 0; i < count; i++) dest[(*used)++] = (char)bytes[i];
    return true;
}

static bool decode_escape(const char **cursor, uint32_t *codepoint) {
    char escape = *(*cursor)++;
    switch (escape) {
        case '"': *codepoint = '"'; return true;
        case '\\': *codepoint = '\\'; return true;
        case '/': *codepoint = '/'; return true;
        case 'u': break;
        default: return false; /* control characters forbidden in task titles */
    }
    if (!hex4(cursor, codepoint)) return false;
    if (*codepoint >= 0xDC00U && *codepoint <= 0xDFFFU) return false;
    if (*codepoint < 0xD800U || *codepoint > 0xDBFFU) return true;
    if ((*cursor)[0] != '\\' || (*cursor)[1] != 'u') return false;
    *cursor += 2;
    uint32_t low;
    if (!hex4(cursor, &low) || low < 0xDC00U || low > 0xDFFFU) return false;
    *codepoint = 0x10000U + ((*codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
    return true;
}

static bool read_json_string(const char **cursor, char *dest, size_t cap) {
    const char *p = *cursor;
    if (*p++ != '"') return false;
    size_t used = 0;
    while (*p && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch < 32U) return false;
        if (ch != '\\') {
            if (used + 1U >= cap) return false;
            dest[used++] = (char)ch;
        } else {
            uint32_t cp;
            if (!decode_escape(&p, &cp) || cp < 32U || !put_utf8(dest, cap, &used, cp)) return false;
        }
    }
    if (*p != '"') return false;
    dest[used] = '\0';
    *cursor = p + 1;
    return true;
}

static bool get_title(const char *body, char *title) {
    const char *p = skip_space(body);
    if (*p++ != '{') return false;
    p = skip_space(p);
    char key[16];
    if (!read_json_string(&p, key, sizeof(key)) || strcmp(key, "title") != 0) return false;
    p = skip_space(p);
    if (*p++ != ':') return false;
    p = skip_space(p);
    if (!read_json_string(&p, title, MAX_TITLE + 1)) return false;
    p = skip_space(p);
    if (*p++ != '}') return false;
    if (*skip_space(p) != '\0') return false;
    for (const char *c = title; *c; c++) {
        if (!isspace((unsigned char)*c)) return true;
    }
    return false;
}

static bool append(char *out, size_t cap, size_t *pos, const char *text) {
    size_t n = strlen(text);
    if (n > cap - *pos - 1U) return false;
    memcpy(out + *pos, text, n);
    *pos += n;
    out[*pos] = '\0';
    return true;
}

static bool json_append_title(char *out, size_t cap, size_t *pos, const char *title) {
    if (!append(out, cap, pos, "\"")) return false;
    for (const char *p = title; *p; p++) {
        const char *add = NULL;
        if (*p == '"') add = "\\\"";
        else if (*p == '\\') add = "\\\\";
        if (add) {
            if (!append(out, cap, pos, add)) return false;
        } else {
            char c[2] = {*p, '\0'};
            if (!append(out, cap, pos, c)) return false;
        }
    }
    return append(out, cap, pos, "\"");
}

static bool append_task(char *out, size_t cap, size_t *pos, const struct Task *task) {
    char prefix[64];
    int n = snprintf(prefix, sizeof(prefix), "{\"id\":%d,\"title\":", task->id);
    if (n < 0 || (size_t)n >= sizeof(prefix)) return false;
    return append(out, cap, pos, prefix)
        && json_append_title(out, cap, pos, task->title)
        && append(out, cap, pos, "}");
}

static void list_tasks(int fd, const struct TaskStorage *storage) {
    char out[MAX_JSON] = {0};
    size_t pos = 0;
    if (!append(out, sizeof(out), &pos, "{\"tasks\":[")) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
    bool first = true;
    for (size_t i = 0; i < MAX_TASKS; i++) {
        const struct Task *task = task_storage_at(storage, i);
        if (!task) continue;
        if (!first && !append(out, sizeof(out), &pos, ",")) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
        first = false;
        if (!append_task(out, sizeof(out), &pos, task)) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
    }
    if (!append(out, sizeof(out), &pos, "]}")) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
    http_json_reply(fd, HTTP_OK, HTTP_REASON_OK, out);
}

static void create_task(int fd, const char *body, struct TaskStorage *storage) {
    char title[MAX_TITLE + 1];
    if (!get_title(body, title)) { http_error_reply(fd, HTTP_BAD_REQUEST); return; }
    const struct Task *task = NULL;
    enum TaskCreateResult result = task_storage_create(storage, title, &task);
    if (result == TASK_CREATE_FULL) {
        http_json_reply(fd, HTTP_INSUFFICIENT_STORAGE, HTTP_REASON_INSUFFICIENT_STORAGE,
                        "{\"error\":\"task limit reached\"}");
        return;
    }
    if (result == TASK_CREATE_ID_EXHAUSTED) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
    char out[MAX_TITLE * 2 + 128] = {0};
    size_t pos = 0;
    if (!append_task(out, sizeof(out), &pos, task)) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
    http_json_reply(fd, HTTP_CREATED, HTTP_REASON_CREATED, out);
}

static void delete_task(int fd, const char *path, struct TaskStorage *storage) {
    const char *id_text = path + strlen("/api/tasks/");
    if (!*id_text) { http_error_reply(fd, HTTP_NOT_FOUND); return; }
    for (const char *p = id_text; *p; p++) {
        if (!isdigit((unsigned char)*p)) { http_error_reply(fd, HTTP_NOT_FOUND); return; }
    }
    errno = 0;
    char *end = NULL;
    long number = strtol(id_text, &end, 10);
    if (errno != 0 || *end != '\0' || number <= 0 || number > INT32_MAX) {
        http_error_reply(fd, HTTP_NOT_FOUND);
        return;
    }
    if (task_storage_delete(storage, (int)number)) {
        http_respond(fd, HTTP_NO_CONTENT, HTTP_REASON_NO_CONTENT, HTTP_MIME_EMPTY_JSON, "", 0);
        return;
    }
    http_error_reply(fd, HTTP_NOT_FOUND);
}

static void serve_page(int fd) {
    FILE *f = fopen("web/index.html", "rb");
    if (!f) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
    char html[MAX_PAGE];
    size_t n = fread(html, 1, sizeof(html), f);
    bool bad = ferror(f) || (!feof(f) && n == sizeof(html));
    fclose(f);
    if (bad) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
    http_respond(fd, HTTP_OK, HTTP_REASON_OK, HTTP_MIME_HTML, html, n);
}

static void route(int fd, const struct Request *req, struct TaskStorage *storage) {
    if (strcmp(req->method, "GET") == 0) {
        if (strcmp(req->path, "/") == 0) { serve_page(fd); return; }
        if (strcmp(req->path, "/health") == 0) {
            http_json_reply(fd, HTTP_OK, HTTP_REASON_OK, "{\"status\":\"ok\"}"); return;
        }
        if (strcmp(req->path, "/api/tasks") == 0) { list_tasks(fd, storage); return; }
    }
    if (strcmp(req->method, "POST") == 0 && strcmp(req->path, "/api/tasks") == 0) {
        create_task(fd, req->body, storage); return;
    }
    if (strcmp(req->method, "DELETE") == 0 && strncmp(req->path, "/api/tasks/", 11) == 0) {
        delete_task(fd, req->path, storage); return;
    }
    if (strcmp(req->path, "/api/tasks") == 0) { http_error_reply(fd, HTTP_METHOD_NOT_ALLOWED); return; }
    http_error_reply(fd, HTTP_NOT_FOUND);
}

static void handle_client(int fd, struct TaskStorage *storage) {
    struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct Request request = {0};
    int status = read_request(fd, &request);
    if (status != 0) http_error_reply(fd, status);
    else route(fd, &request, storage);
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
