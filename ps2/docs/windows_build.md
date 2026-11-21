# Building on Windows (build 128)

How to set up a Windows machine to build, test and package the port, from
nothing. Builds 113-128 were made this way on one VM (docs/machines.md
has its details since build 142c), and 142c's DXIL on the owner's Windows
desktop (CLAUDE.md, Machine); this page is the recipe for the next
machine. Linux
builds from README.md's Build section alone; macOS has
docs/macos_build.md (build 135a).

## What the tree has, and what it does not

**In the tree** (and in every `git archive` tarball of it): all the source,
the Makefile (every recipe, `make dist` and `make art` included), every
extracted asset, the compiled CRT shaders (`shaders/*.spv`, `.dxil`,
`.msl`), the routes and their saves, the harness, the proof and
measurement scripts (`tools/proofs/`, `tools/measure/`), the icon, and the
docs.

**Not in it**, each fetched from its own source by the steps below:

| What | Needed for | Where it comes from |
|---|---|---|
| MSYS2 (UCRT64): gcc, make, pkgconf, SDL3, Python, Pillow, git | everything | msys2.org, then `tools/setup_msys2.sh` |
| Cycfi's Q (header-only) | the Q synth in the game (optional: without it the game builds with the chip alone) | github.com/cycfi/Q, `tools/setup_msys2.sh --q` |
| SDL_shadercross and Microsoft's DXC | only to recompile the shaders (`make shaders`); the compiled ones are checked in | below, "The shaders" |
| CLANG64's clang and compiler-rt | only the sanitized probe (`make route_probe_asan`) | `tools/setup_msys2.sh --sanitizers` |
| The cartridge image, *Phantasy Star II (USA, Europe)* Rev 02 | only re-extracting assets (`make extract ROM=...`; build 185) | your own cartridge's |
| The disassembly, `../PSII_Disasm` (ps2.asm and art/) | only `make roundtrip` and remaking `tools/rom_map.json` (`tools/make_rom_map.sh`, which also needs AS) | beside the tree |

**Drivers:** none beyond the graphics card's own. The game draws through
SDL3's GPU API -- Direct3D 12 by default, Vulkan with
`SDL_GPU_DRIVER=vulkan` -- so any GPU whose Windows driver has Direct3D 12
runs the CRT chain; without one (or with `PS2_GPU=0`) it falls back to
SDL_Renderer, the CRT settings shown but not drawn. Sound is SDL3's audio
over the Windows default device. The packaged game (`make dist`) links
SDL3 and the C and C++ runtimes statically: its only DLLs are Windows's
own.

## The steps

1. **MSYS2.** Install from https://www.msys2.org (the default `C:\msys64`
   is what the docs assume), open "MSYS2 UCRT64" from the Start menu, and
   update: `pacman -Syu` (it may close the shell; open it again and run it
   once more).
2. **The tree.** Clone it -- from its remote, or from a hand-off bundle
   (`git clone ps2-port_128.bundle ps2`, the history with it) -- or unpack
   a hand-off tarball, anywhere; a path with spaces works (the Makefile
   quotes). Keep it off OneDrive: build 113's move off it made the routes
   about 4x faster.
3. **The packages:** in the UCRT64 shell, from the tree's top:

       tools/setup_msys2.sh --q

   (`--q` clones Q to `%USERPROFILE%\GIT\Q`, where the Makefile's `Q=auto`
   looks, at the commit the port was last proved with; `--q DIR` puts it
   elsewhere, then build with `Q_DIR=DIR`.) The packages by name, if you
   would rather: `git make mingw-w64-ucrt-x86_64-gcc
   mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-sdl3
   mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-python-pillow`.
4. **Speed (optional, recommended):** Windows Security > Virus & threat
   protection > Exclusions: add the tree's folder (or its parent) and
   `C:\msys64`. Real-time scanning of every compiler run and route file
   costs minutes a proof set.
5. **Build and test**, in the UCRT64 shell at the tree's top:

       make -j$(nproc) all headless route_probe patches_check
       make -j$(nproc) routes          # 102 PASS at build 128
       make crt_check crt_sim_check    # 27 and 40 checks (38 before 141a)

   and the whole proof set, into /tmp/proofs/NAME (MSYS2's /tmp is
   `C:\msys64\tmp`):

       tools/proofs/proof.sh NAME
       python3 tools/proofs/compare.py /tmp/proofs/OLD /tmp/proofs/NAME

   The first proof set on a new machine is its baseline: CLAUDE.md never
   compares against numbers from another machine or compiler.
6. **Package:** `make dist` -> `dist/ps2-windows/` and
   `dist/ps2-windows.zip` (the game with its assets and shaders inside it,
   the licences, the source; (build 179) no Python: COMPUTER PLAYS is in
   the game). `make art` -> the art
   dump, `../ps2_art` and `../ps2_art.tar.gz`.

## Working from Claude Code on Windows

Claude Code's Bash tool is Git Bash, whose own runtime cannot run MSYS2's
make (every recipe fails with 127, "Cannot create temporary file"). Run
builds through MSYS2's shell:

    MSYSTEM=UCRT64 CHERE_INVOKING=1 /c/msys64/usr/bin/bash.exe -lc \
        'cd "<tree>" && make -j12 ...'

and MSYS2's Python from Git Bash by its full path,
`/c/msys64/ucrt64/bin/python3`. MSYS2 has no `diff` or `cmp`:
`tools/proofs/compare.py` does what they did for the proofs. A heredoc
holding backslashes is mangled on its way through two shells; write such
edits as files. CLAUDE.md's Machine section is the machine's own: fill it
in on the new one (its cores, paths, GPU, display).

## The shaders (optional)

`shaders/` holds the CRT chain compiled for every backend, checked in;
only a change under `src/shaders/*.hlsl` needs `make shaders`, which wants
SDL_shadercross (`shadercross` on the PATH). The one builds 113-128 used,
built from source:

1. `tools/setup_msys2.sh --shaders` (CMake, Ninja, SPIRV-Cross).
2. `git clone https://github.com/libsdl-org/SDL_shadercross.git` (commit
   1ff05bec573988a98ef9e0260b4da44f512b8367 was used; no submodules
   needed, as it builds against MSYS2's SPIRV-Cross and SDL3).
3. Microsoft's DXC release zip from
   https://github.com/microsoft/DirectXShaderCompiler/releases (the one
   used reports dxcompiler 1.9.0.5402, release v1.9.2607), unpacked into
   the clone's `external/DirectXShaderCompiler-binaries/` (`bin/`, `inc/`,
   `lib/`), where its CMake looks.
4. In the clone, from the UCRT64 shell:

       cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release \
           -DSDLSHADERCROSS_VENDORED=OFF -DSDLSHADERCROSS_DXC=ON \
           -DSDLSHADERCROSS_SHARED=ON -DSDLSHADERCROSS_CLI=ON \
           -DSDLSHADERCROSS_INSTALL=ON -DCMAKE_INSTALL_PREFIX=/usr/local
       cmake --build build && cmake --install build
       cp external/DirectXShaderCompiler-binaries/bin/x64/dxcompiler.dll \
          external/DirectXShaderCompiler-binaries/bin/x64/dxil.dll /usr/local/bin/

   `/usr/local/bin` is on the UCRT64 shell's PATH but outside pacman's
   files. Then `make shaders` writes all of `shaders/`; `make
   shaders_dxc` makes the DXIL with DXC alone.

## The sanitizers (optional)

UCRT64's gcc and clang have no ASan or UBSan runtime; CLANG64's clang
does. `tools/setup_msys2.sh --sanitizers`, then build the probe from the
CLANG64 shell (`MSYSTEM=CLANG64`, where `cc` is clang): `make
route_probe_asan`; run it with `/clang64/bin` on the PATH, e.g.
`/ucrt64/bin/python3 tools/fuzz_levels.py --jobs 12` from that shell.
Nuked-OPN2's own left shift of a negative value (ym3438.c:660) is reported
and left alone.
