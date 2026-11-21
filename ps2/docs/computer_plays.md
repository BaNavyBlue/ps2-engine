# COMPUTER PLAYS (build 100b): the design

The Escape menu's COMPUTER PLAYS hands the game to the harness: the campaign
is played by story.py's stages through playthrough.py's loop -- grind and
gear by the rules, fight_smart, lean grinding, the reserve Moon Dew -- in the
window, and Escape or any button hands the pad back. This file was written
before any of the code; where the code later differed, the section says so
("(as built)").

(build 179) The player is C now: `src/player/` (docs/c_player.md), on the
command thread inside the game, with no child process, no pipes and no
Python. What follows about the driver, its pipes and Python describes
builds 100b-178, and the Python driver still runs that way under
`PS2_COMPUTER_DRIVER=python`, the reference the C player is held to
((build 180a) kept as one, with playthrough.py: The owner's choice). "The C
player in the window (build 179)", at the end, is the shape now.

## The shape

```
 window (main.c)                                driver (python3)
 +-------------------------------+   commands   +---------------------------+
 | main thread: frames, the pad, |  <---------  | tools/computer_plays.py   |
 | game_tick on request, render  |   fd 3       |   playthrough's loop      |
 |                               |              |   story.py's stages       |
 | command thread: reads a line, |   log lines  |   play.Session, attached  |
 | runs it with the probe's      |  --------->  |   to the pipes instead of |
 | interpreter, one tick at a    |   fd 4       |   a route_probe it starts |
 | time through the main thread  |              |                           |
 +-------------------------------+              +---------------------------+
```

**One command interpreter, two hosts.** `route_probe -i`'s command language
-- parsing, `press`, `walk`, the waits, the `state`/`bags`/`hands`/`techs`/
`objects`/`roster` lines, the expects -- moves out of `tools/route_probe.c`
into `src/harness/probe.[ch]`, compiled into the probe and into both game
targets. It advances the game only through a host's tick function and
reports only through a host's log function:

* the probe's host ticks `game_tick` directly, then drains the audio queue
  and reads the captured stderr, exactly as its `tick()` did; it keeps what
  only a test rig has: route files, the output directory, PNG writing, the
  stderr capture, the summary line. Its behaviour is unchanged, and proved
  so (every PASS line, every route file, the Dark Force trial);
* the game's host (`src/harness/autoplay.[ch]`) runs the interpreter on a
  thread of its own and hands each tick to the frame loop.

**The handoff.** The command thread holds the game while it interprets a
command. When the command wants a tick it posts the tick's input and waits;
the main thread, at its next frame, runs `game_tick` with that input, draws
the frame if one is due, and releases it. While the command thread reads its
next line from the driver it holds nothing, and the window shows the last
frame. So the two never touch the game at once, and the command code is the
probe's own blocking code, unchanged: a `wait idle` is still a loop of ticks,
each one a frame in the window.

(build 169, the owner: "why is the computer play logic writen in python
instead of C?") Because the driver was there first. play.py and story.py
are the test harness that plays the whole game against `route_probe`. They
were there before build 67, where docs/history.md begins, and do the
grinds, the seeds and the retries: about 9,000 lines by build 168. COMPUTER PLAYS put that player into the window rather than
write a second one in C. With one player, the window plays the game the
probe proves: a seed's story log is the same at every SPEED and in
`playthrough.py`. The C side is what both share, the route language's
interpreter (`probe.c`). The cost is a Python 3.8 or newer at run time
(`make dist` bundles one on Windows) and a pipe's round trip a command.

**The driver** is `tools/computer_plays.py`, started by the game as a child
process: `python3 tools/computer_plays.py --attach 3,4 ...`, its commands on
file descriptor 3 and the log lines back on 4 -- not stdin/stdout, since
story.py's report prints to stdout. Its stdout and stderr go to
`saves/computer/driver.log`. `play.Session(name, attach=(3, 4))` reads and
writes those pipes instead of starting `./route_probe -i`; everything above
the Session -- the stages, fight_smart, the rules -- is the harness as it is.

## What it needs at run time, and when it is missing

(build 179) The C player needs only `assets/data/items.json`,
`routes/saves/story_*.json` (to tell a stage from a save; the first,
`story_paseo.json`, is the one checked) and `src/game/script_ids.h` (its
story log's script names), from the pack or beside the game: NO TOOLS
when one is missing, the file named on stderr. What follows is the Python
driver's.

`python3` on the PATH; `tools/computer_plays.py`, `play.py`, `story.py`,
`playthrough.py`; `routes/new_game.route`; the harness's data under
`assets/data/` (levels, doors, stores, items -- play.py reads them); and
`routes/saves/story_*.json` to tell a stage from a save. The game checks when
the Escape menu opens. With something missing, COMPUTER PLAYS's value column
says `NO PYTHON3` or `NO TOOLS`, choosing it does nothing but the menu's
sound, and one stderr line names what is missing.

The child is started with `fork`/`exec`, the pipes with `pipe`, the thread
with pthreads: POSIX, shared by the SDL3 and the headless targets so the
headless test runs the window's own path. A Windows build would need its own
`autoplay` process and thread file; the SDL3 target is Linux-only for this
feature until then. (build 115: Windows has it, in the same file -- "On
Windows (build 115)" below.)

## The audit: every probe command the stages send

Counted over a whole game (seed 11 on 99b, the probe's own log: every
command, 1.6 million lines).

| Command | Where | A player's? | In COMPUTER PLAYS |
|---|---|---|---|
| `press`, `walk`, `wait idle/ticks/screen/step/ending`, the seed's pauses | everywhere | yes | as is |
| `state` (684,171), `bags`, `hands`, `techs`, `objects`, `roster` | play.py's reading of the game | yes: what the screen and the menus show | as is |
| `expect flag` (36) | `play.nei_available` (whether Nei is alive), story.py's checks that a scene happened (the Recorder, the Key Tube, the dams) | read-only; what a player knows from having played | allowed, as a state query |
| `expect script`, `expect screen`, `expect no-stderr` | `routes/new_game.route` (until build 168e; the driver no longer reads it) | read-only checks | as is (no stderr is captured in the window: the check always passes there) |
| `expect setting language` | (build 168e) `play.japanese_board`: which name board is up, at the new game and the Data Memory saves | read-only; what a player sees | allowed, as a state query |
| `limit`, `shot` | the probe's time limit; the bosses', the ending's and failed tries' PNGs | no game effect | `limit` as is; `shot` logged, nothing drawn |
| `start new` (1) | `Session.new_game` (until build 168e `routes/new_game.route`), the first stage | power on | the console's reset button (below), then the title by the pad |
| `start slot N FILE` (8) | `play.continue_game`: a stage retried from its checkpoint (5), a CONTINUE after a wipe-out mid-stage (3) | **no**: a save copied in and loaded with no title | the file copied into the computer's own slot (its own memory card), the console reset, and the title's CONTINUE by the pad |
| `save-here` (2) | `story.checkpoint` in the Skure field: the roof stage's end (no cap, no Visiphone) and the shir stage's (the flight back from Paseo) | **no**: a save of anywhere | no save there: the stage is played on without a checkpoint, and a retry of the stage after it goes back to the last real one and plays both (below) |
| `give`, `warp`, `members`, `member`, `outfit`, `enemy-hp`, `oneshot`, `flag`, `join-next` (145), `meseta`, `exp`, `party`, `defense`, `keep-equipment`, `fuzz`, `start level/building/battle`, `expect pixel` | never by a stage (`give` only by the trials: boss, dam and party trials, grind_rate) | no | refused by the game's host: the command fails with "not a player's" and does nothing |

So no stage is left out of COMPUTER PLAYS. Two stages lose their checkpoint,
`roof` and `shir` (as built: the design said `motavia`; the probe's log puts
the second `save-here` at the end of `shir`): both end in the Skure field at
60,79, where a player can save only with the Visiphone or at a town's Data
Memory with Rolf in the Magic Cap, and the stage after each starts from
exactly that spot with a few steps to the roof's door. The driver plays on
through; if `skure` or `menobe` then fails, the retry CONTINUEs the
checkpoint before (`dams`, `motavia`) and plays the lost stage again first.
(build 180) The lost stage comes before squeak-by's top-up, which for both
is Zosa's grind on Dezolis: until 179 the top-up ran first, on Motavia,
reached nothing, and every try was the first one again (below, "The C
player in the window"). Now a retry starts in the Skure field after its
top-up, as playthrough.py's does from its `save-here`.

**The one step outside the game** is the retry's copy: playthrough retries a
stage from exactly the save its checkpoint made, and later saves in slot 1
(the stages save at Kueri, Piata, Zosa and more mid-stage) would otherwise be
what CONTINUE finds. The copy only ever writes the computer's own directory.
`--no-swap` on the driver instead CONTINUEs whatever it last saved in the
game, no file touched.

**The console reset** (`game_debug_reset`): what a new game and a retry need
from a game under way. It is what the game does itself after a game over or
the ending -- straight to the Sega screen, the screen not entered, no fade --
plus the random number generator back to its power-on seed, and the Escape
menu shut. The probe keeps `start new` a no-op (it is always at power-on);
the game's host resets.

## Where it starts

The COMPUTER PLAYS row's value is where it will start; left and right change
it:

* `NEW GAME`: the console reset, then `routes/new_game.route`'s way by the pad --
  from any screen. (build 168e) `play.Session.new_game`: the route's commands in
  its order, its pages answered until the street rather than counted (the
  route counts the cartridge's English: the retranslation's Governor has more
  pages), ROLF on the stock board and a blank name on the Japanese one; in
  ENGLISH 1989 the route tick for tick (probe_check.py `new_game_languages`).
  (build 168f) ROLF in ENGLISH 1989 only: in any other language a blank
  name, so the language's own default stands (his Eusis, the cartridge's
  ユーシス). ROLF is the cartridge's default, and a loaded save makes a
  default name the language's, so the retranslation's hero was ROLF until
  the first CONTINUE and Eusis after it.
  The game still asks for the route file among what COMPUTER PLAYS needs.
* `SLOT 1`-`SLOT 4` (the player's slots that exist): the save is read,
  never written. The driver tells its stage from the save (as built,
  `computer_plays.stage_of_save`): each stage's marks are what its
  checkpoint in the story's own chain (`routes/saves/story_<stage>.json`)
  has and the one before lacks -- story flag bytes set, recruits joined, the
  story's kept items (the Prism, the Nei items, the Nei Sword). Chests and
  levels are left out: they differ from game to game. The save has played
  every stage up to the last whose marks it all has; play goes on at the
  stage after. A save at the end of a stage that marks nothing -- `gear`,
  the grinds, `lab_b1`, `zema`, `gum`, `dam_party`, `shir`, `kain`,
  `boss_party` -- plays that stage again rather than skip one. Checked on two
  chains of checkpoints (the story's and seed 11's, 41 each): never a stage
  skipped, the exact next stage 27-28 times, the rest a replay of stages
  like those. Whether each of those goes through a second time on a save
  already past it is not checked. `--first STAGE` overrides; `--classify`
  prints the pick without a game.

* (build 103a) `LAST RUN`, after the slots, when the computer's last run
  left `saves/computer/last.json`: its last save -- a stage's checkpoint
  (play goes on at the stage after) or a save on the way (the same stage
  again, from there). The directory is not cleared for it.

It cannot pick up at `skure` or `menobe`: no player's save stands where
they start, so it picks up at `roof` or `shir` instead. A stage whose start
assumes a place the save is not in fails its tries and hands the pad back,
as any stage out of tries does.

## The player's saves

COMPUTER PLAYS never writes `saves/slotN.json`. While it plays, the game's
save directory is `saves/computer/slots/`; its checkpoints are
`saves/computer/checkpoints/story_<stage>.json`; its report and story log
`saves/computer/report.txt` and `story.txt` (playthrough's, with the seed);
the driver's own output `saves/computer/driver.log`. A new COMPUTER PLAYS
clears that directory first -- (build 103a) unless it starts from LAST RUN. Handing back puts the save directory back to
`saves/`: the game goes on from where the computer left it, under the pad,
and a save the player makes then goes to their own slots.

`settings.json` gains one key, `"computer_speed"`; nothing else in it is
written by COMPUTER PLAYS.

(build 122) On Windows the game first looks for `python\python.exe` in its
own folder -- the embeddable Python `make dist` puts there -- and only then
along the PATH (autoplay.c, `bundled_python`). The line `computer plays:
<python>` on stderr says which it started.

(build 162) Each Python on the PATH is asked first whether it is 3.8 or
newer (`-c` with a check of `sys.version_info`, once a session), and an
older one is passed over for the next; with none, NO PYTHON3. The driver
runs on 3.8: tools/probe_check.py's `driver_on_python38` parses its modules
as 3.8 and looks for the newer Pythons' methods and keywords. Until 162
the game took the first Python it found, and build 161's driver called
`Path.write_text(..., newline=)`, Python 3.10's: The owner's Windows
desktop has a Python 3.8 on the PATH, so every run there stopped at the
first save it noted -- a stage's checkpoint at a Data Memory, or a save on
the way -- the driver died on a TypeError, and the game handed the pad
back with no word (driver.log had the traceback). `play.write_lf` writes
those files now.

## Speed

`SPEED` on the row below: `NORMAL` or `FAST`, kept in settings.json.
(build 120) Five values now, C stepping through them in this order:
`NORMAL`, `AUTO`, `2X`, `AUTO 2X`, `FAST` (settings.json's `"normal"`,
`"auto"`, `"2x"`, `"auto_2x"`, `"fast"`; an old file's two read as
before).

* NORMAL: one tick a frame, as a player gets: 60 Hz. A whole game is about
  40 million ticks (seed 11: 7,000-10,000 fights, ~900,000 of them the
  seed's pauses), which is about a week of play at 60 Hz: a watching speed.
* FAST: ticks back to back, a frame drawn every 4096 ticks or every 15 ms,
  whichever comes first (`PS2_FAST_TICKS` changes the 4096), and the sound
  muted -- the sequencer still runs, only the output is silent, so the game's
  waits on a sound ending are the same. The probe plays a whole game in
  about 160 s here with the driver as the bottleneck; the window runs close
  to that.

* (build 120) 2X: two ticks a frame, the sound on (the music at twice its
  tempo, its pitch the same).
* (build 162) AUTO and AUTO 2X: FAST ((build 169a) 32 and 64 ticks a
  frame, below), but NORMAL (AUTO 2X: 2X) for the
  story's beats -- from the tick one begins until a second (BEAT_HOLD, 60
  ticks) after it ends, so a scene's last frame stands before the game
  races on. The game itself says what a beat is, from what is on the
  screen (game.c, `game_story_beat`; nothing in the game reads it):
  - the ending screen;
  - a boss: a fight with The Place of Death's music, formation $100 and up
    (Neifirst, the Army Eyes, Dark Force, Mother Brain);
  - a scene on the map: a level event of the story's (Darum, the pocket's
    Letter, the Recorder, Teim, the dam locks, Neifirst and Nei's death,
    Climatrol, Gaira, the Esper mansion, Pandora's box, Mother Brain,
    Lutz, the Earthmen), the Teim and Darum scene, a camera case (the
    shakes, the reloads, Gaira's countdown, the dams' check; not the
    gum's way out), the pictures of Nei's death, a demo walk, or the party
    gone or held;
  - a story building, the whole visit: Rolf's house at the start, the
    Governor, Gaira's control panel, Tyler's ship;
  - the story's words: one of the plot's scripts in the text window until
    its end is answered -- the first time it shows since the console's
    last reset (every start and retry of COMPUTER PLAYS resets it) -- and
    on the intro screen between the dream's pages too (`story_script`:
    the Governor's, the opening's, the dream's, the level events',
    Miscellaneous -- Tyler, Lutz, Pandora's box -- and the story's lines in
    the mixed banks: Nei at the start, the knock at the door, the
    recruits' words and naming, the inventor's gum, Central Tower's flood,
    the library's Recorder report, the dark power, and the Prism, the dam
    locks, the note and the Recorder among the items).

  - (build 162a) gear bought: a visit to a weapon or armour store where
    the computer buys, from inside the door to the street (the walk to
    the door stays FAST). The game cannot tell a purchase at the door --
    the shir stage walks in and out of stores with nothing to buy, for
    her thefts -- so the driver says: `@gear on` and `@gear off` around
    gear_up's purchases (`play.Session.buying_gear`), host lines as the
    grinds' (autoplay.c `host_line`, `autoplay_buying_gear`). The owner:
    buying gear can be watched; "it's sort of tedius watching the game
    sell a bunch of loot". The item store -- selling what the new pieces
    replaced, the Monomates, Trimates and Escapipes -- stays FAST.

  Everything else runs FAST ((build 169a) at the grind's 32 or 64 ticks
  a frame): walking, grinding, the ordinary fights,
  selling, the item store, the hospital, Data Memory, the clone labs, the
  roster, chests, lockers, lessons, townspeople, Shir's thefts and her
  coming back, the roof's spaceship (travel: a seed flies a dozen times), and words
  already shown -- Lutz sends the party to Noah from the Esper mansion on
  every trip of the Noah grind, about twenty times a game, with the same
  two scripts. A retry plays its scenes and bosses at NORMAL again. (A
  first cut held the roof, the Esper mansion's every visit and Shir's
  return as beats: 105 beats in seed 12's game, 55 of them those -- 34
  visits to the mansion, 7 to the roof, Shir back 14 times.) Seed 12's
  whole game at AUTO now: 55 beats, 153,348 of its 11,875,022 ticks at
  NORMAL (1.3%, 43 minutes at 60 Hz) -- 65,344 of them the five tries at
  Dark Force, 39,501 Mother Brain's confrontation through the ending --
  and the same story log and checkpoints as FAST. (build 162a) With the
  gear bought: 97 beats, 42 of them purchases (24,630 ticks, about 7
  minutes), 177,978 ticks at NORMAL, 49 minutes; the same game.
  The game's stderr has a
  line as each beat begins, naming it (`computer plays: story beat at
  tick N: the story's words ($1801 I've seen that face! ...)`), and one as
  the game goes fast again. When FAST ends the chips are put back in step
  with the music (docs/audio.md, "Muted, then back"). (build 169) Until
  169 that could strike notes the last song had ended: The owner, "in
  the home a pitch rings when it exits the fast play". At the knock in
  Rolf's house the street's FM4 note rang under the house's music for
  five seconds. Fixed in audio.c's records (route `audio_replay_silenced`).

  (build 169) `PS2_TRACE=1` prints the computer's frames too, a line each:
  `computer rate R  ticks T  (tick N)`, with `<-- short` on a frame at
  NORMAL or 2X that ran fewer ticks than its rate. On such a frame the
  game and its music stood still, because the driver was still thinking
  when TICK_WAIT (12 ms) ran out. `PS2_PACE=1` counts them every five
  seconds: `pace: computer plays, S of N frames at its speed short of
  their ticks`. On the owner's desktop, headless, seed 1 in RETRANSLATED
  at AUTO to Arima: 0 of 8,690 frames at NORMAL short, idle or under a
  33-game grind. So the driver is not behind the music there; another
  machine says for itself.

  (build 169a) Between the beats AUTO plays 32 ticks a frame (main.c,
  `AUTO_GRIND`) and AUTO 2X 64, every frame drawn and the sound muted;
  FAST is as it was. The owner: "I think the fast part on auto might be
  too fast ... I don't get to see a blur of grind fighting it's almost
  like black outs between weapons purchases and story points." FAST draws
  a frame when 15 ms of ticks have run, on the owner's desktop
  (headless) about 1,650 ticks, 27 s of the game; a fight in Arima's grind
  is about 1,000 ticks on the battle screen (the median of 199). So no
  fight lasted a frame, and each drawn frame was a sample. Of 169's 227
  FAST frames on seed 1 to Arima, 98 were a fight (whose screen is
  four-fifths black), 84 the map, 21 a fade-in, 20 a building's portrait
  on black: a dark strobe about four seconds long between the beats. At 32
  a fight is about 31 frames, half a second; the speeds offered were 16,
  32, 64 and 128, and the owner chose 32, and twice it for AUTO 2X. A
  grind frame ends at a beat's first tick, as FAST's did. Seed 1 to Arima:
  AUTO 13,029 frames at 32 (95 short of it, the driver thinking or a beat
  beginning) and 8,526 at NORMAL, six minutes at 60 Hz (169: 9,419
  frames); AUTO 2X 6,560 and 4,265; FAST 350, its code unchanged. The same
  story log, checkpoints and slots at all three, and as 169's AUTO. A
  whole game -- seed 12's 11.7 million ticks between the beats -- is about
  1.7 hours of grind at AUTO and 50 minutes at AUTO 2X, besides the beats
  (49 minutes at AUTO). PS2_TRACE's `<-- short` and PS2_PACE's count take
  only the frames with the sound on.

  (build 120-161) AUTO was the other way round: NORMAL, and FAST only
  while the driver ground -- its host lines `@grind on` and `@grind off`
  (`play.Session.in_grind`, the `play.grinding` decorator), which the
  game's command thread keeps from the interpreter (autoplay.c,
  `host_line`). So everything between the grinds went at 60 Hz: the
  shopping in Paseo between the gear stage's rounds in Arima's field, the
  walks between towns, the chests. The owner (build 162): AUTO is
  "only supposed to drop out of fast mode when it's attempting a story
  beat". The driver still sends the two lines; the game takes them and
  reads nothing from them.

Speed changes nothing the game decides: the driver's commands are counted
in ticks, not frames, so every SPEED plays the same game. (build 120) Seed
1 to Arima at AUTO, AUTO 2X and FAST: the same story log and checkpoints
as 119's, 422,663 ticks in 18,437, 9,617 and 1,110 frames; to Paseo, 2X
7,839 frames against NORMAL's 14,995 for 14,392 ticks.

## Pace (build 101)

The driver plays at the harness's `watch` pace (`play.PACES`,
`computer_plays.py --pace`): in a fight it waits with `wait pad 30` -- the
moment a menu or a page waits on the pad, or 30 ticks -- rather than the
probe's `wait ticks 240`, never presses through a page still typing, and
takes a player's pauses in a fight half as long. The victory's pages are
stops that wait for a button (docs/history.md, Build 101), so each is
answered as soon as it has typed: 177 ticks a won fight against 1,651 at the
probe's pace (seed 12 to Arima). (build 102) Then the owner wanted them up longer: at the watch
pace each victory page's stop is held 30 ticks before its C
(`victory_hold`), 362 ticks a won fight against 175 (seed 12 to Arima on
101b's stages). (build 102b) That was too long: 15 ticks a stop, give or
take up to 5 from the seed's pauses (`victory_slack`), 269 ticks a win on
102a's stages. The window's game and the probe's part at the first
wipe-out: the window CONTINUEs through the title by the pad, the probe
loads the save. A pace is part of the game a seed plays --
`playthrough.py --pace watch` is the window's game on the probe, and the
probe's own runs, routes and trials keep `probe`.

## Handing back

(as built in 100c; the plan was "Escape or any button", which 100b built)
COMPUTER PLAYS's row has a lamp. C lights it or puts it out; the menu stays
up. Closing the menu with the lamp lit starts the computer. While it plays,
Escape opens the menu over its game -- held where its last tick left it, no
tick served, the driver simply waiting -- and closing the menu with the lamp
put out hands the pad back: the driver told and given a second for its
report, the command thread released without another tick, the save
directory `saves/` again. Closing it with the lamp still lit plays on.
Nothing else the player presses reaches the game while the computer has it,
and no other key hands back. The lamp goes out by itself when the driver
ends (the ending, or a stage out of tries). SPEED can be changed in the menu
while the computer plays; it takes effect when the menu closes.

(build 133-133a, read) While the menu is up no tick runs, and the sequencer
runs only in a tick (`audio_music_tick`), so the music stands where the last
tick left it. The chips, rendered on the audio thread, hold what was keyed
on -- FM notes in their sustain until the chip's own decay takes them, PSG
tones at their last volume -- until the menu closes and the song carries on
from that tick. The menu's own clicks, all the selection sound, go to the
sequencer's queue, a single slot each one overwrites, so none sounds while
the menu is up and one plays on the first tick after it closes; a sound the
computer's last tick queued and the driver has not read yet is replaced by
the menu's first click. A player's own Escape menu is held inside the tick,
which runs the music first, so neither happens there. The ring is kept
(The owner likes it); the clicks are his call.

## Tested (as built)

The frame loop waits through the driver's thinking as well (a frame is for
ticks; the next command usually comes within a millisecond), or fast speed
ran about one command a frame.

* Headless, through the window's own path: `PS2_PAD` opened the Escape menu
  at frame 400 and picked COMPUTER PLAYS (FAST from settings.json);
  `PS2_COMPUTER_ARGS="--to arima --seed 12"`, `PS2_COMPUTER_QUIT=1`. Paseo
  and Arima in 8 s: 852,392 ticks in 888 frames. Its story log (75 lines)
  and both checkpoints are byte-identical to `playthrough.py --seed 12 --to
  arima` on the probe: the reset is a power-on, and the handoff moves no
  tick.
* Six hand-backs (a button at frames 637-822, seeds 1-6): each run ends
  "handed back", the driver's report written, the game on under the pad.
  The first try hung one run: a hand-back just after a tick was served left
  the thread to wake, go on and ask for a tick nobody would serve. It checks
  the hand-back before and after every tick now.
* Routes `escape_menu_computer` (PNGs of COMPUTER PLAYS and SPEED) and the
  probe proved unchanged: the 79 PASS lines, 440 route files, the Dark
  Force trial, and seed 11 played to the ending on this build's harness --
  its story (2,722 lines) and all 28 PNGs identical to 99b's packed run.

## Tested (build 101)

Retries, a player's slot, `roof` before a failed Skure, `shir` before
`menobe`, `--no-swap`: all through, in the window's own path
(docs/history.md, Build 101, task 3; `--fail-once STAGE` forces a retry).
Not yet run: a player's save at the end of `zema`, `gum` or `kain` played
again.

## Tested (the plan)

Headless, through the window's own path (main.c, the Escape menu, the
reset, the thread, the child): `PS2_PAD` opens the menu and picks COMPUTER
PLAYS, and the driver plays a seed from a new game to at least Arima. A
route shows the Escape menu's new items. The probe is proved unchanged.

## Saves on the way (build 103a)

On a trip back to a town with a Data Memory, in one of the grinds (arima,
oputa, amy, lab_in, zema, climatrol, dezolis_grind, kain_grind), the
computer saves when since its last save a member's level rose, a member
joined or a story point passed (`play.trip_save`; on Dezolis only for a
live Rolf in the Magic Cap or the Mogic Cap, as the Data Memory itself
checks). The save goes to its own slots as any save does, and a copy to
`saves/computer/checkpoints/mid_<stage>.json`. A stage that does not finish
is tried again from there, not from its start; `last.json` names the latest
save for LAST RUN. The probe's playthrough.py makes the same saves, so the
window's game and the probe's stay one game. Each stage resumes from its own
town: arima and amy skip Rolf's house when the member is in, lab_in a blown
door, climatrol the chests its note says are looted (docs/history.md, Build
103a, the tests through the window's path).

## A wipe-out the waits missed (build 108)

The owner saw a run sit at the title after a wipe-out; the report he uploaded
with build 107 was another run's (The owner, build 108), so that run's own is
gone. Read for it: the driver's loop (computer_plays.py) retries a stage
that returns false from its checkpoint, and most of story.py's walks and
grinds look for the game over, SEGA and title screens after a fight and
CONTINUE (`story.recover`). Two of play.py's waits did not:
`Session.ready()` -- "until the party is on the map under the pad" -- gave a
title it could not get past to `press_through()` up to 40 times, and
`press_through()` answered it with C up to 200 times; the title waits on
Start alone, so a grind (`Session.grind`, `wander` and everything that
calls `ready()` first) could spend up to 8000 unanswered presses there
before it gave up. Both now return at once on those screens (`play.WIPED`),
and the stage's own wipe-out handling, or the driver's retry, takes it from
there. tools/probe_check.py's `ready_at_the_wipe_screens` puts a probe
session on the SEGA screen and the title: `ready()` gives up within its
first command now, and was still pressing after five with 107's play.py.

What the computer does when a stage is out of tries is unchanged; build
107's question to the owner stands: should it CONTINUE the last save and hand
the pad back in the game?


## On Windows (build 115)

COMPUTER PLAYS runs on Windows now, in the same `autoplay.c`: only finding,
starting and stopping the driver differ, one section an OS. The command
thread, the handoff and the run's directory are one code.

* **The driver's process.** CreateProcess in place of posix_spawn. A Windows
  child has no file descriptors past 2, so the two pipes' ends the driver
  gets are inheritable handles named on its command line (`--attach
  CMD,LOG`), which `computer_plays.py` opens as descriptors with
  `msvcrt.open_osfhandle`; play.py is unchanged. Only those two handles and
  its standard ones go down to it (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), so no
  other child can hold a pipe open; its stdout and stderr go to
  `driver.log`, its stdin is NUL, and it has no window of its own. The pipes
  are 64 KB (Linux's; Windows's default is 4 KB). Its environment is the
  game's with `PYTHONHASHSEED=0` in its sorted place, as on POSIX.
  Handing back: its log pipe closed, a second for its report
  (WaitForSingleObject), then TerminateProcess.
* **Which Python.** `python3.exe`, then `python.exe`, then `py.exe` (run
  with `-3`), each looked for along the whole `;` PATH (quoted entries and
  trailing slashes allowed). A file there that is a reparse point counts
  only if `-c ""` exits 0 within 5 s, asked once a session: the Store's
  stand-ins in WindowsApps are app execution aliases that, with no Python
  installed, only say where to get one. None found: NO PYTHON3, as on
  POSIX, and stderr says `no python3.exe, python.exe or py.exe on the PATH`.
* **The thread** is winpthreads', but its **lock and wake-ups are
  Windows's own** (an SRW lock and a condition variable). The first build
  used winpthreads' condition variable, and FAST ran at a seventh of the
  probe's speed: a winpthreads broadcast first waits until the thread it
  woke last has run (the condition variable's gate), so each tick handed
  between the frame loop and the command thread cost about 130 us in this
  VM. A two-thread ping-pong: winpthreads 123-138 us a round trip, Windows's
  1.5-2 us. And a thread about to sleep spins for up to 200 us first,
  watching an interlocked count of wake-ups without the lock (so neither
  thread can keep the other off it): a thread that has slept costs about
  100 us to wake here, which every command's first tick paid. POSIX keeps
  pthreads as it was.

The game gains a DLL: ps2.exe and ps2_headless.exe import
libwinpthread-1.dll, which sits beside SDL3.dll in MSYS2's ucrt64/bin.

Measured headless, seed 12 from a new game through Arima (596,726 ticks) at
FAST: 98 s with winpthreads' lock, 37.9 s with Windows's, 12.2 s with the
spin as well -- `playthrough.py --pace watch` on the probe takes 13.9 s --
and all three the same game, byte for byte. A command costs 110-130 us (the
probe's 80-220), a tick 1-2.4 us in a long wait.

**Tested headless through the window's path** (docs/history.md, Build 115):
seeds 1 and 2 to Arima the probe's game exactly (story logs, checkpoints);
seed 12 the probe's up to its first wipe-out (tick 16,085, as on Linux in
102b), as designed; six hand-backs; the menu opened and shut mid-run (the
game byte for byte the one without it); QUIT mid-run; SLOT 1 (the slot
untouched); LAST RUN; a retry's CONTINUE through the title; NORMAL (the
same game as FAST); no Python on the PATH; no tools/; a driver that would
not leave (terminated after the second). Not run: `py.exe` (none here), a
reparse-point Python (no symbolic links without administrator rights here;
its check was run on its own), and the SDL3 window, which needs the owner.

## Squeak-by (build 118)

The driver plays squeak-by by default, as playthrough.py does
(docs/harness.md, "Squeak-by"): every grind's level target a level or two
lower, the gear bought as ever, and a stage that fails retried from its
last save after a top-up -- three trips of the latest grind for each try
already failed -- up to six tries. The owner's words: live on the razor's edge; that is what saves are for.
Seeds 1-12 at the watch pace (the window's game on the probe): 12.82 M
ticks a whole game against 15.26 M, about 59 hours of play at 60 Hz against
71, every seed to the ending. `PS2_COMPUTER_ARGS=--no-squeak` plays the story's own targets.

## The C player in the window (build 179)

```
 window (main.c)
 +--------------------------------------------------------------+
 | main thread: frames, the pad, game_tick on request, render   |
 |                                                              |
 | command thread: the C player (src/player/computer.c, the     |
 | Python's computer_plays.py) -- each command it sends is run  |
 | there and then with the probe's interpreter, one tick at a   |
 | time through the main thread; its log lines go straight back |
 | to the player's session                                      |
 +--------------------------------------------------------------+
```

The command thread is the player's thread now. `autoplay_start` parses
COMPUTER PLAYS's arguments (`--start` the menu's choice, `--out
saves/computer`, then `PS2_COMPUTER_ARGS`) before it clears the last run, so
arguments that do not parse leave it as it was and the lamp goes out with
the usage on stderr. It then makes a session named "computer" whose sends
call `player_line`: the lock taken, the line run with `probe_run_text`, the
state back to thinking (PIPE) so the frame loop may draw while the player
decides its next command. The player's thread has a 16 MB stack of its own
(PLAYER_STACK), so its depth does not rest on each platform's default for
a thread (512 KB on macOS; `route_probe` runs the player on its main
thread). The
story's lines go to `saves/computer/driver.log`, as the Python's stdout
did; `PS2_COMPUTER_LOG=FILE` writes every log line the interpreter gives,
which is what `tools/computer_check.py` compares with `route_probe
--computer`'s log.txt.

**Handing back.** The menu closed with the lamp out sets the abort as
before; the player's thread, at its next command or tick, gives up the lock
and jumps to its run's end (`player_hand_back`): the report is written
with "finished: handed back" (the Python's ended "handed back (EOFError)"),
last.json names the last save, and the thread ends. The report's ticks are
the player's: a command cut off mid-way had ticks only the window counted
(its "after N ticks" line). The lamp goes out by itself when the run ends,
as before. QUIT mid-run ends it the same way, then the game.

**What runs where.** `route_probe --computer [--language KEY] ARGS` (build
179) is the same run on the window's host -- `player_only`, so `start new`
is the console's reset and `start slot` the file copied into the computer's
slot and CONTINUEd through the title by the pad -- with the probe's files
in `OUT/probe` and the language its settings.json's, as the window's is the
player's setting. `--classify` there prints the stage a save would begin at
and touches nothing, as the Python's does.

**Tested** (docs/history.md, Build 179): seeds 1-12 in us, retranslation
and ja at FAST through the headless window's own path (the Escape menu,
NEW GAME), each with the C player and with the Python driver, against
`route_probe --computer`: all 72 window runs the tool's game, every command
at the same tick, story.txt, report.txt less its seconds, last.json, every
checkpoint and slot byte for byte (the Python's paths less the backslashes
Windows's Python writes; "shot" lines read as the probe's, since the window
draws its own frames). AUTO and AUTO 2X the same on six of those games.
LAST RUN, SLOT 1 (Paseo's save and Gaira's with a member dead; the slot
untouched), `--fail-once` (the retry's CONTINUE through the title) with and
without `--no-swap`, the menu opened mid-run at FAST and at AUTO (the same
game), a hand-back and QUIT mid-run (exit 0, the report "handed back"),
NO TOOLS (story_paseo.json taken away), arguments that do not parse (the
last run kept). `--classify` the Python's on 173 saves (routes/saves/ and
three games' checkpoints). The packed game (`make dist_headless`) in an
empty folder plays seed 1 to Arima as the tree's game does, byte for byte.

**Found, not changed:** 3 of the 36 games (seeds 7 and 10 in us, 11 in ja)
do not finish: Skure's first try wipes out on SkureB2 after "no path on
SkureB2 from (79, 52)", the CONTINUE finds the party in Paseo, and every
retry (roof again from dams' checkpoint, since the Skure field gives none)
plays the same try again -- squeak-by's top-up gains no level -- six times.
The Python driver plays exactly the same games. Build 118 had every seed
reach the ending, so a later change to the Python harness brought it
in; playthrough.py (which saves in the Skure field) finishes all 36.
(build 180) Fixed: the top-up was the trouble -- Zosa's grind, run from
Paseo before the flight, reached nothing. A retry now flies the roof
first and then tops up (above, the audit), and all 36 games reach the
ending (docs/history.md, Build 180).
