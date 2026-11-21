#!/bin/bash
# (build 128) The Windows build's MSYS2 packages, and Cycfi's Q, for a new
# machine (docs/windows_build.md has the whole recipe). Run in an MSYS2
# shell (any of them: pacman is shared) after MSYS2's own `pacman -Syu`:
#
#     tools/setup_msys2.sh               the game, the proofs, make dist
#     tools/setup_msys2.sh --shaders     + CMake, Ninja, SPIRV-Cross, to
#                                        build SDL_shadercross (make shaders)
#     tools/setup_msys2.sh --sanitizers  + CLANG64's clang and compiler-rt
#                                        (make route_probe_asan)
#     tools/setup_msys2.sh --q [DIR]     + clone Q at the commit the port was
#                                        last built and proved with
#
# The options go together. The versions the port was last built with
# (builds 113-128, the VM in CLAUDE.md): gcc 16.2.0, make 4.4.1, pkgconf
# 3.0.7, SDL3 3.4.16, Python 3.14.7, Pillow 12.3.0, CMake 4.4.3, Ninja
# 1.13.2, SPIRV-Cross 1.4.357, clang 22.1.8. pacman installs today's; a
# newer compiler or maths library can move a WAV's last bits, which is why
# CLAUDE.md compares proofs only on one machine.
set -eu

BASE="git make
      mingw-w64-ucrt-x86_64-gcc
      mingw-w64-ucrt-x86_64-pkgconf
      mingw-w64-ucrt-x86_64-sdl3
      mingw-w64-ucrt-x86_64-python
      mingw-w64-ucrt-x86_64-python-pillow"
SHADERS="mingw-w64-ucrt-x86_64-cmake
         mingw-w64-ucrt-x86_64-ninja
         mingw-w64-ucrt-x86_64-spirv-cross"
SANITIZERS="mingw-w64-clang-x86_64-clang
            mingw-w64-clang-x86_64-compiler-rt"
Q_URL=https://github.com/cycfi/Q.git
Q_COMMIT=6cd1aba653765bb66650cff457c0d2333fb4181f      # 2026-09-26; infra 2dff97a

want="$BASE"
q_dir=""
while [ $# -gt 0 ]; do
    case "$1" in
        --shaders)
            want="$want $SHADERS"
            ;;
        --sanitizers)
            want="$want $SANITIZERS"
            ;;
        --q)
            if [ $# -gt 1 ] && [ "${2#--}" = "$2" ]; then
                q_dir="$2"
                shift
            else
                # where the Makefile's Q=auto looks on Windows
                q_dir="$(cygpath -u "${USERPROFILE:-$HOME}" 2>/dev/null || echo "$HOME")/GIT/Q"
            fi
            ;;
        *)
            echo "setup_msys2: unknown option $1" >&2
            exit 2
            ;;
    esac
    shift
done

# shellcheck disable=SC2086
pacman -S --needed --noconfirm $want

if [ -n "$q_dir" ]; then
    if [ ! -d "$q_dir/.git" ]; then
        git clone --recurse-submodules "$Q_URL" "$q_dir"
    fi
    git -C "$q_dir" checkout -q "$Q_COMMIT"
    git -C "$q_dir" submodule update --init --recursive
    echo "setup_msys2: Q at $q_dir ($(git -C "$q_dir" rev-parse --short HEAD))"
fi
echo "setup_msys2: done. Build from the MSYS2 UCRT64 shell: make -j\$(nproc) all headless route_probe patches_check"
