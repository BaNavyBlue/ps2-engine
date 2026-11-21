#!/bin/bash
# (build 128; the scratch /tmp/proof.sh of builds 113-127) The proofs of
# CLAUDE.md, taken on this machine into /tmp/proofs/NAME/, each stage timed
# in /tmp/proofs/NAME.times.txt.
#
#     tools/proofs/proof.sh NAME [JOBS]
#     PROOF_COLOUR=1 tools/proofs/proof.sh NAME [JOBS]   # (142a) + colour_check
#     python3 tools/proofs/compare.py /tmp/proofs/OLD /tmp/proofs/NAME
#
# From the tree's top or anywhere: the tree is found from this script's
# place. On Windows run it in MSYS2's UCRT64 shell (CLAUDE.md, Machine).
# Builds the plain targets first. The routes run in the tree (each clears
# only its own route_out/<name>/); main.c's headless runs and boss_trials
# run in /tmp/ps2_proof, a copy with no saves/.
set -u
NAME=${1:?name}
JOBS=${2:-$(nproc 2>/dev/null || echo 4)}
T="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
P=/tmp/proofs/$NAME
S=/tmp/ps2_proof
rm -rf "$P"; mkdir -p "$P"
TIMES=/tmp/proofs/$NAME.times; : > "$TIMES"
stamp() { echo "$1 $EPOCHREALTIME" >> "$TIMES"; }
stamp start
cd "$T" || exit 1

make -j"$JOBS" headless route_probe crt_ref patches_check > "$P/build.log" 2>&1 \
    || { echo "build failed: $P/build.log"; exit 1; }
X=""
[ -f ps2_headless.exe ] && X=".exe"     # Windows's binaries
stamp built
grep -E "warning|error" "$P/build.log" > "$P/build_warnings.txt"

# Routes: PASS lines in route order, then every file they wrote by checksum.
# (build 114) `make -j routes` (route_probe was built just above); each
# route's PASS line is its summary.txt's first.
make -j"$JOBS" -o route_probe routes > "$P/make_routes.log" 2>&1
for r in routes/*.route; do
    head -1 "route_out/$(basename "$r" .route)/summary.txt"
done > "$P/routes_pass.txt"
stamp routes_run
for r in routes/*.route; do
    n=$(basename "$r" .route)
    find "route_out/$n" -type f | sort
done | xargs -d '\n' sha256sum > "$P/routes_files.sha"

stamp routes_summed

# (build 142a) PROOF_COLOUR=1: tools/colour_check.py over every route, JOBS
# at a time -- every tick's colour plane held to the palette path and every
# shot read back both ways (docs/harness.md). Long on few cores (six
# minutes on one, build 142a), so only when asked; server_proof.sh asks.
# It runs the routes again after their files are summed above (they write
# the same files). colour_check.txt is the verdict alone, so adding a route
# does not make two runs differ; colour_check_full.txt has every route's
# line.
if [ "${PROOF_COLOUR:-0}" = 1 ]; then
    rm -rf "/tmp/proofs/$NAME.colour"
    python3 tools/colour_check.py --jobs "$JOBS" \
        --out "/tmp/proofs/$NAME.colour" > "$P/colour_check_full.txt" 2>&1
    tail -1 "$P/colour_check_full.txt" | sed -E \
        's/^(SAME|DIFF): [0-9]+ routes \(([0-9]+) failed\), [0-9]+ frames composed both ways, ([0-9]+) differ; [0-9]+ shots read back both ways, ([0-9]+) differ.*/\1: \2 routes failed, \3 frames differ, \4 shots differ/' \
        > "$P/colour_check.txt"
    rm -rf "/tmp/proofs/$NAME.colour"
    stamp colour_check
fi
make crt_check 2>&1 | tail -1 > "$P/crt_check.txt"
make crt_sim_check 2>&1 | tail -1 > "$P/crt_sim_check.txt"
./patches_check$X > "$P/patches_check_full.txt" 2>/dev/null
{ grep -c '^ok' "$P/patches_check_full.txt"; tail -1 "$P/patches_check_full.txt"; } \
    > "$P/patches_check.txt"

stamp crt_patches
rm -rf "$S"; mkdir -p "$S"
cp -r assets tools routes "$S/"; cp "ps2_headless$X" "route_probe$X" "$S/"
cd "$S" || exit 1
stamp copied
python3 tools/audio_level_check.py 2>&1 | tail -1 > "$P/audio_check.txt"
stamp audio_check
PS2_FRAMES=2400 PS2_WAV="$P/attract.wav" PS2_OUT="$P/attract.ppm" \
    "./ps2_headless$X" > "$P/attract.log" 2>&1
stamp attract
python3 tools/boss_trials.py --boss dark_force --policy both --repeats 10 \
    --jobs "$JOBS" > "$P/boss_trials.txt" 2>&1
stamp boss_trials
python3 tools/probe_check.py > "$P/probe_check.txt" 2>&1
stamp probe_check
# (build 157d) A refused mod set aside: the headless game with a broken
# file in a scratch mods folder, case by case (tools/mods_check.py).
python3 tools/mods_check.py --jobs "$JOBS" > "$P/mods_check.txt" 2>&1
stamp mods_check
cp route_out/boss_trials_dark_force.json route_out/boss_trials_dark_force.md "$P/"
test ! -e saves || echo "WARNING: something wrote $S/saves"

awk 'NR > 1 { printf "%-14s %6.1f s\n", $1, $2 - p }
     { p = $2; if (NR == 1) { s = $2 } }
     END { printf "%-14s %6.1f s\n", "total", p - s }' "$TIMES" > "$TIMES.txt"
cd "$P"
echo "$NAME: $(grep -c '^PASS' routes_pass.txt) PASS of $(wc -l < routes_pass.txt) routes," \
     "$(wc -l < routes_files.sha) files; crt $(cat crt_check.txt);" \
     "sim $(cat crt_sim_check.txt); patches $(head -1 patches_check.txt) ok," \
     "$(tail -1 patches_check.txt)"
echo "audio: $(cat audio_check.txt)"
echo "probe_check: $(grep -c '^ok' probe_check.txt) ok, $(tail -1 probe_check.txt)"
echo "mods_check: $(grep -c '^ok' mods_check.txt) ok, $(tail -1 mods_check.txt)"
if [ -f colour_check.txt ]; then
    echo "colour: $(cat colour_check.txt)"
fi
sha256sum attract.wav boss_trials_dark_force.json
