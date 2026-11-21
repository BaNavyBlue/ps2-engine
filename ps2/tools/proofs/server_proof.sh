#!/bin/bash
# (build 142a) The whole proof of one commit, unattended, on a machine with
# no display and no GPU (The owner's OVH server; docs/machines.md):
#
#     tools/proofs/server_proof.sh [REF]          # in a clone; REF: HEAD
#
# REF's files are taken out of git (git archive) into a scratch tree, so
# the clone itself is never built in or changed, and proof.sh runs there
# with PROOF_COLOUR=1 -- the routes, colour_check, the CRT model and sim
# checks, patches, the audio level, the attract WAV, the Dark Force trial,
# probe_check. What needs a display or a GPU (the windowed game,
# crt_gpu_check, the shaders) is not here.
#
# Into $PS2_PROOFS (default ~/ps2_proofs):
#     <commit>/          proof.sh's set, as /tmp/proofs/NAME is elsewhere
#     <commit>.txt       its own lines, then compare.py against the last run
#     summary.txt        one line a run, newest last
#     last               the commit of the last run that finished
# Runs one at a time (a lock): proof.sh's scratch directories in /tmp are
# shared. $PS2_PROOF_JOBS sets the jobs (default: every core). Exit status:
# compare.py's (0 when everything matches the last run; a new route counts
# as a difference), 2 when the commit could not be read or built.
set -u
# Run from a git hook, git's own variables would point every git command
# below at the hook's repository instead of this clone.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE
T="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REF=${1:-HEAD}
OUT=${PS2_PROOFS:-$HOME/ps2_proofs}
JOBS=${PS2_PROOF_JOBS:-$(nproc 2>/dev/null || echo 4)}
mkdir -p "$OUT" || exit 2

exec 9> "$OUT/.lock"
flock 9

HASH=$(git -C "$T" rev-parse --short=12 "$REF^{commit}" 2>/dev/null) \
    || { echo "server_proof: $REF is not a commit in $T"; exit 2; }
NAME="c_$HASH"
W="$OUT/work"
rm -rf "$W"; mkdir -p "$W"
git -C "$T" archive "$HASH" | tar -x -C "$W" \
    || { echo "server_proof: git archive $HASH failed"; exit 2; }

LOG="$OUT/$HASH.txt"
{
    echo "commit $HASH ($(git -C "$T" log -1 --format='%cd %s' \
        --date=format:'%Y-%m-%d %H:%M' "$HASH"))"
    echo "jobs $JOBS, started $(date '+%Y-%m-%d %H:%M:%S')"
} > "$LOG"
PROOF_COLOUR=1 bash "$W/tools/proofs/proof.sh" "$NAME" "$JOBS" >> "$LOG" 2>&1
STATUS=$?

rm -rf "${OUT:?}/$HASH"
if [ -d "/tmp/proofs/$NAME" ]; then
    cp -r "/tmp/proofs/$NAME" "$OUT/$HASH"
    cp "/tmp/proofs/$NAME.times.txt" "$OUT/$HASH/times.txt" 2>/dev/null
fi
rm -rf "$W" "/tmp/proofs/$NAME" "/tmp/proofs/$NAME.times" \
       "/tmp/proofs/$NAME.times.txt"
if [ $STATUS -ne 0 ] || [ ! -f "$OUT/$HASH/routes_pass.txt" ]; then
    echo "$HASH  $(date '+%Y-%m-%d %H:%M')  FAILED to build or run: $LOG" \
        >> "$OUT/summary.txt"
    exit 2
fi

# Against the last run that finished, if there is one.
CMP=0
PREV=$(cat "$OUT/last" 2>/dev/null || true)
if [ -n "$PREV" ] && [ "$PREV" != "$HASH" ] && [ -d "$OUT/$PREV" ]; then
    echo "" >> "$LOG"
    echo "against $PREV:" >> "$LOG"
    python3 "$T/tools/proofs/compare.py" "$OUT/$PREV" "$OUT/$HASH" >> "$LOG" 2>&1
    CMP=$?
    AGAINST="against $PREV: $([ $CMP -eq 0 ] && echo same || echo DIFFERENT)"
else
    AGAINST="the first run"
fi
echo "$HASH" > "$OUT/last"

cd "$OUT/$HASH" || exit 2
echo "$HASH  $(date '+%Y-%m-%d %H:%M')  $(grep -c '^PASS' routes_pass.txt)" \
     "PASS of $(wc -l < routes_pass.txt) routes; colour" \
     "$(cat colour_check.txt 2>/dev/null || echo 'not run');" \
     "$AGAINST" >> "$OUT/summary.txt"
tail -1 "$OUT/summary.txt"
exit $CMP
