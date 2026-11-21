#!/bin/bash
# (build 128; /tmp/cp115/run_exp.sh of builds 118-127) Seeds 1-12, whole
# games, in DIR, a tree scratch_tree.sh made: seeds.out, time.txt, and each
# seed's route_out/playthrough_sN/. Summarise with summarize_games.py.
# 7-15 minutes for the twelve at once on 12 cores (the watch pace); any
# JOBS gives the same files.
#
#     tools/measure/scratch_tree.sh DIR
#     tools/measure/whole_games.sh DIR [PACE] [JOBS]      PACE: probe, watch
set -u
D=${1:?dir}; PACE=${2:-probe}; JOBS=${3:-12}
cd "$D" || exit 1
s=$EPOCHREALTIME
python3 tools/playthrough.py --seeds 1-12 --jobs "$JOBS" --pace "$PACE" \
    > seeds.out 2>&1
echo "$D ($PACE): exit $? $(python3 -c "print(round($EPOCHREALTIME - $s))") s" \
    > time.txt
