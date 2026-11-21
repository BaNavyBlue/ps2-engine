# Building on macOS (build 135a; Intel Macs, build 136; first tried on a Mac, build 149a)

How to build and run the port on a Mac, from nothing. Linux builds from
README.md's Build section; Windows has docs/windows_build.md.

## Tried on a Mac (build 149a)

First written from the Makefile and the source in a Linux sandbox; in
build 149a it was built and run on a Mac (the edits made on build 148's
tree, then brought onto 149):

* An Intel MacBook Pro (Core i7-6820HQ, 4 cores, 8 threads), macOS 15.7.7,
  Apple clang 17.0.0 (Xcode's command line tools), MacPorts: SDL3 3.4.16,
  gmake 4.4.1, pkgconfig 0.29.2; Apple's own Python 3.9.6 (no Pillow); Q
  v1.5.4 (efe2611) beside the folder that holds the repositories.
* `gmake -j8` builds `ps2` with Q, linked against libc++ and MacPorts'
  SDL3: no errors, two warnings (clang's `-Wsometimes-uninitialized` in
  src/audio/songtext.c, which gcc does not give).
* `./ps2` opens the window: `display: video cocoa, SDL_GPU metal, MSL
  shaders, 60.00 Hz, vsync on`, then `HDR on, headroom 2.000, SDR white
  1.000 (203 nits)` on the laptop's panel. How it looks, sounds and paces
  is still for someone at the Mac.
* `make routes` 132 of 132 PASS (645 files) in about two minutes at
  `-j8`; `patches_check` PASS; `crt_check` 27 of 27; `crt_sim_check` 40
  of 40; `./crt_gpu_check` 105 of 105 on Metal, with the checked-in MSL.
* The packed game (`build/dist/ps2`, `dist_headless`) links: the pack in
  Mach-O's syntax, the folder found through dyld, the saves in
  ~/Library/Application Support when the game's own folder cannot be
  written (below). `make dist` itself was not run: tools/make_dist.py
  needs Python 3.12 or later, and the Mac had 3.9.

## What the tree has, and what it does not

The tree builds on macOS the way it does on Linux: plain C11 for the game,
SDL3 for the window, sound and input, C++20 only for the optional Q synth.
SDL3's GPU API draws through Metal there, so the CRT chain reads the Metal
shaders (`shaders/*.msl`), which SDL_shadercross writes beside the SPIR-V and
DXIL. Nothing in `src/` is x86-only, so Apple Silicon and Intel Macs build
the same.

| What | Needed for | Where it comes from |
|---|---|---|
| Xcode Command Line Tools: clang, clang++, git, make | everything | `xcode-select --install` |
| Homebrew: `sdl3`, `pkgconf` | the game (`make`); not `make headless` | brew.sh, then `brew install` (Intel Macs: "Intel Macs", below) |
| Homebrew: `python` | COMPUTER PLAYS (it runs `python3` from the PATH), and the tools | `brew install python` |
| Homebrew: `pillow` | only the extractors and the art tools (`make extract`, `make roundtrip`, `make art`); COMPUTER PLAYS does not import it | `brew install pillow` |
| Homebrew: `make` (GNU make 4, as `gmake`) | recommended: macOS's own make is GNU make 3.81 from 2006, which the Makefile has not been run with | `brew install make` |
| Cycfi's Q (header-only) | the Q synth (optional: without it the game builds with the chip alone) | `git clone --recurse-submodules https://github.com/cycfi/Q.git ~/GIT/Q` |
| `shaders/` | the CRT chain (optional: without it the game draws through SDL_Renderer) | your own `make shaders` output, copied from the Linux or Windows machine |
| the disassembly (`ps2.asm`, `art/`) | only re-extracting assets | copy it beside the tree, as on the other machines |

`shaders/` is never in a tarball (it is yours, built on your machines), so
copy the folder over with the tree. A clone of the repository has it
already. Its `.msl` files are the ones Metal reads; `make shaders` itself
needs SDL_shadercross, which nothing here builds for macOS, and is not
needed to play. A fresh clone dates its files in the order git wrote
them, so an `.msl` can look older than its `.hlsl`; with no shadercross
the Makefile takes a shader that is as committed as up to date, and says
so only when one is out of date for real.

## The steps

1. Install Xcode's command line tools, which bring clang, clang++, git and
   make:

   ```sh
   xcode-select --install
   ```

2. Install Homebrew (brew.sh), then the libraries and tools:

   ```sh
   brew install sdl3 pkgconf python make
   brew install pillow          # only for make extract / roundtrip / art
   ```

   Open a new Terminal afterwards so Homebrew's `python3` and `pkg-config`
   come before Apple's on the PATH. Check SDL3 is found:

   ```sh
   pkg-config --modversion sdl3
   ```

   Pillow goes in through Homebrew rather than pip: Homebrew's Python
   refuses `pip install` into itself (PEP 668, "externally managed").

3. (Optional) Q, where the Makefile looks for it:

   ```sh
   git clone --recurse-submodules https://github.com/cycfi/Q.git ~/GIT/Q
   ```

   The Makefile also finds Q beside the folder that holds the
   repositories: a clone at `<GIT>/Claude/phantasy-star-2/ps2` finds
   `<GIT>/Q`, so `~/Documents/GIT/Q` works with the repositories in
   `~/Documents/GIT/Claude`. Anywhere else works with `Q_DIR=/path/to/Q`
   on the make line.

4. The tree: unpack it as on the other machines (a folder named `ps2`
   anywhere, say `~/GIT/ps2`), and copy your `shaders/` folder into it.

5. Build, from `ps2/`:

   ```sh
   gmake -j"$(sysctl -n hw.ncpu)"          # the game, ./ps2
   gmake headless                          # the display-free game, if wanted
   ```

   (`make` instead of `gmake` will probably work too.) `Q=auto`, the
   default, builds Q in when it finds it and says so when it does not;
   `Q=0` leaves it out. On macOS the Makefile links Q against Apple's C++
   runtime, libc++ (`Q_LDLIBS=-lc++`; Linux and Windows keep `-lstdc++`).
   After switching Q on or off, `gmake clean` first.

6. Run it from `ps2/`:

   ```sh
   ./ps2
   ```

   The startup lines say what it found: `display: ...` names the GPU path
   (SDL_GPU on Metal with the shaders, or SDL_Renderer without them), and
   `saves: ...` where your slots and settings.json go (`ps2/saves/`).
   `PS2_GPU=0 ./ps2` forces SDL_Renderer if the GPU path misbehaves.

## Intel Macs (build 136)

Homebrew 7.0.0 (September 2026) moved Intel Macs to its lowest support
tier: no new prebuilt packages, an installer for Apple Silicon only, Intel
support ending in September 2027; it points Intel users at MacPorts. Two
ways round it, neither tried on a Mac yet. Step 1 (Xcode's command line
tools), 3 (Q), 4 (the tree) and 6 (running it) are the same; only step 2
and the make line change.

**MacPorts.** The installer for your macOS from macports.org (its SDL3
port wants macOS 11 or later), then:

```sh
sudo port install SDL3 pkgconfig gmake python313 py313-Pillow
sudo port select --set python3 python313
```

MacPorts installs under `/opt/local` and puts `/opt/local/bin` first on the
PATH (open a new Terminal); its `pkg-config` finds SDL3 there, so `gmake`
builds as in step 5. This is the way the port was first built on a Mac
(above): SDL3, pkgconfig and gmake from MacPorts. The port names were
checked with `port info` (python313 3.13.15, py313-Pillow 12.3.0; python314
and py314-Pillow are there too); Python and Pillow were not installed on
that Mac. Pillow through the port, not pip, as with Homebrew. For proof.sh,
MacPorts' `bash`, `coreutils` and `findutils`, with
`/opt/local/libexec/gnubin` first on the PATH (not checked).

MacPorts builds SDL3 for the macOS it runs on (on macOS 15, `minos 15.0`
in the library): a game linked against it runs on that macOS and later.

**No package manager.** SDL's own release (github.com/libsdl-org/SDL,
Releases, `SDL3-<version>.dmg`) carries `SDL3.xcframework`, built for Intel
and Apple Silicon alike. Copy its macOS framework (the folder for macOS
inside it) to `~/Library/Frameworks/SDL3.framework`. The Makefile asks
pkg-config for SDL3's flags, but flags on the make line win, so with no
pkg-config at all:

```sh
make SDL_CFLAGS="-F$HOME/Library/Frameworks" \
     SDL_LIBS="-F$HOME/Library/Frameworks -framework SDL3 -Wl,-rpath,$HOME/Library/Frameworks"
```

(the same two on `make display_probe` or `crt_gpu_check`; `make headless`
needs neither). The source includes `<SDL3/SDL.h>`, which `-F` finds as
the framework's own headers. Python from python.org's installer (it runs on
Intel and Apple Silicon); its pip installs into it, so `python3 -m pip
install Pillow` for the extractors. macOS's own make is 3.81; if it
misbehaves, GNU make builds from its source (`./configure && make`).

## Known gaps on macOS

- **Started from another folder** (build 149a) the game finds its own
  through dyld (`_NSGetExecutablePath`, then `realpath`;
  `src/platform/home.c`), as Linux does through `/proc/self/exe`: tried
  from a folder with no `assets/`, it says `home: the game's folder, ...`
  and reads them from beside itself.
- **Saves where the game's folder cannot be written** (build 149a) go
  to `~/Library/Application Support/PhantasyStarII/saves` (and `mods/`
  beside it), the Mac's place for them, rather than Linux's
  `~/.local/share`. Tried with the packed game in a read-only folder.
- **`make dist`** (build 149a): the pack (`src/platform/pack.S`) has
  Mach-O's spelling beside ELF's and COFF's (`__TEXT,__const`,
  `_ps2_pack`), and `build/dist/ps2` and `dist_headless` link and run from
  the pack alone. Two things are not done: tools/make_dist.py needs Python
  3.12 or later (`brew install python`, or MacPorts' `python313`; Apple's
  3.9 stops at `rmtree(onexc=)`), and the package's README still talks of
  Windows's folders. The game in it links to the SDL3 it was built with
  (`otool -L`), so it runs on Macs that have the same SDL3 installed, not
  yet on a bare Mac: that wants SDL3's dylib beside it with an
  `@executable_path` install name, or an .app bundle. Apple's linker
  ignores `-s` (it says so); the game is not stripped.
- **HDR is untested.** On Apple displays SDL3 offers HDR through EDR and,
  by its documentation, reports SDR white as 1.0 there. The port takes SDR white to be 203
  nits off Windows (`platform_sdl3.c`), which has not been measured
  against a Mac's EDR; PEAK and the white point may need a look there.
  (Build 149a) On the Intel MacBook Pro's panel SDL3 reported HDR
  on with headroom 2.000 and SDR white 1.000, which the port took as 203
  nits (AUTO 406 nits); whether that looks right is still to be seen.
- **FULLSCREEN with the game running** (build 168a) did nothing: the
  window stayed as it was. Fixed; "Fullscreen", below.
- **The proof scripts want GNU tools.** `tools/proofs/proof.sh` uses
  bash 5 (`$EPOCHREALTIME`), GNU `xargs -d` and `sha256sum`, none of which
  macOS's own versions have: `brew install bash coreutils findutils`, put
  `$(brew --prefix)/opt/coreutils/libexec/gnubin` and
  `$(brew --prefix)/opt/findutils/libexec/gnubin` first on the PATH, and
  run it with Homebrew's bash. `make routes` itself only runs
  `route_probe`, which builds with the rest.

## Fullscreen (build 168a)

FULLSCREEN switched on with the game running did nothing on a Mac.
SDL3 on macOS makes fullscreen a Space of its own by default, and once
the GPU chain has drawn into the window that switch is taken and never
made: SDL_SetWindowFullscreen and SDL_SyncWindow both say yes, the window
stays as it was and SDL_WINDOW_FULLSCREEN stays off, with nothing in the
log. Fullscreen from settings.json at the start got there, being before
the first frame. A test of SDL alone (SDL 3.4.18 on an Intel MacBook Pro,
macOS 15) showed it in three runs of three: a window a GPU device has
drawn into, never; the same window with no GPU device, always; the GPU
window with SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES set to "0", always. So
platform_sdl3.c sets that hint on macOS before SDL_Init, and fullscreen
there is the borderless window over the display, as on Windows and
Linux, with no Space of its own and no slide.

FULLSCREEN is the Escape menu's, as everywhere; it switches with the game
running, both ways.

## If it does not build

- **"SDL3 not found by pkg-config."** `brew install sdl3 pkgconf` (on an
  Intel Mac, `sudo port install SDL3 pkgconfig`, or the framework and the
  make line in "Intel Macs"), open a new Terminal, and check `pkg-config
  --libs sdl3` prints something.
- **Link errors naming `std::` or `-lstdc++`.** The Q object was linked
  against the wrong C++ runtime: `gmake clean`, then `gmake
  Q_LDLIBS=-lc++` (or `Q=0` for the game without Q), and report it: the
  Makefile is meant to pick libc++ on macOS by itself.
- **Compiler warnings.** clang warns about some things gcc does not under
  `-Wpedantic`; warnings do not stop the build. Errors do -- send the
  first one.
- **`ps2.dSYM` folders** beside the binaries are clang's debug symbols
  (`-g`), harmless; `gmake clean` removes them and git ignores them.
- **COMPUTER PLAYS says "no python3 on the PATH".** Start the game from a
  Terminal where `python3 --version` answers (Homebrew's, 3.12 or later is
  what the harness is run with elsewhere).
