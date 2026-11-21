#!/bin/bash
# (build 128; the scratch /tmp/qproof.sh of builds 113-127) Q's own checks,
# for a change that only a Q build reaches, into /tmp/qproofs/NAME/:
#
#     make -j ps2_headless ps2_headless_q q_demo fm_probe drum_hits
#     tools/proofs/qproof.sh NAME
#     python3 tools/proofs/compare.py --all /tmp/qproofs/OLD /tmp/qproofs/NAME
#
# Takes the binaries already built at the tree's top and runs them in a
# scratch copy with no saves/. (attract_q.log names its own output path, so
# it differs between two runs by that line alone.)
set -u
NAME=${1:?name}
T="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
P=/tmp/qproofs/$NAME
S=/tmp/qproof_run
rm -rf "$P" "$S"; mkdir -p "$P" "$S"
cd "$T" || exit 1
X=""
[ -f ps2_headless.exe ] && X=".exe"     # Windows's binaries
cp -r assets tools "$S/"
for b in ps2_headless ps2_headless_q q_demo fm_probe drum_hits; do
    cp "$b$X" "$S/" || { echo "qproof: build $b first"; exit 1; }
done
cd "$S" || exit 1

python3 tools/audio_level_check.py --synth q --binary ./ps2_headless_q \
    > "$P/audio_check_q.txt" 2>&1
PS2_SYNTH=q PS2_FRAMES=2400 PS2_WAV="$P/attract_q.wav" PS2_OUT="$P/attract_q.ppm" \
    "./ps2_headless_q$X" > "$P/attract_q.log" 2>&1
"./q_demo$X" --trim-pass > "$P/trim_pass.txt" 2>&1
"./drum_hits$X" > "$P/drum_hits.txt" 2>&1
"./fm_probe$X" --note 72 --hz 311 --hold 3 --wav "$P/fm_note72" > "$P/fm_note72.txt" 2>&1
"./fm_probe$X" --song step_up --solo fm1 --from 3.3 --seconds 4 --wav "$P/fm_stepup" \
    > "$P/fm_stepup.txt" 2>&1
"./fm_probe$X" --sfx C1 --seconds 0.8 --wav "$P/fm_sfxC1" > "$P/fm_sfxC1.txt" 2>&1
"./fm_probe$X" --drum 80 --wav "$P/fm_drum80" > "$P/fm_drum80.txt" 2>&1
"./fm_probe$X" --alias 74 --hz 1245 --wav "$P/fm_alias74" > "$P/fm_alias74.txt" 2>&1
# (build 129b) an effect over a song, the music's loss and the effect's cut
"./fm_probe$X" --steal B5 --over 92 --chip both > "$P/fm_steal_B5.txt" 2>&1
test ! -e saves || echo "WARNING: something wrote $S/saves"

cd "$P"
echo "$NAME: $(tail -1 audio_check_q.txt)"
echo "files: $(ls | wc -l); wav: $(ls *.wav | wc -l)"
sha256sum * > ../"$NAME".sha
