# orbisRPC Setup installer (OpenOrbis app).
# Flow: daemon payload -> evict old -> token -> done.
# Payload Guest reads /data/payloads/ on this console.
TITLE       := orbisRPC Setup
VERSION     := 1.0.0
TITLE_ID    := ORPC00001
CONTENT_ID  := IV0000-ORPC00001_00-ORBISRPCSETUP000

LIBS        := -lc -lkernel -lc++ -lSceMsgDialog -lSceCommonDialog -lSceImeDialog -lSceSysmodule -lSceNet -lSceUserService -lSceSystemService -lScePad

ASSETS      := $(wildcard installer/assets/**/*)
LIBMODULES  := $(wildcard installer/sce_module/*)

TOOLCHAIN   ?= $(OO_PS4_TOOLCHAIN)
ifeq ($(TOOLCHAIN),)
TOOLCHAIN   := /Users/mac/PS4Toolchain/OpenOrbis/PS4Toolchain
endif
PROJDIR     := installer
COMMONDIR   := $(TOOLCHAIN)/samples/_common
INTDIR      := $(PROJDIR)/x64/Debug

CFILES      := $(wildcard $(PROJDIR)/*.c)
CPPFILES    := $(wildcard $(PROJDIR)/*.cpp)
OBJS        := $(patsubst $(PROJDIR)/%.c, $(INTDIR)/%.o, $(CFILES)) $(patsubst $(PROJDIR)/%.cpp, $(INTDIR)/%.o, $(CPPFILES)) $(INTDIR)/jsonlite.o

# llvmshim (no brew): export PATH="$HOME/llvmshim:$PATH" before make.
LLVM        := $(HOME)/llvmshim
CC          := $(LLVM)/clang
CCX         := $(LLVM)/clang++
LD          := $(LLVM)/ld.lld
CDIR        := macos

CFLAGS      := -DINSTALLER_PS4 -DSETUP_VERSION='"$(VERSION)"' --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include -Iorbisrpc -Iinstaller
CXXFLAGS    := $(CFLAGS) -isystem $(TOOLCHAIN)/include/c++/v1
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o -L$(TOOLCHAIN)/lib

_unused     := $(shell mkdir -p $(INTDIR))

all: $(CONTENT_ID).pkg

$(CONTENT_ID).pkg: installer/pkg.gp4
	cd installer && $(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core pkg_build pkg.gp4 . && mv $(CONTENT_ID).pkg ..

installer/pkg.gp4: installer/eboot.bin installer/assets/daemon.elf installer/assets/evict.elf installer/assets/config.json installer/sce_sys/about/right.sprx installer/sce_sys/param.sfo installer/sce_sys/icon0.png installer/sce_sys/pic1.png $(LIBMODULES) $(ASSETS)
	cd installer && $(TOOLCHAIN)/bin/$(CDIR)/create-gp4 -out pkg.gp4 --content-id=$(CONTENT_ID) --files "eboot.bin assets/daemon.elf assets/evict.elf assets/config.json sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png sce_sys/pic1.png sce_module/libSceFios2.prx sce_module/libc.prx $(patsubst installer/%,%,$(ASSETS))"

installer/sce_sys/param.sfo: installer/Makefile
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_new $@
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

installer/eboot.bin: $(OBJS)
	$(LD) $(INTDIR)/*.o -o $(INTDIR)/$(PROJDIR).elf $(LDFLAGS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-fself-macos -in=$(INTDIR)/$(PROJDIR).elf -out=$(INTDIR)/$(PROJDIR).oelf --eboot "installer/eboot.bin" --paid 0x3800000000000011

$(INTDIR)/%.o: $(PROJDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/jsonlite.o: orbisrpc/jsonlite.c
	$(CC) $(CFLAGS) -o $@ $<

# Stage the daemon + evict payloads + config into the app image.
# config.json carries the SET_ME placeholder; a pre-seeded valid token
# (kept out of the repo) makes the installer skip token entry.
installer/assets/daemon.elf: build-sdk/orbisrpc_sdk.elf
	cp $< $@
installer/assets/evict.elf: build-sdk/evict.elf
	cp $< $@
installer/assets/config.json: installer/config.json
	cp $< $@

$(INTDIR)/%.o: $(PROJDIR)/%.cpp
	$(CCX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf $(INTDIR) installer/eboot.bin installer/pkg.gp4 $(CONTENT_ID).pkg installer/sce_sys/param.sfo installer/assets/daemon.elf installer/assets/evict.elf installer/assets/config.json
