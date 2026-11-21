# COMPUTER PLAYS in C (build 170 on): the plan

The owner, after build 169a: "Take inspiration from the PS1 build and
re-write computer play in C". Until now the window's COMPUTER PLAYS is the
harness's Python player (tools/computer_plays.py over play.py and
story.py, about 9,800 lines) in a child process, its commands sent down a
pipe to the game's probe interpreter (docs/computer_plays.md, "The
shape").

## The PS1 port's way

The PS1 port's COMPUTER PLAYS (its builds 74-77) is its test grind,
`tools/grind.c` (about 11,000 lines of C), compiled into the game with
GRIND_NO_MAIN, so what `build/grind -s SEED` proves is what the window
plays. It runs on a "fiber" (a thread handed turns, one side at a time),
ticks the game itself, and after every tick calls the host, which yields
the frame back when it has its ticks. It reads the game's structs
directly and gives input only through `game_tick`'s pad. Its proof: seed
1 in the window at FAST, AUTO and AUTO 2X, 54,605 notes line for line
the tool's, the ending's title at the same tick.

## This port's way

Taken from PS1's:
- One C player, compiled into the game and into a headless tool for the
  whole games, as `build/grind` is: nothing at run time but the game -- no
  Python, no child process, no pipe.
- Deterministic per seed, with its own small generator.
- One side runs at a time. This port has that already: the command thread
  and the main thread hand the ticks over (autoplay.c, `autoplay_hold` and
  `autoplay_release`), so the speeds, the story's beats, the Escape menu
  and handing back stay as they are. The C player runs where the Python's
  commands run now.

- `make computer_check`, as PS1's: a seed through the window's own path
  (the headless game, the Escape menu, FAST, AUTO and AUTO 2X) plays the
  standalone tool's game -- the same commands, checkpoints and report.

Kept from this port's:
- The verbs: the player gives the probe's commands (`walk`, `press`,
  `wait ...`) to the interpreter in-process, so they do what the routes
  prove, and every command can be set against the Python's.

## The proof: the Python's games (The owner's choice)

The C player plays the Python player's games. Each build ports a group of
the story's stages, and for seeds 1-12 in the three languages, up to the
stages ported, the C player gives the same commands at the same ticks as
the Python, with the same checkpoints and saves. A slip shows at the
first command that differs. The Python stays afterwards as the reference
and for the trial tools (The owner's choice): boss_trials, in the
proof set, fights with play.py. (build 180a) Asked again once the window
ran the C player: "keep both as references" -- the Python driver and
playthrough.py stay, and a change to the computer's game goes into both.

The story log (story.txt) was not to be a target: its lines are Python's
printing of its values. (build 171) The C player prints them the same, so
it is held to it as well.

## Step 0 (build 170): the Python's games made portable

Two things in the Python's games could not be done the same in C:
- The pauses' stream: random.Random(name), CPython's Mersenne Twister
  seeded through a SHA-512 of the name. Now `play.Jitter`: FNV-1a of the
  name seeds a SplitMix64, drawn in C the same.
- Choices that took the first of equals from a set: a set's order is
  CPython's hash table's, by the hash seed and the Python version for
  strings (PYTHONHASHSEED=0 pins the seed, not the version: 3.8 and 3.14
  walk the same set of item names in different orders) and by the
  table's layout for numbers. Each now has an order of its own (docs/
  history.md, Build 170, has the list).

Every seed plays a new game from 170, so the baselines are taken again.

## Notes for the port

From build 170's audit of play.py, story.py, computer_plays.py and
playthrough.py (every function read):
- Python's sort is stable and C's qsort is not. These sort with keys that
  can tie and keep the list's order among equals: motavia_components'
  `out.sort(key=len, reverse=True)`, plan_gear's `options.sort` and
  `pool[name].sort`, town_exit's `dirs.sort`, make_room, cast_res,
  choose_party's `ranked`, the revival tallies. A stable sort, or the
  index as the last key.
- `min`/`max` with a key keep the first of equals: scan in the same order
  with a strict comparison.
- LevelMap.path's heap holds (cost, (x, y)), every entry distinct, pushed
  up, down, left, right: any priority queue on (cost, x, y) pops the same.
- Floats decide (HP fractions against the run, mate and heal lines,
  technique damage, member_value's weights, plan_gear's gain over cost):
  the same operations in the same order. No `math.*`.
- Sets that are only tested (`in`, len, algebra then a test) need no
  order. computer_plays' story_flags mixes numbers and strings and is only
  compared as sets.
- `is` tests stand for flags: `cells is goals` (plan()'s last leg), `rp is
  not cp` (a try resumed from a save on the way).
- Strings sort by code point: strcmp on UTF-8 is the same.

## Step 1 (build 171): the foundation, with Paseo and Arima

The C player is src/player/, about 9,200 lines, built into route_probe:
`route_probe --play [playthrough.py's arguments]` plays playthrough.py's
game, writing what it writes under route_out/<base>/ (report.txt,
story.txt, checkpoints/, probe/). The base is playthrough_s<seed>[_<lang>]
as the Python's, or `--base NAME` (route_out/ holds the uploads' whole
games under playthrough_s*/: a check names its own). Each command goes to
the probe's interpreter in-process (probe_run_text, with route_probe's
line counter), and the log's lines come back to the player as route_probe
-i's stdout carried them to the Python, so the log.txt is the same file
line for line.

| File | The Python's |
|---|---|
| player.h | what a host sees: PlayerIO, the session, the arguments, the run |
| pl.h | the player's own types and functions, under the Python's names |
| util.c | lists, the insertion-ordered set (a Python set or dict's order), files |
| session.c | play.Session: send, the state line (STATE_RE, MEMBER_RE), the pauses (Jitter), downs and revivals, the victory pages, the new game, menu_pick |
| map.c | LevelMap and path, goto, the doors, holes and ways out, plan, travel, the towns' exits |
| town.c | visit and its policies: the shops, the hospital, Data Memory, the clone lab, Rolf's house; the C menu (equip, use_item, give_item, use_tech) |
| gear.c | items.json and the stores, bags and hands, plan_gear and gear_up, surplus, the mates, the map techniques, cast_res, mend, revive, cure_poison, resupply, retreat |
| fight.c | fight_smart, the battle's menus, `techs`, battle.json's enemies and formations, plan_heal, plan_deban, plan_shift, plan_type_tech |
| ground.c | encounter_group, Ground, roam, roam_and_gear, trip_save, lean_short |
| names.c | script_name: script_ids.h's names, then gen_script_ids.py's from the text |
| story.c | story.py's Paseo and Arima stages, the walk to Arima, the top-ups |
| run.c | playthrough.py's loop: the stages' tries, the checkpoints, resume points, the report |

A harness error -- the Python's KeyError, IndexError or ValueError, which
loses the try -- is `pl_error`, a jump back to the try's loop (its unwind
sends `@grind off` and `@gear off` as the Python's `finally`s do). A stage
not ported yet is one: `NotPorted: stage oputa`, so `--to arima` is as far
as this build goes. Inside the stages ported, what no game to Arima does
is a NotPorted too: the jet scooter (travel and retreat while riding),
`geared` (until_geared), the other stages' top-ups. stock_mates takes
Arima's arguments only (four, no Trimates), and revive has no
spend_reserve. plan_type_tech, plan_deban and plan_shift are ported but
unproved: they run against a boss or for Kain and Hugh.

**The proof**: `python3 tools/c_player_check.py --scratch DIR` (after
`make route_probe`) plays seeds 1-12 in us, retranslation and ja to Arima
at the watch pace with both players in a scratch tree, and compares:
36 of 36 the Python's -- the probe's log.txt line for line, every
checkpoint byte for byte, and also story.txt, the summary and the report
less its seconds (the story log matched too, so it is held to it). Six of
the games under ASan and UBSan (CLANG64's `make route_probe_asan`): clean,
the same logs. A game to Arima takes the C player 3-9 s and the Python
67-69 s, on the same machine, 32 games at once.

What the port found in the Python, for the stages to come:
- `s.state` is a new dict at each send, so `st = s.state` holds the state
  as it was: fight_smart's round reads its party and formation from the
  state at the round's top, plan_gear, find_item, mend and worn_head read
  the party from before their `bags`. The C copies the State there.
- `for i, m in enumerate(known)` walks the list it began with when the
  body rebinds `known = techs(s) or known`: the C walks a copy.
- weapon_blow's `s._worn` (the bags, read again once a fight is fought)
  lasts across tries: Session.worn_bags.
- A failed try's `secs` and a row's are measured when the stage returns,
  before the checkpoint.
- `sum()` of floats in CPython 3.12 on is compensated (Neumaier); the C
  adds plainly. Only plan_type_tech sums floats (a technique's value over
  several enemies) -- to be matched when it is first reached.
- The story log prints Python's values: a state as its dict (travel's
  "off the map"), None, the script names (scripts_text). names.c makes
  the names gen_script_ids.py makes for the ids script_ids.h leaves out.

## Step 2 (build 172): the gear to Amy

Six more stages: gear (Arima's shelf and Paseo's, round by round), shure
(F3's body and the Small Key, then every floor's chests), nido (the gear
again, the Dynamite on the door, F1's chests, Teim's scene), darum (the
tube's scene), oputa and amy (Rolf's house for the fourth, and Oputa's
grind until its shelves hold nothing better). The player is about 10,400
lines; what they stand on:

| File | Added |
|---|---|
| things.c (new) | talk, neighbours, search, level_objects (level_sprites.json), chests, chest_item (chests.json), flag_byte, chest_shut, make_room |
| ground.c | open_walk, wander, grind_and_gear (the little square by the gate); roam_and_gear's `until_geared` |
| gear.c | `geared`: plan_gear's `unlimited` (every upgrade whatever the money or the room, no log line) |
| town.c | toss_item |
| story.c | shut_again, loot (LOOT_WIPES, the retreats, the wanted chests gone back to), the six stages, the top-ups of gear (Arima's ground) and of oputa and amy (Oputa's grind) |

A stage past amy is still `NotPorted: stage lab_in`. Ported and not yet
reached by any of the 36 games, so unproved: make_room and toss_item (a
find with no room, $1704), shut_again and loot's CONTINUE (a wipe-out on
a floor of chests), loot's two retreats and its `tired`, Oputa's top-up,
amy's "four in the party already", grind_and_gear's `spot` and `lowest`.
Twelve more games (seeds 13-24 in us) did not reach them either. Each is
written from the Python's line for line; the stages after (the lab's
chests, the Prism dungeons' Nei items) are where they will run.

**The proof**: `c_player_check.py --scratch DIR --to amy`, seeds 1-12 in
the three languages: 36 of 36 the Python's (log.txt 226,735 to 284,280
lines a game, 31,312 fights in all; story.txt, the summary, the report
and the checkpoints), on the first run -- seed 1 alone first, then the
36. Six tries failed and were retried as the Python's were (one at
paseo, three at arima, two at nido, wiped out; each
arima and nido retry after its top-up, Paseo's ground and the gear
stage's Arima ground). Seeds 13-24 in us (`--seeds 13-24
--languages us`): 12 of 12. Six games under ASan and UBSan
(seeds 1, 4 us; 7, 10 retranslation; 3, 12 ja): nothing reported, the
same log.txt as the plain build's. Seed 1 to Amy: the C player 12 s, the
Python 37 s, side by side; with 36 of each on 32 jobs, 21-31 s and 49-86
s.

## Step 3 (build 173): the lab to the gum

Nine more stages: lab_in (the lab's door blown with the Dynamite, then
the grind on its ground floor from Oputa until everyone is L17, L15
squeaking by), lab (F1's and F2's chests, F2's door, the Recorder in
B1) and lab_b1 (B1's chests), hugh (Rolf's house for the fifth, the
Recorder to the Governor for the Key Tube), key_tube (the party chosen,
the tube's locked door), zema (Zema's shelves, then its field until the
one catching up is within five levels of the others), roron (every floor's chests, the scooter's maker on F5), uzo (on
the jet scooter to Uzo's island, the Maruera tree, back to Kueri for the
gum) and gum (the gum facing the ocean floor's spot, the Underwater
Passage, Climatrol's ground floor). The player is about 12,450 lines;
what they stand on:

| File | Added |
|---|---|
| party.c (new) | roster, member_value (no dungeon mix yet: tech_power is 0), choose_party (the core, then by value, the one catching up), pickable (Nei while $C737 is clear), regroup_stops, pick_party |
| scooter.c (new) | the scooter's map (jet_scooter.collision.txt: open over water, its 2x2 footprint, the doors it rides into), walk_region, landing_region, ride (fights on the way, mend when hurt), ride_ashore, ride_to_face, board_scooter |
| ground.c | dungeon_grind (a dungeon's floors, trip by trip, from a town) |
| things.c | the level's objects in the file's order (pixel cells), event_block and facing_cells, search_event, objects (the probe's live slots), talk_to (a person by dialogue number, waited for while they walk) |
| town.c | rolf_house_regroup (the house's Reorganize) |
| gear.c | resupply public; retreat while riding: ashore first, on the ground the town's exit lands on |
| map.c | town_exit_to; travel while riding: ashore first, on a landing that leads to the target |
| story.c | the nine stages (blow_door, resupply_at, lab_ground's anchors, the Governor's and the inventor's YES), loot's per-chest scripts, the top-ups of lab_in (the lab's ground) and of zema (Zema's field) |

A stage past gum is still `NotPorted: stage climatrol`. Ported and not
yet reached by any of the 36 games, so unproved: travel while riding (the
Underwater Passage's pipes and the dams come later), retreat's "could not
get ashore", ride's give-ups (not riding, no way on the water, forty
tries), blow_door with no Dynamite or short of the door, B1's chests left
unreached, the scooter maker's second try and F5 not reached, talk_to's
"not here", uzo's "hurt with nothing left", gum's "could not face",
board_scooter's "not known" and "could not reach the scooter",
dungeon_grind's "could not reach", pick_party's "could not get to Paseo",
the lab's and Zema's grinds giving up. Of build 172's list these games
reached loot's CONTINUE and shut_again (two games), its wipe-out on the
way to the last chest (one) and its retreat on the way (five),
grind_and_gear's `lowest` (Zema's, every game); still not make_room and
toss_item, loot's `tired` and its retreat after a chest, Oputa's top-up,
amy's "four in the party already", grind_and_gear's `spot`.

Found on the way: the C's `same_names` (pick_party's "is it the party
already") compared the sorted names as 16-byte blocks, and what lies past
a name's NUL is the stack's: equal parties could read as different, and
seed 1's gum went to Paseo to regroup into the party it had. It compares
the names now.

**The proof**: `c_player_check.py --scratch DIR --to gum`, seeds 1-12 in
the three languages: 36 of 36 the Python's (log.txt 609,960 to 739,265
lines a game, 74,087 fights in all; story.txt, the summary, the report
and the checkpoints), every game through gum. Seed 1 alone first: the
same through roron, then the gum's regroup above; with `same_names`
fixed, seed 1 and then the 36 on their first run. 27 tries failed and
were retried as the Python's were: 17 at uzo, where Anna comes along at
L1 to catch up (most of them wiped out on Uzo's island before the
scooter, the rest at the tree, on the water or short of Kueri; seed 6
in retranslation four times running), 3 at arima, 2 at nido, 2 at
key_tube, one each at paseo, lab and gum. The retries after them ran the new top-ups: Zema's
field 18 times (3 to 12 trips), the lab's ground 3. c_player_check's
report mask takes a digit in a stage's name now (lab_b1). Seeds 13-24 in
us: 12 of 12. Six games under ASan and UBSan (seeds 1, 4 us; 7, 10
retranslation; 3, 12 ja): nothing reported, the same log.txt as the
plain build's (the same_names slip is a read of bytes that were never
written, which neither sanitizer looks for). The 36 on 32 jobs: the C player 53-93 s a game, the Python 125-197 s; seed 1
alone, the C 30-39 s.

## Step 4 (build 174): Climatrol to the roof

Six more stages: climatrol (the party Rolf, Nei, Rudo and Anna; from
Kueri by the scooter and the gum each time, F2's, F3's, F5's and F6's
chests once, the grind on the passage's landing by its pipe until Rolf,
Rudo and Anna are at their levels -- a wipe-out CONTINUEs Kueri's save
-- then F7, Neifirst's two fights and the death of Nei to the clone
lab), dam_party (the Governor's flags, Rolf's house for Rudo, Amy and
Anna, a save), cards (Ustvestia's Musik in Oputa, the scooter to Piata,
Control Tower F1's keyboard and its four boxes), dam_grind (Piata's
field to dam_trials' levels, Trimates and Escapipes), dams (each dam by
the scooter, the card on its door and on its panel, out by Escapipe when
hurt; the Army Eyes, Gaira's prison with RUN alone, home) and roof (the
Central Tower's ROOF and the flight to Dezolis). The player is about
13,700 lines; what they stand on:

| File | Added |
|---|---|
| party.c | pick_party_want: pick_party's `want` (the caller's party) |
| scooter.c | ride_into (to a door record the scooter pushes into, a dam) |
| gear.c | stock_mates' `want` and `trimates`; stock_item (any item into whichever bags have room) |
| ground.c | wander and trip_party public |
| run.c | the unwind drops s.fight_opts (escape_gaira's `finally`) |
| story.c | the six stages; to_climatrol, back_to_climatrol, climatrol_grind, face_neifirst; to_piata, learn_musik, play_keyboard; open_dam, leave_dam, out_of_dam, army_eyes_to_gaira, escape_gaira, piata_supply; squeak-by's CLIMATROL_TARGET and DAM_GRIND_TARGET; the top-ups of climatrol (the landing's grind) and dam_grind (Piata's field) |

A stage past roof is still `NotPorted: stage skure`. These games reached
much of what is new: climatrol_grind's CONTINUE after a wipe-out (four
games), its "could not reach the landing" (four) and its lean (sixteen),
to_climatrol's "no gum", Neifirst offering no fight, retreat's walk out
failing, a card gone before its door or panel (five), a dam's trip hurt
and out by Escapipe (fifteen), leave_dam failing, dam_grind's lean, the
dams' CONTINUE after a wipe-out (recover, nine games), and both new
top-ups. Ported
and not reached, so unproved: climatrol_grind's "no way back in", "lost"
and "stuck on the way out", Nei winning her fight, the debrief not done,
learn_musik, play_keyboard, the cards and to_piata failing, open_dam's
"not carried", its boarding and riding in failing, a door that does not
open, leave_dam's walk out succeeding, the Army Eyes not carrying the
party off, Gaira's panel not reached, dam_grind's three "could not"s,
the roof not landing. stock_item's 64-purchase bound is the C's own
(the Python's list has none; no call asks for more than twelve).

**The proof**: `c_player_check.py --scratch DIR --to roof`, seeds 1-12 in
the three languages: 36 of 36 the Python's, every game through roof
(log.txt 1,082,360 to 1,424,841 lines a game, 113,213 fights in all;
story.txt, the summary, the report and the checkpoints). Seed 1 alone
first, then the 36, each on its first run. 55 tries failed and were
retried as the Python's were: 26 at climatrol (23 of them at F7, lost
to Neifirst), 17 at uzo, 3 at arima, 2 each at nido, key_tube and dams,
one each at paseo, lab and gum; the climatrol and dams retries after the
new top-ups (the landing's grind 26 times, Piata's field 2). Seeds 13-24
in us: 12 of 12. Six games under ASan and UBSan (seeds 1, 4 us; 7, 10
retranslation; 3, 12 ja): nothing reported, the same log.txt as the
plain build's. The 36 on 32 jobs: the C player 98-176 s a game, the Python 267-482 s;
seed 1 side by side, 80 s and 160 s.

## Step 5 (build 175): Skure to Zosa's grind

Skure and the Prism's dungeons were to be one step; they are two. This
one is Dezolis up to the Prism and the grind before its dungeons, four
stages: skure (Skure B1's chests from where the flight lands, the Magic
Cap on Rolf, B1's musk cats, Zosa's shelves at the Magic Cap's double
prices and a grind, B2's chests and cats, the shelves again in the
Mogic Cap, every Zosa Dezolian talked to), dezolis_towns (Aukba and
Ryuon as the standing rules visit a Dezolis town: Rolf in the Mogic
Cap for the shops, gear, mates, Trimates, Escapipes, the hospital,
every Dezolian, the Magic Cap back on, Data Memory), esper (the mansion's
door Espers with Rolf alive, B1's Esper twice and the scene, Lutz, F1's
chest and the Prism, out to the nearest town) and dezolis_grind (Zosa's
field from open ground by its gate). The player is about 14,400 lines;
what they stand on:

| File | Added |
|---|---|
| gear.c | PIN_HEAD (pin_head_add: Rolf's head kept for his cap); gear_up's return, the plan it put on, and its repr as Python prints the list |
| session.c | ready's `limit` (ready_n) |
| things.c | person_name public |
| ground.c | field_spot (the open cell by a gate with room for wander's walks) |
| story.c | the four stages; wear, fetch_to, cap_for_shops, caps_carried, talk_round, people_here, zosa_visit, dezolis_town_visit, nearest_town; squeak-by's DEZOLIS_GRIND and DEZOLIS_GRIND_2; the top-up of skure and dezolis_grind (Zosa's field, B1's Magic Cap first when no cap is carried) |

A stage past dezolis_grind is still `NotPorted: stage motavia`. These
games reached much of what is new: the top-up's B1 Magic Cap (ten
retries from story_roof.json), B1's wipe-out before the cap ("no Magic
Cap on Rolf"), Zosa out of reach after a wipe-out, the Esper mansion out
of reach, Rolf dead at the mansion's door and revive_party at
nearest_town's choice (Aukba), a wipe-out between the Prism and a town,
talk_to's "not here" in a round, dezolis_grind short of its level and
its lean, cap_for_shops putting the Mogic Cap on and falling back to
the Magic Cap worn (the top-up's visits before B2), and grind_and_gear's
`spot` (ported in build 171, first reached here). Ported and not reached,
so unproved: fetch_to's GIVE (the caps were in Rolf's own bag every
time), a town visit with no cap to wear ("shopping in the cap on", "the
wrong Cap"), the top-up's chest unreached, Aukba or Ryuon out of reach,
B1 out of reach, no scene, the Prism not in the chest.

**The proof**: `c_player_check.py --scratch DIR --to dezolis_grind`,
seeds 1-12 in the three languages: 36 of 36 the Python's, every game
through dezolis_grind (log.txt 1,445,299 to 1,841,694 lines a game,
129,780 fights in all; story.txt, the summary, the report and the
checkpoints). Seed 1 alone first: one C bug, a town's name left out of
dezolis_town_visit's "could not reach" line (level_name hands back its
own string, not always the buffer); then the 36, each on its first run.
73 tries failed and were retried as the Python's were: 26 at climatrol,
17 at uzo, 10 at skure, 6 at esper, 3 at arima, 2 each at nido, key_tube
and dams, one each at paseo, lab, gum, dezolis_towns and dezolis_grind;
Zosa's top-up ran before 18 of the retries. Seeds 13-24 in us: 12 of 12.
Six games under ASan and UBSan (seeds 1, 4 us; 7, 10 retranslation; 3,
12 ja): nothing reported, the same log.txt as the
plain build's. The 36 on 16 jobs: the C player 136-201 s a game, the
Python 256-329 s.

Two of the Python's own ways, kept as the proof requires: a talk round
names the level it began on, so when a talk walks the party out of the
town (Ryuon's, onto DezolisSkure, seed 1 in retranslation; dezolis_towns'
one failed try) the rest are "not here" and the round goes on; and
skure's B1 cats are asked for wherever loot's retreat left the party
(Zosa, four times).

## Step 6 (build 176): Shir and the Prism's dungeons

Six more stages: motavia (the Prism and the Mogic Cap to Rolf, home to
Paseo by Skure's roof door, Shir from Rolf's house into the party in
place of its weakest, and raised to level 10 beside Paseo for the
Central Tower's room), shir (Shir in and out of Paseo's item store and
the tower's room until the party carries a Visiphone, a Star Mist and a
Moon Dew, up to 400 exits; what she stole out of her bag; the party for
Menobe; the roof to Dezolis) and the four Prism dungeons, menobe, ikuto,
guaron and naval, each played the one way: the nearest Dezolis town's
visit, a grind from open ground by its gate on DezolisSkure until the
leader reaches its level, every floor's chests in turn (back to the town
when worn out), its two Nei items, and the town again. The player is
about 15,400 lines; what they stand on:

| File | Added |
|---|---|
| fight.c | dungeon_mix (the living and robot shares of the enemies on the floors ahead) and its repr; tech_power (a member's best attack technique against that mix) |
| party.c | member_value's tech_power; choose_party's mix; pick_party_x and choose_party_ahead (the party for the floors ahead, with its core and catch_up) |
| map.c | held_add (the Prism held) |
| shir.c | new: the steal tables, shir_join, raise_member, shir_thefts |
| story.c | the six stages; keep_on_rolf, empty_shir, worn_out, prism_dungeon; the dungeons' grind levels (L32, 34, 36, 36; squeak-by's two lower) |
| run.c | a checkpoint's "stale" by the slot's own time (below) |

A stage past naval is still `NotPorted: stage nei_sword`. The retries of
the four dungeons take dezolis_grind's top-up (Zosa's field), as the
Python's do. These games reached much of what is new: Shir not raised
to 10 (four games), her bag full and what she stole handed on (eight),
"nobody has room" (nine), make_room's tosses when a chest finds every
bag full (eleven games, make_room first reached), the shir stage's lean
(one), worn_out's retreat from a dungeon (35 games; loot's `tired` first
reached), a door shut again after a CONTINUE (21), a dungeon left short
of its Nei items (eight), Zosa, Aukba or Ryuon out of reach, "lost" in a
dungeon's grind, and fetch_to's GIVE (the Prism from Rudo to Rolf with
Rolf's bag full, one game; unreached in build 175). Ported and not
reached, so unproved: motavia's "could not get home to Paseo", Shir not
in the roster, raise_member failing outright, the thefts' "her bag is
full" stop, fetch_shir not back, a theft that took nothing known,
empty_shir's swap "for room" (its `first` is always empty here), and the
shir stage's "not all stolen yet" without lean.

**The proof**: `c_player_check.py --scratch DIR --to naval`, seeds 1-12
in the three languages: 36 of 36 the Python's, every game through naval
(log.txt 1,732,161 to 2,517,678 lines a game, 145,030 fights in all;
story.txt, the summary, the report and the checkpoints). Seed 1 alone
first: no C bug, but a race in the Python's checkpoint, which the C
never had. playthrough's `checkpoint` called a save stale when slot 1's
time was older than `time.time()` read before the save; Windows stamps a
file from its coarse clock (a tick of up to 15.6 ms), Python 3.14's
time.time() is precise, and save-here writes inside a tick, so seed 1's
shir checkpoint, saved, was "no save written (slot 1 is stale)" and the
stage ran again. Both now call it stale only when the slot's time is no
later than its own time before the save (tools/story.py's checkpoint
and run.c's; the C read the coarse clock, so it never raced). Then the
36, each on its first run. 99 tries failed and were retried as the
Python's were: 26 at climatrol, 17 at uzo, 15 at ikuto, 10 at skure, 7
at motavia, 6 at esper, 3 at arima, 2 each at nido, naval, key_tube and
dams, one each at paseo, menobe, lab, gum, guaron, dezolis_towns and
dezolis_grind. Seeds 13-24 in us: 12 of 12. Six games under ASan and UBSan
(seeds 1, 4 us; 7, 10 retranslation; 3, 12 ja): nothing reported, the
same log.txt as the plain build's. The 36 on 16
jobs: the C player 101-144 s a game, the Python 232-324 s.

One of the Python's own ways, kept as the proof requires: raise_member
never revives a dead member, so when Shir dies at a low level beside
Paseo the trips go on without her and motavia fails ("Shir not raised to
10"), its retry from story_dezolis_grind.json.

## Step 7 (build 177): the Nei Sword to Noah's grind

Four more stages: nei_sword (the Esper mansion with the eight Nei items
in the bags, Lutz's $1690-$1694, F1's chest and the Nei Sword, a slot
made in Rolf's bag, the sword to him and into his hand, Lutz again and
NO for the free HEAL, the nearest town), kain (home to Paseo; whoever
the Kain party leaves out hands over consumables and anything unique
Kain or Shir could wear; Shir's thefts for two Moon Dews and four Star
Mists; Rolf, Rudo, Anna and Kain, geared; the roof back to Zosa),
kain_grind (Kain to L20, squeak-by's 18, on Zosa's field by the party's
lowest level, 40 trips at most) and noah_grind (Lutz's YES onto
NoahGroundF, 20 fights a trip or until worn out, the Nei Sword out to
the mansion, the clone lab or Lutz's NO, every fifth trip the nearest
town, until Rolf is L41 and Kain L30 -- squeak-by's 35 and 24 -- with a
Moon Dew kept off the sword's holder and Shir sent home for one when
there is none). The player is about 16,500 lines; what they stand on:

| File | Added |
|---|---|
| gear.c | revive's `spend_reserve` (revive_x); sold_anywhere, char_id and wearable public; bag_repr (one bag as Python prints it) |
| shir.c | shir_bag public; shir_thefts' tally of what she took, as Python prints the dict |
| story.c | the four stages; lutz, make_room_keep (story.py's make_room, not play.py's toss), wear_hand, lutz_heal, keep_from, nei_sword_out, noah_trip, mansion_mend, sword_holder, reserve_dews, dews_off_holder, restock_dews; KAIN_TARGET and NOAH_TARGET with squeak-by's; no_kain; the top-ups of kain_grind (Zosa's field) and noah_grind (Noah's own, a level past where Rolf and Kain stand for each three trips) |
| run.c | `--no-kain` (playthrough's: Noah and the bosses with Rolf, Rudo, Amy and Anna) and the report's ", no Kain" |
| pl.h | the Session's dews_gave_up (one empty-handed restock a try) |

A stage past noah_grind is still `NotPorted: stage boss_party`.
`tools/c_player_check.py --no-kain` plays both players with it.

These games reached much of what is new: make_room_keep's hand-over
from Rolf's full bag (every game), the sword handed to a dead Rolf and
nei_sword failing ("Rolf is not wearing it", one game), keep_from's "no
room" and "keeps" (ten games), the Kain stage's thefts (all 36), the
sword's holder dead on Noah (34 times in 19 games), revived with the
reserve or not ("no Nei Sword", six games), Lutz out of reach from Noah,
"could not mend" (ten games), the dews off the holder (14), the restock home for Shir
(18 games, 30 times) and its empty-handed end (two), empty_shir's swap
"for room" now that the dews go first (ten; unreached in 176), the
thefts' "her bag is full" stop (16; unreached in 176), four wipe-outs
on Noah and their CONTINUE, and noah_grind's top-up (twelve retries). Ported
and not reached, so unproved: Lutz finding nobody, make_room_keep with
nothing to move, nei_sword's mansion out of reach, "not all eight" and
"no Nei Sword", the sword in Rolf's second hand, the kain stage's "could
not get home" and "the party is", kain_grind short of its level (lean or
out of trips) and its top-up, the sword not taking the party to the
mansion, noah_grind's lean and "out of trips", the restock with no Shir
or its way home failing, and a wipe-out with no save.

**The proof**: `c_player_check.py --scratch DIR --to noah_grind`, seeds
1-12 in the three languages: 36 of 36 the Python's, every game through
noah_grind (log.txt 1,931,853 to 2,754,218 lines a game, 154,616 fights
in all; story.txt, the summary, the report and the checkpoints). Seed 1
alone first, then the 36, each on its first run. 112 tries failed and
were retried as the Python's were: 26 at climatrol, 17 at uzo, 15 at
ikuto, 12 at noah_grind, 10 at skure, 7 at motavia, 6 at esper, 3 at
arima, 2 each at nido, key_tube, dams and naval, one each at paseo, lab,
gum, dezolis_towns, dezolis_grind, menobe, guaron and nei_sword. With
`--no-kain`: seeds 1-12 in us, 12 of 12 the Python's (the kain stage
Shir's round alone, the last member but Rolf out for it and back;
kain_grind nothing; Noah until Rolf is L37; the restock's member
keeping consumables, reached only here). Seeds 13-24 in us: 12 of 12. Six games under ASan and
UBSan (seeds 1, 4 us; 7, 10 retranslation; 3, 12 ja): nothing reported,
the same log.txt as the plain build's. The 36 on
16 jobs: the C player 119-181 s a game, the Python 297-409 s.

Two of the Python's own ways, kept as the proof requires: fetch_to hands
the Nei Sword to Rolf dead at the mansion (TECH_IS_DEAD) and nei_sword
fails, to be retried; and when the sword's holder dies on Noah with no
Moon Dew outside his bag and nobody with Rever, nei_sword_out finds no
living holder, the party stays on Noah, Lutz is out of reach and the
grind fails "could not mend" (eight of noah_grind's twelve failed tries;
the other four, the party wiped out after a trip).

## Step 8 (build 178): the bosses and the ending -- the whole game

The last three stages: boss_party (home to Paseo, whoever the boss
party leaves out hands over what Amy could use, Rolf, Rudo, Amy and
Kain -- tools/boss_trials.py's party, Amy's Deban the win -- geared, the
roof back to the mansion's town), dark_force (Ryuon's Trimates to 32,
the mansion mended, Lutz's YES and the walk to Pandora's box on weapons
and mates alone, revived and mended to half after each fight, the box
opened only with the party at 85% HP and 60% TP, the fight on
fight_smart's boss policy, the Nei Sword out and the town; four fights
a try) and mother_brain (the same walk to the foot of NoahF1's
corridor, up it into her arrival, YES to fighting and NO to returning,
her fight; then the aftermath, YES three times, and the ending played
through its pages, the credits and the last button to the Sega
screen). With these the C player plays the whole game, from a new game
to the Sega screen, and the stage list has no `NotPorted` left. The
player is about 17,000 lines; what they stand on:

| File | Added |
|---|---|
| fight.c | a fight's record keeps its scripts' last eight (Session.fights' "scripts", which the Dark Force line prints) |
| run.c | the unwind puts s.walk_mend back to None (the boss walks' `finally`) |
| story.c | the three stages; tp_line, box_ready, boss_supplies, boss_walk (the two walks' common loop), dark_force_once, mother_brain_once, answer; BOSS_PARTY and no_kain's |

These games reached much of what is new: the box and her corridor out
of reach (five and four games) and the walk again, a second walk to be
in shape (three games each), a boss lost and the CONTINUE (176 of the
stages' own tries lost at Dark Force, 72 at Mother Brain), all four of
a stage's tries lost (dark_force, 18 games), Dark Force beaten with no
way off Noah (one), a fight in her corridor that was not hers (ten
games), and the ending to the Sega screen in every game. Ported and not reached, so unproved: the
boss party's "could not get home", "keeps" and "the party is", never at
the box or her corridor in shape, no fight at the box or in her
corridor, no ending, and the ending stopping short of the Sega screen.

**The proof**: `c_player_check.py --scratch DIR --to mother_brain`,
seeds 1-12 in the three languages: 36 of 36 the Python's, every game
from a new game to the Sega screen (log.txt 2,053,273 to 3,678,220
lines a game, 169,294 fights in all; story.txt, the summary, the report
and the checkpoints). Seed 1 alone first, then the 36, each on its
first run. 158 tries failed and were retried as the Python's were: 37
at dark_force, 26 at climatrol, 17 at uzo, 15 at ikuto, 12 at
noah_grind, 10 at skure, 9 at mother_brain, 7 at motavia, 6 at esper, 3
at arima, 2 each at nido, key_tube, dams and naval, one each at paseo,
lab, gum, dezolis_towns, dezolis_grind, menobe, guaron and nei_sword;
Noah's top-up ran before 58 retries (the bosses' and noah_grind's).
With `--no-kain`, seeds 1-12 in us: 12 of 12, every one to the Sega
screen. Seeds 13-24 in us: 12 of 12. Six games under ASan and UBSan
(seeds 1, 4 us; 7, 10 retranslation; 3, 12 ja): every one to the
Sega screen, log.txt the unsanitized player's, no report. The 36 on 16
jobs, with 24 more games running beside them: the C player 216-356 s a
game, the Python 386-630 s; seed 1 alone, 113 s and 220 s.

Two of the Python's own ways, kept as the proof requires: after Dark
Force, mansion_mend walks a party with anyone dead across DezolisSkure
to the town's clone lab rather than Lutz's free HEAL, and six times the
walk wiped them out, the win lost with no save to show for it; and
mother_brain walks her corridor with the encounters on, so a fight that
is not hers (formations $D7, $DF, $E3, $E4, $F7) ends the try (15
times).

## Step 9 (build 179): the window on the C player

COMPUTER PLAYS runs the C player now. computer_plays.py's own loop --
the start from a new game, LAST RUN or a player's save, the stage a save
begins at, squeak-by's tries and top-ups, the Skure field's stages
replayed before the next one's retry, last.json at every save, the
report and its rows -- is `src/player/computer.c`, its `main` the
Python's line for line, on the stages and the stage loop the earlier
steps ported. The window (`src/harness/autoplay.c`) runs it on its
command thread, each command it sends run there and then by the probe's
interpreter; `PS2_COMPUTER_DRIVER=python` still starts the Python driver
the old way, the reference. docs/computer_plays.md, "The C player in the
window", has the shape.

| File | Added |
|---|---|
| computer.c | computer_plays.py's main: its arguments, stage_of_save (the story flags as the Python's JSON search sees them, recruits, items, NO_PICKUP), last.json written as json.dumps writes it, the loop, the report; --classify; the hand-back's jump to the run's end |
| run.c, pl.h | the stage table, raise_dead, resume_point, stage_begins, party_levels and tick_totals shared with computer.c; the unwind as stage_unwind; the story's lines to an echo file (driver.log in the window) |
| story.c, gear.c | story_reset: every target squeak-by lowers back to the story's own, Kain's and the boss party's choice, the held items and the pinned heads, for a second run in one game |
| ground.c | a trip's save tells computer.c (last.json's "mid") |
| session.c, player.h | a session attached to the window; player_session_free; the computer's API |
| autoplay.c | the C player's thread (16 MB of stack), player_line (a command run under the lock), the arguments parsed before the last run is cleared, NO TOOLS from the three files it reads, PS2_COMPUTER_LOG |
| route_probe.c | `--computer [--language KEY] ARGS`: the same run on the window's host (`player_only`), its files cleared as the window clears them, `--classify` touching nothing |
| Makefile | the player in every game target; `make computer_check` |
| make_pack.py, make_dist.py | the Python driver out of the pack, the embeddable Python out of the package |

**The proof** (`tools/computer_check.py`): the headless game, the Escape
menu and COMPUTER PLAYS from NEW GAME, against `route_probe --computer`,
the window's own path against the tool, every command at the same tick
and every file the run writes. Seeds 1-12 in the three languages at
FAST, with the C player and with the Python driver: 72 of 72 the tool's
game (33 games to the ending, 3 out of tries at Skure, below). AUTO and
AUTO 2X: seeds 1 and 4 in the three languages, the same. LAST RUN, SLOT
1, `--fail-once`, `--no-swap`, the menu mid-run, hand-back, QUIT, NO
TOOLS, bad arguments; `--classify` the Python's on 173 saves; the packed
game from an empty folder. Six runs of `route_probe --computer` under
ASan and UBSan, the plain build's logs. The window with the C player
took 54-126 s a whole game at FAST, the Python driver 144-323 s, the
tool 91-194 s (48 runs at once on 16 jobs).

Two things the window does its own way, which the check allows for: a
`shot` is logged "(not drawn here)" (the window draws its own frames),
and the Python on Windows writes its paths with backslashes (the
report's and last.json's; MSYS2's Python and the C player write
slashes).

**Found, not changed**: seeds 7 and 10 in us and 11 in ja stop at Skure
in COMPUTER PLAYS, the C player's and the Python's alike. The first try
wipes out on SkureB2 ("no path on SkureB2 from (79, 52)"), the CONTINUE
is Paseo's, and each retry -- roof again from dams' checkpoint, since
the Skure field gives none -- plays that same try again, squeak-by's
top-up gaining no level, six times. playthrough.py, which saves in the
Skure field, finishes those games (Step 8's 36 of 36). (build 180)
Fixed in both drivers: a retry plays the lost stage (the roof, Shir's)
before the top-up, which is Zosa's grind and so wants Dezolis; 36 of 36
reach the ending (docs/history.md, Build 180).

## The builds after (roughly)

1. The foundation, with Paseo and Arima: the verbs, the state, the
   pauses, the stage loop (its tries, CONTINUE, the checkpoints,
   last.json, the report). Done in build 171, but last.json, which is
   COMPUTER PLAYS's (the window's), not playthrough's.
2. The gear to Amy (build 172); the lab to the gum (build 173);
   Climatrol to the roof (build 174); Skure to Zosa's grind (build
   175); Shir and the Prism's dungeons (build 176); the Nei Sword to
   Noah's grind (build 177); the bosses to the ending (build 178): the
   whole game.
3. The window on the C player; `make dist` without its Python. Done in
   build 179.
