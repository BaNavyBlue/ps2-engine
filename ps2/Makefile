# Two targets on purpose:
#   ps2           - the real thing, needs SDL3
#   ps2_headless  - core + game only, no display, builds anywhere
# If the headless target stops building, something platform-specific has
# leaked into the core.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
LDLIBS  += -lm

# Wildcards, not hand-maintained lists: a new file in src/core or
# src/game is picked up automatically. Listing them by hand meant every
# added module produced a link error first.
CORE := $(wildcard src/core/*.c) $(wildcard src/audio/*.c)
GAME := $(wildcard src/game/*.c)
# (build 171) The C player (docs/c_player.md): route_probe --play, and
# (build 179) the game's COMPUTER PLAYS and route_probe --computer.
PLAYER := $(wildcard src/player/*.c)
# (build 100b) The route language's interpreter (probe.c), shared by
# route_probe and the game's COMPUTER PLAYS (autoplay.c, POSIX: fork, pipe,
# pthreads; (build 115) Windows: CreateProcess, pipes by handle, winpthreads,
# so -lpthread there too). (build 179) With the C player, which autoplay.c
# runs.
HARNESS := $(wildcard src/harness/*.c) $(PLAYER)
MAIN := src/main.c src/platform/home.c

# (build 121) Cycfi's Q, header-only (see q_demo below). Here, above Q=auto,
# which looks for it: $(HOME)/GIT/Q, or on Windows the same under the
# user's own folder (USERPROFILE, or the users' folder and $USER).
# (build 139d) A clone is known by its envelope generator: Q 1.5 (October
# 2026) keeps it in q/synth/gen/, Q before it in q/synth/. Q_HAS is a shell
# test of one directory, true for either.
# (macOS) Last, Q beside the folder that holds the repositories
# (<GIT>/Claude/phantasy-star-2/ps2 -> <GIT>/Q), wherever that folder is: a
# Mac keeps it under ~/Documents/GIT as often as ~/GIT.
# (build 189a) First of all, Q in the repository's own top folder, beside
# ps2/ (ps2/../Q): a copy of the repository that carries Q's headers there
# builds with them, at their version, with no setting and nothing else to
# fetch.
Q_HAS = { test -f "$(1)/q_lib/include/q/synth/gen/envelope_gen.hpp" \
          || test -f "$(1)/q_lib/include/q/synth/envelope_gen.hpp"; }
Q_DIR ?= $(shell for d in "$$(cd "$(CURDIR)/../Q" 2>/dev/null && pwd)" \
                     "$$HOME/GIT/Q" \
                     "$$(cygpath -u "$$USERPROFILE" 2>/dev/null)/GIT/Q" \
                     "$$(cygpath -H 2>/dev/null)/$$USER/GIT/Q" \
                     "$$(cd "$(CURDIR)/../../../Q" 2>/dev/null && pwd)"; do \
                     $(call Q_HAS,$$d) \
                     && { echo "$$d"; break; }; done)
ifeq ($(Q_DIR),)
Q_DIR := $(HOME)/GIT/Q
endif

SDL_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null)

# Extracted assets are checked in, so building never needs the
# disassembly. `make extract` (or `make assets`, the same thing)
# regenerates all of them; `make extract-<name>` reruns one extractor,
# e.g. `make extract-npcs`, then `make manifest`. Every name is in
# EXTRACTORS below.
#
# The sources are found from PS2_DIR, the disassembly's directory. Any one
# of them can be overridden on the command line:
#     make extract PS2_DIR=../ps2dis
#     make extract PS2_ASM=../ps2dis/ps2.asm ART_DIR=../ps2dis/art
# The constants file is looked for under the dotted name the current
# disassembly uses (ps2.constants.asm), then the older underscored one;
# PS2_CONST names it outright. (build 113a) The recipes quote these paths
# (and Q_DIR, SHADERCROSS, DXC), so one with a space works, but the
# constants file's lookup splits at a space as make does: with a space in
# PS2_ASM's path, name PS2_CONST too.
PS2_DIR   ?= $(HOME)/GIT/PSII/PSII_Disasm
PS2_ASM   ?= $(PS2_DIR)/ps2.asm
PS2_CONST ?= $(firstword $(wildcard $(dir $(PS2_ASM))ps2.constants.asm \
                                    $(dir $(PS2_ASM))ps2_constants.asm))
ART_DIR   ?= $(PS2_DIR)/art
PCM_ASM   ?= $(PS2_DIR)/sound/ps2.pcm_drums.asm
ROM       ?=

# (build 134, 135) In order: winart and nametables read the tile sheets art
# and inline_art write, so they come after both (`make extract` runs the
# list in this order when run without -j; tools/roundtrip.py reads it here).
# (build 156) They, and pictures, read the art from the disassembly now
# (tools/disasm_art.py); the order stands. (build 185) From the cartridge
# image, as every extractor does.
EXTRACTORS := palettes paletteptrs levels collision interactions script \
              buildings stores techs npcs gamedata battle \
              inline_art tables music drums art nametables winart flag_names \
              pictures

.PHONY: roundtrip roundtrip-edits dist patches_check computer_check audio_check audio_check_q crt_check crt_sim_check shaders shaders_dxc shaders-here manifest all headless psg_demo fm_demo smps_trace level_probe pause_probe menu_probe route_probe route_probe_asan routes extract assets extract-rom extract-asm extract-const $(addprefix extract-,$(EXTRACTORS)) clean distclean

all: ps2

# (build 107) The SDL_GPU chain (src/platform/crt_gpu.c) is the game's and
# the read-back check's. Its shaders are read at startup from shaders/
# (`make shaders`); without them the game draws through SDL_Renderer.
# (build 110) `make Q=1` (or `make headless Q=1`) adds the Q backend
# (src/audio/q/, C++20, Q_DIR as for q_demo below) to the game: the AUDIO
# page's SYNTH then offers Q beside the chip. Without it the game is C11
# alone and needs neither a C++ compiler nor Q, and SYNTH shows Q as NOT
# BUILT. Switching Q on or off does not rebuild on its own, so `make clean`
# between.
# (build 121) the owner: Q in, not opt-in. Q=auto, the default, builds the game
# (ps2, and `make dist`'s) with Q wherever Q is found and a C++ compiler
# answers, and without it anywhere else, saying so; Q=1 insists, Q=0 leaves
# it out. ps2_headless takes Q only for Q=1: it is the proofs' binary, and
# ps2_headless_q is the headless game with Q.
Q ?= auto
ifeq ($(Q),auto)
GAME_Q := $(shell $(call Q_HAS,$(Q_DIR)) \
                  && test -f "$(Q_DIR)/infra/include/infra/support.hpp" \
                  && $(CXX) --version >/dev/null 2>&1 && echo 1 || echo 0)
else
GAME_Q := $(Q)
endif
ifeq ($(GAME_Q),1)
GAME_Q_OBJ  := build/q/q_synth.o
GAME_Q_DEFS := -DPS2_HAVE_Q
GAME_Q_LIBS  = $(Q_LDLIBS)
endif
ifeq ($(Q),1)
HEADLESS_Q_OBJ  := build/q/q_synth.o
HEADLESS_Q_DEFS := -DPS2_HAVE_Q
HEADLESS_Q_LIBS  = $(Q_LDLIBS)
endif

# (build 121) Windows by the environment's OS, or MSYS2's or Cygwin's uname
# when a shell was started without it.
DIST_WINDOWS := $(filter Windows_NT Msys Cygwin,$(OS) $(shell uname -o 2>/dev/null))

# (build 123) On Windows the game's resources -- its icon, Rolf's portrait,
# and its version information (src/platform/ps2.rc) -- linked into ps2.exe
# and make dist's. windres comes with MinGW's binutils.
ifneq ($(DIST_WINDOWS),)
WINDRES ?= windres
WIN_RES := build/ps2_res.o

build/ps2_res.o: src/platform/ps2.rc assets/icon/ps2.ico
	@mkdir -p build
	$(WINDRES) --include-dir . -O coff -o $@ src/platform/ps2.rc
endif

ps2: $(CORE) $(GAME) $(HARNESS) $(MAIN) src/platform/platform_sdl3.c \
     src/platform/crt_gpu.c src/platform/first_start_sdl3.c $(GAME_Q_OBJ) \
     $(WIN_RES) | shaders-here
	@test -n "$(SDL_LIBS)" || { \
	    echo "SDL3 not found by pkg-config."; \
	    echo "Install SDL3 development files, or build the"; \
	    echo "display-free target instead: make headless"; \
	    exit 1; }
	@test "$(GAME_Q)" = 1 || echo "(no Q in $(Q_DIR), or no C++ compiler:" \
	    "the game without Q; give Q_DIR=/path/to/Q)"
	$(CC) $(CFLAGS) $(GAME_Q_DEFS) $(SDL_CFLAGS) -o $@ $^ $(SDL_LIBS) \
	    $(GAME_Q_LIBS) $(LDLIBS) -lpthread

headless: ps2_headless

ps2_headless: $(CORE) $(GAME) $(HARNESS) $(MAIN) \
              src/platform/platform_headless.c $(HEADLESS_Q_OBJ)
	$(CC) $(CFLAGS) $(HEADLESS_Q_DEFS) -o $@ $^ $(HEADLESS_Q_LIBS) $(LDLIBS) \
	    -lpthread

# (build 121) `make dist`: the game to hand to someone, in dist/ -- a folder
# and its zip (tools/make_dist.py): the game, assets/, shaders/, what
# COMPUTER PLAYS reads (routes/saves/, src/game/script_ids.h), a README and
# the licences of what is linked in. On Windows the game is linked
# statically -- SDL3, the C and C++ runtimes, winpthreads, iconv -- so it
# is one .exe with no DLL beside it, and a window with no console
# (-mwindows). Elsewhere it links as ps2 does (a Flatpak is for later).
# (build 179) COMPUTER PLAYS is the C player, in the game: builds 122-178
# carried their own Python on Windows for the Python driver.
ifneq ($(DIST_WINDOWS),)
DIST_EXE  := build/dist/ps2.exe
DIST_LIBS  = -static $(shell pkg-config --static --libs sdl3 2>/dev/null)
DIST_NAME ?= ps2-windows
else ifeq ($(shell uname -s 2>/dev/null),Darwin)
# (macOS) Linked as ps2 is, against the SDL3 the Mac has (MacPorts' or
# Homebrew's): the package runs where that SDL3 is installed.
DIST_EXE  := build/dist/ps2
DIST_LIBS  = $(SDL_LIBS)
DIST_NAME ?= ps2-macos
else
DIST_EXE  := build/dist/ps2
DIST_LIBS  = $(SDL_LIBS)
DIST_NAME ?= ps2-linux
endif

# (build 125) The pack: every file the game reads, COMPUTER PLAYS's
# included (tools/make_pack.py), linked into make dist's game (src/platform/
# pack.S, PS2_PACKED), which then reads nothing beside itself. make_pack.py
# runs every time and rewrites build/pack.bin only when its bytes change.
.PHONY: build/pack.bin dist_headless

build/pack.bin:
	python3 tools/make_pack.py --out $@ --scrub

build/pack.o: src/platform/pack.S build/pack.bin
	@mkdir -p build
	$(CC) -c -o $@ src/platform/pack.S

$(DIST_EXE): $(CORE) $(GAME) $(HARNESS) $(MAIN) src/platform/platform_sdl3.c \
             src/platform/crt_gpu.c src/platform/first_start_sdl3.c \
             $(GAME_Q_OBJ) $(WIN_RES) build/pack.o
	@test -n "$(SDL_LIBS)" || { echo "SDL3 not found by pkg-config."; exit 1; }
	@mkdir -p build/dist
	$(CC) $(CFLAGS) -DPS2_PACKED $(GAME_Q_DEFS) $(SDL_CFLAGS) -s -o $@ $^ \
	    $(DIST_LIBS) $(GAME_Q_LIBS) $(LDLIBS) -lpthread

# (build 125) The same pack in the headless game, for testing the package's
# way headless (COMPUTER PLAYS from the pack): not in the package.
dist_headless: build/dist/ps2_headless_packed

build/dist/ps2_headless_packed: $(CORE) $(GAME) $(HARNESS) $(MAIN) \
                                src/platform/platform_headless.c build/pack.o
	@mkdir -p build/dist
	$(CC) $(CFLAGS) -DPS2_PACKED -o $@ $^ $(LDLIBS) -lpthread

dist: $(DIST_EXE)
	python3 tools/make_dist.py --exe "$(DIST_EXE)" --name "$(DIST_NAME)" \
	    --q "$(GAME_Q)" --q-dir "$(Q_DIR)"

# Diagnostic for the FM core: plays real instruments from the game's bank.
# Needs Nuked-OPN2's ym3438.c/h copied into src/audio/.
# (build 136d) The bank from music/fm_instruments.json: instruments.c.
fm_demo: tools/fm_demo.c src/audio/fm.c src/audio/ym3438.c \
         src/audio/drums.c src/audio/instruments.c src/core/json.c \
         src/core/assetfs.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Diagnostic for the level renderer: one frame at an arbitrary camera
# position, as both a PPM and a raw index plane for diffing.
level_probe: tools/level_probe.c src/core/level.c src/core/video.c \
             src/core/assetfs.c src/core/json.c \
             src/core/palette.c src/core/tileset.c src/core/png.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Diagnostic for the window system and the new-game / continue / erase
# flow: scripted pad, real save files under ./saves, frames as PPM.
intro_probe: tools/intro_probe.c src/game/window.c src/game/intro_select.c \
             src/game/gamedata.c src/game/lang.c src/core/json.c \
             src/core/tileset.c \
             src/core/assetfs.c \
             src/core/video.c src/core/palette.c src/core/png.c \
             src/core/script.c src/core/winmap.c src/core/tmx.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# (build 138e) The port's own load and save of a slot file, and its new
# game: what tools/to_srm.py's selftest checks its reading against.
save_resave: tools/save_resave.c src/game/gamedata.c src/game/lang.c \
             src/core/json.c src/core/assetfs.c src/core/tileset.c \
             src/core/png.c src/core/video.c src/core/palette.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Diagnostic for the buildings: scripted pad through each one.
building_probe: tools/building_probe.c src/game/window.c src/game/building.c \
            src/game/gamedata.c src/game/lang.c src/core/json.c \
            src/core/tileset.c \
            src/core/assetfs.c src/core/rng.c \
            src/core/video.c src/core/palette.c src/core/png.c \
            src/core/script.c src/core/winmap.c src/core/tmx.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Diagnostic for the PSG core: writes a WAV exercising every channel.
psg_demo: tools/psg_demo.c src/core/psg.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Diagnostic for the sequencer: prints a piano roll of the real note
# streams, or every chip write. Needs no synthesiser, which is the
# point -- parsing and timing can be checked before anything sounds.
smps_trace: tools/smps_trace.c src/audio/smps.c src/audio/songtext.c \
            src/audio/instruments.c src/core/json.c src/core/assetfs.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# (build 109a) The mix's level through the game's own mixer: the headless
# attract sequence's title theme, its peak between -12 and -1 dBFS and no
# sample at full scale (tools/audio_level_check.py; docs/audio.md). (110)
# The chip by name (PS2_SYNTH=chip), whatever settings.json says.
audio_check: headless
	python3 tools/audio_level_check.py

# (build 112) The patch file's reader (src/audio/patches.c) on its edge cases
# (tools/patches_test.json), and the game's own file listed. C only.
PATCHES_CHECK_SRC := tools/patches_check.c src/audio/patches.c \
                     src/audio/smps.c src/audio/songtext.c \
                     src/audio/instruments.c src/core/json.c src/core/assetfs.c

patches_check: $(PATCHES_CHECK_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
	./patches_check

# (build 160) The controls' check: src/core/keys.c on its own -- the names,
# every pad style's labels, the defaults, random bindings against a model,
# the axes and hats (tools/controls_check.c, Herzog Zwei's hz_controls.c
# with this game's defaults). C only. tools/controls_check.py checks the
# menu, settings.json and a rebind through the headless game.
controls_check: tools/controls_check.c src/core/keys.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)
	./controls_check

# (build 187) The extractors in C (src/extract/; docs/first_start.md, "In
# C"): each Python extractor's twin, run from a command line as the Python
# one is (`./ps2_extract NAME IMAGE -o OUT`). C11 and the tree's own JSON
# and PNG code, nothing else. `python3 tools/extract_c_check.py ROM` runs
# both and holds the C ones to the Python's files. (build 192) `./ps2_extract
# assets FILE...` is tools/make_assets.py's twin (src/extract/exmake.c),
# held to it by tools/make_assets_c_check.py.
EXTRACT := $(wildcard src/extract/*.c)
# (build 190) The music step builds its song text back with the game's own
# song builder: songtext.c and the instrument bank it reads.
EXTRACT_CORE := src/core/json.c src/core/assetfs.c src/core/pngwrite.c \
                src/core/png.c src/audio/songtext.c src/audio/instruments.c

ps2_extract: tools/ps2_extract.c $(EXTRACT) $(EXTRACT_CORE) \
             $(wildcard src/extract/*.h)
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) $(LDLIBS)

# (build 179) COMPUTER PLAYS's check: seed 1 in us to the ending through
# the headless window's own path (the Escape menu, NEW GAME) at FAST, AUTO
# and AUTO 2X, against the stand-alone tool (`route_probe --computer`):
# the same commands at the same ticks, story, report, checkpoints and slots
# (tools/computer_check.py; docs/c_player.md). Its scratch trees go under
# COMPUTER_CHECK_DIR, outside the tree; a game that matches is removed.
COMPUTER_CHECK_DIR ?= /tmp/computer_check

computer_check: headless route_probe
	python3 tools/computer_check.py --scratch "$(COMPUTER_CHECK_DIR)"

# (build 109) The preliminary test of Cycfi's Q (docs/audio.md, "The Q
# test"): one song voiced with Q beside the Nuked-OPN2 path, as two WAVs.
# Optional and the only C++ in the tree: the game, `headless` and
# `route_probe` stay C11 and need neither a C++ compiler nor Q. Q is
# header-only (q_lib and the infra submodule), found through Q_DIR the way
# PS2_DIR finds the disassembly -- a clone beside it, `git clone
# --recurse-submodules https://github.com/cycfi/Q.git`. Q's headers are
# -isystem, so its own warnings stay out of ours. Never packed.
Q_CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2 -g
Q_INCLUDES := -isystem "$(Q_DIR)/q_lib/include" -isystem "$(Q_DIR)/infra/include"
Q_DEMO_C   := src/audio/smps.c src/audio/fm.c src/audio/ym3438.c \
              src/audio/drums.c src/audio/instruments.c src/audio/songtext.c
Q_DEMO_OBJ := $(patsubst src/audio/%.c,build/q_demo/%.o,$(Q_DEMO_C))
# (build 110) The C++ runtime, for linking the Q backend into a C program
# (`-lc++` with clang's libc++, as on macOS).
# (build 135a) macOS: Apple's clang has libc++ and no libstdc++
# (docs/macos_build.md).
ifeq ($(shell uname -s 2>/dev/null),Darwin)
Q_LDLIBS   ?= -lc++
endif
Q_LDLIBS   ?= -lstdc++

build/q_demo/%.o: src/audio/%.c
	@mkdir -p build/q_demo
	$(CC) $(CFLAGS) -c -o $@ $<

# (build 110) The Q backend, src/audio/q/q_synth.cpp: the bank build 109's
# q_demo tested, behind extern "C" and the synth table (synth.h).
build/q/q_synth.o: src/audio/q/q_synth.cpp src/audio/q/q_synth.h \
                   src/audio/synth.h src/audio/fm.h src/audio/drums.h
	@$(call Q_HAS,$(Q_DIR)) || { \
	    echo "Q not found in $(Q_DIR): clone it with --recurse-submodules,"; \
	    echo "or give Q_DIR=/path/to/Q"; exit 1; }
	@test -f "$(Q_DIR)/infra/include/infra/support.hpp" || { \
	    echo "Q's infra submodule is empty: in $(Q_DIR), git submodule update --init"; \
	    exit 1; }
	@mkdir -p build/q
	$(CXX) $(Q_CXXFLAGS) $(Q_INCLUDES) -c -o $@ src/audio/q/q_synth.cpp

# (build 136b) drums.c reads JSON: json.c and assetfs.c beside it. (136d) So
# does instruments.c, which fm.c's fm_load_instruments calls.
DRUMS_JSON_OBJ := build/q_demo/core/json.o build/q_demo/core/assetfs.o

q_demo: tools/q_demo.cpp $(Q_DEMO_OBJ) $(DRUMS_JSON_OBJ) build/q/q_synth.o
	$(CXX) $(Q_CXXFLAGS) $(Q_INCLUDES) -o $@ tools/q_demo.cpp $(Q_DEMO_OBJ) \
	    $(DRUMS_JSON_OBJ) build/q/q_synth.o $(LDLIBS)

# (build 110b) Each drum struck alone, the chip against Q's synthesised
# drums: level, decay and four bands (docs/audio.md, "The drums re-voiced").
drum_hits: tools/drum_hits.cpp build/q_demo/fm.o build/q_demo/ym3438.o \
           build/q_demo/drums.o build/q_demo/instruments.o \
           $(DRUMS_JSON_OBJ) build/q/q_synth.o
	$(CXX) $(Q_CXXFLAGS) $(Q_INCLUDES) -o $@ tools/drum_hits.cpp \
	    build/q_demo/fm.o build/q_demo/ym3438.o build/q_demo/drums.o \
	    build/q_demo/instruments.o $(DRUMS_JSON_OBJ) build/q/q_synth.o \
	    $(LDLIBS)

# (build 111) One sound through the chip and through Q in each voicing: the
# pitch over time, the partials, the envelope, the spectra over time
# (docs/audio.md, "Three sounds described", "The PM voicing").
# (build 112) and the patch file's reader, for --patches.
# (build 129b) and synth.c: the chip is the game's own chip backend.
FM_PROBE_C   := src/audio/patches.c src/audio/synth.c src/core/json.c \
                src/core/assetfs.c
FM_PROBE_OBJ := $(patsubst src/%.c,build/q_demo/%.o,$(FM_PROBE_C))

build/q_demo/core/%.o: src/core/%.c
	@mkdir -p build/q_demo/core
	$(CC) $(CFLAGS) -c -o $@ $<

build/q_demo/audio/%.o: src/audio/%.c
	@mkdir -p build/q_demo/audio
	$(CC) $(CFLAGS) -c -o $@ $<

fm_probe: tools/fm_probe.cpp $(Q_DEMO_OBJ) $(FM_PROBE_OBJ) build/q/q_synth.o
	$(CXX) $(Q_CXXFLAGS) $(Q_INCLUDES) -o $@ tools/fm_probe.cpp $(Q_DEMO_OBJ) \
	    $(FM_PROBE_OBJ) build/q/q_synth.o $(LDLIBS)

# (build 110) The headless game with the Q backend, whatever Q= says, and
# the mix's level through it: the title's theme with PS2_SYNTH=q, its peak in
# the same window as the chip's and within 3 dB of it, none at full scale.
ps2_headless_q: $(CORE) $(GAME) $(HARNESS) $(MAIN) \
                src/platform/platform_headless.c build/q/q_synth.o
	$(CC) $(CFLAGS) -DPS2_HAVE_Q -o $@ $^ $(Q_LDLIBS) $(LDLIBS) -lpthread

audio_check_q: ps2_headless_q headless
	python3 tools/audio_level_check.py --synth q --binary ./ps2_headless_q

# Diagnostic for the pause: the whole game, headless, through Start and
# Start again, printing every chip write and the sequencer's state.
pause_probe: tools/pause_probe.c $(CORE) $(GAME) src/platform/platform_headless.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Diagnostic for the C menu: the whole game, headless, through a button
# script, printing the window stack and the party.
menu_probe: tools/menu_probe.c $(CORE) $(GAME) src/platform/platform_headless.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Testing harness: the whole game, headless, driven by a route script --
# title to new game, into a shop, walking into Darum, a fight -- with waits
# on game state rather than frame counts, PNGs only where the script asks,
# and a log with every command, script id and stderr line by tick. CORE +
# GAME only: no platform, no pacing. See the top of tools/route_probe.c.
route_probe: tools/route_probe.c src/harness/probe.c $(CORE) $(GAME) \
	     $(PLAYER) src/player/player.h src/player/pl.h
	$(CC) $(CFLAGS) -o $@ $(filter %.c,$^) $(LDLIBS)

# The same with AddressSanitizer and UBSan, for fuzz runs: an overrun or
# undefined behaviour reports on stderr, which the probe logs by tick.
route_probe_asan: tools/route_probe.c src/harness/probe.c $(CORE) $(GAME) \
	     $(PLAYER) src/player/player.h src/player/pl.h
	$(CC) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
		-o $@ $(filter %.c,$^) $(LDLIBS)

# (build 127) The art dump (tools/art_export.py): art_dump records the
# battle animations by running the port's own battle objects -- one
# translation unit with src/game/game.c, which it #includes to reach the
# game's static state, so game.c is left out of its sources here -- and
# `make art` runs the lot into ART_OUT (../ps2_art) and packs
# ps2_art.tar.gz beside it: the routes whose shots give the cut-scenes their
# palettes, the plan, the dump, the export. Scratch in build/art/.
ART_DUMP_SRCS := $(filter-out src/game/game.c,$(CORE) $(GAME) $(HARNESS))
ART_ROUTES    := new_game intro_dreams army_eye_capture nei_pictures \
                 esper_b1_lutz ending mother_brain_battle
ART_OUT       ?= ../ps2_art

art_dump: tools/art_dump.c $(ART_DUMP_SRCS) src/game/game.c
	$(CC) $(CFLAGS) -o $@ tools/art_dump.c $(ART_DUMP_SRCS) $(LDLIBS) \
	    -lpthread

.PHONY: art
# (build 185) The picture art from the cartridge image: `make art ROM=...`.
art: extract-rom art_dump route_probe $(addprefix route-,$(ART_ROUTES))
	@mkdir -p build/art
	python3 tools/art_dump_plan.py > build/art/plan.txt 2> build/art/plan.err
	rm -rf build/art/adm
	./art_dump build/art/plan.txt build/art/adm > build/art/dump.txt 2> build/art/dump.err
	rm -rf "$(ART_OUT)"
	python3 tools/art_export.py "$(ROM)" build/art/adm "$(ART_OUT)"
	cd "$(ART_OUT)/.." && tar -czf "$$(basename "$(ART_OUT)").tar.gz" \
	    "$$(basename "$(ART_OUT)")"
	@echo "art: $(ART_OUT) and $(ART_OUT).tar.gz"

# Every routes/*.route, each in its own process (the game's state is
# file-static) and its own route_out/<name>/; fails if any route does.
# (build 114) A target a route, so `make -j12 routes` runs twelve at once.
# Each runs whatever the others did (the recipe's failure is ignored, as
# the loop's was), and `routes` then reads every route's summary.txt, which
# each run removes first: a route that dies before writing one fails too.
ROUTES      := $(sort $(wildcard routes/*.route))
ROUTE_NAMES := $(patsubst routes/%.route,%,$(ROUTES))
ROUTE_RUNS  := $(addprefix route-,$(ROUTE_NAMES))

.PHONY: $(ROUTE_RUNS)

routes: $(ROUTE_RUNS)
	@status=0; files=; \
	for n in $(ROUTE_NAMES); do \
	    if [ -f route_out/$$n/summary.txt ]; then \
	        files="$$files route_out/$$n/summary.txt"; \
	    else \
	        echo "failed: routes/$$n.route (no summary.txt)"; status=1; \
	    fi; \
	done; \
	for f in $$(grep -Lx 'exit 0' $$files); do \
	    n=$${f#route_out/}; echo "failed: routes/$${n%/summary.txt}.route"; \
	    status=1; \
	done; \
	exit $$status

$(ROUTE_RUNS): route-%: route_probe
	-@rm -f route_out/$*/summary.txt; ./route_probe routes/$*.route

# (build 105) The CRT simulation's CPU reference (docs/crt.md): `make
# crt_check` runs its checks by numbers and writes route_out/crt_reference/
# checks.txt; `./crt_ref crop ...` writes crops at display resolution.
CRT_REF_SRC := tools/crt_ref.c src/core/crt.c src/core/palette.c \
               src/core/png.c src/core/pngwrite.c src/core/json.c \
               src/core/assetfs.c

crt_ref: $(CRT_REF_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

crt_check: crt_ref
	./crt_ref check route_out/crt_reference

# (build 107) The GPU passes' CPU mirror (crt_tables_build's tables through
# crt_tables_render, in float) against the model, every SDR crop:
# route_out/crt_reference/sim_check.txt. Needs no GPU.
crt_sim_check: crt_ref
	./crt_ref sim route_out/crt_reference

# (build 107) The read-back check: the SDL_GPU chain drawn on this machine's
# GPU, read back and held against the model (docs/crt.md, "The read-back
# check"). SDL3; run from here after `make shaders`: ./crt_gpu_check
CRT_GPU_CHECK_SRC := tools/crt_gpu_check.c src/platform/crt_gpu.c \
                     src/core/crt.c src/core/palette.c src/core/png.c \
                     src/core/pngwrite.c src/core/json.c src/core/assetfs.c

crt_gpu_check: $(CRT_GPU_CHECK_SRC)
	@test -n "$(SDL_LIBS)" || { echo "SDL3 not found by pkg-config."; exit 1; }
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $^ $(SDL_LIBS) $(LDLIBS)

# (build 107) The CRT shaders, compiled offline: src/shaders/*.hlsl through
# SDL_shadercross into shaders/ as SPIR-V (Vulkan), DXIL (Direct3D 12) and
# MSL (Metal), which are checked in -- the game's build needs no shader
# compiler. Rerun after changing a shader. SHADERCROSS names the tool;
# SHADER_FORMATS narrows the formats (DXIL needs a shadercross built with
# DXC's dxil library, which Windows has).
SHADERCROSS    ?= shadercross
# (build 142) On Linux the default leaves DXIL out: Arch's shadercross (its
# DXC) asserts on every shader there, and the DXIL is made on Windows
# (`make shaders` there, or shaders_dxc). SHADER_FORMATS="spv dxil msl"
# still asks for all three anywhere.
ifeq ($(shell uname -s 2>/dev/null),Linux)
SHADER_FORMATS ?= spv msl
else
SHADER_FORMATS ?= spv dxil msl
endif
# (build 141a) crt_down_rgb and crt_sharp_rgb take the colour plane; the
# index-plane crt_down and crt_sharp they replace are gone (delete their
# old shaders/crt_down.frag.* and shaders/crt_sharp.frag.*). (157e) Their
# sources, src/shaders/crt_down.frag.hlsl and crt_sharp.frag.hlsl, are
# gone from the tree too.
SHADER_STEMS   := crt_fullscreen.vert crt_down_rgb.frag crt_across.frag \
                  crt_sharp_rgb.frag

# (build 141d) `make` -- the game -- brings this machine's compiled shaders
# up to date when it can, so a changed or renamed shader (141a's) never
# leaves the game on the placeholder: each shaders/<stem>.<fmt> missing or
# older than src/shaders/<stem>.hlsl is compiled with SHADERCROSS, in the
# format this machine's game reads (SHADER_FORMATS_HERE: DXIL on Windows,
# MSL on macOS, SPIR-V elsewhere). Without shadercross, or when one fails,
# the game still builds and says so loudly; it then draws without the CRT
# until `make shaders` (every format, for the checked-in set) has run.
# Compiled shaders a rename left behind (SHADERS_GONE) are removed. `make
# clean` keeps shaders/: it is checked in for builders without shadercross.
ifneq ($(DIST_WINDOWS),)
SHADER_FORMATS_HERE ?= dxil
else ifeq ($(shell uname -s 2>/dev/null),Darwin)
SHADER_FORMATS_HERE ?= msl
else
SHADER_FORMATS_HERE ?= spv
endif
SHADERS_GONE := crt_down.frag crt_sharp.frag
SHADERS_HERE := $(foreach s,$(SHADER_STEMS),\
                  $(foreach f,$(SHADER_FORMATS_HERE),shaders/$(s).$(f)))

shaders-here: $(SHADERS_HERE)
	@for s in $(SHADERS_GONE); do \
	    for f in shaders/$$s.spv shaders/$$s.dxil shaders/$$s.msl; do \
	        if test -f $$f; then \
	            rm -f $$f; echo "  removed $$f (renamed in build 141a)"; \
	        fi; \
	    done; \
	done

# One shader in one format, for shaders-here: $(1) the format's shadercross
# name. Never fails the build.
# (macOS) A fresh clone dates its files in the order git wrote them, so a
# checked-in shader can look older than its source. With no shadercross, a
# shader that is as committed, beside a source that is as committed, is
# taken as up to date (and touched, so the next build is quiet); one that
# is older for real is still drawn with, and said so.
define SHADER_HERE_RECIPE
	@mkdir -p shaders; \
	if ! command -v "$(SHADERCROSS)" >/dev/null 2>&1; then \
	    if test -f $@ && git ls-files --error-unmatch $@ >/dev/null 2>&1 \
	        && git diff --quiet HEAD -- $< $@ 2>/dev/null; then \
	        touch $@; \
	    elif test -f $@; then \
	        echo "WARNING: $@ is older than $<, and"; \
	        echo "  $(SHADERCROSS) is not found: the game draws with the old"; \
	        echo "  shader. make shaders SHADERCROSS=/path/to/shadercross"; \
	    else \
	        echo "WARNING: $@ is missing, and"; \
	        echo "  $(SHADERCROSS) is not found: the game will draw without"; \
	        echo "  the CRT. make shaders SHADERCROSS=/path/to/shadercross"; \
	    fi; \
	else \
	    case "$*" in \
	        *.vert) \
	            stage=vertex ;; \
	        *) \
	            stage=fragment ;; \
	    esac; \
	    if "$(SHADERCROSS)" $< -s HLSL -d $(1) -t $$stage -e main -o $@; \
	    then \
	        echo "  $@"; \
	    else \
	        rm -f $@; \
	        echo "WARNING: $@ FAILED to compile (above): the game will"; \
	        echo "  draw without the CRT"; \
	    fi; \
	fi
endef

shaders/%.spv: src/shaders/%.hlsl
	$(call SHADER_HERE_RECIPE,SPIRV)
shaders/%.dxil: src/shaders/%.hlsl
	$(call SHADER_HERE_RECIPE,DXIL)
shaders/%.msl: src/shaders/%.hlsl
	$(call SHADER_HERE_RECIPE,MSL)

shaders:
	@command -v "$(SHADERCROSS)" >/dev/null 2>&1 || { \
	    echo "$(SHADERCROSS) not found: build SDL_shadercross, or"; \
	    echo "make shaders SHADERCROSS=/path/to/shadercross"; exit 1; }
	@mkdir -p shaders; status=0; \
	for s in $(SHADER_STEMS); do \
	    stage=fragment; \
	    case $$s in \
	        *.vert) \
	            stage=vertex ;; \
	    esac; \
	    for f in $(SHADER_FORMATS); do \
	        case $$f in \
	            spv) \
	                d=SPIRV ;; \
	            dxil) \
	                d=DXIL ;; \
	            msl) \
	                d=MSL ;; \
	        esac; \
	        if "$(SHADERCROSS)" src/shaders/$$s.hlsl -s HLSL -d $$d -t $$stage \
	                -e main -o shaders/$$s.$$f; then \
	            echo "  shaders/$$s.$$f"; \
	        else \
	            rm -f shaders/$$s.$$f; \
	            echo "  FAILED shaders/$$s.$$f (removed)"; status=1; \
	        fi; \
	    done; \
	done; \
	exit $$status

# (build 142b) A format that fails is removed, not left behind: Arch's
# shadercross left four empty .dxil files (its DXC asserts), which make
# took for up to date and the game for shaders it then refused.

# (build 113) The DXIL alone, straight from Microsoft's DXC (its release zip:
# dxc.exe with dxcompiler.dll and dxil.dll beside it, so the DXIL is
# signed), for a machine with no shadercross that can write DXIL. The HLSL
# names SDL_GPU's register spaces itself, so DXC needs nothing else.
DXC ?= dxc

shaders_dxc:
	@command -v "$(DXC)" >/dev/null 2>&1 || { \
	    echo "$(DXC) not found: make shaders_dxc DXC=/path/to/dxc"; exit 1; }
	@mkdir -p shaders; status=0; \
	for s in $(SHADER_STEMS); do \
	    profile=ps_6_0; \
	    case $$s in \
	        *.vert) \
	            profile=vs_6_0 ;; \
	    esac; \
	    if "$(DXC)" -nologo -E main -T $$profile -Fo shaders/$$s.dxil \
	            src/shaders/$$s.hlsl; then \
	        echo "  shaders/$$s.dxil"; \
	    else \
	        rm -f shaders/$$s.dxil; \
	        echo "  FAILED shaders/$$s.dxil (removed)"; status=1; \
	    fi; \
	done; \
	exit $$status

# (build 105) The display probe: SDL3, not the game. What SDL says about
# the window and the screen, windowed and fullscreen (docs/crt.md).
display_probe: tools/display_probe.c
	@test -n "$(SDL_LIBS)" || { echo "SDL3 not found by pkg-config."; exit 1; }
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $^ $(SDL_LIBS) $(LDLIBS)

# `assets` is also the name of a directory, which is why it is .PHONY.
# Before this rule existed, `make assets` found that directory, had no
# rule for it and did nothing -- `-B` included -- so nothing was extracted.
assets: extract

extract: $(addprefix extract-,$(EXTRACTORS))
	python3 tools/make_manifest.py assets

extract-asm:
	@test -f "$(PS2_ASM)" || { \
	    echo "PS2_ASM=$(PS2_ASM) not found; set PS2_DIR or PS2_ASM"; \
	    exit 1; }

extract-const: extract-asm
	@test -n "$(PS2_CONST)" && test -f "$(PS2_CONST)" || { \
	    echo "no ps2.constants.asm or ps2_constants.asm beside $(PS2_ASM);"; \
	    echo "set PS2_CONST to the constants file"; \
	    exit 1; }

# (build 185) The extractors read the cartridge image, not the disassembly:
# `make extract ROM=image.bin`, Phantasy Star II (USA, Europe) Rev 02, a
# raw image with no copier header.
# Where things are in it, and what the disassembly calls them, is
# tools/rom_map.json (tools/rommap.py reads both; tools/make_rom_map.py
# makes the map from the disassembly and its listing, when the disassembly
# changes). The disassembly's own paths above stay for the tools that read
# it still: roundtrip's packing back, tools/door_audit.py and
# tools/rom_left.py.
extract-rom:
	@test -n "$(ROM)" && test -f "$(ROM)" || { \
	    echo "ROM=$(ROM) not found: set ROM to the cartridge image,"; \
	    echo "Phantasy Star II (USA, Europe) Rev 02"; \
	    exit 1; }

extract-palettes: extract-rom
	python3 tools/extract_palettes.py "$(ROM)" -o assets/palettes
extract-paletteptrs: extract-rom
	python3 tools/extract_paletteptrs.py "$(ROM)" -o assets/palettes
extract-levels: extract-rom
	python3 tools/extract_levels.py "$(ROM)" -o assets
extract-collision: extract-rom
	python3 tools/extract_collision.py "$(ROM)" -o assets/levels
extract-interactions: extract-rom
	python3 tools/extract_interactions.py "$(ROM)" -o assets/levels
extract-script: extract-rom
	python3 tools/extract_script.py "$(ROM)" -o assets/script
extract-winart: extract-rom
	python3 tools/extract_winart.py "$(ROM)" -o assets/winart.json
extract-buildings: extract-rom
	python3 tools/extract_buildings.py "$(ROM)" -o assets/data
extract-stores: extract-rom
	python3 tools/extract_stores.py "$(ROM)" -o assets/data
extract-techs: extract-rom
	python3 tools/extract_techs.py "$(ROM)" -o assets/data
extract-npcs: extract-rom
	python3 tools/extract_npcs.py "$(ROM)" -o assets
extract-gamedata: extract-rom
	python3 tools/extract_gamedata.py "$(ROM)" -o assets/data
extract-battle: extract-rom
	python3 tools/extract_battle.py "$(ROM)" -o assets/data
extract-flag_names: extract-npcs extract-rom
	python3 tools/extract_flag_names.py "$(ROM)" -o assets/data
extract-inline_art: extract-rom
	python3 tools/extract_inline_art.py "$(ROM)" -o assets/tiles
extract-nametables: extract-rom
	python3 tools/extract_nametables.py "$(ROM)" -o assets/nametables
extract-tables: extract-rom
	python3 tools/extract_tables.py "$(ROM)" -o assets/tables
extract-music: extract-rom
	python3 tools/extract_music.py "$(ROM)" -o assets/music
extract-drums: extract-rom
	python3 tools/extract_drums.py "$(ROM)" -o assets/music
# (build 142) The full-colour pictures (docs/asset_format.md), made from what
# the extractors above wrote: last.
extract-pictures: extract-rom
	python3 tools/extract_pictures.py "$(ROM)" --assets assets -o assets/art
extract-art: extract-rom
	python3 tools/extract_art.py "$(ROM)" -o assets

# (build 130a) The asset round-trip (tools/roundtrip.py): the extractors run
# again into a scratch tree and compared with assets/, and each converted file
# packed back and compared with the ROM's bytes. Report in route_out/roundtrip/.
# (build 185) The extractors read the cartridge image now, the packing back
# the disassembly still: both, `make roundtrip ROM=image.bin`.
roundtrip: extract-rom extract-const
	python3 tools/roundtrip.py --rom "$(ROM)" --asm "$(PS2_ASM)" \
	    --const "$(PS2_CONST)" --art "$(ART_DIR)" --pcm "$(PCM_ASM)"

# (build 131) Proof that level 2 of the JSON tables catches an edit: one
# field a table changed on a scratch copy of assets/, both levels must fail.
roundtrip-edits: extract-rom extract-const
	python3 tools/roundtrip.py --rom "$(ROM)" --asm "$(PS2_ASM)" \
	    --const "$(PS2_CONST)" --art "$(ART_DIR)" --prove-edits

# The list of files the game preloads into RAM. Rerun after adding or
# renaming anything under assets/.
manifest:
	python3 tools/make_manifest.py assets

# (build 113a) Each under its Windows name too (MSYS2's gcc writes ps2.exe
# for -o ps2), and patches_check, which build 112 left out.
BINARIES := ps2 ps2_headless psg_demo fm_demo smps_trace level_probe \
            intro_probe building_probe pause_probe menu_probe route_probe \
            route_probe_asan crt_ref crt_gpu_check display_probe q_demo \
            ps2_headless_q drum_hits fm_probe patches_check art_dump \
            save_resave controls_check ps2_extract

clean:
	rm -f $(BINARIES) $(addsuffix .exe,$(BINARIES)) frame.ppm
	rm -rf build dist
	rm -rf $(addsuffix .dSYM,$(BINARIES))    # (135a) macOS clang's -g symbols

# Assets are tracked, so removing them is deliberate and separate.
distclean: clean
	rm -rf assets/tiles assets/palettes assets/levels assets/winart \
	       assets/winart.json assets/art_manifest.json assets/music
