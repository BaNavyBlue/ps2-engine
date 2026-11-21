#!/bin/bash
# (build 128; /tmp/cp115/run_nk.sh of build 119) The 36 endgames of an
# endgame_setup.sh tree, from stage FROM's checkpoint (nei_sword unless
# FROM= says), at the watch pace, extra playthrough arguments after DIR
# (--no-kain, ...), JOBS at a time (12 unless JOBS= says; any count gives
# the same files). Summarise with summarize_endgames.py --tag=FROM.
#
#     [FROM=nei_sword] [JOBS=12] tools/measure/endgame_run.sh DIR [ARGS...]
set -u
D=${1:?dir}; shift
FROM=${FROM:-nei_sword}
JOBS=${JOBS:-12}
cd "$D" || exit 1
s=$EPOCHREALTIME
python3 tools/playthrough.py --seeds 1-12,101-112,201-212 --jobs "$JOBS" \
    --pace watch --from "$FROM" "$@" > seeds.out 2>&1
echo "$D: exit $? $(python3 -c "print(round($EPOCHREALTIME - $s))") s" > time.txt
