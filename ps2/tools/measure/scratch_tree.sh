#!/bin/bash
# (build 128; /tmp/cp115/setup.sh of builds 115-127) A scratch game
# directory for headless and whole-game runs: assets/, tools/, routes/,
# src/game/script_ids.h (the logs' script names: without it two of Rolf's
# house's scripts are named the other way round) and the built binaries,
# no saves/. main.c writes saves/ in its working directory, so headless
# runs of the game belong in one of these, never in the tree.
#
#     tools/measure/scratch_tree.sh DIR
set -eu
T="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
D=${1:?dir}
X=""
[ -f "$T/ps2_headless.exe" ] && X=".exe"
rm -rf "$D"; mkdir -p "$D"
cp -r "$T/assets" "$T/tools" "$T/routes" "$D/"
mkdir -p "$D/src/game"; cp "$T/src/game/script_ids.h" "$D/src/game/"
cp "$T/ps2_headless$X" "$T/route_probe$X" "$D/"
