CC = gcc
CFLAGS = -std=c17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -D_POSIX_C_SOURCE=200809L
BIN = build/cserve
SOURCES = src/main.c src/http_response.c src/task_storage.c
HEADERS = src/http_response.h src/task_storage.h
RESPONSE_TEST = build/test_http_response
STORAGE_TEST = build/test_task_storage
RESPONSE_TEST_FLAGS = -Wl,--wrap=send -Wl,--wrap=snprintf

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

test: all $(RESPONSE_TEST) $(STORAGE_TEST)
	./$(RESPONSE_TEST)
	./$(STORAGE_TEST)
	python3 -m unittest discover -s tests -v

debug: CFLAGS += -g3 -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
debug: clean all

clean:
	rm -rf build
