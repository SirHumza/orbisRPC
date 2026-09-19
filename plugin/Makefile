# orbisRPC GoldHEN plugin build (plugin/).
# Builds plugin.prx — a GoldHEN plugin that runs the orbisRPC daemon inside the
# game process, so presence auto-starts at every boot with no PC involvement.

DEBUG_FLAGS = -D__FINAL__=1
TYPE = final

OUTPUT_PRX  := orbisrpc_plugin
BUILD_DIR   := build
TARGET      := $(BUILD_DIR)/$(OUTPUT_PRX).prx
TARGET_ELF  := $(BUILD_DIR)/$(OUTPUT_PRX).elf

# Root vars
TOOLCHAIN   := $(or $(OO_PS4_TOOLCHAIN),/Users/mac/PS4Toolchain/OpenOrbis/PS4Toolchain)
GH_SDK      := $(or $(GOLDHEN_SDK),/Users/mac/PS4Toolchain/GoldHEN_Plugins_SDK)
ROOT        := ..
SRCDIR      := .
ORBISRPC    := $(ROOT)/orbisrpc
INTDIR      := $(BUILD_DIR)

# Our daemon sources (payload main.c is NOT part of the plugin; plugin.c is the entry)
DAEMON_SRC  := $(ORBISRPC)/log.c $(ORBISRPC)/cfg.c $(ORBISRPC)/jsonlite.c $(ORBISRPC)/b64.c \
               $(ORBISRPC)/sfo.c \
               $(ORBISRPC)/tls.c $(ORBISRPC)/ws.c $(ORBISRPC)/discord.c $(ORBISRPC)/detect.c \
               $(ORBISRPC)/daemon.c
PLUGIN_SRC  := $(SRCDIR)/plugin.c $(SRCDIR)/compat.c
CFILES      := $(PLUGIN_SRC) $(DAEMON_SRC)
OBJS        := $(patsubst %.c,$(INTDIR)/%.o,$(notdir $(CFILES)))
MBED_SRC    := $(shell find $(ROOT)/third_party/mbedtls/library -name '*.c' ! -name 'net_sockets.c' ! -name 'timing.c' ! -name 'entropy_poll.c' | sort)
MBED_OBJS   := $(patsubst $(ROOT)/third_party/mbedtls/library/%.c,$(INTDIR)/mbed/%.o,$(MBED_SRC))

LIBS := -lSceLibcInternal -lSceNet -lSceNetCtl \
        -lSceSysmodule -lSceUserService -lSceAppInstUtil -lSceAppContent \
        -lGoldHEN_Hook -lkernel

EXTRAFLAGS := $(DEBUG_FLAGS) -std=gnu11 -Wall -Wno-unused \
              -Wno-int-conversion -Wno-incompatible-pointer-types \
              -Wno-address-of-packed-member \
              -DMBEDTLS_NO_PLATFORM_ENTROPY

CFLAGS  := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c $(EXTRAFLAGS) \
           -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include \
           -I$(GH_SDK)/include -I$(GH_SDK) -I$(ORBISRPC) -I$(SRCDIR) \
           -I$(ROOT)/third_party/mbedtls/include
LDFLAGS := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x -e _init --eh-frame-hdr \
           -L$(TOOLCHAIN)/lib -L$(GH_SDK) $(LIBS)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	CC      := $(or $(LLVM_CC),/usr/local/opt/llvm/bin/clang)
	LD      := $(or $(LLD),/Users/mac/lldbuild/build/bin/ld.lld)
	CDIR    := macos
	FSUFFIX := -macos
else
	CC      := clang
	LD      := ld.lld
	CDIR    := linux
	FSUFFIX :=
endif

_unused := $(shell mkdir -p $(INTDIR))

$(INTDIR)/%.o: $(ORBISRPC)/%.c
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/plugin.o: $(SRCDIR)/plugin.c
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/mbed/%.o: $(ROOT)/third_party/mbedtls/library/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $<

$(TARGET): $(OBJS) $(MBED_OBJS)
	$(LD) $(GH_SDK)/build/crtprx.o $(OBJS) $(MBED_OBJS) -o $(TARGET_ELF) $(LDFLAGS)
	OO_PS4_TOOLCHAIN=$(TOOLCHAIN) $(TOOLCHAIN)/bin/$(CDIR)/create-fself$(FSUFFIX) -in=$(TARGET_ELF) -out=$(TARGET_ELF).oelf \
		--lib=$(TARGET) --paid 0x3800000000000011

.PHONY: clean
.DEFAULT_GOAL := all

all: $(TARGET)
	@echo "plugin -> $(TARGET)"

clean:
	rm -rf $(BUILD_DIR)