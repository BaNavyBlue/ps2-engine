# The testing harness: route_probe, play.py, story.py

Moved verbatim from the README in build 81c.

## Routes: the testing harness

`tools/route_probe.c` plays the real game from a script, as fast as the
machine runs it. (build 100b) Its interpreter is `src/harness/probe.[ch]`,
shared with the game's COMPUTER PLAYS (docs/computer_plays.md): the probe
keeps the route files, the output directory, the PNGs, the stderr capture
and the summary. It links CORE and GAME only -- no platform, no pacing --
and its tick counter is its own: one per `game_tick`, from 0, with no
relation to real time. Nothing is drawn unless a line says `shot`, and
then exactly what `main.c` shows (`game_render`, `video_compose`,
`game_overlay`) goes to an indexed PNG: the framebuffer's own indices and
the 80 colours behind them, the game's 64 then the Escape overlay's 16.
Rendering changes no game state (every render function takes the state
const, bar the walk frames copied into the VRAM mirror), so a route with
no shots runs exactly as one with many.

    make route_probe
    ./route_probe routes/darum.route          # -> route_out/darum/
    ./route_probe -v -o /tmp/d routes/darum.route
    make routes                               # every route, one process each
    make -j12 routes                          # (build 114) twelve at once

(build 140) Two switches for the colour plane (video.h): `PS2_DRAW=colour`
writes the shots from the frame's colour plane, as RGB PNGs, instead of
from its indices through the palette; `PS2_COLOUR_CHECK=DIR` composes every
tick's frame as `main.c` would and holds its colour plane to the palette
path pixel for pixel, writes each shot a second time from the colour plane
into `DIR/<route>/`, and reports in `DIR/<route>.txt` -- the route's own
files untouched. `tools/colour_check.py [--out DIR] [ROUTE ...]` runs
routes that way and reads both PNGs of every shot back with Pillow:
build 140, all 119 routes, 223,755 frames and 330 shots, none different,
and the route files the same as a plain run's (so rendering every tick
changed no game state either).

(build 160) `PS2_RUMBLE_CHECK=FILE`: after every tick, what the rumble's
watcher felt (src/game/rumble.h) held against what the game itself did --
the sound ids it asked for (core/audio.c's record) and each member's HP,
read apart from the watcher -- both ways: in a fight, hurt only as a blow
lands (SXFID_Sword) with the HP down, and every blow landing with the HP
down felt; on the map, floor exactly with SXFID_DangerousFloor, its point
of HP not felt again, any other HP lost (Roron's cake) felt as hurt; fell
exactly with the HP down to 0 there; lost exactly as the game over begins;
nothing felt on any other screen (a new game's HP, a load's); the motors
going exactly while something felt still shakes. The report in FILE; the
route's own files untouched. `tools/rumble_check.py` runs every route and
160 fuzz runs that way (docs/history.md, Build 160). (build 161) And from
the effect the sound driver started each tick (`Smps.sfx_started`,
`sfx_starts`) and the game's motion (`game_motion`): blast and hole
exactly as SXFID_Explosion and SXFID_FellInHole start; flight exactly
while a ship flies and shake while the camera shakes, the pause aside,
the motors going, and still once a flight is paused 40 ticks; landed
exactly as SpaceShipLoop ends; a blow's small motor going; and while a
followed effect plays, the big motor at 40% of its loudness or more, no
higher than it where nothing else shakes, and still once it is over. The
report's `followed` line: the ticks a followed sound was heard, the
blows whose sword never started (docs/history.md, Build 161).

(build 168f) `PS2_TEXT_CHECK` (set to anything): in a proportional
language, after every tick, each run of text in every window shown or
painted (a line of a message, a name, a label) held to the cells it is
drawn in, cut at the window's right edge, by lang.c's own rule -- the
first glyph whose ink would cross the end is dropped, and every glyph after
it. A run that loses ink so is said on stderr, once for its place and
what it says up to the first glyph dropped (a line being typed is said
once): `text check: LANG, window $WW row R col C: glyph K of N would end
at X px, the room is W (the text's script $SSSS)`. And a message the game
asks for whose entry is empty in the language: `text check: LANG, script
$SSSS asked for is empty`. The game is not changed by it: the
probe's stderr goes to its log (`    |     `), and playthrough.py's report
lists it under "port stderr". The stock text (ENGLISH 1989) is written a
cell a code and is not checked.

(build 160) The controls (src/core/keys.[ch]): the keys and gamepad inputs
the player has bound on the Escape menu's CONTROLS and BUTTONS pages. A
route's `press` is the Mega Drive pad's buttons, as ever, with no controls
between; `key NAME` and `pad NAME` press a key or a gamepad input as it is,
made what the platform makes of it through the menu's controls (the table
below). The headless game: `PS2_RAW="N:NAME,N:PAD:NAME,..."` presses keys
and gamepad inputs as they are at frame N (as `PS2_PAD` counts frames), the
same way, so a check can bind one through the menu and see it reach the
game (a gamepad input reaches the game only while `PS2_GAMEPADS` connects
one; the menu's own -- Back, the d-pad, the left stick -- always);
`PS2_PAD_STYLE=STYLE` (ps3, xboxone, switch, joystick ...) is whose labels
the connected gamepads wear, none by default. A name or style it cannot
read stops it with a message. `PS2_FRAME_HZ=N`: the frames come N times a
second by the virtual clock, so main.c's pacing runs free -- at 120 every
other frame runs no tick, at 30 each runs two -- and a press must be
carried to the tick that takes it (the window on a 120 Hz display, or a
slow machine, does the same). `make controls_check` is keys.c on its own
(tools/controls_check.c, Herzog Zwei's hz_controls.c with this game's
defaults: every name and label, the defaults, random bindings against a
model, the axes, hats and the menu's directions at their edges);
`python3 tools/controls_check.py` the headless game's menu, settings.json,
a rebind reaching the game and a bind on uneven frames (docs/history.md,
Build 160).

(build 184) A mod's chords past the chip (docs/audio.md, "Chords past the
chip"). `smps_trace` takes `--chords` (the chord voices the music read can
need, as smps_load counts them: `chord voices: music M, effects E`),
`--voices` (with `--regs`: each chord voice's writes, which the hooks alone
carry, as `FRAME  V<v> I|2 REG <- VALUE` lines on its chord chip's part),
`--play ID@FRAME` (a sound id, hex, queued at that frame: a fade, an
effect, the pause; up to 16) and `--resume FRAME` (the pause cleared, as
the game clears it, so a queued unpause is read). `python3
tools/chords_check.py` (`make smps_trace` first; for its last two cases
`make headless ps2_headless_q`, and numpy): each case a scratch copy of
assets/music edited as a mod would --
- game: the game's music needs no voices (0 and 0);
- chord6 and chord16: Phantasy's FM3 chord made six and sixteen notes
  (each note's octaves added, within B0-C#6): 3 and 13 music voices; the
  chip's writes the song's own, frame for frame, 3,000 frames; each voice
  struck with the lead (24 times) at its added note; the fade ($E0 at 600)
  stepping each voice's carriers as often as FM5's (41 against 39) and its
  end keying them off; the pause ($D5 at 600) turning each voice's levels
  down to FM3's and the unpause ($D6 at 700, after `--resume`) back up;
- chord17: a cell of 17 notes and the song text's `chord 17` refused,
  the file and line named;
- effect: the level-change effect ($A4, on FM3) made a chord of three:
  2 effect voices, each struck four times over the music;
- heard_chip and heard_q: chord6 through the headless game (`PS2_MODS`,
  2,400 frames) against the game's own, on the chip and on Q -- the same
  to the bit until the first chord (frame 344), and from it their
  difference the added notes alone (over 99% of its energy on their
  overtones). `--no-heard` leaves these two out.

(build 141) The colour plane is the colour path's own now (docs/
palette_effects.md, step 3): the pictures' colours, the effects' glow and
the fades worked out per pixel, never read from the live palette -- so the
check above is the proof that the two paths draw the same frame (build 141:
the same 223,755 frames and 330 shots, none different). The game shows the
colour plane; the route probe's shots stay the palette path's indexed PNGs
(the routes' reference) unless `PS2_DRAW=colour`, and the platforms draw the
palette path with `PS2_DRAW=palette`.

(build 142) The portraits are drawn from their pictures (assets/art/
portraits, video.h's VideoPicture): a pixel's colour from the picture, its
index from the tile as before -- so the check above holds the picture to
the tiles through the palette, and proof.sh's indexed shots hold the index
plane to the old one. Each route's report, after its counts, says how many
pixels came from pictures, in how many frames, and which (`... 0 differ;
908800 pixels from pictures in 146 frames (item_seller)`), and
colour_check.py totals them: a route that shows a portrait with a count of
0 drew it from its tiles.

(build 146) Each report ends with `the frames' colours $XXXXXXXXXXXXXXXX`,
an FNV-1a hash of every tick's colour plane -- what the game shows --
over the run: two runs of a route with the same hash showed the same
colours on every frame. tools/proofs/compare.py compares them, route by
route, when both proof runs have them (PROOF_COLOUR=1). It is the proof
for a change meant to move no pixel where the index plane or the files'
form does change, such as the portraits drawn without their tiles.

A route is one command per line, `#` for comments, numbers in decimal,
`0x` or `$` hex:

| Command | |
|---|---|
| `start new` | power on at the SEGA screen with no saves |
| `start slot N [FILE]` | CONTINUE from `saves/slotN.json` (or FILE), copied in first: Data Memory's "We meet again" |
| `start level L CX CY` | a new game on that cell, as `PS2_LEVEL` |
| `start building B P F764 F766 [CX CY]` | as `PS2_BUILDING`; commas or spaces |
| `start battle F` | as `PS2_BATTLE` |
| `press a\|b\|c\|start\|up\|down\|left\|right\|esc` | one tick down, one tick up; `esc` (build 100) is the Escape key, the port's menu |
| `key NAME [TICKS]` | (build 160) a key pressed as it is, by its name in src/core/keys.c (Q, SPACE, ESCAPE, BACKSPC, QUOTE ...), held TICKS ticks (1 by default), then a tick of nothing: what the platform makes of it through the Escape menu's controls -- the actions it is bound to, Escape the menu, Backspace the CONTROLS page's back, the arrows and Enter the menu's own -- and `Input.key` as it goes down, which the CONTROLS page binds; held, the menu's cursor repeats as for a bound direction. `press` is the pad's buttons, with no controls between |
| `pad NAME [TICKS]` | (build 160) the same for a gamepad input by its settings.json name (SOUTH, DUP, LSUP, RSLEFT, L2, JOY3 ...): `Input.pad_input`, which the BUTTONS page binds; the actions it is bound to; Back the menu; the d-pad and left stick the menu's own |
| `pad-style STYLE` | (build 160) whose labels PAD 1 wears for the BUTTONS page (src/core/keys.h's keys: none, standard, xbox360, xboxone, ps3, ps4, ps5, switch, gamecube, joystick), as main.c hands PAD 1's over from the platform every frame |
| `walk left\|right\|up\|down N` | hold it until N steps have begun |
| `wait idle` | until the game waits on the pad and nothing else |
| `wait screen NAME` | until NAME is loaded and faded in |
| `wait ending MOMENT` | (build 93a) until the ending screen is at MOMENT: `setup fade_in`, `pageN_draw\|slide\|text\|cleared` (N 1-6), `last_page_hold wipe last_picture to_white white fade_in_again`, `final_days_slide\|text\|cleared`, `credits`, `ufo` (while the UFO is drawn), `button` |
| `wait ticks N` / `wait step` | N ticks with nothing pressed / until the leader is on a cell |
| `wait pad N` | (build 101) until the game waits on the pad, or N ticks, whichever first; never a failure (COMPUTER PLAYS's fight pace) |
| `wait script $XXXX` | until that script reaches a window, on `expect script`'s cursor, which it moves past it |
| `shot NAME` | `NAME.png`: the palette path's frame, an indexed PNG; (build 146a) RGB when a picture is in the frame (its pixels have no palette entry) |
| `expect screen NAME` | `sega title intro level building battle gameover` |
| `expect level L [CX CY]` | on level L, and on that cell if given |
| `expect script $XXXX` | shown since the last script this matched, so a run checks order |
| `expect no-stderr TEXT` | no stderr line so far contains TEXT |
| `expect text "TEXT"` | (build 131a) the text window's last script (window.c's expanded codes, the names in) contains TEXT, written as the script files write it: a character by win_charset, `{XX}` a code in hex (`{C1}` a line break -- the port puts two a break -- `{C3}` a pause, `{47}` the ROM's ellipsis dot) |
| `expect command KIND` | `attack tech item defend`: a character has begun one in a fight since the last match of that kind |
| `expect stat MEMBER STAT LO [HI]` | (build 95a) party position MEMBER's `hp`, `tp`, `attack`, `defense` or `agility` is LO, or within LO..HI |
| `expect rumble KIND N` | (build 160) the rumble's watcher (src/game/rumble.h) felt KIND -- `hurt`, `fell`, `lost`, `floor`; (build 161) `blast`, `shake`, `flight`, `landed`, `hole` -- N times since the last `expect rumble KIND` or the start. Not in COMPUTER PLAYS (no watcher there) |
| `expect rumble going\|still` | (build 160) its motors now, at HIGH |
| `expect setting rumble LEVEL` | (build 160) the GAMEPAD page's RUMBLE: `off`, `low`, `medium`, `high` |
| `expect setting language KEY` | (build 163) the Escape menu's LANGUAGE, by its key in assets/lang/languages.json: `us`, `retranslation` |
| `language KEY` | (build 163) LANGUAGE set as its row would set it, the game taking it up at its next tick (docs/languages.md). Not a player's command |
| `expect keys\|buttons ACTION SLOT1 / SLOT2` | (build 160) what the CONTROLS page (`keys`) or the BUTTONS page (`buttons`, in PAD 1's labels) shows in ACTION's row -- `up down left right a b c start` -- each slot as drawn, `-` for none: `expect keys b X / ;`, `expect buttons up DPAD UP / LS UP` |
| `rumble-log NAME LEVEL` | (build 160) from now on NAME in the route's directory: a line `TICK 1 LOW HIGH` each tick the motors at LEVEL (0 OFF to 3 HIGH) change -- the headless game's `PS2_RUMBLE` log, which counts frames pumped where this counts ticks, so the same presses give the same lines (tools/rumble_check.py holds them). Not a player's command |
| `expect riding yes\|no` | (build 83) the party is on the jet scooter, or on foot |
| `expect setting fullscreen on\|off` | (build 100) the Escape menu's FULLSCREEN; (100b) `expect setting speed normal\|fast`, its SPEED; (100c) `expect setting computer on\|off`, COMPUTER PLAYS's lamp |
| `expect escape-menu open\|closed` | (build 100) the Escape menu is up, or not |
| `expect picture X Y THING\|none` | (build 142) the frame `shot` would write took screen pixel X, Y's colour from THING's picture (assets/art/<kind>/THING; `rolf`, `nei`, `governor`), or from none -- a tile through the palette, the backdrop, the overlay. Not a player's command |
| `expect pixel X Y $CRAM` | in the frame `shot` would write, screen pixel X, Y's palette entry (its index in the live palette, or the Escape overlay's) holds that CRAM word; (build 146a) on a picture's pixel, which has no entry, the colour that CRAM word shows through the DAC ladder. Not a player's command |
| `expect audio ROW VALUE [/ WHY]` | (build 110) what an AUDIO row shows (`summary`, the menu's; `synth`, `master`, `music`) and, after a slash, why it cannot be had (`Q / NOT BUILT`) |
| `synth chip\|q` | (build 110) switch the synth backend now, as the AUDIO page does: the audio thread resets it and replays the register shadow into it; one the build has not got falls back to the chip, reset and replayed all the same. Not a player's command |
| `sound render on\|off` | (build 110) from the next tick, each tick's audio rendered through the mixer (the backend playing) where the probe otherwise throws the queue away. route_probe only; not a player's command |
| `expect sound playing\|silent` | (build 110) over the last four ticks rendered, the mix peaked above -40 dBFS, or not (a switch's cut clicks through the DC blocker for a few ticks: wait a dozen) |
| `expect sound clicks below DB` | (build 143) over the last four ticks rendered, the sharpest bend of either side -- the largest second difference x[n] - 2x[n-1] + x[n-2], the tick before's last two frames carried over -- stayed under DB dBFS. A click is sharp at every frequency; the FM music bends far less from sample to sample, but its own sharpest moments reach -15 dBFS, so the route picks a moment and holds it against the same moment without the click |
| `expect sound-writes N $HASH` | (build 110) the sequencer has sent N writes through its sink since start-up, their FNV-1a hash $HASH (`audio_write_hash`) |
| `expect sound-register PART $REG $VALUE` | (build 183) the YM2612's register REG as the sequencer last wrote it (`audio_register_written`): PART 1 for FM1-FM3 and the chip's own registers, 2 for FM4-FM6; rendered or not. A channel's registers are its operators' plus its number (FM3's levels: `$42`, `$46`, `$4A`, `$4E` for operators 1, 3, 2, 4); a write a music track makes while an effect holds its channel never reaches the chip, and is not seen here |
| `expect song $XX` | (build 139f) the music the sequencer last started (PlaySoundID, `audio_music_started`): what plays. `expect music $XX` is what was last queued, which a sound queued after it in the same field overwrites |
| `expect stuck none\|$MASK` | (build 129d, restored in 132a) the music tracks lent to an effect that no longer holds them, a bit a track (`smps_stuck_tracks`); (build 132c) in the high byte, music tracks a pause turned down that nothing turned back up: none, or exactly MASK (16 bits) |
| `expect menu none` | (build 97d) no window is open or holds the pad: a C press the ROM ignores opened nothing |
| `oneshot $XX` | (build 97d) the event word's high byte at the next level load (a `warp`, a door): the one-shot case, as `move.w #$XX00, (event_flags).w` just before a reload leaves it |
| `join-next N` | (build 145) party_member_join_next ($C606), the last recruit sent to Rolf's house, 1-8: while it is ahead of party_members_joined, each visit there brings the next one to the door (RolfHouse, loc_B04E). `expect join-next N` reads it. Not a player's command |
| `enemy-hp N` | (build 93) every live enemy's HP set to N -- for routes about what follows a boss, not the fight |
| `limit N` | ticks a later wait or walk may take before failing (3600) |
| `save-here N` | (build 84) slot N in the route's save directory, a Visiphone save of where the party stands: CONTINUE lands there, on Dezolis too |
| `fuzz TICKS [SEED]` | play by itself: walks and A under control, C through text, cursor moves then C or B in menus, the battle's menus on purpose, Start on the title |

(build 100) A route reads its own `saves/settings.json` in its output
directory -- none, so the defaults -- never the player's: the Escape menu
draws the settings, and a route must not depend on how the player last left
them. Nothing but main.c writes settings.json.

A press is two ticks because a pad cannot report a new press of the same
button on two fields running; the ROM's `joypad_pressed` is an edge. A
single tick of a direction at rest is enough to start a step, which is
also how a route turns to face something it is blocked by. `walk` fails
at once if the leader is not the pad's, ends early without failing when
the game takes the pad back -- a door, a message, a battle, Darum -- and
fails if he stands blocked with the direction held for 120 ticks, naming
the cell.

**What counts as idle** is `game_debug_state` in `game.c`, and it is
read from what the ROM waits on, never from timers: the finished title;
the party at rest on the map with no window, demo walk, talk or level
event; a message stopped at `$C4` or `{pause}`; a window's poll holding
the pad (`window_index_saved` set, its routine at 2) or the battle's
party triangle; `GamePausedLoop`. The one inference is event code that
reads the pad itself with no window live -- the C menu's stat pages, the
profile in Rolf's house: its counters (`event_routine`, `_sub`, `_sub_2`,
`$DE70`/`$DE72`, the two `$DE5E` timers) standing still with the windows
idle for two ticks in which the screen's logic ran. Counting ticks that
were inside a fade gave a false idle as every building faded in, so
those do not count. A timed message (`$C6`/`$C7`), text still printing,
a window opening or closing, a fade, a dream effect, a fight round and a
scene are all busy.

Script ids come from `LoadScript`'s own lookup (`game_debug_on_script`),
run-on ids included, plus the six messages of the attract crawl, which
draws its box without it.

`fuzz` is for grinding. Its own xorshift (seeded by SEED) picks, at each
wait, an input that wait accepts; a run fails only when the game stays
busy past the limit, and warns when three hundred inputs in a row change
nothing it can see (screen, level, steps, position, scripts shown). A
FIGHT round counts as a wait after 1200 ticks and gets a C, because the
ROM repeats the round until a button interrupts it (`loc_F1D0`): three
fights the first sweep reported as hung were rounds in which nobody could
hurt anybody, looping as the ROM would.

**The battle's menus** (build 62) are not played like the others.
Random moves then C picked FIGHT nearly every time: an ORDR that ends
in a stored command takes STGY, ORDR, the party triangle, the command
triangle and often a list and a target, each with a B in four against
it, and the triangles move on left/right where the lamps move on
up/down, so half the random moves did nothing. Every command stayed
where `loc_F550`/`loc_F584` put it -- an unarmed Nei on Defend for
good, which with Rolf down is a round nobody can win. Now the fuzzer
reads the live cursor (`win_debug_cursor`: which window's poll holds
the pad, its stop, how many stops, which way it moves; in
`GameDebugState` as `menu`, `menu_stop`, `menu_stops`, `menu_across`,
`menu_party`), picks a stop for what it does and walks the cursor
there: FIGHT or STGY evenly; ORDR 85%, RUN 10%, B 5%; a member (B one
in ten); any of attack, technique, item, defend; any entry of the
technique or item list, NEXT included; either group. `commands[4]` in
the state counts what characters began (`battle_debug_commands`, one
per call of the port's `Battle_CommandUsedIndex`); the fuzz's last log
line gives them, and `expect command KIND` checks one happened. Over
seeds 1-40, 8000 ticks each from `start battle 1`, techniques went from
7 to 35 and items from 5 to 23; the route `fuzz_battle` is seed 2, in
which the old fuzzer began no technique.

**Playing it (build 65).** `route_probe -i` reads commands from stdin,
one a line, and echoes its log to stdout as each finishes (flushed per
line, so a pipe sees it at once); a failed command is logged and play
goes on; EOF ends the session. The route language gains three commands:

| Command | |
|---|---|
| `state` | one log line: screen, what it waits on or what keeps it busy, level, cell, facing; then (build 66) the building, the live menu and its cursor, every party member's level, HP and TP, the meseta, and in a fight the formation and each live enemy's HP |
| `bags` | (build 66) every member's bag as ItemID_ names in bag order, a `*` on what is worn |
| `hands` | (build 99) every member's right and left hand as ItemID_ names, `-` for an empty hand (a two-hander in both): which hand gear_up puts a one-hander into |
| `party N LEVEL` | right after a start: the first N characters (Rolf, Nei, Rudo, Amy) in the party, each levelled a row at a time through `battle_apply_level_row` -- the gains a fight's level-up rolls, on the game's RNG -- then HP and TP full |
| `keep-equipment` | the fuzz never takes EQP in the C menu (it strips weapons, and then everyone defends) |

`tools/play.py` drives it. `Session.send(cmds...)` runs commands and
then `state`, parsing it into `Session.state`; `ready()` gets back to the
party under the pad through fades, text and fights; `fight()` plays one
battle with FIGHT, a C every 240 ticks to break a round that loops, and C
through the victory text; `grind(walks, fights)` wanders until that many
battles have been fought. `LevelMap(level)` reads a level's collision
the way walk.c does (the collision plane, the attribute table, plane B's
`$53`/`$72` town edges); `goto(session, cells)` walks there, re-planning
round people and after fights, and `door_cells(level, y, x)` gives the
cells that step onto a record. The probe renders only on `shot`, so the
clock is whatever the machine manages: the whole scripted opening, title
to control of Rolf, is under a second.

A first session (intro, out of Paseo, grinding next to it with Rolf and
Nei at level 1 and no healing) won three fights, gained no level, and was
wiped out on the fourth, at tick 28679: game over, then the SEGA screen.
That is the game as written, not a port bug -- a player heals at the
hospital between fights -- but it is what an unattended grind has to
plan for.

**The C player (build 171).** `route_probe --play [playthrough.py's
arguments]` plays tools/playthrough.py's game with the player in C
(src/player/, docs/c_player.md) inside the probe: each command given to
the interpreter in-process with -i's line numbers, the log's lines read
back as -i's stdout carried them, its files under route_out/<base>/ as
playthrough.py writes them (`--base NAME`; playthrough_s<seed>[_<lang>] by
default). The whole game, Paseo to mother_brain and the ending (build
178; to noah_grind in 177, to naval in 176, to dezolis_grind in 175,
to the roof in 174, to the gum in 173, to Amy in 172, Paseo and Arima
in 171). `tools/c_player_check.py --scratch DIR --to mother_brain`
plays seeds 1-12 in the three languages with both players and compares
their files: 36 of 36.

(build 179) `route_probe --computer [--language KEY] [computer_plays.py's
arguments]` plays COMPUTER PLAYS's game -- computer_plays.py's loop in C
(src/player/computer.c) -- on the window's host: only a player's
commands, `start new` the console's reset, `start slot` a CONTINUE
through the title; its files under `--out` (saves/computer by default,
cleared first unless `--start last`) and the probe's in OUT/probe, its
settings.json there saying the language. `--classify` prints the stage a
save begins at and touches nothing. `tools/computer_check.py` (`make
computer_check`) plays a seed through the headless window's own path at
each SPEED against it, every command at the same tick.

**Playing it further (build 66).** `state` now carries what a driver
needs to decide anything: `, building 4` on the building screen;
`, menu $27 stop 0/2` for the window whose poll holds the pad (`across`
for the battle triangles, `party` for the character one; the name
board is `menu $24` with no stop); `, party Rolf L3 21/38 4/9 Nei L2
...` (level, hp/max, tp/max, then `dead` or `poison`); `, meseta 120`;
and in a fight `, formation 77 enemies 7:14/14 2:23/23` -- each live
enemy slot as id:hp/max, the id from the object's group start. `bags`
is a second command, for what is carried. `game_debug_state` fills the
new fields from `g_data` and `g_battle`; `game_debug_bags` is the bag
line.

`tools/play.py` grew from a grind loop into a player. The `state`
regex is `STATE_RE`/`parse_state`, and `Session` keeps every script
shown (`shown`, `last_script`) and every fight (`fights`: formation,
enemies at entry, ticks, scripts, party after). On top of that:

- `door_cells(level, y, x)` was wrong for towns: a town door is a
  solid special cell pushed *up* into (`Sprites_ChkCanMove` ->
  `interact()`, matched when the record's y,x lie within two cells
  down-right of the pushed cell), so it returns `{cell: direction}` --
  the open cell below a solid special one (any side off a town, which
  the tube's north door needs), the cell itself for stairs.
  `LevelMap.path()` never crosses a special open cell (a town's edge,
  stairs, a hole) except as the goal: it was walking Rolf out of Arima
  on the way to the hospital and up Shure's stairs on the way to a
  chest. `ways_out()`/`plan()`/`travel()` route across levels through
  stairs, town doors, town edges and dungeon exits (any solid special
  cell no record covers -- `loc_6E7E`); Shure's ground floor is not one
  connected region, so a chest can mean up to F1 and down other stairs.
- `visit(s, level, door, policy)` plays any building: C through text, a
  stop per menu by window id and the last script shown. `heal()`,
  `buy()` (item and both equipment stores), `save(slot)` (a blank name
  on the board: up, right, right, C -- from A that lands ADV, RUB, END),
  `clone_lab()`, `rolf_house(take=[...])` (a newcomer only joins through
  REORGANIZE, which `loc_B398` -> `loc_B0DA` is), `continue_game(file)`.
- `equip()`, `use_item()` and `mend()` on the C menu; an item list of
  nine or more opens on the first item with NEXT a stop above it
  (`Win_MenuItemList`'s `$108`), so those stops are slot + 1.
  `resupply()` buys an Escapipe, a Telepipe and up to eight mates;
  `retreat()` gets home whatever the state of the party (Escapipe out
  of a dungeon, Telepipe from the field, clone lab, hospital,
  resupply); with `s.careful` set, `goto` gives up (`s.hurt`) when hurt
  with nothing left to mend with, so a caller can retreat first.
- `fight_smart()` looks before it presses -- the old `press_through`
  sent `wait ticks 240` + C blind, and a C at the FIGHT/STGY menu is
  FIGHT again, which is why its fights ran forty rounds; `loc_F1D0` and
  `BattleScreenLoop` (12245) interrupt the repeat on any button, as the
  port does -- and takes STGY/RUN when someone is under a quarter and
  there is still a lot of enemy to get through. `search()` faces a
  cell and presses A, and uses the Small Key on a chest that answers
  `$1707`. `grind()` goes out by a town's edge, fights, and comes back
  to the hospital.

The story, driven this way from `start new`: shops, Nei's SteelBar
equipped, a grind at Paseo's gate, Data Memory, the tube to Arima, its
hospital, back to Rolf's house (`$90C $91C $91D $913 $90E $90F $910
$92F`, Rudo in the party), a save, Shure F3's body (`$170B $170C`, the
key and the letter), the five ground-floor chests (four locked:
`$1707`, then `$01 $17` and `$1702 $1703`; two Dynamite, a Monomate,
150 meseta; one empty, `$1706`), a save, and Nido's ground floor. Some
120 fights, seven buildings, six levels. `routes/saves/story_rudo.json`
(Rolf L3, Nei L5, Rudo just joined) and `story_shure.json` (the Shure
items in the bag; the party raised to 10 with `party 3 10` for the
tower, since Rolf L5/Nei L9/Rudo L5 with no mates wiped there twice --
`CalculateAttackDamage` (22 and `loc_2AA8`) match the port line by
line, so that is the game) are the checkpoints.

What it found is the game rather than the port, every time it was
checked against `ps2.asm`: EXP divided by the living (`divu` in
`loc_F2A4`), so Rolf levels slowly beside Nei; Nei defends until
armed; RUN failing five times running at escape rates of 51-153;
Blasters' technique `$EB` (odd, so the whole party, at rate 20:
`loc_2CEA` -> `loc_2D08`) taking 40 off everyone a round in Nido; a
blank Data Memory name on an empty slot (`loc_B5A4`); chests locked by
`$C780` flags with the key used from the menu, not with A.

The one thing that is the port's: **Nido's door**. Object `$3C`
(`Obj_DestroyableDoor`, 7961) stands in the gap at 53,45 until
Dynamite is used on it -- `ItemAction_Dynamite` (14337) finds the
faced object with index `$18`-`$1A`, sets its `event_flags` bit and
runs demo 4 (`Demo_DynamiteUsed`: three frames still, three down,
right, up, then `$FD`: `SXFID_DoorOpen` and routine 2 into `$FFFFE822`,
the level's first object), whose routine 2 spawns object `$2B` and
routine 3 clears its box. npc.c spawns neither `$3B` nor `$3C`, so the
gap is open floor and the party walks through with the Dynamite still
in the bag. `routes/gaps/nido_door.route` shows it, with PNGs; it is
kept out of `routes/` because it passes for the wrong reason, and
belongs there with its expect flipped once the door, the explosion
object and demo 4 are ported. The same object guards the Biosystems
lab's ground floor and F2 (indices `$19`, `$1A`); `$3B`,
`Obj_OpenableDoor`, is the tube's locked door, the four dams and
Control Tower F1.

`tools/fuzz_levels.py` starts each level's party at a fair level for
its region (`REGION_PARTY`, by level index; `--party N:LEVEL` for one
everywhere, `--no-party` for the old level-1 Rolf and Nei) and ends
with a coverage table: per level the fights, the distinct scripts, the
levels reached from it and the commands begun, and the levels with no
fight at all -- the fuzz's short walks make encounters rare, about one
a run at 20000 ticks.

`make route_probe_asan` builds
the probe with AddressSanitizer and UBSan; their reports go to stderr and
so into the log by tick (run with `ASAN_OPTIONS=detect_leaks=0`; the
probe does not free its route). (build 113) Not with MinGW's GCC on
Windows, which has no sanitizer runtime. (114a) With MSYS2's CLANG64 clang
it does: `make route_probe_asan` from the CLANG64 shell, run with
/clang64/bin on the PATH (the runtime is a DLL there) -- `/ucrt64/bin/python3
tools/fuzz_levels.py --jobs 12` from that shell. Clang's bounds check found
smps.c's track_byte stepping past a track's array on every route with music
(fixed in 114a, the same bytes); what it still reports is Nuked-OPN2's own
left shift of a negative value (ym3438.c:660).

    printf 'start level 63 3 6\nlimit 15000\nfuzz 40000 1\n' > /tmp/f.route
    ASAN_OPTIONS=detect_leaks=0 ./route_probe_asan /tmp/f.route

**Every level (build 63).** `start new` then `fuzz` rarely gets out of
Paseo, so `tools/fuzz_levels.py` starts a new game on each level in turn
at the first arrival cell any door or exit names for it (Gaira, which no
door enters, on 24,5), fuzzes from there, and prints the runs that
failed, every sanitizer report and every distinct stderr line with the
first run that printed it:

    make route_probe_asan
    python3 tools/fuzz_levels.py --ticks 20000 --seeds 1,2,3

Its routes and logs go to `fuzz_out/`. At build 63, seeds 1-3 at 20000
ticks and 4-6 at 60000 -- 606 runs, all 101 levels, 434 fights -- gave
no failure, no sanitizer report and no stderr line.

Two things any fuzz shows that are the ROM. The fuzz plays EQP a great
deal, and taking things off outnumbers putting them on; `loc_F584` puts
anyone with both hands empty on Defend at the start of every fight, and
Nei starts a new game with nothing in either hand, so commands come out
heavily Defend (the second pass above: attack 344, technique 113, item
56, defend 1703). And ATTACK picked in ORDR for an unarmed character is
`$1205`, "{char} can't fight!": `CommandAction_Attack` (21880) tests
both hands, `$C021` and `$C022`. Seed 12 from `start new` is the clear
case -- its first fight has Rolf's Knife and CarbonSuit unmarked in his
bag after 23 `$0204`s and 20 `$0202`s, and no attack is ever begun.

Each route writes to `route_out/<name>/`: the PNGs; `log.txt`, every
command with the tick it ran on and, between them, each script id,
screen, level entry and stderr line with its own tick; `summary.txt`;
and `saves/`. That last is the route's save directory
(`gamedata_set_save_dir`): the select screen's backup check, ERASE and
Data Memory's saves write there, never to the player's `saves/`. stderr
is caught by pointing file descriptor 2 at a file and reading it back
after every tick. (build 113) On Windows (MSYS2 UCRT64) the file is opened
O_BINARY and read back through a second descriptor of its own, where Linux
uses pread (fd 2 shares the first one's offset), and every file the probe
writes is the same bytes as on Linux. The summary line also goes to stdout, and the exit code is 0
for a pass, 1 for a failure, 2 for a script that does not parse or a
tree it cannot run in.

The routes, all passing as of this build:

| Route | Ticks | What it shows |
|---|---|---|
| `new_game` | 5813 | title, NEW GAME, ROLF typed on the board, the dream, both demo walks, the Governor, Rolf's house, two steps under the pad |
| `paseo_shop` | 320 | two steps and the item store's door, $0801, B, $080D, the street |
| `darum` | 547 | eleven steps left from the east entrance, $1801 with no button, the push again, $1801 again |
| `shure_f3` | 537 | the pocket body: $170B, $170C, the key and the letter, then $170B alone |
| `shure_f3_bodies` | 799 | the other two, 33 steps apart: $170B each |
| `fight` | 2363 | formation 1, FIGHT, victory, $1211-$1213, the map |
| `piata_tower_shut` | 99 | a new game pushing on Piata's tower: nothing happens |
| `piata_tower_open` | 417 | `$C712` set: the same door into ControlTowerGroundF at 46,41 |
| `paseo_rolf_dead` | 416 | Rolf at 0 HP pushing on his own door: "{char} is dead." and no house |
| `fuzz_battle` | 8000 | fuzz from formation 1: an attack, a technique, an item and a defend all begun |
| `eijia_far_column` | 2061 | (build 88) Kain's EIJIA on four Poleziaxes: the hit effect over the far column (`loc_34EE` `$47`) ends and the next round comes |
| `esper_b1` | 112 | Esper mansion F1's stairs down into `$10`, a 16x8 level with a backdrop |
| `esper_door` | 399 | (build 87) F1's door Espers: `$1687`, `$C742`, the two step aside, `$8B`'s `$168B` |
| `esper_door_rolf_dead` | 722 | (build 87b) Rolf dead: `$1688`, `$C742` stays 0, the door stays shut |
| `esper_b1_lutz` | 2493 | (build 87) B1's `$8F` twice, the scene, Lutz's `$190B`/`$190E`, F1 16,16, the Prism |
| `esper_lutz_nei` | 2080 | (build 87) `$33`/event `$34` with the eight Nei items: `$1690`, `$C744`, the Nei Sword, `$1911`, NO, `$1912` |
| `shir_steal` | 3015 | (build 87a) Rolf and Shir through Paseo's item store: the ninth goodbye is the theft, `$1722`, Shir gone |
| `ikuto_ground` | 96 | IkutoB1's stairs up into `$52`, the other one |
| `dezolis_prism` | 417 | CONTINUE on Dezolis holding the Prism: the step up onto Ikuto's block goes into IkutoGroundF 15,13 |
| `dezolis_no_prism` | 37 | a new game on the same cell: open ground, one step north |
| `noah_box` | 539 | A on Noah's box: `$171A`, the reload with no party, `$1910`, Dark Force |
| `jet_scooter_dam` | 1920 | A on the parked scooter: `$1686`, `$171C`; riding, a fight on the water and back on the scooter where the step began, the push into Red Dam F1 at 47,25, walking out onto the scooter at 81,82, `$171E` |
| `scooter_music` | 2526 | (build 139f) jet_scooter_dam's ride with `expect song` at each level change: Restoration ($82) after both fights on the scooter and out of Red Dam riding, Mystery ($88) in it (fails on 139e: the victory music, $96, played on) |
| `jet_scooter_off` | 879 | boarding, `$171E` facing the water, a push down against the shore, `$171D`, on foot at 89,116 facing up, `$171C` alone the second time |
| `ending` | 21707 | (build 93a) mother_brain_won's way to the ending screen, then all of it: "The Place of Death" before the white fade, the fade in blue first, the six pages (a shot each, their scripts in order), the strip wipe, the last picture, the text over the stars, "Never Dream", mid-credits, the ending UFO's last frame, "THE END", the button to the Sega screen ((157a) through SegaScreen's fade, THE END held in plane A's first 32 rows); pixels from an independent composition of the ROM's tables |
| `gameover_message` | 846 | (build 93a) a level-1 Rolf loses to Mother Brain: the game over's message on the title picture with no fill behind it (the font placed as DrawArtTiles2 leaves it) |
| `shu_target` | 752 | (build 95a) Amy's SHU on the party triangle's stop 2 (Nei): `$120D`, Nei's defense 64 -> 84, Rolf's 133 as it was |
| `map_rever` | 566 | (build 96c) Rolf's Rever from the C menu on a dead Rudo (story_dark_force.json with Rudo's HP at 0): `$10A`, 30 TP, Rudo back at 30 HP |
| `motavia_dam_patches` | 437 | (build 97c) Motavia's map by flag, one place three times: no flags, `$C735` (Climatrol destroyed) alone, and the four dams' panels too -- the canals fill |
| `mount_uzo` | 474 | (build 97c) A on the rocky mountain from 172,97: `$171F`'s first page only (the ROM's script ends there), the box closed by the next button |
| `earthman_leader` | 159 | (build 97d) Noah F1 from 33,10 with Mother Brain not fought: the Earthmen's leader (object `$28`) at 33,7, A at him `$1708`, and he blocks the step onto his cell |
| `esper_b1_hold` | 1315 | (build 97d) B1's Esper twice, then C during the hold (the one-shot case `$D`): no menu; the scene goes on to Lutz |
| `climatrol_motavia_menu` | 8654 | (build 97d) climatrol_nei_wins's way to the Motavia views after Climatrol (one-shot cases 4 and 6): C opens no menu over either |
| `oneshot_hold` | 136 | (build 97d) the one-shot case `$D` put in by the probe before a warp onto Noah F1: the party held, the C menu shut |
| `oneshot_clear` | 70 | (build 97d) the one-shot case `$11` the same way: the word cleared, the C menu opens and the party walks |
| `escape_menu_fullscreen` | 401 | (build 100) the Escape menu on the title, down to FULLSCREEN, C on and C off, B closed; PNGs of the menu with the item off and on |
| `escape_menu_computer` | 415 | (build 100b-100c) the Escape menu's COMPUTER PLAYS (NEW GAME; down six from build 110): C lights its lamp and the menu stays up; SPEED to FAST and back; the lamp put out, B; PNGs of the row, the lamp lit and SPEED |
| `locusta_arm_over_box` | 136 | (build 100b) two Locustas, the first one's attack: its arm drawn over the first name box's border (a plane B window's frame has no priority, loc_978C) |
| `telepipe_edited` | 531 | (build 104) a save with `telepipe_lands_at` edited by hand (Zema 23,12) over a raw_c600 that says a Visiphone save made on Dezolis: the Telepipe lands in Zema, not Paseo 31,16 -- the named fields win |
| `dezolis_planet_on_load` | 467 | (build 104) a Zosa save that says Motavia, Rolf's Magic Cap off by hand: into Zosa's Data Memory, "Danyaraha? bebekucha?" (`$810`) -- the level load sets the planet |
| `save_named_fields` | 682 | (build 106) a save with only build 106's names -- `last_recruit_joined` Shir, `last_recruit_sent` "Shir away", Rolf's `map_techniques_known` 3 with `knows_musik`: Rolf's house brings Shir back (`$0927`, the count 8 to 7, Shir last in the party), and Rolf's TECH list is RYUKA, HINAS, MUSIK; build 105 read neither name (the count 3) |
| `audio_menu` | 465 | (build 110) the Escape menu's AUDIO page on the title: AUDIO's value CHIP; SYNTH to Q, NOT BUILT under it and the chip still playing; MASTER stopped at 0 dB, down a dB a press, C round from 0 dB to OFF and on to -30; MUSIC down; B back with the values kept; PNGs of the row, the page, Q not built and the volumes |
| `audio_switch` | 1107 | (build 110) `sound render on` under the title's theme; `synth chip` at tick 420 of it, a held chord: twelve ticks on the mix still sounds (-16.8 dBFS; the key-ons left out of the replay, -48.6 and a FAIL); `synth q`, the chip again in a build without Q; then the sequencer's 2143 writes and their hash, the same as the route with its `synth` lines taken out |
| `audio_replay_silenced` | 855 | (build 169) `sound render on` under the title's theme; `sound play 254` at tick 420 of it, $FE, the driver's silence (SilenceFM's key-offs, the driver's own and no track's): silent 60 ticks on; `synth chip`, the replay the resync after FAST does too, and twelve ticks on still silent -- 168's track records kept the theme's chord down and struck it again (-16.9 dBFS, a FAIL) |
| `battle_big_frames` | 219 | (build 131c) Rolf at L40 against Dark Force: the tip of his jaw (the extractor cut his picture at $400 bytes), then Gra's bolt 60 ticks after FIGHT (battle.h kept 16 pieces a frame) |
| `pause_tail` | 792 | (build 132) Shure under its music, VOICES EXTRA: Start, 90 ticks, `expect sound silent` -- the chord's pad on the parts the pause jingle had lent rang on at -35.5 dBFS (fails on 131c) |
| `menu_pause_stuck` | 589 | (build 129d, restored in 132a) Motavia: the C menu's sounds, a pause over them, the unpause; `expect stuck none` (no music track left lent to an effect that no longer holds it) and the music playing again |
| `unpause_lost_part` | 603 | (build 132c) Motavia: pause, unpause, A three ticks later -- the selection sound takes the unpause jingle's FM5 track before its walk back up; `expect stuck $0E00`, music tracks 1-3 left down (the ROM's own lost part, kept) |
| `talk_mid_step` | 75 | (build 133) Aukba 11,9: a step up toward the Dezolian at 11,7, A five pixels into it -- the box ahead of the leader's pixel position reaches him, `expect script $1651`, and the step runs on to 11,8 (fails on 132d: the A held back, nothing shown) |
| `window_mid_scroll` | 159 | (build 133) Aukba 12,12: the step down that scrolls the screen, A four pixels in at nothing; five ticks later `expect pixel 160 196 $888`, the map -- ProcessWindows starts no window off the 16px grid (without it the box is opening there, `$800`) -- then `$1708` as the step ends (fails on 132d) |
| `fade_channel_order` | 82 | (build 138d) the Sega logo's white through the fade in from black and, after Start, the fade out: `$600` at the third frame (blue first; `$666` before), `$E60` at the tenth, `$EEE` at the 21st; `$EEC` as it leaves (red first; `$CCC` before), `$EC0`, `$000` at the 21st -- PaletteAddColor and PaletteDecreaseColor's order (fails on 138c) |
| `mother_brain_white` | 313 | (build 138d) the approach to Mother Brain from mother_brain_no's start: a black corner into white red first (`$008`, `$000` before), white at the 21st frame, the reload back down blue first (`$CEE`, then `$CAC`) -- LevelScreen's event_flags $E00 test, loc_634E / loc_62CC; then `$1813` (fails on 138c) |
| `damage_floor_restore` | 78 | (build 138d) Gaira's damage floor at 3,6: the white field, then line 2 entry 14 at its loaded `$44E` (loc_7140 copies the fade target back; `$004`, the pulse's, before) until loc_6430 writes `$008` (fails on 138c) |
| `library_graph_overlay` | 1978 | (build 138d) paseo_recorder_key_tube's way to the library's graph: a hidden line at 211,66 the background's `$A22`, then after $1012 the overlay (loc_C4B2's PaletteLoad2 $32, live line 1) shows it `$EEE` (fails on 138c) |
| `emulator_lab_damage_floor` | 448 | (build 138e) the emulator's slot 3 in the port: CONTINUE biolab_recorder.json, the Biosystems Lab B1 at 79,20, two steps left onto a damage floor -- the white field, line 2 entry 15 at its loaded `$08E` (the pulse had `$04A`), then loc_65D8's `$006` (docs/emulator_saves.md; passes on 138d too) |
| `emulator_mother_brain_white` | 724 | (build 138e) the emulator's slot 4 in the port: CONTINUE emu_mother_brain.json, Noah's first floor at the foot of Mother Brain's corridor, up into the arrival through white (`$008` at the third frame, `$EEE` at the 21st), back from white blue first (`$CEE`, `$AEE`), then `$1813` (passes on 138d too) |
| `portrait_pictures` | 1705 | (build 142) the Governor at the start of the game, then the walk home and Rolf's house: `expect picture` on the Governor's face (portrait 23, line 2), the black room beside it (none), Rolf's (his own art at $4A0, line 1) and Nei's arriving mid-scene (line 2). Fails on b141d (no `expect picture`, nothing drawn from a picture) |
| `rolf_picture_gate` | 64 | (build 142) the library with portrait 0 at its door, so Rolf's window opens where the graph's art sits at $4A0 -- the game never does this; the route does, to hold the gate: the window's pixels are the graph's tiles, `expect picture ... none`. With Rolf's picture bound whatever is loaded there, `expect picture` says rolf and colour_check finds about a thousand pixels a frame wrong (checked in build 142). Fails on b141d |
| `tower_alarm_picture` | 245 | (build 142) the Central Tower's desk with Nei gone ($C737 1): "Something's wrong..." then the portrait redrawn in place as the copy window (22, over 21's art and palette) for "Omigosh! The lake is overflowing!" -- `expect pixel 130 70 $4A0`, the guard running in, and `expect picture ... central_tower_outside_copy`. The copy, a painted window, was drawn under the stack until 142: the empty plaza ($888) instead. Fails on b141d |
| `effect_end_click` | 1019 | (build 143) the Governor's briefing under VOICES EXTRA (the default), each stop pressed and 18 ticks waited: eleven ticks after the press, the selection sound's end, `expect sound clicks below -28` at stops 4, 9 and 10 (-32.4, -36.6, -37.0 dBFS). On 142d RestoreBGMChn's upload landed on the music's sounding FM5 voice at the instrument's own levels: -18.7, -18.8, -16.0, and a FAIL at stop 4 (docs/audio.md, "Build 143") |
| `paseo_hospital_picture` | 209 | (build 145) the doctor's portrait (10) in Paseo's hospital, in through the door's bytes: `expect picture 56 64 doctor`, B, the goodbye, the fade out to the street (`none`). One of the twelve pictures no route drew after build 142; with the next six and `recruit_pictures`, every portrait picture is drawn by a route |
| `paseo_armor_picture` | 223 | (build 145) the armour seller's (14) in Paseo's armour store, the stock list beside it; B, the goodbye, the fade out |
| `paseo_teleport_picture` | 375 | (build 145) the teleport clerk's (30) in Paseo's teleport station: a new game knows no other town ($0C03, $0C04), and the party is let out |
| `tower_room_picture` | 405 | (build 145) the item keeper's (20), the way a player goes: the tower's desk, ROOM from "Where to?", the keeper's greeting and menu, B, "take care" ($0E08), the street |
| `aukba_hospital_picture` | 201 | (build 145) Aukba's hospital's Dezolian (17, dezolian2), from the street to the door: the greeting in Dezolian ($0810), C, the street. The four Dezolian portraits are one drawing in four palettes (PalettePtrs 33-36) |
| `aukba_item_picture` | 201 | (build 145) Aukba's item store's Dezolian (19, dezolian4), as above |
| `zosa_item_picture` | 201 | (build 145) Zosa's item store's Dezolian (18, dezolian3), as above. With portraits.json naming dezolian4 for window 18, `expect picture` says dezolian4 and 47 of the first 128 frames differ (checked in build 145) |
| `recruit_pictures` | 1317 | (build 145) Rolf's house with Anna the last to join (`members Rolf Rudo Anna`) and Kain sent (`join-next 6`): the knock, Kain's introduction beside Rolf's window (RolfHouse_Arrival), NO to renaming, then SEE STRENGTH for Rudo, Amy, Hugh, Anna and Kain, each `expect picture`, and OUTSIDE. Both of the house's portrait loads in the middle of a scene. With window 6 named anna, `expect picture` says anna and 112 of the first 249 frames differ (checked in build 145) |
| `gameover_to_sega` | 889 | (build 157a) gameover_message's lost fight, then C at the message: SegaScreen's $E0 and its fade over the message and picture (red first) before the logo; until 157a the port cut to the logo at once; (157c) the battle's windows through the erase |
| `damage_floor_field` | 46 | (build 157b) Gaira's damage floor: the white field is all 64 entries (a star of line 3 entry 10 too), seven fields as loaded, then the iteration's rest -- the HP, loc_6430's step -- at the stall's end; until 157b the rest ran in the white field's tick |
| `map_wipeout` | 901 | (build 157c) a level-1 Rolf on Gaira's damage floor until "ROLF is dead!": GameOverScreen over the level -- the music, the level, Rolf and the window erased (his art not sent again), the message, the SEGA screen; until 157c the port went straight to the SEGA screen |
| `rumble_fight` | 2223 | (build 160) formation 1 against a new game's Rolf and Nei: the first blow on Rolf reckoned at tick 249 (19 HP to 17) with the motors still, and felt at 269 as it lands (SXFID_Sword, loc_3308), 4 ticks held and 10 down at 2 of 19 HP; the fight's eight blows in all. `rumble-log` at HIGH. (build 161) The kick at 269, the sword's sound followed 270-282, the red flash's from 286 to 333, still at 340 |
| `rumble_lost` | 606 | (build 160) a level-1 Rolf against Mother Brain: one blow, hurt and fell in the tick it lands, then the game over, lost, the longest shake (still 160 ticks on) |
| `rumble_floor` | 506 | (build 160) Gaira's damage floor from a level-1 Rolf: each bite felt with its white field, its point eight ticks on not felt again; nineteen bites, the last point his fall, then lost; (build 161) the buzz 8 + 6 |
| `rumble_hole` | 139 | (build 161) a level-5 Rolf walked from cell 50,6 into Crevice's crevice (loc_6FCA): SXFID_FellInHole asked at 54, the driver starting it at 55, its shake followed to 138, still at 139; Crevice B1 |
| `rumble_flight` | 622 | (build 161) roof_yes's flight to Skure: the departure followed as the ship fades in, the throb, the pause in SpaceShipLoop stilling it 45 ticks, Start bringing it back, landed at 580 (both full, the landing's sound till the level's music cuts it), still on Skure |
| `rumble_menu` | 407 | (build 160) the Escape menu's GAMEPAD row (above QUIT) and page: PAD 1 and 2 NONE (the probe has no gamepads, and C on one does nothing), RUMBLE MEDIUM, left and right stopping at OFF and HIGH, C round from OFF to LOW; three shots |
| `controls_keys` | 596 | (build 160) the CONTROLS page (the keys): the main page's CONTROLS row with no value, the defaults (ARROWS and WASD both), UP's KEY 2 to Z taking Z off A, the slot blinking (16 ticks on, 16 off), Escape and a gamepad's Back keeping it, Backspace emptying it, gamepad inputs not heard, the up arrow unbound still moving the cursor and Enter listening, the d-pad moving it too, the unbound up arrow held repeating (three rows in 70 ticks), DEFAULTS the keys only (a BUTTONS slot set first stays); seven shots |
| `controls_buttons` | 584 | (build 160) the GAMEPAD page's BUTTONS row and the BUTTONS page: the defaults in positional names, then as a DualShock (SQUARE CROSS CIRCLE), an Xbox One pad (X A B, MENU) and a Switch pad (Y B A, PLUS) label them; a face button, a right stick's direction, a trigger and a mapless joystick's button bound; a bind taking the input off B; Back and Escape keeping; keys not heard; Backspace emptying; the d-pad unbound still moving the cursor, and held repeating; DEFAULTS the gamepad only (a CONTROLS slot set first stays); eight shots |
| `lang_menu` | 432 | (build 163) the Escape menu's LANGUAGE (above QUIT): ENGLISH 1989 with no setting, C to RETRANSLATED, (165) JAPANESE, and round, left and right both ways; three shots |
| `lang_shop` | 157 | (build 163) ac_slasher_name's store in the retranslation: his 0602 with the item's name whole ("The Laconia Slicer, huh?"), `expect pixel` on the top of the T, where the stock font has paper |
| `lang_battle` | 2339 | (build 163) formation 1 in the retranslation: his wide battle box (its left border at x 60, the floor's in the stock box), "Eusis and the others won!", the experience and meseta lines |
| `lang_names` | 2605 | (build 163) the retranslation chosen, then the cartridge's again before the fight: "ROLF and party", the default name back |
| `lang_gameover` | 852 | (build 163) gameover_message's lost fight in the retranslation: his message on the picture, frameless |
| `lang_crawl` | 3455 | (build 163) the attract crawl in the retranslation: his second message, a line a row in his face on the box's fill |
| `lang_new_name` | 2437 | (build 163) new_game's name board in the retranslation, ROLF typed: his dream's "My name is ROLF. I work as an agent" -- four typed letters the whole name, the six-letter default's fifth gone (on 163's first cut "ROLFs") |
| `lang_menu_pages` | 226 | (build 164) the C menu in the retranslation: the main menu a cell wider (Techniques' last s at 74,50, its border at x 87), "To whom?" on the party list's border, the technique list a cell wider (Ryuker; its border at x 175), the Strength page (Dexterity) |
| `lang_battle_lists` | 1141 | (build 164) battle_tech_next's way in the retranslation: the enemy's box ("Arch Dragon"), the four pages of Amy's techniques, page 4's Nasaresta whole (its last a at 130,139, in the sixth cell) |
| `lang_house` | 1448 | (build 164) recruit_pictures' way in the retranslation (a press fewer): the house's options, each recruit's job and five-line profile, Amia's "whips" (not his rebalance's knives) |
| `lang_places` | 149 | (build 164) Paseo's teleport station with three towns known: his Arimaya, Optano, Skure |
| `lang_six_letters` | 2462 | (build 164) ROLFIE typed on the name board in the retranslation (six letters, his long_names): the dream's "My name is ROLFIE." |
| `lang_ja_shop` | 118 | (build 165) lang_shop's store in JAPANESE: 0602 in kana, the store's list and the party's names in the cartridge's face; the dakuten over だ at 100,190, in the line's upper row |
| `lang_ja_crawl` | 3455 | (build 165) the attract crawl in Japanese, two rows a line, the mark over ゴ at 172,150 |
| `lang_ja_pages` | 226 | (build 165) lang_menu_pages' way in Japanese: the labels from the cartridge's art (the mark over じ at 36,30), だれが? and だれに?, the stock widths |
| `lang_ja_name` | 2198 | (build 165) the Japanese name board: アイウ typed, オワル, the dream's "ぼくの なまえは アイウ。" |
| `lang_ja_names` | 2614 | (build 165) ユーシス across to Eusis: Japanese, then the retranslation before the fight |
| `lang_switch_menu` | 58 | (build 166) LANGUAGE changed with the C menu open, through the three and back: every window built again (his I of Items at 32,16; the mark over じ; the stock I again) |
| `lang_switch_message` | 142 | (build 166) mid-message to Japanese: typed again from its start in kana (on 165 the rest of the US codes in the Japanese face); finished, to the retranslation: shown whole |
| `lang_switch_battle` | 111 | (build 166) a fight's painted boxes built again: the mark over the enemy box's バ, then his B of Biting Ant |
| `lang_switch_crawl` | 3457 | (build 166) the crawl's box kept, its text Japanese (on 165 the box was gone) |
| `lang_switch_board` | 2226 | (build 166) the US name board, B typed, to the Japanese board: ア typed, the dream's "ぼくの なまえは ア。" |
| `lang_switch_select` | 1346 | (build 168d) NEW GAME / CONTINUE / ERASE GAME after the backup check, to the retranslation, Japanese and back: the menu built again, CONTINUE's lamp at 65,56 (until 168d the check's last message typed over the menu) |
| `lang_ending` | 10494 | (build 163) ending's way, the retranslation chosen as the ending screen comes: the first page, the second, the closing line |
| `controls_rebind` | 734 | (build 160) end to end: START's KEY 1 to Q and DOWN's 1ST to the right stick down through the pages, then on the title Enter does nothing and Q starts, on NEW GAME / CONTINUE the d-pad down does nothing and the right stick down moves the cursor (the down arrow still does); one shot |
| `fade_out_overrun` | 540 | (build 183) Gaira's control panel from the cell below it, under Mystery's first chord voice: the explosion's step at its frame 17 writes its own volume to FM3's three carriers (`expect sound-register 1 $46 $08`, `$4A`, `$4E`); the music's fade at frame 22 copies the chord's voice over the explosion's track, and the steps at frames 25, 33 and 41 write the volume on the chord's levels (`$37 $22 $14`, `$3F $2A $1C`, `$47 $32 $24`). Fails on 182 at frame 25 (`$10` in all three) |
| `fade_out_overrun_second_chord` | 1740 | (build 183) the same after 1200 frames on Gaira, under Mystery's second chord voice (one carrier): after the fade only operator 4's register steps (`$4E`: `$10`, `$18`, `$20`) and the other two stay at `$08`. Fails on 182 at frame 25 |

The Prism, Piata and Rolf routes start from saves in `routes/saves/`
(slot 1 with the location, `$C712` and Rolf's HP changed); the two
scooter routes from `jet_scooter.json`, slot 1 moved to Motavia 89,116
with `$C716` = 1 and the scooter parked where `loc_E690` leaves it. They cover `loc_6EA6`
(ps2.asm:10231), which the port's door lookup skipped until a sweep of
every door record found it: two records say building 1 and are not
buildings. `$37` is Rolf's own door, which with `character_data_buffer+2`
at 0 starts `$DE6E = $303` (ObjType_TeimDarum 3, message 6) instead of
opening. `$3E` is Piata's tower, shut until `$C712` is set at the
Governor's debrief (`loc_CA58`) and after that a plain door whose last
three bytes are a level and a cell. The port had entered Rolf's house at
both, the tower asking for window `$3B+$3E = $79`, which does not exist.
`interact()` in walk.c now sets both aside first; game.c copies `$C712`
and Rolf's HP into the Walk before every tick.

Shure F3 is two routes because the pocket body is some 186 steps of
maze from the others, on a floor with encounters.

`dezolis_prism`'s save is a Visiphone save, `$C65A` = 1 and `$C65C` = 1
(`raw_c600` row 5): CONTINUE copies `$C65C` to `$FFFFF766`, and with it 0
Data Memory runs `CheckPlanetAndCaps` first (`loc_B44A`), which on
Dezolis without a Magic Cap is `$810` and the street. A save made in a
Motavian Data Memory and moved to Dezolis by hand does exactly that.

**Techniques by the enemy's type (build 88).** `fight_smart` orders Kain's
and Hugh's damage techniques by the live groups' type (`play.TYPE_TECHS`,
`plan_type_tech`; `s.tech_orders` records them); `member_value`,
`choose_party` and `pick_party(ahead=...)` weigh a member's techniques by
the enemy mix of the levels ahead (`dungeon_mix`). `dam_trials.py
--type-techs none` is the old fight_smart, `--late 0` clears `$C737` for a
Motavian dungeon's first group. docs/history.md, Build 88;
docs/type_trials_build88.md.

**Planning across levels (build 89).** `plan()` is breadth-first over
(level, arrival cell), fewest level changes first; whether a way out can
be walked to is `LevelMap.reaches`, from `LevelMap.pieces()` (the floor
path() crosses, labelled once a map), not a path() search per pair.
`tools/plan_check.py` times the Dezolis plans with the Prism and, with
`--equiv`, checks `reaches` against `path()` everywhere. docs/history.md,
Build 89.

**Bosses (build 94).** Against an enemy that cannot be run from,
fight_smart orders every member's damage techniques at a lower bar, none
to a member the curse sealed, and Deban while half the party is without
it; `shift=True` adds Shift twice a fight. Anyone left on a technique not
planned this round goes back to ATTACK (`reset_techs`: a stored technique
repeats every round, loc_251C). `stop_on_gameover` hands back on a loss.
The state line shows each member's `cursed K`, `marked`, `sealed` and
`deban`. `tools/boss_trials.py --boss dark_force|mother_brain --policy
old,techs,deban,both,all`; docs/history.md, Build 94, has the numbers.
story.py's last stages: boss_party, dark_force, mother_brain (through the
ending to the Sega screen).

**Walks that heal (build 96c).** `play.revive()` brings the dead back on
the map: Rever from whoever knows it and has the TP, else a Moon Dew. With
`s.walk_mend` set to a fraction, `goto` revives, cures poison and mends to
that fraction after every fight on the way (None, the default, is the old
rule: mates only once someone is under half). story.py's walk across Noah
to Mother Brain uses it with `WALK_FIGHT` -- heal techniques and mates in a
fight only for someone low, no damage techniques, and no RUN: fight_smart's
RUN under a quarter, failing round after round against Noah's Vorcanos, is
what wiped the build 94 walk out.

**The walk trial (build 97).** `tools/walk_trials.py --walk
mother_brain|dark_force --policy ... --repeats N` walks from Lutz's YES to
the boss's door many times per fight policy: a start save made once by the
stage's preparation and `save-here` (routes/saves/walk_<walk>.json), a
fresh probe a run, `wait ticks 7k` so each run meets different fights, the
same offsets for every policy; `--first`/`--append` extend a trial. It
settled both walks' policy: `WALK_FIGHT` weapons and mates with no RUN,
`WALK_MEND` 0.5 (docs/walk_trials_build97.md) -- 96c's heals in a fight
were Res, spending the TP the boss wants.

**Build 90-91.** `tools/probe_check.py`: checks of the probe that a route
cannot express (one start a route): `start slot 1` onto the session's own
slot. `tools/boss_trials.py`: Dark Force with each party of Rolf and three
from a save. `dam_trials.py --stores all --outfit --candidates` (the Noah
trial). `plan_check.py --part K/N --sample N`. story.py's report tallies
members down and how each came back (Session.downs / revivals).

**Build 108.** `play.WIPED`, the screens a wipe-out leaves the game on (game
over, SEGA, title): `Session.ready()` returns False and `press_through()`
returns at once on them, where they used to press C at the title up to 8000
times (docs/computer_plays.md, "A wipe-out the waits missed"). probe_check.py
gains `ready_at_the_wipe_screens`. Seed 3 played with 107's tools and 108's:
story.txt identical, the report bar its seconds.

**Build 157.** route_probe attaches the interpreter before game_init, so
what the game writes to stderr while it starts reaches the log; a failed
start ends the log with `game_init failed` and `exit 2`. Until 157 a
failed load's message went to a host not yet attached, and the probe died
(docs/history.md, Build 157). probe_check.py gains `missing_asset`.

**Build 162.** probe_check.py gains `driver_on_python38`: COMPUTER PLAYS's
driver and the modules it imports parse as Python 3.8 (ast's
feature_version) and call none of the newer Pythons' methods and keywords
it lists (Path.write_text's newline, str.removeprefix, zip's strict, ...);
no probe needed (docs/computer_plays.md, "On Windows").

**The whole game, one seed (build 98).** `tools/playthrough.py --seed N`
plays from `start new` to the ending screen by story.py's stages, unchanged
in what they decide. Two things are added. A seed: `Session.jitter` (a
`random.Random`) is a player's pauses -- before a walk or a press on the map
or in a fight, one time in four (`play.JITTER_P`), 1-24 ticks
(`JITTER_MAX`) with nothing pressed, never inside a `.route` file; the RNG
turns every frame, so every fight and blow after a pause differs by seed,
and each stage try draws its own stream (`seed/stage/try`). Lean grinding
(`play.LEAN`): a grind out of trips or time short of its target goes on
healed and geared in town (`play.lean_short`: grind_and_gear,
dungeon_grind; story.py's climatrol_grind, dam_grind, kain_grind,
noah_grind, and Shir's thefts), and the Prism dungeons' time budget is
lifted -- the Nei items are the story. A stage that does not finish is
played again from the checkpoint before it, up to `--tries` (4; build 118:
6, and a top-up first, under squeak-by below), with the
dead raised first (`raise_dead`); a stage only counts once its checkpoint
was written, and nobody is saved dead. A harness exception or a probe
stall is a lost try, reported. `--from STAGE` continues that seed's own
checkpoint; output under `route_out/playthrough_s<N>/` (the docstring).
(build 99) A seed replays exactly: no wall clock decides anything (the
Kain and Noah grinds count trips, the thefts store exits, the Prism
dungeons have no limit) and the run pins PYTHONHASHSEED. (build 170) The
stream is `play.Jitter`, which C draws the same (FNV-1a of `seed/stage/try`
seeding a SplitMix64), and no choice takes the first of equals from a set
any more, so a seed's game is the same under any hash seed and on any
Python (docs/c_player.md, "Step 0"). On Noah the last
Moon Dew is kept for the Nei Sword's holder (`Session.dew_reserve`,
`story.restock_dews` before each trip).
With no seed and LEAN off, play.py and story.py behave as before: the
routes and the boss trial are unchanged (build 98's proof). story.py has
`--saves DIR` for where its checkpoints go (default routes/saves).
(build 168f) `--language KEY` (us, retranslation, ja) tells each probe the
language at its first tick, as LANGUAGE on the Escape menu would (a new
game and a CONTINUE keep it), and writes under
`route_out/playthrough_s<N>_<KEY>/` (us keeps `playthrough_s<N>/`); with
`PS2_TEXT_CHECK` set the report's port stderr is the text check's.
`--seeds` passes it to every seed.


**Parallel runners (build 114).** `make -j12 routes` runs a route a job
(each route is a target, `route-<name>`; every route runs whatever the
others did, and `routes` then fails if any route's summary.txt is missing
or does not say `exit 0`). `boss_trials.py`, `walk_trials.py` and
`fuzz_levels.py` take `--jobs N`, and `playthrough.py --seeds 1,2,5-8 --jobs
N` plays each seed as its own `--seed` run; the files come out the same at
any N. `tools/jobs.py` runs a trial's runs in spawned worker processes
(fresh on every OS: nothing the parent did reaches a run), yields their
results in order, pins the workers' PYTHONHASHSEED (0 unless set, as
playthrough.py's), and gives each worker an index for a probe session of
its own (route_out/boss_trials/w<N>, route_out/walk_trials_<walk>/w<N>):
two runs at once never share a session directory. fuzz_levels' runs were
separate probes already (threads start them); playthrough's seeds are child
processes, each under its own route_out/playthrough_s<N>/. boss_trials used
to play every fight in one session, each starting where the last left the
RNG, which no split can reproduce: now each fight is a fresh probe with its
repeat's offset (`--step`, 7 ticks, the same for every triple, policy and
arrival), walk_trials' way since build 97 -- a new draw of the trial, and
`--chain` keeps the old one for the tables up to build 113.

**COMPUTER PLAYS (build 100b).** The game runs the same interpreter against
its own host and `tools/computer_plays.py` as the driver: docs/computer_plays.md.
(build 179) The player is the C one now, on the game's own thread; what
follows is the Python driver's, which `PS2_COMPUTER_DRIVER=python` still
starts.
`play.Session(attach=(cmd_fd, log_fd))` talks to the game's pipes instead of
starting a probe; `Session.saves_dir` is where the game writes the session's
slots (the probe's `route_out/<name>/saves`, as before).

## Grinding by long walks (build 102a)

`play.roam_and_gear(s, town, level, ground)` is `grind_and_gear` with long
walks in place of a little square by the town's gate: in town the hospital,
gear and mates as ever; out onto the ground (through Paseo's tube from
Paseo); `play.roam` walks anchor to anchor, fighting what comes, Res after a
fight, mates when hurt, 8 fights a trip; back to town fighting on the way.
A `play.Ground` is the open cells of some encounter groups
(`play.encounter_group`: Motavia's on-foot table, ProcessRandomBattle's
lookup) reached from a cell, with every other open cell kept off the walks'
plans (`goto`'s `keep_off`). `story.mild_ground()` -- groups 0-2 north of
Paseo's tube, Darum's approach to Arima's gate -- is what stages `arima` and
`gear` grind on (docs/history.md, Build 102, task B; the picture is
`route_out/mild_ground/`, `tools/ground_map.py`). The other grinds still
walk patterns (`wander`), each walk cut short at the first solid cell
ahead (`play.open_walk`): the probe's `walk` would stand against a wall for
BLOCK_GRACE (120 ticks) before giving up.

(build 103) A dungeon floor is a ground too: `play.encounter_group` gives
a level's own byte off Motavia (and Skure's table), and a `Ground` there
keeps the floor's stairs, holes and damage floors out.
`play.dungeon_grind(..., ground=, gear=)` walks it with `roam` instead of
standing at one cell, and gears up in town at every return. Stage `lab_in`
grinds the Biosystems Lab's ground floor that way (`story.lab_ground()`,
six anchors) from Amy's joining to everyone L17. `tools/grind_ledger.py`
(playthrough with a ledger of ticks and levels by fight; `--summary`) and
`tools/grind_trials.py` (one grind policy from a save for a tick budget:
EXP a member a 100 k ticks, wipe-outs) are how the grinds were measured
(docs/history.md, Build 103).

(build 103a) With `play.MID_SAVES` set (playthrough.py, computer_plays.py),
the grinds save on a trip back to town when a level rose, someone joined or
a story point passed (`play.trip_save`), and a failed stage is retried from
its last such save (docs/computer_plays.md, Saves on the way). (build 118)
Never with anyone dead: a save with the dead in it and no money for the
clone lab sent every retry back into it.

## Squeak-by (build 118)

The owner: "I don't think you should be grinding to assure 100% survival in 1
playthrough. That's why saves exist. Live on the razors edge squeek by mode.
The grind is to accomplish bare minimum survival gear and levels. Getting
best gear is better than level padding." playthrough.py and
computer_plays.py play that way by default; `--no-squeak` plays the story's
own targets as before (and story.py always does).

* `story.squeak()` lowers every level target a level or two: the Paseo
  grind L7 (8), the lab L15 (17), Climatrol Rolf 20, Rudo 20, Anna 17 (22,
  22, 19), the dams' grind 22, 22, 18, 20 (24, 24, 20, 22), Skure's L28
  (30), Zosa's L31 (33), the Prism dungeons' 30, 32, 34, 34 (32, 34, 36,
  36), Kain L18 (20), Noah Rolf 35 and Kain 24 (41, 30; 39 and 28 in
  build 118). The stages that
  grind for gear (gear, oputa, amy: until the town's shelf is bought) keep
  theirs, and gear is still bought at every return to town.
* A failed try is retried from its last save, as ever, but first
  `story.top_up`: three trips (about 24 fights) for each try already
  failed -- 3, then 6, 9 -- of the latest grind stage's own grind
  (`story.TOP_UP`: Paseo's long walks, Arima's, Oputa's field, the lab's
  ground floor, Zema's field, the Underwater Passage, Piata's field, Zosa's
  field; on Noah its own grind, Rolf and Kain a level up for each try
  already failed, build 119). A retry
  resumes from the same save as the one before (the top-up is not saved),
  so each is ground a little further than the last, and each seed pays for
  the levels it turns out to need. (build 119) A top-up that ends wiped out
  goes back to the save, and the dead are raised again before the stage.
  (build 168f) Zosa's top-up (Skure's, Dezolis's grind's and Kain's)
  takes Skure B1's Magic Cap first when the party carries no cap -- a
  Skure retry starts from the roof's save, before Skure's chests -- and
  every Zosa or Dezolis town errand starts with `story.cap_for_shops`:
  Rolf alive (the clone lab), then a cap fetched to him from whoever
  carries it and put on. Zosa's shops, hospital and Data Memory answer
  only to a live Rolf in a cap; until 168f the top-up shopped and healed
  bare-headed there, nothing sold, and the party went back to Skure hurt.
  None before paseo, motavia, shir, dam_party, cards and roof
  (`NO_TOP_UP`: nothing ground yet, the other planet, or a party just
  changed).
* Six tries a stage (`SQUEAK_TRIES`), not four.

Measured (docs/history.md, Build 118): seeds 1-12 whole games, 15.26 M
ticks a game to 12.82 M at the watch pace (COMPUTER PLAYS's) and 24.23 M to
19.83 M at the probe's, every seed to the ending at both; 40 and 46 failed
tries in all against 19 and 19, paid for and still cheaper. Dropping the
late grinds altogether (the Dezolis dungeons' chest runs paying the way)
came out worse: 13.34 M at the watch pace, the levels pushed onto Noah, 64
failed tries.

(build 119) Noah at 35/24 and the two retry fixes: 12.41 M at the watch
pace (~57 h), 12/12, 44 failed tries. Every other target a level lower
(12.40 M, seed 2 lost at Skure) or two (12.06 M, seed 1 lost at Uzo) buys
nothing worth a lost seed.

**Without Kain** (build 119): `playthrough.py --no-kain` (`story.no_kain()`)
takes Noah and the bosses with Rolf, Rudo, Amy and Anna, the party the Nei
Sword came with. The kain stage is then only Shir's thefts (Anna out for
her round and back), kain_grind does nothing, Noah's grind runs until Rolf
is at `NOAH_KAIN_ROLF` (37 under squeak-by, 41 without), and boss_party
has nothing to change. It came out worse over 36 endgames from the Nei
Sword: 2.63 M against 2.41 M, 35/36 endings, 233 Dark Force losses against
154. Kain stays the default.

## Bag room (build 169)

A store answers a purchase for a full bag (sixteen items) with "no room"
($0609, $0709) and shuts (building.c, `equipment_store`'s EQ_CHOSEN). So
whatever came after it on the list was never bought: gear_up's "not
bought?" lines. 168f's last grind had 33, all of them a full bag:
- Zema: Rolf's two Laser Knives filled his bag, and his Boots then shut
  the armour store on Hugh's and Rudo's too.
- Ryuon: Kain's Garda Boots, at every visit, kept off by his Trimates.
- Amy's second Lacon Emel, and Rolf's Titanium Chest at Kueri.

`play.plan_gear` now counts each member's room, so a purchase is planned
only into a bag with a free slot when it is made. gear_up GIVEs before it
buys, so a piece given to someone else frees its slot first. A buyer still
full hands over one consumable first, a "stow": `play.STOWABLE`, the mates,
Antidote, Star Mist and the pipes, never a key item, a piece of gear or a
Moon Dew (whose holder matters to `story.reserve_dews`). It goes to the
living member with the most room: `gear: Amy Hidapipe (from Kain, for
room), Kain GardaBoots`. A full buyer with nothing to hand over is planned
nothing. With `unlimited` (what is still worth grinding for) the room is
not counted, as before. 169's grind: no "not bought?", 12 stows.

## Names in the logs (build 106)

The story logs, the probe's log and the state line name what they show:

* **Levels** by the name `assets/levels/levels.json` gives them: the state
  line is `..., level Paseo cell 45,18 facing down`, the probe's level
  change `*     level DarumTube, leader at cell 27,9`, `save-here`'s `slot 2:
  level Paseo cell ...`; a level with no name would show as `$XX`.
  `play.parse_state` reads either (`play.state_level`), so an older probe
  still drives the harness. The route language still takes the number
  (`start level $0E 27 9`, `expect level 4 45 18`): routes are read before
  the game loads its tables.
* **Scripts** by name (`play.script_name`, `play.scripts_text`):
  `src/game/script_ids.h`'s `SCRIPT_` name less the prefix where the game's
  code uses the id (`ACTION_NOTHING_UNUSUAL_HERE`, `STOOD_IN_AMAZEMENT`),
  else `tools/gen_script_ids.py`'s rule for the rest (the bank, then the
  line's first words; `_2`, `_3` for a name taken, ids in order). All 611
  scripts have one, all different: the text is in `assets/script/`.
* **Townspeople** by kind and the cell they are placed at
  (`play.person_name`, from `level_sprites.json` and `npc_types.json`):
  Skure B1's cats are `MuskCat at 23,71`, `MuskCat at 25,71`, ...
* **Chests, doors, trips, towns** by level: `chest on SkureB1 (9, 49)`,
  `trip 0 on BiosystemsLabGroundF`, `Aukba gear: ...`; the tower's card
  boxes by card (`the GreenCard's box:`); a party's enemy mix by type
  (`[mix living:0.10 both:0.90]`, play.ENEMY_TYPE_NAME).

Still numbers: the probe's `event $XXX` line (a level event's number) and
the route language's arguments.
