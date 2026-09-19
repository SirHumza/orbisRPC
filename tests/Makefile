CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -I../orbisrpc

TARGET := test_utils

.PHONY: all clean test

all: $(TARGET)

test: $(TARGET)
	./$(TARGET)

$(TARGET): test_utils.c ../orbisrpc/jsonlite.c ../orbisrpc/b64.c ../orbisrpc/sfo.c ../orbisrpc/tmdb_crypto.c
	$(CC) $(CFLAGS) -o $@ test_utils.c ../orbisrpc/jsonlite.c ../orbisrpc/b64.c ../orbisrpc/sfo.c ../orbisrpc/tmdb_crypto.c

clean:
	rm -f $(TARGET)
