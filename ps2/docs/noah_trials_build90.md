# Kain and Hugh before Noah, build 90

`tools/dam_trials.py` from routes/saves/story_nei_sword.json (Ryuon, the Nei
Sword on Rolf, the regulars in their Nei items). Each run: `members`, each
member raised to the level its own EXP reaches after G fights at 3459 EXP a
member (NoahGroundF's group 82: 13,836 EXP a fight on average over its eight
formations, split four ways), Kain, Hugh and Shir outfitted with the best
one-hand weapon twice from every store (`--stores all --outfit
Kain,Hugh,Shir`: the others keep the save's gear), 8 Trimates dealt round,
`warp` to NoahGroundF 32,107 (where Lutz's YES lands), `play.wander` for 20
fights with fight_smart. Fights per trip: fights won before a wipe or before
someone is down or under half HP with nothing left to mend, capped at 20.

Noah's enemies are 97% type `$60` by HP (dungeon_mix of `$63`/`$64`), which
both Kain's and Hugh's techniques touch (loc_27AA).

G fights at 3459 EXP take Kain and Hugh (L10, L13) to: 40 -> 20, 80 -> 24,
160 -> 28, 250 -> 31/32. The regulars (L35-37) gain one to four.

## All ten parties, 10 runs each

`--grind 0,80,250 --fights 20 --repeats 10`

| grind fights | party (Rolf +) | levels | cleared 20 | fights per trip (mean) | wiped |
|---|---|---|---|---|---|
| 0 | Rudo+Amy+Anna | 37/36/35/36 | 2/10 | 10.1 | 0/10 |
| 0 | Rudo+Hugh+Anna | 37/36/13/36 | 0/10 | 7.5 | 0/10 |
| 0 | Amy+Anna+Kain | 37/35/36/10 | 0/10 | 3.6 | 0/10 |
| 0 | Rudo+Anna+Kain | 37/36/36/10 | 0/10 | 3.4 | 0/10 |
| 0 | (the other six) | | 0/10 | 1.4-2.4 | 0/10 |
| 80 | Rudo+Hugh+Anna | 38/38/24/37 | 2/10 | 13.7 | 0/10 |
| 80 | Rudo+Anna+Kain | 38/38/37/24 | 1/10 | 12.5 | 0/10 |
| 80 | Rudo+Amy+Anna | 38/38/36/37 | 0/10 | 11.7 | 0/10 |
| 80 | Amy+Hugh+Anna | 38/36/24/37 | 0/10 | 10.8 | 1/10 |
| 80 | (the other six) | | 0/10 | 5.1-7.9 | 0-2/10 |
| 250 | Rudo+Anna+Kain | 41/40/40/32 | 7/10 | 18.1 | 0/10 |
| 250 | Rudo+Hugh+Anna | 41/40/31/40 | 5/10 | 17.1 | 0/10 |
| 250 | Rudo+Amy+Anna | 41/40/39/40 | 4/10 | 15.7 | 0/10 |
| 250 | Amy+Anna+Kain | 41/39/40/32 | 3/10 | 13.7 | 0/10 |
| 250 | (the other six) | | 0-2/10 | 8.1-14.2 | 0-2/10 |

## The four leaders, 30 runs each

`--grind 40,80,160,250 --repeats 30 --triples
Rudo+Amy+Anna,Rudo+Anna+Kain,Rudo+Hugh+Anna,Rudo+Hugh+Kain`

| grind fights | party (Rolf +) | levels | cleared 20 | fights per trip (mean) | wiped |
|---|---|---|---|---|---|
| 40 | Rudo+Amy+Anna | 38/37/36/37 | 6/30 | 12.0 | 0/30 |
| 40 | Rudo+Anna+Kain | 38/37/37/20 | 2/30 | 11.0 | 0/30 |
| 40 | Rudo+Hugh+Anna | 38/37/20/37 | 1/30 | 9.0 | 0/30 |
| 40 | Rudo+Hugh+Kain | 38/37/20/20 | 0/30 | 7.8 | 2/30 |
| 80 | Rudo+Amy+Anna | 38/38/36/37 | 10/30 | 13.2 | 0/30 |
| 80 | Rudo+Hugh+Anna | 38/38/24/37 | 2/30 | 12.0 | 0/30 |
| 80 | Rudo+Anna+Kain | 38/38/37/24 | 3/30 | 11.9 | 1/30 |
| 80 | Rudo+Hugh+Kain | 38/38/24/24 | 0/30 | 7.7 | 2/30 |
| 160 | Rudo+Anna+Kain | 39/39/39/28 | 11/30 | 15.7 | 1/30 |
| 160 | Rudo+Amy+Anna | 39/39/38/39 | 8/30 | 13.8 | 0/30 |
| 160 | Rudo+Hugh+Anna | 39/39/28/39 | 3/30 | 13.0 | 0/30 |
| 160 | Rudo+Hugh+Kain | 39/39/28/28 | 2/30 | 11.8 | 2/30 |
| 250 | Rudo+Anna+Kain | 41/40/40/32 | 22/30 | 18.5 | 0/30 |
| 250 | Rudo+Amy+Anna | 41/40/39/40 | 17/30 | 16.3 | 0/30 |
| 250 | Rudo+Hugh+Anna | 41/40/31/40 | 10/30 | 17.1 | 0/30 |
| 250 | Rudo+Hugh+Kain | 41/40/31/32 | 2/30 | 13.6 | 1/30 |

## What it says

Raising Kain pays on Noah, but not at once. At the same grind, Rudo+Anna+Kain
is behind Rudo+Amy+Anna up to 80 fights (Kain L24) and ahead from 160 (L28:
15.7 to 13.8, 11 to 8 cleared) and 250 (L32: 18.5 to 16.3, 22 to 17). Hugh in
Amy's place never clearly beats her over 30 runs; the 10-run table's 80-fight
lead (13.7) did not hold (12.0). Hugh and Kain together is the worst of the
four at every grind.

What the trial leaves out: levels_after gives every member the share every
fight, but a member who dies in a fight gets none (loc_F2A4 splits by the
living), and at 0 grind Kain's parties manage 3.4 fights a trip -- the first
~40 fights with Kain along cost two to three times the trips. Raising him to
~20 somewhere cheaper first (play.raise_member) avoids that. His gear here is
the best one-hand weapon sold anywhere, twice; the Nei items he could wear
(Neimet, NeiArmor, NeiShield: equip masks $41, $44, $50 include Kain's bit $40)
stay on the regulars. The runs share one probe session, so the RNG runs on
between them; build 88's same-logic rows put the noise near 1.5 fights a trip
at 30 runs.
