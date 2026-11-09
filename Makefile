CC = gcc
CFLAGS = -std=c17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -D_POSIX_C_SOURCE=200809L
BIN = build/cserve
SOURCES = src/main.c src/http_response.c src/task_storage.c src/http_parser.c src/router.c src/server.c
HEADERS = src/http_response.h src/task_storage.h src/http_parser.h src/router.h src/server.h
RESPONSE_TEST = build/test_http_response
STORAGE_TEST = build/test_task_storage
PARSER_TEST = build/test_http_parser
SERVER_TEST = build/test_server
# glibc fortification redirects snprintf/recv past the fault-injection wrappers.
WRAP_TEST_CFLAGS = -U_FORTIFY_SOURCE
RESPONSE_TEST_FLAGS = $(WRAP_TEST_CFLAGS) -Wl,--wrap=send -Wl,--wrap=snprintf
PARSER_TEST_FLAGS = $(WRAP_TEST_CFLAGS) -Wl,--wrap=recv
SERVER_TEST_FLAGS = -Wl,--wrap=socket -Wl,--wrap=setsockopt -Wl,--wrap=bind -Wl,--wrap=listen -Wl,--wrap=accept -Wl,--wrap=close

.PHONY: all test clean debug
all: $(BIN)
$(BIN): $(SOURCES) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SOURCES) -o $@

$(RESPONSE_TEST): tests/test_http_response.c src/http_response.c $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc tests/test_http_response.c src/http_response.c $(RESPONSE_TEST_FLAGS) -o $@

$(STORAGE_TEST): tests/test_task_storage.c src/task_storage.c src/task_storage.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc tests/test_task_storage.c src/task_storage.c -o $@

$(PARSER_TEST): tests/test_http_parser.c src/http_parser.c src/http_parser.h src/http_response.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc tests/test_http_parser.c src/http_parser.c $(PARSER_TEST_FLAGS) -o $@

$(SERVER_TEST): tests/test_server.c src/server.c src/task_storage.c $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc tests/test_server.c src/server.c src/task_storage.c $(SERVER_TEST_FLAGS) -o $@

test: all $(RESPONSE_TEST) $(STORAGE_TEST) $(PARSER_TEST) $(SERVER_TEST)
	./$(RESPONSE_TEST)
	./$(STORAGE_TEST)
	./$(PARSER_TEST)
	./$(SERVER_TEST)
	python3 -m unittest discover -s tests -v

debug: CFLAGS += -g3 -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
debug: clean all

clean:
	rm -rf build
