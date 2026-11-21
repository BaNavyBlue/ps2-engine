# The bosses as the walks leave the party, build 97b

Build 97 set the Noah walks to weapons and mates with no RUN, mending to half
after each fight (walk_trials' `w_mend50`), and left one question open: that
walk arrives with about four fewer mates than build 96c's healing walk
(`b96c`). This measures what the mates cost at the two bosses.

**How.** `tools/boss_trials.py --walk-policies b96c,w_mend50` (new in 97b):
each fight starts from the walk's own start save
(`routes/saves/walk_<boss>.json`: Noah's ground floor, the party Rolf L41,
Rudo L40, Amy L35, Kain L32, full HP and TP, 18 mates) with no refill, the
save's Trimates cut to the arrival's count in the order the walk spends them
(the first living holder's first: Rolf's, then Rudo's, ...) and each
member's TP set. The arrival is the mean over that policy's **box_ready**
walks in walk_trials' results -- the walks the stage takes into the fight;
the others walk again. (The table in walk_trials_build97.md averages every
walk that reached the end; the gap in mates is the same, four or five.)
`--full-tp` deals the mates alone. Fight policy `both` (fight_smart's
default); 500 fights a row, the two arrivals interleaved.

| boss | arrival | mates | TP dealt | won |
|---|---|---|---|---|
| Mother Brain | b96c | 18 | Rolf 90, Amy 92, Kain 107 | 373/500 |
| Mother Brain | w_mend50 | 13 | Rolf 97, Amy 135, Kain 107 | 398/500 |
| Mother Brain | b96c, TP full | 18 | full | 463/500 |
| Mother Brain | w_mend50, TP full | 13 | full | 439/500 |
| Dark Force | b96c | 18 | Rolf 96, Amy 102, Kain 107 | 203/500 |
| Dark Force | w_mend50 | 14 | Rolf 103, Amy 134, Kain 107 | 186/500 |
| Dark Force | b96c, TP full | 18 | full | 191/500 |
| Dark Force | w_mend50, TP full | 14 | full | 184/500 |

(route_out/bt97b_<boss>[_fulltp].md and .json.)

## What they say

**The mates alone cost something at Mother Brain.** With TP held equal, five
mates fewer won 24 fewer fights in 500 (92.6% against 87.8%). Against Dark
Force the mates alone are within noise (191 against 184); winning fights
there left 17.3 of 18 and 13.7 of 14 mates, and fight_smart reaches for a
mate only when no heal technique can be ordered.

**The arrival as a whole favours the weapons walk at Mother Brain.** It keeps
Amy's TP (135 against 92), which is worth more than the five mates: 398
against 373. At Dark Force 96c's arrival did better by 17 in 500 -- within
noise (about 3 points each side), and not the mates' doing (the mates alone
differ by 7).

**With the walks counted.** A stage walks up to four times until it arrives
fit (story.py's BOX_TRIES), so a try reaches the boss fit with probability
r(1 + n + n^2 + n^3) -- r fit, n neither fit nor wiped, per walk
(walk_trials, 300 and 600 seeds):

| | reaches the boss fit | wins the fight | a try wins |
|---|---|---|---|
| Mother Brain, b96c | 0.942 | 0.746 | 0.703 |
| Mother Brain, w_mend50 | 0.964 | 0.796 | 0.767 |
| Dark Force, b96c | 0.975 | 0.406 | 0.396 |
| Dark Force, w_mend50 | 0.995 | 0.372 | 0.370 |

**Kept.** The fewer mates do not lose more boss fights than the walk saves:
ahead at Mother Brain, level at Dark Force within what 500 fights can tell.
WALK_FIGHT, WALK_MEND and boss_supplies stay as build 97 set them, and the
two stages were not rerun.

    python3 tools/boss_trials.py --boss mother_brain \
        --save routes/saves/walk_mother_brain.json --candidates Rudo,Amy,Kain \
        --walk-policies b96c,w_mend50 --repeats 500 --out route_out/bt97b_mother_brain
    (the same with --full-tp, and for dark_force with walk_dark_force.json)

A Mother Brain fight is about 0.2 s here; the 2000 fights of one boss take a
few minutes (in the background: a tool call stops at 300 s).
