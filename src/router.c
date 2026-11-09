#include "router.h"
#include "http_parser.h"
#include "http_response.h"
#include "task_storage.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MAX_JSON = 65536, MAX_PAGE = 32768,
    JSON_KEY_CAPACITY = 16, TASK_PREFIX_CAPACITY = 64,
    JSON_ESCAPE_FACTOR = 2, CREATE_JSON_OVERHEAD = 128,
    CREATE_JSON_CAPACITY = MAX_TITLE * JSON_ESCAPE_FACTOR + CREATE_JSON_OVERHEAD,
    CONTROL_CHARACTER_LIMIT = 32, JSON_HEX_DIGITS = 4, HEX_DIGIT_BITS = 4,
    HEX_ALPHA_OFFSET = 10, ID_DECIMAL_BASE = 10,
    UTF8_MAX_BYTES = 4, UTF8_CONTINUATION_BITS = 6,
    UTF8_ASCII_MAX = 0x7F, UTF8_TWO_BYTE_MAX = 0x7FF, UTF8_THREE_BYTE_MAX = 0xFFFF,
    UTF8_TWO_BYTE_LEAD = 0xC0, UTF8_THREE_BYTE_LEAD = 0xE0, UTF8_FOUR_BYTE_LEAD = 0xF0,
    UTF8_CONTINUATION_LEAD = 0x80, UTF8_CONTINUATION_MASK = 0x3F,
    UNICODE_MAX = 0x10FFFF, UNICODE_SUPPLEMENTARY_MIN = 0x10000,
    SURROGATE_HIGH_MIN = 0xD800, SURROGATE_HIGH_MAX = 0xDBFF,
    SURROGATE_LOW_MIN = 0xDC00, SURROGATE_LOW_MAX = 0xDFFF,
    SURROGATE_PAYLOAD_BITS = 10
};
static const char ROOT_PATH[] = "/";
static const char HEALTH_PATH[] = "/health";
static const char TASKS_PATH[] = "/api/tasks";
static const char TASK_ID_PREFIX[] = "/api/tasks/";
static const char GET_METHOD[] = "GET";
static const char POST_METHOD[] = "POST";
static const char DELETE_METHOD[] = "DELETE";
static const char DASHBOARD_PATH[] = "web/index.html";
static const char BINARY_READ_MODE[] = "rb";
static const char TITLE_KEY[] = "title";
static const char HEALTH_JSON[] = "{\"status\":\"ok\"}";
static const char STORAGE_FULL_JSON[] = "{\"error\":\"task limit reached\"}";
static const char TASK_PREFIX_FORMAT[] = "{\"id\":%d,\"title\":";
static const char TASK_LIST_OPEN[] = "{\"tasks\":[";
static const char TASK_LIST_CLOSE[] = "]}";

static const char *skip_space(const char *p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    return p;
}

/* Narrow, bounded JSON string decoder, including \uXXXX escapes. */
static bool hex4(const char **cursor, uint32_t *value) {
    uint32_t result = 0;
    for (int i = 0; i < JSON_HEX_DIGITS; i++) {
        unsigned char ch = (unsigned char)**cursor;
        if (ch == 0U) return false;
        (*cursor)++;
        unsigned digit;
        if (ch >= '0' && ch <= '9') digit = (unsigned)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') digit = (unsigned)(ch - 'a') + HEX_ALPHA_OFFSET;
        else if (ch >= 'A' && ch <= 'F') digit = (unsigned)(ch - 'A') + HEX_ALPHA_OFFSET;
        else return false;
        result = (result << HEX_DIGIT_BITS) | digit;
    }
    *value = result;
    return true;
}

static bool put_utf8(char *dest, size_t cap, size_t *used, uint32_t cp) {
    unsigned char bytes[UTF8_MAX_BYTES];
    size_t count;
    if (cp <= UTF8_ASCII_MAX) { bytes[0] = (unsigned char)cp; count = 1; }
    else if (cp <= UTF8_TWO_BYTE_MAX) {
        bytes[0] = (unsigned char)(UTF8_TWO_BYTE_LEAD | (cp >> UTF8_CONTINUATION_BITS));
        bytes[1] = (unsigned char)(UTF8_CONTINUATION_LEAD | (cp & UTF8_CONTINUATION_MASK));
        count = 2;
    } else if (cp <= UTF8_THREE_BYTE_MAX) {
        bytes[0] = (unsigned char)(UTF8_THREE_BYTE_LEAD | (cp >> (UTF8_CONTINUATION_BITS * 2U)));
        bytes[1] = (unsigned char)(UTF8_CONTINUATION_LEAD | ((cp >> UTF8_CONTINUATION_BITS) & UTF8_CONTINUATION_MASK));
        bytes[2] = (unsigned char)(UTF8_CONTINUATION_LEAD | (cp & UTF8_CONTINUATION_MASK));
        count = 3;
    } else if (cp <= UNICODE_MAX) {
        bytes[0] = (unsigned char)(UTF8_FOUR_BYTE_LEAD | (cp >> (UTF8_CONTINUATION_BITS * 3U)));
        bytes[1] = (unsigned char)(UTF8_CONTINUATION_LEAD | ((cp >> (UTF8_CONTINUATION_BITS * 2U)) & UTF8_CONTINUATION_MASK));
        bytes[2] = (unsigned char)(UTF8_CONTINUATION_LEAD | ((cp >> UTF8_CONTINUATION_BITS) & UTF8_CONTINUATION_MASK));
        bytes[3] = (unsigned char)(UTF8_CONTINUATION_LEAD | (cp & UTF8_CONTINUATION_MASK));
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
    if (*codepoint >= SURROGATE_LOW_MIN && *codepoint <= SURROGATE_LOW_MAX) return false;
    if (*codepoint < SURROGATE_HIGH_MIN || *codepoint > SURROGATE_HIGH_MAX) return true;
    if ((*cursor)[0] != '\\' || (*cursor)[1] != 'u') return false;
    *cursor += 2;
    uint32_t low;
    if (!hex4(cursor, &low) || low < SURROGATE_LOW_MIN || low > SURROGATE_LOW_MAX) return false;
    *codepoint = UNICODE_SUPPLEMENTARY_MIN + ((*codepoint - SURROGATE_HIGH_MIN) << SURROGATE_PAYLOAD_BITS) + (low - SURROGATE_LOW_MIN);
    return true;
}

static bool read_json_string(const char **cursor, char *dest, size_t cap) {
    const char *p = *cursor;
    if (*p++ != '"') return false;
    size_t used = 0;
    while (*p && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch < CONTROL_CHARACTER_LIMIT) return false;
        if (ch != '\\') {
            if (used + 1U >= cap) return false;
            dest[used++] = (char)ch;
        } else {
            uint32_t cp;
            if (!decode_escape(&p, &cp) || cp < CONTROL_CHARACTER_LIMIT || !put_utf8(dest, cap, &used, cp)) return false;
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
    char key[JSON_KEY_CAPACITY];
    if (!read_json_string(&p, key, sizeof(key)) || strcmp(key, TITLE_KEY) != 0) return false;
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

/* Private callers maintain *pos < cap, reserving space for the NUL terminator. */
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
    char prefix[TASK_PREFIX_CAPACITY];
    int n = snprintf(prefix, sizeof(prefix), TASK_PREFIX_FORMAT, task->id);
    if (n < 0 || (size_t)n >= sizeof(prefix)) return false;
    return append(out, cap, pos, prefix)
        && json_append_title(out, cap, pos, task->title)
        && append(out, cap, pos, "}");
}

static bool serialize_tasks(char *out, size_t cap, const struct TaskStorage *storage) {
    size_t pos = 0;
    if (!append(out, cap, &pos, TASK_LIST_OPEN)) return false;
    bool first = true;
    for (size_t i = 0; i < MAX_TASKS; i++) {
        const struct Task *task = task_storage_at(storage, i);
        if (!task) continue;
        if (!first && !append(out, cap, &pos, ",")) return false;
        first = false;
        if (!append_task(out, cap, &pos, task)) return false;
    }
    return append(out, cap, &pos, TASK_LIST_CLOSE);
}

static void list_tasks(int fd, const struct TaskStorage *storage) {
    char out[MAX_JSON] = {0};
    if (!serialize_tasks(out, sizeof(out), storage)) {
        http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    http_json_reply(fd, HTTP_OK, HTTP_REASON_OK, out);
}

static void create_task(int fd, const char *body, struct TaskStorage *storage) {
    char title[MAX_TITLE + 1];
    if (!get_title(body, title)) { http_error_reply(fd, HTTP_BAD_REQUEST); return; }
    const struct Task *task = NULL;
    enum TaskCreateResult result = task_storage_create(storage, title, &task);
    if (result == TASK_CREATE_FULL) {
        http_json_reply(fd, HTTP_INSUFFICIENT_STORAGE, HTTP_REASON_INSUFFICIENT_STORAGE,
                        STORAGE_FULL_JSON);
        return;
    }
    if (result == TASK_CREATE_ID_EXHAUSTED) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
    char out[CREATE_JSON_CAPACITY] = {0};
    size_t pos = 0;
    if (!append_task(out, sizeof(out), &pos, task)) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
    http_json_reply(fd, HTTP_CREATED, HTTP_REASON_CREATED, out);
}

static void delete_task(int fd, const char *path, struct TaskStorage *storage) {
    const char *id_text = path + sizeof(TASK_ID_PREFIX) - 1U;
    if (!*id_text) { http_error_reply(fd, HTTP_NOT_FOUND); return; }
    for (const char *p = id_text; *p; p++) {
        if (!isdigit((unsigned char)*p)) { http_error_reply(fd, HTTP_NOT_FOUND); return; }
    }
    errno = 0;
    char *end = NULL;
    long number = strtol(id_text, &end, ID_DECIMAL_BASE);
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
    FILE *f = fopen(DASHBOARD_PATH, BINARY_READ_MODE);
    if (!f) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
    char html[MAX_PAGE];
    size_t n = fread(html, 1, sizeof(html), f);
    bool bad = ferror(f) || (!feof(f) && n == sizeof(html));
    fclose(f);
    if (bad) { http_error_reply(fd, HTTP_INTERNAL_SERVER_ERROR); return; }
    http_respond(fd, HTTP_OK, HTTP_REASON_OK, HTTP_MIME_HTML, html, n);
}

void router_handle(int fd, const struct Request *req, struct TaskStorage *storage) {
    if (strcmp(req->method, GET_METHOD) == 0) {
        if (strcmp(req->path, ROOT_PATH) == 0) { serve_page(fd); return; }
        if (strcmp(req->path, HEALTH_PATH) == 0) {
            http_json_reply(fd, HTTP_OK, HTTP_REASON_OK, HEALTH_JSON); return;
        }
        if (strcmp(req->path, TASKS_PATH) == 0) { list_tasks(fd, storage); return; }
    }
    if (strcmp(req->method, POST_METHOD) == 0 && strcmp(req->path, TASKS_PATH) == 0) {
        create_task(fd, req->body, storage); return;
    }
    if (strcmp(req->method, DELETE_METHOD) == 0 && strncmp(req->path, TASK_ID_PREFIX, sizeof(TASK_ID_PREFIX) - 1U) == 0) {
        delete_task(fd, req->path, storage); return;
    }
    if (strcmp(req->path, TASKS_PATH) == 0) { http_error_reply(fd, HTTP_METHOD_NOT_ALLOWED); return; }
    http_error_reply(fd, HTTP_NOT_FOUND);
}
