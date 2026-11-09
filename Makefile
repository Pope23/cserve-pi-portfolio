CC = gcc
CFLAGS = -std=c17 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -D_POSIX_C_SOURCE=200809L
BIN = build/cserve

.PHONY: all test clean debug
all: $(BIN)
$(BIN): src/main.c
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o $@

test: all
	python3 -m unittest discover -s tests -v

debug: CFLAGS += -g3 -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
debug: clean all

clean:
	rm -rf build
