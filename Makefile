# Package metadata.
TITLE       := PS4RP Discord Rich Presence
VERSION     := 1.00
TITLE_ID    := BREW00052
CONTENT_ID  := IV0000-BREW00052_00-PS4RPRICHPRESS00

# Libraries linked into the ELF. kernel = syscalls for process detection.
LIBS        := -lc -lkernel -lSceNet -lSceSsl -lSceHttp -lSceSysmodule

# Root vars
TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
PROJDIR     := $(shell basename $(CURDIR))
INTDIR      := $(PROJDIR)/x64/Debug

CFILES      := $(wildcard $(PROJDIR)/*.c)
OBJS        := $(patsubst $(PROJDIR)/%.c, $(INTDIR)/%.o, $(CFILES))

CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c \
               -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr \
               -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o

_unused     := $(shell mkdir -p $(INTDIR))

UNAME_S     := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
		CC      := clang
		LD      := ld.lld
		CDIR    := linux
endif
ifeq ($(UNAME_S),Darwin)
		CC      := $(TOOLCHAIN)/bin/macos/clang
		LD      := $(TOOLCHAIN)/bin/macos/ld.lld
		CDIR    := macos
endif

# Default target: raw ELF payload for GoldHEN, plus an installable PKG.
all: payload pkg

payload: $(INTDIR)/ps4rp.elf
	@echo "payload built: $(INTDIR)/ps4rp.elf"

pkg: $(CONTENT_ID).pkg

$(CONTENT_ID).pkg: pkg.gp4
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core pkg_build $< .

pkg.gp4: eboot.bin sce_sys param.sfo icon0.png
	$(TOOLCHAIN)/bin/$(CDIR)/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$^"

sce_sys param.sfo:
	@echo "sce_sys/param.sfo already pre-seeded (see assets)"

eboot.bin: $(INTDIR)/ps4rp.elf
	$(TOOLCHAIN)/bin/$(CDIR)/create-fself -in=$(INTDIR)/ps4rp.elf -out=$(INTDIR)/ps4rp.oelf \
		--eboot "eboot.bin" --paid 0x3800000000000011

$(INTDIR)/%.o: $(PROJDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/ps4rp.elf: $(OBJS)
	$(LD) $(OBJS) -o $@ $(LDFLAGS)

clean:
	rm -f $(CONTENT_ID).pkg pkg.gp4 eboot.bin $(INTDIR)/*.o $(INTDIR)/ps4rp.elf $(INTDIR)/ps4rp.oelf