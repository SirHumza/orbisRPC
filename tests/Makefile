CC ?= cc
CFLAGS += -std=c11 -Wall -Wextra -Werror -DHEALTH_TESTABLE -I../orbisrpc -I../third_party/mbedtls/include -I$(SQLITE_DIR)

TARGET := test_utils
OBJDIR := .objs
ASAN_OBJDIR := .objs-asan
MBEDTLS_DIR := ../third_party/mbedtls/library
# Same exclusion set as scripts/build.sh (POSIX-only modules).
MBEDTLS_SRCS := $(filter-out $(MBEDTLS_DIR)/net_sockets.c $(MBEDTLS_DIR)/timing.c $(MBEDTLS_DIR)/entropy_poll.c,$(wildcard $(MBEDTLS_DIR)/*.c))

ORBIS_SRCS := ../orbisrpc/jsonlite.c ../orbisrpc/b64.c ../orbisrpc/sfo.c ../orbisrpc/tmdb_crypto.c ../orbisrpc/updater_util.c ../orbisrpc/art.c ../orbisrpc/log.c ../orbisrpc/health.c ../orbisrpc/manifest.c ../orbisrpc/compat.c ../orbisrpc/cfg.c ../orbisrpc/appdb.c ../orbisrpc/discord.c
INST_SRCS := ../installer/icfg.c ../installer/nettest.c ../installer/send.c
# SQLite amalgamation: -O0 for host iteration speed (payload uses -O2).
SQLITE_DIR := ../third_party/sqlite/sqlite-amalgamation-3510100
SQLITE_FLAGS := -O0 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_DEPRECATED -I$(SQLITE_DIR)

ORBIS_OBJS := $(patsubst ../orbisrpc/%.c,$(OBJDIR)/orbis_%.o,$(ORBIS_SRCS))
INST_OBJS := $(patsubst ../installer/%.c,$(OBJDIR)/inst_%.o,$(INST_SRCS))
MBEDTLS_OBJS := $(patsubst $(MBEDTLS_DIR)/%.c,$(OBJDIR)/mbed_%.o,$(MBEDTLS_SRCS))
TEST_OBJS := $(OBJDIR)/test_utils.o $(ORBIS_OBJS) $(INST_OBJS) $(MBEDTLS_OBJS) $(OBJDIR)/sqlite3.o

ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_ORBIS_OBJS := $(patsubst ../orbisrpc/%.c,$(ASAN_OBJDIR)/orbis_%.o,$(ORBIS_SRCS))
ASAN_INST_OBJS := $(patsubst ../installer/%.c,$(ASAN_OBJDIR)/inst_%.o,$(INST_SRCS))
ASAN_MBEDTLS_OBJS := $(patsubst $(MBEDTLS_DIR)/%.c,$(ASAN_OBJDIR)/mbed_%.o,$(MBEDTLS_SRCS))
ASAN_OBJS := $(ASAN_OBJDIR)/test_utils.o $(ASAN_ORBIS_OBJS) $(ASAN_INST_OBJS) $(ASAN_MBEDTLS_OBJS) $(ASAN_OBJDIR)/sqlite3.o

.PHONY: all clean test asan

all: $(TARGET)

test: $(TARGET)
	./$(TARGET)

$(TARGET): $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) -lm

asan: $(ASAN_OBJS)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o test_utils_asan $(ASAN_OBJS) -lm
	./test_utils_asan

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(ASAN_OBJDIR):
	mkdir -p $(ASAN_OBJDIR)

$(OBJDIR)/test_utils.o: test_utils.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ test_utils.c

$(OBJDIR)/orbis_%.o: ../orbisrpc/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/inst_%.o: ../installer/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -I../installer -c -o $@ $<

$(OBJDIR)/mbed_%.o: $(MBEDTLS_DIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

$(OBJDIR)/sqlite3.o: $(SQLITE_DIR)/sqlite3.c | $(OBJDIR)
	$(CC) $(SQLITE_FLAGS) -c -o $@ $<

$(ASAN_OBJDIR)/sqlite3.o: $(SQLITE_DIR)/sqlite3.c | $(ASAN_OBJDIR)
	$(CC) $(SQLITE_FLAGS) $(ASAN_FLAGS) -c -o $@ $<

$(ASAN_OBJDIR)/test_utils.o: test_utils.c | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -c -o $@ test_utils.c

$(ASAN_OBJDIR)/orbis_%.o: ../orbisrpc/%.c | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -c -o $@ $<

$(ASAN_OBJDIR)/inst_%.o: ../installer/%.c | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -I../installer -c -o $@ $<

$(ASAN_OBJDIR)/mbed_%.o: $(MBEDTLS_DIR)/%.c | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -Wno-unused-parameter -c -o $@ $<

clean:
	rm -rf $(TARGET) test_utils_asan $(OBJDIR) $(ASAN_OBJDIR) *.o
