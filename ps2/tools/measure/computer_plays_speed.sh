#!/bin/bash
# (build 128; /tmp/cp115/speeds.sh of builds 120-127) COMPUTER PLAYS
# headless through the window's own path, in a fresh scratch tree: the
# Escape menu at frame 400, six downs to COMPUTER PLAYS, C (its lamp), down
# to SPEED, C pressed PRESSES times from NORMAL (0 NORMAL, 1 AUTO, 2 2X,
# 3 AUTO 2X, 4 FAST), Escape (the computer starts). game.log, time.txt, and
# the run's files in DIR/saves/computer/.
#
#     tools/measure/computer_plays_speed.sh DIR PRESSES "DRIVER ARGS" [FRAMES]
#     e.g. ... /tmp/cp 4 "--to arima --seed 1"     (seed 1 to Arima, FAST)
set -u
D=${1:?dir}; N=${2:?presses}; ARGS=${3-}; FRAMES=${4:-400000}
"$(dirname "${BASH_SOURCE[0]}")/scratch_tree.sh" "$D"
cd "$D" || exit 1
X=""
[ -f ps2_headless.exe ] && X=".exe"
PAD="400:esc,410:down,420:down,430:down,440:down,450:down,460:down,470:c,480:down"
f=490
for i in $(seq 1 "$N"); do PAD="$PAD,$f:c"; f=$((f + 10)); done
PAD="$PAD,$f:esc"
s=$EPOCHREALTIME
PS2_PAD="$PAD" PS2_COMPUTER_ARGS="$ARGS" PS2_COMPUTER_QUIT=1 \
    PS2_FRAMES=$FRAMES "./ps2_headless$X" > game.log 2>&1
echo "exit $? in $(python3 -c "print(round($EPOCHREALTIME - $s, 1))") s" > time.txt
