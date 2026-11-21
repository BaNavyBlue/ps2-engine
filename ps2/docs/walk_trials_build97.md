# The walks across Noah, build 97

`tools/walk_trials.py`: the two walks the last stages make from Lutz's YES,
many times per fight policy. Each walk's start save is made once by the
stage's own preparation -- Ryuon's Trimates (`boss_supplies`), the town,
the mansion's mending and Lutz's YES -- then the probe's `save-here` on
Noah's ground floor at 32,107, full HP and TP, 18 mates:

* the Mother Brain walk, from story_dark_force.json (build 96c's
  checkpoint), to the foot of Noah F1's corridor --
  `routes/saves/walk_mother_brain.json`;
* the Dark Force walk, from story_boss_party.json (Pandora's box not yet
  opened), to the cell below the box -- `routes/saves/walk_dark_force.json`.

The party is Rolf L41, Rudo L40, Amy L35, Kain L32 in both.

Each run is a fresh probe: CONTINUE the start save, wait `7 k` ticks (run
k), and `travel` to the walk's end with the policy's fight options and its
mending after each fight, as story.py's stages walk it. The RNG turns every
frame, so each offset meets different fights, and every policy is run on
the same offsets. At the end every policy gets the same arrival: Rever or a
Moon Dew on the dead, then mates (Res first) to 0.9 -- and `box_ready`
(nobody dead, HP 0.85 and TP 0.6 of the party's) decides whether the party
is fit for the boss. A run is about 0.2 s.

## The policies

| name | fights on the way | after each fight |
|---|---|---|
| b94 | weapons and mates (mates under 0.45), RUN once anyone alive is under 0.25 -- build 94's walk, and the Dark Force stage's until now | goto's old rule: mates to 0.6 once someone is under half; nobody revived |
| b94_norun | b94 without RUN | old rule |
| b96c | build 96c's WALK_FIGHT: heal techniques under 0.4, mates under 0.3, no damage techniques, no RUN | revive, cure poison, mend to 0.5 (WALK_MEND) |
| b96c_run | b96c, RUN under 0.25 | 0.5 |
| heal30, heal50 | b96c, heal techniques under 0.3 / 0.5 | 0.5 |
| mate20, mate45 | b96c, mates under 0.2 / 0.45 | 0.5 |
| mend30, mend70, mend_old | b96c | 0.3 / 0.7 / the old rule |
| w_mend50 | b94_norun | revive, cure poison, mend to 0.5 |
| w_mate30, w_mate60 | b94_norun, mates under 0.3 / 0.6 | old rule |
| w_mate60_mend50 | b94_norun, mates under 0.6 | 0.5 |

## The Mother Brain walk

The first eleven policies at 100 seeds, the next four added on the same
100, then the front-runners to 300 and the last two to 600. TP is the
mean at the corridor after the arrival's mending; "down / brought back"
counts members going down on the way and coming back on the way.

| policy | runs | reached | wiped out | gave up | box_ready | mates left (mean) | Rolf TP (mean) | Amy TP (mean) | Kain TP (mean) | dead on arrival (mean) | down / brought back (mean) | fights (mean) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| b94 | 300 | 283 | 17 | 0 | 230 | 11.8 | 74/146 | 119/135 | 107/107 | 0.16 | 0.4 / 0.0 | 6.1 |
| b94_norun | 600 | 587 | 13 | 0 | 478 | 11.5 | 79/146 | 120/135 | 107/107 | 0.13 | 0.2 / 0.0 | 6.0 |
| b96c | 300 | 290 | 10 | 0 | 195 | 15.9 | 71/146 | 71/135 | 107/107 | 0.06 | 0.3 / 0.1 | 5.7 |
| b96c_run | 100 | 96 | 4 | 0 | 54 | 14.7 | 65/146 | 67/135 | 107/107 | 0.20 | 0.5 / 0.1 | 6.0 |
| heal30 | 100 | 94 | 6 | 0 | 50 | 14.3 | 42/146 | 75/135 | 107/107 | 0.05 | 0.6 / 0.3 | 5.8 |
| heal50 | 100 | 97 | 3 | 0 | 58 | 14.8 | 79/146 | 49/135 | 107/107 | 0.03 | 0.2 / 0.1 | 6.3 |
| mate20 | 100 | 95 | 5 | 0 | 56 | 14.8 | 69/146 | 59/135 | 107/107 | 0.12 | 0.4 / 0.1 | 5.9 |
| mate45 | 100 | 95 | 5 | 0 | 70 | 13.8 | 82/146 | 74/135 | 107/107 | 0.05 | 0.3 / 0.1 | 5.9 |
| mend30 | 100 | 96 | 4 | 0 | 51 | 15.2 | 62/146 | 67/135 | 107/107 | 0.08 | 0.3 / 0.0 | 6.0 |
| mend70 | 100 | 97 | 3 | 0 | 53 | 15.3 | 42/146 | 79/135 | 107/107 | 0.09 | 0.3 / 0.1 | 6.4 |
| mend_old | 100 | 94 | 6 | 0 | 47 | 15.1 | 63/146 | 70/135 | 107/107 | 0.11 | 0.3 / 0.0 | 6.1 |
| w_mend50 | 600 | 583 | 17 | 0 | 470 | 11.2 | 82/146 | 116/135 | 107/107 | 0.05 | 0.2 / 0.1 | 6.1 |
| w_mate30 | 300 | 287 | 13 | 0 | 223 | 13.6 | 55/146 | 119/135 | 107/107 | 0.15 | 0.3 / 0.0 | 6.0 |
| w_mate60 | 100 | 96 | 4 | 0 | 78 | 7.8 | 93/146 | 119/135 | 107/107 | 0.07 | 0.2 / 0.0 | 6.0 |
| w_mate60_mend50 | 100 | 93 | 7 | 0 | 75 | 7.9 | 95/146 | 117/135 | 107/107 | 0.02 | 0.3 / 0.1 | 6.0 |

## The Dark Force walk

The same, about five fights to the box instead of six.

| policy | runs | reached | wiped out | gave up | box_ready | mates left (mean) | Rolf TP (mean) | Amy TP (mean) | Kain TP (mean) | dead on arrival (mean) | down / brought back (mean) | fights (mean) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| b94 | 300 | 290 | 10 | 0 | 238 | 12.8 | 81/146 | 122/135 | 107/107 | 0.16 | 0.3 / 0.0 | 4.9 |
| b94_norun | 600 | 584 | 16 | 0 | 501 | 12.5 | 85/146 | 125/135 | 107/107 | 0.05 | 0.2 / 0.0 | 4.9 |
| b96c | 300 | 295 | 5 | 0 | 224 | 16.2 | 79/146 | 85/135 | 107/107 | 0.05 | 0.2 / 0.1 | 4.7 |
| b96c_run | 100 | 98 | 2 | 0 | 65 | 15.2 | 71/146 | 86/135 | 107/107 | 0.10 | 0.3 / 0.1 | 5.1 |
| heal30 | 100 | 96 | 4 | 0 | 64 | 15.1 | 57/146 | 89/135 | 107/107 | 0.04 | 0.5 / 0.3 | 5.1 |
| heal50 | 100 | 98 | 2 | 0 | 66 | 15.0 | 84/146 | 65/135 | 107/107 | 0.05 | 0.2 / 0.0 | 5.1 |
| mate20 | 100 | 97 | 3 | 0 | 66 | 14.9 | 74/146 | 76/135 | 107/107 | 0.10 | 0.3 / 0.1 | 5.0 |
| mate45 | 100 | 99 | 1 | 0 | 79 | 13.9 | 84/146 | 87/135 | 107/107 | 0.02 | 0.2 / 0.1 | 5.1 |
| mend30 | 100 | 94 | 6 | 0 | 66 | 16.3 | 75/146 | 85/135 | 107/107 | 0.09 | 0.4 / 0.1 | 4.9 |
| mend70 | 100 | 100 | 0 | 0 | 66 | 15.4 | 58/146 | 87/135 | 107/107 | 0.10 | 0.2 / 0.1 | 5.1 |
| mend_old | 100 | 95 | 5 | 0 | 65 | 15.5 | 72/146 | 86/135 | 107/107 | 0.13 | 0.3 / 0.0 | 5.0 |
| w_mend50 | 600 | 598 | 2 | 0 | 493 | 12.1 | 88/146 | 120/135 | 107/107 | 0.04 | 0.1 / 0.0 | 4.9 |
| w_mate30 | 300 | 291 | 9 | 0 | 243 | 14.5 | 68/146 | 125/135 | 107/107 | 0.08 | 0.2 / 0.0 | 4.8 |
| w_mate60 | 100 | 99 | 1 | 0 | 85 | 9.6 | 98/146 | 123/135 | 107/107 | 0.12 | 0.2 / 0.0 | 4.9 |
| w_mate60_mend50 | 100 | 98 | 2 | 0 | 84 | 9.9 | 101/146 | 119/135 | 107/107 | 0.03 | 0.1 / 0.0 | 4.9 |

## What they say

**Weapons alone arrive fit more often.** Every weapons-alone policy
arrived fit 75-85 times in 100; every 96c variant 50-66 (Mother Brain) or
64-66 (Dark Force). 96c heals in a fight with Res where a Trimate would do,
and Amy reaches the corridor with 71 of 135 TP against about 120; the
weapons-alone walks spend mates instead (about 11-12 left against 15-16).

**RUN costs.** Build 94's walk against the same without RUN, 300 seeds:
wiped out 17 times against 7 on the Mother Brain walk (10 against 9 on the
Dark Force walk). Build 96c's single run blamed RUN for build 94's wipe;
over many seeds it is the pattern on the longer walk.

**Reviving after each fight helps.** Weapons alone with no RUN, with and
without the revive and mend to half after each fight, 600 seeds a walk:
the Dark Force walk wiped out 2 times against 16; the Mother Brain walk 17
against 13, which is within noise. Pooled, 19 against 29 in 1200 walks,
fit about as often (963 against 979).

**The wipe counts between 1 and 7 in 100 do not rank policies** on their
own: that is within what 100 seeds can tell apart. The rankings above rest
on the 300- and 600-seed rows.

## What was set

story.py's `WALK_FIGHT` is weapons and mates, no RUN
(`{"use_techs": False, "type_techs": (), "run_below": 0.0}`), and
`WALK_MEND` 0.5 (w_mend50), for both walks: the Mother Brain stage's
(`mother_brain_once`) and now the Dark Force stage's (`dark_force_once`),
which also revives at the box before its ready check as the Mother Brain
stage does at the corridor.

**Not measured here:** the boss fights. The walks that keep the TP arrive
with about four fewer mates, and how that trades against Mother Brain or
Dark Force is boss_trials' question (it deals its own mates). The stages
were rerun with the new policy; history.md's Build 97 has their reports.

    python3 tools/walk_trials.py --walk mother_brain --repeats 100
    python3 tools/walk_trials.py --walk mother_brain --first 100 \
        --repeats 200 --append --policy b94,b94_norun,w_mend50,w_mate30,b96c
    python3 tools/walk_trials.py --walk dark_force --repeats 100
