# ---------------------------------------------------------------------------
#  orbisRPC Setup -- PS4 homebrew
#
#    make                 build the installer
#    make payloads        stage the payload ELFs from build-sdk/
#    make clean           remove build artefacts
#
#  Sources:
#    installer/   everything -- SDL2 menu, native dialogs, payload placement,
#                plus the renderer, pad input, system queries and HTTP client
# ---------------------------------------------------------------------------

# Which app to build. There is only one now; the second app this project once
# carried (a separate "Get Token" homebrew) went along with its directory.
# The variable stays because scripts and muscle memory still pass it, and an
# unexpected APP= is still rejected below rather than silently ignored.
APP        ?= installer

ifeq ($(APP),installer)

	SOURCEDIR := installer
	TARGET    := orbisrpc_setup
	# No SHARED list any more. The renderer, pad input, system queries, format
	# helpers, variable-font selection and the HTTP client used to live in
	# ps4util/ and were compiled in from there by name. They are ordinary sources
	# in installer/ now, so the $(wildcard $(SOURCEDIR)/*.cpp) above already
	# picks them up and the shared-object rule further down has nothing to do.
	SHARED    :=
	EXTRA_INCLUDES :=

	TITLE       := orbisRPC Setup
	# Bump this on every build you intend to install. The PS4 package manager
	# treats an identical TITLE_ID + VERSION as already-installed and will not
	# replace the files, so rebuilding with an unchanged version silently leaves
	# the old eboot running -- which looks exactly like "my fix did nothing".
	# It also feeds SETUP_VERSION, the fallback for the installed-version marker.
	#
	# Note this is the SETUP app's own version and is unrelated to the payload's.
	# The payload version is whatever tag the release carries; the updater
	# compares that against /data/orbisRPC/version, not against this.
	VERSION     := 1.0.0
	TITLE_ID    := ORPC00001
	CONTENT_ID  := IV0000-ORPC00001_00-ORBISRPCSETUP000

	#   -lSceMsgDialog -lSceCommonDialog   the dialogs the wizard is built from
	#   -lSceImeDialog                      the on-screen keyboard (ui_input)
	#   -lSDL2                              the menu / Settings surface
	#   -lSceFreeType                       on-screen text for the menu
	#   -lSceNet -lSceSsl -lSceHttp         the update check in the install flow
	# SDL2_image is NOT used: this platform's build has no image decoders
	# compiled in (IMG_Init reports "PNG images are not supported"), so artwork
	# is shipped as 32-bit BMP and read by installer/bitmap.cpp instead.
	#   -lSceNet -lSceUserService           network + logged-in user
	#   -lSceSystemService                  system service calls
	#   -lScePad                            progress dialogs + menu input
	# libSceAudioOut is pulled in only because libSDL2.a references it.
	LIBS        := -lc -lkernel -lc++ -lSceMsgDialog -lSceCommonDialog \
	               -lSceImeDialog -lSceSysmodule -lSceNet -lSceSsl -lSceHttp \
	               -lSceUserService -lSceSystemService -lScePad -lSDL2 \
	               -lSceFreeType -lSceVideoOut -lSceAudioOut

	# SETUP_VERSION is reported in the about text and used as the fallback for
	# the installed-version marker.
	#
	# INSTALLER_PS4 used to sit here too, selecting the PS4 code paths in
	# send.c. That file is gone, along with every other consumer, so the define
	# was compiling into 30 objects and branching in none of them.
	EXTRA_DEFS  := -DSETUP_VERSION='"$(VERSION)"'
	SYS_FILES   := sce_sys/icon0.png sce_sys/pic1.png
	SYS_PATHS   := $(addprefix $(SOURCEDIR)/,$(SYS_FILES))

else
$(error Unknown APP '$(APP)'. The only app is APP=installer)
endif

# ---------------------------------------------------------------------------

# PkgTool.Core (LibOrbisPkg) ships as a .NET Core 3.0 bundle whose crypto layer
# probes for OpenSSL 1.x, and it unpacks itself into a cache directory on first
# run. Ubuntu 24.04 has neither, so both are handled here rather than relying on
# an interactive shell having them set.
DOTNET_BUNDLE_EXTRACT_BASE_DIR ?= $(HOME)/.cache/dotnet-bundle
export DOTNET_BUNDLE_EXTRACT_BASE_DIR
DOTNET_SYSTEM_GLOBALIZATION_INVARIANT ?= 1
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT
_unused_bundle := $(shell mkdir -p $(DOTNET_BUNDLE_EXTRACT_BASE_DIR))

# OpenSSL 1.1 is unpacked into a private prefix by
# scripts/setup-pkgtool-openssl.sh. Putting it on LD_LIBRARY_PATH only when it
# exists keeps the system OpenSSL 3 untouched.
OPENSSL11_DIR    ?= $(HOME)/.local/opt/libssl1.1
OPENSSL11_LIBDIR := $(OPENSSL11_DIR)/usr/lib/x86_64-linux-gnu
ifneq ($(wildcard $(OPENSSL11_LIBDIR)/libssl.so.1.1),)
	export LD_LIBRARY_PATH := $(OPENSSL11_LIBDIR)$(if $(LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))
endif

# Root vars ------------------------------------------------------------------
TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
INTDIR      := $(SOURCEDIR)/x64/Debug

# Scoped to the goals that actually compile the app. `make payload` only shells
# out to scripts/build_sdk.sh and needs the PS4 SDK, not this one, so erroring
# at parse time on an unset toolchain would take `make payload` down with it.
# A default goal (bare `make`) counts as installer-side.
INSTALLER_GOALS := all installer pkg elf gfx rebuild clean
ifneq ($(filter $(INSTALLER_GOALS),$(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)),)
ifeq ($(strip $(TOOLCHAIN)),)
$(error OO_PS4_TOOLCHAIN is not set. Add this to your ~/.bashrc:  \
	export OO_PS4_TOOLCHAIN="$HOME/OpenOrbis-PS4-Toolchain")
endif
endif

CFILES      := $(wildcard $(SOURCEDIR)/*.c)
CPPFILES    := $(wildcard $(SOURCEDIR)/*.cpp)
OBJS        := $(patsubst $(SOURCEDIR)/%.c, $(INTDIR)/%.o, $(CFILES)) \
               $(patsubst $(SOURCEDIR)/%.cpp, $(INTDIR)/%.o, $(CPPFILES))

# Sources pulled in from another app's directory.
SHARED_OBJS := $(patsubst %.cpp, $(INTDIR)/shared/%.o, $(SHARED))
OBJS        += $(SHARED_OBJS)

CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c \
               $(EXTRA_DEFS) $(EXTRA_INCLUDES) \
               -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
CXXFLAGS    := $(CFLAGS) -isystem $(TOOLCHAIN)/include/c++/v1
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr \
               -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o

_unused     := $(shell mkdir -p $(INTDIR))

CC          := clang
CCX         := clang++
LD          := ld.lld
CDIR        := linux

# Assets: .gitkeep files keep empty folders in git but must not ship.
#
# Recursively expanded (`=` not `:=`) on purpose. A simply-expanded assignment
# evaluates the wildcard while the makefile is being read, which is before any
# rule has run -- so anything a rule generates, such as the artwork, would be
# missing from the list. That is not hypothetical: it is why an earlier build
# produced a pkg containing no images at all while pkg.gp4 itself looked fine.
# The recursive form defers the wildcard to the point of use, by which time the
# gfx rules below have already run.
# find -type f, not $(wildcard). Two reasons, both learned the hard way:
#
#   1. In $(wildcard), `**` matches exactly one directory level -- it is not a
#      recursive glob. So `assets/**/*` silently drops every file sitting
#      directly in assets/, which is where the payload ELF lives. orbisrpc.elf
#      sat there un-packaged while the $(warning) below reported it found, and
#      the build succeeded. The install then failed on the console.
#   2. Adding `assets/*` to catch those files also matches the *directories*
#      (audio, fonts, images, misc, videos). create-gp4 takes them as files and
#      PkgTool.Core aborts: "Could not find file '.../assets/audio'".
#
# -type f excludes directories, and the recursion is not limited, so one pattern
# covers both cases.
#
# Recursively expanded (`=` not `:=`) on purpose: a simply-expanded assignment
# runs the shell while the makefile is being read, before any rule has run, so
# anything a rule generates -- the artwork -- would be missing from the list.
# That is not hypothetical: it is why an earlier build produced a pkg containing
# no images at all while pkg.gp4 itself looked fine. The recursive form defers
# evaluation to the point of use, by which time the gfx rules have run.
ASSET_FILES = $(filter-out %.gitkeep,$(shell find $(SOURCEDIR)/assets -type f))

# create-gp4 emits a fixed set of <dir> entries under assets/ -- audio, fonts,
# images, misc, videos -- whatever files it is handed. PkgTool.Core aborts with
# "Sequence contains no elements" if one of those directories is missing on disk,
# and it also aborts if a file sits in any *other* subdirectory, because the
# directory is then not in the tree it built. So:
#
#   - all five directories must exist, kept by .gitkeep markers (filtered out of
#     ASSET_FILES above, so they package as empty)
#   - artwork must live in one of them. It ships in images/.
ASSET_DIRS  := audio images misc videos
ASSET_MK    := $(foreach d,$(ASSET_DIRS),$(SOURCEDIR)/assets/$(d)/.gitkeep)
LIBMODULES  := $(wildcard $(SOURCEDIR)/sce_module/*)

ifeq ($(APP),installer)
PAYLOADS    := $(wildcard $(SOURCEDIR)/assets/*.elf)
LOGO        := $(wildcard $(SOURCEDIR)/assets/logo.png)

# Read out of paths.h rather than written again here, so the packaged name and
# the name the installer copies to cannot drift apart.
#
# This was being sed'd out of installer.c, which does not define it -- paths.h
# does. So PAYLOAD_NAME silently expanded to empty: REQUIRED_PKG_FILES was only
# ever "logo.png", the payload was never actually checked for, and anything
# built from $(PAYLOAD_NAME) (the staging path) resolved to a bare directory.
# Hence the hard error below rather than a warning -- an empty PAYLOAD_NAME is
# never a valid state, and it fails quietly in ways that only show up on the
# console as "This install is missing its payload."
PAYLOAD_NAME := $(shell sed -n 's/^#define[[:space:]]\+PAYLOAD_NAME[[:space:]]\+"\([^"]*\)".*$$/\1/p' $(SOURCEDIR)/paths.h)
ifeq ($(strip $(PAYLOAD_NAME)),)
$(error PAYLOAD_NAME is empty -- expected '#define PAYLOAD_NAME "orbisRPC.elf"' in $(SOURCEDIR)/paths.h)
endif

# Release outputs ---------------------------------------------------------------
# `make all` leaves both artifacts here, version-stamped so a second build does
# not overwrite the first. OUTDIR is NOT in .gitignore; add `output/` to it.
OUTDIR      := output
PAYLOAD_SRC := build-sdk/orbisrpc_sdk.elf
# Staged under PAYLOAD_NAME, which is the same string pkg.gp4 lists and
# paths.h copies to /data/payloads/. Spelling it out here rather than as a
# literal in a recipe is what keeps those three from drifting.
STAGED_PAYLOAD := $(SOURCEDIR)/assets/$(PAYLOAD_NAME)
PAYLOAD_OUT := $(OUTDIR)/orbisRPC-$(VERSION).elf
PKG_OUT     := $(OUTDIR)/orbisRPC-$(VERSION).pkg
# The check that actually matters is not "is the ELF on disk" but "does the
# package contain it". A $(warning) about the former is easy to read as the
# latter and be wrong: the ELF was present and reported found for days while
# being absent from every pkg, because ASSET_FILES did not match it.
#
# A missing payload is only discoverable on the console, at install time, as
# "This install is missing its payload." So the pkg.gp4 rule verifies it, below.
#
# The payload icon sits on the same footing for a quieter reason: without it the
# install still works and the payload still runs, but the launcher draws a blank
# row. Nothing in the flow fails and nothing is written to the console log, so
# this check is the only place it can be caught.
#
# Named here rather than taken from $(PAYLOADS): a wildcard is empty precisely
# when the file is missing, so looping over it checks nothing at exactly the
# moment there is something to check. That is the blind spot this guard was
# written to close, reproduced by the obvious way of writing it.
REQUIRED_PKG_FILES := $(PAYLOAD_NAME) logo.png
else
$(warning No payload ELFs in $(SOURCEDIR)/assets -- the app will build and run, \
but the install step has nothing to send. Copy orbisRPC.elf \
from build-sdk/, or run `make APP=installer payloads`.)
endif

.PHONY: all payload stage-payload payloads installer pkg clean rebuild elf gfx

# `all` runs payload then installer, and that order is the whole point: the
# installer must package the payload that was just built, not whatever stale
# ELF happens to be lying in installer/assets. Without .NOTPARALLEL a `make -j`
# would let the two interleave and silently package the old one.
.NOTPARALLEL:

# Full chain: build the payload, stage it into the installer, package, and
# leave both artifacts in $(OUTDIR).
all: payload installer

# Build the daemon payload ELF only. Does not need the OpenOrbis toolchain --
# only the PS4 SDK that scripts/build_sdk.sh wants (PS4_PAYLOAD_SDK, and
# PS4_SDK_SRC for the 13.52 firmware gate).
payload:
	./scripts/build_sdk.sh
	@mkdir -p $(OUTDIR)
	cp -f $(PAYLOAD_SRC) $(PAYLOAD_OUT)
	@echo "==> $(PAYLOAD_OUT)"

# Copy the built payload into the package's assets. Split out because `make
# installer` needs it but must not rebuild the payload, and because the two
# names (build-sdk/orbisrpc_sdk.elf, assets/orbisRPC.elf) genuinely differ.
stage-payload:
	@test -f $(PAYLOAD_SRC) || { \
		echo "ERROR: $(PAYLOAD_SRC) not found -- run 'make payload' first."; \
		exit 1; }
	@# Any OTHER elf in assets/ is stale. pkg.gp4 is a checked-in file, so
	@# once a wrong-named elf has been listed there it keeps shipping forever
	@# while stage-payload only ever writes PAYLOAD_NAME -- the pkg carried a
	@# 40-byte-older daemon beside the current one and nothing complained.
	@# The console cannot see this: the installer opens assets/$(PAYLOAD_NAME)
	@# specifically, so the extra file just rides along. Catch it here.
	@for f in $(SOURCEDIR)/assets/*.elf; do \
		[ -e "$$f" ] || continue; \
		case "$$f" in \
			*$(PAYLOAD_NAME)) continue ;; \
			*) echo "ERROR: stale payload $$f (only $(PAYLOAD_NAME) belongs in assets/)"; \
			   echo "       rm it, and remove its <file> entry from $(SOURCEDIR)/pkg.gp4"; \
			   exit 1 ;; \
		esac; \
	done
	cp -f $(PAYLOAD_SRC) $(STAGED_PAYLOAD)
	@echo "==> staged $(STAGED_PAYLOAD)"

# Old name for stage-payload, kept because the warning above points at it.
payloads: stage-payload

# Build the PKG and copy it to $(OUTDIR). Does not build the payload.
installer: pkg
	@mkdir -p $(OUTDIR)
	cp -f $(CONTENT_ID).pkg $(PKG_OUT)
	@echo "==> $(PKG_OUT)"

# Stage the payload, then package. The stage step is what stops the pkg from
# shipping without a payload when installer/assets/orbisRPC.elf was never built.
pkg: stage-payload $(CONTENT_ID).pkg

# Compile + link only, no packaging. Fastest loop while editing.
elf: $(SOURCEDIR)/eboot.bin

# Artwork only, for when a PNG changes and you want to eyeball it.
gfx: $(GFX)

$(CONTENT_ID).pkg: $(SOURCEDIR)/pkg.gp4
	cd $(SOURCEDIR) && $(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core pkg_build pkg.gp4 . && mv $(CONTENT_ID).pkg ../

$(ASSET_MK):
	mkdir -p $(@D) && touch $@

# Pre-scale the artwork to its draw size here rather than on the console: the
# assets are BMP because SDL2_image has no decoders on this platform, and baking
# the size in means the frame path only ever does 1:1 blits.
#
# Every source PNG needs an entry. A source with no entry is an error rather than
# being skipped, because artwork that exists but is not packaged is exactly the
# silent failure this log exists to catch.
GFX_SIZE_select   := 34x34
GFX_SIZE_back     := 34x34
GFX_SIZE_navigate := 40x40
GFX_SIZE_logo     := 200x200

# Settings preview panel artwork.
GFX_SIZE_youtube                  := 80x80
GFX_SIZE_naruto_storm_connections := 80x80
GFX_SIZE_images_web_browser       := 80x80
GFX_SIZE_ps_logo_big              := 80x80

# ps_logo_small is the little badge that overlaps a preview card's icon, not a
# card icon in its own right.
GFX_SIZE_ps_logo_small            := 30x30

# Shoulder buttons, drawn inside the preview tab bar's Previous / Next pills.
# Shoulder button art. Icon only in the preview tab bar, sized to match the
# footer's 34px button art.
GFX_SIZE_L1                       := 34x34
GFX_SIZE_R1                       := 34x34
# Already 28x28 in the source, so this is a no-op target that keeps the shape of
# the table consistent. 24px of glyph inside 2px of padding on each side.
GFX_SIZE_wrench                   := 28x28

GFX_NAMES  := select back navigate logo L1 R1 \
              youtube naruto_storm_connections images_web_browser \
              ps_logo_big ps_logo_small wrench

# wrench is listed here for a reason worth recording. It originally had no entry
# in this table: assets/images/wrench.bmp was hand-converted and dropped straight
# into the output directory. `make clean` removes assets/images/*.bmp, so the
# next clean build deleted it and nothing could regenerate it -- the glyph
# vanished from the FIXES row and nothing in the build complained. Anything in
# assets/images/ that is not produced from installer/artwork/ by this rule is
# deleted by clean and never comes back.

# The artwork step only ever applied to the installer; it is now the only app,
# so the guard that used to scope it is gone.
ifeq ($(APP),installer)

# foreach, not "$(GFX_NAMES).png". Appending a suffix to a multi-word variable
# binds it only to the final word, so that form searches for a single file called
# "select back navigate logo.png" and quietly matches nothing.
GFX_SRC    := $(foreach n,$(GFX_NAMES),$(wildcard $(SOURCEDIR)/artwork/$(n).png))
GFX_FOUND  := $(notdir $(basename $(GFX_SRC)))

# filter-out, not filter: this is the set of names that were NOT found. Using
# filter here inverted the test and reported every present file as missing.
GFX_MISSING := $(filter-out $(GFX_FOUND),$(GFX_NAMES))
ifneq ($(strip $(GFX_MISSING)),)
$(error Missing artwork in $(SOURCEDIR)/artwork: $(GFX_MISSING).png)
endif

GFX        := $(patsubst $(SOURCEDIR)/artwork/%.png,$(SOURCEDIR)/assets/images/%.bmp,$(GFX_SRC))

endif

$(SOURCEDIR)/assets/images/%.bmp: $(SOURCEDIR)/artwork/%.png scripts/gfx2bmp.py
	python3 scripts/gfx2bmp.py --out $(SOURCEDIR)/assets/images \
		$(SOURCEDIR)/artwork/$*.png=$(GFX_SIZE_$*)

$(SOURCEDIR)/pkg.gp4: $(ASSET_MK) $(GFX) $(SOURCEDIR)/eboot.bin \
                      $(SOURCEDIR)/sce_sys/param.sfo \
                      $(SOURCEDIR)/sce_sys/about/right.sprx \
                      $(SYS_PATHS) $(LIBMODULES)
	cd $(SOURCEDIR) && $(TOOLCHAIN)/bin/$(CDIR)/create-gp4 -out pkg.gp4 \
		--content-id=$(CONTENT_ID) \
		--files "eboot.bin $(SYS_FILES) sce_sys/param.sfo sce_sys/about/right.sprx \
$(patsubst $(SOURCEDIR)/sce_module/%,sce_module/%,$(LIBMODULES)) \
$(patsubst $(SOURCEDIR)/%,%,$(ASSET_FILES))"
	@# The payload check that matters is "does pkg.gp4 list it", not "is it on
	@# disk". A $(warning) about the latter reads like the former and can be
	@# wrong: orbisrpc.elf sat in assets/ reported as found for days while being
	@# absent from every pkg, because ASSET_FILES did not match a file at that
	@# depth. Without a payload the failure is otherwise only visible on the
	@# console, at install time, as "This install is missing its payload."
	@# Each entry must appear as assets/<name> in the generated gp4.
	@# cd once, outside the loop: with a single payload the difference did not show,
	@# and with two the second iteration ran `cd installer` from inside installer,
	@# which failed every build with logo.png named in the error.
	@cd $(SOURCEDIR) && for b in $(REQUIRED_PKG_FILES); do \
	   grep -q "assets/$$b" pkg.gp4 || { \
	     echo "ERROR: $$b is not listed in pkg.gp4 -- it would not be packaged."; \
	     echo "       If it is in $(SOURCEDIR)/assets, ASSET_FILES is not matching it;"; \
	     echo "       if it is not, the install step will be missing it on the console."; \
	     rm -f pkg.gp4; exit 1; }; \
	 done

$(SOURCEDIR)/sce_sys/param.sfo: Makefile
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

$(SOURCEDIR)/eboot.bin: $(INTDIR) $(OBJS)
	$(LD) $(OBJS) -o $(INTDIR)/$(TARGET).elf $(LDFLAGS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-fself -in=$(INTDIR)/$(TARGET).elf -out=$(INTDIR)/$(TARGET).oelf --eboot "$(SOURCEDIR)/eboot.bin" --paid 0x3800000000000011
	@echo "==> built $(SOURCEDIR)/eboot.bin"

# Every header this app compiles against, as an order-only-free prerequisite on
# each object. Without this, editing a header rebuilds nothing: the pattern rule
# below only names the .c/.cpp, so a change to paths.h or icfg.h left stale .o
# files in place and the build linked a mixture of old and new code.
#
# That is not hypothetical -- it is how an extern "C" added to icfg.h failed to
# take effect, producing "undefined symbol: token_valid" from a function that
# plainly existed and plainly had the right signature. Coarse on purpose: naming
# every header rebuilds the lot whenever any one changes, which at this size costs
# a couple of seconds and is cheaper than debugging a stale object.
HEADERS    := $(wildcard $(SOURCEDIR)/*.h)

# Makefile is a prerequisite too, for the same reason. EXTRA_DEFS feeds
# -DSETUP_VERSION into every compile, so bumping VERSION
# changes what the objects should contain without touching a single .c or .h.
# Tracking headers alone left installer.o holding the old string: param.sfo
# regenerated to the new version (it names Makefile directly) while the binary
# still reported the previous one, so the package manager and the about text
# disagreed -- and the about text was the one that was wrong.
BUILD_DEPS := $(HEADERS) Makefile

$(INTDIR)/%.o: $(SOURCEDIR)/%.c $(BUILD_DEPS)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/%.o: $(SOURCEDIR)/%.cpp $(BUILD_DEPS)
	$(CCX) $(CXXFLAGS) -o $@ $<

# The shared-source rule is gone along with ps4util/. It existed to compile
# sources out of a second directory; with one source tree the pattern rules above
# cover everything.

clean:
	rm -rf installer/x64 \
	       installer/eboot.bin \
	       installer/pkg.gp4 \
	       installer/sce_sys/param.sfo \
	       installer/pkg \
	       $(OUTDIR) \
	       *.pkg
	# By glob, not via $(OBJS): OBJS is derived from the current source list, so
	# an object left behind by a deleted source would survive clean and then get
	# linked by the $(INTDIR)/*.o glob.
	rm -f installer/x64/Debug/*.o
	# assets/images/*.bmp is build output, not source: the pkg.gp4 rule rebuilds it
	# from installer/artwork/*.png via scripts/gfx2bmp.py. Removed here so a clean
	# really does prove the artwork step still works.
	rm -f installer/assets/images/*.bmp

rebuild: clean all
