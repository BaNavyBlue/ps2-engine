# The game: start, saves, buildings, the C menu, the world's objects

Moved verbatim from the README in build 81c.

## Game start: `ScreenID_Intro`

Start on the title sets `building_index` to 0 and enters `IntroScreen`
(ps2.asm:12341). Zero does two jobs: `loc_89EE` resets RAM to a new game,
and bit 0 clear sends `IntroScr_CheckRoutine` to `EventSelectGame` rather
than `EventStartGame`. The port is three modules:

- `game/window.[ch]` -- ProcessWindows, CheckPrepareWindows, LoadScript,
  RunScript and the cursor objects. It keeps the original's queues
  (`window_index`, `script_id`) and its three idle conditions, because
  every event routine is written against them. It does not keep the
  saved-background buffer at `$FFFFDE00`: planes are rebuilt every frame,
  so a window's text lives in the window's own cell grid instead.
- `game/intro_select.[ch]` -- the eighteen states of `EventSelectGame`,
  labelled with their `loc_` addresses.
- `game/intro_start.[ch]` -- the five states of `EventStartGame`, the
  dream. `IntroScreen` is entered a second time for it, fade and all,
  with `building_index` = 1 so the reset is skipped.
- `game/gamedata.[ch]` -- the save block as a struct, the new-game reset,
  and the save files.

The dream's three effects block the main loop in the original, so they
pause everything else here too. The battle picture's map goes up over
blank tiles and its art bleeds in through `FadeArtTiles`, the title's
dissolve: four frames a step, 64 steps. `loc_6C54` takes it out again in
32 one-frame steps, each clearing a byte -- two pixels -- per tile, and
then line 3 of the live palette is zeroed, which is what actually
finishes the job. The waking picture goes up whole under that blacked-out
line, palette `$30` loads into the fade target and `loc_61F4` walks line
3 up to it over 21 frames. Both maps draw with palette line 3: the first
picture is simply the title palette's fourth line.

Text is two tile rows per line. `VDPCharacterMaps` (ps2.asm:27449) maps a
CHARSET code to an (upper, lower) tile pair; the upper is `$26` throughout
this ROM. That table is where "+28" comes from, it is why `:` (`$77`) is
tile `$80`, and it puts script digits at `$97`+d -- the 118+ set is the
one WinArt's static text uses. WinArt strings are assembled under their
own charset (ps2.asm:29410) and are absolute tile indices; `loc_980A`
adds the byte straight onto `$8500`.

### Save files

`saves/slotN.json`, one per slot. The cartridge's checksum, mirror copy
and copyright stamp are not carried over: a slot is in use if its file
exists, and the backup check is whether it parses. A file that does not
is renamed `slotN.json.bad` and reported with the original's "cannot be
played" message -- it is never overwritten, since it may be a hand edit
with a comma missing.

Items are named by their `ItemID_` constant and party members by name.
State the port has no name for yet is kept as rows of hex
(`event_flags`, `treasure_chest_flags`, and the whole `$C600` block as
`raw_c600`) so a load and save loses nothing. Named fields win over
`raw_c600` on load; naming another is one line each in `pack_c600` and
`unpack_c600`.

(build 138e) Into the emulator and back: `tools/to_srm.py` writes a port
save into a slot of the cartridge's battery RAM as RetroArch keeps it, with
the checksum, mirror copy, copyright and the names' tiles the ROM wants, and
reads a slot back into this format (docs/emulator_saves.md).

### The $C600 block (build 104)

SaveData (ps2.asm 19604) writes the 512 bytes of the characters, the 256
of the flags and the 158 of `$C600`-`$C69D` whole, then a checksum
(`$C69E`) and the copyright tail; LoadSavedData reads 256 bytes back from
`$C600`. Every byte of the block, by what reads it -- read in every routine
that names a `$C6xx` address, and the three that reach it through a
register (SaveData and LoadSavedData, the tower room's keeper as character
`$18`, the C menu's ORDER):

| Bytes | What | In the save |
|---|---|---|
| `$C600`-`$C60F`, `$C620` | party, recruits, money | `party`, `money` |
| `$C627`, `$C628`-`$C637` | the Central Tower room's keeper: a count (the room says it is full at 16, loc_C224) and the items; the keeper is character `$18`, so its bag sits where a ninth character's would | `tower_room_items`, item names |
| `$C640`-`$C645` | map, y, x | `location` |
| `$C646`-`$C64B` | where the last Data Memory save was made (loc_B5A4), a new game's start until then | `telepipe_lands_at` |
| `$C64C`-`$C651` | where the party last went into a dungeon: written by a level exit from a map under `$10` into one at `$10` or more (loc_6F1C: the map left and the party's cell), by the Mruragum (loc_9CD6: Motavia, the scooter's cell), a new game's start until then | `dungeon_entrance` |
| `$C652`, `$C654`/`$C656` | on the scooter; its parking cell | `location` |
| `$C658` | the planet: set by every level load (LoadLevelData, 13229: bit 7 of the level record's first byte) | `location.planet` |
| `$C65A` | the planet the last save was made on | `last_save.planet` |
| `$C65C` | the last save was made with the Visiphone: a copy of the building's third byte (`$F766`), 1 from Visiphone_ItemSelected, 0 at every town's Data Memory door (all eight) | `last_save.by_visiphone` |
| `$C660`-`$C67F` | the eight names | `characters[].name` |
| `$C680`-`$C685` | the slot list's name and Rolf's level | `slot_label` |

**Where a pipe goes.** The Telepipe works on the four world maps only
(Motavia, Skure B2 and B1, the Skure field); Ryuka is the same code
(TechEffect_Ryuka). On Motavia: a last save made on Dezolis lands in Paseo
by the Central Tower's door (cell 31,16); a Visiphone save, in Paseo one
under the Data Memory's door (17,8); otherwise at the last save's own place.
On Dezolis: a last save made on Motavia lands on the Skure field at the
spaceship's cell (60,79); a Visiphone save, in Aukba one under its Data
Memory's door (7,6); otherwise at the last save's place. The Escapipe and
Hinas work from the Esper Mansion B1 up (every dungeon) and go to
`dungeon_entrance`; so does YES at the Control Tower's keyboard (loc_DAF0).
CONTINUE (loc_D3B6) shows Motavia's attendant, or Dezolis's when the last
save was not a Visiphone one and was made on Dezolis.

**Scratch the save carries.** Nothing reads these back after a load:
`$C602`, a step counter the Data Memory's level report, ORDER, the regroup
and shop party lists and the victory's loop share; `$C610`-`$C617`, the order
ORDER is building (its first half also cleared when Shir or Nei leaves);
`$C618`-`$C61F` and `$C624`, the party held while Nei fights Neifirst alone
(loc_DD12, put back in loc_F1F6 or loc_DD7A) and the copy the shop and STRNG
party lists draw from; `$C63A`, the name board's cursor, and `$C63C`-`$C63F`,
the letters typed; `$C69C`, `$FFFF` when the slot holds a save (the save
list and CONTINUE read it in the slot, not here). The opening Motavia crawl
also uses `$C648`/`$C64A` as its camera's targets (loc_E47C-loc_E4D8), which
a new game or CONTINUE overwrites before anything reads them.

**Nothing touches** `$C626`, `$C638`-`$C639`, `$C65E`-`$C65F` or
`$C686`-`$C69B` except SaveData and LoadSavedData, which carry them whole:
no instruction names them, and no base register points at them.

**The file.** The named fields above are written from the block and win
over `raw_c600` on load; `raw_c600` stays so older saves load and the scratch
survives. Maps are written by their names in `assets/levels/levels.json`
("Paseo", "MotaviaOutside"), and read back as a name in any case or a
number. `src/game/gamedata.h` names the offsets (`GD_LAST_SAVE_MAP`,
`GD_ENTRANCE_MAP`, `GD_KEEPER_ITEMS`, ...) and code reads them through
`gamedata_word`. Saves the port made on Dezolis before build 104 say
Motavia (`location.planet`, `last_save.planet`): the next level load puts
the planet right; a last save's planet stays as written until the next save.

`make intro_probe` runs new game, continue and erase end to end against
real files with a scripted pad. The headless build takes the same kind
of script: `PS2_START=400 PS2_PAD="800:c,960:right,980:c"`.

### The recruits and the technique counts (build 106)

**The recruits** (`$C604` party_members_joined, `$C606`
party_member_join_next). Both count characters in the order they join,
which is their character numbers -- Nei 1, Rudo 2, Amy 3, Hugh 4, Anna 5,
Kain 6, Shir 7 -- so each names a recruit. A new game sets 0 and 1 (the
dream's Nei); Nei's joining after the dream makes the first 1 (loc_AFE6).
The second is the last recruit the story has sent to Rolf's house: the first
visit to Arima, Oputa, Zema, Kueri and Piata each raises it by one, in that
order only (LoadSpritesInLevel's tail, ps2.asm 848-886), and so does Hugh's
level action (loc_DBF8, 20162); a theft of Shir's raises it to 8
(ProcessStealItem) and takes her out of the party. At Rolf's house
(Building_RolfHouse, loc_B04E): the two equal, "Ahh, home at last" and the
party's companions; else, with the second at 8, "Hey, Shir is coming back!"
and loc_B282 lowers it to 7 and puts her back last in the party; else the
first goes up by one, "There's a knock at the door!", and that recruit's
portrait and introduction (RolfHouse_CharIntroTextPtrs by the first).

The save writes them as `party.last_recruit_joined` and
`party.last_recruit_sent`: "none", "Nei" ... "Shir", and for the second's 8
"Shir away". Saves before build 106 had the numbers as `recruits_joined`
and `next_recruit_event` (not an index into a table: the recruit number);
both still load, and so does a number under the new keys.

**The technique counts** (`+$25` and `+$26` of a character's record).
loc_112C4 builds the $C800 list every technique window reads: for each
character, the low five bits of `+$26` say how many of the level-screen half
of CharAvailableTechniques are known, and the low five bits of `+$25` how many
of the battle half; bit 7 of either puts Musik (`$33`) in that half's last
known place (loc_112FC). A level-up adds the level row's last byte, its high
nibble to `+$25` and its low to `+$26` (Character_ProcessLevelUp). Ustvestia's
lesson sets bit 7 of `+$26` and, the first time, adds one to the count
(loc_C7D6), so Musik is counted among the map techniques. Nothing sets bit 7
of `+$25`.

The save writes them as `battle_techniques_known`, `map_techniques_known`
(Musik counted) and `knows_musik`; `battle_musik` appears only if bit 7 of
`+$25` is set, which no game does. Before build 106: `techniques_a` and
`techniques_b`, the raw bytes, still read.

One difference kept: CommandAction_Technique tests the whole battle byte
(`tst.b`), the port its low five bits; they differ only with bit 7 of `+$25`
set and a count of 0, which no game reaches. With a count of 0 and bit 7 set,
loc_112FC would write Musik into the byte before the half, which the port
does not either.

Route `save_named_fields`: a save with only these names (Shir away, Rolf
three map techniques with Musik) -- Rolf's house brings Shir back
(`$0927`, the count to 7, Shir last in the party) and Rolf's TECH list is
RYUKA, HINAS, MUSIK. Fails on build 105, which read neither name (the count
3, line 31).

### From the dream to control of Rolf

`loc_D5EA` leaves the dream with `building_index` = the Governor,
`portrait_index` = `$17` and `demo_flag` set, and everything after that
is those three values being passed back and forth between two screens:

1. `LevelScreen`, Paseo. `Demo_HouseToTower` walks Rolf to the tower;
   its `$FF` switches to `ScreenID_Building`.
2. `BuildingScreen`, the Governor. With no Recorder in the party and
   `$C736` clear he gives the briefing, `$1101` then `$1105`. `$1101` has
   no terminator and runs on through `$1104`. `loc_CA00` then names
   `BuildingID_RolfHouseStart`, portrait `$37`, `demo_index` 1, and
   `CloseAllWindows` leaves `event_routine` at 0, which is the way out.
3. `LevelScreen` again, from the tower door, on `Demo_TowerToHouse`.
4. `BuildingScreen`, Rolf's house. Portrait `$37` is past the end of
   `PortraitTable`, so `LoadPortraits` loads nothing and window `$72` is
   Rolf himself, on the right. `$901`, then Nei's window and her art
   loaded mid-scene, then `$902`-`$907`. `loc_AFE6` clears `demo_flag`,
   sets the party to Rolf and Nei and closes everything.
5. `LevelScreen` a third time, outside the house, Nei behind Rolf, the
   pad live. This is the first frame the player has control.

`game/building.[ch]` is `Building_CheckRoutine` and those two buildings.
Every building works the same way: `event_routine` 1 opens window
`WinID_PortraitStart` + `portrait_index`, 2 and up are the building's own
states, one each time the windows fall idle, and 0 returns to the level.

A portrait is an ordinary window whose interior bytes index different
art. `loc_980A` adds each byte to `$FFFFF72C`; `Win_Portraits` sets that
to `$C400` (line 2, the art `LoadPortraits` put at tile `$400`) for the
draw and back to the font's `$8500` after, and `Win_RolfPortrait` uses
`$A400` (line 1, `RolfPortraitArt` at `$4A0`, which `BuildingScreen`
always loads). The frame is drawn from a hard-coded `$8500` whatever the
base is. `tools/extract_buildings.py` writes `PortraitTable` and
`BuildingMusicPtrs` as `assets/data/portraits.json` and `buildings.json`.

The rest of the party has no movement of its own (`Obj_FollowingChars`).
The leader records his position every moving frame in a 64-entry ring at
`$FFFFDD00`; follower *n* takes the entry 16*n* back, turns to face the
way that moved it, and borrows the leader's step counter for its walk
cycle. `ObjCL_Init` fills the ring with the start position, so the party
begins stacked on the leader and fans out as he walks. Art and palette
line per character come from `loc_118C8`: Nei, Hugh and Shir draw with
line 1, and Amy/Shir and Hugh/Kain share art.


### The shops

`Building_WeaponStore` (ps2.asm:17136) and `Building_ArmorStore` (17279)
are one routine assembled twice with a different script bank, `$6` and
`$7`; `Building_ItemStore` (17422) is longer because it has a SELL branch.
All three are in `game/building.[ch]`, with the windows they open in
`game/window.[ch]`: `WinID_BuySell`, `WinID_StoreMeseta`,
`WinID_StoreInventory`, `WinID_StoreCharList` and the two pages of
`WinID_MenuItemList`.

**The door carries the stock.** A town's interaction entry has four bytes
after the cell: building, portrait, and two that `loc_6EE6` writes to
`$FFFFF764` and `$FFFFF766`. The second indexes `StoreEquipItemArray`
(26269), six item ids per shop; the first is the cursor's last stop, so a
five-item stock (every item store ends in `ItemID_None`) is still six
bytes wide but scrolls over five. `tools/extract_stores.py` writes the 25
stocks to `assets/data/stores.json`, named from the disassembly's own
comments, and `walk.c` now passes both bytes through to `BuildingState`.
`doors.json` had them all along as `f764` / `f766`.

**Two ways out, and B is the abrupt one.** `event_routine_sub` is the
exit path: 1 says goodbye (`$608` / `$708` / `$80D`), anything else
runs `CloseAllWindows`. Every list window's poll (`loc_11028`) bumps
`event_routine_sub` on B, so B on the stock list, the party list or the
BUY/SELL box does not step back a menu, it leaves the shop. The only
"back" is NO on a yes/no. The item list is the exception: `loc_11060` on
B closes one window and steps `event_routine` back instead, which is why
B there returns to the party list.

**Cursors keep their places while their window is open.** After "who's
going to take this?" the party list is not reopened but reactivated
(`window_index_saved = WinID_StoreCharList`), so the lamp is still on
whoever was refused. A closed window forgets its cursor, which is why the
BUY/SELL box is back on BUY the next time round.

**The second page.** `Win_MenuItemList` loads its cursor with `$108` when
a character carries nine or more things: `ObjRRC_Init` takes the high
byte as the start index and the low as the last stop, so the lamp opens
on the first item with NEXT one stop above it, in the header row. NEXT
opens `WinID_MenuItemList2` one row down and one column right, whose
own NEXT closes it again; picking from it adds 7 to the slot (the index
is 1-based on that page). Selling then closes two windows or three
(`loc_BF02` tests bit 3 of the slot).

**Prices.** `CheckIfDoublePrice` doubles everything on Dezolis when Rolf
wears a Magic Cap; `CheckPlanetAndCaps` refuses to trade at all on
Dezolis unless a living Rolf wears a Magic or Mogic Cap (`$810`,
"Danyaraha? bebekucha?"). A sale pays half the list price, or a flat 10
under the Magic Cap on Dezolis. `AddToMoneyOwned`'s cap test is on the
amount added rather than the total, as written. Something you sell
while wearing it comes off first (`loc_AE2E`): the item's body-part
nibble names the slot, a one-handed weapon is whichever hand holds it,
a two-handed one is both, and the equipment bonuses are taken off and
put back around the change (`loc_ADBE`, `loc_AD4C`; the same pair
`SetCharEquipment` uses, now exposed per character in `gamedata`).

**Shir.** `ProcessStealItem` runs on the goodbye: with Shir in the
party, alive, not full, and level 10 for the Central Tower room, one
roll in sixteen puts an item from `StealItemArray` in her bag and takes
her out of the party, and writes `$C00` to the level's `event_flags`
for the scene outside. That dispatcher (`loc_7E02`) is only ported as
far as the opening crawl, so `BuildingState.event_flags` holds the
value and the level screen does not yet act on it.

**Window order in a `move.l`.** `move.l #((WinID_StoreMeseta<<$10)|
WinID_ScriptMessage2), (window_index)` is big-endian: `window_index`
gets the meseta box and `window_index+2` the message window, so the
message window opens *last*. That matters. Its phase 0 leaves
`window_index_saved` set, which holds the building off for a frame, and
its phase 1 is what points the text at it and loads the greeting. With
the order reversed the meseta box opens last, clears
`window_index_saved` at once, the building runs its next state before
any text has loaded, and the message window's phase 1 never runs, so
the text lands in stack slot 0 -- the portrait -- drawn with the
portrait's tile base. ps2-port_31 shipped with that reversal in all
three shops; it showed as garbage over the shopkeeper and an empty text
box. Every long-pair write in the buildings is now in ROM order, with
the ROM's word noted beside it.

### The other buildings

All from `game/building.c`, each labelled by its `loc_` states.

**Hospital** (`Building_Hospital`, 16928). HEAL costs the number of
living members short of full HP or TP times the door's `$FFFFF764`
(8 in Paseo), doubled under the Magic Cap on Dezolis; CURE costs 10 and
clears bit 15 of the status word. Nobody hurt is `$509`. Paying opens
`WinID_MenuCharStats`, waits 30 frames (`$FFFFDE5E`), heals or cures,
redraws the panel, waits 30 more, closes it. The panel is seven columns
a member -- name, then a two-tile label (`loc_11432`: dead, the poison
skull, or "HP"/"TP") and three-digit HP and TP. Columns for members you
do not have keep the art's own "HP ... 0" placeholders, as in the ROM.

**Clone lab** (`Building_CloneLab`, 16773). Level x 20 meseta to bring
back someone dead; someone alive is "no place for stupid jokes". With
`demo_flag` set -- after Neifirst -- `$408` replaces the greeting, anyone
else dead is revived free, and the lab hands over to the Central Tower
on `Demo_CloneLabToTower` (demo 7, now in `walk.c`).

**Data Memory** (`Building_DataMemory`, 16552). One report per member
of the experience to the next level, `levels[level].exp - experience`
from the extracted tables (absolute per row; `gamedata_level_exp`);
level 50 is `$30D`. Then save: slot, overwrite check, name entry (a
blank name keeps the slot's old one), and `SaveData` fields `$C646`-
`$C64A`, `$C65A`, `$C65C`, `$C684`, `$C69C`, all into the save file.
"No" to more adventure is `jmp EntryPoint`, which in the port is
`BUILDING_RESET`: back to the SEGA screen. **CONTINUE lands here**:
`$FFFFF764` = 1 gives "We meet again", the report, "Be careful out
there!" and the street where you saved.

**Teleport station** (`Building_TeleportStation`, 18522). 60 meseta,
doubled on Dezolis under the cap. The town list is `$C700 + level`
holding `level` for every town you have been in, written by
`LoadSpritesInLevel` on every level load -- the port now does that in
`enter_level_walk` -- minus the town you are in. Arrival cells are
`TeleportLocCoord`. No other town known is `$C03` then `$C04`.

**Central Tower desk** (`Building_CentralTowerOutside`, 17657). ROOM,
LIBRARY, OUTSIDE, and ROOF once `$C73F` is set. The first two (three)
change building with `screen_changed_flag`, which is now real:
`BuildingScreen` runs again for the new building, and one the port does
not have leaves you on the street. With the Recorder and Rolf alive it
is "This must get to the governor!" straight to the Governor. The
`$CD22 = $B4` on the `$C736 == 1` branch is dead: the message window's
own phase 1 resets it a frame later, in the ROM as here.

**Central Tower room** (`Building_CentralTowerRoom`, 17741): the item
keeper. `character_index` `$18` is a ninth bag, and its count and slots
land where a ninth character's would, `$C627`/`$C628`, inside the
`$C600` block -- so it was already in the save file. `gamedata_bag`
returns either kind of bag and every add, remove and item list goes
through it. KEEP BAGGAGE takes anything with bit 5 of its action byte
(taking it off first); BAGGAGE PLEASE gives it back to whoever has room.
`Win_RoomOptions` sets `event_routine` back to 4 on every frame it waits.
Shir can steal here too, from the Visiphone list, at level 10 or more.

**Library** (`Building_Library`, 17932): five topics from
`LibraryTextIndArray`, and news that follows the story (`$C722`,
`$C737`, `$C73F`). `BuildingScreen` loads the graph's art and palette
(43) in Rolf's portrait slot here. With the Recorder the librarian shows
the graph and swaps the Recorder for the Key Tube in the same hands; the
`PaletteLoad2 $32` on the way is not ported (it only sets a fade target
nothing fades to before the screen changes), and that branch has not
been checked against the emulator.

**Rolf's house** (`Building_RolfHouse`, 16198), when you walk in rather
than at game start. With nobody new: "home at last", who is with you,
and whether to go out again; NO opens SEE STRNGTH / REORGANIZE /
OUTSIDE (B on it is OUTSIDE). SEE STRNGTH lists everyone who has joined,
loads the chosen character's portrait over Rolf's (`LoadPortraits`
mid-scene) and shows `WinID_HouseLVEXP` (name, level, job from
`loc_11456`, experience), the profile text and `WinID_StrngStats`
together until B or C closes all four. REORGANIZE rebuilds the party
from Rolf, Nei unless `$C737` is set, and up to two more picked from
`WinID_RegroupCharList`; with only Rudo joined it stops after him
(`$92F`). B on that list leaves the party as picked so far, as in the
ROM. Like the ROM it also puts every character's battle command back to
its default first (`loc_F550`; see "Command defaults" under the fight).
When `party_members_joined` is behind `party_member_join_next` there is
a knock at the door instead: the newcomer's portrait, their
introduction (`RolfHouse_CharIntroTextPtrs`), and the offer to rename
them (`_CharNameChangeTextPtrs`); a name left blank keeps theirs, and
Shir keeps hers regardless. Shir's return after a theft is the same
door with `$927`/`$928`.

What moves `party_member_join_next` on is `LoadSpritesInLevel`'s tail
(ps2.asm:822): first entering Arima with it at 1, Oputa at 2, Zema at
4, Kueri at 5 or Piata at 6 adds one (3, Hugh, is a level action). The
same code parks the jet scooter at a fixed spot in Paseo and those
towns when it is owned (`$C716 == 2`). Ported in build 42: before it,
Rudo never knocked.

The house reads the pad itself while a profile is up
(`WaitJoypad_B_C_Pressed`), with no window live, so `building_tick`
now takes the input.

**Gaira's control panel and Tyler's spaceship** (18668, 18715; builds
81-82). The panel is words, waits and the red alarm (`loc_66F6`); Tyler's
spaceship shows the dream's fight (`loc_70A32` over `loc_6D7BA`), a second
picture (`loc_70D42` over `loc_6F040`), then Tyler and the monitor
(`TylerPortraitTileInd`, `TylerSpcshp_MonitorTileInd`'s five frames over
`TylerSpaceshipArt`, palette `$2A`) with Palm's explosion
(`Obj_ExplosionTylerSpaceship`), each picture dissolved in by `loc_6BBE`
and out by `loc_6C54` with the rest of the screen stopped. Tyler's two
maps come from `art/*.bin` (`make extract-nametables`, from the ROM since build 185;
the `.tmx` files are in the tree) and are checked pixel for pixel against
ROM data in build 83a, all five monitor frames. `loc_CE5E` gives the gear back and
hands over to demo walk 8. Details in docs/history.md, Builds 81, 82 and 83a.

**The roof** (`Building_Roof`, 18055; build 84). ROOF on the desk and
Skure's door (79,59) are both building 11, portrait `$18`. On the tower:
`$F01`, YES/NO, NO is `$F03` and the street, YES is `$F02` and the flight to
DezolisSkure 60,79. On Skure `loc_8318` adds 5 to event_routine and the
flight to Paseo 31,16 starts on the first idle pass, with no question. The
flight (`loc_C58A`) is the planet in plane B (`$39` Dezolis, `$3A` Motavia)
and `Obj_Spaceship` for 120 frames after a 21-frame fade-in; game.c runs it
inside the building screen. Details in docs/history.md, Build 84.

**The window cell grid** was 26 columns ("widest interior is 24"); the
party panel's interior is 27, so each row's last cell ran into the next
row. It is 32 now, and `build_entry` refuses a map that does not fit
rather than writing past it.

`make building_probe && ./building_probe DIR` runs every building above
and the shops with a scripted pad: buying, refusing, insisting, running
out of money, selling a worn item, healing, curing, reviving, the
post-Neifirst revival, saving to a slot with a typed name and resetting,
the CONTINUE arrival, teleporting, the tower desk, and Rolf's house --
a profile, Rudo arriving and being renamed, and regrouping with him. It prints every
script id shown, where each one ended (street, building change, reset)
and the party's money, bags, HP/TP and status after, and drops a PPM
wherever the pad says `s`. It writes `saves/slot3.json` in the current
directory.

`PS2_BUILDING="building,portrait,f764,f766[,cellx,celly]"` starts the
real game inside a building with a new-game party, exactly as a door
with those bytes would; the optional cell is where on Paseo leaving puts
you, so `PS2_BUILDING=5,13,5,2,57,18` leaves the weapon store standing
under the item store's door.

### Pause and the C menu

**Start pauses** (`CheckGamePause`, ps2.asm:8556), on the map and in
buildings. One tick is one VBlank, and the ROM's loop is CheckGamePause,
WaitForVBlank, logic -- with CheckGamePause reading the pad as it was
at the *previous* VBlank. So the tick Start is pressed on runs, and the
pause starts on the next. While paused, **B held or C pressed** makes
`GamePausedLoop` return, but the main loop then waits one more VBlank
before any logic, and that read recomputes `joypad_pressed`. The frame
let through is therefore the tick *after*, with its own input: the C
that released it is gone, so **the menu does not open**, and B held
runs one frame in two -- half-speed slow motion. Start in the loop
unpauses the same way. (Build 35 ran the let-through frame on the same
tick with the C still pressed, which opened the menu, at full speed.)

The pause sound `$D5` is a short jingle on FM4 and FM5 whose first
track ends in `cfE8 $01`, which mutes every track (`RefreshVolume` with
`$FF - volume`) and stops the driver. A stopped driver never reads its
queue, so unpausing plays `$D6` and clears `paused_mode` directly, as
the ROM does (`audio_music_resume`).

**The stuck warbling voice (build 35) was the sequencer, not the pause.**
Both jingle tracks `cfF8`-gosub into a shared phrase. `PlaySFX` clears a
track and copies nine header bytes, so an SFX track's gosub stack
pointer (`+$09`) is 0, and the return address is written `$FC` past the
track -- beyond track RAM for the SFX tracks, into RAM at `$FFD210` up
that nothing in the ROM names, which simply holds it. The port sent
those writes to a single dummy byte, the return went nowhere sensible,
`cfE8` was never reached, and the jingle's pan-animated notes carried on
unmuted over everything, pause or not. `Smps` now has those 256 bytes
(`ram_after_tracks`). Any other SFX that gosubs was affected the same
way.

**Build 37 still played something during the pause, with a pitch that
carried on after unpausing. Build 38 does not fix it, because at every
level that can be run without a synthesiser the pause is clean.** What
was checked:

- The sequencer alone (`smps.c`) through `$D5` and `$D6` on every song
  `$81`-`$97` at ~285 pause moments each: `paused_mode` is set 28 ticks
  after `$D5` every time (the FM4 phrase is 3+3+6+8+8 ticks, then
  `cfF9`, `cfE8 $01`, `$F2`); nothing is emitted while paused; no SFX
  track is left in use and no music track left overridden after
  unpausing. Every SFX `$A0`-`$DF` ends on its own on top of the town
  music except `$AD` (JetScooter) and `$D8`, which loop by design.
- The whole game, headless, through a Start on the Paseo map and one
  in the weapon store (`tools/pause_probe.c`, below): the mute writes
  land on FM1-FM3 with TL `$6B` or more (the bank's carriers are TL
  0-5, so `$FF - volume + TL` never wraps into loud), FM4 is keyed off
  by the jingle's end, FM5 was keyed off at its rest six ticks earlier,
  the last drum plays out, and from then until Start again there are no
  writes at all. Unpausing emits exactly the ROM's order: `$D6`'s
  `loc_BA656` kills the leftover FM5 SFX track, `SetMaxRelRate` on FM4
  and FM5, the jingle, `cfE8 $00` restoring FM1-FM3 twenty-eight ticks
  later, and FM3's chord voices back on FM4/FM5.

So whatever sounds is downstream of the writes: `fm.c`'s pacing of them
into Nuked, or the SDL audio thread. Two ways to tell which, both new
in this build:

- `PS2_SNDLOG=1 ./ps2` prints every chip write from the real build on
  stderr with its tick, plus each id queued. Pause, wait, unpause, and
  the stretch between the mute writes and the `queue D6` line should be
  empty. If it is and the tone is still there, the sequencer is
  exonerated for certain.
- The headless build renders the mixer with no SDL and no second
  thread, and with `ym3438.c/h` in `src/audio/` it renders real FM:

      PS2_FRAMES=1800 PS2_BUILDING=5,13,5,2,57,18 \
      PS2_PAD=200:b,260:b,320:b,900:start,1300:start \
      PS2_WAV=pause.wav ./ps2_headless

  leaves the store, pauses on the map at tick 929 and unpauses at 1301,
  so the file is paused from 15.5 s to 21.7 s. A tone in that stretch
  means `fm.c` or Nuked; a clean stretch means the audio thread.

`tools/pause_probe.c` is the game-level trace above as a tool:
`make pause_probe`, then `./pause_probe 900 1300 1800 5,13,5,2,57,18
200:b,260:b,320:b` prints the writes for 40 ticks after each Start,
anything at all in between, and the track blocks at each boundary
(`--freq` keeps the per-tick `$A0`-`$A6` frequency writes, which
vibrato repeats every tick).

Build 39 also moves the `$D5` write: `LevelScreenLoop` (11443) is
`CheckGamePause`, `WaitForVBlank`, `RunObjects`, so the ROM writes the
pause sound to slot 0 at the top of the loop *after* the logic of the
tick that saw Start, on top of anything that logic queued. The port
had queued it before the tick's logic, so a level-change or talk sound
on the same tick replaced it and the music played through the pause.
`pause_gate` now only flags the press and `game_tick` queues `$D5` at
its end -- the same driver frame picks it up, so nothing else moves.

One thing that did change in build 38: `smps_play` now overwrites queue slot 0,
because every one of the ROM's eighty writes is `move.b #id,
(sound_queue).w` and nothing ever touches slots 1 and 2. Filling the
first empty slot let two ids queued in the same field both play, where
the ROM keeps the last.

A ROM quirk met on the way, for the emulator: in the Movement theme
(`$94`) a pause in the first ~45 ticks never mutes the music. The song
hands off to `$96` at that moment, `PlayMusic`'s `SilenceAll` wipes the
jingle before it reaches `cfE8`, and the game is paused with the music
running. The port does the same.

**C opens the menu** (`ProcessPlayerMenu`, 14099) when no window is up,
no demo walk is running and no level event is (`tst.b event_flags`, the
high byte). It runs between the party's move and the A button, like
`LevelScreenLoop`, on the window system's queues: `event_routine` 0 is
the menu shut, and the entry chosen (`$FFFFDE80`) owns the states from
1. B on the menu closes it.

STRNG is ported: the party list, then level/job/experience, HP/TP, the
seven stats and the five equipment slots together; B steps back to the
list. C goes on to the technique lists (`WinID_FullTechList`, `_2`),
map techniques in the first and battle ones in the second, sixteen each
in two columns; B there steps back to the stats, C closes the menu.

What a character knows is `loc_112C4`'s table at `$C800 + 32 x
character`: the first N of `CharAvailableTechniques`' map list and of its
battle list, N being the low five bits of the character's `+$26` and
`+$25` bytes, zero-filled, with Musik (`$33`) in the last place taken
when bit 7 of the count byte is set (`gamedata_known_techs`).
`tools/extract_techs.py` writes `assets/data/techniques.json`: all 64
`TechniqueData` records (name, TP, action bits, rate), named by their
own labels because the constants name only 54 of them, and each
character's two lists by technique name.

**ITEM** (`Menu_ItemEntry`, 14143): `WinID_MenuItemChar` -- the party
with "WHO?" in its bottom border -- then the item list, then USE / GIVE
/ TOSS (`WinID_ItemAction`, `$FFFFDE88`). Every state is driven by the
`event_routine_sub` count the way the ROM's chains of `subq.w #1, d1`
count it down, so each handler is a switch on that value:

- **USE** opens the "'Character' uses 'Item'" message first, and the
  item's own handler either keeps it (a description), replaces the
  queued word with `WinID_ChosenItemChar` ("ON?") for the mates,
  Antidote and Moon Dew, or refuses when the user is dead. Items with
  bit 3 of their kind byte clear do nothing ("Nothing happens."). The
  mates heal 20 / 60 / all on the target after a `WinID_MenuCharStats`
  box and a 30-frame wait (`$FFFFDE5E`), then a `(6<<8)|MenuCharStats`
  redraw and `SXFID_Healed`; a poisoned target gets `$10C` and no
  healing, a dead one `$102` and back to the list. Antidote clears the
  status word, Star Mist fills everyone who is standing and unpoisoned,
  Moon Dew sets the target to full. Telepipe and Escapipe check where
  you are (`$27` "can't be used here" above Skure, or below the Esper
  Mansion), then set `level_index` and the arrival cell -- Paseo,
  Skure or Aukba with `$C65A` / `$C65C` deciding which, or the outside
  position saved at `$C64C` -- queue `SXFID_Teleport` and reload the
  level (`MenuState.level_change`, screen_changed_flag = -1). Hidapipe
  plays its sound and sets `$FFFFCB0E`, which only battles read. The
  Nei Sword goes to the Esper Mansion from Noah. Teim, the letter, the
  recorder, the leaf, the prism and the ring show their scripts; the
  Visiphone opens Data Memory as a building with the map position
  kept (`MenuState.to_data_memory`).
- Keys, dynamite, the key tube, Maruera gum and the four cards look at
  what the party faces (`loc_9BFE` / `loc_9C12`,
  `Level_ChkTargetInteract`). Containers and dungeon fixtures are not
  in the levels yet, so they take the "nothing there" path: the key
  "is a key for a small container", dynamite is "taken out and put
  back", a card shines, the gum says the ocean bottom would be a good
  place -- exactly the ROM away from the object.
- **GIVE** opens the "ON?" list; a dead target (`$102`), a full bag
  (bit 4 of `+$27`, `$0A`) or the plasma ring (`$0F`) refuse and go
  back to the list. Otherwise `loc_AE2E` takes the item off if worn
  (bonuses off, the slot cleared, bonuses on), `RemoveItemFromInventory`
  shifts the bag down, `AddItemToInventory` appends, and the menu shuts.
  Giving to yourself is `$24` and a no-op shift, as in the ROM.
- **TOSS** asks Yes/No (`WinID_YesNo`, `$C`) unless the item's kind
  byte has bit 7 clear, which is the key items (`$B`); a dead tosser is
  refused (`$6`). YES unequips and removes and shuts the menu; NO is
  `$D` and closes the question and the message.

**STATE** (`Menu_StateEntry`, 15044): `WinID_StateOrder`. STATE opens
one `WinID_FirstCharStats + n` box per member -- HP/TP with maximums,
the dead or poisoned icon over "LV", the level and the name -- and
`WinID_MenuMeseta`; B on the meseta closes the boxes back to the
STATE/ORDER window. ORDER copies `party_member_id` to `$C618`, opens
`WinID_CharOrderDestination` then `WinID_CharList2`, and every pick sets
bit 15 on the member in `$C618`, appends it to `$C610` and redraws both
(destination first, the long's high word); the last member places
itself, `$C610` becomes `party_member_id`, the list closes, and the next
B or C shuts the menu. Alone, ORDER shows `$927` -- the ROM's wrong
pointer, "Hey, Shir is coming back!" -- as the ROM does.

**TECH** (`Menu_TechEntry`, 15152): the stats box and the "WHO?" list,
a dead character (`$6`) or one with no field technique (`$101`), then
`WinID_LevelTechList` (two pages, NEXT at stop 0 when nine or more are
known). Picking a technique queues the "ON?" list and checks TP
against `TechniqueData+5`; too little makes it technique 0, which
`loc_A65C` turns into `$103`. Res / Gires / Nares heal 20 / 60 / all on
the target, Rever 30 (or `$10A` on a dead one; its later steps are
Res's one step on -- TechEffect_Rever subtracts once before joining
Res's code, TechEffect_Res twice -- which the port missed until build
96c, so nobody came back), Sar / Gisar / Nasar
the party, Sak fills the target at the cost of the caster's life and
Nasak the party the same way, Anti clears the poison bit; each with
the message, a `$8001` close, the 30-frame wait, the stats redraw and
-- only for the Res group and Rever -- a sound, since the ROM's Sar,
Sak, Nasak and Anti queue none. The effects that need no target write
their message over the queued "ON?" word (`win_replace`), as their
`move.w` to `window_index` does. Ryuka and Hinas are Telepipe and
Escapipe with `Level_TechEffect_SubtractTP` in place of the item; Musik
needs the wall it is played at, so it takes the "nothing happens" path.

**EQP** (`Menu_EqpEntry`, 15669): the party list, a dead or empty-handed
character refused, then `WinID_StrngLVEXP`, `WinID_EqpEquipList` (the
STRNG equipment art), `WinID_EquipStats` (agility, attack, defense)
and the bag on `WinID_ItemList2` / `_3`. An item whose mask (`+$D`)
lacks the character's bit is refused with `$205` (nobody can wear it),
`$201`, or `$203` for Nei; a worn item comes off (`loc_ADBE`, the slot
cleared, `$204`); an unworn one goes on: `WinID_RightLeft` first for a
one-handed weapon, both hands for a two-handed one, otherwise the slot
its kind byte names, with whatever was there unmarked in the bag and a
two-handed weapon leaving both hands (`loc_AC82`), then `$202`. After
the message `loc_AD00` puts the bonuses back on (`loc_AD4C`), closes it,
redraws the equipment and the item page in place, and steps back to
picking. The stats box is never redrawn: `$FFFFDE74/78/7C` hold
agility, attack and defense as (target, shown) pairs, `loc_AAE6` sets
both to the current value and `loc_AD00` writes the new ones into the
targets, and `loc_96EC`, run every VBlank that `$FFFFDEA8` is set -- the
item pages and the Right/Left window set it on every frame they are
polled, B clears it -- moves each shown value one point toward its
target and rewrites the three numbers. So the box counts up or down to
the new stats once the item page is back (`equip_stats_step`).

One rule the whole file follows, found the hard way (build 39's GIVE
said "can't carry any more items" and then gave the item): the ROM
picks a message by writing candidates to `(script_id).w` in turn, and
the last write wins. `win_script` appends to the script queue, which
played all of them; the menu uses `win_script_set` / `win_script_set2`,
the word itself.

`window.c` carries the fifteen windows these use -- descriptors from
`WindowArtLayoutPtrs`, fills (`fill_menu_char_list`,
`fill_individual_stats`, `fill_order_dest`, `fill_level_tech_list`,
`fill_equip_stats`) and polls -- plus `Win_YesNo` itself, and `{char2}`
now resolves from `character_index_2`. `CloseAllWindows` clears
`event_routine_sub_2` as well, which TOSS relies on.

`tools/menu_probe.c` drives all of it headless from a frame:button
script and prints the window stack and the party -- the flows above
were checked with it (USE, GIVE to a dead member, TOSS, STATE, ORDER,
Res on a living target, Hinas in town, unequip, equip, the Right/Left
window).

### Townspeople

`LevelSpriteData` places up to 32 objects per level, six bytes each: an
object id and interaction type, a dialogue number, and a position.
`LoadLevelData` decompresses the level's people art into VRAM from the
list its extra-art byte names (`loc_283EE`). `tools/extract_npcs.py`
writes all of that as `assets/data/level_sprites.json`, `npc_types.json`
and `people_dialogue.json`, plus `assets/tiles/people_*.png`.

The nine Motavian types share two wandering routines that differ only in
axis (`game/npc.[ch]`); Roron's Motas (`$1B`, build 74), the Dezolians
(`$1A`) and the musk cats (`$1C`, build 85) have four-way routines of
their own -- see docs/history.md for each, and for the Dezolians'
sideways check, which reads a d6 it never loads (`NpcSet.d6`). The
Dezolians and the musk cats talk sense only to a live Rolf in a Magic
Cap or a Mogic Cap (`loc_E584`). Every `$20` frames a person rolls 0-15: four
values turn them, two walk them a cell one way and two the other, the
rest do nothing. They never stray more than five cells from where they
started and do not step onto a solid quadrant, the party, or each other.
The hit test is `Level_ChkTargetInteract`: a 16x16 box one cell ahead of
the mover against the 16x16 box of everyone drawn last frame.

A talks to whoever the box ahead of the leader overlaps
(`ProcessAButtonPress`, every frame): the cell he faces when he stands on
one, and (build 133) wherever he has got to mid-step -- until then the port
held that A back to the step's end. The person turns to face him and
freezes until the conversation is over; a person who is mid-step cannot
be talked to. The dialogue number goes through
`loc_E70C`/`loc_E730` (or `loc_E6B0` after Climatrol) to a script in bank
`$16`; an empty cell gets `$1708`, "There seems to be nothing unusual
here."

**The first frame (build 52).** `LevelScreen` runs `RunObjects`,
`RenderCharSprites` and `BuildSprites` once before the fade in
(ps2.asm:11418), so the people and chests are in the first frame shown.
The port's screen logic waits for the fade, so until build 52 the objects
only took their places on the first tick after it, a frame or two late.
`npc_setup` is that pass: each object's init once and its place on
screen, nobody stepping.

### Treasure chests (build 45)

Chests are `LevelSpriteData` objects like the people, with interaction
type 2 and the chest's number as the index: `Obj_DezoTreasureChest`
(`$39`) and `Obj_MotaTreasureChest` (`$3A`). Their art is already in the
level's extra-art list (Shure's has both, at tiles `$48A` and `$49D`).
`extract_npcs.py` now also writes `assets/data/chests.json`: both
objects' tile word, mappings and hit box, and
`TreasureChestContentArray` as `{"index", "item" | "meseta" |
"garbage"}` -- edit a chest's contents there.

A chest is an object in the same slots as the people, so the one hit
test covers it: it stops the party and the people, and A finds it.
`$2C` picks the object's own box out of `loc_7278`, which the port now
carries whole -- a person is row 4 (16x16), the Motavian chest row `$D`
(32 wide, 16 tall), the Dezolian one row 4. Before build 45 every
target was taken to be 16x16. Each frame the chest shows frame 1 once
`treasure_chest_flags[index] & $7F` is set (`loc_5656`; type 0 reads
`event_flags` instead). The Motavian chest has a second set of mappings,
8px up and left, used on level type 1.

`ObjType_TreasureChests` (21386), on A:

| Flag | Content | Result |
|---|---|---|
| index 0, or flag set without bit 7 | | `$1706`, nothing inside |
| bit 7 (locked) | non-zero | `$1707`, locked |
| bit 7, or 0 | 0 | flag = 1, `$1705`, garbage |
| 0 | meseta | added, flag = 1, `$1701` naming the first member standing |
| 0 | item | the first member standing with a free slot gets it, flag + 1, `$1702` then `$1703`; with nobody, `$1702` then `$1704` and the chest stays shut |

Taking something plays `SXFID_ItemReceived`, which overwrites the
selection click in the same `sound_queue` byte, so only the one is
heard. `AddToMoneyOwned` compares the *amount added* with 99,999,999,
not the total; a chest's amount is a word, so the port leaves the cap
out.

A new game locks chests `$10`-`$13` (`move.l #$80808080, $FFFFC790`):
four of Shure's ground-floor five. The Small Key (`ItemAction_SmallKey`)
facing a chest with bit 7 clears it: "uses", then "The container
opens." The item menu reaches the map through `WinSys.facing_object`,
which game.c points at the same hit test.

`LoadSpritesInLevel` adds three chests after the table: the Esper
mansion's (`$20E` while `$C78E` is clear, `$20F` while `$C78F` is --
written to one slot, so the second wins while both are shut) and Noah's
(`$035`, an event rather than a chest, while `$C745` is clear). A on an
event object's number other than 0, 3 or `$11` (the dead bodies below)
is not ported and says so on stderr (`$35`, Noah's box, is ported in
build 63; below).

### Darum and Teim (build 49)

The tube (level `$E`, `SpriteData_MotaPassageDarum`) places two objects
with interaction type 3: Teim (`$2A`) in slot 0 and Darum (`$29`) in
slot 1, both index 1. Teim's init deletes her at once in the tube unless
her `$24` is set, which the placed one's is not; Darum's deletes him once
`$C715` is set. Until then he stands at `$C8,$98` with hit-box row 8 of
`loc_7278` (64 x 96), which is what closes the tube. A on him is
`ObjType_TeimDarum` 1: "I've seen that face! ..." in the big box.

No button is needed, though (build 58). `CharSprites_ChkMoveLeft`
(ps2.asm:5290) is the one direction that looks at what stopped it: when
a step left is blocked by an object whose `$2E` type is 3, it writes
`$40` to the pad word (`move.w #$40, (joypad_held).w` -- nothing in
`joypad_held`, A in `joypad_pressed`) and skips the right test. The menu
then sees no C, and `ProcessAButtonPress`, later the same frame, finds
A pressed and Darum to the left. So walking into him from the east
starts the line by itself, and does again each time the left push
resumes after the box closes. Up, down and right have no such check. In
the port `walk_tick` sets `Walk.bump_a` (it does not own the pad) and
the level screen rewrites that tick's input copy the same way, leaving
the Escape key alone.

Entering the tube with Teim in anyone's bag (`LoadSpritesInLevel`'s
tail) takes her out of the bag and starts the scene: `$DE70` = 1,
`$DE72` = 0, `$DE6E` = `$302`, demo walk 2 (`Demo_TeimScene`) and "A
Prologue" (`$8F`). `ObjType_TeimDarum` 2 then runs one step per idle
visit of `ProcessAButtonPress`, `$DE70` naming the step:

| Step | What | Moves on when |
|---|---|---|
| 1 | the walk in | the demo's `$FE` |
| 2 | "What's Teim doing?" | the text is read |
| 3 | `$1803`; Teim put in slot 0 a cell left of the leader, `$24` = 1 | read |
| 4 | Teim's routine 1: fifty-six frames walking left | her walk ends |
| 6 | `$1804`; Darum's routine 1: forty frames walking right | read, and he has stopped |
| 7 | Darum's routine 2, then his swing, a frame every eleven | the swing ends |
| 9 | `$1805` | read |
| 10 | Teim, struck on his frame 3 and fallen (frames 4-9), is let go: `$6E` frames, then the burst (`$2B`, `SXFID_Explosion`) where he stood and she is gone -- and he with her | her routine 6 ends |
| 11 | the box shut, demo walk 3 (`Demo_AfterTeimDeath`) | its `$FE` |
| 12 | `$1806` | read |
| 13 | `$C715` = 1, demo off, then `CloseAllWindows` | |

The objects step `$DE70` themselves (Teim at the end of her walk and on
leaving, Darum at the end of his swing) and read each other by slot
(`$FFFFE800`, `$E840`, `$E880`), so `NpcSet` now keeps every
`LevelSpriteData` slot, the ones it does not run as free entries.
`extract_npcs.py` writes the three objects' tiles, mappings and boxes as
`assets/data/scene_objects.json`; without it the tube is empty and the
game says so on stderr. Demo byte `$FE` (`loc_37EC`) is ported: the pad
back and `$DE70` + 1. `ObjType_TeimDarum` 3 (message 6) is in; index 0
is not.

`PS2_ATRACE=1` (build 56) prints every A press on the level screen on
stderr: the leader's position and facing; the probe box, row 0-3 of
`loc_7278` about him; every object slot with its object id, `$2E`,
position, box row, velocity and whether it is on screen and overlaps
the probe; and which way `ProcessAButtonPress` went -- ignored (windows
busy, a demo walk, a level event already running), nothing
there, a moving target, or the object it dispatched on. Each visit to
`ObjType_TeimDarum` prints its event, `d2` and `$DE72`.

### The dead scoundrels in Shure (build 53)

`SpriteData_ShureF3` places the three bodies as raw `$6C` bytes rather
than the `(ObjID_...-$14)<<2` expression the other entries use: object
`$2F`, `Obj_DeadBody`, with interaction type 0, so they fell into the
"event objects not spawned" gap and the top floor was empty.

| Slot | Event | x, y |
|---|---|---|
| 1 | `$11` | `$158`, `$238` |
| 2 | `$03` | `$398`, `$218` |
| 3 | `$03` | `$528`, `$258` |

The object (ps2.asm:7583) is an init and nothing else: tile word `$2400`
(line 1, tile `$400`, the first entry of Shure's extra-art list 7,
`loc_5B452`, `sprite_dead_body.png` -- build 97b corrected this from
`loc_5AE9C`, the explosions' art, which is the tube and lab lists'), mappings `loc_5B5AC` (one 32x16 piece at -16,-16) and hit
box row 6 of `loc_7278`. It checks no flag and never deletes itself, so
a body stays after its pocket is empty. `extract_npcs.py` writes it into
`scene_objects.json` as `DeadBody`, and `npc.c` runs it as a scene
object.

A on a body is `ObjType_Events` by its index byte, with `a0` =
`event_flags` + the number, run as `ObjType_TeimDarum` is: the first
visit from the A press, then one per idle visit with `$DE70` as d2 and
`CloseAllWindows` once `$DE72` is set.

- **`$03`** (`loc_DA8C`): `$170B`, "Those are bodies of the scoundrels.
  I wonder if the Biohazards killed them?", and the box closes.
- **`$11`** (`loc_DBB2`), counting in `$C721`: `$170B`, then `$170C`
  ("Oh, my! There is something inside the pocket."), then the Small Key,
  then the Letter. Each item goes through `loc_E84A`, now
  `level_give_item`, which the chests share: the first member standing
  with a free slot, `$1702` then `$1703` and the flag + 1, or `$1702`
  then `$1704` with nobody. A key nobody can take closes the box with the
  flag at 0, so the whole search replays; a Letter nobody can take leaves
  it at 1, and the next search offers only the Letter. At 2, `$170B`
  alone.

`$C721` is in the `$C700` block, which the save carries whole.

### Pandora's box on Noah (build 63)

`LoadSpritesInLevel` puts one more `$39` object on NoahGroundF (`$63`) at
`$210`,`$D8` while `$C745` is clear, with `$2E` = `$035`: an event
object, `ObjType_Events` `$35` (`loc_E0BE`, ps2.asm:20584). A on it is
`$171A`, "What is this? I feel the tremendous strength of dark power!",
and `$DE70` + 1. On the next idle visit it writes 1 to its flag
(`$C745`), `$B00` to `event_flags`, the leader's cell to
`level_x/y_pos`, and raises `screen_changed_flag` -- without closing
the message window. `LevelScreen` runs again, fade and all, and the box
is not placed this time.

`loc_7E02`, called after `LoadSpritesInLevel`, takes the high byte of
`event_flags` as a one-shot case. `$B` (`loc_7FF2`) clears `$FFFFE400`
-- there is no party object, so nobody is drawn or walks, and
`LevelScreen` spawns no followers, which it only does when that slot is
filled -- and sets `$DE70` = 1, `$DE6E` = `$36` and `event_flags` =
`$8000`. `ProcessAButtonPress` runs `$DE70` events before it looks at
the pad or `event_flags`, so `$36` (`loc_E0F8`) starts with no button:
`$1910` in the big box ("Ha,ha,ha,ha,ha! This is Pandora's box! ...",
a timed message), then `enemy_data_buffer` = `$102` and
`ScreenID_Battle`: Dark Force, with no enemy boxes (`$102` and up skip
them). `ProcessPlayerMenu` is shut throughout by `$DE70`. The level
after the fight is an ordinary load, the party back.

`loc_7D08`'s `bclr #7, (event_flags)` finds bit 15 set on that entry,
clears it and skips `loc_9002`, the full redraw of both planes. On the
Mega Drive the nametables therefore keep what the last frame before the
reload drew -- the same room at much the same camera, and `$171A`'s box,
which was never closed -- until the big box opens over it. The port
draws its planes from the map every frame and cannot keep a stale one,
so it fades back in on the clean room. Worth a look in the emulator: if
the old box shows through the fade, or the room's edges are wrong where
the camera recentred, that is the ROM.

In the port `loc_7E02`'s cases are `g_level_oneshot` (cases 1-7, `$A`-`$C`,
and since build 92 Mother Brain's arrival `$E` and aftermath `$F`; another
non-zero one says so on stderr), `g_party_gone` is `$E400` being empty,
and an event asks the level loop for a reload or a battle through
`g_event_reload` / `g_event_battle`, acted on as soon as the event
returns. Route `noah_box`.

### The jet scooter (build 64)

`$C716`, event flag 6, says whether there is one: 0 none, 1 given, 2
ridden once. The gift is the Mota person with dialogue `$87` in RoronF5
(`loc_E690`: `$1685`, `$C716` = 1, parked at x `$590`, y `$730`); that
person is object `$1B`, which is not ported yet, so for now a save has to
set the flag. `jet_scooter_flag` (`$C652`) says whether the party is on
it, and `$C656`/`$C654` are where it is parked, a cell corner -- the save
file names them `location.on_jet_scooter` and
`location.jet_scooter_parked`.

**Parked.** `SpriteData_MotaWorldMap` places object `$22` first, and
LoadSpritesInLevel's tail puts it back in slot 0 at the parking place,
`$24` = `$2A` = 6, whenever `$C716` is set and event_flags' high byte is
clear. At `$C716` = 2, entering Paseo, Arima, Oputa, Zema, Kueri or Piata
moves the parking place to just outside that town. `ObjJetSctr_Init`
(ps2.asm:6780) runs every frame while it is parked: tile `$400` (Motavia's
extra-art list 10, `loc_5D98E`), `Map_JetScooter`, hit box row 5 (32 x
32), `$2E` = 6 -- an event object -- and it deletes itself while
`$C716` is clear. A on it is ObjType_Events 6 (`loc_DAA4`): `$171C`,
"Let's get on the jet scooter!", after `$1686` (the note) the first time,
which also moves `$C716` to 2. The next idle visit turns the scooter to
the leader's facing, sets `jet_scooter_flag` and closes the box.

**Riding.** On level 0 with the flag set, Obj_CharactersInLevel and
Obj_FollowingChars set bit 1 of 2(a0) and return: hidden, never in the
display list, so nothing can bump into them, and nothing else of theirs
runs. RenderCharSprites draws nobody. The scooter's init, seeing the flag,
points `$F72E` at `loc_29E16`, moves to its main routine, takes hit box 0
and queues `$D8` (special SFX 0, the engine). `ObjJetSctr_Main`:

- reads the pad only between steps with no window open; a step under way
  finishes regardless;
- tries up, down, left, right, the first clear one winning, a blocked one
  still turning it. The scooter covers the 2 x 2 cells up and left of its
  origin, so each direction probes the pair beyond that edge -- up (-32,
  0) and (-32, -16), down (+16, 0) and (+16, -16), left (0, -32) and (-16,
  -32), right (0, +16) and (-16, +16), as (y, x) -- through
  `JetScooter_ChkCanMove`: solid, then special, then `loc_6E4E`. No
  Level_ChkTargetInteract: people do not stop it;
- steps 2px a frame for 8 frames, twice walking speed, setting `$CB0A`;
- moves the camera 2px a frame when its place on screen last frame is
  past `$E0` up, `$100` down, `$100` left or `$140` right;
- hovers: frame = facing + (`$32` & 1), `$32` on every 4 frames, always;
- copies its position and facing to the leader (`loc_4AE2`) before
  RunObject adds this frame's velocity.

That last is load-bearing. Everything that reads the leader reads the
copy: `loc_6F1C`'s return position, BattleScreen's `level_x/y_pos`, and
ProcessRandomBattle's gate, which on the scooter (`loc_11700`) wants the
copy on a cell corner. On a step's first frame the copy is still on the
cell being left, so the roll comes as the step begins, and a fight leaves
the party on that cell: the step never happens. On foot the leader has
moved a pixel by then and the roll waits for the step's end.

`loc_29E16` is open over water and solid on land; the dam entrances
`$0E`, `$11`, `$31` and `$55` are solid and special, which is how the
scooter reaches Motavia's four dam records. In the dam the flag is still
set, but only level 0 looks at it: the party walks. Leaving again,
LevelScreen (ps2.asm:11375) parks the scooter on the arrival cell and
they are riding once more; so after a fight on the water. The Escapipe
and Telepipe, the Governor, the clone lab and the return to Paseo clear
the flag.

**The music on arriving riding** (build 139f). Such an arrival queues two
sounds into the one-byte `sound_queue`: LoadLevelData the level's music
(through UpdateSoundQueue, when it is not the latch's), then the
scooter's init its engine, `$D8`, when RunObjects first runs it. In the
ROM the music is started between the two: LevelScreen loads with the
vertical blank on, VBlankExit runs SoundDriverInput every field, and the
art loads after UpdateSoundQueue take fields. The port loads in one tick,
so until 139f the engine overwrote the music -- after a fight on the water
the victory music played on, and out of a dam the dam's -- until
enter_level_walk let the driver take the level's music at once
(`audio_take_queue`) whenever it queued it. Route `scooter_music`.

**Getting off.** A with nothing ahead is ObjType_Events 0 (`loc_D9AA`),
`$1708` anywhere else. Riding on Motavia it moves the leader's copy by
`JetScooter_CharOffPosOffsets` for the scooter's facing (up -32 y, down
+16 y, left -32 x, right +16 x) and asks the walking table (`loc_29B64`,
the solid bit only) about the scooter plus that offset. Solid: `$171E`,
"We can't get off here!", and the box closes. Open: `$171D` and `$E1`,
which stops the engine; the next idle visit (`loc_DA26`) parks the
scooter on its own cell, clears the flag, sets every party member's `$22`
to 0 and the scooter's too. ObjCL_Init then runs again on the offset
copy: 8 in from the cell corner, `$2A` = `$24` -- and nothing touched the
leader's `$24` while he rode, so he faces the way he did when he got on
(down after a ride that began at a level entry, LevelScreen's `$24` = 3),
not the way the scooter does. The followers land on him from wherever
they were hidden.

In the port the scooter is a scene object in slot 0 (`npc.c`, its init
and RunObject's tail) whose main routine is `walk.c`'s, run by
`walk_tick` while `Walk.riding`; `Walk.x`/`y`/`facing` are the leader's
copy. `tools/extract_collision.py` writes the table as
`assets/levels/jet_scooter.collision.txt` and `tools/extract_npcs.py` the
object as `JetScooter` in `scene_objects.json`: `make extract-collision
extract-npcs`, then `make manifest`.

Worth a look in the emulator:

- A pressed mid-step. ProcessAButtonPress does not look at the leader's
  motion, and the step goes on copying the scooter's position over the
  leader's while the box is up, which undoes the get-off offset: the
  party should come off onto the scooter itself, two pixels short of its
  last position.
- The leader's facing after getting off, as above.
- The encounter on a step's first frame, as above.

### The level screen with the party on it

`LevelScreen` spawns `ObjID_CharactersLevel` whenever `controls_locked`
is clear; the attract crawl is the case where it is set. `demo_flag` does
not lock anything -- `ObjCL_Main` just reads its pad byte from
`DemoScriptPtrs[demo_index]` instead of `joypad_held`, one byte each time
the character is standing still. `$FF` ends the script by switching to
`ScreenID_Building`.

Movement is on a 16px grid (`game/walk.[ch]`). A direction is read only
at rest; a step sets a one-pixel velocity and a 15-frame counter, and
`RunObject` adds the velocity on every frame including the first, so a
step is 16 frames. A blocked direction still turns the character and the
test falls through up, down, left, right. The walk cycle is frames
0,1,0,2 of the facing, seven frames each, and the right-facing frames are
the left ones mirrored.

Collision is per 16x16 quadrant of a 32x32 block: the level record points
at a byte per block, low nibble solid, high nibble special, and the map it
indexes is `$FFFFF732`: plane A, or plane B when bits 4-6 of the
collision pointer's flag byte are set (`LoadLevelData`, 13310). Every
dungeon sets them, because its plane A is the backdrop overlay (pipes
and the like, drawn over the party and never solid). `loc_6F56` uses
plane B for level type 2 regardless. `tools/extract_collision.py` writes these as
`assets/levels/<stem>.collision.txt`, four `#`/`.` per nibble in the
order top-left, top-right, bottom-left, bottom-right.

**Two cameras (build 46).** `$FFFFF71C/$F71E` is the main camera: the
sprites, collision and plane B go by it. Plane A has its own,
`$FFFFF718/$F71A`, and `loc_8BBA` moves it right after the main one, by
the same step -- or by twice the step when bit 7 of the level's
extra-art byte (`$FFFFF73C`) is set. Every level whose plane A is a
backdrop sets that bit, and no other level does: the overhead pipes and
girders are nearer the viewer than the floor, so they slide past two
pixels to its one. Over a backdrop, plane A's camera starts at 0,0
whatever the arrival point (`loc_91F2`), wraps in the backdrop's 512 x
256 instead of clamping, and holds still on a frame the main camera is
held at an edge. `ProcessWindows` places windows from plane A's camera
too. Up to build 45 the port scrolled both planes with the main camera,
so the overhead layer moved in lockstep with the floor. The bit is the
`extra_art` property in each level's `.tmx`; a `.tmx` extracted before
build 46 lacks it, and `level_load` then takes the byte from the level's
`.json`, which has always carried it. `PS2_LEVELINFO=1` prints, on each
level entry, the flag byte, the extra-art byte, and the parallax, backdrop
and collision plane they produced.

**Tile animation (build 51).** `loc_118F4`, in the level screen's VBlank
(`VBlankSub_10`), copies a frame of raw 4bpp art over a run of VRAM tiles
every 32 fields: on Motavia (level `$00`) `loc_760D8`, two frames of 8
tiles, to tile 1 -- the sea; in the Underwater Passage (level `$13`)
`loc_762D8`, three frames of 16 tiles played 0 1 2 1, to tile `$10E`.
`$F63C`/`$F63E` are zeroed on every `LevelScreen` entry, so the first
frame goes up on the first field. It runs through the damage floor's
wait (VBlank `$10` too) and stops while paused (VBlank `$20`).
`extract_levels.py` writes the frames as `assets/tiles/anim_<label>_<n>.png`
and where and when in `assets/data/level_anims.json`; the constants come
from the routine, which is code rather than a table, and are listed at
the top of `write_tile_anims`.

**Palette animation (build 47).** `loc_6430`, every frame of
`LevelScreenLoop`, writes the displayed palette by level type: type 5
(Shure and the other type-5 dungeons, the tubes) pulses entries
`$2E-$2F`, ramps `$3C` and swaps `$3D-$3E` every 8 frames, with extra
steps for BiosystemsLab B1 and Gaira's alarm; the towns before Aukba
flicker `$3C-$3D`; Uzo rotates `$2B-$2D` every 16; Noah on glows `$33`,
`$38`, `$3A`. `$F632`/`$F634` are not reset on entering a level, so the
phase on arrival carries over. The `$F656` branch in Noah (set by Mother
Brain's palette effect) is not carried.

A plane-B backdrop (Gaira, Noah) is never pointed at by any camera:
`ClearSpriteAndScroll` zeroes it on entry and `LevelScreenLoop` drifts
it a pixel a frame -- down and left while `$C73E` is set in Gaira, up
always in Noah -- whether or not anyone is walking.

The camera moves a pixel a frame, only during a step, and only when the
character is past a threshold on screen and facing that way: `$C8` up,
`$118` down, `$D8` left, `$168` right, in sprite space. Bits 4 and 6 of a
demo byte force it up or left regardless. It wraps with the map when the
level's flag nibble is zero and stops at the edges otherwise.

**Dungeon floors** (`loc_6F56`, build 44). A special quadrant
underfoot is looked up by level type and block id before the
interaction list is:

| Type | Block | What |
|---|---|---|
| 2 | `$53`, `$72` | a town's edge (below) |
| 3 | `$63` | damage floor |
| 3 | `$50`-`$62` in levels `$15`/`$16` | Crevice's holes, to fixed spots |
| 5 | `$1A`, `$46`, `$77`, `$78` | damage floor |
| 5 | `$3A` | hole |
| 6 | `$04` | hole |
| 6 | `$33` | the approach to Mother Brain (`loc_70D6`, build 92) |

Anything else goes to `loc_6E4E`, which is where stairs are. A hole
(`loc_7082`) reads `loc_718A`, three signed bytes by `level_index -
$4E` clamped at 0: every level before Ikuto B4 falls to the previous
entry in `LevelData` at the same spot -- Shure F1 to Shure's ground
floor, and so on -- and the Ikuto/Guaron entries carry their own level
and block offsets. It queues `SXFID_FellInHole` (`$A5`) rather than
`SXFID_LevelChanged`. Before build 44 all of these fell through to the
interaction list, found nothing, and took the dungeon's way out -- which
is why a hole in Shure put the party outside.

A damage floor (`loc_7108`) bites only once per step onto it: `$30(a0)`
is set at `loc_38FC` and cleared here. The whole displayed palette goes
to `$EEE` for one field with `SXFID_DangerousFloor` (`$B0`), is put back
from `$FB80`, and seven more `WaitForVBlank`s follow -- all inside
`ObjCL_Main`, so the level loop stops for eight fields; the port holds
`tick_level_walk` for eight ticks. Everyone still standing loses a
point of HP. Whoever drops to 0 (the last, in party order) sets
`$FFFFDE70` = 1 and `$FFFFDE6E` = `$30`, which `ProcessAButtonPress`
runs as `ObjType_Events` `$30` (`loc_DFFE`) without any button: script
`$1721`, then `ScreenID_Sega` if nobody is left, then `CloseAllWindows`.

(build 157b) It all happens inside `ObjCL_Main`, the leader's object, so
the rest of `LevelScreenLoop`'s iteration -- the other objects,
`ProcessAButtonPress` (which opens `$1721`), `ProcessRandomBattle`, the
colour cycles of `loc_6430` -- runs after the eight fields, and so does
the HP (`loc_7162`). The port's white field is the flash's own tick; the
HP and the rest run on the stall's last tick (`level_floor_hp`,
`level_walk_rest`). Until 157b they ran in the flash's tick: `loc_6430`
wrote its entries over the white on the palette path, and its step was
put back by `loc_7140` instead of shown at the stall's end. Route
damage_floor_field.

(build 157c) `ScreenID_Sega` there is the game over: the level loop's
`loc_7DEC`, with the controls unlocked, branches to `GameOverScreen` when
it finds it ("branch if all characters are dead") -- the game over music,
tiles 0-`$5BF` erased over 32 frames under the level, the party and the
`$1721` window as they stood (VBlank `$24` sends no party art, so their
tiles go too), PaletteFadeFrom, the message, then the SEGA screen. Until
157c the port went straight to the SEGA screen. The same build keeps the
windows through the battle's game over erase as well (they went at its
first frame). Routes map_wipeout, gameover_to_sega.

Leaving a place is two tables (`tools/extract_interactions.py`):

- Each level's interaction list, `<stem>.doors.json`: a cell, and either
  a level and arrival cell or a building and portrait. (Build 131) The ROM
  gives a list no length, only the `$FF` that ends it; the longest is
  ControlTowerGroundF's, 70 entries. `WALK_DOORS_MAX` is 128 (it was 64,
  which lost that floor's last six stairs up: route
  `tower_ground_stairs`), and a file with more says so on stderr. It is consulted
  when a step is blocked by a quadrant that is solid and special (in a
  town, only walking up -- doors are entered from below) and, through
  `loc_6F56`, when the cell underfoot is special but not solid (the
  overworld's town entrances).
- `exits.json`, `loc_28546`: one arrival per level from index 4. A town's
  edge is plane-B blocks `$53`/`$72` with their special bits set, and
  standing on one uses this table. Plane B, not A: `loc_6F56` reads the
  map at `$FFFF9000` for level type 2 only.

Without the exits the town simply ended in empty map, the character
walked off it and wrapped, and the camera -- clamped at the edge while
the character was on the far side -- saw him past the follow threshold
and scrolled after him while he was off screen.

**Which door records can fire (build 63).** An entry is read only by
`loc_6E4E` (ps2.asm:10190): an entry at (y, x) matches a probe cell
(py, px) when py <= y <= py + 2 and px <= x <= px + 2, so each entry
answers a 3 x 3 patch of cells up and left of it. Three things probe:
a step blocked by a quadrant solid and special (`loc_6DC4`: the leader
only, and in a town only walking up), the same on the jet scooter
(`JetScooter_ChkCanMove`), and a special quadrant underfoot once
`loc_6F56`'s level-type cases have passed on it. The attributes come from
`$FFFFF72E` and the map from `$FFFFF732`. Nothing but LoadLevelData
writes `$F732` or the list pointer `$F736`; `$F72E` is also written by
the scooter (`ObjJetSctr_Init` and `loc_D9AA` point it at `loc_29E16`,
the scooter's own table, on Motavia); and nothing but LoadLevelData's
tail (below) writes into the block maps after they are decompressed.

`python3 tools/door_audit.py PS2_ASM -v` runs every one of the 867
records against that, with every map patch applied. 19 can never fire,
in any state of the game, and none of them is a port bug:

- The fifteen same-level links in the four dams, Biosystems Lab ground
  floor and F2, and Nido (NidoGroundF 45,52 to 43,52 and so on). Each
  sits on the door blocks `$7E`/`$7D`, which carry no special bit in any
  collision table (`$04` and `$0C` in the type-5 table). The way through
  is the open lower-right quadrant of `$7E`, and the door is an object:
  Yellow Dam places `Obj_OpenableDoor` (`$3B`) at 280,447, cell 17,27,
  on that gap, and `loc_5750` clears its hit box once it is open. The
  records look like the teleport doors the objects replaced.
- Gaira's and Gaira_Copy's two to NavalF1: the three levels share one
  list, `loc_296D0`, and these are NavalGroundF's stairs, live there. In
  Gaira the cells they answer are floor or damage floor, which
  `loc_7040` sends to `loc_7108` first.

Eight more fire only one way: Motavia's four dam entrances on the
scooter (two scooter probes each, into `$31`, `$11`, `$55`, `$0E`; the
walking table has nothing there), and Dezolis's Menobe, Ikuto, Guaron
and Naval with the Prism. The scooter is ported in build 64 (below).

**Block maps written after loading (build 63).** LoadLevelData ends in
`loc_9258`-`loc_9300` (ps2.asm:13389), which writes into the maps it has
just decompressed:

| Level | When | Plane | What |
|---|---|---|---|
| `$00` Motavia | `$C735` | B | every `$8B` becomes `$89`; `$89` at four places |
| `$00` Motavia | `$C735` and `$C72F`, `$C733`, `$C731`, `$C72D` | B | one `loc_9302` run list each (`loc_17C94`, `17CDA`, `17D4A`, `17DA2`) |
| `$03` Dezolis | anyone in the party holds the Prism | B | block `$63` at `$096`, `$0EC`, `$7B9`, `$ED0` |
| `$64` NoahF1 | `$C747` | A | block `$28` at `$1B0` |

The dam lists only ever apply under `$C735` too: `loc_92CA`, where a
clear `$C735` branches, is the routine's `rts`. A run entry is row,
column, n, at row x `$60` + column: n with bit 7 set is the block written
at that one place, otherwise n & `$3F` blocks of `$87`, across, or down
when bit 6 is set. With no flags the Motavian channels are dry; with the
dam flags they fill. Motavia collides on plane A, so its patches are
only seen.

Build 97c read the patch routine (`loc_9258`) and its run writer
(`loc_9302`) in full and checked the port against them: the four dam
tables entry for entry (23, 37, 29 and 29), the order (blue `$C72F`, red
`$C733`, yellow `$C731`, green `$C72D`), the whole-map change under
`$C735` alone -- every `$8B`, dry orange ground, becomes `$89`, the sea
around the islands, and four more cells with it -- and the run decoding.
No difference. Route `motavia_dam_patches` shows one place three times:
no flags, `$C735` alone, and the four panels too (the black canals fill).

Dezolis is the one that matters. Its collision plane is B (flag byte
`$18`), and `$63` is `$FF` in its table -- solid and special in all four
quadrants -- so the Prism puts four doors on open ground, one under each
of the entries to Menobe (5,45), Ikuto (7,89), Guaron (61,115) and Naval
(119,33). Without the Prism those four entries cannot match. The Prism
test is `CheckItemExistInventory` (19067): every party member's sixteen
slots, whatever the count says, masked `$7F`; it leaves
`character_index` on the member it stopped at, and so does the port.
Up to build 62 the port did none of this, and with the Prism the four
dungeons stayed shut.

`extract_levels.py` writes the table as `assets/data/map_patches.json`
-- the run lists from their labels, the rest from the routine's code,
noted in the extractor -- and `load_level` in `game.c` applies it on
every load, the attract crawl's included, as LoadLevelData does. A flag
is named `"$C7xx"` and tested as a byte; `"item"` is an item id. Without
the file nothing is patched, and the game says so once.

`RenderCharSprites` does not keep a character's art in VRAM: it copies
the current frame's eight tiles to `$300 + 8 * slot` every field, out of
one block (`CharWalkArt.png`, 72 tiles a character) that `LevelScreen`
decompresses once.


### The one-shot cases at a level load (build 97e)

The one-shot dispatch (loc_7E02) takes the event word's high byte at
every level load: cases 1-7 the death of Nei and Climatrol (build 78), $A
the Gaira prison, $B Pandora's box, $C Shir gone, $D the Esper B1 hold, $E
and $F Mother Brain's arrival and aftermath, $10 the opening; 8 and 9 do
nothing and nothing sets them; past $10 the word is cleared. While the
level runs the high byte is the next load's case, or the hold, and the C
menu stays shut while it is not 0 -- the port reads that from its own
state (`MenuState.event_word_high`), not the save's `$C710`. The table and
where each case comes from: docs/history.md, Build 97e. Routes
esper_b1_hold, climatrol_motavia_menu, oneshot_hold, oneshot_clear.

### The rocky mountain on Motavia (build 97c)

ObjType_Events `$2B` (`loc_DF24`, ps2.asm:20444) is an event block on Uzo's
island at 2768,1544 (cell 173,96). From the cell below it (173,97) a step
up is Uzo's entrance -- the door record's window (`loc_6E4E`) catches any
target within two cells -- so the event is reached one cell to the left:
at 172,97 the mountain turns the party without a step, and A there hits the
block. It loads `$171F` into the small box, sets `$DE72` (the next button
closes the box) and runs `loc_DDA2` for `{char}`. The script's first page
ends in `$C4`, and LoadScript ends a script at any code `$C4` or above
(`loc_11268`), so the game shows only "There is a high rocky mountain in
front of"; the rest, "{char} and the others! Is this Uzo mountain?", is
never loaded (the disassembly marks the `$C4` "this should be `$C3`" and
the rest "cut-off text"). The port does the same -- ROM behaviour. Route
`mount_uzo`.

### Mother Brain (build 92)

The approach, the arrival scene, the confrontation and her battle: see
docs/history.md, Build 92, which has the whole sequence by name. In short,
NoahF1's corridor floor north of cell 33 (the approach, `loc_70D6`) reloads
the floor with the arrival scene pending (`loc_7E02` case `$E`): the party one
cell down, Mother Brain's image (object `$2C`) forming between two energy
arcs (object `$45`), then the confrontation (level event `$37`) -- two
YES/NOs, of which only YES then NO fights. Her glow on the level is palette
entry `$2D` through `loc_66C0` while the confrontation runs. The *mother
brain fought* flag (`$C747`, `FLAG_MOTHER_BRAIN_FOUGHT`) is set as the battle
starts; it stops the approach and makes LoadLevelData's tail put block `$28`
over her place on NoahF1. Routes mother_brain_no, mother_brain_return,
mother_brain_battle.

### After Mother Brain (build 93)

The aftermath, Lutz's warning, the Earthmen and the ending screen's frame:
see docs/history.md, Build 93. The aftermath restores everyone's HP and
shows Lutz's revival message only if someone was dead -- otherwise its box
opens empty for a moment, as the ROM's does. The two scripted walks step the
events with their stop bytes; the first is resumed past its stop rather
than restarted. The Earthmen's last step stands the six members other than
Rolf and Nei on the bridge (the party's own followers among them). The
ending screen fades from white and waits for a button (build 93; the rest
in 93a, below). Route mother_brain_won.

The Earthmen's leader (object `$28`, `loc_4DD8`; build 97d) stands at the
far end of the corridor, NoahF1 33,7 -- the only object on the level's
sprite list, placed at every load whatever the flags. The walk to the
Earthmen stops two cells below him, so he is the one the party faces for
"Welcome to the spaceship Noah". His routine is an init only, run every
frame: frame 0 of three (the other two are never shown), hit-box row 4 --
he blocks the corridor and never moves -- and his `$2E` is 0, so A at him
is event 0, "nothing unusual here" (`$1708`). Art
`assets/tiles/sprite_earthman_leader.png`. Route earthman_leader.

### The ending screen (build 93a)

`src/game/ending.[ch]`; docs/history.md, Build 93a, has it frame by frame.
"The Place of Death", the fade to white and back, then six epilogue pages
-- a portrait slid in on plane A, its palette, its text typed beside it
for 720 frames with the controls locked, cleared, 60 frames -- on a
starfield whose palette line stays black. The strip wipe opens the last
picture from its middle rows; the fade to white brings the stars' palette,
and "I wonder what the people will see in the final days?" is typed over
them. The credits scroll under "Never Dream" a pixel every three frames,
written a row at a time below the screen; 3600 steps in, the planet's rim
lights up a row of the ramp every eighth step, and the ramp's last row
starts the ending UFO, which is deleted on the step the credits stop.
(build 136e) The credits themselves are text,
`assets/script/ending_credits.txt` (README, "Asset formats").
A, B or C returns to the Sega screen. The planes are 64x64 for the whole
screen. Routes ending, gameover_message (the game over's font, cleared of
colour 11 the same way).

(build 157a) The button puts the planes back to 64x32 (`$9001`) and sets
ScreenID_Sega, and SegaScreen, like every way to it but the console's
reset, sends `$E0` (the music's fade) and then darkens what is on screen
for 21 frames (PaletteFadeFrom) before its setup: the credits fade out,
the VDP reading plane A's first 32 rows by then (THE END stays where it
was). Game over's end goes the same way, its message and picture fading
out. Until 157a both cut straight to the logo's setup, and the `$E0` of
every way there went after the fade instead of before it
(`goto_sega`). Routes ending, gameover_to_sega.

