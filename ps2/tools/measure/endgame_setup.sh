#!/bin/bash
# (build 128; /tmp/cp115/setup_nk.sh of build 119) The endgame, 36 games a
# variant: a scratch tree whose seeds 1-12 have the checkpoints of a
# whole_games.sh run's seeds 1-12, and 101-112 and 201-212 the same ones
# again (other pauses, other fights). Then endgame_run.sh.
#
#     tools/measure/endgame_setup.sh DIR SOURCE      SOURCE: a whole-games DIR
set -eu
D=${1:?dir}; SRC=${2:?source run}
"$(dirname "${BASH_SOURCE[0]}")/scratch_tree.sh" "$D"
for s in 1 2 3 4 5 6 7 8 9 10 11 12; do
    for t in $s $((100 + s)) $((200 + s)); do
        mkdir -p "$D/route_out/playthrough_s$t"
        cp -r "$SRC/route_out/playthrough_s$s/checkpoints" \
            "$D/route_out/playthrough_s$t/"
    done
done
