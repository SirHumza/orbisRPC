CC ?= cc
# Host tests target POSIX systems (glibc under -std=c11 hides fileno/strdup/etc;
# _DEFAULT_SOURCE re-enables usleep and friends).
CFLAGS += -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Werror -DHEALTH_TESTABLE -I../orbisrpc -I../third_party/mbedtls/include -I$(SQLITE_DIR)

HDRS := $(wildcard ../orbisrpc/*.h)
SAFETY := gateway_safety auth_rotation installer_safety
SAFETY_BINS = $(addprefix $(OBJDIR)/,$(SAFETY))
ASAN_SAFETY_BINS = $(addprefix $(ASAN_OBJDIR)/,$(SAFETY))

TARGET := test_utils
OBJDIR := .objs
ASAN_OBJDIR := .objs-asan
MBEDTLS_DIR := ../third_party/mbedtls/library
# POSIX-only mbedTLS modules are excluded from the payload-compatible host link.
MBEDTLS_SRCS := $(filter-out $(MBEDTLS_DIR)/net_sockets.c $(MBEDTLS_DIR)/timing.c $(MBEDTLS_DIR)/entropy_poll.c,$(wildcard $(MBEDTLS_DIR)/*.c))

ORBIS_SRCS := ../orbisrpc/jsonlite.c ../orbisrpc/b64.c ../orbisrpc/sfo.c ../orbisrpc/tmdb_crypto.c ../orbisrpc/updater_util.c ../orbisrpc/art.c ../orbisrpc/log.c ../orbisrpc/health.c ../orbisrpc/compat.c ../orbisrpc/cfg.c ../orbisrpc/appdb.c ../orbisrpc/discord.c ../orbisrpc/procwalk.c ../orbisrpc/bigapp.c ../orbisrpc/pkgzone.c ../orbisrpc/gamecache.c ../orbisrpc/fw.c ../orbisrpc/notify.c ../orbisrpc/retro.c ../orbisrpc/custom.c ../orbisrpc/jsonstr.c ../orbisrpc/timesync.c ../orbisrpc/supervise.c
INST_SRCS := ../installer/icfg.c ../installer/jsonlite.c
# SQLite amalgamation: -O0 for host iteration speed (payload uses -O2).
SQLITE_DIR := ../third_party/sqlite/sqlite-amalgamation-3510100
SQLITE_FLAGS := -O2 -I$(SQLITE_DIR) # clean default compile; OMIT/THREADSAFE=0 overrides break sqlite3_open_v2 on the ubuntu runner

ORBIS_OBJS := $(patsubst ../orbisrpc/%.c,$(OBJDIR)/orbis_%.o,$(ORBIS_SRCS))
INST_OBJS := $(patsubst ../installer/%.c,$(OBJDIR)/inst_%.o,$(INST_SRCS))
MBEDTLS_OBJS := $(patsubst $(MBEDTLS_DIR)/%.c,$(OBJDIR)/mbed_%.o,$(MBEDTLS_SRCS))
TEST_OBJS := $(OBJDIR)/test_utils.o $(ORBIS_OBJS) $(MBEDTLS_OBJS) $(OBJDIR)/sqlite3.o

ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_ORBIS_OBJS := $(patsubst ../orbisrpc/%.c,$(ASAN_OBJDIR)/orbis_%.o,$(ORBIS_SRCS))
ASAN_INST_OBJS := $(patsubst ../installer/%.c,$(ASAN_OBJDIR)/inst_%.o,$(INST_SRCS))
ASAN_MBEDTLS_OBJS := $(patsubst $(MBEDTLS_DIR)/%.c,$(ASAN_OBJDIR)/mbed_%.o,$(MBEDTLS_SRCS))
ASAN_OBJS := $(ASAN_OBJDIR)/test_utils.o $(ASAN_ORBIS_OBJS) $(ASAN_MBEDTLS_OBJS) $(ASAN_OBJDIR)/sqlite3.o

.PHONY: all clean test asan

all: $(TARGET)

test: $(TARGET) test_icfg $(SAFETY_BINS)
	./$(TARGET)
	./test_icfg
	@for t in $(SAFETY_BINS); do ./$$t || exit $$?; done

$(TARGET): $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS) -lm

asan: $(ASAN_OBJS) test_icfg_asan $(ASAN_SAFETY_BINS)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o test_utils_asan $(ASAN_OBJS) -lm
	./test_utils_asan
	./test_icfg_asan
	@for t in $(ASAN_SAFETY_BINS); do ./$$t || exit $$?; done

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(ASAN_OBJDIR):
	mkdir -p $(ASAN_OBJDIR)

# test_utils.c #includes ws_skip.c and ws_env.c directly (they are the pure
# parts of ws.c, which cannot be linked on the host because it owns a
# tls_ctx_t). They must be listed here or make treats the object as up to date
# after they change -- which silently tested stale code.
$(OBJDIR)/test_utils.o: test_utils.c $(HDRS) ../orbisrpc/ws_skip.c ../orbisrpc/ws_env.c ../orbisrpc/ws_env.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ test_utils.c

$(OBJDIR)/orbis_%.o: ../orbisrpc/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/orbis_timesync.o: ../orbisrpc/timesync.c | $(OBJDIR)
	$(CC) $(CFLAGS) -DORBISRPC_SDK_PAYLOAD -c -o $@ $<

$(OBJDIR)/inst_%.o: ../installer/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -I../installer -c -o $@ $<

$(OBJDIR)/mbed_%.o: $(MBEDTLS_DIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

$(OBJDIR)/sqlite3.o: $(SQLITE_DIR)/sqlite3.c | $(OBJDIR)
	$(CC) $(SQLITE_FLAGS) -c -o $@ $<

$(ASAN_OBJDIR)/sqlite3.o: $(SQLITE_DIR)/sqlite3.c | $(ASAN_OBJDIR)
	$(CC) $(SQLITE_FLAGS) $(ASAN_FLAGS) -c -o $@ $<

$(ASAN_OBJDIR)/test_utils.o: test_utils.c $(HDRS) ../orbisrpc/ws_skip.c ../orbisrpc/ws_env.c ../orbisrpc/ws_env.h | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -c -o $@ test_utils.c

$(ASAN_OBJDIR)/orbis_%.o: ../orbisrpc/%.c $(HDRS) | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -c -o $@ $<

$(ASAN_OBJDIR)/orbis_timesync.o: ../orbisrpc/timesync.c | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -DORBISRPC_SDK_PAYLOAD -c -o $@ $<

$(ASAN_OBJDIR)/inst_%.o: ../installer/%.c | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -I../installer -c -o $@ $<

$(ASAN_OBJDIR)/mbed_%.o: $(MBEDTLS_DIR)/%.c | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -Wno-unused-parameter -c -o $@ $<

$(OBJDIR)/test_icfg.o: test_icfg.c | $(OBJDIR)
	$(CC) $(CFLAGS) -I../installer -c -o $@ test_icfg.c

$(ASAN_OBJDIR)/test_icfg.o: test_icfg.c | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -I../installer -c -o $@ test_icfg.c

test_icfg: $(OBJDIR)/test_icfg.o $(INST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJDIR)/test_icfg.o $(INST_OBJS) -lm

test_icfg_asan: $(ASAN_OBJDIR)/test_icfg.o $(ASAN_INST_OBJS)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $@ $(ASAN_OBJDIR)/test_icfg.o $(ASAN_INST_OBJS) -lm

clean:
	rm -rf $(TARGET) test_utils_asan test_icfg test_icfg_asan $(OBJDIR) $(ASAN_OBJDIR) *.o

# Standalone protocol/persistence and installer safety regressions.
$(OBJDIR)/gateway_safety: test_gateway_safety.c ../orbisrpc/discord.c ../orbisrpc/ws_env.c ../orbisrpc/ws_frag.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $< ../orbisrpc/jsonlite.c ../orbisrpc/b64.c
$(ASAN_OBJDIR)/gateway_safety: test_gateway_safety.c ../orbisrpc/discord.c ../orbisrpc/ws_env.c ../orbisrpc/ws_frag.c $(HDRS) | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $@ $< ../orbisrpc/jsonlite.c ../orbisrpc/b64.c
$(OBJDIR)/auth_rotation: test_auth_rotation.c ../orbisrpc/auth_rotation.h | $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $<
$(ASAN_OBJDIR)/auth_rotation: test_auth_rotation.c ../orbisrpc/auth_rotation.h | $(ASAN_OBJDIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $@ $<
SETTINGS_TEST_FLAGS := -std=c++11 -Wall -Wextra -Werror -I../installer -DSETTINGS_PATH='"./installer-safety-fixture/config.json"' -DSETTINGS_DIR='"./installer-safety-fixture"'
$(OBJDIR)/installer_safety: test_installer_safety.cpp ../installer/settings.cpp ../installer/settings.h $(OBJDIR)/inst_jsonlite.o | $(OBJDIR)
	$(CXX) $(SETTINGS_TEST_FLAGS) -o $@ test_installer_safety.cpp ../installer/settings.cpp $(OBJDIR)/inst_jsonlite.o
$(ASAN_OBJDIR)/installer_safety: test_installer_safety.cpp ../installer/settings.cpp ../installer/settings.h $(ASAN_OBJDIR)/inst_jsonlite.o | $(ASAN_OBJDIR)
	$(CXX) $(SETTINGS_TEST_FLAGS) $(ASAN_FLAGS) -o $@ test_installer_safety.cpp ../installer/settings.cpp $(ASAN_OBJDIR)/inst_jsonlite.o
