# Encounters and the battle screen

Moved verbatim from the README in build 81c.

## Random encounters and the battle screen

`ProcessRandomBattle` (ps2.asm:26535) runs every tick of
`LevelScreenLoop`. It bails while a window is up, a talk runs or a demo
walk is playing, and otherwise waits for `$FFFFCB0A` -- set by
`ObjCL_Main` at loc_38FC, the start of every step -- and for the
character to be back on a cell: `(pos + 8) & $F == 0` on foot, `pos &
$F == 0` on the jet scooter, since `ObjCL_Init` sits the object eight
in from the cell corner. So it is one roll per completed step, and the
port keeps the flag in `Walk.stepped`.

The enemy **group** comes from where you are:

- Motavia (level 0): one byte per 256x256px region, twelve columns by
  eight rows -- `loc_23C3A`; `loc_23C9A` on the scooter; `loc_23CFA`
  after Climatrol (`$FFFFC737`); and the two offsets *add*, so both is
  `loc_23D5A`. Counter +1, +1 more on foot, +2 more without Hidapipe
  (`$FFFFCB0E`).
- Skure (level 3): eight by eight, `loc_23DBA`. Counter +2, +2 more
  without Hidapipe.
- Everywhere else: the flag byte on the level's plane-B pointer in
  `LevelData`, or the plane-A pointer's after Climatrol -- now the
  `enemy_group` / `enemy_group_late` properties in the level `.tmx` and
  `.json`. Zero is a town: no counter, no roll. Counter +2.

Then `counter &= $FF`, and `random & $FF < counter >> 3` is a battle:
the formation is `EnemyFormationTable[group * 8 + (random high word &
7)]` -- the high word, which is the seed *before* the step, hence
`rng_next32` -- and the counter resets. Two things easy to miss:
`LevelScreen` zeroes the counter on every entry (a level change, leaving
a building, coming back from a battle), and a formation id is 1-based
(`loc_796` does `subq.w #1` before indexing `EnemyBattleFormationData`);
the four bosses sit at `$100`-`$103`.

`PS2_ENCLOG=1` prints each hit -- tick, level, cell, formation. From a
new game standing in the field east of Paseo the first battle came 22
steps in, which is about what the counter predicts.

`BattleScreen` (12047) then:

1. Snaps `level_x/y_pos` to the cell and **flashes to white**, not
   black: `loc_634E` steps every displayed entry up to `$EEE` one
   channel a frame, red until it reads `$E`, then green, then blue,
   over 21 frames. `$FFFFFB00` is the *displayed* palette and
   `$FFFFFB80` the loaded one (`PaletteLoad1` adds `$80`, `VBlankSub_1C`
   DMAs `$FB00`). The port's `FADE_OUT_WHITE` is that; battle → level is
   the ordinary black fade, `PaletteFadeFrom` then `PaletteFadeTo`.
2. With the display off: VRAM `$0000-$3FFF` zeroed, `FontsIconsArt` at
   `$A000` (tile `$500`, same as the map), the floor art `loc_7567A` at
   `$F020` (tile `$781`) and its 40x16 nametable `loc_75BD8` at
   `$66000003` = plane B row 12, `PalettePtrs $2F`, the object RAM
   cleared, and the 1-in-8 ambush roll (`random & 7 == 0`, never
   against Mother Brain).
3. `loc_796`: `EnemyBattleFormationData[formation - 1]` is id, count,
   id, count, x-layout; the x positions are `EnemyXPosArray[layout]`
   and every enemy stands at y `$F8`. Each group's `EnemyData` record
   (64 bytes: 15 CRAM words, the art pointer, EXP, meseta, attack,
   defense, type, agility, HP, technique, technique rate, success-rate
   index, escape rate, object id, animation duration, four animation
   bytes, two SFX ids, the map pointer, the animation-script index)
   supplies the palette -- fifteen colours into entries 1-15 of loaded
   line 2 or 3 -- the art, decompressed into a RAM image at tile 1 or
   `$101`, and the stats. The escape rate starts at `$100` and each
   group lowers it to its own (`loc_8D4`): the lower of the two is the
   battle's. (Builds up to 41 took the first group's.) The party's battle art goes to tile `$200 + $80 n` with
   `Battle_CharCoordinates`, line 1 for Nei, Hugh and Shir.
4. Every character's stored command is checked (`loc_F4E8`): a
   technique becomes attack, an item stays only while it is still held,
   and nothing in either hand means the shield icon (`loc_F584`).
5. The four stat boxes open with `$0600` and FIGHT/STGY with `$0700`,
   drained before the display comes on. **Bit 2 of the word (`$0400`)
   means "draw it and don't push it"**: `loc_94B0` skips the count, so
   these, both option boxes and the enemy boxes are pixels on the
   plane, never on the stack, never closed by `CloseAllWindows`. The
   port keeps them as `WinSys.painted`, one per id, with one shared
   cursor -- the ROM spawns each cursor at object slot
   `current_active_objects_num`, which is 0, so a new one replaces the
   old. The plane is one surface, so whatever was painted last shows:
   a repainted id moves to the end of the draw list (41b). Build 41
   put it back in its old slot, so FIGHT/STGY reopened by B on ORDR/RUN
   was drawn *under* ORDR/RUN's leftover pixels -- B looked dead, the
   lamp seemed to jump back to ORDR, and the next C picked STGY on the
   hidden box instead of RUN. (build 142) And a painted window is drawn
   over the stack windows open when it was painted and under any opened
   since (`WinEntry.over`), as the nametable has them; until 142 every
   painted entry was drawn under the whole stack -- no battle box meets a
   window it was painted over, so no fight changed, but the Central
   Tower's copy portrait stayed hidden (docs/history.md, Build 142).
   `$0700`'s extra bit is `WIN_F_PROCESSED`, so that first box is
   drawn without a cursor; the standby routine reopens it with `$0600`
   when the loop starts and gets the lamp.
6. The display comes on white and `loc_62CC` fades it down over 21
   frames -- blue first, then green, then red, one channel a frame --
   then `FadeArtTiles` dissolves tiles `0-$1FF` in over the 64 positions
   of `FadeEffectArray`, one a frame, with `RunObjects` running so the
   enemies already animate.
   The battle music is queued at this point, not after the dissolve:
   `UpdateSoundQueue` with `$92` (`$93` for a boss) sits at 12179,
   before the display comes back on, so "Rise or Fall" plays through
   the fade from white. `UpdateSoundQueue` itself skips the queue when
   `$FFFFF640`, the id last queued, already matches.
7. The enemy name and info boxes open with `$0400` -- for the first
   group at `$6082`/`$609A`, the second at `$60AA`/`$60C2`; **bit 13 of
   the position word is plane B**, `$2000` past plane A, where they go
   because the enemies rebuild plane A rows 0-16 every frame. Not for
   Dark Force or Mother Brain. Then `$92`, `$93` for a boss. (build 100b)
   Nothing of these boxes has priority: loc_978C writes a plane B frame
   with `$500` (a plane A one with `$8500`), and their routines draw the
   interior with `$500` too, so an enemy reaching up -- a Locusta's arm --
   is drawn over them (route `locusta_arm_over_box`).

Enemies are not sprites, mostly. `BuildSprites` reads each object's map
frame: a count byte under `$80` is sprite pieces (loc_526, five bytes
each: y, size, tile, x); zero or negative is a nametable block (loc_572:
x word, y word, w-1, h-1, then w*h words) written into the `$FF6000`
buffer that `VBlankSub_18` DMAs to plane A rows 0-14 (15-16 while
fighting), with zero cells skipped and the object's tile word (`$4000`
for group 1, `$6100` for group 2 -- line 2 or 3, tile 0 or `$100`) added
to the rest. A map set mixes both: the ants' idle frames are blocks,
their attack frames pieces. `extract_battle.py` keeps them apart
(`cells` or `pieces`) and `battle_render` draws either.

The idle animation is `Battle_AnimateSprites` (4727) over `loc_2601A`:
a table of word offsets by the record's last-but-one byte, each script
a run of frame-plus-one bytes (zero hides the enemy) to `$FF`, which
loops from the first; `$FE`/`$FD` are the action scripts' marks. One
step every `anim_duration + 1` frames. `ObjEnmBattle_Main`'s standby
branch runs it only for a type byte with bit 3 set or its low three
bits clear.

`BattleScreen_CheckRoutine` (21714) waits for the windows to go idle
and `$FFFFCC06` (actions still animating) to be zero, then runs
`battle_main_routine_index`'s routine: 0 standby, 1 fighting, 2 run.
Standby (21743): state 1 opens FIGHT/STGY with `$0600`; state 2 reads
`$FFFFDEDA` -- FIGHT starts the fight, STGY opens ORDR/RUN; state 3
reads `$FFFFDEDC` -- RUN hands over to the run routine, ORDR spawns the
triangle cursor for the per-character commands. B on ORDR/RUN steps the
routine back two, which is "reopen FIGHT/STGY". The run routine
(22580): `WinID_BattleMessage` with script `$1202` ("{char} and
friends{pause}run away!" -- `{pause}` is `$C3`, a genuine wait for a
button, per loc_FB56), then `random & $FF <= escape rate` succeeds:
`ScreenID_Level` and `RestoreCharDataAfterBattle`, which keeps HP, TP,
experience and the equipment and puts agility, luck, dexterity and the
three attack/defense words back from the copy taken at battle start.
Failure is `$1203` ("But they're trapped!") and one free round for the
enemies. The disassembly notes the roll's oversight: a 0 escapes a boss
too.

### The fight (build 42)

The fight is the ROM's object system, ported as it runs rather than
redrawn as a turn-based state machine, because the timing of a round is
in the objects' animation scripts: a hit lands, its sound plays and the
boxes redraw on a mark in whichever script is running.

**Objects.** 64 slots of 64 bytes from `$FFFFE000`. A character is 128
bytes at `$E400 + $80 * id`, an enemy 128 at `$E800 + $80 * slot`, and
the second 64 bytes of each is a *child*: the thrown weapon or spell in
flight for a character, an overlay for an enemy. The child's id is the
negated mark byte that launched it -- `$F5`/`$F4`/`$F3` are the effect
objects `$B`/`$C`/`$D`, `$F0`/`$EF` an enemy's `$10`/`$11`. Spawned hit
effects go to the sixteen slots at `$EC00` (`$B` for a character's
action, `$12`/`$13` for an enemy's). `BObj` in `battle.h` names every
field by its offset; `obj[i - 1]` is the ROM's `-$40(a0)`.

**Marks** (`Battle_AnimateSprites`, 4727): `$FF` ends the script (an
acting object clears bit 0 of 3(a0) and decrements `$FFFFCC06`), `$FE`
destroys the object, `$FD` hits the target (`loc_3308`: SXFID_Sword, a
`$30`-frame blink, the TECH/ITEM-used box, the stat and enemy boxes,
then the queued battle message), `$FC`/`$FB`/`$FA`/`$F9` spawn hit
effects on the target, on everyone shown, on enemies still owed shots
(`$32`), or with a column-dependent script from `loc_34EE`; `$F8` hits
everyone; `$F7` and `$F6` spawn an overlay; anything else launches the
child.

**Turn** (`loc_F004`): living members' agility plus 0-15, entered twice
for the attack command; living enemies likewise; fourteen passes of a
swap sort, highest first. An ambush skips the party. The event routine
then marks each entry acting in turn (`loc_F188`) and waits for
`$FFFFCC06` to drain, checking between actions whether the party still
stands and whether any enemy object is left.

**Characters** (`Obj_CharactersBattle`, 2548): hidden during the fight
unless shown by an action (bit 7 of 3(a0)); a paralysed character loses
a turn, a sleeping one does nothing, Dark Force's side effects (`$1224`
on) and the Nei Sword's cure run from here. Attack is
`CalculateHitRate` (dexterity * 256 / agility against `$CC`/`$E5`/`$FF`,
halved under bit 11) and `CalculateAttackDamage` (`(rand&$1F + $54 +
attack) * 256 / (defense * 5 + 100) * (weapon attack + 2) / 256`); guns
are `loc_284C` at the weapon's own rate, slashers hit the whole group,
multi-shot guns spread over it. Defend sets bit 8. All 52 technique
actions and `ItemUsedIndex` are ported: damage techniques through
`CalculateTechniqueDamage`, bosses immune to the rate-`$FF` ones and a
technique failing on types it doesn't touch (`$1204`, "Didn't work!").
A command that cannot run (no TP, no one to heal) falls back through
`loc_F4F8` and runs again.

**Enemies** (`Obj_EnemiesBattle`, 3879): `Enemy_PickCharToAttack` from
`Battle_CharTargetChances`, `loc_2AA8`'s damage (halved by Deban or
defending, lost half the time under bit 12), or a technique at the
record's rate -- a negative byte is damage at that rate (odd: everyone),
2-19 the table at 4181: paralysis, poison, sleep, drain, Neifirst's
ninth-turn exit to Gaira, Dark Force's side effects, healing an ally,
TP/attack/defense loss, instant death. The success roll is
`EnemyTechSuccessRate[index + luck/50]`.

**After** (`loc_F1F6`-`loc_F428`): everyone down is `$121C` then `$1208`
and `GameOverScreen`; victory waits 30 frames, plays MusicID_Movement,
shows `$1211`/`$1212` with the experience split among the living
(`exp_points_buffer` is `meseta_value`, which is why `{meseta}` prints
it), `$1213` with the meseta, restores the stat copy, and walks the eight
characters through `loc_F328` with the equipment bonuses off around each
check -- `GenerateRandStatIncrease` is `gain * (rand&$FF + $80) / 256`
per stat, `$1214`-`$1217` as they apply. `Character_ProcessLevelUp` is
reached by a branch, not a call, so its `rts` leaves `loc_F328` with
`$FFFFC602` where it was: once the message has been read the same
character is checked again, and a big win gives several levels one after
another. Only a check that finds nothing (`loc_F372`) moves on to the
next character. Before build 55 the port moved on regardless, so a
second level waited for the next fight. A failed RUN is one round with
the party skipped and the fight interrupted after it, so the commands
come back. Any button during a round sets
`fight_interrupted_flag` and the round's end returns to FIGHT/STGY.

**ORDR** (21800-22170): the party triangle (`Obj_TriangleCursCharSelection`)
at slot count + 1, the name box then the commands window with its own
triangle (`Obj_TriangleCursCommSelect`, four stops `$18` apart); attack
asks for a group only when there are two, a technique or item opens its
four-a-page list (`NEXT` at five or more), then the group box or the
party triangle depending on the technique's flags. The record is three
words a character: command, technique or item, target.

**Command defaults** (build 55). The commands (`char_battle_command_index`,
`$FFFFCC10`, 16 bytes a character) are not in the save. `loc_F550` sets
them from `CharStartCommandsArray` (22742) -- Attack for Rolf, Nei, Rudo,
Anna and Kain, **Defend for Amy, Hugh and Shir** -- and `loc_F584` then
puts anyone with nothing in either hand on Defend; holding a weapon never
moves anyone to Attack. So an armed Amy opening her first fight after
CONTINUE on DEF is the ROM, not a bug (Nei doing it was: her entry is
Attack). `loc_F550` runs at the new game (`loc_AF8E`, 16164; the port
does it in `enter_intro`), on CONTINUE (Data Memory's welcome with
`$FFFFF764` set, 16596; the port does it leaving the select screen), and
on REORGANIZE (`loc_B1A0`, 16334; `BuildingState.reset_commands`, which
`tick_building` acts on -- new in build 55). Between those a command
sticks from fight to fight; `loc_F4E8`, at the start of each, turns a
technique into Attack, and an item no longer in the bag, or one with bit
6 of its kind byte, into Attack, and puts anyone unarmed on Defend.

**Effects' art**: `WeaponTechArtPtrs` names up to two pieces a weapon
or technique, loaded to `$8000`/`$9000` (tiles `$400`/`$480`) when the
action starts; 29 of the 30 are inline in `ps2.asm` and come out with
`extract_inline_art.py`, the Knife's is `art/knife_art.bin`.

**Not faithful, on purpose**: Shinb takes the disassembly's fix (the
ROM branches to `loc_27AA` and runs into data, which is why it never
works on a real cartridge -- say if you want the bug). The clone
technique (enemy technique 12, `loc_2F6C`) is an attack for now. Losing
to Neifirst regroups the party from `$C618`, which the save block does
not carry yet.

`PS2_FTRACE=1` prints the fighting routine every tick: event routine,
`$FFFFCC06`, the turn position, HP, and every live object's
state/flags/x/script. `PS2_BTRACE=1` is still the standby trace.
`PS2_BATTLE=<formation>` starts a new game straight in a formation.

**The effect scripts' index (build 88).** `loc_27364` has 72 entries and its
`$41` points into a script: `(loc_274C6+6)-loc_27364`. `extract_battle.py`
keys such an entry `loc_274C6+6`, the script from six bytes in. Until
build 88 it was dropped and the table was one short from `$41` on --
`loc_3462`'s hit effects ran the next script, and `$47` (an enemy at x
`$198`) none, which hung the fight. A battle object whose script index is
past its table now says so on stderr once.

### Boss battles' palette effects (build 92)

`loc_67B8` runs once a frame of the battle loop, after the battle's routine,
by `Battle.palette_effect` (`$CC98`): 1 is the Neisword's light, set when a
cursed member's 1-in-16 roll lifts Dark Force's curses -- the backdrop
through six colours every other frame; 2 is Mother Brain's lights, set as her
battle's dissolve ends -- the chase, the glow and the rainbow that are all
there is of her colours. docs/history.md, Build 92, has both, and Dark
Force's curse checked against the ROM. The Neisword's light borrows the
level's palette-cycle counters (`$F632`/`$F634`), which game.c hands to the
battle and takes back.
