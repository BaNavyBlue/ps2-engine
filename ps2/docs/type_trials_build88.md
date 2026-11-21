# Techniques by enemy type, build 88

`tools/dam_trials.py`, 30 runs of 40 fights a party, from routes/saves/story_climatrol.json (mended first).
`--type-techs none` is build 87's fight_smart (no damage technique ordered); `Kain,Hugh` is build 88's plan_type_tech.
Fights per trip: fights won before a wipe or someone under half HP with nothing left to mend, capped at 40, mean of 30.

## Red Dam F2 (group 52: robots only), 150 grind fights

`python3 tools/dam_trials.py --grind 150 --repeats 30 --fights 40 --type-techs {none,Kain,Hugh}`

| party (Rolf +) | levels | 87b | wiped | 88 | wiped |
|---|---|---|---|---|---|
| Rudo+Amy+Anna | 23/22/18/20 | 21.2 | 0/30 | 19.7 | 0/30 |
| Rudo+Hugh+Anna | 23/22/16/20 | 19.8 | 0/30 | 18.6 | 0/30 |
| Rudo+Anna+Kain | 23/22/20/15 | 17.4 | 0/30 | 18.0 | 0/30 |
| Amy+Anna+Kain | 23/18/20/15 | 11.8 | 1/30 | 16.9 | 0/30 |
| Amy+Hugh+Anna | 23/18/16/20 | 16.8 | 1/30 | 16.7 | 1/30 |
| Hugh+Anna+Kain | 23/16/20/15 | 12.7 | 0/30 | 15.9 | 1/30 |
| Rudo+Amy+Hugh | 23/22/18/16 | 11.8 | 1/30 | 14.8 | 0/30 |
| Rudo+Hugh+Kain | 23/22/16/15 | 12.4 | 0/30 | 13.5 | 0/30 |
| Rudo+Amy+Kain | 23/22/18/15 | 11.3 | 0/30 | 13.2 | 1/30 |
| Amy+Hugh+Kain | 23/18/16/15 | 7.0 | 0/30 | 11.0 | 0/30 |

## Crevice ground floor (group 64: 87% biomonsters by HP), 450 grind fights

`python3 tools/dam_trials.py --grind 450 --repeats 30 --fights 40 --floor 0x16 --cell 30,42 --type-techs {none,Kain,Hugh}`

| party (Rolf +) | levels | 87b | wiped | 88 | wiped |
|---|---|---|---|---|---|
| Rudo+Amy+Anna | 24/24/20/22 | 9.4 | 5/30 | 10.7 | 4/30 |
| Rudo+Anna+Kain | 24/24/22/18 | 10.1 | 7/30 | 10.1 | 4/30 |
| Rudo+Hugh+Kain | 24/24/18/18 | 7.7 | 6/30 | 9.3 | 5/30 |
| Rudo+Hugh+Anna | 24/24/18/22 | 9.1 | 4/30 | 9.3 | 5/30 |
| Rudo+Amy+Hugh | 24/24/20/18 | 9.4 | 7/30 | 8.7 | 12/30 |
| Rudo+Amy+Kain | 24/24/20/18 | 7.8 | 5/30 | 7.6 | 3/30 |
| Hugh+Anna+Kain | 24/18/22/18 | 3.8 | 4/30 | 6.0 | 5/30 |
| Amy+Hugh+Kain | 24/20/18/18 | 5.6 | 8/30 | 5.7 | 3/30 |
| Amy+Hugh+Anna | 24/20/18/22 | 4.4 | 9/30 | 5.3 | 5/30 |
| Amy+Anna+Kain | 24/20/22/18 | 4.0 | 6/30 | 4.6 | 6/30 |

Rudo+Amy+Anna runs the same logic in both columns (neither Kain nor Hugh), so its
difference -- 21.2 to 19.7 on Red Dam F2, 9.4 to 10.7 on Crevice -- is the noise: the runs share
one probe session, and the RNG runs on from one run to the next.

Climatrol F5 with `$C737` cleared (`--late 0`, group 35, all biomonsters) was tried first and
is too easy for these levels: every party cleared 20 fights nearly every time, either way.
