# Build history

One section a build, newest last (builds 67-81 moved verbatim from the README in build 81c). Each build appends its own here and updates the subsystem doc it touched.

## Build 67: the doors, and grinding for gear

**The doors.** `Obj_OpenableDoor` (`$3B`, ps2.asm:7915) and
`Obj_DestroyableDoor` (`$3C`, 7961) are spawned (npc.c, `door_tick`),
with their art and mappings from `tools/extract_npcs.py` (rerun `make
extract-npcs`). Both inits set tile word `$44B7`, mappings `loc_5D4F8`
(shut, ajar, open, blown) and hit-box row 6, then look at
`event_flags[$2F(a0)]`: set, the door is placed open (`$24` = 2) or
blown (3) in routine 3; clear, routine 1, which does nothing. Every
placed door is slot 0 on its level -- Nido, the Biosystems lab's ground
floor and F2, the tube, the four dams, Control Tower F1 -- which is
what the demo's `$FD` byte relies on: `loc_37FE` queues
`SXFID_DoorOpen` and writes 2 to `$FFFFE822`, slot 0's routine. `$3B`'s
routine 2 steps `$24` every `$F` ticks to 2; `$3C`'s drops the burst
(`$2B`, already ported for the Teim scene) into the first free slot on
its own x and y and shows frame 3. Routine 3 clears `$2C` and `$2E`
every frame, so nothing bumps into or talks to the door again.

`ItemAction_Dynamite` (14336) and `ItemAction_KeyTube` (14360) are
menu.c's `use_dynamite` / `use_key_tube`: the faced object (`loc_9C12`
through `WinSys.facing_object`) must be an event object with index
`$18`-`$1A` (`$1B` for the tube); bit 0 of its flag is set -- already
set, and the item is only looked at -- then `$0001` or `$0018`,
`MenuState.start_demo` = 4 or 5, and the item leaves the bag. game.c
starts the walk; walk.c reads it only once the menu is shut, as
`ObjCL_Main` does. `Demo_DynamiteUsed` is three idle reads, down three,
right, up, idle, `$FD`; `Demo_KeyUsed` seven idle reads and `$FD`. A on
a door is `EventType_DialogueIndex`'s one line (`level_door_line`):
`$170D` for the dynamite doors, `$170E` for the tube's and Control
Tower F1's, `$171B` for the dams.

New routes: `nido_door` (the party stopped on 53,46, `$170D`),
`nido_dynamite` (USE, the walk back to 54,48, the burst, through to
53,44), `tube_key_tube` (`$170E`, `$0018`, the door sliding open, up
through it). `routes/gaps/` is gone. The route language gains `give
ITEM [MEMBER]`: an `ItemID_` name into a party position's bag.

**Grinding for gear.** Build 66's story reached Shure by raising the
party with `party 3 10`. That is not how the game is played: it needs
money and EXP ground out so the party can buy better weapons and armour
as often as it can afford them. `tools/play.py` now does that:

- `plan_gear` / `gear_up`: each time through a town, for every member,
  the best head, body and legs piece and the best pair of hands (a
  two-hander, or one-handers in each hand -- a shield is a one-hander
  with no attack; attack counts double) from the town's weapon and
  armour shelves (`stores.json`, cut to the door's `$F764` length) *and*
  from the member's own bag, which costs nothing -- so a chest's find,
  or something Shir brings back, is worn when it beats the shelf. Bought
  best gain per meseta first within the money less a reserve, put on
  with EQP (and into the other hand when it went over the wrong one --
  a second copy of a worn weapon put over the first changes nothing,
  since `loc_AC82` clears the first worn entry of the old id), and
  `surplus()` sold at the item store: unworn pieces some store sells
  that no member who could wear them would want. Anything no store sells
  is never sold.
- `list_pick`: ITEM's and EQP's lists page at nine items -- NEXT at stop
  0 above slots 0-7, a second window for slots 8-15. `equip()` used to
  pick the wrong slot once a bag grew.
- `sell_policy`: only the item store buys; the equipment stores have no
  SELL (`equipment_store`, loc_BAA8 on).
- `fight_smart` orders a mate in battle (STGY, ORDR, the holder, ITEM,
  the mate, the target) for anyone under 45% when someone carries one.
- `town_exit` takes the last step off a town's two-deep edge;
  `enter_town` aims at the arrival cells Motavia's doors name, never an
  edge cell, since a party standing on one leaves with its next step.
- `grind_and_gear(s, town, level)`: heal, gear up, a few mates, out,
  fight, back; the clone lab when someone dies.

`tools/story.py` plays the opening that way from `start new`, stage by
stage, writing `routes/saves/story_<stage>.json` after each and
`route_out/<session>/story.txt` at the end (fights, deaths, every
distinct stderr line):

    python3 tools/story.py --to arima
    python3 tools/story.py --from arima --to nido

From a new game to the end of `arima` -- Paseo to Rolf L5, the tube,
Arima to L7, Rudo at Rolf's house (`$090C $091C $091D $0913 $090E
$090F $0910 $092F`), Paseo to L8 -- is ~240 fights, no deaths, no
stderr, under four seconds: Rolf two SonicGuns, FiberCoat, Headgear;
Nei two SteelBars; Rudo two Daggers, FiberCoat, Boots, Headgear.
`story_paseo.json` and `story_arima.json` are those checkpoints.
`story_shure.json` is still build 66's levelled one.

Shure from `story_arima.json` does not finish yet: the F3 body works
(`$170B $170C`, the key and the Letter), the F2 chest at 67,44 and the
ground floor's locked one at 73,15 (`$1707`, the Small Key from the
menu, `$0001 $0017`, A, `$1701`) open, and six chests come back
unreached with the party at a few HP. See NOTES_next_context.md.

## Build 68: Teim, the Blastoids' copies, gearing at Arima

**Teim (ObjType_Events $17).** `Obj_Teim`'s init puts 4 in `$2C`, so A
on her in Nido F2 is `ObjType_Events` with her index byte, $17 --
`loc_DC36` (ps2.asm:20180), which game.c did not have: A gave `$1708`
and "event $17 is not ported" on stderr. Now `level_teim`: the big box;
with `event_flags[$17]` set or no Letter in any bag
(`CheckItemExistInventory`, 19067: party order, 16 slots, bit 7
masked), `$1807` and the box closes on the next button; with the
Letter, `$1808` (which runs on into `$1809`), and the next idle visit
(`loc_DC64`) sets the flag, takes the Letter out
(`RemoveItemFromInventory2`, 16065: the slots above close up), puts Teim
into the same bag (`AddItemToInventory2` with `character_index`) and
plays `SXFID_ItemReceived`. The flag is `$C727`, which `Obj_Teim`'s
routine 0 reads every frame, so she is gone at once. Route:
`nido_teim` (`$1807`, the Letter given, `$1808`, Teim in Rolf's bag,
the party standing where she stood).

**The Blastoids' copies (EnemyTechniqueTable's `loc_2F6C`).** Enemy
technique 12 -- the Blastoid's (45) and the Slugmess's (33) -- was an
attack in battle_obj.c. `enemy_clone_into` is `loc_2FA4`: the enemy
slot before the caster (F_ALT set) or after it (F_ALT clear) takes a
copy if it is dead -- id 0, routine 4 -- and not below `$E800`. The
object's 64 bytes are copied but the copy keeps its own `$A` long (x
and home x) and `$40`; `3(a2)` = `$80`, routine 4; the stats' 64 bytes
follow; `$2C(a0)` points at the slot. Then `loc_2C28` on the copy (its
`+$16` is the caster's unused word: luck 0, as `loc_2F10`'s), `$1A` =
0, routine 3. `ObjEnmBattle_Dead` brings it in when the script's hit
lands (F_HIT without F_MISS: shown, the group's count + 1, routine 1)
or clears it on a miss. Neither neighbour free, and it is `loc_2A98`'s
attack. Slot 8 would be `$EC00`, the first effect object, with its
stats past the buffer; five columns never reach it, so the port stops
at slot 7. The probe gains `expect enemy-joined` (an enemy has come
into a fight since the last match, from `battle_debug_enemies_joined`).
Route: `blastoid_copy` (formation 45 at L35: one dies, a copy with its
neighbour's 43 HP takes the slot).

Not a port bug: the Blaster (44, formation 44) that wiped the party in
Nido in build 67's grind has technique `$EB` -- negative, so
`Enemy_ProcessTechnique` takes `loc_2CEA`: `$15`, odd, `loc_2D08`,
20 to the whole party, at rate 255 every turn.

**The grind (tools/play.py, tools/story.py).**

- Techniques: `map_techs` (techs_b summed over the level table indexes
  CharAvailableTechniques' map list), `find_tech`, `use_tech` (C, TECH,
  caster, list stop, ON). `cast_res` (Gires for a big gap, else Res)
  runs in `mend` before any mate and after every fight in `wander`
  below 75%; `retreat` casts Hinas / Ryuka before an Escapipe /
  Telepipe.
- `give_item` (ITEM, holder, slot, GIVE, member) and `plan_gear`
  counting other members' bags: a chest's SilRibbon is Nei's whoever
  picked it up; Rolf's old Knives went to Rudo when he joined.
- `plan_gear(..., unlimited=True)` and `geared(s, town)`: nothing on
  the shelf is an upgrade for anyone, whatever the money.
  `grind_and_gear(..., until_geared=True)` keeps going until then.
- `gear_up` sells the surplus even when there is nothing to buy (it
  returned early before). `sell_policy` handles the second page of the
  item list (`$04`, slot 8 up): before, slot 8 asked for stop 9 on a
  nine-stop list and the cursor went round until the visit gave up.
- `enter_town` from Motavia plans the whole way when the town's door
  is not on this side (Paseo from Arima's gate is through the tube).
- travel / goto log why they stop.

`stage_gear`: Arima has a weapon store and no armour or item store, so
rounds of Arima's field (gearing each time through) and then Paseo --
its item store buys back what was replaced, its armour store, the bags
handed round -- until neither town has an upgrade left. It runs before
Shure and again before Nido. From `start new` to Teim in Rolf's bag:
789 fights, no deaths, no stderr, all nine Shure chests and Nido F1's
three; Rolf and Rudo two CeramKnives each, Nei the SilRibbon.
Checkpoints: `story_paseo`, `story_arima`, `story_gear`, `story_shure`,
`story_nido`.

## Build 69: the invisible blocks, the towns' signs, Darum, Oputa, Amy

**Obj_InvisibleBlock ($37) and Obj_InvisibleBlock2 ($38).** npc_spawn
kept a free slot for them ("event blocks: not ported"), so A on every
one gave `$1708`. ps2.asm:7820: the init writes `2(a0)` = 0, mappings
`loc_5D98C`, tile word `$2000`, `$2C` = 9 or 7, routine 1, which does
nothing. Never drawn, but RunObject's tail marks it on screen, so it is
solid and `Level_ChkTargetInteract` finds it; its `$2E` from the
level's sprite data is the `ObjType_Events` index. npc.c now spawns
them as scene objects of the port's own (no art, so not in
scene_objects.json) with `block_tick` (loc_55BC / loc_55F8).

**ObjType_Events 1, 2, 4, 8-$F.** Each a script and `$DE72` + 1:
1, `loc_DA74`, Arima's six blown doors ("Those scoundrels seem to have
blown open the door with dynamite ...", `$170A`); 2, `loc_DA80`, the
control towers in Paseo (4), Arima, Oputa (2), Zema, Kueri (2) and
Piata (5) ("This is the control tower which links the town and Mother
Brain in one network.", `$1709`); 4, `loc_DA98`, Uzo's six not-quite
Maruera trees (`$170F`); 8-$E, `loc_DB22` on, Skure's old newspapers
(`$1719` and one of `$1712`-`$1718`); $F, `loc_DB84`, the odd-coloured
water on Motavia (`$1720`). Routes: `oputa_tower`, `arima_signs` (the
blown door, then the tower from above -- the block's box, 32 by 16 from
its x, is solid).

Other events the levels place that are still not ported: $07, $10,
$12-$16, $25, $2B, $2F (their levels: see `level_objects` in
tools/play.py). A on one says so on stderr.

**Darum and Teim.** Already ported in build 50-ish (LoadSpritesInLevel's
tail, `level_teim_scene`); `darum_teim` is its route now: Teim given
on Motavia, into the tube from the east, `$1802` through `$1806`, Teim
out of the bag.

**The story on.** `stage_darum` (the tube with Teim), `stage_oputa`
(its armour, weapons and item store: Fibergear for Rolf and Rudo, a
FiberVest and two CeramBars for Nei), `stage_amy` (Rolf's house:
REORGANIZE, stop 0 twice -- each pick leaves the list -- Rudo and then
Amy; Oputa again for her). A checkpoint saves at the town's own Data
Memory when it has one. From `story_nido.json` to Amy geared: no
deaths, no stderr.

## Build 70: Ustvestia's house

**Building_UstvestiaHouse (ps2.asm:18151), building 12 in Oputa.** It
was "building: 12 is not ported". Now `ustvestia` in building.c, by
`UstvesHouseEventIndex` (event_routine 2 on; the dispatcher's d1 is
event_routine - 1 and the table is entered at -4):

- `$A01` in WinID_ScriptMessage2, YES / NO. YES: "Great! Pick a song."
  (`$A02`) and event_routine + $B to the songs. NO: "Oh, I get it, you
  want to learn to play the piano!" (`$A03`), YES / NO: NO is sub 1,
  "Well, actually, I'm kind of busy right now, I've got to go!"
  (`$A04`); YES is `$A05` and YES / NO again: NO, "Why, you--go on, get
  out of here." (`$A06`, sub 3, closed); YES, "Who is going to learn?"
  (`$A07`) and the store's character list.
- `loc_C73C`: bit 0 of `character_index` -- odd, the women, `$A09` and
  5000; even, the men, `$A08` and 2000. The money window and YES / NO
  together (`move.l`). NO: `$A04`. YES: CheckSubtractMoney -- short,
  "You don't have enough money. I'm not cheap." (`$A0A`); else the
  money redrawn and "Ok, I'll start the lessons." (`$A0B`).
- `loc_C7AC`, once the line is out: PaletteFadeFrom, SXFID_Musik ($B1),
  $78 frames, PaletteFadeTo, all inside one call. The building returns
  the new `BUILDING_LESSON` and game.c's `tick_lesson` does the fade out
  (FADE_FRAMES), the sound, the wait and the fade back to the palette
  it copied, with nothing else running.
- `loc_C7D6`: "Now you are also a musical artist! Come again!" (`$A0C`);
  `bset #7` on the character's map technique count (`$C026 + $40 * n`,
  techs_b) and, the first time, the count + 1 and `$A0D` in `$CD02`
  ("ROLF has learned the MUSIK technique."). gamedata_known_techs
  already puts Musik last when bit 7 is set.
- `loc_C7FC` / `loc_C80E`, the songs: `$CB12` = 0 and
  WinID_UstvestiaSoundtracks (`$74`, new in window.c: descriptor
  `$451A`, 13 x 3, WinArt_UstvestiaSoundtracks; one name from
  SoundtrackCharArray in the 2-row font, upper halves in row 1, lower in
  row 2; never waits). The building reads the pad: up / down step two
  bytes through Ustvestia_MusicPointers, wrapping ($2A at the top, 0
  past $2C), and keep going the same way past a song whose event flag
  (`c700[$10 + n]`: 6 for Secret Ways and Mystery, $2F for five, $34 for
  Under, Exclaim and Never Dream) is clear; C plays the song; B is sub 1
  (`$A04`). Each press redraws the window (`$0600 | $74`).

The probe gains `meseta N`. Routes: `ustvestia_songs` (YES; Phantasy,
Restration, up past the locked songs to Over; B), `ustvestia_lesson`
(NO, YES, YES, Rolf, 2000 paid from 9000, the fade, `$A0C`, `$A0D`, and
Musik in Rolf's TECH list at L1), `skure_newspaper` (build 69's event 8
in a `$3A` container: `$1719` then `$1712`).

## Build 71: who can wear what, and the attack rolls

**Equipment limits.** Every item's who-can-equip byte (byte 14 of its
InventoryData record, `CharID_*_Mask` = `1 << id`), attack and defense
in assets/data/items.json checked against ps2.asm: all 127 match. The
port's EQP refuses by the mask (`loc_AB6A`, $205 / $201 / $203), and
play.py's `wearable` reads the same mask, so the harness only plans
pieces the character can wear. Rudo: Knife, Dagger, CeramKnife,
SonicGun, Shotgun and Bowgun (two-handed), not the Sword (Rolf's) or
the Scalpel (Amy's and Hugh's); Nei: SteelBar and CeramBar only.

**The rolls, against the ROM.**

- `CalculateHitRate` tests `_btst #4, 0(a3)`: byte 0 is the status
  word's high byte, so bit 12 ($1000). battle_obj.c's `hit_rate` tested
  bit 11 ($0800), which is what `loc_27C8`'s technique rolls test
  (`#3`). Now `ST_HALF_ATK` ($1000). Dexterity ($18) against the
  enemy's agility ($14), `lsl.w` truncating the dexterity to its low
  byte, as before.
- `CalculateAttackDamage`: (0-31 + $54 + attack_base, `$1A`: before
  equipment) x 256 / (defense x 5 + 100), then `mulu.w` by the weapon's
  attack + 2 and `lsr.w #8` -- the product's low word only. The port
  shifted the whole product, so a product of $10000 or more gave too
  much. Fixed.
- `loc_2AA8`, an enemy's blow: the same `mulu.w` / `lsr.w #8`, fixed the
  same way. This one bites: an Archdragon (attack 254) against defense
  20 does 152-232 in the ROM and did 408-488 in the port; against
  defense 40, 15-69 against 271-325.
- `GenerateRandStatIncrease`: `ext.w` on the gain and the low word of
  the product. No gain in the level tables reaches $80, so nothing
  changes; mirrored anyway.

No route yet shows the wrap: the probe has no `expect hp`.

## Build 72: the save's flags by name

The flag bytes are bytes, not words: `$C700-$C70F` loose ones,
`$C710-$C77F` event_flags (ObjType_Events n uses event_flags + n),
`$C780-$C7FF` treasure_chest_flags. A chest's byte, as
ObjType_TreasureChests reads it: 0 shut, bit 7 ($80, `bmi`) locked --
the Small Key's `bclr #7` makes it 0 -- and 1 once taken (`move.b #1`).

Save format 2 writes the three sections as JSON objects, one member a
byte, keyed by address and name, e.g.

    "event_flags": {
      "c721_shure_f3_body_searched": 2,
      "c727_teim_given_the_letter": 1,
      ...
    "treasure_chest_flags": {
      "c798_chest_shure_f3_41_21_sil_ribbon": "taken",
      "c790_chest_shure_ground_f_19_13_monomate": "locked",
      ...

Loading reads only the `c7XX` at the front of a key, so the rest of the
name is free to edit; a value may be a number, true / false, "shut",
"locked", "taken" or "$XX". A key that is not a `c7XX` in its section,
or a value that is not a byte, is reported on stderr and skipped.
Format 1 saves (hex rows) still load. `raw_c600` stays hex.

The names come from `tools/extract_flag_names.py` ->
`assets/data/flag_names.json` (`make extract-flag_names manifest`): the
hand-checked ones first (the teleport lists, Shure's body, Teim, the
Governor's errand, Nei gone, the roof, the Esper mansion's chests),
then for a chest byte the chest the levels place with that index and
what is in it, then the first routine in ps2.asm that names the address
(`flag_loc_xxxx`), then an event object placed with that index. 149 of
256 are named; the rest are `unused` -- nothing in the ROM names them
directly, though an indexed use can still reach them. Each entry's
`notes` lists the routines that touch it.

Harness: `grind_and_gear(..., lowest=True)` levels the lowest member;
`stage_amy_grind` (Oputa's field, everyone to L14) ran 479 fights with
no deaths, Amy L6 to L10, and gave up at its trip limit.


## Build 73: the Recorder, the keyboard, the Hidapipe, and the lab

**ObjType_Events $10, $12, $13-$16 (game.c).** Each goes through
loc_E84A (level_event_item: $1702, then $1703 or $1704, the flag + 1,
SXFID_ItemReceived, $DE72 + 1):

- $10, `loc_DB90`, Uzo's Maruera tree: the first visit "This is it!
  This is the Maruera-tree." ($1710) and nothing more; the next idle one
  the Maruera Leaf. Flag set: $1708.
- $12, `loc_DBF8`, the Recorder in the Biosystems Lab's basement. No d2
  test: the Recorder at once, and with `party_member_join_next` at 3 it
  goes to 4 -- Hugh's knock at Rolf's door. Before this build A there
  was $1708 and the story stopped.
- $13-$16, `loc_DC1E`, Control Tower F1's four boxes: item event - $E,
  the Green, Blue, Yellow and Red cards.

An invisible block's box is not the cell its x,y names: `$37` is
loc_7278 row 9 (64 wide from x - 32, 64 high ending at y - 1), `$38`
row 7 (32 wide from x - 16, 16 high from y). The Recorder's block at
x 1280, y 320 is the machine at cells 78-81, rows 16-19, faced from
row 20.

**Sounds waited out in place.** A few routines queue a sound, clear
`track_timer` and loop until the driver sets it to $80 at the track's
end. VBlank clears `vblank_routine` after every field, so during the
loop only `VBlankExit` runs -- SoundDriverInput and
GenerateRandomNumber: no ProcessWindows, no joypad read. game.c's
`g_sound_wait` does the same: while set, `tick_screens` runs the RNG and
the sequencer and returns. `audio_track_ended` / `audio_track_timer_
clear` (audio.c) read and clear the sequencer's own `track_timer`; with
no music data there is nothing to wait for. A wait over 20 seconds ends
with a line on stderr -- the port's guard, not the ROM's.

- **Musik** (`TechEffect_Musik`, 15597): facing the event block with
  index 7 -- Control Tower F1's keyboard -- $109 alone, `$C734` = 1
  (event_flags $24, the shutter at slot 0), SXFID_Musik waited out,
  `bset #0` on `$C717` and, the first time, demo 5, whose $FD slides the
  shutter open. Anything else faced: $109 then $004, as before.
- **The keyboard** (ObjType_Events 7, `loc_DAD4`): $1711, WinID_YesNo2,
  and YES jumps to the Telepipe's target, `$C64C`-`$C650`, with
  SXFID_Teleport waited out.
- **The Hidapipe** (`ItemAction_Hidapipe`, 14573): the port played the
  sound and took the item but never set `$FFFFCB0E`, so it did nothing.
  Now: SXFID_Hidapipe waited out (about 470 ticks), then the flag, which
  `ProcessRandomBattle` reads -- the counter gains 2 a step on foot on
  the planets instead of 4. `loc_6F1C` clears it on the next way into a
  dungeon (a level under $10 into one of $10 up), and a new game zeroes
  it. It is not an escape item.

**Routes** (each fails on build 72 and passes here): `biolab_recorder`,
`uzo_maruera`, `tower_cards`, `tower_musik`, `tower_keyboard`,
`hidapipe`; and `damage_wrap` (build 71's `loc_2AA8` fix: an Archdragon
against Rolf L30 with defense 24 takes him to 25-98 HP; the pre-71
formula kills him). The probe gains `expect item NAME [MEMBER]`,
`expect join-next N`, `expect hp MEMBER LO [HI]`, `expect hidapipe
on|off` and `defense MEMBER N`.

**Pipes and Ryuka, for the harness.** Escapipe and Hinas go to
`$C64C`, which `loc_6F1C` writes on the way into a dungeon: the
dungeon's own entrance. Telepipe and Ryuka on Motavia go to `$C646`,
which Data Memory's save (`loc_B5A4`) writes: the town of the last
save (Paseo instead after a Visiphone or Dezolis save). `play.Session`
keeps `save_town` (set by `save()` and by `continue_game` from the
save's location) and `retreat()` defaults to it; retreat also uses the
town's own clone lab, hospital and item store instead of Paseo's.

**The harness through the lab.**

- `holes(level)` (loc_718A's table, type 5 block $3A, type 6 block
  $04, Crevice's two) are ways off a level in `plan()`: the only way to
  B1 is F2 -> hole -> F1 -> hole -> ground floor -> hole -> B1.
- `LevelMap.path` crosses damage floors at five steps' cost; B1 is
  some 440 cells of them.
- `event_block` / `facing_cells` / `search_event`: A on an invisible
  block by its box.
- `dungeon_grind`: fight on a dungeon floor, mend, back to a town's
  hospital when the mates run out.
- A checkpoint no longer copies a stale `slot1.json` when the save
  failed, and none is written off the map.

`tools/story.py` stages after `amy_grind`: `lab_grind` (Oputa's field
to L16: 831 fights for two levels, gave up), `lab_in` (the ground
floor's Dynamite door, then the ground floor to L18: some 1600 fights,
Rolf L14 to L17), `lab` (F1 and F2's chests -- F2's holds the Dynamite
for F2's own door, index $1A, which stands between the floors and the
holes down -- then the Recorder), `lab_b1` (B1's chests: two of three),
`hugh` (Rolf's house: $090C, Hugh's $091F $0920, Rudo and Amy kept; the
Governor: $0D04, $1107 $1108 $1109, the library's $1010-$1014, the
Recorder traded for the Key Tube). Checkpoints `story_lab_grind`,
`story_lab_in`, `story_lab`, `story_lab_b1`, `story_hugh`. No stderr
line, no port fault found in the lab: every wipe was the party's level.

## Build 74: Roron's people, and choosing the party

**Object `$1B` (`loc_4288`, ps2.asm:6273).** It has no `PtrObj_` label
and no `Obj_` name: the Mota people on Roron F2-F5 (levels `$1E`-`$21`),
dialogue `$80`-`$87`. Before this build `npc_spawn` kept their slots free,
so Roron was empty and the jet scooter could not be given in play.

- Init, `loc_42A6`: tile word `$400` (Roron's art list 9, `loc_59A10` at
  `$400`), mappings `loc_59F62` (twelve frames of 2x3 tiles), hit box row
  4, first wait `$30`, `$34` = `$35` = 0. With `$2E` = `$186` or `$187`
  and `$C716` = 2 -- the scooter ridden -- it deletes itself
  (`move.b #1, 2(a0)`) before it is ever drawn.
- Wander, `loc_42F8`, four-way where the nine town types go along one
  axis. Every `$30` frames a roll of 0-15: 1 turns it down unless it faces
  up, 3 up unless down, 5 left unless right, 7 right unless left; `$A/$B`
  a step up (not from facing down), `$C/$D` down, 8/9 left, `$E/$F`
  right. `$35` counts cells down and stops at -3 and 3, `$34` across at
  -5 and 5. The facings are frame bases -- 0 up, 3 down, 6 left, 9 right
  -- but a step up or down leaves `$2A` on 1 or 4, the first walking
  frame, and a step refused (range or `Sprites_ChkCanMove`) still shows
  the new `$2A` (`loc_44D8`). A step is sixteen frames, a frame every
  four from `Level_SpriteMappingsArray2` (0, 1, 0, 1).

`tools/extract_npcs.py` writes it into `npc_types.json` as `MotaRoron`
with `"wander": "four"` (`make extract-npcs`; nothing else changes).
`npc.c` runs it as `decide_four` / `walk_four`; `Npc.range_x` is its `$34`.

**ObjType_People `$87` and `$85`** (ps2.asm:20500, game.c):

- `$87`, `loc_E690`, the scooter's maker on F5: `$1685` ("I think I'm
  gonna try and ride this over the ocean ..."), `$C716` = 1, and the
  scooter parked at x `$590`, y `$730` on Motavia (`$C656`, `$C654`).
- `$85`, `loc_E5FE`, the cake on F4, one step per idle visit with d2 =
  `$DE70` (`level_people_scene`, `g_level_event` = `$185`): `$1681`,
  `WinID_YesNo2`; NO is `$1683` and done; YES is `$1682`, then
  `WinID_MenuCharStats`, `$1E` frames, ten HP off every member (not below
  0: a member under 10 dies of it), the stats redrawn in place, and
  `WaitJoypad_B_C_Pressed`. `character_index` is zeroed on every visit, so
  `{char}` is ROLF's name whoever leads. The last step reads the pad with
  no window polling, so `game_debug_state` reports it as an event wait.

Routes (each fails on build 73's game code and passes here):
`roron_cake` (YES: Rolf 19 -> 9, Nei 12 -> 2), `roron_scooter` (`$1685`,
`$C716` = 1), `roron_scooter_gone` (`$C716` = 2, down F5's stairs and
back up: `$86` and `$87` are gone).

**The probe.** `objects` logs every live slot as
`slot:object:type/index:cell` (`m` mid-step); `roster` everyone who has
joined, at home or not, `NAME:Llevel:hp/max:tp/max` and `*` for the
party; `flag $C7XX N` sets one `$C700` byte (read wherever the game next
looks -- an object's init at the next level load); `expect flag $C7XX N`
and `expect person $XX present|absent` check.

**Note 12 of the handoff is the ROM.** Amy saved with status `$0100`:
bit 8 is Defend (`CommandUsed_Defense`, `bset #0, 0(a3)`), cleared only by
`CommandUsed_Attack`'s `bclr`, and `RestoreCharDataAfterBattle` keeps
`$8100` of the status word. A character whose last command was Defend
carries the bit out of the fight and into the save, and has
`loc_2AA8`'s halved damage until she next attacks. `battle.c` masks the
same.

**Note 4 too, as far as the code goes.** `loc_F2A4` and
`CheckEnemyAlive` match the port line by line (the share `divu` by the
living, masked to 16 bits, plus 1; EXP only for kills; the 9,999,999
cap), and the checkpoints lose nothing -- Rudo is 827 behind Rolf and Amy
4,271 behind at every one. The level table is what slows it: Rolf's L18
is 64,881 and L19 86,896.

**Choosing the party (tools/play.py).** `choose_party(members)` keeps
Rolf, Nei and Rudo and fills the fourth place by `member_value`: HP +
1.5 x attack + defense + half agility at the member's level, from the
level table (GenerateRandStatIncrease averages 1), plus the healing their
map techniques give between fights. A candidate `CATCH_UP_GAP` (5) or more
levels behind the best of the others takes the place instead, to catch
up: EXP is split among the living and the table is absolute, so a low
member levels cheaply. `pick_party(s)` reads `roster`, chooses, and
regroups at Rolf's house (`house_policy(regroup=True)`: NO to `$092E`,
REORGANIZE, the stops from `regroup_stops`). In play this alternates by
itself: Hugh (L1, sixteen behind Amy) went to Zema and reached L13 in 299
fights with no deaths; the rule then put Amy back for Roron (value 320
against Hugh's 220).

**The story on** (`tools/story.py`, from `story_hugh.json`, no deaths, no
stderr):

- `key_tube`: Hugh in; the tube's door from the north (`$170E`, the Key
  Tube `$0018`); through, and TubeNearZema, to Zema.
- `zema`: the shelf (LaserKnives for everyone who can hold one,
  Titanigear, Titanimet, Boots), then the field until Hugh is within
  `CATCH_UP_GAP`.
- `roron`: Amy back; the six chests (a Cannon, a CeramBar, and four
  `$1705` garbage, which is `TreasureChestContentArray` for `$1E`, `$1F`,
  `$21`, `$23`); the maker, `$1685` on the first try.

Checkpoints `story_key_tube`, `story_zema`, `story_roron`.

**Harness fixes on the way** (none of them the port's):

- `battle_list_pick`: the battle's item list is four a page with NEXT at
  stop 0 from five items (`battle_list_cursor`). Ordering a mate from slot
  5 or up asked for a stop the list does not have, and the cursor went
  round for 34,000 ticks while the fight waited. A failed order now backs
  out to FIGHT/STGY once a round.
- `buy` / `sell`: `visit`'s 80 steps ran out part way through eleven
  sales in Zema; the limit now grows with the list. `Session.ready`
  backs out of a building with B instead of pressing C through it, which
  had gone on selling whatever the cursor was on and looped on `$0805`
  (a key item: bit 7 of its kind byte clear, `loc_BE8A`); `sell_policy`
  skips an item answered `$0805` or `$080E`.
- `checkpoint` leaves a building before saving; `talk_to(s, index)`
  finds a wanderer with `objects`, walks up, faces them and presses A.

## Build 75: the tree cleaned

Leftovers from earlier iterations are gone: the preview PNGs and demo
WAVs at the top level, `atrace.txt`, `route_out/` (every route rewrites
its own), the `.lvl`/`.blk` level files from before Tiled, the `.pal`
palettes and `palettes/swatches/` (the PNG's PLTE is the palette), the
`.ps2t` tilesets, `assets/art/`, the placeholder font, an old
`art_manifest.json` and `callgraph.json`, `tools/shop_probe.c` (it no
longer built; `building_probe` covers the shops) and
`tools/make_placeholder_font.py`. The preload went from 1172 files, 4.8
MB, to 812 files, 3.3 MB.

`assets/data/flag_names.json` was missing, so saves wrote their flags as
bare addresses; it is back, and the next save names them (only the
`c7XX` part of a key is read, so older saves load as before).
`extract_script.py` is now `make extract-script` and part of `make
extract`; the `font` extractor and `--swatches` are gone; `level_probe`
links `json.c` again; `make clean` also removes `intro_probe` and
`building_probe`; `callgraph.py` writes `route_out/callgraph.json`.

## Build 76: NEXT in the battle lists, and Kueri's gum inventor

**NEXT closed the battle's technique and item lists.** With five or more
entries, C on NEXT (`Win_BattleTechList`, 24937, `loc_10C6C`; the item
list's is the same code at 25049) writes `$0600 | the list's id` --
redraw, instant -- and moves `$DEEC` (`$DEEE`) on four, back to 0 once
the count is not above it. `ProcessWindows` runs in VBlank, and its first
pass over a word only sets it up (`btst #1` then rts, 13570); the next
frame `CheckPrepareWindows` sees the word unmarked, sets
`window_index_saved` to the list and runs it from phase 0, and only then
is it drawn and shifted. The port drew a redraw of a window already open
in one pass, the same pass the handler had written it in: the word was
gone before `win_prepare` saw it, `window_index_saved` stayed 0, nothing
polled the list, the command routine carried on with no choice and the
windows closed back to FIGHT/STGY. `win_process` now leaves a redraw the
prepare pass has not marked for the next frame. Every other redraw is
written by event code, which `win_prepare` always sees first, so nothing
else moves.

Routes (each fails on build 74's `window.c`: `menu $60` where the list
should be): `battle_tech_next` -- Amy at L30, thirteen techniques, four
pages and the wrap, NASAR from page 4 (technique 44, cast in the round)
-- and `battle_item_next` -- her worn Scalpel, CarbonSuit and Shoes, four
Monomates and a Dimate, two pages and the wrap, the Dimate (item 18)
from page 2 on Rudo.

The probe has `expect menu $XX [STOP]`: the window whose poll holds the
pad, as `state` shows it, and its cursor's stop.

**Kueri's gum inventor** (`Building_InventorHouse`, 18378; building 13,
the door at 15,39, portrait `$0C`). `GumInvHouseEventIndex`:

- `loc_C8CA`: `WinID_ScriptMessage2`, `$B08` written and overwritten at
  once by `$B01` ("... a new kind of gum!") -- `$B08` never shows, as the
  disassembly notes.
- `loc_C8E2`: `CheckItemExistInventory` for `ItemID_MruraLeaf`. Found:
  `$B05` and three states on. Not: `$B02`, "Do you know what MARUERA
  LEAVES are?"
- `loc_C902`/`loc_C90E`: `WinID_YesNo2`; YES `$B03`, NO `$B04` (the tree
  on an island in the sea) then `$B03`; then `CloseAllWindows`.
- `loc_C92A`/`loc_C936`: `WinID_YesNo2`; NO `$B07`; YES (`loc_C948`)
  takes the leaf from the bag and slot the check left in
  `character_index` and `$DE84`, puts `ItemID_MruraGum` after that bag's
  last item, `SXFID_ItemReceived`, `$B06`.

No flag is set: every visit asks again, and another leaf makes more gum.
`CheckItemExistInventory` leaves `character_index` on each member as it
searches and `$DE84` counting slots, so a miss leaves the last member and
16; `check_item_exist` does the same. The portrait is the Mota item
seller's (`$0C`, from Kueri's door record) -- worth a look in the
emulator. Routes `kueri_inventor` (the leaf in, the gum out) and
`kueri_inventor_no_leaf` (NO: `$B04`, `$B03`); before this build Kueri's
door said "level: door to building 13 (portrait `$0C`) is not ported"
and stayed shut.

The gum's own use (`ItemAction_MruraGum`, into the Underwater Passage)
was already in `menu.c`.

## Build 77: the gum's way out, and the jet scooter in the harness

**The gum's way out of the Underwater Passage.** `ItemAction_MruraGum`'s
`loc_9CD6` (14391) records where a pipe or Hinas brings the party back to:
`$C64C` = Motavia, and `$C64E` / `$C650` = object slot 0's y and x
(`$FFFFE80E`, `$FFFFE80A`) & `$FFF0` -- the jet scooter, parked over the
ocean floor where the gum was eaten. The port wrote `$C64C` only, so an
Escapipe from the passage or Climatrol went to whatever `$C64E`/`$C650`
held from the last way into a dungeon (Uzo's doorstep, in the harness's
run). The level screen now hands the menu slot 0's position each frame
(`MenuState.slot0_x`/`slot0_y`) and the gum writes it. The gum is not used
up (nothing in the routine removes it), and the party comes back riding.
Route `gum_way_out` from `routes/saves/gum_spot.json` (jet_scooter.json
saved riding at 157,12): the gum facing the block, `$19`, the passage at
112,23, an Escapipe, back at 157,12 on the scooter. On build 76's code the
pipe lands at 129,25.

**A at the Biosystems Lab's capsules** is `$1708`, "There seems to be
nothing unusual here." `ProcessAButAction` (19806) asks
`Level_ChkTargetInteract` for an object in front and nothing else; the
capsules are map blocks, and the lab's floors place only chests, the
Recorder's block and the two doors. Route `biolab_capsule` (F1, 5,30
facing the capsule at 6-7, 28-31) keeps it that way; it is not a fix.

**The harness rides the jet scooter** (`tools/play.py`):

- `ScooterMap`: Motavia through `loc_29E16` (`jet_scooter.collision.txt`)
  with the 2x2 footprint and `ObjJetSctr_Main`'s two probes a direction,
  the first deciding; a solid special probe goes to the door records as
  `interact()` does. `plan()` is a BFS to any (cell, direction) a goal
  accepts.
- `board_scooter`: out onto Motavia if need be, to a cell facing the
  parked scooter (object `$22`, its box the two cells up and left of its
  corner), A and C through `$1686`/`$171C`.
- `ride`, `ride_into` (a push into a dam), `ride_ashore` (turn toward
  ground connected on foot to the goal, A: `$171D`), `ride_to_face` (the
  leader's A box, `loc_7278`'s facing row from the scooter's corner, over
  an object's box).
- `travel()` gets off first when it starts riding on Motavia, landing
  somewhere connected to the target's own door (or, failing that, any
  piece of ground `plan()` gets from).
- `state` says `riding`; while riding the cell is the scooter's corner >>
  4.

Two harness fixes: a door onto the same level (Uzo's caves) ends the leg
instead of looping on it, and a town's edge cell counts only if it can be
walked onto -- `check_underfoot` reads plane B but the step is walked on
the collision plane, and Kueri's whole west side is a wall there.

`tools/story.py` has two new stages. `uzo`: the scooter from Roron, a
landing on Uzo's island (its door is on the island's ground, not a push
from the water), the Maruera tree (`$1710 $1702 $1703`), back on the
scooter, ashore by Kueri, the inventor (`$0B01 $0B05 $0B06`), gear.
`gum`: the scooter to the ocean floor's event `$F` block, the gum (`$19`),
the Underwater Passage walked to Climatrol's ground floor (82,8), back to
Kueri. Checkpoints `story_uzo.json` and `story_gum.json`. Neither run met
a port complaint; the catch-up choice of Anna at L1 died in 19 of the Uzo
stage's 40 fights, which is the party policy, not the port.


## Build 78: Neifirst, and the death of Nei

**Neifirst's object** (`Obj_Neifirst`, `$32`, ps2.asm:7699): one 16x32
piece at tile `$450` (art list 6's `loc_5D05E`), hit-box row 4, on
Climatrol F7 at 48,25 with `$2E` = `$0025`. Her init deletes her when
`$C737` is set or slot 0 (`$FFFFE400`) is empty -- and loc_7E02 runs
before the objects' inits, which is what keeps her off the death scene's
screens. `NpcSet.party_gone` carries that; `extract_npcs.py` writes her
and `Obj_RandomExplosion` (`$2D`, 7509) into `scene_objects.json`
(`make extract-npcs`; npc.c's `SCENE_TYPES_MAX` was 8, and the ninth
entry, the Dynamite door, fell off it until it was raised).

**A on her is ObjType_Events $25** (`loc_DCA8`, 20217): `$180A`+`$180C`
(each running on: `$180B`, `$180D`); Nei dead → `$180E` and done, else
`$180F`+`$1810`; YesNo2; NO → `$1822`; YES → `$1823`, then formation
`$100`, `event_flags` = `$100`, the party saved to `$C618`/`$C61C`/
`$C624` (c600_raw 0x18, 0x1C, 0x24 -- in the save, as the ROM's are) and
Nei alone. BattleScreen's own `level_x/y_pos` from the leader's cell
(12065) is now done for event battles too; before, the level came back
where the party had entered it.

**The battle side** (22321-22401) had two stubs. All enemies dead is
`move.b #$87, $FFFFF640.w` -- the latch alone, which `audio_set_last_
sound` / `WinSys.music_latch` now is; the old `ws->music(0x87)` *played*
Advanced over the victory. Everyone down in the Neifirst battle brings
the party back from `$C618` before counting the living (`loc_F1F6`), so
Nei alone going down is the level again, not the SEGA screen.

**After the battle, seven level reloads** (`loc_7E02` cases 1-7,
11494-11600, and `loc_809C`'s camera cases 1-4, 11733-11830), each case
once in `enter_level` (`g_level_oneshot`, which the case sets to the
next), with the scene events in `level_objtype_events`:

| Reload | Level | Case | Scene |
|---|---|---|---|
| 1 | F7 | picture `loc_AF4EA` (27x13, plane A row 2 col 6 -- the dream pictures' place) from `loc_AED00` at tile 1, PalettePtrs `$35` on line 1, party object gone, plane redraw skipped, Power queued, latch `$87` | `$26`: `$1826` (Nei won), or `$183D`/`$183E` and the whole party's fight |
| 2 | F7 | + `loc_AF7FA` (5x3 at row 9 col 17: her eyes closed), `$36`; Power again if Nei is dead | `$27`: `$C737` = 1, `$1827`, Nei out of `party_member_id` |
| 3 | F7 | party back, camera 3 (a 0/1-pixel jitter, `loc_80D4`), latch `$82` | `$28`: `$1825` |
| 4 | Motavia $490,$4A0 | party gone, 300 frames (camera 1), `$87` | -- |
| 5 | F7 $300,$1A0 | camera 2 (0-7 px plane A, 0-3 plane B a frame, `loc_8226`), 300 frames, `$C735` = 1, `$82` | `$29`: sixteen explosions, timers 64-127 |
| 6 | Motavia | 300 frames (camera 4, the level from `$F748`), `$87` | -- |
| 7 | F7 | latch `$84` | `$2A`: explosions (0-63), `$1828`, then Paseo 2,23 with demo 6 into the clone lab (`$408`) |

The picture is on black. (Build 78 said otherwise -- "BattleScreen
DMA-fills VRAM to zero at its start and nothing clears it at its end",
so the battle's floor stayed on plane B under the picture -- and was
wrong: LevelScreen's own ClearSpriteAndScroll (ps2.asm:8490), before
loc_7E02, is a VRAM fill of $4800 bytes from $B800, which is the sprite
table, H scroll, plane A ($C000) and plane B ($E000). Fixed in build 80;
the emulator shows black.)

**The explosions** (`Obj_RandomExplosion`, 7509) are screen-relative:
bit 4 of 2(a0) makes `$A`/`$E` sprite coordinates with no camera and no
`$80` bias, so `RandomExpl_PosOffsets` cover the whole view; bit 1 hides
one until its timer runs out, then five frames eight fields each, bit 5
ahead of every row, `SXFID_Explosion` on the first, and a new spot up to
15 pixels from the base with a new count (three RNG rolls, `loc_51F8`).
`npc_spawn_explosions` is `loc_DE52`; `Npc.hidden`/`screen_fixed`/
`front` are the three bits.

`Demo_ClimatrolToCloneLab` (5518) is demo 6: six steps right, five up,
the door. The clone lab's `$408`/`$40B` path was already there.

**Routes** (both "event $25 is not ported" on build 77's game code):
`climatrol_nei_wins` -- story_gum.json on F7 at 48,23 with Nei's HP and
attack raised: the talk, YES, Nei alone, one FIGHT to the end, `$1826`,
`$1827` with `$C737` and the party without Nei, `$1825`, the reloads,
`$1828` at 48,26, the clone lab; `climatrol_nei_dies` -- Nei's stats cut,
the rest raised: Nei down, the level with `$183D`/`$183E`, the party's
fight, `$1827` on. The probe has `expect party NAME [NAME ...]` (exactly
these members, in order, by CharID_ name).

**The story** (`tools/story.py --from gum --to climatrol`): the scooter,
the gum, the passage, every chest on F2, F3, F5 and F6 (each `$1702
$1703`), then F7 and A on Neifirst. The first run ended on F4 at 45,41,
and the reason is the map, not `plan()`: Climatrol is level type 5 and
F5 has 460 cells of block `$3A`, the type-5 hole (`loc_7082`, 10382:
`level_index - $4E` clamped to 0, so one floor down with no offset) --
the grating floor the walkways cross, which the port already drops the
party through. `door_cells()` for the stairs at F5's 45,43 included the
hole at 45,41, `goto` took it as the nearest, and the party fell.
`ways_out()` now keeps `holes()` out of a door's cells; the plan is F6
stairs 24,34 → F5 → F4 → F5 → F6 → F5 → F6 stairs 2,42 → F7. The second
run reached her: Nei at L36 lost alone (`$1206`, `$183D` `$183E`), then
Rolf L19, Rudo L18 and Anna L11 lost as a party (`$1218`, the title
screen) -- the game, not a bug. A grind on F2 by the stairs was tried
first and cannot mend: `dungeon_grind`'s retreat to Kueri's hospital has
no way back on foot (the scooter and the gum are the way, and `plan()`
walks), so the stage grinds Kueri's field to `NEIFIRST_LEVEL` (26)
before riding out (`grind_and_gear`, as the earlier stages do) -- and
sixty trips there moved nobody a level. So `climatrol_grind`: F2's
landing at 15,24, eight fights a trip, and `back_to_climatrol` (a
retreat to Kueri, the hospital, the stores, the scooter, the gum, the
passage -- the gum is not eaten, ItemAction_MruraGum 14375) when the
party is hurt. `--from climatrol --redo` reruns the stage from
`story_climatrol.json`, the checkpoint a retreat's save wrote after the
chests. A Climatrol fight pays Rolf about 80 XP (85032 → 103889 over
234 fights) against 110999 for L20 and 138112 for L21, so the target is
L20 and the fight is gear and mates. The grind does a trip and fails
coming back: `retreat()` now rides ashore before Ryuka (loc_9E86 clears
jet_scooter_flag and nothing else, which would strand the scooter at
sea), but it parks on Kueri's exit landing, from which A is `$1708` --
open in NOTES. `fight_smart` and `wander` had to be imported.

## Build 79: whom to take to Neifirst, and the story through Climatrol

**The choice, from ps2.asm.** `RolfHouseOption_Reorganize` (16330)
clears the party and, while `$C737` is clear, puts Nei in slot 1 itself:
before Climatrol the party is Rolf, Nei and two of Rudo, Amy, Hugh, Anna,
Kain and Shir (15 pairs); after it Rolf and three (20 triples) -- so the
pair taken to Climatrol is two thirds of the party for the rest of the
game. Event $27's search for Nei (`loc_DDFC`) has no bound, safe only
because she is always there. The victory's EXP (`loc_F2A4`) is the total
over the *living* members plus one, to each of them: Nei alive takes a
quarter, and it is lost with her. She cannot be left dead to save it:
event $25 with her HP at 0 is `$180E` and nothing more. Her bag goes
with her.

**tools/party_trials.py** plays Neifirst for every pair: CONTINUE a
save, `members Rolf Nei A B`, Rolf and the pair raised by the real
level-up rows (`member`), outfitted from what stores 0-12 sell (Paseo to
Kueri) in a hand loadout (`outfit`), Trimates dealt into the three
fighters' bags, `warp` to F7's 48,24, `story.face_neifirst`. `--fights F`
puts each member at the level its own EXP reaches after F fights at
`--share`. New probe commands: `members NAME...`, `member NAME LEVEL`,
`outfit NAME HEAD RIGHT LEFT BODY LEGS`, `warp LEVEL X Y`, `exp`
(game.c's `game_debug_members/_member_level/_outfit/_warp`).

Two weapons beat weapon and shield everywhere (600 fights). With mates
working (below), ten repeats a point, F = F2 fights at 190 EXP each:

| pair | 0 | 150 | 300 | 450 |
|---|---|---|---|---|
| Rudo + Anna | 5/10 | 7/10 | 9/10 | 10/10 |
| Anna + Kain | 1/10 | 2/10 | 9/10 | 10/10 |
| Amy + Kain | 1/10 | 5/10 | 8/10 | 10/10 |
| Hugh + Anna | 1/10 | 5/10 | 7/10 | 10/10 |
| Rudo + Kain | 1/10 | 6/10 | 6/10 | 9/10 |
| Rudo + Amy (the story's pair before) | 3/10 | 4/10 | 4/10 | 8/10 |

The full table is `docs/party_trials_build79.md`. `fight_smart`
casts no techniques in battle, so Amy's and Hugh's healing counts for
nothing in it -- and worse (build 80): Amy and Hugh join on DEFEND
(CharStartCommandsArray) and the harness never ordered them off it, so
in every one of these fights they defended throughout. Read the table
as Rudo, Anna and Kain against two bystanders.

**tools/grind_rate.py** measures a spot: Rolf, Nei, Rudo, Anna on F2's
landing, 190 EXP a member a fight, nobody down in 30; with Nei at 0 HP,
Anna down by the eighth -- Nei grinds alive. In the Underwater Passage
beside its pipe (104,25), 114 a fight, nobody down in 30.

**Harness bugs found on the way** (none in the port):
- `give ITEM POS` puts *one* item in party position POS's bag; the
  first matrix passed a count and gave nothing. `play.give_round`.
- The battle's triangle cursor is not party order. `loc_EBF8` and
  `loc_EE00` read `Battle_CharacterIdIndex` -- 4, 0, 2, 6, byte offsets
  into `party_member_id`, the third member farthest left, one byte on
  with fewer than three -- so stop 0 is position 2. `battle_order_item`
  picked stop = position: since build 74 every in-battle item order in
  a party of three or four went through the wrong member's bag (Rolf's
  Trimate order used Rudo's slot of that number, or failed).
  `play.battle_stop`. The same trial fight went from Rolf 10/138 left
  to 75/138.
- Entering a town with the scooter owned parks it at the town's place
  (`LoadSpritesInLevel`'s tail, 862-893; Kueri y $5F0 x $0F0), and
  getting off writes it too (`loc_DA26`). The port does both;
  `board_scooter` trusted a cell remembered from before a town and
  pressed A at nothing (`$1708`). It reads the object now.
- `needs_healing` counts poison and nothing cured it: the start save's
  Rudo was poisoned, so `wander` stopped before its first step and a
  whole run's "grind" was travel fights. `play.cure_poison` (Anti, else
  an Antidote) runs before each walk.
- `fight_smart(stop_on_level=True)` hands back when the level returns
  after a fight, for scenes that follow one (`face_neifirst`).
- `needs_healing` skips `s.expendable` names (a member carried dead on
  purpose).

**The way out of Climatrol.** No member of this party learns Hinas (only
Shir), and no item store before Climatrol offers an Escapipe (each
store's count stops short of it -- Kueri's offers five of its six). So
`play.WALK_OUT`: from Climatrol or the passage, on foot to the passage's
pipe and out onto the water at 159,15, riding; `retreat` then rides
ashore, and Kueri. From F2 that walk is three floors and the whole
passage, and two runs were wiped out on it; the grind spot is the
passage beside the pipe instead. Each Kueri visit mends, buys gear, 12
Trimates and Antidotes, and saves at the Data Memory; a wipe continues
that save.

**The story.** `stage_climatrol` (`--from climatrol --redo` starts from
`routes/saves/story_climatrol_start.json`, pinned: a failed stage now
checkpoints as `story_<stage>_failed.json` rather than over its own
start): Rolf's house for Rudo and Anna, 62 trips of eight in the passage
to Rolf 22 / Rudo 22 / Anna 19, Kueri, F7, Neifirst -- Nei alone and
lost, the party and won -- `$1827`, `$1825`, `$1828`, the clone lab
(`$0408`). 792 fights, 2 with someone down, no stderr.
`story_climatrol.json` is the new checkpoint: Paseo, party Rolf, Rudo,
Anna.

## Build 80: the death of Nei against the emulator, the dams, and the party

**What the emulator showed** (the Nei-loses path), one step a reload of
loc_7E02: the picture on black with $183D/$183E, the flash and the
party's fight; her eyes closed, $1827, and the sad music from there into
Paseo; Climatrol shaking, $1825; the pump on Motavia at $490,$4A0; the
explosions; the lake full at the pump; the party among explosions, $1828;
the clone lab, $0408. `climatrol_nei_dies` and `climatrol_nei_wins` shoot
every one of those now (13 and 11 shots) and check what follows.

**On black.** LevelScreen (ps2.asm:11338) calls ClearSpriteAndScroll
(8490) before LoadLevelData: a VRAM fill from $B800 for $4800 bytes, and
VDPSetupArray (8466) puts the sprite table at $B800, H scroll at $BC00,
plane A at $C000 and plane B at $E000 -- all of it zero, every level
load. With bit 15 of event_flags skipping loc_9002, plane B stays empty
and the pictures sit on the backdrop, line 0 colour 0. Build 78 drew the
battle's floor there (`g_scene_planes_kept`, now `g_scene_no_redraw`).
The routes check it with `expect pixel 18 131 $000` -- $800, the floor's
blue, on build 79. loc_634E (9188) is not in this scene: LevelScreen uses
it instead of PaletteFadeFrom only when event_flags is exactly $E00. The
flash before the party's fight is BattleScreen's own.

**The music, by id.** UpdateSoundQueue (8547) queues d0 only if it is not
the latch ($F640), and sets the latch. LevelScreen runs LoadLevelData
(which asks for the level's music that way, 13296), LoadSpritesInLevel,
then loc_7E02:

| When | Id | Why |
|---|---|---|
| Nei alone, and the party's fight | $93 ThePlaceOfDeath | BattleScreen (12165-12179): $92, or $93 with enemy_data_buffer's high byte set -- formation $100 |
| Reload 1, the picture | $90 Power | case 1 writes sound_queue itself; the loss (loc_F24E) latched $87, so F7's Advanced was never queued |
| Reload 2, the eyes | $90 Power | case 2 queues it again with $C042 (Nei's HP) 0; Nei winning, nothing is queued and case 1's plays on |
| Reloads 3-7 | -- | only the latch: $82, $87, $82, $87, $84 -- the *next* level's own music (Motavia Restoration, F7 Advanced, Paseo Pleasure), so each reload's request matches and Power plays on |
| The clone lab | $8C | the building's |

The port queued the level's music at the end of `enter_level_walk`, after
the one-shot case -- against the latch the case had just set for the next
level -- so from reload 3 each reload restarted Advanced or Restoration
($87 $82 $87 $82 $87 in the log). It goes before the cases now, as
LoadLevelData does. The probe logs every music id that reaches the
sequencer (`music $90`) and `expect music $XX` checks the last one.

**The pump** is object $3D (`loc_580A`, 8013, no PtrObj_ label), which
SpriteData_MotaWorldMap puts at 1168,1168 -- in view from loc_7E02's
$490,$4A0. Its init (`loc_5822`) hides it (2(a0) = 2) with tile word $4437
(line 2, tile $437: art list 10's loc_5E322), mappings loc_5E516 and
hit-box row 4, and leaves routine 0 to run again until $C737 is set; then
it shows and runs `loc_585C`, frames Level_SpriteMappingsArray2 (0, 1, 0,
1) a field each -- the pumping -- until $C735 is set, then `loc_588A`,
frames 0-3. `extract_npcs.py` writes it into `scene_objects.json` as
"Pump" (**rerun `make extract-npcs`, then `make manifest`**); npc.c's
`pump_tick`. A hidden object no longer counts in `npc_in_the_way`
(RunObject gives bit 7 only to what it draws, loc_3E4), so the pump's box
does not block anyone before Climatrol. Routes: `expect object $3D
visible` at reloads 4 and 6, `hidden` in `jet_scooter_off`.

**The lake** was right already: loc_9258's $8B to $89 over Motavia's map
and four cells with $C735 set, which case 5 does before reload 6.
**Reload 5 shows the party**: case 5 does not clear $FFFFE400 and
controls_locked is only the title's, so ObjID_CharactersLevel stands at
$300,$1A0 with the followers -- as the port had it. loc_8226's shake is
added to the camera ($F71C & $FF, $F71E & $1FF), which at a fresh load
is where plane A's own camera starts too.

**The Key Tube** (reported missing): route `paseo_recorder_key_tube` --
the desk ($0D04), the Governor's $1107 (CheckItemExistInventory searches
every party bag), the library's $1010-$1014 and loc_C4CA putting the Key
Tube in the bag that held the Recorder. Nei's, in the report.

**After Climatrol.** The debrief happens on the way out of the clone
lab (demo 7 to the tower, loc_CA42/loc_CA58: $C736 and $C712); after it
the tower's desk offers ROOM, LIBRARY and OUTSIDE, not the Governor. His
$110A-$1110 send the party to open the four dams.

**The dams' panels.** ItemAction_GreenCard .. RedCard (14406-14446): the
card faced at its own door index ($1C, $1E, $20, $22) opens the door
(demo 5, as before); at the next index up, the dam's panel (object $38, an
invisible block with box row 7: Green Dam F1 113,28, Blue Dam F3 21,32,
Yellow Dam F2 51,24, Red Dam F2 45,20), `loc_9D70` sets bit 0 of
event_flags + index ($C72D, $C72F, $C731, $C733), `move.w #8,
(event_flags).w`, $18, and removes the card; the next visit is $1B and
SXFID_DamOpened. The dam's own map does not change; Motavia's rivers do
at its next load (loc_9258). The low byte 8 is loc_809C's case 8,
`loc_81E6`: once no window is up or opening, if the four words
$C72C-$C732 are all $0101 -- every door and panel -- enemy_data_buffer =
$101 and the battle; event_flags = 0 either way. MenuState.event_flags
carries the write to the level screen. Routes: `green_dam_panel` (one
dam: $171B on A, $18, $1B, $C72D set, no battle) and `army_eye_capture`
(the other three set in the save: the battle with $93, the capture,
Gaira with $88).

**The Army Eyes** (formation $101: three of them, 3000 HP, defense 104)
cannot be beaten: their technique (`loc_2E24`; battle_obj.c had it as
Neifirst's) counts `$CC92` on every use and the ninth -- round three --
is $1219 and the party carried to Gaira at $50,$2B0 with
`move.w #$A00, (event_flags).w`. The port now keeps the $A00: loc_7E02
case $A (`loc_7FDA`) on arrival, $DE70 = 1, $DE6E = $2D. Event $2D
(`loc_DF32`) is the prison: $1839, a 30-second demo_timer with camera
case 6, and every character's equipment and bag replaced; it and what
follows ($2E on) are the next thing to port, and the route stops at the
"event $2D is not ported" line.

**Choosing the party.** CharStartCommandsArray (the port's
`start_command`) is 0, 0, 0, 3, 3, 0, 0, 3 by CharID: Amy, Hugh and Shir
come into the party on DEFEND, and `loc_F4F8` puts only a used technique
back to attack, so a defend lasts from fight to fight until an ORDR
changes it. `fight_smart` pressed FIGHT and nothing else: in every trial
and story fight before this build Amy and Hugh defended throughout. Now,
at each round's menu, it orders anyone on DEFEND to ATTACK, and gives
whoever is furthest under half their HP a mending technique
(`plan_heal`: RES/GIRES/NARES, the cheapest that covers the deficit, from
any member with the TP) before falling back to mates. The probe's new
`techs` line gives each member's stored command and battle techniques
(NAME:TP:flags); `play.battle_order` is ORDR for any command.
`battle_list_pick` looked for the item list's window after NEXT, which
sent every technique on a second page back out of the list.

The next boss cannot be fought for a result, so `tools/dam_trials.py`
measures the next dungeon instead: every party of Rolf and three of
Rudo, Amy, Hugh, Anna and Kain (ten), each raised to the level its own
EXP reaches after G grind fights at 190 a fight, outfitted, four
Trimates, forty fights on Red Dam F2 with fight_smart and wander's
between-fight RES and mending. The measure is **fights per trip**: fights
won before the run ended -- a wipe, or someone under half HP with no
mate and no TP left to mend -- out of forty, the mean of twenty runs.
From the Climatrol checkpoint (`docs/dam_trials_build80.md`):

| grind fights | 1st | 2nd | 3rd | last |
|---|---|---|---|---|
| 0 | Rudo+Amy+Anna 16.2 | Amy+Hugh+Anna 11.5 | Rudo+Anna+Kain 10.9 | Amy+Hugh+Kain 3.8 |
| 150 | Rudo+Amy+Anna 19.1 | Rudo+Hugh+Anna 17.4 | Rudo+Anna+Kain 15.2 | Amy+Hugh+Kain 7.8 |
| 300 | Rudo+Amy+Anna 22.6 | Rudo+Hugh+Anna 18.3 | Rudo+Anna+Kain 17.5 | Amy+Hugh+Kain 8.7 |
| 450 | Rudo+Amy+Anna 22.5 | Rudo+Hugh+Anna 21.8 | Amy+Hugh+Anna 18.1 | Amy+Hugh+Kain 8.8 |

Rudo+Amy+Anna first at every grind cost, with no grind needed to lead;
Anna in every top party; Kain (L10) and Hugh (L13) weigh a party down
until ground up. A run of the same matrix on build 79's checkpoint
ranked it first at 0, 300 and 450 and second at 150, half a fight
behind Rudo+Hugh+Anna.

**The story.** `stage_climatrol` replays from its pinned start with the
new fight_smart (795 fights, 2 with someone down, no stderr), and the
new stage `dam_party` checks $C736/$C712, takes Rolf, Rudo, Amy and Anna
from Rolf's house, heals and saves: `story_dam_party.json`. `pick_party`
picked `party[2:]` -- REORGANIZE puts Nei in slot 1 itself, but only
while $C737 is clear -- so after Climatrol it left a member out; it
picks all three now.

**The probe** gains `wait level L`, `expect music $XX`, `expect object
$XX visible|hidden|absent`, `expect pixel X Y $BGR` and `techs`; the
`objects` line marks hidden objects (`h`) and gives each one's frame
(`#N`). `audio_debug_on_sound` is the hook the music log uses.

**Routes** (49 as of this build): new `paseo_recorder_key_tube`,
`green_dam_panel`, `army_eye_capture`; `climatrol_nei_dies`/`_wins` and
`jet_scooter_off` extended. Each new check fails on build 79's code:
the floor pixel, the music at $1828, the pump's `expect object`, the
panel's flag and the Army Eyes' battle.

## Build 81: the Gaira prison, and Tyler's spaceship

**The prison** (ObjType_Events $2D, `loc_DF32`, ps2.asm:20448 -- $2C is
the same routine). loc_7E02 case $A starts it on arrival with `$DE70` = 1.
It shows $1839 in the big box and bumps `$DE72` (the next idle visit
closes it), sets `demo_timer` = $708 and `event_flags` = 6 -- the high
byte 0, so A still works; the low byte is camera case 6. loc_6D5A four
times copies all eight $40-byte character records from $C000 to $DA00;
loc_ADB0 takes every character's equipment bonuses off; then for each of
the eight: TP 0, only $7B PrsnClths worn, the bag a $0C PlsmRing and the
PrsnClths ($FB), and SetCharEquipment. HP and techniques are not touched.
`gamedata_prison`; $DA00 is a static copy in gamedata.c and is not in the
save, as in the ROM (nothing in Gaira can save).

**Camera case 6** (`loc_815C`, 11730): demo_timer counts only while
`$DE70` is 0, `window_index` is 0 and the window stack is empty -- the 30
seconds start once $1839 is closed, with the party free to walk. At 0:
`$DE70` = 1, `$DE6E` = $2E, `event_flags` = 0. The harness counts the
leader as under the pad during it.

**$2E** (`loc_DFA8`): SXFID_Explosion, demo_timer = $78, `$DE70` + 1; the
next visits count it down; then `$C73E` = 1 (a0 is event_flags + $2E), the
big box, $1812 ("ROLF hears a large explosion ... fix the Gaila's
orbit!"), `$DE72` + 1. `$C73E` was already what drifts Gaira's backdrop
and sounds its alarm.

**$2F** (`loc_DFE6`): Gaira's one object, SpriteData_Gaira's $37 (an
invisible block) at $360,$1A0 with box row 9 -- cells 52-55 x 22-25, so it
is faced from row 26. No test of d2 or `$C73E`: `$C73F` = 1,
BuildingID_GairaControlPanel with portrait $1B (the Radar), and
ScreenID_Building; BuildingScreen's clear of $DE00-$DFFF drops the event.

**The ring.** On Gaira, ObjCL_Main (5186) swaps the leader's mappings
every frame between loc_12B06 (the plain 16x32) and loc_12B36: tiles
+$22 at y-16 ahead of the body, the body, tiles +$20 at y-24 behind it.
Each follower takes the mappings of the object before it
(ObjFlwngChar_Main), and art list 13 puts loc_5D12C, a ring, at $320,
$328, $330 and $338 -- $20 on from each party slot -- so every member has
a ring round the waist every other frame. `render_walker`; checked with
`expect pixel 160 125` on two frames running ($008 floor, $0AAA ring).

**Building_GairaControlPanel** (18668): $1901; 300 frames; SXFID_
Explosion; PaletteFadeFrom and $E0; 300 frames; then Tyler's spaceship
(portrait $1C) with screen_changed_flag. loc_66F6 (9527) is its palette:
while event_routine < 4, every eighth frame, SXFID_Alarm on step 0 of 16,
the backdrop $00E (red) for eight steps and black for eight, line 2
colour 5 through loc_6790.

**Building_TylerSpaceship** (18715): BuildingScreen's loc_8326 clears
VRAM and starts it at event_routine 2 with no portrait. $1902, two
pictures (loc_70A32 over loc_6D7BA, loc_70D42 over loc_6F040) of 300
frames each with $1903 and $1904, Tyler and the monitor with $1905-$1906,
$1907, TylerSpaceShip_EventFrames a step every fourth frame (with the
MonitorExplosion object at $FE), $1908-$190A, then **loc_CE5E**: loc_ADB0,
HP = max HP and TP = max TP for all eight, the five worn slots, the bag
count and the sixteen bag slots from $DA00, SetCharEquipment; Paseo at
$3D0,$170 on foot, the Governor (portrait $17) at the end of demo walk 8
(Demo_TylerSpcToTower, checked byte for byte). `gamedata_prison_release`.
**The pictures, Tyler's portrait and the monitor are not drawn yet**: the
screen is black behind the text (stderr says so once); the words, the
waits, the monitor's palette cycle and what happens to the party are
ported.

**The harness.** `debug_event_sig` now counts the buildings' demo_timer,
the monitor's frame wait and a building's own fade, so `wait idle` rides
through the panel's and Tyler's timed stretches instead of calling them
idle.

**Route** `army_eye_capture` goes on from Gaira's arrival: $1839, the
bags swapped (`expect item PrsnClths 0`, `PlsmRing 0`), $C73E still 0
after 1700 ticks, $1812 and $C73E set, then a `warp` to 53,26 below the
panel -- the walk there is 159 steps with fourteen damage floors and
Gaira's own encounters in prison clothes (no weapon, $1205; no TP), so
only RUN, which is the game -- A on it, $C73F, $1901 with the red alarm,
$1905-$190A, Paseo, the Governor's $1111, the street at 31,16 with every
bag and every worn piece back and HP/TP full. It fails on build 80's code
at `expect script $1839`. 49 routes, all passing.

**The story, after the prison was ported** (same build, second pass):
two new stages in `tools/story.py`.

`cards` (from `story_dam_party.json`, checkpoint `story_cards.json`):
Control Tower F1's four boxes are behind the shutter at 47,15
(Obj_OpenableDoor, index $24), and no ItemAction opens index $24 -- only
Musik does: TechEffect_Musik (ps2.asm:15597) faced at the keyboard (event
7, 47-48 on row 26) sets `$C734`, and demo 5 slides it open. So the stage
takes Rolf to Ustvestia in Oputa (7,7: NO to his playing, YES, YES, Rolf,
2000 meseta, $A0C), then the scooter to Piata (it is not on Paseo's side
of the water; ashore by its record 83,91), Piata's stores, the tower's
door (record $3E, which play's door list leaves out: stand at 9,24 and
push up), the keyboard from 47,27 with Musik (Rolf's last map technique),
the four boxes ($1702 $1703 each), and Piata again. 18 fights, nobody
down.

`dams` (from `story_cards.json`): does **not** finish yet. The Green Dam
works -- the scooter's push lands on GreenDamGroundF at 93,17, the card
on the door ($171B, $0018, demo 5), F1's panel ($171B, $0018, $001B,
`$C72D` set) -- and the party (Rolf 23, Rudo 22, Amy 18, Anna 19, no
grind since Climatrol) is wiped walking back to the way out, twenty
fights in. That matches `docs/dam_trials_build80.md`: this party at no
grind lasts about sixteen dam fights a trip. `leave_dam` (the arrival
cell, then a step down onto the water: no Escapipe is sold yet, nobody
knows Hinas, and `retreat()` cannot plan across the water) and `to_piata`
are there for the trip home once the party can make it.

Harness fixes on the way: `ride_into` returned False when the ride's own
last push had already gone into the dam; `goto` takes the fight policy
from `s.fight_opts` (the Gaira walk RUNs every fight).

## Build 82: Tyler's spaceship's pictures

Built on 81c; there was no 82 before this one.

**The pictures** (`Building_TylerSpaceship`, ps2.asm:18715). All go to
plane A in the dream's place (`$410C0003`: row 2, column 6) with their
art at tile 1:

- `loc_CC8E`: `loc_70A32` -- `DreamBattleTileInd`, the dream's fight --
  over `loc_6D7BA`, `loc_6BBE` on `$FA` tiles; `loc_CCCC` takes it off
  with `loc_6C54` on the same count.
- `loc_CCE6`: `loc_70D42` (28x14, line 3, the map between the dream's
  two) over `loc_6F040`, `loc_6BBE` on `$96` tiles, and only then
  `$1903` ("Help!!!"). `loc_6F040` is 148 tiles: the last two of the
  150 come from whatever the RAM buffer held, and the map never names
  them.
- `loc_CD50`: palette `$2A` (`Pal_CentTow2Port`, lines 2-3) by
  `PaletteLoad1` and `PaletteLoad2`, live and target (build 81 did not
  load it); rows 2 and 15 cleared; `TylerPortraitTileInd` 13x12 at row 3
  column 6; `loc_CDDE` frame 0 at row 3 column 19; `TylerSpaceshipArt`
  by `loc_6BBE` on `$B4` tiles; `$1905`.
- `loc_CDDE` draws frame d0 of `TylerSpcshp_MonitorTileInd`, frames
  `$168` bytes (15x12 words) apart, five of them. `loc_CDF8` draws 1;
  `loc_CE20` a positive `TylerSpaceShip_EventFrames` byte, `$FE` puts
  `ObjID_MonitorExplosion` in `$E800`, `$FF` moves on. Build 81 stepped
  the table and drew nothing.

`loc_6BBE` is 64 times: four `WaitForVBlank`s with `vblank_routine` `$24`,
then `FadeArtTiles`. `VBlankSub_1C_24` is the joypad and the CRAM and
sprite DMAs only -- no `ProcessWindows`, no `RunObjects`, no `loc_66F6`
-- so the screen is frozen for 256 frames but for the tiles. A
`FadeEffectArray` entry is a pixel index, row by row (bits 0-1 the nibble
of its word, the rest the word); `FadeArtFill` ORs that pixel of every
tile in. `loc_6C54` is 32 times: one VBlank, then `loc_6CA4` ANDs byte
(entry >> 1) of every tile away. The first 32 entries name all 32 bytes,
so the tiles come out blank, which is why a later dissolve can OR into
them.

In the port the building returns `BUILDING_PICTURE` with `bs->fx`;
`tick_building` runs `picture_fx_step` a frame at a time with nothing
else moving, then that pass's `CheckPrepareWindows` and `loc_66F6`, as
the dream's effects do on the intro screen. `render_tyler` draws the map
the view stands for. The dream's `dream_erase_step` takes a tile count.

**The explosion** (`Obj_ExplosionTylerSpaceship`, object 7, ps2.asm:2471).
The init -- `$118,$98`, `2(a0)` = `$30`, tile word `$6000`,
`Map_PalmExplosion`, `$26` = 3 -- falls into the main routine the same
call: `$26` down, below zero back to 3 and `$24` + 1, and at 6 `2(a0)` = 1.
`RunObject` clears a slot with bit 0 of `2(a0)` set in that same call
(`loc_3DC`), so `BuildSprites` never draws the seventh step: 3 + 4 x 5 =
23 frames. `Map_PalmExplosion` (32032) has six entries, mappings 1, 1, 2,
2, 3, 4; the last has 33 pieces, so `NPC_SCENE_PIECES` is 33 (it was the
jet scooter's 6). `extract_npcs.py` reads it with `parse_scene` like any
scene object; `npc_scene_type` hands it to the building screen. The
monitor's palette cycle in `loc_66F6` starts at event_routine `$A`
(`cmpi.w #$A` / `bcs`), after the explosion, so it draws in
`Pal_CentTow2Port`'s line 3 as loaded.

**Not drawn yet: Tyler and the monitor.** Both maps are bincludes
(`art/tyler_port_tile_ind.bin`, `art/tylr_spc_monitor_tile_ind.bin`) and
this session had no `art/`. `make extract-nametables` with `ART_DIR`
writes `TylerPortraitTileInd.nam` and `TylerSpcshp_MonitorTileInd.nam`
(15x60, the five frames stacked); `game.c` draws them when they are
there and says once on stderr when they are not. `assets/tiles/
tylr_spc_monitor_tile_ind.png` is `extract_art` decoding the map as if it
were compressed art -- five garbage tiles -- and nothing uses it. If the
monitor map's cells turn out to have priority set, the explosion (priority
0) goes behind them as it would on the console, and its route checks will
need to move.

**Checked.** Every drawn pixel of both pictures in the port's shots
against a composition straight from ROM data (the map, the decompressed
art, the colours from `Pal_TitleScr`'s `dc.l`s): 0 of 25088 and 0 of
14507 differ. Every pixel of all four explosion mappings against
`Map_PalmExplosion`, `TylerSpaceshipArt` and `Pal_CentTow2Port`: full
match, shown for 7, 8, 4 and 4 frames.

**Extractors.** `extract_inline_art` (`loc_6F040` -> `DreamHelpArt.png`),
`extract_nametables` (`loc_70D42` -> `DreamHelpTileInd.nam`, the two Tyler
maps; a missing `art/*.bin` now skips its table and keeps the old `.nam`
and json entry instead of stopping the run), `extract_npcs`
(`MonitorExplosion` in `scene_objects.json`). To regenerate:

    make extract-inline_art extract-nametables extract-npcs
    make manifest

Everything else the three write came out byte-identical to 81c's.

**Harness.** `wait script $XXXX`: until that script reaches a window, on
`expect script`'s cursor, which it moves past the match.

**Route.** `army_eye_capture` shoots the dream's fight 450 ticks after
`$1902`, the second picture at `$1903`, and the explosion 46, 53 and 57
ticks after C on `$1907` (mappings 2, 3 and 4), with eleven `expect
pixel`s. It fails on 81c at line 143 (`$000`, the black screen). 49
routes, all passing.

## Build 83: the dams, the Army Eyes and Gaira, played

Built on 82. The story now runs from `story_cards.json` to the Governor's
`$1111` in Paseo with no level cheats:

    python3 tools/story.py --from cards --to dams

**The party is not permuted.** `docs/dam_trials_build80.md` ranks
Rudo+Amy+Anna first at every grind cost (16.2 fights a trip at 0, 19.1 at
150, 22.6 at 300, 22.5 at 450), and that is the party `dam_party` took.

**`dam_grind`** (new stage, checkpoint `story_dam_grind.json`). Motavia's
field by Piata's gate, 94,85, left and right three cells: 20 fights
measured there paid 9336 EXP a member -- about 467 a fight (formations
$7D and $81-$84), two and a half times Climatrol F2's 190 that the trials
priced grind in -- so the trials' 450-row levels, Rolf 24, Rudo 24, Amy 20,
Anna 22, are about 180 fights. Up from the arrival cell 91,82 is Piata's
door, so `grind()`'s four-way walks go straight back in (a first try got no
fight at all). Trips of ten with the hospital between: 17 trips, 170
fights, nobody down, no stderr. Amy (18 to 20) and Anna (19 to 22), the
newest, gained the most levels.

**Piata sells the Escapipe** (item store, building 7, stock 13: Monomate,
Dimate, Trimate, Telepipe, Escapipe). `piata_supply` runs before every dam
trip: the clone lab for anyone dead, the hospital, 12 Trimates and 2
Escapipes by `stock_item` -- every bag already held eight, so `stock_mates`,
which fills only the first page, would have bought nothing -- and a save at
Piata's Data Memory, so a wipe continues there.

**The Escapipe out of a dam, from ps2.asm.** The scooter's push into a dam
goes through the door list as a step does (`JetScooter_ChkCanMove` ->
`loc_6E4E`), and `loc_6F1C` (10277), for a record from a level under $10
to one at $10 or more, writes `$C64C` = the level and `$C64E`/`$C650` = the
leader's `$E40E`/`$E40A` -- which while riding are the scooter's
(`loc_4AE2`). `ItemAction_Escapipe` (14544) loads those three and does not
touch `jet_scooter_flag`: the party comes out on the scooter at the dam's
mouth. The port does the same (walk.c `interact()`, menu.c
`use_escapipe`): Green Dam to 65,43, Blue to 92,65, Yellow to 43,65, riding
each time, which is where each push went in (records 47,65 / 65,91 /
65,45). `out_of_dam` uses it; `leave_dam`'s walk is the fallback.

**`dams`**: `open_dam` is resumable -- the door's flag (`$C72C` + index -
$1C, the panel's less one) set by an earlier trip skips the door -- and
returns "done", "hurt" or a failure; `stage_dams` sets `s.careful` for it,
and on "hurt" takes the party out by Escapipe, back to Piata, resupplied,
and in again (eight tries a dam). This run needed one trip a dam: Green,
Blue, Yellow and Red, each `$171B $0018` at the door and `$171B $0018
$001B` at the panel; the fourth panel's `loc_81E6` starts the Army Eyes,
`$1219`, Gaira, `$1839` (running on into `$183A`: it ends `{pause}{cont}`),
`$1812`, the panel at 53,26, `$1901`-`$190A` and `$1111`.
`story_dams.json`: Paseo, Rolf 25, Rudo 24, Amy 21, Anna 23, every bag and
worn piece back. 90 fights, nobody down, no stderr.

**Harness: `goto` after a damage floor.** A damage floor takes the pad for
a few frames, so a `walk` over one ends short ("the game took the pad
after 2 of 20 steps") and a `wait idle` later the party is back under the
pad -- on a cell the rest of goto's plan did not start from. goto went on
with the next run from there and stood blocked for 120 ticks, over and
over: the first pass had 31 such blocks on Gaira's 159 steps (fourteen
damage floors), with Gaira's encounters coming all the while, and Amy and
Anna reached the panel dead. goto now re-plans whenever the party is not
where the run should have ended; the second pass had none on Gaira and
nobody down.

**What the first pass showed that is the ROM's.** Amy and Anna died on
Gaira and stood at Tyler's with full HP. Death is HP 0 -- every test is
`tst.w 2(a1)` on the record (`Enemy_PickCharToAttack`, 4044;
`CheckPlanetAndCaps`, 19192; Shir's, 19111) -- and `loc_CE5E` copies max HP
to HP (`move.w (a0), -(a0)` at record + 4) for all eight, so the dead get
up; the status word at + 0 is not touched, in the ROM or in
`gamedata_prison_release`.

**Probe**: `expect riding yes|no`.

**Route** `dam_escapipe`: from `story_cards.json`, what play.py sends to
board the scooter at Piata and ride into the Green Dam, replayed; then an
Escapipe: `$0012`, Motavia 65,43, riding, `Obj_JetScooter` visible,
`01_dam_mouth.png`. It passes on build 82's code too -- it is a record of
the ROM's behaviour, not a fix.

**Route** `gaira_dead_member`: army_eye_capture's steps (its shots and
pixel checks left to it) from `gaira_dead_member.json` --
`green_dam_last_panel.json` with Anna at 0 HP -- checking `expect hp 2 0
0` in the prison and 111/111 (and Rolf's and Rudo's max) at home, with a
shot of each. It too passes on 82's code: the ROM's, recorded. On arrival
at Gaira the followers stand on the leader's cell, one sprite, as in
army_eye_capture's `05_prison`.

No port code changed in this build; the one fix (goto) is the harness's,
and the story run above is its check. **Fuzz**: `fuzz_levels.py --ticks
20000 --seeds 1,2,3` under ASan on Motavia, Piata, the dams' floors
($24-$31) and Gaira ($41): 51 runs, no failure, no sanitizer report, no
stderr line (5 fights -- the fuzz's short walks rarely meet one on those
floors). 51 routes, all passing.

## Build 83a: Tyler and the monitor, checked

Same session as 83; NOTES 0a. The upload had `art/tyler_port_tile_ind.bin`,
`art/tylr_spc_monitor_tile_ind.bin` and `art/tyler_spaceship_art.bin`.
`make extract-nametables` and `make manifest` with them changed nothing:
`TylerPortraitTileInd.nam` and `TylerSpcshp_MonitorTileInd.nam` were
already in the tree, byte-identical, and `09_tyler` has shown both since
build 82's tree was made. What was left was to look and to check.

**Priority.** Neither map has a cell with bit 15 set (156 and 900 words);
Tyler's are all line 2, the monitor's lines 0 and 3, 152 of them flipped.
So Palm's explosion (priority 0) draws over the monitor as the port has
it, and 09c-09e's checks stay where they are.

**The frames, from ps2.asm** (`loc_CD50`-`loc_CE52`, 18789-18898).
`loc_CD50` draws Tyler 13x12 at `$418C0003` (row 3, column 6) and frame 0
by `loc_CDDE` at `$41A60003` (row 3, column 19; frames `$168` bytes
apart), sets `$F764` = 0 and `$F766` = 3. `loc_CDF8` is `$1907` and frame 1.
`loc_CE20` counts `$F766` down and steps `TylerSpaceShip_EventFrames` when
it passes zero -- a step on the 4th, 8th, ... call: `$01`, seven `$00`,
`$FE` (the explosion, into `$E800`), six `$00`, then `$02 $03 $02 $03 $02
$03 $02 $04 $02 $04` and `$FF` (event_routine + 1, `loc_CE52`, `$1908`).

**Checked.** Every drawn pixel of Tyler and of each monitor frame,
composed from the two maps, `tyler_spaceship_art.png` (VRAM tile 1 on,
`$40200000`) and the lines the words name, against the port's shots:
Tyler 0 of 9984 differing in every shot; frame 0 at `$1905`; frame 1 from
2 to 34 ticks after C on `$1907` and again at 60; the explosion over it
from 36 to 58 (23 frames); then 2, 3, 2, 3, 2, 3, 2, 4, 2, 4 on ticks 64,
68, ... 100, frame 4 thereafter -- each 0 differing, one shot every other
tick. The port follows the table to the tick.

**Route** `army_eye_capture`: four pixels on `09_tyler` (three of Tyler,
one of frame 0) and shots with a pixel each for frames 1-4 (`09f`-`09i`,
at 20, 66, 70 and 94 ticks after C: `$620`, `$EEE`, `$00E`, `$6EE`, each a
colour no other frame has there). With the two `.nam` files moved away it
fails at line 155 (`$000`); with them it passes. 51 routes, all passing.
README's "Not done yet" entry for Tyler is gone.

## Build 84: the Central Tower's roof, and the flight

Built on 83a; NOTES 0b's port. Routines read first: BuildingScreen's roof
case (`loc_8318`, 11889), `Building_CheckRoutine` (16100), `Building_Roof`
(18055-18148), `Obj_Spaceship` (8138) and its path `loc_59E4`,
`VBlankSub_04_08`, `PaletteFadeTo`/`PaletteFadeFrom`, `PaletteLoad1`/`2`,
`ClearSpriteAndScroll`, `LevelScreen`'s head.

**Where the dams stage left it** (NOTES 0b until build 85, moved here).
`$1111` is Building_CentralTowerGovernor's `loc_C98E` with `$C736` set --
script `$1111`, event_routine + 6 (`loc_CA64`: demo off, windows closed),
Paseo `$100,$1F0`, jet_scooter_flag 0, no flag written. The script sends
the party to "the spaceship on the rooftop": ROOF on the tower's desk
(`$C73F`, set by Gaira's `$2F`) is building 11, `Building_Roof`
(ps2.asm:18055), which building.c had no case for before this build
("building: 11 is not ported").

**How Skure's side is reached.** `Building_CheckRoutine` dispatches on
`event_routine - 1`, so event_routine 2-6 are `RoofEventIndex`'s five
entries. `loc_8318` skips `RolfPortraitArt` and `pal_id_rolf_port` for the
roof (line 1 stays palette 2's) and, on DezolisSkure, adds 5: from 1 to 6,
`loc_C578` on the first idle pass -- no portrait window, no question, the
flight to Paseo `$100,$1F0`. Skure's door record (79,59) is building 11,
portrait `$18`. NOTES 0b's "routine 5" is event_routine 6 in these terms.

**The roof** (`building.c roof()`): 2 `WinID_ScriptMessage2` and `$F01`
(three pages); 3 `WinID_YesNo2`; 4 NO sets `event_routine_sub`, YES writes
`$F02` (`move.w`, into the open window), DezolisSkure `$4F0,$3C0`; 5
`loc_C572`, PaletteFadeFrom then the flight; 6 `loc_C578`. `loc_C500`: sub 1
is `$F03` and + 1, anything else CloseAllWindows. No flag is written and
`jet_scooter_flag` is not touched.

**The flight** (`loc_C58A`, `SpaceShipLoop`; game.c `flight_begin`,
`tick_flight`, `spaceship_tick`, `render_flight`). ClearSpriteAndScroll
fills VRAM `$B800-$FFFF`: both planes and the sprites. `loc_71362` at tile 1
under `loc_716BA` (6x6, plane B row 7 col 22, line 3); `loc_71702` at tile
`$400`; ObjID_Spaceship (`$44`) in `$EC00`; PaletteLoad1 (the target) `$39`
if level_index -- the destination by now -- is DezolisSkure, else `$3A`, both
line 3, and `$3C`, line 2; demo_timer `$78`; SXFID_SpaceshipDeparted (`$AE`);
PaletteFadeTo, which zeroes all 64 live entries and runs 21 frames on
vblank `$24` (no objects, no demo_timer). Then 120 frames of CheckGamePause,
vblank 8 (demo_timer - 1), RunObjects, BuildSprites; ScreenID_Level and
SXFID_SpaceshipLanded (`$AF`); LevelScreen fades it out. `Obj_Spaceship`:
init (`2(a0)` `$10`, tile word `$4400`, `loc_71DEA`, `$24` = -1) falls into
the main routine; every fifth frame the next mapping and the next `loc_59E4`
pair + `$80` into `$A` (x) and `$E` (y); at `$24` = `$A` the slot is cleared
in that call, so mappings 0-9 show five frames each and the eleventh never.
The harness counts the flight as busy.

**Found on the way: `palette_load` wrote 256 entries.** Every palette PNG's
PLTE is 256 long (what editors write), and `palette_load` copied all of it
from the line it was given: `$3C` to line 2 zeroed line 3, so the planet
came out black. PaletteLoad1/2 write only the words PalettePtrs names; the
loader now takes one line per image row. `make routes` was 51/51 before and
after that change alone.

**Checked.** A route shooting every tick from C on `$F02`, against a
composition straight from ROM data (`loc_71362`/`loc_71702` decompressed from
their `dc.b`, `loc_716BA`, `loc_71DEA`'s records, `loc_59E4`, `loc_127C4` and
`loc_12804`): 7 fade-in frames with the planet's indices exact, then 14 with
the final palette, then mappings 0-9 five ticks each and 70 of the planet
alone -- every drawn pixel 0 differing, both lines' colours equal to the
words. The Paseo-bound flight is the same code with `$3A`, checked by pixel.

**Extractors**: `extract_inline_art` (`SpaceshipPlanetArt.png`,
`SpaceshipArt.png`), `extract_nametables` (`SpaceshipPlanetTileInd.nam`),
`extract_npcs` (`Spaceship` in `scene_objects.json`). To regenerate:

    make extract-inline_art extract-nametables extract-npcs
    make manifest

Everything else the three write came out byte-identical to 83a's.

**Routes.** `roof_yes`: the desk, ROOF, `$F01`, YES, `$F02`, the flight
(mappings 0, 1 and 9 and the planet alone, eight pixel checks), Skure 60,79;
then left into Skure's door, the flight back (`$3A`: the planet's colour 5 is
`$6E2`), Paseo 31,16. `roof_no`: NO, `$F03`, the street, `$C73F` still 1.
Both fail on 83a at `wait screen building` ("building: 11 is not ported").
53 routes, all passing.

**The story to Skure** (same build, second pass). `tools/story.py` stage
`roof`, from `story_dams.json`:

    python3 tools/story.py --from dams --to roof

`visit()` on the tower's door (15,32) with a policy of ROOF on the desk's
menu `$36` and YES on `$1F`: `$0D01 $0F01 $0F02`, the flight, DezolisSkure
60,79. No fights, no stderr. Checkpoint `story_roof.json`: Rolf 25, Rudo 24,
Amy 21, Anna 23, meseta 843336, as story_dams left them; `continue_game` on
it lands on DezolisSkure 60,79 under the pad.

**Probe: `save-here N`.** `checkpoint()` saves at the town's Data Memory,
or Paseo's. On DezolisSkure neither will do: the overworld has no Data
Memory, and one on Dezolis without a Magic Cap is `$810` and the street
(`CheckPlanetAndCaps`, `loc_B44A`), while going back to Paseo to save
would undo the stage. `save-here N` writes slot N in the route's save
directory as `ItemAction_Visiphone` would have Data Memory write it --
`$C65C` = 1, `$C65A` the planet, `$C646`-`$C64A` the level and the
leader's cell, `$C684` Rolf's level, `$C69C` = `$FFFF` -- so CONTINUE skips
`CheckPlanetAndCaps` and lands where the party stood (the path
`dezolis_prism`'s save already takes). It is the harness's: the party
carries no Visiphone. `checkpoint()` uses it on level 3 only.

**Sanitizers and fuzz.** `roof_yes` and `roof_no` under
`route_probe_asan`: no report. `fuzz_levels.py --ticks 20000 --seeds 1,2,3
--levels 3,4`: 6 runs, no failure, no sanitizer report, no stderr line, 7
fights; Skure's runs reached Paseo (`$04`), which from DezolisSkure is the
roof's door and the flight home. 53 routes, all passing.

## Build 85: the Dezolians and the musk cats

Built on 84; NOTES item 4. Routines read first: `Obj_Dezolian`
(ps2.asm:6083-6287: init `loc_3FF8`, decision `loc_402C`, step
`loc_4234`), `Obj_MuskCat` (6482-6690: `loc_456C`, `loc_45A0`,
`loc_475C`), `Level_SpriteMappingsArray`/`3`, the mappings `loc_59EBA` and
`loc_5A394`, `Sprites_ChkCanMove`/`loc_6DA4` (10103),
`Level_ChkTargetInteract` and `loc_7278` (10503), `RunObjects`/`RunObject`
(182), `BuildSprites` (343), the level loop (`LevelScreenLoop`),
`loc_5F74`, `CharSprites_ChkMove*` (5254), `ProcessAButAction` (19806),
`ObjType_People` (20946) and `CheckPlanetAndCaps`.

**Obj_Dezolian (`$1A`).** Thirty placements: Aukba 10, Zosa 8, Ryuon 12.
Init: tile word `$2400` (tile `$400`, line 1: art list 2, `loc_59272`),
mappings `loc_59EBA` -- twelve frames of two pieces, 16x32 at y-40 over
16x8 at y-8, so `NpcType` now holds up to two pieces a frame -- box row 4,
first wait `$28`, `$2A` = 5 (the mappings' high word, as for the others).
The decision, `loc_402C`, is not `$1B`'s:

| roll | `$1A` (`loc_402C`) | `$1B` (`loc_42F8`) |
|---|---|---|
| turns | 1 up, 2 down, 3 left, 4 right | 1 down, 3 up, 5 left, 7 right |
| step up | 8/9: `$2A` 0; `$35` to -5; `loc_6DA4` two cells up, then one up | A/B: `$2A` 1; `$35` to -3 |
| step down | A/B: `$2A` 3; `$35` to 5 | C/D: `$2A` 4; `$35` to 3 |
| step left | C/D: up-left cell (d6 as found), left cell (d6 2), *then* the turn, then `$34` | 8/9: turn, `$34`, left cell |
| step right | E/0: the mirror | E/F |
| wait | `$28` | `$30` |
| walk frames | 0,1,0,2 (`Level_SpriteMappingsArray`), every 5 | 0,1,0,1 (`Array2`), every 4 |

**The sideways check's d6.** `loc_4164` and `loc_41C8` call
`Sprites_ChkCanMove` for the cell above-left/right with no `moveq` into
d6, and `Level_ChkTargetInteract` uses d6 as the row of `loc_7278` for the
mover's box. What d6 holds is whatever last touched it:

- Between one frame's `BuildSprites` and the next frame's `RunObjects`
  nothing does (`ProcessAButtonPress` aside, below): `CheckGamePause`,
  `WaitForVBlank`, `CheckPrepareWindows`, `RenderCharSprites`,
  `loc_809C`, `ProcessRandomBattle`, `loc_6430` leave it alone, and
  `loc_5F74` is the stepped art decompressor, only running with `$F7C0`
  set. `BuildSprites` sets d6 to 2 on every row with anyone in it and adds
  2 an object, so it ends at 2 + 2 x the objects in the last row it drew
  -- RunObject files by `(~($1E - $80) & $FC) / 4`, so that is the topmost
  one on screen: 4 when someone is alone up there.
- Row 4 is an object's own box, and the scan runs from `$E400` over every
  slot with nothing to skip `a1 = a0`: a Dezolian drawn last frame hits
  *itself*. Rows 4-13 all contain the object's own box. So an on-screen
  Dezolian's sideways step is refused, without a turn, unless something
  earlier in the same `RunObjects` left d6 elsewhere: the leader
  (`$E400`, before `$E800`) starting a step (`CharSprites_ChkMove*`: its
  `moveq`, then `$FFFF` after a clean scan of `$E800`'s 32 slots, or
  `$1F` - slot at a hit), another object's check, or the A button's probe
  the frame before (`ProcessAButAction`: the leader's `$2A` / 3, then the
  scan).
- `$FFFF` is row -1: `adda.w` reaches the four bytes before `loc_7278`,
  `moveq #-2, d4` and `rts` (`$78 $FE $4E $75`), a box x+120..x+374,
  y+78..y+195.
- A hit leaves `$2F` - k (k the slot from `$E400`), which for the towns'
  objects is row 20 or more: past the table into SegaScreen's code. The
  port has no bytes for those rows and takes them to hit nothing.

`NpcSet.d6` carries it: `build_sprites_d6` at the end of every
`npc_tick`/`npc_setup`, `npc_note_leader_check` from walk.c's `can_move`
and game.c's A button and item-menu probes, and every object check the
new routines make (`obj_can_move`). The other walkers' checks do not write
it yet; no level mixes them with Dezolians.

Measured with `PS2_NPCLOG=1` (one stderr line a decision), Aukba, the
party standing still at 11,9, 12,000 ticks: 393 sideways rolls by
Dezolians on screen found a self row and none stepped; the 155 sideways
steps taken on a self row were all off screen (not drawn, so nothing to
hit). **For the emulator:** stand still in Aukba -- the Dezolians in view
should step up and down and turn, and almost never step sideways; walk
about and they should, on the frames the leader's step begins.

**Obj_MuskCat (`$1C`).** Five placements, all under Skure: B2 `$7B`,
`$7C`, B1 `$7D`-`$7F`. Init: tile `$400`, line 0 (art list 3,
`loc_59FB6`), mappings `loc_5A394` (twenty one-piece 16x16 frames at
x-16, y-25), box row 4, wait `$30`. `loc_45A0`: 1 faces up unless facing
down, 3 down unless up, 5 left (`$C`) unless right (`$10`), 7 right unless
left; B steps up and D down (`$35` to -4 and 4), turning only once the
check passes; 9 steps left and F right (`$34` to -5 and 5), their guards
comparing `$2A` with 9 and 6, which a cat never has -- a cat facing right
steps straight off to the left. Walking bases 0, 3, `$D`, `$11`;
`Level_SpriteMappingsArray3` (0,1,2,0) every six frames; at the end of a
sideways step `$2A` drops back to `$C` or `$10`.

**Talking to them.** `ObjType_People`'s `loc_E584` for dialogue
`$5D`-`$7F`: a live Rolf (`character_data_buffer+2`) whose `$C020` byte,
unmasked, is `ItemID_MagicCap` reads `loc_E70C` - 1 + index, running on
into `loc_E730`; `ItemID_MogicCap` reads `loc_E730` - 1 + index; anything
else is `$1651` ("Ga? dote-mew? banaput?"), or `$1652` ("Mew! Meeyaw!")
from `$7B`. `npc_dialogue_script` gave the last two whatever Rolf wore; it
takes the cap now (game.c `rolf_cap`). Aukba's `$67`: `$1651`, `$1653` with
a Magic Cap, `$1665` with a Mogic Cap. The A button turns them with
`AButton_TargetFacingDirArray` (3, 0, 9, 6) like everyone, which for a cat
is frames 3, 0 and the standing left/right mappings at 6 and 9.

**Extractor**: `extract_npcs.py` writes `Dezolian` (`"wander":
"dezolian"`) and `MuskCat` (`"muskcat"`) into `npc_types.json`; nothing
else it writes changes. To regenerate: `make extract-npcs`, then `make
manifest`.

**Routes** (each fails on build 84's code and passes here):
`dezolis_people` (`$67`/`$66` in Aukba, `$5D`/`$64` in Zosa, `$6F`/`$7A` in
Ryuon; 600 ticks of wandering; four shots), `dezolis_talk` (`$1651`,
`$1653`, `$1665`), `skure_muskcat` (`$7D`-`$7F` present; `$1652`, `$167B`,
`$1658`). On 84: `$67` absent, `$1708` for the talk, `$7D` absent.

**The story on: stage `skure`** (tools/story.py, from story_roof.json;
checkpoint `story_skure.json`, Zosa's Data Memory). Skure is two dungeon
floors under the spaceport, entered from DezolisSkure; the towns with
buildings are Aukba, Zosa and Ryuon. `TreasureChestContentArray` puts a
Magic Cap in chests 4 (B2) and 10 (B1) and the Mogic Cap in chest 2
(B2). The first run went straight from B1 to B2 on story_roof's supplies
and lost the party there (seven fights ending with someone dead, then the
title), so the stage now goes B1, the cap on Rolf, Zosa (gear at double
prices -- the money is there, 889,470 -- mates, Escapipes, the hospital,
a save), Zosa's field until Rolf is L30 (DEZOLIS_GRIND), B2, then Zosa
again. Result: 275 fights, none ending with anyone dead, no stderr;
Rolf L30, Rudo L29, Amy L27, Anna L28, 1,015,506 meseta. Bought in
Zosa: two AcSlashers (Anna), Laconimet and Laconinish (Rudo), LaconCape
(Anna); found and worn: GardaBoots, LaconChest (Rolf).

- **What they say.** In the Magic Cap every one of Zosa's eight
  Dezolians gives one of three lines -- `$1653` "We are Dezo people...",
  `$1654` "We don't ever lie!", `$1655` "There isn't anybody else but us
  on this planet." -- because `loc_E70C` - 1 + `$5D`..`$64` lands in the
  stretch of `loc_E730` that holds them. The per-person lines ("This town
  is called Zosa.", `$165B`) are at `loc_E730` - 1 + index: the Mogic
  Cap's table. The cats in the Magic Cap: `$167D` (`$7F`), `$1679`
  (`$7B`), `$167A` (`$7C`). The next stage should talk in the Mogic Cap
  too.
- **Shopping.** A Magic Cap on Rolf doubles the price (CheckIfDoublePrice)
  and makes a sale 10 meseta; a Mogic Cap does neither, and the shops,
  the hospital and Data Memory take either (CheckPlanetAndCaps). The
  second Zosa visit shops in the Mogic Cap.
- **Harness fixes** (none of them the port's): `town_arrivals` took only
  Motavia's door records, so a Dezolis town's goals fell back to its
  edges and the party, arriving on one, walked straight back out (the
  purchases then went to the wrong level: "not in Anna's bag -- not
  bought?"); it reads DezolisSkure's records too. `plan_gear` prices the
  shelves as the shop will (`price_factor`) and leaves a pinned head alone
  (`PIN_HEAD`, the stage pins Rolf's), so no helmet takes the cap off.
- **Not reached:** B1's cats `$7D` and `$7E` (23,71 and 25,71): talk_to
  found no way up to them, though `skure_muskcat` stands at 23,72 from a
  start. `$7F` answered.


## Build 87: the Esper mansion

Built on 85 (build 86 never reached the user, and its Esper work is not in
this tree). NOTES item 00. Routines read first: `LoadSpritesInLevel`
(ps2.asm:799-930) and `SpriteData_EspMansionB1`/`SpriteData_EspMansion`,
objects `$23`-`$27` (`loc_4B28`-`loc_4DD6`, 6936-7166), `Obj_Esper` and
`Obj_MovingEsper` (7555-7640), `$31` (`loc_5408`) and `$33` (`loc_549A`),
`RunObject` (193), `ProcessAButtonPress`/`ProcessAButAction` (19781),
`ObjType_People`'s dispatch (20946), `loc_E05A`, `loc_E07A`, `loc_E0AA`/
`loc_E0D8`, `loc_E5D0`, `loc_7E02`'s cases `$A`-`$11` (11489), `loc_3786`
(the leader's input test), `Building_CheckRoutine`, `Building_EsperMansion`
(18940-19030), `NeiEquipmentArray`, `loc_DDA2`, `loc_B9D8`,
`CloseAllWindows`, `loc_8364` (BuildingScreen's mansion case) and
`TreasureChestContentArray` 14-15.

**F1 (`$11`, art list 11).** Placements: two `Obj_MovingEsper` (`$30`,
dialogue `$88`) at 15,27 and 17,27 (x `$F0`/`$110`, y `$1B8`); six
`Obj_Esper` (`$2E`, `$89`-`$8E`); `$33` (`$2E` = event `$34`) and `$31`
(event 0), both at `$110`,`$FF`; and the chest slot LoadSpritesInLevel's tail
writes (`$20E` while `$C78E` is 0, `$20F` while `$C78F` is 0, the second
overwriting the first).

- `loc_E05A` (`$88`): `$1688`, or with Rolf standing `$C742` = 1 and
  `$1687`; `$DE72` = 1. `Obj_MovingEsper` does nothing while frozen;
  routine 1 waits for `$C742`, routine 2 faces the one at `$F0` left
  (6, x -1) and the other right (9, +1), routine 3 walks eight frames on
  `Level_SpriteMappingsArray` (0 1 0 2) and stops on frame 3. With `$C742`
  already set its init puts them at `$E8` (frame 9) and `$118` (frame 6).
  The one not spoken to steps at once; the other when the talk ends
  (`loc_E53A` unfreezes it).
- `Obj_Esper`: frame 3, box 4; `$89`-`$8E` go through `loc_E5D0`, their own
  lines from `loc_E730` -- or `$16A1` for any dialogue `$88` and up while
  `$C743` is 3 (NOTES item 5, now in game.c's `level_talk`).
- `$31`: box `$C`, frame 0, event 0 (`$1708`). `$33`: hidden, frame 1, box
  `$C`, shown by its init only if `$C743` is 3 when F1 loads; A on it is
  event `$34`, `loc_E0AA`: Building_EsperMansion with portrait `$39`, the
  level to come back to the leader's cell (`loc_E0D8`).

**B1 (`$10`, art list 5).** Two `Obj_Esper` with dialogue `$8F`, deleted
by their init on B1 once `$C743` is 3. `loc_E07A`, with `d2` = `$DE70`:

| visit | `$C743` 0 | `$C743` 1 |
|---|---|---|
| first (d2 0) | `$1697` "Welcome. Our master has been waiting for you." | `$1698` "I personally wish to have my master sleep quietly, but..." |
| next idle (d2 2) | `$C743` = 1 | event_flags = `$D00`, `$C743` = 2 |

both then `$DE70` + 1 and no `$DE72` on the first, CloseAllWindows (not
`loc_E53A`: the Esper stays turned and frozen) on the second. So it takes
two conversations. event_flags' high byte stops the leader (`loc_3786`)
and the A button; `loc_7E02`'s case `$D` is `rts`, so it holds until the
building clears it. The port keeps it in `g_event_hold`.

Objects `$23`-`$27` rerun their inits every frame until `$C743` is 2, so
the scene starts on the frame of the second talk. None tests 3(a0).

| obj | at | frame | then |
|---|---|---|---|
| `$23` | `$F8`,`$87` | 3 | after `$D0` frames, 1 px right every 3rd frame until x `$158` |
| `$24` | `$108`,`$87` | 2 | after `$160`, 1 px right every 3rd frame until `$138` |
| `$25` | `$100`,`$7D` | 1 | after `$1F8`, 1 px left every 4th frame until `$F8` |
| `$26` | `$100`,`$7C` | 0 | never moves; box `$A`, `$B` while `$C743` is 3; event 0 |
| `$27` | `$F8`,`$7A` | 0 | after `$240`, 1 px left every 2nd frame until `$D0`; `$78` frames; frame +1 every 8 to 2; `$50` frames; then Building_EsperMansion, portrait `$39` (`loc_4DBE`), every frame until the screen changes |

**Building_EsperMansion.** `loc_8364` draws Lutz (LutzPortraitArt at tile
1, LutzPortraitTileInd 30x14 at plane A row 2 column 5, palette `$3B`) and
adds 1 to event_routine: no portrait window. The states are in building.c
(`esper_mansion`): with `$C743` = 2, `$190B`/`$190E`, event_flags = 0,
`$C78E` = 0, then `$C743` = 3 and F1 at `$100`,`$100`; otherwise `$168F`,
or `$1690` (`$C744` = 1, `$C78F` = 0) with all eight of NeiEquipmentArray
in the bags; with `$C744` set, `$1913` -- or `$1911` and YES/NO holding the
Nei Sword: YES `$1695` and NoahGroundF (`$63`) at `$200`,`$6B0`, NO `$1912`
and `loc_B9D8`. `TreasureChestContentArray` 14 is the Prism and 15 the Nei
Sword; flag_names.json called `$C78F` Noah's box (that is level `$63`'s
`$035`), fixed.

**Port fixes on the way.** `SCENE_TYPES_MAX` was 16 and there are 22 scene
types now: the last six (`$33` among them) were dropped without a word.
It is 32, with a stderr line if the JSON outgrows it. `WIN_TEXT_BUF` was
1024 and `$1690`-`$1694` run on as one text of about a thousand codes --
the text that overruns sound RAM on the cartridge (the music freeze) --
so the closing `$C4` was cut and the window never ended. It is 4096, with
a stderr line on truncation.

**Extractors changed:** `make extract-npcs` (nine scene types in
scene_objects.json), `make extract-inline_art` (LutzPortraitArt.png),
`make extract-flag_names`, and `make extract-nametables` for
LutzPortraitTileInd -- a binclude, so it needs `ART_DIR`: this tree does not
have the .nam and the building's plane A stays blank until it is made --
then `make manifest`.

**Routes** (each fails on 85 and passes here): `esper_door` (`$1687`,
`$C742`, the door open, `$168B` from `$8B`; on 85 `$1708`),
`esper_b1_lutz` (`$1698`, `$C743` 2, the party held, the scene, `$190B`
and `$190E`, F1 16,16, `$C743` 3, `$C78E` 0, the Prism; on 85 nothing
answers), `esper_lutz_nei` (`$33` shown, `$1690`, `$C744`, `$C78F`, the Nei
Sword, `$1911`, NO, `$1912`; on 85 `$33` absent).

**For the emulator:** the door Esper not spoken to should step aside while
the text is still up; after a reload with `$C742` set the two face inward
(frames 6 and 9), after their step they face down (frame 3).

## Build 87a: Shir's thefts

NOTES item 2. Read: `ProcessStealItem` and `StealItemArray` (ps2.asm:
19093-19185), its four callers (the weapon, armour and item stores'
`loc_BA66` and twins, 17148/17291/17434; the Central Tower room's
`loc_C148`, 17753), `loc_7E02`'s case `$C` (`loc_8010`, 11624) and
`ObjType_Events` `$31` (`loc_E042`, 20545). building.c's `steal_item` was
already the routine line for line (Shir in the party, the room's level 10,
alive, bag not full, `& $F` zero, `& 7` into the list, her own bag,
`party_member_join_next` + 1, `$C610` cleared, out of the party,
event_flags `$C00`); what was missing is what `$C00` does. The level
screen now takes it as the next load's one-shot case `$C`: `$DE70` = 1,
`$DE6E` = `$31`, and on the first idle frame `loc_E042` -- the message
window, `$1722` "What? Shir is gone?", `$DE72` + 1, event_flags 0 -- then
CloseAllWindows. Rolf's house then says `$0927` and puts her back
(`$0928`), as it already did.

Route `shir_steal` (fails on 87, where the theft happened with nothing
said): Rolf and Shir at L1 in and out of Paseo's item store; the ninth
goodbye is the theft, `$1722`, and the party is Rolf alone.

**The harness** (tools/play.py, "Shir's thefts"): `shir_join` (the knock
at Rolf's house, then REORGANIZE her in for the weakest of four by
`member_value`), `raise_member` (fights by the town beside the strong,
EXP shared by the living, the hospital every few trips), `shir_thefts`
(the Paseo door for the list the wanted item is on -- item store, weapon,
armour, or the tower desk's ROOM, `room_policy` -- out again, and
`fetch_shir` from Rolf's house after each theft, until the bags hold what
was asked) and tools/shir_trials.py to run it.

Measured from story_dams.json (Paseo, Shir not yet joined; she joins at
L1, the party Rolf L25, Rudo L24, Anna L23 and her, Amy out):

| list | exits | thefts | rate | ProcessStealItem |
|---|---|---|---|---|
| item | 180 | 9 | 0.050 | 1/16 = 0.0625 (11.25 expected, sd 3.2) |
| room | 12 | 1 | 0.083 | 1/16 |
| both | 192 | 10 | 0.052 | 0.0625 (12 expected) |

The nine item-store thefts: Trimate 5, StarMist 2, MoonDew 2, Hidapipe 0
(list: 4, 1, 1, 2 in eight). Shir went from L1 to L10 in the fields by
Paseo by tick 47,785 (Rudo to L25 on the way), then took the Visiphone on
the twelfth exit from the room. Within the counting noise of the ROM's
odds; nothing to fix.

## Build 87b: the door Espers, checked

The owner, in the emulator: the door Esper not spoken to steps aside as soon
as the conversation starts -- as ported (only the one spoken to is frozen,
`3(a1) = $FF`, until `loc_E53A`). The other two checks (facing down after
the step, inward after a reload) wait until he reaches the mansion.

With Rolf dead, `loc_E05A` gives `$1688` and leaves `$C742` clear, so
`Obj_MovingEsper` stays in routine 1 and the two keep the doorway shut.
Nothing to change; route `esper_door_rolf_dead` (the dead-Rolf save warped
to F1 15,28: pushes up from 15 and 16 get nowhere, before and after
`$1688`) pins it.

## Build 88: Kain and Hugh by the enemy's type, and the far column's hit effect

Built on 87b. Prompt task 1. Read first: `ProcessTechnique` and
`TechniqueActionIndex` (ps2.asm 2932-3033), `TechAction_Foi`/`_Zan`/`_Gra`
(`loc_2148`), `TechAction_Conte`..`_Shinb` (`loc_2232`/`loc_223C`, 3149-3220),
`CalculateHitRate`, `loc_27AA`, `CalculateAttackDamage`,
`CalculateTechniqueDamage` (3662-3740), `loc_8D4` (667, the enemy stats),
`EnemyData`'s header (39190), `loc_ED50`-`loc_EE8C` (ORDR's technique, 21944),
`loc_3462` and `loc_34EE` (the hit effects), `loc_3622`, `loc_27364`'s index.

**The rule.** `ProcessTechnique` keeps TechniqueData byte 7 & `$60` in
`$FFFFF73A`; `loc_8D4` copies EnemyData byte 43 into `1(a6)` of each enemy's
stats; `loc_27AA` ANDs the two, and nothing left is `$1204` ("Didn't work")
with a hit rate of 0. Before that, a rate byte of `$FF` (500: Vol, Savol,
Brose) always fails against an escape rate of 0 (the bosses). Every damage
and status technique goes through it. Bit `$20` is a biomonster, `$40` a
robot. Kain's own techniques are `$Dx` -- robots only: Forsa, Eijia, Rimet,
Gaj/Gigaj/Nagaj, Conte, Sag/Gisag/Nasag, Brose. Hugh's own are `$Bx` --
biomonsters only: Rimit, Doran, Gen, Sagen, Shinb, Shiza, Vol, Savol. Foi,
Zan, Gra and their kin are `$Fx`, both. EnemyData: 73 biomonsters, 31
robots, 22 both (Dezolis's dungeon monsters from Wrestler on, Noah's, Dark
Force). battle_obj.c's `roll_tech_enemy` was already this line for line.

By level (the level record's group, HP-weighted over its eight
formations): every dam, the Control Tower, Gaira and every Motavian
dungeon's late group (`$C737` set) are robots only; the same dungeons
before Climatrol, Climatrol included, biomonsters only; Crevice 87%
biomonster; Skure B2 57% bio (70% late), B1 67% robot (62% late); Menobe,
Ikuto, Guaron and Naval 74-100% both, the rest biomonster -- there Hugh's
techniques reach everything and Kain's all but the biomonsters.

**The port bug the harness found.** The first EIJIA fight_smart ordered on
Red Dam F2 (formation 148: four Poleziaxes, x layout 4) never ended: the
state stood at "actions animating" with `$CC06` at 1. EIJIA's cast ends in
`loc_3462` (`$F9`), an effect over each shown enemy whose script is
`loc_34EE[(d3A & $7F) * 7 + column]`, the column from the enemy's x -- and
`$198`, layout 4's fourth place, is none of the six named, so column 6 and
script `$47`. `loc_27364` has 72 entries; its `$41` is written
`(loc_274C6+6)-loc_27364`, and `extract_battle.py`'s `read_anim_table` took
only `loc_X-table`. The entry was dropped, every one after it moved down one
-- hit effects `$41`-`$46` played the next script -- and `$47` was past the
end: an effect with no script never reaches `$FE`, so `$CC06` never came
back to 0. Three enemies never stand in column 6, which is why EIJIA had
worked before. The extractor reads `(label+N)` entries now, keyed
`loc_274C6+6` with the script from there; re-extracted from the uploaded
disassembly, only `anim_tables.effect` changed (71 -> 72). `animate()` says
so on stderr once (`battle: object %X has no animation script %d`, a
`warned` byte in `BObj`) rather than hanging without a word.
**Rerun `make extract-battle`** (battle.json is already in the manifest).

Route `eijia_far_column` (fails on 87b's battle.json at the `wait idle`
after FIGHT, passes here): Rolf L23 and Kain L15 against formation 148,
ORDR Kain EIJIA, FIGHT, the next round, `expect command tech`, no stderr
line; two PNGs.

**The harness** (tools/play.py, "Techniques by the enemy's type"):

- `fight_smart` orders each round, for the members in `TYPE_TECHS` (Kain,
  Hugh), the damage technique `plan_type_tech` picks, after DEFEND to
  ATTACK and before the heal (the healer is left alone). `loc_F4F8` puts a
  used technique back to ATTACK, so it is ordered again every round.
- `plan_type_tech`: a technique that targets enemies (bit 7) with a rate,
  the TP for it, type bits that touch a live group (`live_groups`: the
  formation's two enemy ids and the state's live HP), not `$FF` against a
  boss. Its worth is `CalculateTechniqueDamage`'s mean (the rate) times
  its hit rate (TechniqueProperties byte 0) over one enemy, the group, or
  every enemy it can touch, each capped at that enemy's HP; the weapon's
  is `CalculateAttackDamage`'s mean for each weapon worn, attack from the
  level table (+$1A is before equipment), at 90%. It must beat the weapon
  by 1.25 and gain 2 a TP. A member with a mending technique keeps twice
  the cheapest's TP.
- `battle_order(..., group=)`: `Win_EnemyGroups` stop 0 is the first
  group, 1 the second (window.c).
- `member_value(name, level, mix)`: with `dungeon_mix(levels)`, the best
  damage technique known at that level (capped at 120, times the enemies
  it reaches, times the share of the mix its type bits touch), at half
  weight. `choose_party(..., mix=)`, `pick_party(..., ahead=[levels])`.

**Measured** (`docs/type_trials_build88.md`: dam_trials, 30 runs of 40 fights
a party from story_climatrol.json; `--type-techs none` is 87b). Red Dam F2
(robots) at 150 grind fights: every party with Kain or Hugh up -- Amy+Anna+
Kain 11.8 -> 16.9, Amy+Hugh+Kain 7.0 -> 11.0, Hugh+Anna+Kain 12.7 -> 15.9,
Rudo+Amy+Hugh 11.8 -> 14.8 (Hugh's Gra and Zan), Rudo+Anna+Kain 17.4 ->
18.0. Crevice (87% biomonster) at 450: Hugh+Anna+Kain 3.8 -> 6.0,
Rudo+Hugh+Kain 7.7 -> 9.3, the rest within noise -- its enemies (attack 127
on the mean) wear the party down whatever it does. Rudo+Amy+Anna, the same
logic both ways, moved 21.2 -> 19.7 and 9.4 -> 10.7: that is the noise (the
runs share a session and the RNG runs on). No `$1204` in over 6000
orders. `choose_party` on those levels still takes Rolf, Rudo, Anna and
Amy on either mix, as the trials rank them; Kain's value on the robot mix
256 -> 286 (L15) and 300 -> 360 (L18), Hugh's on the biomonster mix 263 ->
315 and 294 -> 346. Climatrol F5 with `$C737` cleared (`--late 0`) was too
easy to rank anything.

**The story on Dezolis** (prompt task 2, tools/story.py; build 86's stage
never arrived, so played again from story_skure.json). Three stages, each
with its checkpoint in routes/saves/:

- `dezolis_towns` (story_dezolis_towns.json, Ryuon): Aukba, then Ryuon --
  Rolf in the Mogic Cap for the shelves and the talk, mates, Trimates,
  Escapipes, the hospital, the Magic Cap back on for Data Memory
  (`dezolis_town_visit`). Every Dezolian answered with their own line:
  Aukba's ten `$1663`-`$166C`, Ryuon's twelve `$166D`-`$1678`. Aukba had
  nothing worth buying; Ryuon sold Rolf a LacSword, Rudo GardaBoots and
  Amy two LaconEmels.
- `esper` (story_esper.json): the door Esper (`$1687`), B1's Esper twice
  (`$1697`, `$1698`), the scene and Lutz (the probe waits for the
  building: event_flags `$D00` holds the pad, so `ready()` alone came back
  before the scene), F1 16,16, and F1's chest at 13,15 -- `$1702 $1703`,
  the Prism. That chest is LoadSpritesInLevel's tail's, not in the
  level's chest list, so the stage searches it by cell.
- `dezolis_grind` (story_dezolis_grind.json, Zosa): 25 trips from open
  ground by Zosa's gate (`play.field_spot`, `grind_and_gear(spot=)`):
  183 fights, nobody down, Rolf L30 -> 32, Rudo 29 -> 31, Amy 27 -> 30,
  Anna 28 -> 31; meseta 1.13M. Walked from the gate's own cell -- at
  Aukba and at Zosa -- the trips fought nothing: the walks were blocked.

The harness's map now knows the Prism: `play.HELD` (the story adds the
Prism once carried) applies map_patches.json's item entries -- block `$63`
at Menobe's, Ikuto's, Guaron's and Naval's doors on DezolisSkure -- to
`level_map`, as game.c's `level_map_patches` does; without it `plan()`
found "no way from level 10 to 72". With it the breadth-first `plan()`
takes minutes from Zosa to Aukba (the four doors put some twenty dungeon
floors in its search). `level_distances` (a level graph from the door
records and exits) now prunes it to level changes that bring the target
nearer, with the full search as the fallback -- but a Zosa-to-Aukba plan
still ran past two minutes, so the stages `menobe`, `ikuto`, `guaron` and
`naval` (`prism_dungeon`: the nearest town, every floor's chests, the Nei
items) are written and not yet played. That is where the next session
starts.

## Build 88a: the door Espers' first check, and where plan() stalls

No code. The owner, in the emulator, on first entry to the Esper mansion:
the two door Espers part when the talk starts and face south as soon as
their step ends. That is `Obj_MovingEsper`'s routine 3 (`loc_53C0`,
ps2.asm 7604): eight frames of the walk cycle from frame 6 (left, x -1)
or 9 (right, x +1), then `$24` = 3 and routine 4, which is `rts`. The
port matches -- `esper_door`'s `02_stepped_aside` shows both facing south
either side of Rolf. Still for the emulator: after leaving F1 and coming
back with `$C742` set, `loc_532C` puts them at `$E8` with frame 9 and
`$118` with frame 6 -- facing each other across the doorway, not south.

**plan() with the Prism, profiled.** From Zosa's arrival (0xB 25,3) to
Aukba's (0xA) with `play.HELD = {"Prism"}`: `_plan` ran twice -- the
pruned search (level_distances: Zosa 2, DezolisSkure 1, Aukba 0) came
back with no route, and the full breadth-first search then made some
31,700 `LevelMap.path` calls in 90 s without finishing. Without the Prism
the same walk plans at once (stage dezolis_towns). So the likely fault is
the patched map, not the search: the four `$63` blocks are written into
the harness's plane B at map_patches.json's offsets (150, 236, 1977, 3792)
as indices into the tmx layer, and if the tmx layer's stride differs from
the RAM block map's, one of them lands in a corridor between Zosa and
Aukba. Compare the patched cells against the game's own level map (a
`shot` on DezolisSkure with the Prism held) before touching plan().

## Build 89: plan() across Dezolis, and the Prism's dungeons

Built on 88a. Prompt task 1, then task 2.

**The Prism's blocks were never misplaced.** Read: `loc_9258`/`loc_92CC`
(LoadLevelData's tail, ps2.asm 13392) and `loc_6DE8` (10147), game.c's
`level_map_patches`. loc_92CC writes `$63` at `$FFFF9000` + `$96`, `$EC`,
`$7B9`, `$ED0`; loc_6DE8 reads that map at row * (`$F722` >> 5) + column,
and `$F722` is the level's width in pixels -- 2048 on DezolisSkure, 64
blocks, the tmx layer's width, which both game.c and play.py index with.
So 150, 236, 1977 and 3792 are the blocks at cells 44,4 88,6 114,60 and
32,118: each directly above its dungeon's arrival cell (Menobe 45,6,
Ikuto 89,8, Guaron 115,62, Naval 33,120). On the game's own map, holding
the Prism, the step up from the cell door_cells gives goes into
MenobeGroundF 48,61, GuaronGroundF 68,45 and NavalGroundF 5,43 (route
`dezolis_prism_doors`, three PNGs of the entrances; `dezolis_prism` has
Ikuto). The route passes on 88a as well: it is the evidence, not the fix.

**What stalled plan().** DezolisSkure is not one piece of ground. Zosa's
side of it (1187 cells) has two ways off: Zosa, and one door down into
Skure B1. Every trip from Zosa to Aukba -- Prism or not -- is Zosa,
DezolisSkure, B1, B2, B1, B2, B1, DezolisSkure, Aukba. Build 88's
`level_distances` pruning refused any level change that did not bring the
target nearer in the level graph, and B1 (2 hops from Aukba) is further
than DezolisSkure (1): from Zosa the pruned search could never find a
route, and came back with nothing after two path() calls, every time. The
full search behind it then ran `LevelMap.path` -- a Dijkstra over the
level -- for every (level, arrival cell) node and every way out. Without
the Prism that is 0.4 s; with it the four dungeons add some forty floors
within the eight level changes, the ~31,700 calls build 88a counted.

**The fix (tools/play.py).** `LevelMap.pieces()` labels, once a map, the
cells path() crosses as plain floor (open, not a town edge, not special
unless a damage floor), joined across the wrap; `LevelMap.reaches(start,
goals)` is `start in goals or path(start, goals) is not None` from those
labels -- path() walks within a piece and steps onto a goal from `start`
or from any floor beside it. `plan()` is the old full breadth-first search
with `reaches` in place of `path` (same nodes, same order, same legs);
`level_distances` and the pruning are gone; `ways_out` is kept per map as
`level_map` is. And `goto()` and `town_exit()` built their maps with
`LevelMap(level)` -- the Prism's blocks missing, so on DezolisSkure a walk
could plan straight through a dungeon's door block and push into it; both
use `level_map` now.

`tools/plan_check.py` is the check (it fails on 88a: its first plan never
finishes). From Zosa's street, with and without the Prism: Aukba (9 legs,
0.29 s / 0.65 s), MenobeGroundF (9 legs, 0.50 s), IkutoGroundF (3, 0.00 s),
GuaronGroundF (7, 0.18 s), NavalGroundF (11, 0.60 s); none of the
dungeons without it. `--equiv` asks every level, with and without the
Prism, whether each arrival cell reaches each way out, both through
`reaches` and through `path()`: 0 differences in 173,069 checks, levels
0-`$2F` and `$50`-`$6F`; `$30`-`$4F` (the Motavian dungeons' big floors)
did not finish inside a tool call and are unchecked.

**plan() off the planet.** Skure's roof door (DezolisSkure 79,59) is a
building record (11) with no to_level, so no plan reached Motavia from
Dezolis. `play.FLIGHTS` maps it to Paseo 31,16 (Building_Roof's flight
home, route roof_yes); the other way asks ROOF and YES at the tower's
desk and stays story.stage_roof's.

**Shir's thefts, back on Motavia** (prompt task 2's standing rule).
Two stages before Menobe:

- `motavia` (story_motavia.json, Paseo): the Prism and Mogic Cap kept on
  Rolf, home by the roof door, `shir_join` (Anna left at Rolf's house),
  Shir raised 1 -> 10 by Paseo for the Central Tower's room.
- `shir` (story_shir.json, DezolisSkure 60,79): `shir_thefts` for a
  Visiphone, a Star Mist and a Moon Dew -- 10 thefts in 95 exits (room 1
  in 18: the Visiphone; item store 9 in 77: 3 Star Mists, the Moon Dew,
  3 Hidapipes, 2 Trimates). Everything stolen is given out of her bag
  (`empty_shir`) before she leaves; then the party for Menobe and the
  roof's flight. The first run's party was Rolf, Rudo and Kain -- three:
  `pick_party`'s default core names Nei, who after Climatrol is in the
  roster but never in the party. Now `core=("Rolf",)`, catch_up off
  (the party that fights best on the mix: Rolf, Rudo, Amy, Anna, as
  build 88's trials rank them); catch_up's choice (Kain, Shir, Hugh to
  raise) is logged beside it. Raising Kain and Hugh has not been played.
  `--failed` continues story_<stage>_failed.json and redoes that stage.

**The Prism's dungeons, played** (build 89a, same build number: no game
code changed). Four stages, each a checkpoint in routes/saves/, party Rolf,
Rudo, Amy and Anna throughout:

| Stage | Checkpoint | Nei items | Leader | Fights with someone down |
|---|---|---|---|---|
| menobe | story_menobe.json (Aukba) | Crown, Helmet (Neimet) | L33 | 0 |
| ikuto | story_ikuto.json (Zosa) | Slasher, Shot | L34 | 5 |
| guaron | story_guaron.json (Ryuon) | Cape, Armor | L36 | 0 |
| naval | story_naval.json (Ryuon) | Shield, Emel | L37 | 8 |

Meseta 1.14M -> 1.48M; every Nei item worn by whoever gear_up picked. What
the runs found, all the harness's (nothing in the port):

- **Poison.** Menobe's walks all gave up "hurt with nothing left to mend
  with" with the party full: Anna was poisoned, `needs_healing` counts
  poison, and `goto` only called `mend()` (only `wander` cured). Read:
  `Building_Hospital` (16928): HEAL (`loc_B9D8`) sets HP and TP to their
  maxima and leaves poison; only CURE (`loc_B9B0`, `andi.w #$7FFF`, one
  member, `$503` then YES) clears it -- so the port's hospital is right and
  `hospital_policy`'s HEAL never could. `goto` calls `cure_poison` before
  `mend`; `heal()`/`heal_here()` follow HEAL with `cure_policy` (CURE for
  each poisoned member).
- **Worn out.** Menobe's first run went on with Rolf's and Amy's TP at 2
  and was wiped out by formation 235 (three enemy $6F, 365 HP). `loot(tired=
  worn_out)`: anyone dead, HP under half or TP under a third -> retreat.
  `s.hurt` was never cleared, so after one retreat every walk gave up:
  `goto` clears it first. The walk home is the retreat: `s.careful` off for
  it.
- **Holes in chains.** Ikuto B1's hole at 19,18 lands on B2 41,18, itself
  a hole; the next move falls again (walk.c runs check_underfoot before
  the pad, its reading of `loc_382A`, not re-read here). Guaron's +2/-3
  floors land on 12,10 five floors running. `holes()` follows a chain to
  its last landing; `ride_holes()` takes the falls before a plan when the
  party stands on one; `travel` re-plans (up to four plans) after arriving
  somewhere its plan did not expect.
- **The nearest town** is by plan() legs now, not straight-line distance:
  Ikuto's door is on Zosa's piece of DezolisSkure, not Aukba's.
- `prism_dungeon` returns False unless it gets back to town (Guaron's first
  run reported success on GuaronF12, and its checkpoint was no save).
- Deaths: Ikuto 5 and Naval 8 fights ended with someone down; the party
  came out whole each time; how each was brought back (a technique, the
  Moon Dew, the retreat) was not tallied -- worth logging next. Kain and
  Hugh were not raised: the trials' party finished every stage.

Routes: 63, all green (`dezolis_prism_doors` new). No game code changed.

## Build 89b: the door Espers' second check

No code. The owner, in the emulator: back upstairs from B1, the two door
Espers face each other across the doorway. That is `Obj_MovingEsper`'s
init with `$C742` set (`loc_532C`, ps2.asm 7604): routine 4 at x `$118` on
frame 6 for the one placed at `$110`, `$E8` on frame 9 for the other. The
port's `moving_esper_tick` case 0 is that line for line. Route
`esper_door_return` (new, passes on 89a too -- the evidence, not a fix):
B1's stairs up with `$C742` set, `objects` (`$30` at 14,27 frame 9 and
17,27 frame 6), `01_facing_in`, then F1 entered again by warp for
`02_doorway` with the pair in frame. Both of the build 87 emulator checks
on the door Espers are now closed. 64 routes.

## Build 90: the Nei Sword, Nei after Climatrol, and Kain before Noah

Built on 89b. Prompt tasks 1 and 3 (task 2's revival tally is in).

**What the game does after Lutz.** Read: `Building_EsperMansion`
(ps2.asm 18940) in full, `NeiSword_ItemSelected` (14868), ObjType_Events'
table (19862) and `loc_E0BE`/`loc_E0F8`/`loc_E122`, the start of
`loc_E1E4`, the `$C745`/`$C747` references (932, 10418, 13452). Once
`$C743` is 3, A on F1's `$33` is the building:

- `$C744` clear: `$168F`, or with all of `NeiEquipmentArray` in the party's
  bags (worn counts: `CheckItemExistInventory` masks `$7F`) `$1690`-`$1694`,
  `$C744` = 1 and `$C78F` = 0 -- the chest slot at 13,15 becomes the Nei
  Sword (`$20F`).
- `$C744` set, no Nei Sword carried: `$1913`.
- The Nei Sword carried: `$1911` and WinID_YesNo2. YES is `$1695` and
  `loc_CFC0`: NoahGroundF, x `$200`, y `$6B0` (cell 32,107). NO is `$1912`
  and `loc_B9D8`, the hospital's HEAL for nothing -- a free inn.
- `NeiSword_ItemSelected`: USE on level `$63` or later takes the party to
  EsperMansionF1 16,16 (`SXFID_Teleport`); anywhere else `$27`.

Noah is two levels, `$63` and `$64`, each 64 wide (112 and 144 high), joined
by 28 pairs of stairs; no chests. Pandora's box (object `$39` at
`$210`,`$D8` while `$C745` is clear; events `$35`/`$36`) is Dark Force
(`$102`): ported (build 63, route noah_box). Not ported: event `$37`
(`loc_E122`: `$1813`, two YES/NOs, Mother Brain `$103`, event_flags
`$F00`), event `$38` (`loc_E1E4`), and `loc_70D6` (level type 6, block
`$33`: with `$C747` clear and y at most `$218`, x `$210` and event_flags
`$E00`) -- game.c's default case prints `level: event $XX is not ported`.
So the story can reach Dark Force and no further until those are.

**The stage `nei_sword`** (story.py; checkpoint story_nei_sword.json,
Ryuon): travel to F1, `lutz()` (face `$33` from 17,16, A, C through the
text, a YES/NO answered by the caller; talk()'s limit of six Cs cannot get
through `$1690`-`$1694`), F1's chest, the sword on Rolf, `lutz_heal()`
(NO: `$1911 $1912`), then the nearest town as `dezolis_town_visit`. The
first run left the sword in Anna's bag: Rolf's held sixteen, two of them
LaserKnives Shir had stolen, so GIVE failed. `make_room()` moves an unworn
spare out first; `wear_hand()` puts a hand item on as gear_up would. Rolf:
GardaBoots, MagicCap, LaconChest, NeiSword (75 attack for the LacSword's
62). Played: 19 fights, nobody down.

**Nei after Climatrol (task 3).** Read `RolfHouseOption_Reorganize` (16318)
and `Win_RegroupCharList` (24423): the list is character ids from 2 up --
never Rolf or Nei -- and `loc_B1A0` puts Nei in slot 1 itself only while
`$C737` is clear. `play.nei_available(s)` asks the probe for `$C737`;
`play.pickable(s)` is the roster less Nei once it is set, and is what
`pick_party` and stage_shir's logged alternative pass to choose_party. From
story_nei_sword.json, the old members made `Rolf, Nei, Rudo, Amy` (core,
Nei L40: a three-member party at the house); the new make
`Rolf, Rudo, Amy, Anna`. `core=("Rolf",)` in stage_shir can stay; the
default core is now safe too.

**Kain and Hugh before Noah (task 3).** docs/noah_trials_build90.md.
`dam_trials.py --stores all --outfit ... --candidates ...`. At the same
grind on Noah (3459 EXP a member a fight), Rudo+Amy+Anna leads to 80 fights
(Kain L24); Rudo+Anna+Kain leads from 160 (L28: 15.7 fights a trip to 13.8)
and 250 (L32: 18.5 to 16.3, 22 of 30 cleared to 17). Hugh in Amy's place
does not beat her over 30 runs. So raising Kain pays for a grind of that
length, and Hugh does not.

**Revivals (task 2).** `Session` notes every member who goes down and every
one who comes back (`s.downs`, `s.revivals`: the screen, the building, the
last scripts); story.py's report tallies them by how (`revival_how`: the
clone lab, in a fight after `$XXXX $YYYY`, on the map after ...).

Routes: 64, all green. No game code changed.

## Build 91: the Noah grind, Dark Force lost, and plan() checked everywhere

Built on 90. Prompt tasks 1 to 4, continued.

**plan_check --equiv over $30-$4F (task 4).** `--part K/N` checks the K-th
of N slices of a level's arrival cells; `--sample N` a fixed-seed sample of
N (arrival, way out) pairs; the Prism's pass skips a level whose map it does
not change (no item_patches) and whose arrivals are the same -- the very
same checks. Every level from `$30` to `$4F` checked, 0 differences:
one level a call for most (`$44`-`$46`: 40,128, 76,032 and 109,552 checks);
`$33`, `$3A`, `$3E` in quarters; `$4D` (72,668) and `$4F` (58,900) whole.
Ikuto B6 (`$4C`: 209 arrivals x 773 ways out, 26 ms a path() that finds
nothing, about 71 minutes) and B4 (`$4E`: 658 x 934, about 28) are sampled:
3,000 and 6,000 pairs, 0 differences. The timed plans pass.

**Stages** (story.py; every one resumable with `--failed`):

- `kain` (story_kain.json, Zosa): home by the roof door; Amy, who stays at
  Rolf's house, hands her consumables and every unworn piece no store sells
  that Kain or Shir could wear to whoever stays (`keep_from`: Trimate,
  CrystCape, Neimet, StormGear, MoonDew went to Anna); Shir in for a bounded
  round of thefts (10 in 97 item-store exits: 6 Trimates, 2 Hidapipes, a
  Star Mist, a Moon Dew -- the party then carries 2 Moon Dews, 4 Star
  Mists), empty_shir; Rolf, Rudo, Anna, Kain (the Noah trial's party), Kain
  geared in Paseo, the roof to Skure, Zosa's shelves in the Mogic Cap.
- `kain_grind` (story_kain_grind.json): Zosa's field until Kain is 20
  (grind_and_gear, lowest=True): 16 trips.
- `noah_grind` (story_noah_grind.json): Lutz's YES, fights on NoahGroundF
  until worn_out or 20, the Nei Sword out (`nei_sword_out`), Lutz's NO for
  the free HEAL (`mansion_mend`: the dead to a clone lab -- loc_B9D8 skips
  a member at 0 HP), a town every fifth trip. 188 fights in 25 trips,
  nobody down: Rolf L41, Rudo L40, Anna L40, Kain L31. The first run could
  not leave Noah: find_item skips worn pieces and Rolf wears the sword;
  nei_sword_out looks at the whole bag (ITEM lists worn pieces and USE runs
  NeiSword_ItemSelected, which does not ask).
- `dark_force`: to 33,14 below the box, open it only in shape (BOX_HP 0.85,
  BOX_TP 0.6 -- else the Nei Sword, NO, YES and the walk again), the walk's
  fights on weapons and mates (`s.fight_opts`: the first tries arrived with
  Rolf's and Kain's TP at 0/146 and 1/108 every time). Arrived in shape
  (TP 99/146, 85/85, 104/107), `$171A $1910`, formation 258: **lost**, Dark
  Force at 865 of 2560 after 18,000 ticks. Battle lines on the way: `$1223`
  becomes evil, `$1228` loses confidence (no techniques), `$1229` gets lazy.
  Anna's TP stayed 85/85 all fight. PNG: docs/dark_force_build91.png (the
  screen as the ROM builds it for `$102`: no enemy boxes).

**Dark Force, every party** (tools/boss_trials.py, from story_noah_grind
.json, each member at the save's level and gear, 8 Trimates, warp to 33,14):

| party (Rolf +) | levels | won | Dark Force HP left (mean, lost) | ticks (mean) |
|---|---|---|---|---|
| Rudo+Amy+Anna | 41/40/35/40 | 0/10 | 1324 | 16518 |
| Rudo+Amy+Kain | 41/40/35/31 | 0/10 | 1606 | 12285 |
| Rudo+Anna+Kain | 41/40/40/31 | 0/10 | 1461 | 14247 |
| Amy+Anna+Kain | 41/35/40/31 | 0/10 | 1026 | 16137 |

Forty fights, none won. So the story stops at Dark Force for now, and it is
not yet known whether that is the game at these levels or something to
check: Dark Force's EnemyData row and its round against the ROM, and
fight_smart, which orders damage techniques only for Kain and Hugh
(TYPE_TECHS) -- Rolf's 146 TP went on heals, never Megid or the like.

**The probe's `start slot N FILE` onto itself.** After the first wipe
`recover()` CONTINUEd `route_out/story/saves/slot1.json` -- the session's own
slot -- and copy_file opened the destination for writing first, emptying
the source: "will not load", "json: ...: syntax error near byte 0".
copy_file now leaves a file copied onto itself alone (same st_dev/st_ino).
A route allows one start, so the check is a session: tools/probe_check.py
(PASS; FAIL on the 89b probe: the slot comes out 0 bytes).

**Revivals**: the tally's first: one member down on the approach, brought
back by the clone lab (Zosa). The Prism dungeons' deaths (Ikuto 5, Naval 8)
predate the tally.

Routes: 64, all green. No game code changed; tools/route_probe.c did.

## Build 92: Dark Force checked, and the way to Mother Brain

Built on 91. Prompt tasks 1 and 2 (first checkpoint). Names are used in the
text; the ROM's labels and addresses are in parentheses for checking.

**Dark Force against the ROM (task 1): no port differences.** Its battle
data (`Enemy_DarkFrce`) matches battle.json field for field: 2222 EXP, attack
320, defense 30, both enemy types, agility 148, 2560 HP, technique 8 at rate
`$FF`, escape rate 0. With that rate every one of its actions is its
technique, which is Dark Force's curse (technique 8 lands on the table's
seventh entry, `loc_2E82`):

- a quarter of the time, the party-wide blast (`loc_2D08`): every living
  member takes (84-115) x 90 / 100 = 75-103, halved only by Deban or
  defending -- armour does not count;
- otherwise, when the second random byte's low three bits are 1-6, the curse
  of that kind on the **slowest** living member not already cursed (the
  lowest agility; a tie goes to the later party slot), `$1223` "becomes
  evil" -- and anything else is the blast again.

On a cursed member's turn (the character's routine, `loc_1C1C`-`loc_1CF2`)
there is first a 1-in-16 chance of the Neisword's light (`$122A`): every
curse on everyone lifted. The ROM checks no one's equipment for this, the
disassembly's comment notwithstanding. Otherwise the kind decides: 1-3 turn
traitor (`$1224`), are crushed by despair (`$1225`) or turn greedy (`$1226`)
and lose that turn -- every turn, until the light; 4 "fights less
furiously" (attack base -40), 5 "loses confidence" (techniques sealed), 6
"gets lazy" (agility -40), once each, after which the member stays marked
and is never cursed again. All of it matches the port instruction for
instruction; so do the party's hits on it (`CalculateHitRate` with its
dexterity truncation, `CalculateAttackDamage`, and the technique check,
under which Kain's robot and Hugh's biomonster techniques both work on it
and Vol and Brose fail). Losing to it at build 91's levels is the game. The
port's comment called the curse's pick the quickest; corrected.

The battle row in docs/dark_force_build91.png, ANNA ROLF | FIGHT | RUDO
KAIN for the order Rolf, Rudo, Anna, Kain, is the ROM's layout: slots 0
and 1 are the front row in the middle (sprites at x `$100`, `$140`; stat
boxes at columns 9 and 23), slots 2 and 3 the back row outside them (`$C8`,
`$178`; columns 1 and 31), FIGHT/STGY at column 17
(`Battle_CharCoordinates`, `PtrWin_Battle*CharStats`).

**The Neisword's light** (the battle's palette effect 1, `loc_67B8`): the
port set it and never drew it. Now the backdrop colour steps through six
colours, one every other frame, white-hot and back to dark blue; the cure
restarts its step (`$F632` = 0), and the level's two palette-cycle
counters it borrows are handed to the battle and back.

**The approach to Mother Brain** (`loc_70D6`). NoahF1's corridor floor
(block `$33`, column 16) with the leader at cell 33 or north of it and the
*mother brain fought* flag clear: NoahF1 again with the party at x `$210` on
his row and the arrival scene pending. No sound. game.c now takes a pending
scene from the walk (`Walk.go_scene`).

**The arrival scene** (`loc_7E02` case `$E`): the leader facing up one cell
further down, **Mother Brain's image** (object `$2C`, `loc_50AE`) and two
**energy arcs** (object `$45`, `loc_59FA`), and the confrontation with no
button. Her image is hidden for 29 frames, forms over 20 (frames 3-6, the
sparkle), then stands flickering between frames 7 and 8 with frame 9 every
40; each arc shows after 26 frames and deletes itself about 48 later.
Their art is Noah's own (tile `$400`), so they needed only
scene_objects.json entries.

**The confrontation** (level event `$37`, `loc_E122`). It waits for her image
to stand, turns on **Mother Brain's glow** on the level (palette entry
`$2D` through a slow red pulse, `$F656`/`loc_66C0`), and asks "Will they have
to fight to pry Algo from the grasp of Mother Brain?" NO: her laugh (`$1816`
into `$1817`), the image goes, the glow stops; walking north starts it all
again. YES: "... disable me! If not, return now!" (`$1814` into `$1815`) and a
second question. YES there ("return") is `$1817` on its own -- the box opens
mid-sentence on "destruction.", a ROM quirk, route mother_brain_return's PNG
-- and it ends the same way. NO is "I will show no mercy. Die!" (`$1818`),
the *mother brain fought* flag set and her battle, with the aftermath scene
pending for the level after it. Two ROM consequences worth knowing: losing is
the ordinary game over, and running away (a 1-in-256 roll against any boss)
leaves the flag set and the aftermath pending, as if she had been beaten.
The port keeps the pending scene outside the flag block the ROM keeps it in
(`event_flags`, `$C710`), so a game over now drops it -- CONTINUE would
otherwise have run the aftermath on the next level.

**Mother Brain's battle.** Her battle data's fifteen colours are all black:
everything of her on screen comes from **Mother Brain's lights** (the
battle's palette effect 2). Every fourth frame the chase lights one of eight
colours of her picture at a time and steps along (`loc_6958`, then `loc_6978`
after its first lap); once it has lapped, the glow fades eight of her colours
in from dark blue to near-white (`loc_6998`) and then slides back and forth
along a white-cyan-blue ramp (`loc_6A18`); after the glow's fade-in, six more
fade in through the blues (`loc_6A44`) and then slide along the rainbow --
blue, cyan, green, yellow, orange, red, pink, purple (`loc_6A8C`). The chase
waits while the backdrop is red from a hit on the party. **Her picture**
across the top of the backdrop (`loc_AE8A0`, 40x14 at plane B row 0, over the
floor's first two rows) is drawn from her own art. All seven tables were
checked word for word against the disassembly.

**Names.** `WinSys.de70`/`de72` are `event_step`/`event_done`, `Battle.cc98`
is `palette_effect`; `FLAG_PANDORA_BOX_OPENED` and `FLAG_MOTHER_BRAIN_FOUGHT`
in gamedata.h. The save's two flags are renamed `c745_pandora_box_opened`
(was `flag_skip_darum_teim_event`) and `c747_mother_brain_fought` (was
`flag_loc_70d6`); the loader reads a key's address prefix only, so older
saves load.

Extractors changed: `make extract-npcs extract-nametables extract-flag_names`
(the nametable target wants ART_DIR to exist; with an empty one it skips only
the tables that binclude art), then `make manifest`.

Routes: 67, all green; mother_brain_no, mother_brain_battle (two pixel
checks on her lights) and mother_brain_return are new and fail on build 91.
Not yet ported: the aftermath (`$38`) and everything after it -- Lutz's
warning, the Earthmen, the two scripted walks, the ending screen.

## Build 93: after Mother Brain -- the aftermath, Lutz's warning, the Earthmen

Built on 92. Prompt task 1, first checkpoint. Names in the text; the ROM's
labels and addresses in parentheses, for checking.

**The aftermath** (level event `$38`, `loc_E1E4`). Started with no button on
the level after her battle (the one-shot case `loc_8070`). One step a visit
with no window open -- the visits go on while a scripted walk runs, which
is why its third step tests the walk rather than counting on:

1. The big box. Every party member's HP to their maximum; the ones found at
   0 are counted, and if there were any, **Lutz's revival message** (`$181E`,
   "Then they could hear Lutz's voice ... came back to life"). If nobody
   died, the ROM writes no script at all -- and every script shown before
   has been shifted out of the queue by then, so **the box opens empty** for
   a visit before the next step fills it. That is ROM behaviour, kept
   (route mother_brain_won, shot 02_empty_box).
2. **"Stood in amazement"** (`$183B`, which has no terminator and runs on
   into `$183C`, "... a sense of hope"), then **"Let's get back to Mota"**
   (`$181F`) from the queue.
3. The box closed and **the walk after Mother Brain** started (DemoScriptPtrs
   entry 9, `loc_3AC4`). No step count here: the visits come back to this
   step while the walk runs and find it running.
4. **"{char} wait! ... There is still somebody in the ship!"** (`$1831`).
5. Every window closed (event step and done back to 0), the same walk
   resumed one byte past its stop, and the event becomes Lutz's warning.

**How the two walks step the event.** A walk's stop byte (`$FE`, `loc_37EC`)
counts the running event on by one and hands the pad back, and it does NOT
move the walk's read position: it stays on the stop byte. The walk after
Mother Brain is two walks in one script -- three steps back down the corridor
to a stop (aftermath step 3 -> 4), then eleven north with the camera forced up
(`$11`, up plus bit 4) to a second stop. The aftermath's last step resumes it
with the read position one past the first stop (`addq.w #1,
(demo_input_index).w`, loc_E288) -- `walk_resume_demo` in walk.c; restarting
it would walk the first three steps again. The second stop takes Lutz's
warning from step 0 to 1. The walk to the Earthmen (entry 10, `loc_3AD8`) is
thirteen north forced, four plain, three idle reads and a stop, which takes
the Earthmen from 0 to 1.

**Lutz's warning** (event `$39`, `loc_E29E`). Step 1: the music "Exclaim"
(`$91`), the big box, "Who are these people?" (`$1832`). The next visit: every
window closed, the walk to the Earthmen started, the event becomes the
Earthmen at step 0.

**The Earthmen** (event `$3A`, `loc_E2E2`). "Welcome to the spaceship Noah ...
Are you here because you think we are enemies?" (`$1819`) and a YES/NO: YES
"Of course we hate you" (`$181A`), NO "Thanks to the Mother Brain which we
created" (`$181B`); both then "So you are the creators ..." (`$181C`) and
"We are not people of Algo ... Do you want to know why?" (`$1833`), queued.
Second YES/NO: YES is **the story** (`$1834`, running on through `$1835` and
`$1836`, "... You will die!") and straight to the last step; NO is "Since
you are going to die anyway, you might as well know." (`$1837`) and a third
YES/NO -- YES "In place of the home we have lost ..." (`$183F`), NO "If you
want to know, we will tell you." (`$1838`) and then the story after all.
Last step: "Thanks to Lutz's power, they were sent on their way ... attacked
by several hundred earthmen!" (`$1821`) and **the six on the bridge**: the ids
2-7 not in the party's first four slots are appended after the party, and six
following-character objects are written over object slots 1-6 -- the party's
own followers among them, in slot order -- standing (their third routine is
a plain return), facing up, at the six positions of
`NoahEnding_TeleportedCharPos`. A member at 0 HP gets no sprite (`loc_11868`
skips the id and packs the next into the slot). The party count is forced to
three afterwards whatever it was. The visit after: **the ending screen**
(`loc_E458`).

**The ending screen** (`EndingScreen`, ps2.asm:10795) is a screen now,
`SCREEN_ENDING`: the music "The Place of Death" (`$93`), the same fade to white
a battle opens with (`loc_634E`), the fade in from white over 21 frames
(`loc_62CC`), and A, B or C back to the Sega screen (`loc_785C`). Not ported
yet, and said once on stderr: the six epilogue pages (a 15x23 portrait slid
in on plane A, palettes `$44`-`$49`, scripts `$182A`-`$182F` typed beside it
for 360 frames each), the strip wipe and last picture (`$1830`, palette `$4B`),
the credits (EndingCredits_Script, a line every eight pixels of plane A's
scroll) under "Never Dream" (`$97`), and **the UFO** (object 8,
`Obj_EndingUFO`): at x `$156`, y `$100`, hidden for 29 frames, then five
frames of its mappings two frames apart, then gone. It is switched on by the
credits' palette fade (`loc_6AE0`) when its step table runs out, sixty-odd
seconds in. Its art and the others' are inline blobs (`loc_AF82A`,
`loc_B6E30`, `loc_B6C2C`, the starfield `loc_B7572` and thirteen portrait and
wipe tables in `loc_79F2`), so they need extractor entries first.

**Running away from her**, for the record: the *mother brain fought* flag is
set when her battle starts and the aftermath is pending from then, so a
successful RUN (a 1-in-256 roll against a boss) lands on the aftermath as if
she were beaten. Losing is the ordinary game over, which drops the pending
aftermath (build 92).

**Harness.** `enemy-hp N` (route_probe): every live enemy's HP set to N.
The winning route uses `enemy-hp 1` at FIGHT/STGY: a level-50 party in the
Nei items still loses to her (about 140 damage a round against 2450 HP).

Routes: 68, all green. **mother_brain_won** is new: her battle won, the
aftermath with nobody dead (the empty box), both walks and their cells
(33,37 after three steps back; 33,26 at Lutz's warning; 33,9 at the
Earthmen), "Exclaim", YES and YES to the Earthmen, the six on the bridge and
the ending screen. On build 92 it fails at "stood in amazement" with "level:
event $38 is not ported". Nothing extracted changed: no `make extract-*`
target to rerun.

## Build 93a: the ending screen in full

Built on 93. Prompt task 1. Names in the text; the ROM's labels and
addresses in parentheses, for checking. The whole screen is now
`src/game/ending.[ch]`, driven from game.c through a small host struct
(video, VRAM tiles, font, window system, fade target, sound queue).

**What the screen does, frame by frame** (EndingScreen, ps2.asm:10795,
read in full with every routine it calls):

1. *Setup.* "The Place of Death" (`$93`) is queued first, then the same
   fade to white a battle opens with (`loc_634E`). With the display off:
   the font placed as DrawArtTiles2 leaves it (colour 11, the window fill,
   cleared from every tile), the epilogue pictures at tile 1 (`loc_AF82A`),
   the starfield-and-UFO art at tile `$480` (`loc_B6E30`: the starfield's
   50 tiles, then the UFO's 45), the credits' 26-letter alphabet at tile
   `$500` over the font's first 26 tiles (`loc_B6C2C`; no text code uses
   those, every upper half is the fill tile `$26`), the starfield on plane
   B (`loc_B7572`, 40x28), **both planes 64x64** (`$9011`), the title
   palette as the fade's target with **line 3 black**, every entry white.
2. *Fade in* from white over 21 frames, one channel a frame, blue first
   (`loc_62CC`/`loc_631E`).
3. *Six epilogue pages* (the page routine, `loc_7932`; the pictures,
   `loc_7914` over `loc_79F2`'s entries 0-5). Each: the portrait (16x24,
   plane A row 2, column 4 or 20) drawn, its palette straight into line 2
   (`$44`-`$49`), then **the slide**: plane A's scroll preset to `$120` on
   one or both axes and stepped 32 px a frame back to 0 -- page 1 from the
   right, 2 up from the bottom, 3 and 6 from the bottom right, 4 from the
   right, 5 down from the top (the only one that takes nine frames). Then
   **the text**, typed beside the portrait (row 11, column 22 or 2; up to
   twelve lines): 360 loops of two frames, the window processor at the
   first frame's VBlank and CheckPrepareWindows after the second, so one
   code every two frames and a line break every eight. The controls are
   locked, so no button hurries it (`loc_FB56`'s `loc_FBE8`). Then plane A
   cleared (`loc_799A`) and 60 frames of nothing. The pages are Rudo, Amy,
   the red-clad swordsman, Shir, the green-clad technique user and Anna,
   with scripts `$182A`-`$182F`. **The starfield is invisible throughout**:
   its palette line (3) is black until step 5.
4. *After page six*, 120 frames more (`loc_79EA`), then palette `$4A` into
   line 2 and **the strip wipe**: loc_79F2's entries 6-12 are seven windows
   into one 40x14 table (`loc_B55CC`, read as one run), a row apart, and
   drawing them a frame each opens the last picture -- Rolf on a light
   band -- from its middle two rows (plane rows 13-14) out to all
   fourteen (rows 7-20). 360 frames on it.
5. The music faded (`$E0`), the fade to white, palette `$4B` into the
   target's lines 2-3 and `$A44` into line 1 entry 1 (the lowercase
   credits' colour), plane A cleared, 60 frames of white, the fade in
   again -- **the stars appear** -- and the page routine once more with no
   picture and no slide: "I wonder what the people will see in the final
   days?" (`$1830`, row 8, column 12), 720 frames, cleared, 60 frames.
6. *The credits* under "Never Dream" (`$97`): one step every three frames
   (`loc_7824`). A step runs the objects, builds the sprites, runs the
   credits' palette fade, scrolls plane A up a pixel, and every eighth
   pixel **the credits writer** (`loc_787E`) fills the row 256 px below
   the top of the view (32 px under the screen) from EndingCredits_Script:
   a text line then two blank rows (one after `$C3`, 28 after `$C4`),
   blank runs for bytes under `$14`, a letter at tile `$4BF` + its code
   with lowercase (bit 5) the same tile in palette line 1. It stops when
   the next byte is the script's `$C5`: step 3704, with "THE END" at
   screen y 96.
7. *The credits' palette fade* (`loc_6AE0`): 3600 steps in, then every
   eighth step a row of a nine-row ramp (`loc_6B10`) into line 3,
   entries 9-12, from black to its brightest -- the planet's rim lighting
   up. Its ninth row (step 3665) starts **the ending UFO**.
8. *The ending UFO* (Obj_EndingUFO, object 8, ps2.asm:2505): in screen
   coordinates at `$156,$100` in the first object slot, hidden for its
   first 29 runs, then its five frames (`loc_B7E32`: a glint at the
   planet's rim for three, then the ship pulling away, larger, up and
   left; the last frame 47 pieces) two runs each; its sixth frame sets
   bit 0 of its flags and RunObject deletes it. So it is drawn for steps
   3694-3703 and deleted on 3704, the very step the credits stop on --
   worked out independently from the object and from the writer.
9. *A, B or C* (`loc_785C`): the planes back to 64x32 (`$9001`), the Sega
   screen.

**Checked against an independent composition.** A Python composer built
from the extracted tables and the reading above -- no code shared with
ending.c, the credits writer replayed separately -- matches the port's
frames pixel for pixel: page 1 and page 5 (their typed text masked), the
last picture, mid-credits at step 1500, the UFO's frames 3 and 4 at steps
3700 and 3702, and "THE END" at step 3704. The route's pixel values come
from it.

**Build 93 slips, fixed.** The music started at the screen, 21 frames
after the white fade it should precede. The fade in from white used the
fade-up-from-black step (all channels at once, 7 frames) where the ROM
steps one channel a frame for 21; the ROM's step, which battle.c already
had as a static, is now `palette_fade_from_white_step` in palette.c, and
the fade into white `palette_fade_to_white_step` beside it (game.c's copy
removed).

**The game over's message** (found on the way): GameOverScreen also
loads the font through DrawArtTiles2 (ps2.asm:12270), so its message sits
on the title picture with nothing behind it. The port placed the font as
it was, and the message sat in a solid box of colour 11 (`$800`) hiding
the middle planet. `tileset_place_clearing` in tileset.c now does
DrawArtTiles2's job for both screens. Route gameover_message.

**Core changes.** The planes have a runtime height (`video_set_plane_rows`,
32 or 64; storage for 64): the scroll size register sets both planes at
once, and every screen entry puts 32 back. `nametable_cell` decodes one
entry. `TABLE_MAX` 256 -> 512 (the credits script is 441 bytes).
`NPC_SCENE_PIECES` 33 -> 47 (the UFO's last frame). `win_plain_text`
clamps its entry to `WIN_MAX_ROWS` (the pages' twelve lines made it 28
rows of an 18-row array), and `win_drop_plain_text` removes the entry
when the plane under it is cleared.

**Extractors.** `extract_inline_art.py`: EndingPicturesArt,
EndingStarfieldUfoArt, EndingCreditsLettersArt. `extract_nametables.py`:
EndingStarfieldTileInd, EndingPage1-6PortraitTileInd (the ROM stores them
in the order 1, 2, 5, 6, 3, 4), EndingLastPictureTileInd.
`extract_tables.py`: EndingCreditsFadeRamp, EndingCredits_Script (and an
OUTPUT_NAMES map for tables whose label is not a name). `extract_npcs.py`:
the ending UFO as scene object `EndingUfo`. Rerun:

    make extract-inline_art extract-nametables extract-tables extract-npcs
    make manifest

(`extract-nametables` needs ART_DIR to exist; an empty directory is fine
-- the bincluded tables are skipped and keep their files.)

**Probe.** `wait ending MOMENT`, from `GameDebugState.ending_moment`.

Routes: 70, all green. **ending** is new (fails on build 93 at the music,
"the last music was $91"; past that at the fade, `$444` for `$6EE`; past
that at Rudo's portrait, `$000` for `$888`). **gameover_message** is new
(fails on build 93 with `$800` behind the message).

## Build 94: the harness against the last two bosses, and the story to the end

Built on 93a. Prompt task 2. Harness only: **nothing under src/ changed**
(the probe's state line gained words; tools/*.py). Names in the text; the
ROM's labels in parentheses.

**Anna's full TP against Dark Force was the harness, not the curse.** The
state line now carries the curse (below), and the trials counted it: Anna
is cursed in most Dark Force fights (16-27 of 30) but sealed in only 2-7,
and spent 0 TP under every policy, sealed or not. fight_smart gave damage
techniques only to Kain and Hugh (TYPE_TECHS, build 88), and her own
(Foi, Fanbi, Zan) never beat her weapon anyway. Her one useful technique,
Shift (+20 attack, dexterity and agility a cast, for the fight), measured
neutral: 26/80 wins with it against 27/80 without, though it did move her
off the bottom of the order the curse picks from (cursed in 16 of 30
fights against 21-22). It is there (`shift=True`) and off by default.

**fight_smart against a boss** (an enemy that cannot be run from):

- every member may be ordered a damage technique, not only Kain and Hugh,
  at a lower bar (BOSS_TECH_MARGIN 1.0, BOSS_TECH_GAIN_PER_TP 0.5: there
  is no way out to keep TP for) -- Rolf's Nathu about six times a fight;
- nobody whose techniques the curse sealed is ordered one;
- **Deban** from whoever knows it (Amy) while half or more of the living
  party is without it. Read in the port's ROM port: Deban's bit is set per
  member it lands on and nothing in the fight clears it (only
  RestoreCharDataAfterBattle), so one cast halves Dark Force's party-wide
  blast (`loc_2D08`) and every blow for the rest of the fight.

**A harness bug under every fight since the heals went in.** A technique
command stays stored and is cast again every round until the caster
cannot pay for it (`loc_251C`, "branch if we can still use the same
technique in the next turn") -- ROM behaviour. fight_smart never put
anyone back on ATTACK, so a heal was repeated on the same member, Deban
recast after it had landed, Shift cast until the TP ran out, and Kain's
last damage technique went on after the planner stopped choosing it. Now
(`reset_techs`) anyone on a technique not planned for them this round is
ordered ATTACK.

**tools/boss_trials.py** takes `--boss dark_force|mother_brain` and
`--policy old,techs,deban,both,all`, and records each member's TP spent,
curse kinds, sealing and techniques ordered. Mother Brain is entered as
the routes do (NoahF1 33,37, four up, YES, NO); an encounter on the way is
fought first. fight_smart can stop on the game over (`stop_on_gameover`)
and the trials allow 4000 steps: her fights run to 50000 ticks, and the
first run's 600 cut most of them off mid-fight. From story_noah_grind.json
(Rolf L41, Rudo L40, Amy L35, Anna L40, Kain L31), eight Trimates dealt to
each member, ten fights a party and policy:

| | old (build 91) | techs | deban | both | all (both + Shift) |
|---|---|---|---|---|---|
| Dark Force, four parties | 0/40 | 1/40 | 17/80 | 27/80 | 26/80 |
| Mother Brain, four parties | 0/40 | | | 24/39 | |

Deban is what wins; the party without Amy (Rudo+Anna+Kain) won 1 of 20
against Dark Force and 0 of 9 against Mother Brain. Rudo+Amy+Kain: Dark
Force 11/20, Mother Brain 9/10. With no Trimates dealt the same party won
Dark Force 1 of 10 (route_out/boss_trials_df_nomates.md). Tables:
route_out/boss_trials_dark_force.md, route_out/boss_trials_mother_brain.md.

**The probe's state line** gives each member's curse: ` cursed K` while a
kind is pending or repeats (1-3 lose every turn), ` marked` once 4-6 has
taken (never cursed again), ` sealed`, and ` deban`. play.py parses them
into `curse`, `sealed` and `deban`.

**The story to the end** (tools/story.py, three stages after noah_grind):

- `boss_party`: home to Paseo by the roof, Anna's consumables and her
  Crystal Cape to Kain (keep_from), Amy in at Rolf's house (BOSS_PARTY:
  Rolf, Rudo, Amy, Kain), gear, the roof back, the mansion's town (Aukba).
  Checkpoint story_boss_party.json.
- `dark_force`: first Ryuon for Trimates (BOSS_TRIMATES 32 across the bags
  with room -- Aukba sells none; bag room held it to 16), then the box as
  before, with up to BOSS_TRIES (4) tries, a loss continuing the last
  save. **Beaten on the first try**, Amy down, the others standing.
  Checkpoint story_dark_force.json.
- `mother_brain`: Ryuon's Trimates, Lutz's YES, NoahGroundF to NoahF1 by
  plan() (seven floor changes) on weapons alone, topped up at box_ready,
  then **north up her corridor on foot**: the corridor is block `$33`,
  every cell with the collision table's special bit -- the bit stairs,
  exits and holes carry -- so LevelMap keeps it out of its floor and
  plan() cannot end there; the stage travels to the plain floor at 33,40
  and walks. YES, NO, her fight (the first try was wiped out on the walk
  across Noah; **the second beat her** with nobody down), then the
  aftermath, Lutz's warning, the Earthmen (YES, YES, YES), the bridge,
  and the ending screen played to its last button and the Sega screen,
  with shots (route_out/story_mb: page 3, the text over the stars,
  mid-credits, the UFO, the end). A `wait idle` on the ending screen
  waits out the step limit, so the aftermath runs at `limit 600`.

    python3 tools/story.py --from noah_grind --to boss_party
    python3 tools/story.py --from boss_party --to dark_force
    python3 tools/story.py --from dark_force --to mother_brain

Routes: 70, all green (the probe changed; no route did).

## Build 95: the technique switch, one case a line

Built on 94. Prompt task 1 -- a refactor that must not change behaviour,
and proved not to (below). Names in the text; the ROM's labels in
parentheses.

**What changed.** `process_technique` in battle_obj.c dispatched the
fight's techniques through eight `case a: case b:` lines and eight more
labels falling through into a shared body, with the group techniques
jumping into the every-enemy body by `goto` when the technique came
from an item; the enemies' technique switch had two more such pairs.
Each shared body is now a named static function and every technique
has its own case line with its own body calling it:

| body | techniques | ROM |
|---|---|---|
| `tech_one_enemy` | Foi, Tsu, Brose, Gaj, Gen, Vol and their Gi-/Na- forms | TechAction_Foi |
| `tech_group` | Zan, Eijia, Savol and the Gi-/Na- forms; every enemy from an item | TechAction_Zan, its `bne.s TechAction_Gra` |
| `tech_every_enemy` | Gra, Sag, Sagen and the Gi-/Na- forms | TechAction_Gra, loc_2148 |
| `tech_afflict_enemy` | Conte and Shiza ("is quiet", bit 3), Doran and Forsa ("has gone insane", bit 4; Forsa from the middle of the screen) | TechAction_Conte/Doran/Forsa, loc_223C |
| `tech_paralyse` | Rimit, Rimet | TechAction_Rimit |
| `tech_boost_party` | Shu/Sashu (defense), Ner/Saner (agility), Deban | loc_22FA, loc_235C, loc_2330 |
| `tech_heal` | Res/Gires/Nares (the member), Sar/Gisar/Nasar (the party) | TechAction_Res/Sar, loc_2392 |
| `tech_give_up_life` | Sak, Nasak: `tech_heal` in full, then the caster's life | TechAction_Sak/Nasak, loc_23EC |
| `tech_shift`, `tech_fanbi`, `tech_shinb`, `tech_anti`, `tech_rever` | one each | their TechAction_ |
| `tech_megid` | Ryuka, Hinas, Musik, Megid | TechAction_Megid |

The every-enemy loop was in the file twice -- the ROM's Megid *calls*
loc_2148 with the rate set to 400, where the port had copied the loop --
so `tech_hit_every_enemy` is now called from both. Under them,
`tech_target` is the one-enemy actions' shared opening (loc_270C: the
target picked from the command's group and shown, the caster over it)
and `tech_hit_enemy` the roll, the damage and the info word (loc_27AA,
CalculateTechniqueDamage, loc_288E). The actions take one `TechCast`
record (the caster, the command, the properties, the hit rate, the
power, the target group, from-item) and return whether the technique
goes on to its animation; a `false` means the turn already ended
(nothing to do, or the command fallen back), which is what the old
`return`s before `finish_tech` were.

The enemies' switch: `enemy_tech_strike_one` for its entries 11 and 13
(two identical ROM routines, loc_2F5C and loc_3004) and
`enemy_tech_defense_down` for 17 and 18 (loc_307E and loc_30A4, which
differ only in the message -- and the second's, "Agility decreased!",
is the wrong one, as written in the ROM).

**Names.** The technique ids are an enum in gamedata.h, `TECH_FOI` ..
`TECH_MEGID` (TechID_* in the constants file, pinned by a static
assert), so the case lines and the item-to-technique table read by name;
menu.c's thirteen local copies of the same numbers are gone in its
favour. The enemies' technique table entries are `ENEMY_TECH_*`
(`ENEMY_TECH_CLONE`, `ENEMY_TECH_DARK_FORCE`, ...) in battle_obj.c.

**Proof of no change.** Before touching anything: every route's PASS
line (commands and ticks), the whole `route_out/` tree (every log and
PNG, 402 files, checksummed) and `boss_trials --boss dark_force --policy
both --repeats 10` for all four parties (40 fights), run twice to
confirm the trial itself is deterministic (it is, byte for byte). After:
the 70 PASS lines identical, the 402 checksums identical, the 40
fights' results identical. The trial run is a different draw from the
one build 94's table came from (Rudo+Amy+Anna 5/10, Rudo+Amy+Kain 3/10
here); it was compared against itself.

**Found, not fixed here (a refactor keeps what it found).** The port's
Shu and Ner raise the *first* member's defense or agility whoever was
chosen: the old switch took `party_member_id[0]` for both. In the ROM,
TechAction_Shu and TechAction_Ner leave a5 on the command's target word
-- only Sashu and Saner point it at the party list -- so they act on the
member the cursor picked, as Res does (and as the port's Res does). The
flag byte of both (TechniqueData: $11) is the one-member kind. The fix,
with a route that fails on this build, is build 95a's; `tech_boost_party`
carries the note.

Routes: 70, all green, none changed. Nothing under `assets/` changed; no
extractor changed.

## Build 95a: Shu and Ner on the member chosen

Built on 95. What build 95's reading found, fixed with its route.

**The bug.** The port's Shu and Ner raised the *first* member's defense
or agility whoever the cursor chose: the old switch took
`party_member_id[0]` for the single-member forms. In the ROM,
TechAction_Shu (ps2.asm:3288) and TechAction_Ner (3323) set the loop
count to 0 and join their party forms' loops (loc_22FA, loc_235C) with
a5 still on the command's target word -- the member picked on the party
triangle -- where TechAction_Sashu, Saner and Deban point a5 at
`party_member_id` first. Both techniques' flag byte (TechniqueData: $11)
is the one-member kind, the same as Res's, and the port's Res already
read the target word. Now `tech_boost_party`'s single forms take
`command[2]`, the shape `tech_heal` has.

**The route** (`shu_target`, 752 ticks): party 4 30 (Rolf, Nei, Rudo,
Amy), Amy's SHU on the triangle's stop 2 -- the stops run in the
screen's order (Battle_CharacterIdIndex), so stop 2 is Nei, party
position 1 -- FIGHT, "Defense ability increases!" ($120D), then Nei's
defense 64 -> 84 and Rolf's 133 unchanged. On build 95's code (with this
build's probe) it fails at Nei's: her defense stays 64, and Rolf's had
gone to 153.

**Probe.** `expect stat MEMBER hp|tp|attack|defense|agility LO [HI]`,
that party position's stat; `expect hp` is the older form for HP.

**No other change.** The 70 routes of build 95 are identical in ticks
and in every log and PNG (none of them casts Shu or Ner; the fuzz's
seed did not either), and the 40-fight Dark Force trial is identical
(fight_smart never orders Shu or Ner). Routes: 71, all green.

## Build 96a: script ids by name

Built on 95a. The first part of prompt task 2, the rename pass over
game.c, building.c and npc.c -- a refactor, proved not to change
behaviour.

**What changed.** Every script id in the three files is a name. The
names live in `src/game/script_ids.h` (255 of them), written by
`tools/gen_script_ids.py` from the extracted script text: the bank, then
the line's first words -- `SCRIPT_ACTION_LETS_GET_JET_SCOOTER` ($171C),
`SCRIPT_MEMORY_WELCOME_DATA_MEMORY` ($0301),
`SCRIPT_EVENT_WHATS_TEIM_DOING` ($1802) -- with the line as the game
shows it in the comment beside each. The fifteen endgame names build 93
gave by hand (`SCRIPT_LUTZ_REVIVES_THEM` and on) keep their names and
move from game.c into the header. The generator reads the header it
wrote, so a name once given stays when it is rerun.

One trap it guards: bank 0's ids ($0004-$000E) are small numbers that
also occur as palette words in game.c's tables, so a bank-0 id counts
only on a line that names a script (there is one, the "is dead" of the
Neifirst event).

**Also in the tree, not used yet:** `tools/gen_rom_ids.py`, which writes
`src/game/rom_ids.h` for 96b -- `MUSIC_*` (23) and `SFX_*` (55) from the
constants file's MusicID_ and SXFID_, `LEVEL_*` (101) from LevelID_,
`FLAG_*` for every named byte of the $C700 block (flag_names.json), the
A-button events by the port's names (`EVENT_PANDORAS_BOX`,
`EVENT_SHIR_GONE`, ...) and the demo walks in `DEMO_SCRIPT`'s order. The
header is not generated into this build.

**Proof of no change.** Against the baseline taken on 95a before
touching anything: the 71 PASS lines identical (commands and ticks), the
439 route_out logs and PNGs identical by checksum, and the 40-fight Dark
Force trial identical.

**Still to do for 96 (96b):** the flag bytes (`c700[0x16]` and the rest)
to `FLAG_*`, the event switch's `case 0x2A:` and npc.c's event numbers
to `EVENT_*`, the one-shot level events (the loc_7E02 switch, which also
has names to reconcile with game.c's `SCENE_*`), music and sound ids,
window ids and demo numbers; the local duplicate defines in game.c and
building.c (`FLAG_C712`, the `SFX_*`, `LEVEL_*` and `ITEM_*` copies) in
favour of the headers; ROM labels into comments.

Routes: 71, all green, none changed.

## Build 96b: flags, events, scenes, sounds, levels and demos by name

Built on 96a. The rest of the rename pass over game.c, building.c and
npc.c (prompt task 1) -- a refactor, proved not to change behaviour.
Names in the text; the ROM's labels in parentheses.

**The header.** `src/game/rom_ids.h` is now generated and used
(`python3 tools/gen_rom_ids.py path/to/constants.asm`, from ps2/). From
the constants file: `MUSIC_*` (23), `SFX_*` (55), `LEVEL_*` (101) and,
new, `ITEM_*` (128). By hand, each entry read in ps2.asm first:

* `EVENT_*`, every A-button event object 0-$40
  (EventType_DialogueIndex, ps2.asm:19875), each with its routine in
  the comment. Reading them all corrected two: event 5 is Darum's face
  ("I've seen that face!", loc_E9A8, the same routine as the Teim and
  Darum kind's first entry), not Neifirst's; and $2B, still unported, is
  the rocky mountain on Motavia ("Is this Uzo mountain?", loc_DF24). The
  eight card slots are `EVENT_DAM_LOCK_1..8` -- the four dams' doors and
  panels, whose own bytes are the run `FLAG_DAM_LOCKS_FIRST..LAST`.
  The opening's six pages over Motavia are `EVENT_OPENING_PAGE_1..6`
  (loc_E47C on).
* `EVENT_KIND_*`: the A button's target word's high byte (events,
  people, chests, Teim and Darum; AButton_ObjTypeIndex), and
  `TEIM_DARUM_*` for the last kind's three entries -- Darum's face, the
  scene, and "{char} is dead." at Rolf's own door.
* `SCENE_*`: the one-shot level events (loc_7E02), reconciled with the
  two game.c already had -- `SCENE_NEI_DYING`, `SCENE_NEI_DEAD`,
  `SCENE_CLIMATROL_SHAKES`, `SCENE_MOTAVIA_BEFORE`,
  `SCENE_CLIMATROL_EXPLODES`, `SCENE_MOTAVIA_AFTER`,
  `SCENE_RETURN_TO_PASEO`, `SCENE_GAIRA_PRISON`,
  `SCENE_PANDORAS_BOX_OPENED`, `SCENE_SHIR_GONE`, `SCENE_ESPER_B1_HOLD`
  (a case that does nothing and is set only to hold the party),
  `SCENE_MOTHER_BRAIN_ARRIVAL`, `SCENE_MOTHER_BRAIN_DEFEATED`,
  `SCENE_OPENING`. game.c's own "scene" position macros are
  `SCENE_PICTURE_COL/ROW` now, so the prefix means one thing.
* `DEMO_INDEX_*`: DemoScriptPtrs (ps2.asm:5401) in full, 0-10.
* `FLAG_*`: the $C700 block's bytes. Where flag_names.json names a
  byte only by the first routine that touches it, the generator's
  `FLAG_NAMES` gives it a name read from its uses: the towns visited
  (the base of that list), the scene camera (event_flags' low byte),
  the Piata tower unlocked (set at the Governor's first debrief, read
  by Piata's tower door), the Darum scene done, the Recorder found,
  Climatrol destroyed (set by the explosions; Motavia's map patch reads
  it), Gaira off its orbit (the explosion event's own byte; the ring
  and the drifting scroll read it), the door Espers letting Rolf in,
  the B1 Esper's talks, Lutz having seen the Nei items, and the Esper
  mansion's two chests (`FLAG_CHEST_PRISM`, `FLAG_CHEST_NEI_SWORD` --
  chests $0E and $0F's own bytes, preset to 1 by a new game). Runs the
  port indexes: `FLAG_EVENT_SLOTS` (event n's byte), `FLAG_CHESTS`
  (chest n's), `FLAG_DAM_LOCKS_FIRST/LAST`. The save's keys are
  unchanged; the header lists the bytes it does not name yet.

**What changed in the code.** The A-button event switch, the ported
list and the door lines; the one-shot switch (its cases, the next case,
the events it starts, the music latches -- Advanced, Restoration,
Pleasure); every `c700[0x..]`; the Teim scene's target word and music
(Dream, the disassembly's "A Prologue"); the card doors' item (event
less the green card door, plus the green card); levels (Gaira, Noah's
ground floor, Ryuon as the last town, the Esper mansion's floors, the
Darum tube), sounds and the demo indices; npc.c's flag reader and its
two flag locals (`nei_gone`, `climatrol_destroyed`); walk.c's tower
flag (`piata_tower_unlocked`) and Rolf's door. The Governor's check reads
a word, so its second byte is the *Nei gone* flag: the code says so now.

**Removed duplicates.** game.c's and building.c's local copies of
sound, level, item, music, event and demo numbers, building.c's
`FLAG_C712` / `FLAG_C722` / `FLAG_GOVERNOR` / `FLAG_ROOF_OPEN`,
npc.h's `NPC_LEVEL_DARUM_TUBE`, `NPC_LEVEL_ESPER_B1` and
`NPC_EVENT_BOARD`, walk.h's two scene numbers, gamedata.h's two flags.
Kept, with names, because the ROM's lists do not have them: the sound
driver's fade-out and stop-effect commands.

**Not done here.** menu.c's own item copies and walk.h's three sound
copies (outside the three files); two townspeople's dialogue numbers in
npc.c; the eight dam bytes one by one (which door or panel of which
dam: the run's name only).

**Proof of no change.** Against the baseline retaken on 96a before any
edit: the 71 PASS lines identical (commands and ticks), all 405 files in
the 71 routes' output directories identical by checksum, and the 40-fight
Dark Force trial identical. Route, headless, building, menu and pause
probes build without a warning. No extractor changed; nothing under
`assets/` changed.

Routes: 71, all green, none changed.

## Build 96c: the walk to Mother Brain heals, and Rever on the map

Built on 96b. Prompt task 2 -- the walk across Noah to Mother Brain was
fought on weapons alone and wiped out once in build 94 -- and a port bug
it turned up, fixed with its route.

**Before.** build 94's stage, rerun unchanged on 96b (`python3
tools/story.py --from dark_force --to mother_brain`), gives build 94's
report line for line: the first walk wiped out on Noah, the second
through, 57 fights. The log says why. It was not the missing heals: in
the last fight (two Vorcanos), once anyone alive was under a quarter,
fight_smart ordered STGY/RUN every round; the escape failed round after
round ("can't escape"), nobody attacked or healed, and all four went
down. Two members had also walked on dead from earlier fights -- the
harness had no way to revive between fights.

**The harness** (tools only):

* `play.revive()`: Rever from whoever knows it and has the TP (Rolf and
  Amy both learn it on the map), else a Moon Dew, on each dead member.
* `Session.walk_mend`: set to a fraction, `goto` revives, cures poison
  and mends to it after every fight on the way. None, the default, keeps
  the old rule for every other stage.
* story.py's Mother Brain walk: `WALK_FIGHT` (heal techniques and mates
  in a fight only for someone low -- heal under 0.4, a mate under 0.3 --
  no damage or type techniques, nothing a boss fight orders, and no RUN)
  and `WALK_MEND` 0.5; a revive at her corridor before the ready check;
  the corridor's log line counts the mates left.

Two settings were tried first and are why these are the numbers. Mending
to 0.7 after every fight with heals at the usual 0.5 spent all 18
Trimates and Rolf's and Amy's TP on the walk: three tries reached her
corridor with nothing left for her, and the fourth with Rolf dead, whose
Nei Sword -- the way back to the mansion -- a dead member cannot use.
Heals at 0.35 with RUN still allowed was wiped out on the first walk,
the same way as build 94.

**After** (route_out/story_mb_walk): the first walk reaches her corridor
with nobody down, all 18 Trimates carried, Rolf TP 123/146, Amy 57/135,
Kain 107/107; Mother Brain beaten on the first try (15617 ticks); the
aftermath, Lutz's warning, the Earthmen and the ending to the Sega
screen; 27 fights in the stage, none ending with anyone dead. One
deterministic run: it shows the walk that failed now goes through, not
how often it would.

**The bug: Rever on the map revived nobody.** Checking `revive()` on a
save with two dead members spent Rolf's 30 TP and showed "... comes back
to life!" ($10A), and the member stayed dead. TechEffect_Rever
(ps2.asm:15564) takes the TP and the message at its step 1, and from
step 2 joins Res's code at the window close (loc_A6BE) -- after one
subtraction of the step count, where TechEffect_Res makes two. So
Rever's step n is Res's step n + 1: the close, 30 frames, then 30 HP
(loc_A6D4's Rever case) and SXFID_Revived. The port's `tech_rever`
handed Res the step as it was, so the step after the message was Res's
close for a dead target (loc_A6B8) and the heal never ran. Now it passes
`d1 + 1`. (Rever on a living member takes the same path in the ROM:
$104, and 30 HP.) The Mother Brain run above is the same before and
after this fix -- nobody died on its walk.

**The route** (`map_rever`, 566 ticks): routes/saves/rever_rudo_dead.json
(story_dark_force.json with Rudo's HP at 0, Aukba), C, TECH, Rolf,
Rever, Rudo: `$10A`, Rolf's TP 146 -> 116, Rudo's HP 0 -> 30. On 96b's
code it fails at Rudo's HP (0).

**No other change.** The 71 routes of 96b are identical in PASS lines
and in all 405 output files, and the 40-fight Dark Force trial is
identical (neither casts Rever on the map). Routes: 72, all green.

## Build 97: the walks across Noah trialled, and their policy set from it

Built on 96c. Prompt tasks 1 and 2 (harness only), then task 3 (below).

**The walk trial** (`tools/walk_trials.py`, docs/walk_trials_build97.md).
In the manner of boss_trials: the two walks the last stages make from
Lutz's YES -- to the foot of Noah F1's corridor (the Mother Brain stage)
and to the cell below Pandora's box (the Dark Force stage) -- many times
per fight policy. A start save per walk is made once by the stage's own
preparation and the probe's `save-here` on Noah's ground floor
(`routes/saves/walk_mother_brain.json`, `walk_dark_force.json`); each run
is a fresh probe that continues it, waits 7 k ticks and `travel`s with the
policy, the same offsets for every policy. Fifteen policies: build 94's
(weapons, RUN), 96c's WALK_FIGHT / WALK_MEND, and the heal, mate and mend
thresholds varied one at a time around each. Recorded: reached, wiped out,
box_ready at the end, TP and mates left, members down and revived.

What it found. Weapons and mates alone arrive fit for the boss far more
often than 96c's healing walk (Mother Brain, 300 seeds: 239 against 195;
Amy arrives with 71 of 135 TP under 96c, about 120 on weapons -- 96c's
heals in a fight are Res where a Trimate would do). RUN costs: build 94's
walk was wiped out 17 times in 300 against 7 without it. And reviving the
dead and mending to half after each fight helps: 600 seeds a walk, the
Dark Force walk wiped out 2 times against 16 (the Mother Brain walk 17
against 13, within noise). Wipe counts of 1-7 in 100 do not rank policies;
the 300- and 600-seed rows do.

**Set from it** (tools/story.py): `WALK_FIGHT` = weapons and mates, no RUN;
`WALK_MEND` = 0.5 -- for both walks. `dark_force_once` now walks with them
(it had fought on weapons with RUN allowed and nobody revived) and revives
at the box before its ready check, as the Mother Brain stage does at the
corridor.

**The stages rerun.** `story.py --from boss_party --to dark_force`
(route_out/story_df97): the first walk to the box fit (9 mates, Amy's TP
full), Dark Force beaten on the first try (21460 ticks), 38 fights, nobody
down; checkpoint story_dark_force.json rewritten. `--from dark_force --to
mother_brain` from it (route_out/story_mb97): the first walk reached her
corridor fit (14 mates, Amy's TP full) and her fight was lost, all four
down (9741 ticks); the retry from the last save walked there fit again,
beat her (20775 ticks) and played the aftermath and the ending to the Sega
screen. 70 fights, one ending with anyone dead (hers). The loss is the boss
fight's own odds (build 94's trial: Mother Brain 24 of 39), not the walk;
the trial does not measure how the four mates fewer the weapons walk
arrives with trade against her fight -- that is boss_trials' question.

## Build 97a: the rename pass finished, the dams' bytes one by one, the save's names

Built on 97. Prompt task 3: a refactor, proved not to change behaviour,
then the save's flag keys renamed (The owner asked for his own slot to be
updated this once).

**The dams' eight bytes, read.** Each card's item action (ItemAction_
GreenCard .. RedCard, ps2.asm:14407) carries its dam's door event -- green
$1C, blue $1E, yellow $20, red $22 -- and the target search (loc_9C12)
returns the faced event n with a1 = event_flags + n, so a card sets the
byte of the event it is used on. On the door (loc_9D3E) that is the door's
byte and the key walk (demo 5), the door swinging open; on event n + 1, the
panel (loc_9D70), the panel's byte, the card used up, and the level
screen's camera case 8 (event_flags' low byte) -- the check (loc_81E6) that
wants all eight at 1 before the Army Eyes' battle. With Climatrol
destroyed, each panel's byte also patches Motavia's map at its load
(loc_9258: $C72F, $C733, $C731, $C72D, each through loc_9302 with its own
table; loc_9302 read only in its opening). So: green door $C72C, panel
$C72D; blue $C72E / $C72F; yellow $C730 / $C731; red $C732 / $C733 -- the
event records agree (each door on its dam's lower floor, each panel
higher).

**Named.** `FLAG_GREEN_DAM_DOOR` .. `FLAG_RED_DAM_PANEL` (tools/
gen_rom_ids.py's FLAG_NAMES; `FLAG_DAM_LOCKS_FIRST/LAST` kept for the run
game.c walks), and `CAMERA_DAMS_CHECK` (a new list of the level screen's
camera cases, only the one read in full: game.c's own cases 1-4 and 6 are
still numbers).

**The code.** menu.c: its local sound, item and level copies removed
(every one had the header's value; four levels take the header's longer
names -- the Esper mansion's floors, the Underwater Passage, Noah's ground
floor), rom_ids.h included, and the item-on-fixture code by name: the event
kinds (the target word's high byte, compared as before), the water around
the Underwater Passage for Mruragum, the dynamite doors, the strong shutter
for the Key Tube, the four cards' door events, the event and chest runs
(`FLAG_EVENT_SLOTS`, `FLAG_CHESTS`), the dynamite and key walks, and
`CAMERA_DAMS_CHECK`. walk.h's three sound copies removed; walk.c and game.c
queue `SFX_LEVEL_CHANGED` (loc_6F56, loc_6E4E), `SFX_FELL_IN_HOLE` (the
Crevice falls, loc_6FCA) and `SFX_DANGEROUS_FLOOR` (loc_7108), each checked
in ps2.asm. Left as numbers: menu.c's script ids (script_ids.h covers
game.c, building.c and npc.c).

**Proof of no change.** A baseline retaken on 97 before any edit: the 72
PASS lines (commands and ticks), all 410 files in the 72 routes' output
directories by checksum, and `boss_trials --boss dark_force --policy both
--repeats 10` (all four parties, 40 fights, 9 won). After: all 72 PASS lines
identical, all 410 files identical, the trial's results identical (only
its `out` argument differs). The probe builds without a warning.

**The save's names.** The save's keys come from assets/data/flag_names.json
(tools/extract_flag_names.py). Seventeen bytes it still called by the
routine that touches them, or by an event's place, now have names in the
extractor's hand table: the eight dam bytes (`green_dam_door` ..
`red_dam_panel`) and the nine build 96b named in the header only
(`scene_camera`, `piata_tower_unlocked`, `darum_scene_done`,
`recorder_found`, `climatrol_destroyed`, `gaira_off_orbit`,
`door_espers_let_rolf_in`, `esper_b1_talks`, `lutz_saw_nei_items`). The
first byte keeps its key (its header name is for the list it is the base
of). Checked: the unchanged extractor reproduces the old flag_names.json
byte for byte; the new one differs in those 17 names only; rom_ids.h
regenerates unchanged; the 72 routes' PASS lines and all 410 files are
identical with it (no route writes a save; the saves under route_out are
copies of the routes' own start saves, which keep the old keys and load by
address as ever). Rerun with `make extract-flag_names` (with the
disassembly) or `python3 tools/extract_flag_names.py ps2.asm
constants.asm -o assets/data`.

**The owner's slot** (saves/slot1.json, shipped in 97a only): the 17 keys
renamed in place, every value and every other section untouched. The loader
keys flags by address, so the name is a label; to show it, the original and
the renamed file were each continued in the game (Kueri 27,18, tick 376,
nothing on stderr) and saved back where the party stood: the two saves the
game wrote are byte-identical, and both carry the new keys.

Routes: 72, all green, none changed.

## Build 97b: the mates at the bosses, the rename pass's last numbers, the assets by name

Built on 97a. Prompt task 1 (harness only), task 2 (a refactor, proved), and
The owner's addition: every asset named for what it is, dead files purged, the
tree slimmed. Task 3 (the map-patch routine, NOTES 9) not reached.

**The mates at the bosses** (docs/boss_arrival_trials_build97b.md).
boss_trials.py gained arrivals: `--arrival LABEL:MATES[:NAME=TP,...]` and
`--walk-policies`, which start the fight from a walk_trials start save as a
walk leaves the party -- no refill, the Trimates cut in the order the walk
spends them, the TP set -- from the means over each policy's box_ready walks.
500 fights a row: at Mother Brain the five mates fewer cost 24 fights in 500
with TP equal, but the weapons walk's whole arrival (Amy's TP kept) won 398
against 373; at Dark Force every gap is within noise. With the walks counted
a try wins Mother Brain 0.77 against 0.70, Dark Force 0.37 against 0.40. So
the fewer mates do not lose more than the walk saves: WALK_FIGHT, WALK_MEND
and boss_supplies unchanged, the stages not rerun. The default path is
unchanged: with no arrival the trial reproduces 97a's 40 fights exactly.

**menu.c's script ids.** tools/gen_script_ids.py reads menu.c too. There the
ids are short literals (`0x01`, `0x102`) passed through menu.c's own helpers
(the describe path, the cards, the dynamite/Key Tube door, the window script
calls, ternaries inside them), so a literal counts only inside a call that
takes a script (`MENU_CALLS`); `--apply` rewrote the 88 of them to
`SCRIPT_*`. The dam panel's second visit (loc_9D92) clears the script --
`move.w #0, (script_id).w` -- and is `SCRIPT_NONE`. script_ids.h: 310 names.

**The level screen's camera cases, all eight read** (loc_809C, ps2.asm:11667,
with loc_8226, loc_8240, loc_824C): event_flags' low byte ($C711), every
frame of LevelScreenLoop.

| case | name | what |
|---|---|---|
| 0 | `CAMERA_NONE` | nothing |
| 1 | `CAMERA_WAIT_THEN_RELOAD` | demo_timer down; at 0 the byte cleared and screen_changed_flag -1: the level reloads |
| 2 | `CAMERA_SHAKE_THEN_RELOAD` | loc_8226 on each axis (plane A 0-7 px, plane B 0-3), then as 1 |
| 3 | `CAMERA_JITTER` | 0 or 1 px an axis a plane a frame; no timer |
| 4 | `CAMERA_WAIT_THEN_NEXT_LEVEL` | as 1, the reload going to $F748/$F74A/$F74C |
| 5 | `CAMERA_BACK_TO_WAY_OUT` | no window asked for or open: the level from $C64C/$C64E/$C650 (the gum's way out), reload |
| 6 | `CAMERA_GAIRA_COUNTDOWN` | no event, window or window object: demo_timer down, at 0 event $2E |
| 7 | `CAMERA_PAN_TO_TARGET` | a pixel a frame toward $C648/$C64A, then the next event from $F780 (the opening crawl) |
| 8 | `CAMERA_DAMS_CHECK` | the eight dam bytes at 1 -> the Army Eyes' battle; event_flags cleared |

The port's cases (game.c level_scene_camera: 1-4, 6, 8) agree with what was
read; every `case`, assignment and comparison is now by name. Nothing ported
sets case 5; case 7 is the opening crawl's own code.

**The assets by name** (tools/asset_names.py). With the owner's `art/` a full
`make extract` of the unchanged tree reproduces its assets (battle.json's
key order and Lutz's entry in nametables.json aside), so the renames went
into the extractors, from one shared table:

* the 77 PalettePtrs palettes (`ptr00`..`ptr76`) by what loads them --
  levels, portraits, battle, dream, the flight, Nei's pictures, the ending's
  pages; entries 8 and $43 are `unused_08`/`unused_43` (nothing names them),
  $32 `library_graph_overlay` (not ported). Code: `palette_load_entry(p,
  line, PALETTE_*)` (core/palette.c reads paletteptrs.json once); the names
  are in rom_ids.h (gen_rom_ids.py).
* the 29 `Pal_*.png` (extract_palettes.py) were byte-for-byte copies of
  PalettePtrs entries and none was loaded but two, now loaded by index:
  extract_palettes.py writes palettes.json only (extract_art's colours).
* the level sprites' 24 art sheets (`people_loc_*`) as `sprite_<name>`,
  named by the object types placed at their tiles; `level_sprites.json`
  keeps the label as `"source"`. One is unidentified: object $28's
  (`sprite_noah_f1_object_28`, loc_4DD8, not ported, NoahF1 33,7).
* the six tilesets (`level_tilesN`, `blocksN`) as `tileset_<name>` and
  `blocks_<name>` (motavia_outside, dezolis_skure, towns, caves,
  motavia_dungeons, dezolis_dungeons); game.c and level_probe take the files
  from the level's record; every .tmx names `blocks_<name>`.
* the tile animations by level; 15 abbreviated art names
  (`mot_arm_sel_port` -> `armor_seller_portrait`, `tit_scr_phan_star_logo_art`
  -> `title_logo_art`, `battle_carrier_act` -> `battle_carrier_art`, ...),
  applied in extract_art, extract_buildings and extract_battle alike.
* `loc_BA818.bin` is `FMRegisterOrder.bin` (fm_demo reads it).
* dropped: `tylr_spc_monitor_tile_ind.png`, a tile-index map decoded as art
  (extract_art skips the five `*_tile_ind` files, extract_nametables' own);
  `art_manifest.json`, which nothing read.

Checked: the new extraction against the old tree, file by file -- 169 files
byte-identical under new names, 2 dropped as above, and every changed JSON
or .tmx differing only in the renamed references (and the added `"name"`
and `"source"` keys). docs/game.md had Shure's dead bodies on the explosions'
art (loc_5AE9C); their list starts with loc_5B452 (8 tiles, the one 32x16
piece), now `sprite_dead_body`.

**Proof of no change**, taken twice against one baseline retaken on 97a
before any edit (72 PASS lines, all 410 route output files by checksum, the
40-fight Dark Force trial): after the script ids and cameras, and again after
the assets. Identical both times. Every target builds without a warning
(the SDL3 one not built here).

**The tree slimmed** (179 MB unpacked to about 30, 19 MB zipped to 4.5 MB): route_out kept only what
a doc cites or a tool reads -- the routes' own outputs, the walk trials'
results (boss_trials reads them), the boss trials' tables, 97b's trials and
proof runs, and the four story runs the docs name. Gone: the trial sessions'
logs (100 MB and 42 MB), debug directories, build 88-90's story runs and
superseded trial files, the arrival saves and transcripts; and
routes/saves' four `story_*_failed.json` (stages that later passed).

Routes: 72, all green, none changed.

## Build 97c: the dams' patches to Motavia checked, and the rocky mountain

Built on 97b. Prompt task 3, which 97b did not reach.

**The map patches, read in full** (docs/game.md, the level screen's map
patches). The level-load routine (loc_9258) and its run writer (loc_9302),
all of both. Build 97a's account was right -- each dam panel's byte patches
Motavia's map at its load, once Climatrol is destroyed ($C735), each
through the run writer with its own table -- and the port matches the ROM
throughout: the four tables entry for entry, their order (blue, red,
yellow, green), the whole-map change under $C735 alone (every $8B, dry
orange ground, to $89, the sea, and four more cells), the Prism's four
doors on Dezolis and Noah's block after Mother Brain. No code change; the
item-use code's comment on the panels now says what the tables do (they
fill the empty canals). Route motavia_dam_patches: the same place with no
flags, with $C735 alone, and with the four panels -- PNGs for checking
against the emulator.

**The rocky mountain on Motavia** (ObjType_Events $2B, loc_DF24; NOTES 9).
Read with loc_DDA2 (the leader's name), the A button's small box
(loc_D84A, ObjType_Events' first test) and LoadScript's end code. Ported:
$171F in the small box, $DE72 + 1 so the next button closes it, and the
leader named. The event was missing from the ported list, so A there gave
people line 0 ($1708).

The script is cut short in the ROM. Its first page ends in $C4 -- the
disassembly: "this should be $C3", and "cut-off text" over the rest -- and
LoadScript stores any code $C4 or above and returns (loc_11268), so the
game shows "There is a high rocky mountain in front of" and nothing more;
"{char} and the others! Is this Uzo mountain?" is never loaded, and the
leader's name is never seen. The port's text ends there too: ROM
behaviour, kept. (Showing the whole line would be a one-byte change to
the extracted script; not done.)

It is on Uzo's island (cell 173,96). The door record at 173,97 catches any
step target within two cells (loc_6E4E), so a step up from below it is
Uzo's entrance; one cell left, at 172,97, the mountain turns the party
without a step, and A there reaches the event. Route mount_uzo: fails on
97b ("nothing unusual here"), passes on 97c.

**Fixed on the way.** tools/gen_script_ids.py read 97b's hand-written
`SCRIPT_NONE` line back as a script id, raised on it, and -- the header
being open for writing -- left script_ids.h cut short, whose names the next
run then lost. The line is skipped when read, and the header is written
aside and moved into place. script_ids.h regenerates as 97b's plus the
mountain's name.

**Proof.** 74 routes green. The 72 from 97b: every PASS line and all 410
output files identical to 97b's baseline; the 40-fight Dark Force trial
identical. The two new routes add their own.

## Build 97d: the Earthmen's leader

Built on 97c. Prompt task 1 (NOTES 14).

**The figure at the far end of Noah F1's corridor is the Earthmen's leader**
(object `$28`, loc_4DD8, ps2.asm:7166; read in full with the routine
before it, object `$27`, Lutz's slide on the Esper mansion's B1, which
build 87 ported and which still matches). What was read:

* Noah F1's sprite list holds one object: `$28` at 33,7 with `$2E` = 0 (an
  event object, event 0). The level-load routine (LoadSpritesInLevel,
  ps2.asm:799) has no case for Noah F1, so he is placed at every load,
  before Mother Brain and after.
* His routine: nothing while frozen (`tst.b 3(a0)`); otherwise its only
  entry is the init (loc_4DEE), which never advances the routine index and
  so runs every frame -- tile word `$44C` (line 0, the tile where Noah's
  art list 12 puts loc_5DFEA), mappings loc_5D986, hit-box row 4. It never
  sets the frame: of the three 24x32 frames only the first is ever shown,
  and he never moves.
* The A button (ProcessAButtonPress, loc_D84A): only a person (`$2E` type
  1) is turned to face the party and frozen; his `$2E` is 0, so A at him
  is event 0 (loc_D9AA), "There seems to be nothing unusual here" (`$1708`)
  on any level but Motavia.
* Where he stands: the walk to the Earthmen stops at 33,9, two cells below
  him, and the six on the bridge are placed at 32-33, 9-12. He is the one
  the party faces for "Welcome to the spaceship Noah".

So: a scene object with an init and nothing else. Ported as
NPC_OBJ_EARTHMAN_LEADER (npc.c earthman_leader_tick: the hit box from the
scene type while not frozen). Before, the port kept his slot empty, so the
Earthmen spoke to bare floor and the party could walk onto his cell.

**His art** is `assets/tiles/sprite_earthman_leader.png` (was
`sprite_noah_f1_object_28.png`, byte-identical), named in
tools/asset_names.py; extract_npcs.py adds him to scene_objects.json
(`EarthmanLeader`: tile 1100, line 0, box 4, three frames).
**Extractor changed:** `make extract-npcs`, then `make manifest`.

Route earthman_leader: Noah F1 from 33,10 with the *mother brain fought*
flag clear -- he is there (`expect object $28 visible`), A at him from
33,8 is `$1708`, and a step up from 33,8 stays on 33,8. On 97c it fails
at the object ("absent"); with that line skipped, the step up walks onto
33,7. mother_brain_won's shots 06-08 now have him in them (its PASS line
unchanged).

## Build 97e: the one-shot cases $C-$11, and the C menu's gate

Built on 97d. Prompt task 2 (NOTES 10).

**The one-shot cases, read** (loc_7E02, ps2.asm:11489-11665, every case
through the tail; with LevelScreen's call of it and the `bclr #7` after,
11393-11412, ProcessPlayerMenu's gate, 14099, and every write to the event
word in the ROM). The dispatch is a chain of cases 1 to $10; anything past
$10 falls to loc_8094. After a case, LevelScreen clears bit 7 of the word,
so while the level runs the high byte is the case the *next* load will
take (case 1 leaves $82, which is 2) -- or $C, $D, $E, $F until an event
clears it.

| case | name | what | port |
|---|---|---|---|
| $C | `SCENE_SHIR_GONE` | $DE70 = 1, $DE6E = $31 (Shir gone), the word left for loc_E042 | build 87a |
| $D | `SCENE_ESPER_B1_HOLD` | `rts`: the word stays $D00, the hold goes on | **97e** |
| $E | `SCENE_MOTHER_BRAIN_ARRIVAL` | her image and the arcs, the confrontation | build 92 |
| $F | `SCENE_MOTHER_BRAIN_DEFEATED` | the aftermath | build 93 |
| $10 | `SCENE_OPENING` | $DE70 = 1, $DE6E = $3B, the word left | the crawl (the opening's own path) |
| $11 on | -- | loc_8094: the word cleared, both bytes | **97e** |

Where each comes from: $D only from B1's Esper's second talk (event $33,
loc_E07A, with $C743 already 1), cleared by Building_EsperMansion
(loc_CF0A) when Lutz's slide opens it; $10 only from MoveToOpeningScreen's
$1007. Nothing writes a high byte past $10: the direct writes are 1, 4-7,
$A-$10, $80, $82 and $83, and the indexed ones (an event's own byte, a
door's) write 1 at an index past 0.

**The C menu's gate (found reading $D).** ProcessPlayerMenu opens nothing
while `tst.b (event_flags).w` -- the high byte -- is not 0. The port's gate
read the save block's copy of that byte ($C710), which nothing ported
writes: the port keeps the high byte as the next load's case
(g_level_oneshot) and the hold (g_event_hold). So in the port C opened the
menu during the Esper B1 hold, and over both Motavia views after Climatrol
(cases 4 and 6: no party, no event, the byte 5 and 7 in the ROM). The gate
now takes `event_word_high` from game.c: g_level_oneshot or g_event_hold
not 0. Everywhere else the byte is set in the ROM an event is also running
($DE70), which already kept the port's menu shut.

**The cases.** $D: the hold kept across the load (g_event_hold), no message
-- unreachable in play (nothing reloads while the menu is shut and the
leader still), ported so the switch has it. $11 on: nothing to do past what
every load does (the one-shot and the camera case start clear); the "not
ported" message is gone. $10: the opening is the crawl (verified against
the emulator, build 20); a walking level's load carrying it -- which the ROM
never does -- says so on stderr. Cases 8 and 9 (`rts`, set by nothing) keep
the message.

**The probe.** `expect menu none` (no window open or holding the pad) and
`oneshot $XX` (the event word's high byte at the next load; game.c
game_debug_oneshot).

Routes, each failing on 97c (for the two with `oneshot`, 97c's game code
with only the setter added) and passing on 97e:

* esper_b1_hold -- B1's Esper twice, C in the hold: no menu (97c: menu $01).
* climatrol_motavia_menu -- C over each Motavia view: no menu (97c: menu $01).
* oneshot_hold -- $D before a warp onto Noah F1: held, the menu shut (97c:
  "one-shot event $0D is not ported", and the party walks).
* oneshot_clear -- $11 the same way: the menu opens, the party walks (97c:
  "not ported").

**Proof.** 79 routes green (the halves: the first 30 and the remaining 49).
The 74 from 97c keep their PASS lines, and every output file of 97d's 75
is unchanged; the 40-fight Dark Force trial is identical to 97c's.

## Build 97f: the save's per-place event bytes named

Built on 97e. Prompt task 3 (NOTES 11), a proved refactor.

Twenty-one bytes of the event block were still called by where their
event is placed (`event13_control_tower_f1_41_6`). Each is event n's own
byte (event_flags + n); what was read to name them: the events' routines
(loc_DA8C-loc_DC90, loc_DF24), loc_E84A (the item an event hands over,
`addq.b #1, (a0)`), and every indexed use of the block -- loc_5656 (a
chest object placed as an event reads its event's byte for its open
frame), Obj_OpenableDoor and Obj_DestroyableDoor (`$2F(a0)`), loc_9C12
with ItemAction_Dynamite, ItemAction_KeyTube and ItemAction_MruraGum,
and Ustvestia's song gates -- plus the objects each event is placed with
(Obj_DeadBody and Obj_InvisibleBlock2 read no byte).

| byte | name | why |
|---|---|---|
| $C713 | `shure_bare_bodies_unused` | Shure F3's bare bodies (event $03): nothing touches it |
| $C714 | `uzo_fake_maruera_trees_unused` | Uzo's six not-Maruera trees ($04): nothing |
| $C717 | `tower_keyboard_unused` | the Control Tower's keyboard ($07): nothing |
| $C718-$C71E | `skure_newspaper_1_opened` .. `_7_opened` | Skure's seven newspaper boxes ($08-$0E) are Motavian chest objects placed as events: loc_5656 shows each open while its byte is set -- and nothing sets it |
| $C71F | `underwater_passage_water_unused` | the water by the Underwater Passage ($0F): the gum tests the event's number, not its byte |
| $C720 | `maruera_leaf_taken` | the Maruera tree ($10): loc_E84A sets it with the leaf; set, $1708 |
| $C723-$C726 | `tower_green_card_taken` .. `tower_red_card_taken` | Control Tower F1's four boxes ($13-$16): the same, with the cards |
| $C728-$C72A | `nido_door_blown`, `biosystems_lab_door_blown`, `biosystems_lab_f2_door_blown` | the three dynamite doors ($18-$1A): the Dynamite's `bset #0`, the door object reads it |
| $C72B | `tube_shutter_opened` | the tube's locked door ($1B): the Key Tube's `bset #0`, the door object reads it |
| $C73B | `mount_uzo_unused` | the rocky mountain ($2B): nothing |

Found on the way: events $13-$16 were `EVENT_*_CARD_DOOR` with "the dam
doors" in their comment; they are Control Tower F1's four boxes, now
`EVENT_TOWER_GREEN_CARD` .. `EVENT_TOWER_RED_CARD` (game.c's four cases
and the card's item follow). rom_ids.h gains a FLAG_ for each named byte
but the `_unused` ones (gen_rom_ids.py skips those), none used yet.

**Old saves load unchanged:** only a key's `c7XX` is read back
(gamedata.c read_flags), so a save with the old place names -- yours in
saves/ among them, untouched -- keeps every byte. A save written now
carries the new names (checked with a scratch `save-here`).

**Extractor changed:** `make extract-flag_names`, then
`python3 tools/gen_rom_ids.py ps2_constants.asm`. Only flag_names.json's
21 entries changed.

**Proof.** Before any edit (97e as packed): 79 PASS lines, checksums of
all 440 files the routes write, and the 40-fight Dark Force trial. After:
the same 79 PASS lines, all 440 files byte-identical (the route saves are
the routes' start saves, copied), and the same trial.

## Build 98: the whole game by the rules, one seed at a time

Built on 97f. The owner's task: complete the game by the harness's rules,
semi-non-deterministically, without spinning wheels on grinding. Harness
only: **nothing under src/ changed.** No ps2.asm routine was read: nothing
was ported. The one ROM behaviour the fixes lean on -- the Esper mansion's
door Espers keep the doorway shut while Rolf is dead -- is build 87b's
(route esper_door_rolf_dead).

**tools/playthrough.py** (docs/harness.md, "The whole game, one seed"):
story.py's stages from `start new` to the ending screen, with a seed (a
player's pauses: 1-24 idle ticks before one walk or press in four, on the
map or in a fight, so the RNG -- which turns every frame -- deals every seed
its own game), lean grinding (a grind short of its target at its trip cap
or time budget goes on healed and geared), retries from the checkpoint
before a stage (4 tries, each on its own stream), and a report.

Plain story.py from a new game, as 97f left it, stopped at the Oputa grind
with Amy: 120 trips (509 fights) with the lowest member L11 of L14. That
is the grind the lean rule ends.

**Five seeds, all to the ending** (route_out/playthrough_s1 .. s5; a whole
game is two to five minutes here, 5,800-8,500 fights):

| seed | how | fights | tries lost | lean go-ons |
|---|---|---|---|---|
| 1 | new game to the Kain grind; then --from kain_grind (after fix 1) | 7,759 + 502 | 14 + 1 | 19 + 0 |
| 2 | new game, one run | 8,536 | 4 | 16 |
| 3 | new game to the dam grind; then --from dam_grind (after fixes 5-7) | 5,827 + 1,949 | 4 + 4 | 7 + 6 |
| 4 | new game, one run (before fixes 2-8) | 7,280 | 3 | 13 |
| 5 | new game to the dam grind; --from dam_grind to Ikuto; --from shir (after fix 8) | 5,953 + 1,089 + 1,375 | 4 + 7 + 3 | 6 + 2 + 6 |

Every seed met the last bosses with about the same party -- Rolf L41, Rudo
L40, Amy L34-35, Kain L32-33 -- since the Noah grind's target is a level,
not a fight count. Dark Force took one or two stage tries (each up to four
fights). **The port printed nothing on stderr in any run, and the probe
never stalled:** everything that stopped a seed was the harness's.

**Fixed (harness):**

1. The Nei Sword's holder dead on Noah: `nei_sword_out` looked for the sword
   only on the living, so with Rolf (who wears it) dead the party could not
   leave Noah -- seed 1's Noah grind, four tries. The dead are revived first
   (play.revive: Rever, a Moon Dew).
2. Dark Force beaten with nobody able to raise the sword's holder: the stage
   reported success stuck on Noah, no save written, and the next stage
   could not start (seed 2). Now a lost try, from the checkpoint before it.
3. The Esper mansion with Rolf dead: the door Espers stay put and the stage
   could not reach B1 (seed 1's rerun). Rolf is revived (or taken to the
   nearest town's clone lab) before the door; nobody leaves the stage dead
   (`revive_party`).
4. Checkpoints with the dead in them (seeds 1 and 3: the Esper mansion's,
   Ikuto's): the next stage walked on with Rolf dead. The driver raises the
   dead before every checkpoint and every retry, and a stage counts only
   once its checkpoint file was written by it.
5. Gaira: fight_smart's "run when it is going badly" skips a fight nearly
   won (enemy HP left under 12 a live member), so with `run_below` 2.0 --
   "always run", Gaira's rule, no weapons there -- a lone 20-HP enemy was
   fought bare-handed to fight_smart's limit. `run_below` over 1 now always
   runs (seed 3, all four tries).
6. A fight starting on the step onto Gaira's panel: goto was done, the
   party on the battle screen, and the panel's code pressed C through it --
   FIGHT with no weapons. It runs now, and walks back if moved.
7. goto on no map (a fight, a fade): `level_map(None)` raised KeyError; it
   returns False. The driver counts any harness exception as a lost try.
8. A game over in a Prism dungeon: every chest after it was "unreached"
   and the stage gave up (seed 5 in Ikuto, four tries). `loot` continues
   the last save and goes on with the list.

Also: story.py `--saves DIR` (checkpoints anywhere but routes/saves); lean's
message for a town grind out of trips with gear still to buy.

**Seen, not changed:** a pause on one of Gaira's damage floors costs HP (a
player standing there would lose it too); the gear rule swaps Anna between
the Nei Slasher and the Nei Emel at every town visit (harmless, a bag slot
each time); the wall-clock budgets (the Kain and Noah grinds, the thefts)
make a seed reproducible only until one runs out.

**Proof that nothing else moved** (baseline on 97f as packed, before any
edit): the same 79 PASS lines, all 440 route output files byte-identical,
and the 40-fight Dark Force trial identical bar its `out` argument. With no
seed and LEAN off, play.py and story.py decide as before.

Packed: each seed's report and story logs and its PNGs (fail_<stage>_<try>
for every lost try, the bosses', the ending's); seed 4's checkpoints (one
clean run, a save at every stage from Paseo to Mother Brain). Not packed:
the probe logs (up to 213 MB a seed) and the other seeds' checkpoints.

## Build 99: footwear by what it gives, the hands the plan empties, seeds that replay

Built on 98. Harness only: **nothing under src/ changed** (tools/play.py,
tools/route_probe.c, tools/story.py, tools/playthrough.py).

**Read for it:** the routine that applies equipment (SetCharEquipment, with
loc_AD4C and loc_AD8C) and its undo (loc_ADB0, loc_ADBE, loc_ADF8);
CalculateHitRate and its four callers in the party's attack; the enemies'
blow (loc_2AA8 with Enemy_PickCharToAttack); the round's order (loc_F004).

**What a piece of footwear gives.** For each member the equipment routine
reloads attack from its base, adds every worn piece's attack and defence
(InventoryData +$E, +$F: the legs slot's too), and then -- when the legs
slot holds Shoes through Garda Boots -- AgilityIncreasesArray's agility:
Shoes 2, Sandals 3, Boots 3, Knife Boots 5, Long Boots 7, Hirza Boots 7,
Shune Boots 10, Garda Boots 8. The port's gamedata_equip_bonuses_add does
the same. The party's agility is read by the round's order (agility plus
0-15, fastest first) and by Dark Force's curse (the slowest living member,
build 92); the hit roll is the attacker's dexterity against the target's
agility only for the party's own blows, and an enemy's blow never rolls
against a member's agility.

**The gear rule** (play.plan_gear, play.surplus) scored head, body and legs
by defence alone. Anna's footwear -- Boots, Knife Boots, Long Boots, Shune
Boots -- is all defence 7, so nothing beat her Boots and she wore them all
game in every build 98 seed; the Knife and Long Boots' attack went
uncounted too. Now `piece_score`: attack twice and defence once (the scale
hands_score uses), plus the footwear's agility at `AGILITY_WEIGHT` 0.5 a
point (member_value's weight). For Anna: Boots 8.5, Shune Boots 12, Long
Boots 20.5, Knife Boots 23.5 -- Kueri's Knife Boots when she passes it,
Zosa's Long Boots otherwise. Shune Boots would beat Knife Boots only with
an agility point worth more than 2.8 defence points. Amy's Boots give way
to Aukba's Hirza Boots (+4 agility). Seed 4's Zosa checkpoint, geared up:
Anna into Long Boots.

**Anna's Nei Slasher / Nei Emel swap.** gear_up put a one-hander "into the
empty hand, else over the first" -- bags() says a piece is worn, not in
which hand. Anna held the Nei Emel (right) and an Ac Slasher (left) with
the Nei Slasher carried; the plan was the Nei Slasher and the Nei Emel,
taking off the Ac Slasher. The Nei Slasher went over the right hand -- the
Nei Emel -- and the next town planned the Nei Emel back over the Nei
Slasher, 19 times each in seed 4. The probe has `hands` (each member's
right and left hand), play.hands() reads it, and play.hand_for picks the
empty hand, else the one holding a piece the plan takes off. Seed 4's
naval checkpoint, geared up three times: the Nei Slasher into the Ac
Slasher's hand once, then nothing.

**Proof, task 1** (baseline on 98 as uploaded, before any edit): the same 79
PASS lines, all 440 route output files byte-identical, and the 40-fight
Dark Force trial identical bar its `out` argument (the trial itself is
97b's: the same 40 results).

**A seed replays exactly** (task 2). Four wall clocks decided things in a
playthrough; each is now a count (story.py):

| was | now | build 98's seeds used |
|---|---|---|
| the Kain grind, 200 s | `KAIN_TRIPS` 40 | 16-24 trips |
| the Noah grind, 200 s | `NOAH_TRIPS` 80 trips onto Noah, a wipe-out counted | 42-51 |
| Shir's thefts in Paseo, 190 s | `SHIR_EXITS` 400 store exits | 10-295 |
| Shir's thefts before Noah, 120 s | `NOAH_THEFT_EXITS` 300 | 116-254 |

The Prism dungeons' clock (`PRISM_BUDGET`, 170 s before a floor, lifted to
an hour by build 98's playthrough) is `None` in a playthrough: no limit;
story.py alone keeps it. And playthrough.py restarts itself under
`PYTHONHASHSEED=0`: a few choices take the first of equals from a set of
item names (plan_gear's one-handers, sorted by score, ties in set order),
and str hashes differ per process otherwise.

Checked: seed 6 from a new game, twice with the same code -- story logs
identical line for line (2,308), all 41 checkpoints and every PNG
byte-identical, the reports the same bar their seconds.

**The reserve Moon Dew.** No shop sells one; Shir steals them in Paseo. Seed 2
won Dark Force with Rolf -- who wears the Nei Sword, the only way off Noah --
dead and nobody able to raise him: the walk's revivals had spent the dew.
Now:

* `play.revive` leaves the last `Session.dew_reserve` Moon Dews alone unless
  told to spend them; only `nei_sword_out` does, for the sword's holder.
  The reserve is 0 unless a Noah stage sets it, so every trial and route
  is as before.
* `story.restock_dews` before every trip onto Noah (the Noah grind, each of
  Dark Force's and Mother Brain's walks): `DEW_RESERVE` (1) carried, or home
  for it -- the roof door to Paseo, the last member but Rolf out for Shir,
  her thefts for a dew, the party as it was, the roof back. One
  empty-handed restock and the trips go on without (no spinning).
* Shir's thefts no longer stop at a full bag: `on_full` hands what she
  stole on (`empty_shir`) and they go on -- seed 2's thefts before Noah
  ended there with no Moon Dew. `empty_shir(first=("MoonDew",))` hands the
  dews on first, and makes room for one when every bag is full (a
  Trimate, Hidapipe or the like to Shir in its place): seed 6's first run
  of the restock left its dew in her bag behind Hidapipes and Trimates.

Seed 6 (below) restocked twice: once in the Noah grind after the sword's
holder was raised with the dew, once before Dark Force's second try.

**The soak** (task 3): seeds 6 and up from a new game, the port watched for
anything it prints on stderr, a probe stall, or a stage that breaks on the
port's side. **No port fault in any seed:** no stderr line, no stall, no
harness exception. What the seeds did find was the harness's, fixed on the
way (each seed's run below is on the code as it stood then; the fixes go
into every run after):

1. **A chest's find left behind for want of room** (seed 6, Naval). Read in
   full: the treasure chests' routine (ObjType_TreasureChests, loc_E84A):
   an item goes to the first living member, in party order, with a free
   slot of sixteen ($1703, the chest's byte counted up); with none, "But
   they can't hold any more" ($1704) and the byte is left alone -- the
   chest stays shut and can be opened again. Now `loot` answers $1704 with
   `play.make_room` (TOSS -- `play.toss_item`, ItemAction_TossEntry's
   $000C / $000E -- of the commonest thing in the fullest bag: a Monomate,
   Dimate, Antidote, Telepipe, Escapipe, Hidapipe or Trimate, in that
   order) and the chest again.
2. **A CONTINUE in the middle of a floor's chests** (seed 8, Naval): the
   party opened one chest, was wiped out walking to the next, continued the
   last save -- which shut the first again -- and went on down the list, the
   second "unreached". `loot` starts the list again after a CONTINUE (a chest
   still open says $1706, "nothing inside"), at most `LOOT_WIPES` (2) times a
   call.
3. **The Moon Dew restock with every bag full** (seed 7): Shir came back
   from Rolf's house with sixteen things and so did everyone else; no theft
   was tried, Rolf then died on Noah with no reserve, and the Noah grind lost
   all four tries. The restock now tosses from Shir's bag down to thirteen
   before stealing and whenever it fills (`on_full`), and a stolen dew with
   nowhere to go makes room -- a Trimate or the like to Shir, or tossed from
   the fullest bag -- so it is never left behind with her. One restock try a
   stage try (seed 7's first fix gave up for the whole session).

| seed | code | result | tries lost | fights |
|---|---|---|---|---|
| 6 | task 2 | the ending (replayed: identical) | 4 (Naval 2 -- fix 1; the Noah grind 1, Dark Force 1: wiped out) | 7,555 |
| 7 | task 2 | stopped at the Noah grind (fix 3) | 5 | 7,162 |
| 8 | + fix 1 | the ending | 1 (Naval -- fix 2) | 7,226 |
| 9 | + fix 1 | the ending | 2 (Menobe: hurt, a chest unreached; Dark Force wiped out) | 7,241 |
| 7 | + fixes 1-3 (first version) | the ending (replayed: identical) | 1 (Guaron: wiped out) | 7,375 |
| 10 | + fixes 1-3 (first version) | the ending | 0 | 7,181 |
| 7 | + fixes 1-3 | the ending (the dew handed on, 1 carried) | 1 (Guaron) | 7,277 |
| 11 | + fixes 1-3 | the ending | 9 (the lab 2, Climatrol 1, Shir's raise 2, the Noah grind 2 -- fix 4, Dark Force 2) | 10,751 |
| 11 | + fix 4 | the ending (the Noah grind first time) | 6 (Dark Force 1 -- fix 5) | 10,304 |
| 12 | + fix 4 | the ending | 0 | 7,197 |
| 11 | + fix 5 | the ending (the Noah grind and Dark Force first time) | 5 (the lab 2, Climatrol 1, Shir's raise 2) | 10,295 |

Proof after every fix, the last one included: the same 79 PASS lines, all
440 route output files byte-identical, and the Dark Force trial identical
bar its `out` argument. Packed: seeds 6-12's reports, story logs and PNGs
(the runs above that are last in the table for each seed); no checkpoints,
probe logs or probe saves. Not done: a second replay check on the final
code (seeds 6 and 7 replayed on earlier code; nothing since reads a clock).

4. **The reserve dew in the sword holder's own bag** (seed 11, the Noah
   grind): the restock handed the dew to Rolf first; with Rolf dead,
   find_item looks only at the living, so "0 revived" and two tries lost.
   The reserve now counts only dews outside the Nei Sword holder's bag
   (`reserve_dews`), and `dews_off_holder` hands his to a living member with
   room (tossing to make it) before every Noah trip.
5. **Amy's map techniques one stop off** (seed 11, after Dark Force: "is
   dead", $0102, for a Rever on Rolf). With nine or more map techniques the
   list is two pages with NEXT at stop 0 above the first eight
   (Win_LevelTechList, loc_FEF2, as ported in window.c -- not reread this
   build; the probe showed nine stops, the cursor on 1), as an item list;
   `use_tech` picked stop n directly, so Amy (ten at L34) cast Nares for
   Rever and NEXT for Res. It uses list_pick now: from seed 11's boss
   checkpoint Amy's Res, Rever (stop 7) and Nasar (stop 9, the second page)
   each land ($0104, $0104, $0108). And `revive` falls back to a Moon Dew
   when a Rever does not raise the target, instead of giving up.

Seed 7's second run replays exactly too: story logs identical line for
line (2,261), all 41 checkpoints and 24 PNGs byte-identical, the reports the
same bar their seconds. A background run does not outlive the turn here:
seed 10's first run was cut off in Climatrol when the session ended (not a
stall -- nothing was left running), and was run again.

## Build 100: the grind testing closed; FULLSCREEN; COMPUTER PLAYS

Built on 99b.

### Task 1: one seed twice on 99b's code (the grind testing closed)

Nothing under src/ or tools/ changed for this part.

Seed 11 -- the seed whose last build 99 run was on the final code, and the
one with the most retries (five tries lost and replayed from their
checkpoints, each on its own stream: the Biosystems Lab's way in twice,
Climatrol once, raising Shir twice) -- played from a new game to the ending
twice, each run in its own untouched copy of the 99b tree:

| | run A | run B | 99b's packed run |
|---|---|---|---|
| story log | 2,722 lines | identical to A | identical to A |
| checkpoints | 41 | all 41 byte-identical to A | (not packed) |
| PNGs (bosses, ending, failed tries) | 28 | all 28 byte-identical to A | all 28 byte-identical to A |
| report | 10,295 fights, 5 tries lost | the same bar the seconds | the same bar the seconds |

The only difference anywhere is the wall-clock seconds in the report's
lines. Each run took 160 s on this machine. So a seed replays exactly on
the final code as well as on the earlier code seeds 6 and 7 were replayed
on, retries included, and matches what build 99 packed. No new soak seeds:
the check did not fail. NOTES item 15 is done.

Before any edit for tasks 2 and 3 (the proof, on 99b as uploaded): 79 PASS
lines, checksums of all 440 files the routes write (every one also equal to
the uploaded tree's copy), and the 40-fight Dark Force trial
(`boss_trials --boss dark_force --policy both --repeats 10`), whose results
equal 97b's `bt97b_after.json` bar its `out` argument.

### Task 2: FULLSCREEN in the Escape menu (build 100a)

Changed: `src/platform/platform.h` (`platform_set_fullscreen`),
`platform_sdl3.c`, `platform_headless.c` (ignores it), `src/game/sysmenu.[ch]`,
`src/main.c`; `tools/route_probe.c`; route `escape_menu_fullscreen`.

**The menu.** A FULLSCREEN option between DISPLAY and QUIT, its value ON or
OFF in the value column; C, A or Enter flips it, like the options above it.
`saves/settings.json` keeps it as `"fullscreen": true|false`; a file
without the key (every file before build 100) reads as windowed. The menu
is laid out from its item count now (rows `2 * items + 2`, centred), and 30
cells wide with the values in one column 18 cells in, so the next build's
longer labels fit.

**The platform.** `platform_set_fullscreen` is SDL3's borderless fullscreen
-- the fullscreen mode set to none, which is the desktop's own size on the
display the window is on -- then `SDL_SyncWindow` to wait the change out
where the window system allows. It prints one `display:` line: fullscreen
or windowed, the output size in pixels, the desktop's size and density,
and where the frame landed. The window events for entering and leaving
fullscreen keep the platform's idea of it right when the window system
changes it by itself.

**The placement is recomputed on the size change** -- read, not assumed:
`platform_present` calls `SDL_GetRenderOutputSize` every frame and
`frame_layout` places the frame from it (the largest whole multiple of 224
lines, the aspect across, bars round), and `scaled_target` remakes the
whole-multiple texture whenever the layout's size differs from the one it
holds. Nothing in the placement is cached from window creation.

**Settings in a route.** The Escape menu draws the settings, so a route
that opens it would have drawn whatever the player last chose: the probe's
`game_init` read the player's `saves/settings.json`. `sysmenu_set_settings_path`
now points the probe at its own route directory (none there: the defaults);
the game still reads and writes `saves/settings.json`. The probe gains
`press esc`, `expect setting fullscreen on|off` and `expect escape-menu
open|closed`.

**Route** `escape_menu_fullscreen`: on the title, Escape, down three, C (on),
C (off), B; PNGs `menu_open`, `menu_fullscreen_off`, `menu_fullscreen_on`
(the waits put the lamp in the lit half of its blink). On 99b's probe it
fails -- the language had no Escape key (exit 2 at `press esc`). Also run
headless through main.c in a scratch directory: FULLSCREEN chosen writes
`{"controls": "arrows", "display": "tv", "fullscreen": true}`, and the next
start reads it back (the menu shows ON).

**Proof** (against the baseline taken before any edit): all 79 PASS lines
unchanged, 80 routes green, all 440 files byte-identical, the Dark Force
trial identical.

**Not compiled here:** `platform_sdl3.c` -- this box has no SDL3. The headless
target, the probe and every tool build with no warnings. To check on your
machine:

1. `make` builds with no warnings (the new SDL3 calls:
   `SDL_SetWindowFullscreenMode`, `SDL_SetWindowFullscreen`,
   `SDL_SyncWindow`, `SDL_GetDesktopDisplayMode`, the
   `SDL_EVENT_WINDOW_ENTER_FULLSCREEN` / `LEAVE_FULLSCREEN` events).
2. Escape, FULLSCREEN: the window fills the display with no mode switch
   (no blank flash, the desktop's resolution kept), and the `display:` line
   on stderr gives the output size and the frame's place -- 1920x1080
   should say 4x up and down, the frame at y 92.
3. The frame is centred with even bars, in every DISPLAY aspect, the first
   frame after the switch and after switching back.
4. FULLSCREEN again returns to the window at its old size.
5. Quit fullscreen and start again: it opens fullscreen
   (`saves/settings.json` has `"fullscreen": true`).
6. A desktop shortcut out of fullscreen (if your window manager has one),
   then FULLSCREEN in the menu: the menu said ON, the press makes it OFF and
   does nothing to the window; a second press goes fullscreen. That one
   mismatch is known and left.

### Task 3: COMPUTER PLAYS (build 100b)

Design first: `docs/computer_plays.md` (written before the code; "as built"
marks where the code taught otherwise). Changed: `src/harness/probe.[ch]`
(new: the route language's interpreter, out of `tools/route_probe.c`),
`src/harness/autoplay.[ch]` (new: the game's host), `src/main.c`,
`src/game/sysmenu.[ch]`, `src/game/game.[ch]` (`game_debug_reset`),
`src/core/audio.[ch]` (`audio_set_muted`), `Makefile` (HARNESS, `-lpthread`),
`tools/route_probe.c`, `tools/play.py`, `tools/story.py`,
`tools/computer_plays.py` (new); routes `escape_menu_computer`,
`locusta_arm_over_box`.

**The shared interpreter.** Every command of the route language now lives in
`src/harness/probe.c`, compiled into the probe and both game targets; it
ticks only through its host's tick and speaks only through its host's log.
The probe keeps route files, its output directory, PNG writing, stderr
captured into the log and the summary. Proved on the split before anything
else changed: the 79 PASS lines, all 440 route files and the Dark Force
trial identical.

**The game's host.** `posix_spawn` starts `python3 tools/computer_plays.py
--attach 3,4`; a command thread runs the interpreter; each tick a command
wants is posted, and the frame loop runs `game_tick` with it and draws while
the thread waits. Only a player's commands run (the audit in the design
doc: the stages send nothing else but `start slot` and `save-here`). `start
new` and `start slot` press the console's reset (`game_debug_reset`: the
Sega screen as after a game over, the generator's power-on seed); the title
is the pad's (`play.continue_game` presses Start there now). Its saves are
`saves/computer/slots`, its checkpoints `saves/computer/checkpoints`, its
report and story log beside them; the game's save directory is set back to
`saves/` on the hand-back. FAST: 4096 ticks or 15 ms a drawn frame
(`PS2_FAST_TICKS`), the output muted with the sequencer running.

**The Escape menu.** COMPUTER PLAYS (left and right: NEW GAME or a slot of
the player's that holds a save; NO PYTHON3 or NO TOOLS when it cannot run)
and SPEED set in under it (`"computer_speed"` in settings.json). Escape or
any button hands the pad back.

**The harness.** `play.Session(attach=...)` reads and writes the game's
pipes, with no read timeout (at 60 Hz a wait is minutes of silence);
`Session.saves_dir` is where the game writes its slots (the probe's own
directory, as before, or `saves/computer/slots`) -- story.py's three reads
of the session's slot 1 use it. `computer_plays.py` is playthrough's loop
with the two differences the audit asks for: no checkpoint where the
harness used `save-here` (the stage after replays it on a retry), and a
retry's CONTINUE through the title. The retry copies the checkpoint into the
computer's own slot first (its own memory card; `--no-swap` CONTINUEs its
last in-game save instead). A save of the player's is placed by its story
marks (`stage_of_save`).

**Tested.** Headless through the window's path, seed 12 from the Escape
menu to Arima: 852,392 ticks in 888 frames, 8 s, and the story log and both
checkpoints byte-identical to the probe's playthrough of the same seed.
Six hand-backs at different moments, all clean (a race fixed on the way:
docs/computer_plays.md). Seed 11 to the ending on this build's harness:
story and PNGs identical to 99b's run. Proof: 79 PASS lines, 440 files and
the trial identical; 82 routes green.

**Not compiled or run here:** the SDL3 build. To check on your machine:
`make` (with `-lpthread`); Escape, COMPUTER PLAYS at NORMAL -- the party
walks and fights at the game's own pace, the music on; Escape hands back
at once; SPEED FAST, COMPUTER PLAYS again -- silent, frames jumping,
`saves/computer/report.txt` growing; a slot of yours picked with right and
the stage it chose in `saves/computer/story.txt`'s first line; your
`saves/slotN.json` untouched throughout. Windows would need its own
autoplay file (POSIX spawn, pipes, pthreads).

### The Locusta's arm over the enemy boxes (build 100b)

The owner: a Locust's or Locusta's arm, reaching up over the name and HP boxes
at the top of the fight, is partly covered by them; in the game it is drawn
over them.

**Read:** the window frame writer (loc_978C) and the enemy boxes'
routines (Win_FirstEnemyName / Win_SecondEnemyName, Win_FirstEnemyInfo /
Win_SecondEnemyInfo). The four enemy boxes are plane B windows -- bit 13 of
their position word, since the enemies rebuild plane A's rows 0-16 every
frame (docs/battle.md). loc_978C writes a window's frame with `$8500`
(priority, line 0) on plane A and `$500`, no priority, on plane B; the boxes'
own routines set the interior's base to `$500` for the same boxes. The
enemies' cells (plane A) and their attack frames' sprite pieces carry no
priority. So nothing of the box is above the enemy: the arm is drawn over
the whole box.

**The port** gave every frame cell priority (`window.c put`), so a box's
border, high on plane B, covered the arm's low plane A cells and sprites;
the interior was already `$500`. Now a frame cell has priority on plane A
only. Route `locusta_arm_over_box` (two Locustas, the first one's attack,
the arm's tip on the first name box's bottom border): on the old code the
pixel is the border's grey `$AAA`, on the new the arm's green `$6C4`. No
other route's picture changed (all 440 files identical).

### COMPUTER PLAYS's lamp (build 100c)

The owner: any key handing the pad back was the handover prompt's idea, not
his -- a lamp on the COMPUTER PLAYS row instead, lit or put out with C, the
computer starting when the menu closes with it lit and stopping when it
closes with it out. He also asked whether the new game sits through the
whole intro: it does (below).

Changed: `src/game/sysmenu.[ch]` (`computer_on`, the lamp drawn with the
menu's own housing and the cursor's lit lamp; the menu 32 cells wide),
`src/main.c` (the menu runs over the computer's game; `computer_begin`,
`computer_draw`, `apply_settings`), `src/harness/autoplay.[ch]`
(`autoplay_hold` can wait only for the game to be let go, to draw the menu
without a tick), `src/harness/probe.c` and `tools/route_probe.c`
(`expect setting computer on|off`); route `escape_menu_computer` rewritten.

While the computer plays, the player's keys go to the Escape menu and
nowhere else: Escape opens it over the held game, and only the lamp put
out, or QUIT, ends the computer's run. Tested headless: the lamp lit and the
menu closed starts seed 12, whose story to Arima is still byte-identical to
the probe's; A, C, Start and Left pressed mid-run change nothing; Escape,
down four, C, Escape hands back ("the pad is yours again", the report
"handed back").

**The intro.** A new game is `routes/new_game.route`, the probe's own, so
it sits through all of it: the Sega screen and both of the title's
dissolves (1,055 ticks, about 18 s at NORMAL) before Start, the backup check
(to 1,326), ROLF typed on the board (1,446), the dream with every page
pressed through as soon as it stops (the Governor at 3,317), the Governor's
talk, the walk home and Rolf's house: control at 5,781 ticks, 96 s at
NORMAL. The ROM takes Start on the Sega screen and at any moment of the
title, which would save the first 17 s; not done, since the route is the
probe's check of the opening and the window's run would no longer be the
probe's game tick for tick.

Proof: 79 PASS lines, 440 route files and the Dark Force trial identical;
82 routes green.

## Build 101: COMPUTER PLAYS's pace, the first grind by way of Arima, COMPUTER PLAYS's untested paths, the CRT simulation's phase 1

Built on 100c.

Before any edit (the proof's baseline, on 100c as uploaded): 82 PASS lines,
checksums of all 453 files the routes write (every one equal to the
uploaded tree's copy), and the 40-fight Dark Force trial, equal to 97b's
`bt97b_after.json` bar its `out` argument.

### Task 1: COMPUTER PLAYS's pace

The owner, watching it: it dwells too long on the fight's selections and on the
victory numbers -- a quarter to an eighth of the time on the victory numbers.

**Read: how the ROM ends the victory's messages.** The victory's routines
(loc_F270 the victory and the EXP, loc_F308 the meseta, loc_F328 /
Character_ProcessLevelUp the level-ups) only set a script and return; the
window's text routine does the rest. Its per-frame end check (loc_FB56,
called from Win_Null while a window is up) goes by the byte the text stopped
on:

| Byte | In the script files | What ends it |
|---|---|---|
| `$C3` | `{pause}` | A, B or C; the text goes on (a stop mid-message) |
| `$C4` | `{end:C4}` | A, B or C; the window closes (loc_FC00). **No timer** |
| `$C5` | | ends at once |
| `$C6` | `{end:C6}` | 60 frames (`$CD22` from `$3C`, down to below zero) **or** A, B or C, whichever first |
| `$C7` | | 60 frames; a press does nothing |

Any other byte is text still typing: RunScript draws one character a frame
(a line break `$C1` waits 3 more), and a press there (loc_FBE8, unless the
controls are locked) sets `$CD20`, which prints everything up to the next
stop in one frame. Every victory page -- "<leader> and party are
{pause}victorious!" (`$1211`), "<n> experience{pause}points!" (`$1212`),
"Receive <n>{pause}meseta!" (`$1213`), "<member> gains a level." (`$1214`),
"New HP maximum!" / "New TP maximum!" (`$1215`, `$1216`), "Learned a
new{pause}technique!" (`$1217`) -- ends in `$C4`, and five of them stop at a
`{pause}` first. So the victory's numbers are stops that wait for a button,
never timed: a won fight sits on its last page until a button. The timed
ones (`$C6`: "fell asleep!", "run away!", "But they're trapped!", "Too
tired to fight!") are cut short by a press.

**The cause, in the probe's log.** While the fight screen is busy, fight_smart
and press_through sent `wait ticks 240` and looked again. A page types in
~20 ticks, so each page sat ~220 ticks finished before the harness saw it;
after FIGHT the round's menu came back part-way through the 240 and waited
there. Seed 11 (the whole game, the probe's pace): 1,601 ticks of victory
pages a won fight, 62% of all fight time and 43% of the whole game's ticks.

**The pace, a Session setting** (`play.PACES`, `Session.set_pace`):

* `probe` -- every route, trial and seed since build 60: `wait ticks 240`,
  then C if the fight is still busy. The default everywhere but COMPUTER
  PLAYS.
* `watch` -- COMPUTER PLAYS's (`computer_plays.py --pace`, default watch):
  `wait pad 30`, a new probe command, until the game waits on the pad (a
  menu up, a page at its stop) or 30 ticks, whichever first; then C only
  if the fight is still busy with something other than a page typing (a
  round that repeats is interrupted as before; a page is never hurried --
  a C mid-typing would print the rest at once). So each page is answered
  the moment it waits. The player's pauses in a fight are half as long
  (`fight_jitter_max` 12 of JITTER_MAX 24; the odds unchanged).

`wait pad N` (src/harness/probe.c): up to N ticks until any wait on the pad;
running out is not a failure. A player's command (the game's host allows
it). `playthrough.py --pace watch` plays the window's game on the probe.
Both reports gain a ticks column and a line: ticks, ticks in fights (and a
fight), victory pages (from the first page, `$1211`, to the map; and a win),
pauses (and in fights), the pace.

**Measured**, seed 12 from a new game to Arima through the window's path
(headless main.c, the Escape menu, the lamp, FAST):

| | probe pace (as build 100) | watch pace |
|---|---|---|
| ticks | 852,392 (build 100's figure exactly) | 496,220 (58%) |
| fights | 240 | 246 |
| ticks a fight | 2,498 | 953 |
| victory pages a win | 1,651 | 177 (1/9.3) |
| pauses in fights | 6,374 | 3,679 |
| party at Arima | Rolf L8 Nei L14 Rudo L5 | the same |

At 60 Hz a won fight's pages go from 27.5 s to 3 s. The victory lands just
past the eighth the owner suggested: pressed the moment each page waits, a page
is on screen for its typing. With full-length pauses in fights (the watch
pace otherwise) the pauses were 6,265 ticks, 26 a fight (2.7% of fight time,
~8 ticks of each win's pages), halved 15 -- small, kept halved.

`playthrough.py --seed 12 --to arima --pace watch` on the probe: the same
496,220 ticks and 246 fights, story log identical bar the checkpoints'
directory in two lines, both checkpoints byte-identical.

**Proof** (the probe's pace unchanged): 82 PASS lines, 453 route files and
the Dark Force trial identical; seed 11 from a new game to the ending on
this build's harness -- story log (2,722 lines) and all 28 PNGs identical to
build 100's packed run.

### Task 2: the first grind by way of Arima (tools/story.py)

The owner: travelling between Paseo and Arima is fine for the early grind --
the encounters are mild -- and going straight to Arima gets Rudo much sooner
(he knocks at Rolf's house once the party has reached Arima).

**Read first:** stage_paseo (a new game, gear, Paseo's field to L5) and
stage_arima (the walk to Arima, its hospital and weapons, Arima's field to
L7, back to Rolf's house for Rudo, Paseo's field to L8 with him) in full.

**Now:**

* `paseo`: a new game, Nei's weapon (gear_up), a save at Paseo's Data
  Memory, then `walk_to_arima` -- the walk is the grind: every fight on the
  way fought, mates between fights, and hurt with nothing left to mend with
  (`s.careful`), back to Paseo's hospital and item store and out again; a
  wipe-out CONTINUEs Paseo's save; ARIMA_WALKS (8) walks at most. At Arima:
  its hospital and its weapon store. The checkpoint walks back to Paseo's
  Data Memory (Arima has none), which carries the knock.
* `arima`: Rudo from Rolf's house, then the three-member grind in Paseo's
  field to L8 and `gear` as before. A save from before Arima (COMPUTER
  PLAYS from a player's slot) finds no Rudo: the walk there and back, and
  the house again.

**Measured**, seeds 1-12 from a new game to the start of Shure (playthrough's
loop, the probe's pace), build 100's stages against these:

| | build 100 | build 101 |
|---|---|---|
| fights until Rudo joins | 132-143 (mean 137) | 1-6 (mean 2) |
| ticks until Rudo joins | 510,190-554,432 (mean 527,220) | 17,016-46,882 (mean 25,727) |
| fights until Shure | 679-733 (mean 704) | 696-755 (mean 719) |
| ticks until Shure | 2.22-2.39 M (mean 2.30 M) | 2.24-2.43 M (mean 2.32 M) |
| wipe-outs on the way | 0 | 2 (seeds 6 and 7, each once, the pair at L1 on the first walk; each CONTINUEd Paseo's save and went on) |
| party after `arima` (seed 1) | Rolf L8 Nei L14 Rudo L5 | Rolf L8 Nei L14 Rudo L7 |

Rudo joins twenty times sooner by ticks, and plays the whole Paseo grind
with the party (two levels more by the end of it). To Shure it is a wash:
the grind targets (L8 in Paseo, then Arima's shelf in `gear`) are unchanged,
and a party that arrives at level 1 grinds them from lower down. Seed 7 twice:
story log (211 lines) and all three checkpoints identical.

This changes every seed's game from `paseo` on (intended): build 100's seed
reports no longer describe this build's seeds. Routes and trials do not use
the stages; the proof below is unaffected.

### The dams' lights on Motavia's map (a question, no change)

The owner: the southernmost dam may be the green one, and it does not seem to
have its green light.

**Read:** Motavia's door records (the four dam entrances), Motavia's level
sprites (SpriteData_MotaWorldMap: the jet scooter, two invisible blocks and
one more -- nothing at a dam) and the dams' own (doors, fixtures, invisible
blocks), and the level screen's palette effects (loc_6430) with the tile
animation (loc_118F4). No object or sprite draws a dam's lights: each dam
is a gate across its channel in Motavia's own map tiles, its lights in the
dam's colour. The only animated colours on Motavia's map are palette line
3's entries 12-13 blinking every 8 frames (loc_650E: $EA0/$E80 and
$EC0/$E60), which the port has (`level_palette_cycle`, case 2), and the
water's two-frame tile animation.

**Which is which** (entrance cells): Green 65,47 -- the northernmost, just
south-east of Oputa; Yellow 45,65; Blue 91,65; Red 81,81 -- the
southernmost, south-east of Paseo. So the south dam is the Red dam, drawn
with red lights; the green light is on the Green dam's gate.
`route_out/dam_lights/four_dams_on_motavia.png` is the port's map at each
entrance (level_probe: the planes from the ROM's block map, no palette
blink). If the Red dam's gate looks wrong against the emulator -- it is
smaller than the other three, its lights a dim red -- a screenshot of it
there settles it.

### Task 3: COMPUTER PLAYS's untested paths (build 101b)

Changed: `tools/computer_plays.py` -- `--fail-once STAGE`, a test's: that
stage's first try counts as not finished after it plays, to force a retry
where no seed fails. Every run below is headless through the window's own
path (main.c, the Escape menu, the lamp, FAST) in a scratch directory with
its own saves/, never this tree's.

| Path | Run | Result |
|---|---|---|
| A retry in the window | seed 7, new game `--to arima --fail-once arima` | the first walk's wipe-out CONTINUEd the computer's slot (the reset, Start and CONTINUE on the title); arima's forced failure CONTINUEd the `paseo` checkpoint, copied into the computer's slot -- try 2 began from its party (L1, 35 meseta) and Rudo knocked: the knock is in the save |
| A start from a player's slot | slot 1 = the story's `dams` checkpoint; slot 1 = `motavia`'s | picked `roof` and `shir`; the player's slot byte-identical after |
| `roof` replayed before a failed Skure | the `dams` slot, seed 12, `--to skure --fail-once skure` | Skure's tries 1 and 2 failed by themselves (wipe-outs), each retry CONTINUEd the start save and played `roof` again first; try 3 went through |
| `shir` replayed before `menobe` | the `motavia` slot, seed 12, `--to menobe --fail-once menobe` | the forced failure, then a wipe-out in Menobe (the game over's title timed out into the attract crawl before the stage gave up); `shir` replayed before each retry; try 3 went through |
| `--no-swap` | seed 12, `--to arima --fail-once arima --no-swap` | the retry CONTINUEd the computer's own last save (the `paseo` checkpoint's, Rolf L1 Nei L2 192 meseta), no file copied; through |
| Saves at the end of `zema`, `gum`, `kain` | `--classify` on the story's checkpoints | each picks its own stage again; **whether the replay goes through is not run yet** |

Skure in the window: at both paces Skure wipes parties out (the probe's pace,
seeds 1-4 from `dams`: five wipe-outs in four runs; the watch pace: none on
seeds 1-3, one on 4). On the probe the roof stage's Visiphone save
(`save-here`) makes a wipe-out a CONTINUE in the Skure field; in the window
there is no player's save there, so a wipe-out loses the try and the retry
replays `roof` -- as designed, and slower. Found on the way (a harness gap,
both paces, not changed): `loot()` recovers a wipe-out only at the top of its
list, so one on the way to a level's last chest leaves the stage at the title
(seed 4's Skure, seed 12's Menobe in the window). The retry covers it.

## Build 102: the victory's pages held, the grind's long walks, a player's saves replayed, the CRT simulation's phase 1

Built on 101b.

Before any edit (the proof's baseline, on 101b as uploaded): 82 PASS lines,
checksums of all 453 files the routes write (each equal to the uploaded
tree's copy), the 40-fight Dark Force trial (equal to 97b's
`bt97b_after.json` bar its `out` argument), and -- since 101a's first grind
changed every seed's game from `paseo` on -- 101b's own run of seed 11 from a
new game to the ending at the probe's pace, taken twice: story log (2,233
lines), all 41 checkpoints and all 25 PNGs identical between the two.

### Task A: the victory's pages about twice as long (the watch pace only)

The owner watched 101 and wants the victory's pages up longer. The pages
("<leader> and party are victorious!" `$1211` through "Learned a new
technique!" `$1217`, Build 101) each end at a stop that waits for a button,
five with a `{pause}` stop first; build 101's watch pace answered each stop
the moment it waited.

**Now** (`tools/play.py`): `PACES` gains `victory_hold` -- 0 at the probe's
pace, 30 ticks at the watch pace -- and `Session.answer_page` is the C at a
page's stop: when the fight waits on text and the last script shown is one
of the victory's pages (`SCRIPT_VICTORY_PAGES`), `wait ticks victory_hold`
first, then C as before (the player's pause before a press is unchanged).
`fight_smart`'s C at a stop and `press_through`'s go through it. At the
probe's pace it sends the C alone: the same commands as before.

**Measured**, seed 12 from a new game to Arima through the window's path
(headless main.c, the Escape menu, the lamp, FAST), on 101b's stages:

| | 101b (watch) | 102 (watch) |
|---|---|---|
| ticks | 550,597 | 584,513 |
| fights | 290 | 290 |
| ticks a fight | 840 | 1,012 |
| victory pages a win | 175 | 362 (2.07x) |
| pauses in fights | 3,717 | 3,626 |
| party at Arima's checkpoint | -- | Rolf L7 Nei L14 Rudo L7, 4,130 meseta |

At 60 Hz a won fight's pages go from 2.9 s to 6 s. (Build 101's 177 was
seed 12 on build 100's stages; 101a's walk to Arima changed that game, so
the before is re-measured here.) `playthrough.py --seed 12 --to arima --pace
watch` on the probe: the same 584,513 ticks, story log identical bar the
checkpoints' directory in two lines, both checkpoints byte-identical.

**Proof** (the probe's pace untouched): 82 PASS lines, 453 route files and
the Dark Force trial identical to the baseline; seed 11 from a new game to
the ending -- story log, 41 checkpoints and 25 PNGs identical to 101b's run.

### Task B: the grinds by long walks (build 102a), and no more standing at walls

The owner: watching the party go round the little square by Paseo's gate
(`grind_and_gear`'s walks: down, left, up, right 3) is boring; a much larger
area with the same level of creatures runs from Paseo to Darum's, and the
tube to Arima. And, watching it: when the party hits a wall it stands there
for a moment before moving again, then goes round in a circle.

**Read first: the random encounters' roll** (ProcessRandomBattle, ps2.asm
26535, in full). On Motavia the group comes from where the party stands: one
byte a region of 256x256 pixels -- 16x16 cells, twelve across and eight down,
row `(y >> 8 & 7) * 12` plus column `(x >> 8 & $F)` -- from the table on foot
(loc_23C3A), on the jet scooter (loc_23C9A), after Climatrol (loc_23CFA) or
both; group 0 is a real group there (the "no encounters" test is only for
other levels' own bytes). The step counter goes up 1, 1 more on foot and 2
more without the Hidapipe; a fight when a random byte is below the counter's
eighth; the formation is the group's row of eight in the formation table
(EnemyFormationTable) by three bits of the random number.

**What each stretch meets** (tools/extract_battle.py's `groups`, `formations`
and `regions` in assets/data/battle.json): Paseo's column of regions is
group 0, Arima's group 1, the column east of it and Darum's tube group 2.
All three meet Mosquitoes, Fire Ants, Bees, Poisoners, Froggies and Locusts,
18-34 HP a formation (means: HP 24-26, EXP 10-11, meseta 20-24). The groups
beside them (3-5, west and east) are about three times that -- Armorants,
Waspies, Buzzers, Spinners (HP 38-42, EXP 30-32) -- and group 7 more again.

**The ground** (Motavia's map and collision, play.LevelMap): Paseo's gate
stands in a walled pocket of 60 open cells (x 69-76, y 53-61) whose only way
out is the tube just north of it (TubeNearPaseo, no encounters in it before
Climatrol). That pocket is where the square was walked: 8x9 cells, so its
three-step walks kept meeting its walls. North of the tube, groups 0-2 are one
piece of 1,258 open cells, x 67-111, y 10-63, from Darum's tube in the
north-west to the east coast, Arima's gate inside it.
`route_out/mild_ground/motavia_mild_ground.png` (tools/ground_map.py) is the
map with every region's group, that ground dotted and the walks' anchors.

**The pause at the wall** was the harness's, not the game's: the probe's
`walk` holds the direction against whatever stops it for BLOCK_GRACE (120
ticks, two seconds at 60 Hz) before it gives the command up -- time for a
person in the way to move off. The square's next walk then started from where
the blocked one stopped, so the square drifted off the wall: the circle.
Seed 11's whole game on 101b stood blocked 5,249 times, 4,369 of them the
grinds' three-step walks on Motavia (about 145 minutes at 60 Hz).

**Now** (tools/play.py, tools/story.py):

* `play.encounter_group` (the roll's table lookup), `play.Ground` (the open
  cells of some groups reached from a cell; anchors snapped onto it; every
  other open cell kept off), `play.roam` (anchor to anchor round the list
  from the one after the nearest -- Darum's approach, the north-east, the
  south-east, the tube's north end, Arima's gate; legs of 22-98 steps --
  fighting what comes with fight_smart, Res after each fight, mates when
  hurt, until the trip's fights and the leg's end, or hurt with nothing left:
  `s.careful`) and `play.roam_and_gear` (grind_and_gear's trips with roam in
  place of the square: the hospital, gear and mates in town, out through the
  tube from Paseo, 8 fights a trip, back fighting what comes).
  `story.mild_ground()` is the ground; stage `arima` grinds Paseo to L8 and
  stage `gear` Arima's shelf by it.
* `play.goto` gains `keep_off` (cells its plan never enters) and
  `after_fight` (called after each fight on the level); both default to off,
  so every other walk sends what it did.
* `play.open_walk`: every pattern walk the other grinds still use (`wander`:
  the other towns' fields, the dungeons, Noah) is cut short at the first
  solid cell ahead on the level's own collision, and skipped when the next
  cell is solid; a person in the way still blocks as before.

**Measured**, seeds 1-12 from a new game to the start of Shure (stages
`paseo`, `arima`, `gear`), the probe's pace; the grinds are `arima` and `gear`:

| | the square (101b) | long walks (102a) |
|---|---|---|
| fights a level (party levels gained in the grinds) | 20.0-21.9 (mean 20.9) | 19.0-21.3 (mean 19.9) |
| ticks a level | 63,666-69,920 (mean 66,969) | 57,393-65,456 (mean 60,718) |
| fights to Shure | 696-755 (mean 719) | 628-668 (mean 649) |
| ticks to Shure | 2.24-2.43 M (mean 2.31 M) | 1.91-2.07 M (mean 1.99 M) |
| wipe-outs | 2 (seeds 6 and 7, the first walk to Arima) | the same 2 (the walk is unchanged) |
| walks stood blocked | 242-294 a seed | 6-15 (goto in Paseo and Arima: townspeople) |
| party at Shure | Rolf L10 Nei L18-19 Rudo L9-10 | Rolf L9 Nei L18 Rudo L9 |

The long walks reach Shure 14% sooner by ticks and 10% sooner by fights:
a level costs a tenth fewer ticks, and Arima's shelf is bought sooner (its
money comes in with fewer trips back through town). The party arrives a
level lower for Rolf, since `gear` stops at the shelf, not a level. Seed 7
twice: story log, report and all ten PNGs and checkpoints identical.

Seed 11 from a new game to the ending (probe's pace), both changes: through,
28.14 M ticks against 101b's 28.15 M, walks stood blocked 5,249 -> 384 (the
rest are goto's, people in towns). Seed 12 to Arima through the window's path
(watch pace): 599,074 ticks, 363 a win's pages; `playthrough.py --pace watch`
on the probe identical (both checkpoints byte-identical).

This changes every seed's game from `arima` on (intended): 101b's and build
102's seed reports no longer describe these seeds. Routes and the Dark Force
trial do not use the grinds: 82 PASS lines, 453 route files and the trial
identical to the baseline.

### Build 102b: the victory's hold halved, with slack; a player's saves replayed; loot()'s last chest

**The victory's pages.** the owner, watching 102: the hold more than doubled
the pages -- half it, with a little random slack either way to feel human.
The watch pace now holds each victory page's stop `victory_hold` 15 ticks,
plus or minus up to `victory_slack` 5 drawn from the seed's own pauses
(`Session.jitter`: the same seed holds the same stops alike). The probe's
pace draws nothing and sends the same commands. Seed 12 from a new game to
Arima on 102a's stages, the watch pace:

| hold a stop | ticks | a win's pages | at 60 Hz |
|---|---|---|---|
| none (build 101) | 538,495 | 175 | 2.9 s |
| 30 (build 102) | 599,074 | 363 | 6.1 s |
| 15 +/- 5 (102b), through the window's path | 575,372 | 269 | 4.5 s |

The window's run and `playthrough.py --pace watch` on the probe are the same
game up to seed 12's first wipe-out (the first walk to Arima, tick 16,085, in
both); after it they part, by design: the probe CONTINUEs with `start slot`
(the save loaded, no title), the window by the pad through the title and
CONTINUE (docs/computer_plays.md, the audit), which takes other ticks. Build
101's and 102's matches were seeds with no wipe-out on the way.

**Task C: a player's save at the end of `zema`, `gum`, `kain`.** Each
story checkpoint in the player's slot 1, COMPUTER PLAYS started from SLOT 1
(the Escape menu, right on its row), seed 12, headless through the window's
path in a scratch directory. Each picks its own stage again (the stage marks
nothing a later save lacks) and plays it through:

| Save | Played again | Result |
|---|---|---|
| end of `zema` | Zema's grind: Hugh L13 -> L14, 160 fights, 302,270 ticks | through, checkpoint written |
| end of `gum` | the scooter to Climatrol's door with the gum, and back ashore | through, checkpoint written |
| end of `kain` | the Mogic Cap, gear at the town, to Zosa | through, checkpoint written |

The player's slot is byte-identical after each.

**loot()'s last chest.** Read: `loot` recovers a wipe-out at the top of its
list, so one on the way to the level's last chest left the loop with the game
at the title. What followed decided the cost: a later `loot` call recovered
at its own top and went on without the chests the CONTINUE had shut again; a
stage with nothing after it failed its try. Now the loop runs until the list
is done and the party is standing (`loot: wiped out on the way to the last
chest`, then the list again after the CONTINUE, as a wipe-out earlier in it
does). Measured, the probe's pace, seeds 1-3 to the ending on 102a's stages
(seeds 4-12 were not run: the background run ended with the turn):

| Seed | Where it happened | Before (102a) | After (102b) |
|---|---|---|---|
| 1 | Ikuto B4 | B3's loot recovered it, B4's chest left shut; retries: Guaron, Dark Force; 28.31 M ticks | B4's list again; retries: Ikuto; 27.50 M ticks |
| 2 | Menobe F2, the stage's last | Menobe's try 1 lost at the title, retried from its checkpoint; 26.81 M ticks | CONTINUE, F2's list again; 26.82 M ticks |
| 3 | not reached | -- | -- |

All reach the ending both ways. Where the gap is not met the fix sends the
same commands: seed 11 to the ending on 102b is identical to its run on 102a
(story log 1,943 lines, 67 PNGs and checkpoints) -- which also shows the
victory's change leaves the probe's pace alone. Routes and the Dark Force
trial: unchanged (nothing under src/).

## Build 103: the grinds at the Biosystems Lab, measured and moved inside

Built on 102b.

Before any edit (the proof's baseline, on 102b as uploaded): 82 PASS lines,
checksums of the 453 files the routes write (each equal to the uploaded
tree's copy), the 40-fight Dark Force trial (equal to 97b's
`bt97b_after.json` bar its `out` argument), and 102b's own run of seed 11 from
a new game to the ending at the probe's pace: 28,135,360 ticks, through,
three tries that did not finish (story log, report and 41 checkpoints kept).

### Task A: the grinds at a dungeon's entrance

The owner, watching COMPUTER PLAYS: the team spends a long time walking side to
side at a dungeon's entrance. That was the Biosystems Lab's ground floor just
inside its blown door (47,70): stage `lab_in`'s `dungeon_grind`, "left 3,
right 3", until everyone was L18 or 200 trips were up.

**Read first.** The random encounters' roll (ProcessRandomBattle, ps2.asm
26535, in full, including the path off Motavia): on a level other than
Motavia and Skure the group is the level's own byte, one for the whole
level, which the level's load put in the enemy buffer (ps2.asm 13259: the
high byte of the level record's plane-B pointer, plane A's after Climatrol);
zero there means no encounters. The step counter goes up 2 a step in a
dungeon (4 on foot on Motavia without the Hidapipe), a fight when a random
byte is below the counter's eighth, only on a whole cell. So a floor meets
the same fights a step wherever the steps go. The victory's EXP (loc_F270,
ps2.asm 22407): the formation's EXP over the members alive at the end, plus
one, to each of them. The lab's floors, a formation on average (the
level's group in EnemyFormationTable, eight formations each a roll in eight):

| Floor | Group | HP | EXP | Meseta |
|---|---|---|---|---|
| ground floor | 23 | 90 | 116 | 170 |
| F1 | 24 | 78 | 110 | 152 |
| F2 | 25 | 67 | 68 | 111 |
| B1 | 22 | 94 | 134 | 175 |
| Oputa's field (Motavia, group 4, where amy_grind and lab_grind walked) | 4 | 42 | 31 | 52 |

**Measured on 102b** (tools/grind_ledger.py: playthrough.py with a ledger of
ticks and levels after every fight, nothing sent; seeds 1-12 from a new game
to the end of `lab`, the probe's pace). The three grinds from Amy's joining:

| Stage | Where | Target | Fights | Ticks | Lowest member | Lowest levels a 100 k ticks | Ends |
|---|---|---|---|---|---|---|---|
| amy_grind | Oputa's field, the little square | lowest L14 | 601-606 | 1.72-1.77 M | Amy L6 -> L11 | 0.28-0.29 | its 120 trips, short (12 of 12) |
| lab_grind | the same | lowest L16 | 1,001-1,009 | 2.82-2.86 M | Amy L11 -> L13 | 0.07 | its 200 trips, short (12 of 12); last level-up of anyone 0.42-0.69 M ticks before its end |
| lab_in | the lab's ground floor, the entrance cell | everyone L18 | 1,595-1,611 | 5.08-5.19 M | Rudo, Amy L13-14 -> L17 | 0.08 | its 200 trips, short (12 of 12: Rudo and Amy L17); the lowest's last level-up 0.77-1.03 M ticks before its end |

Every grind ended at its trip limit, never at its target: 9.64-9.80 M ticks
(mean 9.71 M) and 3,204-3,218 fights to take the lowest member from L6 to
L17. Why L18 was out of reach: Rudo needs 22,298 EXP and Amy 22,666 to go from
L17 to L18, and four alive on the ground floor share 116 EXP a fight -- about
30 each, some 750 fights, 2.4 M ticks, for that one level. Nei (L34 there,
whose curve is far flatter) takes a full quarter of every fight.

**Policy against policy** (tools/grind_trials.py: 1.5 M ticks from a seed's
own save, seeds 1 and 2, EXP counted from each fight won by the victory's
split):

| From | Policy | EXP a member a 100 k ticks | Ticks a fight | Wipe-outs | Levels |
|---|---|---|---|---|---|
| end of `amy` (Amy L6) | Oputa's field | 290-299 | 2,905-2,919 | 0 | Amy 6 -> 10-11 |
| | the ground floor, entrance cell | 858-861 | 3,331-3,381 | 0 | Amy 6 -> 13 |
| | the ground floor, long walks | 850-860 | 3,357-3,364 | 0 | Amy 6 -> 13 |
| end of `amy_grind` (Amy L11) | Oputa's field | 300-308 | 2,839-2,890 | 0 | Amy 11 -> 12 |
| | the ground floor, entrance cell | 874-879 | 3,300-3,371 | 0 | Amy 11 -> 14 |
| | the ground floor, long walks | 849-901 | 3,229-3,278 | 0 | Amy 11 -> 14 |

The ground floor pays about 2.9 times what Oputa's field pays a tick, from
Amy's first level on, and the long walks pay what the entrance cell pays (the
roll counts steps, not where they go).

**Now** (tools/play.py, tools/story.py):

* `play.encounter_group` answers off Motavia: Skure's table (loc_23DBA,
  eight by eight) and every other level's own byte. `play.Ground` on a
  dungeon floor keeps its stairs, holes and damage floors out of the ground
  (Motavia's grounds are as build 102a made them: `mild_ground` is the same
  1,258 cells).
* `play.dungeon_grind` gains `ground` -- onto the ground, then `roam` anchor
  to anchor for the trip's fights -- and `gear` -- the best gear the money
  covers at every return to town. Both default to off: the old path sends
  what it did.
* `story.lab_ground()`: the ground floor reached from inside the door, 1,133
  cells -- the entrance hall, the east hall, the west corridor and its south
  rooms, the north-west hall, the north rooms -- with six anchors round it
  (legs of 22-128 steps).
* Stage `lab_in` now follows `amy` directly: Oputa's hospital, gear and
  mates; the door blown (unless its byte, `biosystems_lab_door_blown`, is
  already set); the ground floor by long walks, gear in Oputa at every
  return; until everyone is L17 (`LAB_TARGET`), 400 trips at most. Stages
  `amy_grind` and `lab_grind` are gone (their story checkpoints in
  `routes/saves/` are no longer read by anything).

Why L17 and not L18: it is what the three grinds reached in all twelve seeds,
and `lab` (F1 and F2's chests, F2's door, the Recorder in B1) went through at
it on its first try in all twelve. A level more here costs about 2.4 M ticks
at 116 EXP a formation; the next grinds that need levels (Zema's for Hugh,
the Underwater Passage's before Climatrol, 444 EXP a formation) pay more a
tick.

**Measured against 102b**, seeds 1-12 from a new game to the end of `lab`,
the probe's pace:

| | 102b | 103 |
|---|---|---|
| the grinds from Amy's joining (fights) | 3,204-3,218 (mean 3,210) | 1,772-1,823 (mean 1,795) |
| the grinds from Amy's joining (ticks) | 9.64-9.80 M (mean 9.71 M) | 5.73-5.87 M (mean 5.79 M) |
| lowest member's levels a 100 k ticks | 0.11 | 0.19 |
| party after | Rolf L17-18, Nei L34-35, Rudo L17, Amy L17 | Rolf L17, Nei L34, Rudo L17, Amy L17 |
| ticks from the grind's last level-up to its end | 0.77-1.03 M (the lowest's) | 2-23 k |
| a new game to the end of `lab` (ticks) | 12.40-13.40 M (mean 12.56 M) | 8.46-9.66 M (mean 8.67 M) |
| fights to the end of `lab` | 4,073-4,324 | 2,655-2,958 |
| tries that did not finish | 2 (seed 6's first walk, `paseo`; seed 5 wiped out in `lab_in`) | 2 (seed 6's `paseo`, the same; seed 8 wiped out in `lab_in`, about 1 M ticks in) |
| time | 38 s a seed | 29-31 s a seed |

A wipe-out in the lab still costs the try: seed 8's second try replayed about
1 M ticks from `amy`'s checkpoint and still reached the end of `lab` at 9.66
M ticks against 102b's 12.40 M. That is the next task's (the computer's own
saves). Seed 7 twice: story log, all ten checkpoints and the report identical
(bar wall-clock seconds). Every seed's game changes from `lab_in` on (the
first grind touched): 102b's seed reports no longer describe these seeds past
`amy`.

**Proof** (nothing under src/): 82 PASS lines, the 453 route files and the
Dark Force trial identical to the baseline -- no route or trial uses the
grinds.

### Build 103a: COMPUTER PLAYS's own saves on the way, and its last run

The owner, after 102b: COMPUTER PLAYS should save in its own folder on a trip
back to a town when a level rose, a member joined or a story point passed,
so a failed stage resumes from its last such save rather than its start
(lab_in was 5 M ticks), and a later COMPUTER PLAYS can go on from the
computer's last run -- never writing the player's slots.

**Read first.** Which towns have a Data Memory (town_buildings, the
buildings' table): Paseo, Oputa, Zema, Kueri, Piata, Aukba, Zosa, Ryuon; not
Arima, the tubes or the Skure field. Dezolis's save check (CheckPlanetAndCaps,
ps2.asm 19188, in full): on Dezolis a live Rolf wearing the Magic Cap *or the
Mogic Cap* -- either passes; Rolf dead or bareheaded fails. playthrough's
retry (the checkpoint before the stage, `start slot`), computer_plays's loop
(the checkpoint copied into the computer's slot, the console reset, CONTINUE
by the pad; `pending` stages from the Skure field) and `stage_of_save` (each
stage's marks from the story's own chain of checkpoints).

**Now** (tools/play.py, story.py, playthrough.py, computer_plays.py; src/):

* `play.trip_save(s, town)`: in a stage of `TRIP_SAVE_STAGES` -- arima,
  oputa, amy, lab_in, zema, climatrol, dezolis_grind, kain_grind: the grinds
  -- on a trip back to a town with a Data Memory (on Dezolis only for a live
  Rolf in either cap), save when since the last save a member's level rose,
  a member joined or a story point passed (`play.mark_story`: the lab's door
  blown, Climatrol's chests looted), and keep the save as
  `<checkpoints>/mid_<stage>.json` with a note of what the stage had done by
  then (`mid_<stage>.note.json`: Climatrol's chests). Called by
  `grind_and_gear` and `roam_and_gear` in town, by `dungeon_grind` (with
  `gear`) at each return, and by `back_to_climatrol` after its own Kueri save.
  `play.MID_SAVES` switches it on: playthrough.py and computer_plays.py set
  it; story.py, the routes and the trials leave it off.
* `dungeon_grind(gear=True)` also goes back to town when the lowest member's
  level has risen since it was last there: before, the lab's first save came
  at the first forced retreat, and three of twelve seeds wiped out before it
  (each replaying 0.5-0.7 M ticks). Now 13-15 saves a lab grind.
* A retry resumes the stage from its last save on the way (`playthrough.
  resume_point`, `stage_begins`), both hosts; `playthrough.py --fail-once
  STAGE` as computer_plays.py's. Resumable: stage `arima` skips Rolf's house
  with Rudo in, `amy` skips Paseo and the house with four in, `lab_in` skips
  a blown door, `climatrol` its looted chests (the note); `zema` recomputes
  its catch-up target from the levels it resumes with; the rest start in
  their town as they did.
* The computer's last run: computer_plays.py writes `saves/computer/last.json`
  at every checkpoint and every save on the way (the stage, end or mid, the
  save, the seed). COMPUTER PLAYS's row gains **LAST RUN** after the slots
  when that file exists (sysmenu.c, main.c); started from it the directory is
  not cleared (autoplay.c) and the driver (`--start last`) goes on from that
  save: the stage after an end, the same stage (with its note) from a mid
  save. Picture: `route_out/computer_last_run/computer_plays_last_run.png`.

**Tested through the window's path** (headless main.c, the Escape menu,
SLOT 1 holding the checkpoint before the stage, FAST, the watch pace, seed
11's saves from this build, `--first S --to S --fail-once S`: the first try
plays the whole stage and is counted not finished):

| Stage | Saves on the way, first try | Retry resumed from | Retry's ticks |
|---|---|---|---|
| arima | 18 | Rolf L7 Nei L14 Rudo L7 (the house skipped) | 74,016 |
| amy | 1 | Amy L1, just joined (Paseo skipped) | 193,392 |
| lab_in (seed 2) | yes | Rolf L17 Rudo L17 Amy L16 | 114,812 (first try 3.33 M) |
| zema | 11 | Hugh L12 | 168,881 |
| climatrol | 17 | Rolf L22 Rudo L22 Anna L19, the chests skipped | 64,308 (first try ~2.35 M) |
| dezolis_grind | 8 (Rolf in a cap) | Rolf L32 | 109,827 |
| kain_grind | 8 | Kain L19 | 21,858 |
| oputa | 0 (no level rose) | its checkpoint, as before | 19,534 |

Every one went through; the player's slot byte-identical after each. The
last run: handed back mid lab_in (last.json: its save at Rolf L14, Amy L13),
then COMPUTER PLAYS from LAST RUN went on from that save to everyone L17
(2.55 M ticks), last.json then naming lab_in's checkpoint.

**A forced failure late in lab_in, the probe** (seeds 2 and 3, `--fail-once
lab_in`): the retry replayed 793 k and 405 k ticks, against the whole stage
(5.8 M on 103, 5.1 M on 102b).

**Seeds 1-12 to the end of `lab`** with both of build 103's changes (the
harness as packed): the grind from Amy's joining 5.75-6.05 M ticks (mean
5.86 M) against 102b's 9.71 M, a new game to the end of `lab` 8.56-8.91 M
(mean 8.70 M) against 12.56 M; one wipe-out in the lab (seed 5, resumed from
its save on the way at Amy L7). Seed 4 twice: story log and checkpoints
identical. The save walks change every seed's game from `arima` on (the
first stage that saves), as expected.

Seed 11 from a new game to the ending (before the lowest-level rule): through,
28.08 M ticks against 102b's 28.14 M -- the finished stages 3.8 M ticks less,
spent instead on a Climatrol try that wiped out and started over, which is
what made climatrol resumable.

Seen on the way, not fixed: seed 7's `nido` checkpoint was written with Rolf
dead (`raise_dead` did not catch it), so `darum`'s first try failed at once;
its retry revived him and went on.

**Proof** (sysmenu.c, main.c, autoplay.c changed): 82 PASS lines, the 453
route files and the Dark Force trial identical to the baseline; `make
headless` and `make route_probe` with no warnings. The SDL3 build was not
compiled here.

## Build 104: the save made human, and the planet a level load sets

Built on 103a.

Before any edit (the proof's baseline, on 103a as uploaded): 82 PASS lines,
checksums of the 453 files the routes write (each equal to the uploaded
tree's copy), the 40-fight Dark Force trial (equal to 97b's
`bt97b_after.json` bar its `out` argument), and 103a's own run of seed 11
from a new game to the ending at the probe's pace: through, 24,705,430
ticks, 6,194 fights, one try that did not finish (story log, report and 39
checkpoints kept).

### Task A: the `$C600` block by name

The owner: "anything that makes the game more human comprehensible" -- the
save's `raw_c600`, 158 bytes, most of it unexplained.

**Read first**, in full: the Data Memory's save (loc_B5A4) and the states
around it, SaveData and LoadSavedData, the new game (loc_89EE), the
Telepipe and the Escapipe (ItemAction_Telepipe with its planet test,
ItemAction_Escapipe; TechEffect_Ryuka and TechEffect_Hinas branch into
them), the Mruragum (loc_9CD6), the level exit into a dungeon (loc_6F1C),
the Control Tower's keyboard (loc_DAF0), CONTINUE (loc_D3B6),
Visiphone_ItemSelected, CheckPlanetAndCaps, the tower room's keeper
(loc_C224-loc_C34A), the C menu's ORDER (loc_A48A), Neifirst's party
(loc_DD12, loc_DD7A, loc_F1F6), the name board, the save list, the
opening crawl's camera (loc_E47C-loc_E4D8) and the level screen's camera
cases that read `$C648`/`$C64A` and `$C64C` -- every routine that names a
`$C6xx` address (129 lines), and the three that reach the block through a
register. What each byte is, and the Telepipe's six landings, are in
docs/game.md, "The $C600 block". The port's Telepipe already matched the
ROM branch for branch.

**Now:**

* `src/game/gamedata.h` names every byte of the block something reads
  (`GD_KEEPER_COUNT`, `GD_KEEPER_ITEMS`, `GD_LAST_SAVE_MAP`/`_Y`/`_X`,
  `GD_ENTRANCE_MAP`/`_Y`/`_X`, `GD_SCOOTER_PARKED_Y`/`_X`,
  `GD_LAST_SAVE_PLANET`, `GD_LAST_SAVE_VISIPHONE`; the scratch `GD_STEP`,
  `GD_ORDER_NEW`, `GD_PARTY_HELD`, `GD_PARTY_HELD_NUM`, `GD_NAME_TYPED`,
  `GD_SLOT_IN_USE`), read through `gamedata_word` / `gamedata_set_word`.
  Every bare offset in game.c, menu.c, building.c, battle.c, probe.c and
  gamedata.c goes through them (proved alone first: 82 PASS lines, 453
  files, the trial -- all identical).
* The save gains `last_save` (`planet`, `by_visiphone`),
  `telepipe_lands_at`, `dungeon_entrance` (each `map`, `x`, `y`) and
  `tower_room_items` (item names). Maps are written by name, in
  `location` too ("Zema", "MotaviaOutside"), and read as a name or a
  number. `raw_c600` stays; the named fields win. `tools/play.py` reads a
  save's map through `map_index` (name or number).
* Comments: the new game's (it said nothing had looked at what reads
  `$C646`-`$C650`), and the tower keyboard's (its YES goes where an Escapipe
  goes, not a Telepipe; the port also clears the scooter flag there, which
  the ROM does not -- the same thing in the tower).
* Route `telepipe_edited`: a save with `telepipe_lands_at` edited by hand to
  Zema 23,12 over a raw block that says a Visiphone save made on Dezolis.
  103a: the Telepipe lands in Paseo 31,16 (FAIL); now Zema 23,12.

**Found on the way: the planet.** LoadLevelData (ps2.asm 13229) sets
`planet_index` from bit 7 of the level record's first byte on every load.
The port never set it: it stayed what the last save loaded said. So on
Dezolis the port took the party for on Motavia -- CheckPlanetAndCaps
passed a dead or bareheaded Rolf at every shop and Data Memory, the Magic
Cap never doubled a price nor made a sale 10 meseta, the Telepipe took its
Motavia branch, and every Data Memory save on Dezolis recorded Motavia (the
story's Dezolis checkpoints in `routes/saves/` say so). `load_level` now
sets it. Route `dezolis_planet_on_load`: a Zosa save (it says Motavia),
Rolf's Magic Cap off by hand, into Zosa's Data Memory: `$810` "Danyaraha?
bebekucha?" (103a: `$301`, welcome, FAIL).

The harness had been written for the ROM's rule (price_factor, the caps,
trip_save's check) but had never met it. Seed 11 with the fix alone: through,
but 28.64 M ticks, five tries that did not finish and 310 purchases that did
not happen -- Rolf dead in a Dezolis town, the shops refusing him.
`play.rolf_alive_for_shops`: on Dezolis, Rolf dead, the town's clone lab
before gear_up, stock_mates, stock_item and a Data Memory save.

**Seed 11, 103a against 104** (from a new game to the ending, the probe's
pace): every stage through `roof` identical (story log, fights, ticks);
from `skure` on the game changes, as it should: through, 24,357,582 ticks
against 24,705,430, 6,133 fights against 6,194, one try that did not finish
(`menobe`'s first, against 103a's `noah_grind`), 5 purchases missed against
2. With only the save's names (before the planet fix) the run was identical
to 103a's: story log and report bar the seconds, and all 39 checkpoints
equal once the new fields are set aside and a map's name read as its
number; every checkpoint's named fields equal to its raw bytes.

**Still bare numbers** (looked over, not changed): in the save,
`next_recruit_event` (an index into the recruits' table), `techniques_a` /
`techniques_b` (how many battle and map techniques are known, not which),
`status` (never read), `slot_label.name` of four spaces for a blank, and the
teleport station's towns in `flags_c700` as map numbers; in the story log,
script ids (`$1701`, `$183B`), chest events (`chest 0x2`) and the musk cats'
object numbers (`$7D`); in the probe's `state` line, `level $04`. Not seen
through yet: `play.save_town` takes a Dezolis save's town as where the next
Telepipe lands even on Motavia, where it lands in Paseo.

**Proof** (src: gamedata.[ch], game.c, menu.c, building.c, battle.c,
harness/probe.c): 84 PASS lines, the 82 old routes' lines and their 453
files identical to the baseline, the two new routes adding their own; the
Dark Force trial identical; `make headless` and `make route_probe` with no
warnings. The SDL3 build was not compiled here.

## Build 105: the CRT simulation, phase 1

Built on 104.

Before any edit (the proof's baseline, on 104 as uploaded): 84 PASS lines,
checksums of the 461 files the routes write (each equal to the uploaded
tree's copy), the 40-fight Dark Force trial, and 104's own run of seed 11
from a new game to the ending at the probe's pace, in a scratch directory:
through, one try that did not finish. (The seed 11 report packed in the
tree is an older one, from before build 101's ticks column.)

The owner, on the white: D65, what the American sets used -- not the Japanese
sets' 9300 K the handover had as a second setting. So there is no WHITE
POINT row; `crt.c` keeps the white as a gain per gun, (1, 1, 1), so another
white would be three numbers.

### The Escape menu's CRT page

A CRT row after FULLSCREEN (so COMPUTER PLAYS is five stops down now, not
four). Its value is what will be drawn: OFF, SCANLINES, GRILLE, SLOT MASK or
SHADOW MASK -- the mask when this display can have it, else the scanlines
when it can have those. C opens the CRT page, its own window (36 cells, the
menu's frame and font, a title, rows three apart): SCANLINES, MASK,
SUBPIXELS, HDR. C on HDR opens the HDR page: HDR, PEAK, WHITE. Left and
right step a value (held, they repeat four frames apart after the cursor's
delay), C steps on, B goes back a page, Escape closes the menu from any
page. Everything is kept in `settings.json` under `"crt"`; a file without it
reads as everything off, PEAK AUTO, WHITE 200.

SCANLINES is the page's addition to the handover's list: it gave the
scanlines a rule of their own (two display lines a scanline) and no
setting.

A value the display cannot have says why on the line under it, so the
choice stays in view: NEEDS FULLSCREEN (fullscreen on this display would
do), NEEDS 480P, NEEDS 1080P, NEEDS 1440P (not even fullscreen here),
DESKTOP SCALED (the desktop's resolution is not the panel's; nothing in the
game helps), NO HDR. The rules are `crt.c`'s (`crt_scanlines_why`,
`crt_mask_why`, `crt_hdr_why`): the panel's own pixels -- fullscreen, or a
window the desktop does not scale -- and k, the whole multiple of the 224
lines that fits, at least 2 for scanlines, 4 for the grille, 6 for the slot
and shadow masks.

What the display is (`CrtDisplay`) comes from `platform_display_info`: the
SDL3 platform asks the renderer and the window every frame the menu is up;
the headless platform reads `PS2_DISPLAY` (`crt_display_parse`'s words:
`3840x2400 fullscreen hdr=6.25/2.5`, `1280x800 full=3840x2400
window-scaled`); the route probe's `display` command sets it, and `expect
crt ROW VALUE [/ WHY]` compares what a row shows.

Routes: `crt_menu` (every value of the CRT page against eight screens, ten
shots) and `crt_hdr` (the HDR page: AUTO from SDL's headroom, PEAK's and
WHITE's stops, NO HDR, three shots). Both fail on 104, which has neither
command. `escape_menu_computer` takes one more `down`; its three PNGs and
`escape_menu_fullscreen`'s three show the menu a row taller.

Headless main.c (in a scratch directory): the page's settings written to
`settings.json`, read back by the next run (a second run toggling one value
keeps the rest), a file with `"peak": "auto"` and an out-of-range white
(clamped to 1000), and a file from before build 105 (everything off).

### The platform

`platform_sdl3.c` creates its window with `SDL_WINDOW_HIGH_PIXEL_DENSITY`
as well as `SDL_WINDOW_RESIZABLE`: on a scaled desktop the window's buffer
is the panel's own pixels, and the frame is already placed by
`SDL_GetRenderOutputSize`. `platform_display_info` takes the window as
scaled when its pixel density falls short of the desktop mode's; it does
not decide DESKTOP SCALED (no SDL call gives the panel's native size, and
the largest fullscreen mode is not it on macOS), which only a route or
`PS2_DISPLAY` can show until `tools/display_probe.c` has been run on
The owner's screens. `platform_set_crt` keeps the settings for phase 2. Not
compiled here (no SDL3).

### The PNG writer

`tools/route_probe.c`'s PNG writer moved to `src/core/pngwrite.c`,
unchanged, and grew RGB, 16-bit samples and a cICP chunk for `crt_ref`'s
crops. Proved: every route PNG byte for byte what it was.

### The reference

`src/core/crt.c`'s chain, run by `tools/crt_ref.c` (docs/crt.md, "The model"
and "The reference"): the index through the DAC ladder and a gamma of 2.4,
the picture laid out at k, each source pixel a box through a normal across,
each line a normal whose height grows with its brightness, scaled so a flat
field keeps its level, the mask in display pixels, the light (HDR: WHITE on
average, the scanlines whole first and the mask as deep as PEAK allows;
SDR: the full chain, the brightest subpixel at 1.0), the sRGB and PQ
encodes. `make crt_check`: 27 checks, all passing -- PQ's codes at known
points, the mask's layout (every channel averages 1 over a period; the
stripes' lit subpixels every 4 in a 4-pixel triad, 2, 2, 5 in a 3-pixel
one; OTHER whole pixels; the slot's bridges, the shadow's offset), what
each display can have, a white field's mean within 1% of WHITE with nothing
over PEAK for every display, mask, subpixel order and PEAK 400-2000, D65
through the chain, SDR nothing over 1.0, a frame of the game under PEAK.
The crops in `route_out/crt_reference/`: the title's logo and the fight's HP
box at the four displays, SDR, and HDR10 PQ with a cICP chunk.

The triad: the handover's plan -- three pixels below 4K, four from 4K up --
stands, but Sony Megatron's coarsest 1080p grille is four pixels (R, G, B,
dark), so the owner may prefer four everywhere: `crt_plan_resolve`'s one line.
Three pixels cannot space the lit subpixels evenly; the reference takes the
panel's order reversed (Megatron's 4K "800 TVL" grille), two apart and a
gap of five. The shadow mask is four pixels everywhere.

### The display probe

`tools/display_probe.c` (`make display_probe`, SDL3 only; not compiled
here): SDL's version, the displays and their modes, the GPU devices
preferring performance and low power (the device's name from SDL 3.4,
behind `#ifdef`), and for the window windowed, fullscreen and windowed
again: points, pixels, pixel density, display scale, SDL's HDR state, the
swapchain compositions and present modes the device offers, and the same
as `PS2_DISPLAY` words. Both SDL3 files were compiled here only against a
stand-in header written from SDL 3.2's documented declarations (a check of
the C, not of the API).

### The ProArt P16, measured; PEAK's AUTO

The owner ran `display_probe` on the P16 (SDL 3.4.16, Wayland; docs/crt.md
has the numbers). High pixel density does what it should: the
960x672-point window is 1824x1277 pixels, the desktop's own density of
1.9, so it is not scaled -- k 5 windowed (scanlines, the grille), 10
fullscreen. HDR is on with a headroom of 1.624 over an SDR white of 1.000;
all four swapchain compositions; SDL_GPU on Vulkan, the RTX 5070 Laptop GPU
(not a 5060) preferring performance, the Radeon 890M preferring low power.
The largest fullscreen mode is the desktop's pixels, so on Wayland DESKTOP
SCALED could be decided from it (not done until Windows and macOS are
measured).

Found: PEAK's AUTO read SDL's SDR white as scRGB's (1.0 = 80 nits), which is
Windows's; on Wayland (and macOS) SDL's SDR white is 1.0 with no nits said,
so the P16 got 130 nits. `CrtDisplay.sdr_nits` is now the platform's reading
(Windows: the level x 80; elsewhere: the level x 203, BT.2408's reference
white), AUTO is headroom x that (330 on the P16), `crt_display_parse` takes
`sdr=N`, and the probe prints it. Route `crt_p16`: the probe's own words,
windowed and fullscreen -- fails on 105a (`sdr=` unknown; AUTO 130), passes.
With so little headroom WHITE sets the mask's depth: at PEAK 330, WHITE 200
gives 11%, 100 gives 55%, 80 gives 77%.

### The Windows PC, measured

The owner ran the probe on Windows (docs/crt.md): the PA32UCX and a Dell
UP2718Q, both 3840x2160 at 100%, Windows's HDR off; Direct3D 12 on an RTX
3080 Ti. The window is not scaled; k 3 windowed, 9 fullscreen; NO HDR, AUTO
1000. Route `crt_pa32ucx`: its words, windowed and fullscreen. The probe's
own AUTO line printed headroom x SDR white (80) with HDR off, where the
game falls back to 1000: it now says what the game does. Direct3D 12 gives
the driver's version but no name: `(not given)`.

With Windows's "Use HDR" on, the PA32UCX: a headroom of 14.995 over an SDR
white of 1.000, AUTO 1200 nits -- its rated peak, so the scRGB x 80 reading
is right on Windows; still no HDR_EXTENDED_LINEAR from Direct3D 12 (phase 3
there is PQ). `crt_pa32ucx` has both, HDR off and on. At 1200 nits on 4K the
mask is whole up to WHITE ~238.

### Escape goes back a page

The owner: the pages should go back a level on Escape. Escape now does what B
does -- HDR page to CRT page, CRT page to the menu (its cursor on CRT), and
closes the menu from the menu itself. Before, Escape closed the menu from
any page. Route `crt_escape_back`: fails on 105c (the menu closed at the
first Escape, line 22), passes.

### Windows

The handover wanted Windows builds; COMPUTER PLAYS's host is POSIX. Now:
`autoplay.c` compiles to a stub on `_WIN32` (COMPUTER PLAYS shows POSIX ONLY,
never starts); main.c's two `access()` calls are an `fopen` check; the
route interpreter's `outfit` splits with its own `split` rather than POSIX's
`strtok_r` (its four routes byte for byte what they were), and says "an
empty command" rather than read past an empty one (no caller passes one:
both skip blank lines first); `sysmenu_save_settings` makes `saves/` when a
fresh tree has none (tested headless). README: MSYS2's UCRT64 packages and
`make`. Not compiled on Windows here -- `-D_WIN32 -fsyntax-only` of main.c
and the harness on Linux's headers is as far as the sandbox goes. The owner
built and ran the game from build 105's tree on both Windows and Linux.

### Task B: the harness on 104's planet fix

**Seed 7's `nido` checkpoint with Rolf dead.** Read: `raise_dead` runs, then
`story.checkpoint` -- which, on a level with no Data Memory (Nido), walks to
Paseo's; seed 7's walk met fights that killed Rolf, and the save was made
with him dead, so `darum`'s first try failed at once. `playthrough.py` now
checks the party after the checkpoint and, with anyone dead, raises them
(Paseo's clone lab) and saves again, with no walk. Seed 7 to `darum`: the
checkpoint alive, `darum`'s first try through.

**A Dezolis save's town on Motavia.** `play.retreat` went for `s.save_town`,
the town of the last Data Memory save, even on Motavia after a save on
Dezolis, where the Telepipe lands in Paseo (docs/game.md, "The $C600
block"). `play.pipe_town`: Paseo on Motavia's field or in its towns after a
save on Dezolis, else the last save's town (on Dezolis after a Motavia save
the pipe lands on the Skure field, no town -- as before); taken after any
dungeon is behind.

**Seed 11 replayed** on the new harness against 104's own run: the story log
and all 39 checkpoints identical, the report identical bar the seconds.

**Seeds 1-12 from a new game to the ending** (the probe's pace; reports and
story logs in `route_out/playthrough_s<N>/`), all through:

| Seed | Ticks | Fights | Tries not finished | loot()'s last-chest wipe-outs |
|---|---|---|---|---|
| 1 | 23,774,209 | 6,054 | 1 (Dark Force) | 1 |
| 2 | 22,843,798 | 5,939 | 0 | 1 |
| 3 | 23,207,718 | 5,997 | 0 | 1 |
| 4 | 23,817,705 | 6,038 | 2 (Menobe, Noah) | 0 |
| 5 | 23,324,705 | 5,945 | 1 (the lab) | 1 |
| 6 | 23,806,012 | 6,075 | 1 (Paseo) | 0 |
| 7 | 23,304,709 | 5,990 | 6 (Motavia, Shir, Menobe x2, Noah x2) | 1 |
| 8 | 24,201,891 | 6,153 | 4 (Uzo, Skure, Noah, Dark Force) | 2 |
| 9 | 24,123,526 | 6,145 | 4 (Menobe x2, Dark Force x2) | 3 |
| 10 | 19,667,600 | 5,042 | 4 (Climatrol, Skure, Dark Force x2) | 2 |
| 11 | 23,603,989 | 5,993 | 1 (Menobe) | 1 |
| 12 | 24,066,418 | 6,031 | 1 (Noah) | 0 |

(Ticks and fights of the stages that finished.) **loot()'s last-chest fix**
(102b) was met in nine seeds, twelve times, and every time the CONTINUE was
followed by the list again and the level's chests opened. Seed 7's walk to
the save raised the dead once.

**Found, not fixed: Menobe a Nei item short.** In seeds 4, 7 and 9
Menobe's try ends with one of the Nei Crown and the Neimet: a wipe-out
later in the dungeon CONTINUEs a save from before an earlier floor's chest,
which the CONTINUE shuts again, and loot() redoes only the current floor's
list. The stage is played again from its checkpoint (a try each; seed 7
twice). A fix: after a CONTINUE, loot() again the floors of this stage the
save came before.

**Proof** (src: core/crt.[ch], core/pngwrite.[ch], game/sysmenu.[ch],
platform/platform.h, platform_headless.c, platform_sdl3.c, harness/probe.[ch],
harness/autoplay.c, main.c; tools/route_probe.c, crt_ref.c, display_probe.c,
play.py, playthrough.py; Makefile): 89 PASS lines (with `crt_pa32ucx` and
`crt_escape_back`; the Escape change moved no other route's line); the 82 routes the menu
does not touch give PASS lines and 453 files identical to the baseline;
`escape_menu_computer`'s line is 31 commands and 413 ticks (30 and 411), for
its extra `down`; the Dark Force trial identical; `crt_check` 27 of 27;
seed 11 identical to 104's run. `make headless`, `make route_probe` and `make crt_ref` with no
warnings. Not compiled here: `make` (platform_sdl3.c) and `make
display_probe`.

## Build 106: the last bare numbers named, loot() after a CONTINUE, the CRT's four-pixel triads, phase 2

Built on 105.

Before any edit (the proof's baseline, on 105 as uploaded): 89 PASS lines,
checksums of the 493 files the routes write (each equal to the uploaded
tree's copy), the 40-fight Dark Force trial, `crt_check` 27 of 27, and 105's
own run of seed 11 from a new game to the ending at the probe's pace, in a
scratch directory: through, one try that did not finish (`menobe`), its
story log byte for byte the one packed in 105's tree.

The owner's answers to 105's two questions: the triads "whatever is most
compatible" -- taken as four pixels everywhere (below); phase 2's shaders
compiled on his machine with SDL_shadercross and checked in -- yes.

### Task A: the bare numbers named

**The save** (docs/game.md, "The recruits and the technique counts"; every
routine that reads or writes them read in full: loc_112C4/loc_112FC,
Character_ProcessLevelUp, loc_C7D6, CommandAction_Technique,
Win_BattleTechList, Win_LevelTechList, LoadSpritesInLevel's tail,
Building_RolfHouse's loc_B04E and loc_B282, loc_AFE6, ProcessStealItem, Hugh's
loc_DBF8). `next_recruit_event` was not an index: `$C606` is the last recruit
the story has sent to Rolf's house by character number (Nei 1 ... Shir 7), 8
while Shir is away with a theft; `$C604` the last to have come. They are now
`party.last_recruit_joined` and `party.last_recruit_sent`, by name ("none",
"Nei" ... "Shir", "Shir away"). `techniques_a` / `techniques_b` were a
character's `+$25` and `+$26`: how many of the battle and map halves of
CharAvailableTechniques are known, and bit 7 -- Musik in that half's last
known place, the map byte's once Ustvestia's lesson is learned. Now
`battle_techniques_known`, `map_techniques_known` (Musik counted),
`knows_musik`, and `battle_musik` only if that bit is ever set (no game does).
The old keys still load (every route's save is in the old form); a number
under a new recruit key too. `computer_plays.story_flags` reads either.

Kept as the port had it: CommandAction_Technique tests the whole battle byte
where the port tests its count -- different only with bit 7 of `+$25` set
and nothing known, which no game reaches.

**The logs.** The story logs name scripts (`ACTION_GOTTEN_HOLD_MESETA`: the
game's `SCRIPT_` names, else `gen_script_ids`' rule -- 611 names, all
different), levels (`chest on SkureB1 (9, 49)`, `Aukba gear:`, `trip 0 on
BiosystemsLabGroundF`), townspeople by kind and placed cell (`MuskCat at
23,71`), the tower's boxes by card and a party's enemy mix by type. The
probe's state line, level-change line and `save-here` line name the level
(`level Paseo cell 45,18`); `play.parse_state` reads a name or an older
probe's `$XX`. docs/harness.md, "Names in the logs". Still numbers: the
probe's `event $XXX` line and the route language's arguments.

Route `save_named_fields` (a save with only the new names; Rolf's house brings
Shir back, the count 8 to 7, Shir last in the party; Rolf's TECH list RYUKA,
HINAS, MUSIK): fails on 105 at line 31 (the count read as 3), passes. A save
in the old form (`rever_rudo_dead`, Rolf's Musik bit set, both counters 7)
loaded and written back: every character's two counts and both recruits the
same, only the new keys written.

**Proof.** 90 PASS lines: the 89 old routes' lines identical to the
baseline. Of their 493 files, 419 identical; the other 74 are logs, each
byte for byte 105's once its level names are turned back into `$XX`. The
Dark Force trial identical (its JSON bar the `--out` it echoes). **Seed 11**
on the new harness against 105's own run (the unchanged-play proof the
handover asked for): the report identical bar the seconds; all 39
checkpoints equal once the new fields are turned back into the old; the
story log's 1,636 lines, each 105's once the names are set aside -- script
names mapped back to ids, levels, townspeople -- and the rewordings
allowed for ("in town" / "on level" to the name, "chest" to "chest on",
"Climatrol F" to the floor's name, the tower's `box $13` to `the GreenCard's
box`, with the same scripts and outcome, the mix's `0x20` to `living`).
`make headless` and `make route_probe` with no warnings.

### Task B: loot() after a CONTINUE, and a chest left for hurt

105 found Menobe's tries ending a Nei item short (seeds 4, 7, 9). Read in
full first: `loot()`, `prism_dungeon`, `recover()`, `checkpoint()`,
`play.save`, `play.search`, and the game's `level_open_chest` (a chest's byte
is `c700[(FLAG_CHESTS + index) & 0xFF]`, `$C780` up: 0 shut, `$80` locked,
anything else opened). 105's own story logs show two gaps:

* **A CONTINUE redid only the current floor.** Every wipe-out in a Prism
  dungeon CONTINUEs the save made in town before it, which shuts again every
  chest opened since; `loot()` built its list again for the floor it was on
  and never went back up. Menobe's Nei Crown is on the ground floor (55, 37),
  so seeds 4 (try 1), 7 (tries 1 and 2), 9 (try 2) and 11 (try 1 -- the one
  that failed in the baseline too: the same gap) ended with the Neimet only.
* **A chest left "unreached" for hurt was left for good.** Seed 9's first try:
  hurt on the way to F2's (41, 47), a retreat, hurt again, "unreached" -- and
  the Neimet was in it.

The fix (tools only). The session keeps the floors `loot()` has worked since
the last save (`s.looted`; `play.save` and the checkpoint's Visiphone save
clear it, a stage's start too), and `recover()` marks a CONTINUE
(`s.continued`). `loot(..., wanted=)` -- `prism_dungeon` passes the dungeon's
Nei items -- then puts first in its list the chests on those floors that hold
a wanted item nobody carries and that the save shut again
(`story.shut_again`: `play.chest_item` from chests.json, `play.chest_shut`
from the chest's own byte, asked with the probe's `expect flag`, which costs
no tick). A wanted chest left unreached while the party was hurt
(`s.hurt`) goes once more to the end of the list. The log says `loot
MenobeF2: after the CONTINUE, shut again: MenobeGroundF (55, 37), ...`.

A first cut went back for every chest the save shut. Seed 1's Ikuto -- the
same as 105's up to there -- then walked B2 and B3 again after each wipe-out
for chests with no Nei item (a LacDagger and a PlsCannon), met more wipe-outs,
and lost its try at the title (105: one CONTINUE and both Nei items, which
are deeper). Hence `wanted`.

Seeds 1-12 to the ending on the final code (their report.txt and story.txt
now in route_out/playthrough_s<N>/):

| seed | fights 105 -> 106 | tries not finished | Menobe's try | what changed |
|---|---|---|---|---|
| 1, 2, 3, 5, 6, 8, 10, 12 | the same | the same (1, 0, 0, 1, 1, 4, 4, 1) | 1 | nothing: every stage's fights and ticks 105's; the story logs 105's with the names set aside |
| 4 | 6038 -> 6217 | 2 -> 1 | 2 -> 1 | Menobe: one shut-again |
| 7 | 5990 -> 6046 | 6 -> 1 | 3 -> 1 | Menobe first try; motavia's one lost try as before |
| 9 | 6145 -> 5967 | 4 -> 3 | 3 -> 1 | Menobe: the unreached Neimet taken on its second go |
| 11 | 5993 -> 6050 | 1 -> 1 | 2 -> 1 | Menobe: three wipe-outs, the ground floor's and F1's chests looted again after each; one Noah-grind try lost later |

All twelve through; tries that did not finish 25 -> 18. The rest are the
grind's and Dark Force's variance as in 105 (seed 9 one at Guaron).
Routes: 90 PASS, the 493 older files identical to task A's; the Dark Force
trial identical. `src/` unchanged by task B.

Log wording after a wipe-out at the title: a level that is None is "no
level (off the map)", and a talk round names people by the level it
started on (seven log calls with the level on a continuation line missed
by task A named too).

The owner saw COMPUTER PLAYS leave the title's attract loop running after a
wipe-out. That is its giving up -- a stage out of its four tries, or a
CONTINUE that did not take -- after which the driver ends and the game is
left where the last try was. Which of the two it was is in that run's
`saves/computer/report.txt`; not yet looked at.

### The triad: four pixels on every display

The owner's answer, "whatever is most compatible": four pixels everywhere
(R, G, B, dark), the layout that spaces the lit subpixels evenly on every
panel -- RGB and BGR stripes and OTHER alike, even when SUBPIXELS is set
wrong -- and Sony Megatron's coarsest 1080p grille. `crt_plan_resolve` sets
`p->triad = 4`; the model keeps the 3-pixel layout (`triad_channel`), so
going back is that line.

`make crt_check`: 27 of 27 (the layout checks run both triads by the model,
not the plan). checks.txt's light table below 4K is four-pixel now: the
1080p grille at PEAK 400 takes a mask depth of 0.24 (0.36 with three) and is
whole from PEAK 1000 as before; in SDR a white field under the 1080p grille
averages 0.214 of SDR white (0.29 with three), 1440p's 0.205. The crops:
the documented title command reproduces 105's 20 crops byte for byte but for
the three the triad moves (1080p grille, 1440p grille and slot) -- shown side
by side: 105's lit columns bunched in the panel's order reversed, 106's R, G,
B, dark every four. 105's fight crop had no recorded source (a search for it
in the routes' PNGs was given up), so it was made again from one written
down in docs/crt.md (climatrol_nei_dies's fight, Rudo's HP box). Routes: 90
PASS, every file the same.

### Where build 106 stops

Phase 2 was not started. After tasks A and B and the triad, the conversation's
context was too full to read the model (crt.c) and SDL_GPU closely and write
two shaders and a GPU path that cannot be compiled or run here and must match
crt_ref's crops; it starts in a fresh context as build 107 (NOTES, "Next
(after 106)"). Nothing of phase 2 is in this tree: no shader, no SDL_GPU in
the game.

## Build 107: the CRT simulation's phase 2 (SDR through SDL_GPU), and COMPUTER PLAYS's report

Built on 106 (the tree as 106d left it).

Before any edit (the proof's baseline): 90 PASS lines, checksums of the 499
files the routes write (each the uploaded tree's copy), the 40-fight Dark
Force trial, `crt_check` 27 of 27, and the two documented crop commands
reproducing all 40 crops in route_out/crt_reference/ byte for byte.

### Task 1: phase 2 of the CRT simulation, SDR

Read in full first: crt.c's model (crt_cram_linear, beam_sigma, beam_row,
white_beam_peak, triad_channel, crt_mask_full, crt_mask_at, mask_peak_of,
crt_plan_resolve, crt_render), crt.h's CrtPlan, platform_sdl3.c,
display_probe.c's SDL_GPU setup, and docs/crt.md. docs/crt.md, "Phase 2
(build 107)" and "The read-back check", has the design; in short:

* **The model as tables** (crt.c, `crt_tables_build`): the beam by DAC level
  and row offset, the weights across by display column, the mask over two
  triads by one line -- each from the functions `crt_render` uses
  (`across_weight` and `across_span` taken out of it for this; its 40 crops
  and checks.txt byte for byte the same after). The shaders sum; they model
  nothing.
* **Two passes**, the model's sum in the other order (exact: a column's
  weight across depends on neither the line nor the level): down into a
  float target 320 wide and the output's height, then across with the mask
  and sRGB's curve into the swapchain. The handover's plan had the beam
  pass at the picture's size and a mask pass after; down-first is cheaper
  and needs no intermediate at display resolution.
* **The flat picture** when the CRT is OFF: the placeholder's layout
  (`crt_sharp_layout`, what `frame_layout` computed) and look, on the GPU.
* **The platform** (`platform_sdl3.c`, `crt_gpu.[ch]`): SDL_GPU when a device,
  the compiled shaders and the window can be had, else SDL_Renderer as
  before. The settings each switched off where the page says the display
  cannot have them; HDR ON drawn SDR (phase 3). `display:` and `crt:` lines
  on stderr. `PS2_GPU=0` forces the placeholder, `PS2_GPU_DEBUG=1` the
  debug layer.
* **The shaders** (`src/shaders/`, HLSL) and `make shaders` (SDL_shadercross
  to `shaders/`, SPIR-V, DXIL, MSL; checked in from the owner's machine). The
  recipe was run here against a stand-in `shadercross` that records its
  arguments: twelve files, `-s HLSL -d SPIRV|DXIL|MSL -t vertex|fragment
  -e main -o shaders/<name>.<spv|dxil|msl>`.

**Checks.** `make crt_sim_check` (new; `crt_check` stays 27 of 27): the
passes in C and float against the model, 32 checks, every code within 1 (16
of 3.8 M off by one). `crt_gpu_check` (new, SDL3): the read-back check on
the real GPU, 72 checks. Here it was run against a mock SDL_GPU that runs
the shaders as C from the HLSL: 72 of 72; and the game's own
`platform_present` against it at 1920x1080 gave crt_ref's grille and
scanlines crops byte for byte and the flat picture exactly.

**Not compiled or run here:** the HLSL (no shader compiler), SDL_shadercross,
and every SDL call -- `platform_sdl3.c`, `crt_gpu.c` and `crt_gpu_check.c`
were compiled only against a stand-in `<SDL3/SDL.h>` written from SDL 3.2's
documented declarations (a check of the C, not of the API), and run only on
the mock. `make ps2` was not built here. What the owner should check, on the
P16 (Vulkan) and the Windows PC (Direct3D 12):

1. `make shaders` -- that SDL_shadercross takes these flags and writes all
   twelve files (DXIL may need Windows); errors are HLSL's, and wanted
   verbatim.
2. `make crt_gpu_check && ./crt_gpu_check` -- `72 checks passed`, else the
   FAIL lines of route_out/crt_gpu_check/report.txt.
3. `make && ./ps2` -- the `display:` line says SDL_GPU; the picture as
   before with the CRT OFF; then fullscreen with SCANLINES and each MASK:
   the `crt:` lines, the look, and whether it holds 60 Hz (PS2_PACE=1).
4. With `PS2_GPU=0`, the placeholder exactly as build 106.

**Proof** (the rule for refactors, though only the CRT's files changed): 90
PASS lines identical to the baseline; the 499 files the routes write
identical; the Dark Force trial identical (its JSON bar `--out`);
`crt_check` 27 of 27, checks.txt identical. `make headless`, `make
route_probe` and `make crt_ref` with no warnings.

### Task 2: COMPUTER PLAYS left at the title

The upload carried `saves/computer/` (read here, not packed): report.txt is
a run of seed 33032 from the owner's slot 2 at the `lab` stage, 236 s, 8 fights,
"tries that did not finish: 0", ending `finished: handed back (EOFError)` --
the pipe closed under the driver, which is what putting the lamp out does.
Neither of 106's explanations (a stage out of its four tries; a CONTINUE
that did not take) fits it. If this is the run that sat at the title, the
likelier story is a wipe-out the driver never noticed, waiting on a state
the title never reaches until the pad was taken back. Asked: whether this is
that run, and whether giving up should CONTINUE the last save and hand the
pad back in the game. Nothing changed for it.

## Build 108: crt_gpu_check's six FAILs, the CRT simulation's phase 3 (HDR), and a wipe-out the waits missed

Built on 107 with the owner's `make shaders` (SPIR-V, the P16) and his
crt_gpu_check run (route_out/crt_gpu_check/report.txt, kept as his).

Before any edit (the proof's baseline): 90 PASS lines, checksums of the 499
files the routes write (each the uploaded tree's copy), the 40-fight Dark
Force trial, `crt_check` 27 of 27, `crt_sim_check` 32 of 32, and
route_out/crt_reference/ byte for byte.

### Task 1: crt_gpu_check on the P16, 66 of 72

Read first: the three shaders in full, crt.c whole (the tables and their
C mirror), crt_gpu.c, platform_sdl3.c, and crt_gpu_check.c and crt_ref.c's
parts. Every crop passed; the six FAILs were the title whole against the
mirror, slot and shadow masks only, at 1440p, 4K and 3840x2400 -- the two
masks whose rows differ within a line. The across pass took the mask's row
as `(y - pic_y) % k` and added k when that came out negative. Above the
picture's top, where the first line's beam tail still lights rows, the
dividend is negative, and HLSL leaves such a remainder undefined. The owner's
compiled crt_across.frag.spv, decoded here, holds exactly what the HLSL
says (a signed remainder, then the branch), so the difference is the GPU's:
re-rendering those rows on the CPU as an unsigned remainder, which skips
the branch, gives his worst code exactly in five of the six FAILs (71, 71,
91, 125, 97; 70 where he had 69), and no other reading tried does. The rest
of each FAIL's count is the encode's ordinary one-code rounding, as the
passing grille's whole-frame lines show.

The fix: `CrtTables.mask_phase`, -pic_y wrapped into 0..k-1 on the CPU; the
shader takes `(y + mask_phase) % k`, never a negative dividend, on any
backend; the C mirror the same, through `crt_tables_row`. Checks that fail
on 107's arithmetic and pass now:

* `crt_sim_check`: the across pass's row at every row of each display, the
  remainder's dividend not negative and the row the model's -- a scratch
  crt.c with 107's arithmetic fails it at row 0 of all four displays;
* the mock SDL_GPU (rebuilt: 107's was not packed) aborts on a remainder of
  a negative number, as HLSL leaves it undefined: 107's crt_gpu_check with
  107's shader aborts there at 1080p's first row (-92 % 4); 108's passes.

`crt_gpu_check` now says, under any FAIL, which rows and columns are more
than a code apart, the picture's rows, and the worst pixel with both codes.

### Task 2: phase 3, HDR

docs/crt.md, "Phase 3 (build 108)", has it. In short:

* **The swapchain** (`platform_sdl3.c`): with HDR ON where SDL says the
  window has HDR, HDR10 (Rec.2020, PQ) if the window offers it, else scRGB;
  `PS2_HDR_SWAPCHAIN=scrgb` or `=pq` picks. Switched each frame before the
  swapchain texture is acquired when the wanted one changes (idle first,
  the pipelines remade for its format); a refused one not asked for again
  until the window's HDR state changes; SDR the floor. It follows
  `SDL_EVENT_WINDOW_HDR_STATE_CHANGED` and `..._DISPLAY_CHANGED`.
* **The encodes** (crt.c: `CrtEncode`, `crt_encode_params`, `crt_encode_f`,
  `crt_encode_flat_f`, `crt_pq_encode_f`, `crt_encode_model`): HDR10 the
  nits into Rec.2020 (BT.2087's matrix as uniforms), over 10000, ST 2084;
  scRGB the nits over the nits of its 1.0 -- `sdr_nits / sdr_white`, 80 on
  Windows (the handover's figure), 203 on Wayland, where SDL's SDR white is
  1.0 and the platform takes it as 203 nits; the flat picture's SDR colour,
  sRGB-decoded, at WHITE nits.
* **A bug the mock caught:** `crt_gpu.c`'s `same_plan` did not compare the
  encode, so an scRGB plan after an HDR10 one alike in light reused HDR10's
  tables and wrote PQ signals into the float target. It compares the
  encode, scRGB's unit and WHITE now.
* **Checks:** `crt_sim_check` 38 (the row check at four displays, the
  pq1000 crops through HDR10 and scRGB, 10-bit PQ codes, within 1);
  `crt_gpu_check` 84 (the pq1000 crops and the title whole by HDR10 into an
  R10G10B10A2 target and the P16's by scRGB into an RGBA16 float one; the
  flat picture at WHITE 200 in HDR10 at 1080p and 3840x2400 and scRGB at
  3840x2400). `crt_check` 27 of 27; route_out/crt_reference/'s crops and
  checks.txt unchanged (sim_check.txt has the new lines).

**Run here on the mock only** (the stand-in `<SDL3/SDL.h>` from SDL 3.2's
documented declarations, and a mock SDL_GPU running the four shaders as C
from the HLSL; neither packed): `platform_sdl3.c`, `crt_gpu.c` and
`crt_gpu_check.c` compile against the stand-in with no warnings;
`crt_gpu_check` 84 of 84; the game's own `platform_present`:

* 1080p scanlines and grille, SDR: crt_ref's crop byte for byte;
* the P16 (3840x2400, OTHER, shadow, PEAK 1000, WHITE 200), HDR10 and
  scRGB (1.0 at 203 nits), and the PA32UCX's 4K RGB grille, HDR10: within
  one 10-bit code of crt_ref's pq1000 crops;
* HDR switched off and on in the desktop while running: HDR10, SDR, HDR10,
  a `display:` and a `crt:` line each time (PEAK AUTO 330, mask depth
  0.11 at the P16's headroom);
* HDR ON on a window without HDR: SDR, the `crt:` line says so; HDR ON with
  the CRT off: the flat picture through HDR10; `PS2_HDR_SWAPCHAIN=pq` where
  only scRGB is offered: SDR; no override there: scRGB.

**Not compiled or run here:** the HLSL (no shader compiler), SDL itself, a
GPU. For the owner, on the P16 (Vulkan) and the Windows PC (Direct3D 12):

1. `make shaders` (`SHADER_FORMATS=spv` on the P16) -- the across and flat
   shaders changed; the down and vertex shaders did not.
2. `make crt_gpu_check && ./crt_gpu_check` -- `84 checks passed`, else the
   FAIL lines and the line under each.
3. `make && ./ps2` with HDR ON on the P16: the `display:` lines (the
   swapchain), the `crt:` line, the look; `PS2_HDR_SWAPCHAIN=scrgb ./ps2`
   for the other swapchain; HDR switched off and on in the desktop while it
   runs.
4. The same on the PA32UCX (Direct3D 12: HDR10 only).

### Task 3: COMPUTER PLAYS, a wipe-out the waits missed

The owner: the report uploaded with 107 is not the run that sat at the title
after a wipe-out. Read: the driver's loop and its retry, and the waits
under it (play.py's loops and story.py's grinds). Most look for the game
over, SEGA and title screens after a fight and CONTINUE; `Session.ready()`
and `press_through()` did not. At the title `ready()` handed the pad to
`press_through()` up to 40 times, and it pressed C up to 200 times -- the
title waits on Start alone -- so a grind could spend up to 8000 presses at
the title before giving up. Both now return at once on those screens
(`play.WIPED`). `tools/probe_check.py`'s new `ready_at_the_wipe_screens`
(a probe session on the SEGA screen and the title) fails on 107's play.py
(still pressing after five commands) and passes now (none).

Proof that it changes nothing else: seed 3 played to the ending with 107's
tools and with 108's, each in a scratch directory: story.txt identical,
report.txt identical bar its seconds (fights, ticks, levels and tries the
same). Asked again: when a stage is out of tries, should the computer
CONTINUE the last save and hand the pad back in the game?

**Proof** (all three tasks): 90 PASS lines identical to the baseline; the
499 files the routes write identical; the Dark Force trial identical (its
JSON bar `--out`); `crt_check` 27 of 27. `make headless`, `make route_probe`
and `make crt_ref` with no warnings. src/ and src/shaders/ changed: `make`,
and `make shaders` on the owner's machines.

### Build 108a: the flat picture's three HDR FAILs on the P16

The owner's crt_gpu_check run of 108 on the P16: 81 of 84. Every CRT check
passed (107's six slot and shadow FAILs among them, and every HDR crop); the
three FAILs were the flat picture in HDR -- HDR10 at 1080p (worst 6, at
1048,775: GPU 6, CPU 0) and HDR10 and scRGB at 3840x2400 (worst 2, a single
column). Near black: 10-bit PQ code 6 is about 0.0009 nits. The flat pass
took the pixel's place in the step as `px * step / dst - 0.5` in float,
at magnitudes near 1000-3000 where a float's step is ~1e-4; the GPU rounds
that division its own way, and where the CPU landed exactly on a texel beside
black (the blend's fraction exactly 0) the GPU blended a trace of it in. In
8-bit sRGB that is a thirtieth of a code; ST 2084's foot is steep enough to
make it several. 108's mock rounded as the C mirror did, so it could not see
it. Made the mock divide as a GPU may (a reciprocal, then a multiply): 108's
shader and check fail there as on the P16 -- at 1080p the same worst pixel,
codes and box (rows 263-983, columns 375-1544) exactly; at 3840x2400 the
same kind, one column, another one.

The fix (`crt_sharp.frag.hlsl`, and its mirror in crt_gpu_check.c): the
place in whole numbers. With the frame's place and size whole (as
`crt_sharp_layout` makes them), u = nu / (2 w) with nu = (2 (x - dst.x) + 1)
step_w - w, never negative (step_w >= w); the texel is nu / (2 w) and the
blend's fraction the remainder over 2 w -- one division, and a fraction the
CPU has as exactly 0 is 0 on every GPU. The lerps written out as a + f (b -
a). On the GPU-like mock: 84 of 84, and the SDR flat picture closer to its
mirror too (1080p TV: 12 codes one apart; the P16's run of 108 had 6914).
The game's own `platform_present` cases as in 108. The uniform block and
crt_gpu.c are unchanged; nothing the routes build changed (the flat shader
and the check only).

For the owner: `make shaders` again (the flat shader changed), `make
crt_gpu_check && ./crt_gpu_check` -- 84 of 84 expected.

## Build 109: crt_gpu_check's one FAIL, and a preliminary test of Cycfi's Q

Built on 108a with the owner's shaders/ and his crt_gpu_check report
(route_out/crt_gpu_check/report.txt, kept as his). Q came as a second
upload (q.zip: the clone with its infra submodule); it is not packed.

Before any edit (the proof's baseline): 90 PASS lines, checksums of the 499
files the routes write (each identical to the uploaded tree's copy), the
40-fight Dark Force trial (`--policy both --repeats 10`), `crt_check` 27 of
27, `crt_sim_check` 38 of 38.

### Task 1: crt_gpu_check on the P16, 83 of 84

The one FAIL is the flat picture in HDR10 at 1080p, worst 6 at 1048,775
(GPU 6, CPU 0), rows 263-983 and columns 375-1544 -- exactly the FAIL build
108a reproduced on the mock with 108's flat shader, and it is 108's flat
shader that ran. The uploaded `shaders/crt_sharp.frag.spv`, decoded here,
has two floors, three mixes and four clamps (108's float place in the step
and its lerps) where 108a's has integer divisions, mins and written-out
lerps and no floor; `crt_sharp.frag.msl` (SPIRV-Cross's text) reads
`(x * step) / dst.z - 0.5`. All of shaders/ is dated 21:34:40, 108a's
`crt_sharp.frag.hlsl` 21:41:48. Nothing in the code to fix: `make shaders
SHADER_FORMATS=spv` on the P16, then `./crt_gpu_check` (84 of 84 expected).
The four `.dxil` files in shaders/ are 0 bytes (the default formats, where
the P16's shadercross cannot write DXIL); on a Direct3D 12 device the game
would print `refused` and draw through SDL_Renderer, so `make shaders` on
the Windows machine before HDR is tried there.

### Task 2: a preliminary test of Q

docs/audio.md, "The Q test (build 109)", has it all: what the sequencer
hands the chips and where a note-level sink could sit (written before any
design), the build, the demo, what it measured, Q's strengths and
weaknesses, the cost, and a design for a re-voiced backend for the owner's
decision. In short:

* **The build.** `make q_demo`, the tree's only C++, optional: `Q_DIR`
  (`~/GIT/Q` unless given), Q's headers `-isystem`, the three audio C
  files compiled as C11 into `build/q_demo/`; no warnings under `-Wall
  -Wextra -Wpedantic` with g++ 13.3 at `-std=c++20`. With every C++
  compiler on the path replaced by a failing one and `Q_DIR` nowhere,
  `make headless route_probe crt_ref smps_trace fm_demo` build with no
  warnings and nothing calls one; `make -n ps2` names none.
* **The demo** (`tools/q_demo.cpp`): the sequencer unchanged, its writes to
  Nuked-OPN2 and to a Q bank that decodes a shadow of the chip's registers
  into voices -- each carrier a band-limited oscillator through an `svf`,
  the waveform from its modulator's ratio, the filter opened by the
  modulation index through the modulators' envelopes, the chip's envelope
  rates as times; the drums synthesised by what the samples measure as.
  Its C calls have fm.c's shape; C reaches C++ only through `extern "C"`.
  `--solo`, `--instrument`, `--bench`.
* **Measured against the chip** (the title's theme): whole mix +0.4 dB,
  loudness envelopes correlating 0.84; the melody -0.5 dB, 0.96, 96.5% of
  its 50 ms frames within 50 cents; the bass +1.2 dB, 0.82, 96.8%; a chord
  part +0.3 dB, 0.52, 99.3%; the drums 0.0 dB, 0.42. Four more songs within
  +1.0 to +2.2 dB, 0.85-0.94. Three fixes came from those numbers: the
  chord's saw turned over (a rising saw against a square at one pitch
  cancelled: -0.2 to 0.52), the detune step measured on Nuked (the bass's
  beat was twice the chip's), and the attacks at 0.85 of the model (timed
  on Nuked at rates 10-46).
* **The cost**: about 100 ns a voice-sample on this sandbox's 2.1 GHz core
  (0.5% of a core at 48 kHz), 55-60 ns with the filter retuned every 16
  samples; the title's whole bank 365 ns a sample against Nuked's
  1402-1435.
* **Q**: well -- header-only, composes as promised, a stable per-sample
  `svf`, band-limited oscillators on a fixed-point phase, multi-segment
  envelopes; badly -- `adsr_envelope_gen::sustain_level()` sets the wrong
  segment (and the docs show it), the docs call the envelope
  non-retriggerable when the code retriggers, zero-width segments give a
  bad sample, a ramp's curvature is not reachable from a segment, segments
  are shared pointers (copies share state), `noise_gen.hpp` defines a
  non-inline global, and `fast_inverse` puns through `reinterpret_cast`.

Listening files (not packed; FLAC): `phantasy_q.flac` and
`phantasy_opn2.flac` (60 s), `phantasy_fm1_*` (the melody alone, 30 s),
`instrument_64_*` (the bass patch's arpeggio).

Noticed, not changed: `smps.c` still has stacked `case` labels sharing a
body (the two pan-animation switches, the two track-end flags); they go
when a build touches smps.c, proved.

**Proof**: after every edit, 90 PASS lines identical to the baseline; the
499 files the routes write identical; the Dark Force trial's JSON identical
(bar `--out`); `crt_check` 27 of 27, `crt_sim_check` 38 of 38. **Nothing
under src/ changed**: the Makefile (the optional `q_demo` target, `clean`),
`tools/q_demo.cpp`, docs and the README. Nothing to rebuild for the game;
`make q_demo` to hear the test.

### Build 109a: the mix 12 dB up, and a soft knee for the clip

The owner, on 109's finding that the music sat 20 dB under full scale: far too
quiet for a final mix -- do it. Read first: audio.c's render and its gains
(read in full for 109), platform_headless.c's WAV path.

Measured through 109's chain (Nuked, FM bus 0.7, master 0.8), songs 60 s,
effects to their end: the songs peaked at -18.2 to -22.6 dBFS (RMS -33.3 to
-40.6), the loudest effects alone at -15.6, those three every two seconds
over the loudest song (Advanced) at -13.2. docs/audio.md, "The mix's level
(build 109a)".

* `src/audio/mix.h` (new): the mix's gains and `mix_soft_limit`, shared by
  audio.c and tools/q_demo.cpp. The master 0.8 to 3.2 (+12 dB): the worst
  case at -1.1 dBFS, songs peaking at -6 to -11 dBFS.
* audio.c's last stage: the clamp at full scale (commented "soft limit")
  replaced by a soft knee above -3 dBFS -- tanh, slope 1 at the knee, never
  reaching full scale. The worst case comes out at -1.3 dBFS.
* `tools/audio_level_check.py`, `make audio_check`: the headless attract
  sequence's title theme through audio_render, peak -12 to -1 dBFS, none at
  full scale. 109's ps2_headless, built from the uploaded tree: FAIL (-22.0
  dBFS); 109a's: PASS (-10.0, RMS -25.5).
* q_demo's files at the game's new level: the title's theme -10.0 dBFS
  through the chip, -5.3 through Q (Q peakier at equal loudness; noted in
  the backend design). The listening FLACs re-rendered.

Proof: 90 PASS lines identical to 109's baseline, the 499 files identical,
the Dark Force trial's JSON identical (bar `--out`), `crt_check` 27 of 27,
`crt_sim_check` 38 of 38 -- no route renders audio, and the sequencer's
writes are untouched. `make headless`, `route_probe` with no warnings.
**src/ changed**: `make` (and `make q_demo` for the demo).

## Build 110: the synth backends -- the table, the Q backend, the AUDIO page

Built on 109a with the owner's shaders/ and his crt_gpu_check report. Q came as
`q_tar.gz` (the clone with its infra submodule); it is not packed.

Before any edit (the proof's baseline): 90 PASS lines, checksums of the 499
files the routes write (each identical to the uploaded tree's copy), `make
audio_check` (PASS, the title's theme -10.0 dBFS peak, RMS -25.5), the
headless attract sequence's WAV (2400 frames) byte for byte, the 40-fight
Dark Force trial (`--policy both --repeats 10`), `crt_check` 27 of 27,
`crt_sim_check` 38 of 38.

### Task 1: nothing failed

The owner's route_out/crt_gpu_check/report.txt: 84 checks passed, 0 failed
(SDL 3.4.16, Vulkan, SPIR-V) -- 109's one FAIL was the stale flat shader, and
it is gone with `make shaders`. He reported nothing wrong with 109a's louder
mix or its soft knee. Nothing to fix.

### Task 2: the backend table (110)

Read first, in full: fm.c and fm.h, audio.c and audio.h, mix.h.

* `src/audio/synth.[ch]` (new): `SynthBackend`, a table of calls -- create,
  load_drums, reset, write, drum, render, shutdown -- with a name for
  settings.json and a label for the menu. `synth_backend(SYNTH_CHIP)` is
  Nuked-OPN2 through fm.c, each call the fm.c call audio.c made before
  (fm_write, fm_dac_play, fm_render, fm_load_drums, fm_shutdown) on an Fm of
  its own; `SYNTH_Q` is NULL unless the build has Q. create and load_drums are
  in the table because nothing may read assets/ after start-up, so every
  backend loads its drums at launch; reset because a backend switched to
  starts from a reset chip (below).
* `fm_reset` (fm.c, new): OPN2_Reset, the write queue emptied, the DAC
  stopped, the DC blockers and the resampler back where fm_init leaves them;
  the bank and the drums stay loaded. Nothing on the chip's path calls it
  yet.
* audio.c: `Fm fm` and `fm_ready` gave way to `synth_inst[]`, `synth`,
  `synth_id` and `synth_ready`; audio_init writes the DAC-on byte, audio_load_
  music loads the drums, audio_drain hands each FM write and drum byte, and
  audio_render asks for the FM bus, all through the table. Nothing outside
  audio.c used the old fields.

**Proof** (with CHIP, the only entry): 90 PASS lines identical, the 499 files
identical, `make audio_check` identical (-10.0 dBFS, RMS -25.5), the headless
WAV identical byte for byte, the trial's JSON identical (bar `--out`),
`crt_check` 27 of 27, `crt_sim_check` 38 of 38. `make headless`,
`route_probe`, `smps_trace`, `fm_demo`, `pause_probe`, `menu_probe` with no
warnings. **src/ changed**: `make`.

### Build 110a: the Q backend, the AUDIO page, and its routes

Read first, in full: q_demo.cpp (the bank and the demo), sysmenu.[ch] (the
CRT page's code with the rest), main.c, audio_level_check.py; in probe.c
the settings and CRT commands, `tick`, and the host's audio drain.

**The owner's answers** to 109's choices: (1) unsure where the notes should
come from, wanting the trade-offs -- given in chat and in docs/audio.md,
"The Q backend"; the decoder built, as recommended, until he picks hooks
(or the cheaper way found here: the effects told apart after each tick,
smps.c untouched); (2) the AUDIO page; (3) not answered: derived patches
only; (4) examples of both drums: both built, FLACs beside the tarball.

**Task 3, the Q backend** (docs/audio.md, "What was built"):
`src/audio/q/q_synth.[ch]`, q_demo's bank moved out, identical byte for byte
on six renders; the ROM's drums as a second mode (Hermite through a
low-pass at the Z80's rates, +0.8 dB to meet the chip's by RMS); a trim per
drum mode from `q_demo --trim-pass` over all 23 songs (0.760 synthesised,
0.929 samples; the loudest song -6.7 and -5.7 dBFS); the table's entry.
`make Q=1`, `ps2_headless_q`, `make audio_check_q` (Q -11.6 dBFS against
the chip's -10.0: PASS).

**Task 4, the AUDIO page**: the row after CRT, the page (SYNTH, MASTER,
MUSIC; no EFFECTS with the decoder), `"audio"` in settings.json, main.c's
`apply_audio`, `PS2_SYNTH`. audio.c creates every backend at start-up,
keeps the register shadow, and switches on the audio thread with a replay.
`fm_reset` was changed on the way: zeroing the DC blockers put the chip's
DC back as a -34 dBFS thump at every switch; they now run on. Scratch runs
of the headless game (a scratch directory, assets/ and tools/ linked):
no file, a file from before 110, Q asked for without it, odd values ("banjo",
+7, -45: the chip, 0 dB, OFF), the page driven by PS2_PAD writing
`"synth": "q", "master": -2`, and a switch to Q mid-theme with no gap in
25 ms frames either side. ASan and UBSan over the Q backend through three
and four live switches in each drum mode: nothing.

**Task 5, the routes**: `audio_menu` and `audio_switch` (docs/harness.md),
with the probe's `expect audio`, `synth`, `sound render`, `expect sound
playing|silent`, `expect sound-writes`. Both fail on 110's probe (unknown
commands); `audio_switch` also fails with the replay's key-ons left out
(-48.6 dBFS twelve ticks after the switch), and its write hash is the same
with and without its two switches. `escape_menu_computer` one `press down`
more: COMPUTER PLAYS is a row lower.

**Checked**: 92 PASS lines -- the 90 as before bar escape_menu_computer's
(32 commands, 415 ticks), and the two new; of the 499 files, 11 changed: the
nine shots of the Escape menu's main page, each differing only inside the
window's rectangle (a row taller: 32,32 to 288,192), and escape_menu_
computer's log and summary. The Dark Force trial identical (bar `--out`);
`make audio_check` the same numbers (its line now says "on the chip"); the
headless WAV identical byte for byte; `crt_check` 27 of 27, `crt_sim_check`
38 of 38. With every C++ compiler on the path failing and Q_DIR nowhere,
`make headless route_probe smps_trace fm_demo` build with no warnings and
call none; `make -n ps2` names none; `make headless Q=1` without Q stops
with the message; with it, builds and plays Q. Not compiled here: `ps2`
itself (no SDL3; platform_sdl3.c did not change, and main.c and sysmenu.c
are in every headless build), and `ps2` with `Q=1`.

**src/ changed**: `make` (or `make Q=1` for Q, `make clean` first).

### Build 110b: Q's synthesised drums re-voiced

The owner, on 110a's listening files: the kick has far more low bass than the
chip's, the snare and hi-hat are far quieter (the decay too fast?); a kick's
sub bass is good but wants gated noise for its attack. docs/audio.md, "The
drums re-voiced", has the measurements and the table. In short: the drum
voice's gated noise layer (`click_level`, `click_ms`, `click_hz`), the
table fitted to each ROM drum through the chip in three passes with
`tools/drum_hits.cpp` (new, `make drum_hits`), the synthesised mode's trim
0.760 to 0.798 from the trim pass, and `make audio_check_q` held to the
chip's peak (Q -12.7 against -10.0: PASS; the absolute window's floor would
have failed it by 0.7 dB, the title being the song where Q strays furthest).
ASan and UBSan over every drum: nothing in Q's code.

Only src/audio/q/q_synth.cpp changed under src/ (and tools/, the Makefile,
docs): no build without Q links it, so the routes, the trial, `make
audio_check` and the default headless WAV are 110a's by construction.
**src/ changed**: `make clean && make Q=1` (plain `make` is 110a's game).

## Build 111: the hooks, three sounds described, the PM voicing, the toms

Built on 110b's upload with the owner's shaders/ and Q (`q_tar.gz`, not packed).
docs/audio.md, "Build 111", has the design and every measurement.

Before any edit (the proof's baseline): 92 PASS lines, checksums of the 507
files the routes write (identical to the upload's bar audio_switch's log,
whose uploaded copy came from an ASan run and carries Nuked's shift warning),
`make audio_check` (-10.0 dBFS, RMS -25.5), the headless attract WAV (2400
frames) byte for byte, the Dark Force trial (`--policy both --repeats 10`),
`crt_check` 27 of 27, `crt_sim_check` 38 of 38; `make audio_check_q` (Q -12.7).

**Task 1.** the owner's checks of 110b reported nothing failing but the tom
rolls in Secret Ways, and after 111's first report: the tom wants a higher
voicing, a higher pitch at the strike falling lower. Measured and re-voiced
(docs/audio.md, "The toms").

**Task 2** (read first in ps2.asm: DoModEnv and its four tables, cfF4,
FinishTrkUpdate, SendFMFreq, SendFM3Freqs, cfFE, cfFC, DoPitchSlide, the write
routines, BuildingMusicPtrs, SndDrv_ReadPtr): the vibrato envelope, Data
Memory's song (Step Up) and four battle effects, described and measured with
the new `tools/fm_probe.cpp`. Found on the way: `assets/tables/
FMInstruments.bin` is one off from instrument 71 (its extractor stopped at a
label inside two instruments and dropped them); the game never reads it, the
tools that do (fm_demo, q_demo --instrument) are affected. tools/
extract_tables.py fixed (105 instruments, every one equal to the sequencer's);
the asset is not shipped regenerated: `make extract-tables`.

**Task 3.** the owner's answers: (1) PM, as hardcore as it can without burning
the CPU; (2) the hooks; (3) the synthesised drums; (4) the patch file low
priority; (5) parallel voices. Built:

* smps.c: the hooks (`track_write`, each write by its track with the track's
  own channel, `SMPS_TRACK_DRIVER`, `SMPS_HOOK_CLAIM`); the stacked case labels
  split -- the track end (cfE1, cfF2 and the pause flag's fall into it) into
  `cf_track_end`, the pan animation's starts and steps into
  `pan_start_left_only`, `pan_start_right_only`, `pan_settle_both`,
  `pan_step_across`, one case a line.
* audio.c, synth.[ch], fm.[ch]: `CMD_TRACK`, the tracks' shadow and its
  replay, `track_write` and a render with music and effects gains in the
  table, the chip's effect marks through its write queue and per-clock
  weights, EFFECTS.
* sysmenu.[ch], main.c: EFFECTS on the AUDIO page, `"effects"`, the start-up
  line. probe.[ch]: `sound play ID`, `expect audio effects`, the page's
  volumes handed to the mixer when the probe renders sound.
* src/audio/q/: the virtual channels and the hooks, the PM voicing (the
  default), parallel voices, the toms, trims 0.865 / 1.031.
* tools: fm_probe.cpp (new: --note, --click, --alias, --song, --sfx,
  --sfx-list, --steal, --drum), q_demo.cpp (the hooks, --voicing),
  audio_level_check.py (Q held by its loudest 400 ms), extract_tables.py.

A fault found on the way: the first cut took a track's channel from its first
write, and at the SEGA screen the fade writes every empty track's volume to
FM1, so FM2 and FM3's music was thereafter taken for writes to another
channel and dropped -- `audio_check_q` failed 4.9 dB under. The hooks now carry
the track's own channel.

**Proof** (the C side, with the hooks set and Q not built): 92 PASS lines
identical and the new route's; of the 507 files, identical bar the AUDIO
page's three shots (the EFFECTS row); `make audio_check` identical; the
headless WAV identical byte for byte; the trial identical; `crt_check` 27,
`crt_sim_check` 38; the sequencer's register writes identical song by song
(smps_trace --regs against 110b's). With every C++ compiler failing and no Q,
`make headless route_probe smps_trace fm_demo` build with no warnings; `make
-n ps2` names no C++.

**Route** `audio_effects` (93 routes): fails on 110b's probe (no `sound play`,
no EFFECTS row; with those stubbed, MUSIC OFF leaves the mix at -17.3 dBFS),
passes on 111.

**Q's checks**: `make audio_check_q` PASS (loudest 400 ms +0.0 dB against the
chip; subtractive -1.1); ASan and UBSan over fm_probe's song, effect, steal,
drum and click cases at 4 x and 2 x, parallel on and off: nothing in Q's code
(Nuked's shift, as before).

**Not compiled or run here:** `ps2` with or without `Q=1` (no SDL3). src/
changed: `make` (or `make clean && make Q=1`); `make extract-tables` for the
instrument file.

Listening, beside the tarball: `data_memory_step_up_{chip,q_pm}_111.flac`,
`battle_rise_or_fall_{chip,q_pm}_111.flac` (eleven effects over the battle
music), `secret_ways_{chip,q_pm}_111.flac`.

### Build 111a: OVERSAMPLE on the AUDIO page

The owner liked 111's listening files and asked for a row for the oversampling.
docs/audio.md, "Build 111a". Changed: synth.[ch] (`set_oversample` in the
table), audio.[ch] (`CMD_OVERSAMPLE`, `audio_set_oversample`), q_synth.cpp
(the rate set while notes sound: each voice's steps again), sysmenu.[ch] (the
row, `"oversample"`), main.c (the page to the mixer, `PS2_Q_OVERSAMPLE`, the
start-up line), probe.c (`expect audio oversample`), audio_level_check.py (Q
checked at 4).

Route `audio_oversample` (94 routes): fails on 110b's probe (no such row),
passes. Proof: 92 PASS lines as before, audio_effects' and the new route's;
of the 507 files, identical bar the AUDIO page's three shots (two rows
taller now); `make audio_check`, the headless WAV byte for byte, the trial
identical; crt 27 and 38. Q: `make audio_check_q` PASS (+0.0 dB); the rate
switched every 2 s through the title's theme, the largest level step 0.55 dB,
sanitizers clean; settings.json read (2), overridden (1), an odd value (3)
read as 4. **src/ changed**: `make` (or `make clean && make Q=1`).

### Build 111b: the level-up and the heal

The owner heard the level-up and the hospital's heal come out weird on Q. Read
SFX_LevelUp and SFX_Healed first; docs/audio.md, "Build 111b". Fixed in
src/audio/q/ alone: parallel tails fade out in 15 ms (they rang their whole
release, stacking notes the chip never has), and Q takes its writes at the
chip's pace (one every two native samples, as fm.c's queue), so unisons no
longer add whole. New: `PS2_Q_EFFECTS=take`. Trims 0.804 and 0.947.

Only q_synth.[ch] under src/ (and fm_probe.cpp, q_demo.cpp): no build without
Q links them, so the routes, the trial, `make audio_check` and the headless WAV
are 111a's by construction. Q's checks: `make audio_check_q` PASS (-0.3 dB);
the rate switched mid-song (largest step 0.37 dB); ASan and UBSan clean.
**src/ changed**: `make clean && make Q=1`.

### Build 111c: the level-up's tail

The owner: the level-up still sustained in 111b's files. The effect's voice kept
its own slow release after the effect ended, where the chip's channel takes
the music's instrument back (RestoreBGMChn) and the music's next strike ends
the note. q_synth.cpp: the music's writes that reach the chip on a channel an
effect's note still sounds on go to that note's voice too; a music strike
there fades it (docs/audio.md, "Build 111c"). Only Q's file under src/:
`make clean && make Q=1`.

## Build 112: VOICES, TAILS and the patch file; Step Up's chord is the chip's grain

Built on 111c's upload with the owner's shaders/ and Q (`q_tar.gz`, not packed).
docs/audio.md, "Build 112", has the design and every measurement.

Before any edit (the proof's baseline): 94 PASS lines, checksums of the 513
files the routes write (identical to the upload's bar audio_switch's ASan
log, as in 111), `make audio_check` (-10.0 dBFS, RMS -25.5), the headless
attract WAV (2400 frames) byte for byte, the Dark Force trial (`--policy both
--repeats 10`), `crt_check` 27 of 27, `crt_sim_check` 38 of 38; `make
audio_check_q` (Q -0.3 dB against the chip).

**Task 1.** the owner's checks of 111c reported nothing failing.

**Task 2, Step Up's chord instrument (74).** Read: its 26 bytes (algorithm 0,
four operators in series, operator 3 attacking at rate 1 into a carrier at 2
x, sustain level 15). `fm_probe --note 74`: the chip's tail carries the
carrier's odd multiples (6, 10, 14 ... 38 x the pitch at 0.3 s, 30-45 dB under
it) and at 1.0 s sits 20 dB above Q's. Read in Nuked (OPN2_ChOutput and the
channel accumulator): each carrier enters the 9-bit sum truncated, and the
YM2612's DAC adds +4 steps on the positive side and -3 on the negative -- a
jump of 7 at zero, which a quiet note turns into a square wave that stops
decaying. Put into Q as a diagnostic (`PS2_Q_GRAIN=1`): 74's first 300 ms
4.6 dB apart becomes 0.5, the tail's partials match the chip's within about
1 dB each, the whole note 11.7 becomes 6.7 (the rest is the deepest tail,
where the chip's envelope steps below its last bit); 72 0.9 to 0.4, 66 3.0 to
2.0. **It is the chip's grain**, which the PM voicing leaves out on purpose.
Off by default; a GRAIN row if the owner wants it.

**Task 3.** the owner's answers: (1) keep the extra voices, a row for limited
voices; (2) he loves the ring (111a's tails); (3) yes to a patch override
file. Built:

* smps.[ch]: `SMPS_HOOK_INSTRUMENT` -- the hooks say which instrument a
  track uploads (cfEF_SetFMIns and RestoreBGMChn, both read), before
  SetFMIns's 26 writes; `smps_instrument` (the bank read as cfEF reads it),
  `smps_ins_regs` public, `SMPS_INSTRUMENTS` 105.
* synth.[ch]: `SynthOption` (VOICES, TAILS, PATCHES), `SynthPatch`, the table's
  `set_option` and `set_patches` (NULL for the chip).
* audio.[ch]: `CMD_SYNTH_OPTION`, `audio_set_synth_option`, the tracks'
  shadow keeps each track's instrument and a switch replays it first; the
  patch file read at start-up and handed to every backend that takes it.
* src/audio/patches.[ch] (new): the patch file's reader.
* src/audio/q/: VOICES (111b's take-channels as a switch), TAILS (RING: a
  tail rings its release, at most 12 at once, the quietest faded to make
  room), the patch rules, `fine_cents`, `volume_db`, a tail per instrument,
  `PS2_Q_TAILS`, `PS2_Q_PATCHES`, `PS2_Q_GRAIN`.
* sysmenu.[ch], main.c, probe.c: the rows, `"voices"`, `"tails"`, `"patches"`
  in settings.json, the start-up line, `expect audio voices|tails|patches`.
* assets/music/q_patches.json (new, in the manifest): instrument 70 --
  Advanced's chord drone -- set to ring; instrument 74 written out whole, off,
  as an example. The manifest also gained sprite_noah_f1_object_28.png, which
  was in assets/ and not listed (no route reads it).
* tools: q_patch_template.py and patches_check.c with patches_test.json
  (new), fm_probe.cpp (`--patches`, `--music-only`), the Makefile
  (`patches_check`, fm_probe's objects).

**Proof** (the C side, Q not built): the 94 PASS lines identical; of the 513
files, identical bar five AUDIO page shots (three rows taller: audio_effects'
one, audio_menu's three, audio_oversample's one); `make audio_check`
identical; the headless WAV identical byte for byte; the trial identical;
`crt_check` 27, `crt_sim_check` 38; the sequencer's register writes identical
song by song (smps_trace --regs, all 23 songs, 60 s each). With `CXX=false`
and no Q, `make headless route_probe patches_check` build with no warnings;
`make -n ps2` names no C++.

**Routes** `audio_voices`, `audio_tails`, `audio_patches` (97 routes): each
fails on 111c's probe (no such row) and passes. **`make patches_check`**: 15
checks of the reader on its fixture, PASS; fails on 111c (no reader).

**Q's checks**: `make audio_check_q` PASS (-0.3 dB, unchanged: songs play the
same with the file's one patch off their path, and q_demo's trim pass does not
read the patch file, so the trims stand); ASan and UBSan over Advanced with
the patch file, Gaj over the battle music, RING, LIMITED, GRAIN, 1X: nothing
in Q's code (Nuked's shift, as before).

**Not compiled or run here:** `ps2` with or without `Q=1` (no SDL3). **src/
changed**: `make`, and `make clean && make Q=1`. Nothing to extract.

Listening, beside the tarball (FLAC): `victory_level_up_{chip,q}_112`,
`advanced_{chip,q_fade,q_patchfile_ring}_112` (25-55 s, instrument 70 from
about 30 s), `battle_gaj_{chip,q_voices_extra,q_voices_limited}_112`,
`step_up_{chip,q_grain}_112`.

### Build 112a: the mouse cursor hidden in fullscreen

The owner: the game needs the mouse cursor hidden in fullscreen. The game has no
use for the mouse. platform_sdl3.c: `follow_cursor` hides it
(SDL_HideCursor) whenever the window is fullscreen and shows it again in a
window -- from the menu's FULLSCREEN, a start in fullscreen from
settings.json (main.c calls platform_set_fullscreen at start-up), and the
window system's own switches (SDL_EVENT_WINDOW_ENTER_FULLSCREEN / LEAVE).
**Not compiled here** (no SDL3): check on the P16 and on Windows -- FULLSCREEN
on and off from the menu, a start with `"fullscreen": true`, and the desktop's
own fullscreen shortcut. Nothing the routes, the WAV or the trial reach
(platform_sdl3.c is only in `ps2`). **src/ changed**: `make` (or `make clean
&& make Q=1`).

### Build 112b: the attract sequence plays on under the Escape menu

The owner: the menu doesn't stop the music, so over the title the music came
out of step with the title's crawl. game.c's tick_screens: where nobody
plays -- the SEGA logo, the title and its dissolves, the attract crawl across
Motavia (the level screen before any walk), the ending -- the screen now
runs on under the menu with the pad withheld, in time with its music; the
title runs out its wait and moves on to the crawl exactly as if nobody
touched it. Everything the player drives stays frozen under the menu, as
before, and COMPUTER PLAYS's held game is untouched (main.c never ticks it
while the menu is open).

Route `title_under_menu` (98 routes): the menu opened 60 ticks into the
title, the figures dissolving in behind it, the crawl under it 2400 ticks on,
and still the crawl when it closes; fails on 112's probe (still the title).
Proof against 112's run: the 97 PASS lines identical (commands and ticks);
every log identical; five shots changed, each the title's dissolve going on
behind the AUDIO page (54 pixels at the page's edges in audio_effects' and
audio_oversample's; the three new rows' shots the same way); the attract
WAV identical byte for byte (the headless run never opens the menu).
**src/ changed**: `make`.

## Build 113: the Windows VM -- set-up, the port, the proofs here

The first sessions on the owner's Windows 10 VM, Claude Code run in the tree
(handover_113_windows.txt: his answers and a task list, now in
NOTES_next_context.md). Tasks 0 and 1; the tree moved off OneDrive between
the two sessions.

**Set-up (task 0).** The tree was no repository: `git init`, a .gitignore
(the build products under both names and `*.exe`, build/, __pycache__,
saves/, route_out/*/saves/, listen/, *.flac), and the upload committed as it
was (6718fd2) before any edit, so everything since is a diff. CLAUDE.md's
"Machine" has the VM: 12 cores; the RTX 5070 Laptop GPU (and the VM's QXL
adapter, unused); a 3840x2160 60 Hz display that looks like a dummy plug,
seen through Looking Glass, so HDR, PEAK and pacing checks here are not the
real thing (Windows had HDR off for it at the set-up and on later: the
plug's EDID says 1499 nits); MSYS2 UCRT64 (gcc and g++ 16.2.0, make 4.4.1,
SDL3 3.4.16) and its Python 3.14.7 with Pillow; no WSL distribution. Claude
Code's Bash tool is Git Bash, whose msys runtime cannot run MSYS2's make
(every recipe exits 127), so builds go through MSYS2's own shell: `MSYSTEM=UCRT64
CHERE_INVOKING=1 /c/msys64/usr/bin/bash.exe -lc 'cd "<tree>" && make ...'`.
`make`, `make headless` and `make Q=1` build with no warnings (Q_DIR by its
8.3 name: the Makefile does not quote it, and the path has a space).

**The DXIL.** the owner's shaders/*.dxil were 0 bytes (the P16 cannot write
DXIL, build 109), so Direct3D 12 refused them and the game drew through the
SDL_Renderer placeholder unless told `SDL_GPU_DRIVER=vulkan`. MSYS2 packages
neither SDL_shadercross nor DXC. First, Microsoft's DXC release (1.9.2607:
dxc.exe with dxcompiler.dll and dxil.dll beside it, so the DXIL is signed)
compiled the HLSL as it stands -- the shaders name SDL_GPU's register spaces
themselves -- through a new `make shaders_dxc DXC=/path/to/dxc.exe`. Then
The owner asked for shadercross itself: built from its source (commit 1ff05be)
against MSYS2's SPIRV-Cross and that DXC release, installed into MSYS2's
/usr/local/bin with DXC's two DLLs beside it, so `make shaders` runs here
(README, "Build"). It writes the owner's four .spv byte for byte, and three of
his four .msl; the fourth, crt_sharp.frag.msl, is his own from before 108a's
HLSL (build 108's float place; his .spv were rebuilt alone in 109) and was
left as it is. shaders/*.dxil are shadercross's (`make shaders
SHADER_FORMATS=dxil`); its DXIL and DXC's differ in their bytes (shadercross
hands DXC the source with `-E` and `-T` alone, dxc.exe reads the file) and
in a few codes of two 4K HDR crops, and both pass crt_gpu_check 84 of 84 on
Direct3D 12 (`SDL_GPU direct3d12, DXIL shaders`); on Vulkan (SPIR-V) 84 of
84 too. Plain ps2.exe starts on Direct3D 12 with the CRT chain.

**The port (task 1; the owner's answer: the tests run here).** Everything under
`_WIN32` or `os.name == "nt"` or the same on every OS; nothing changes on
Linux.
* route_probe.c: mkdir takes the path alone (`_mkdir`); the stderr capture's
  file is opened O_BINARY and read back through a second descriptor of its
  own where Linux uses pread (fd 2 shares the first one's offset, so a seek
  there would move where the game writes), and closed before it is removed
  (Windows cannot remove an open file); clear_previous never removes a cut
  path (MinGW's d_name is 260 bytes, glibc's 256: -Wformat-truncation);
  log.txt and summary.txt written "wb".
* src/harness/probe.c, found here: `copy_file` (build 91) skipped the copy
  when the two files had one device and inode, and Windows has no inode --
  st_ino is 0 for every file -- so any two files on one drive were "the
  same": a session's second `start slot 1 FILE` CONTINUEd the first save
  again (Zosa again, where the Biosystems Lab's checkpoint was asked for). It now
  reads the whole of FILE before it opens the slot, which is what build 91
  wanted, on every OS, with no identity test. tools/probe_check.py:
  `continue_another_file`, FAIL on 112b's probe here (the second CONTINUE on
  Dezolis again), ok on 113's; `continue_own_slot`, build 91's, still ok
  (the session's own slot read whole and written back, 19243 bytes).
  play.continue_game, story.py's retries and playthrough's stage retries
  all CONTINUE more than once a session.
* src/game/gamedata.c: the save written "wb", as settings.json already was.
  Text mode writes "\r\n" on Windows, and story.py and walk_trials copy the
  game's saves into the tracked checkpoints (routes/saves/story_<stage>.json).
* crt_ref.c and crt_gpu_check.c: mkdir; their reports "wb".
* patches_check.c: the one-entry fixture in %TEMP% ("/tmp" is C:/tmp on
  Windows, which is not there), and a fixture it cannot write now FAILs
  where it left its four checks out without a word (10 checks ran here).
  It is 14 checks: build 112 counted 15.
* tools/*.py: every text file written with newline="\n" (62 places; Windows
  text mode writes "\r\n"). `make extract` in a scratch copy reproduces 906
  of the 907 assets byte for byte; the 907th, battle.json, is equal as data,
  its key order following PYTHONHASHSEED on every OS (three seeds, three
  files; queued).
* gen_script_ids.py: its replacement map in the temporary directory on
  Windows. playthrough.py's pin_hash_seed: Windows has no exec -- os.execve
  there starts a new process and ends this one at once, so whatever waits
  sees it exit while the run goes on (MSYS2's Python crashed) -- so the run
  is a child, waited on, its exit status passed on.
* q_synth.cpp, fm_probe.cpp, drum_hits.cpp: std::numbers::pi (C++20), where
  MinGW has no M_PI.

The harness on Windows, in short runs: playthrough seed 1 and story.py
each to Arima (13 and 12 s), walk_trials (two runs), fuzz_levels over all
101 levels (`--probe ./route_probe`), boss_trials (the proof's), probe_check.
`route_probe_asan` does not build: MinGW's GCC has no ASan or UBSan runtime.

**Proofs, on this machine.** The baseline, once the C tools built and ran
here (route_probe's capture, the mkdirs, patches_check's fixture) and before
the rest: 98 PASS lines, checksums of the 526 files the routes write, `make
audio_check` (-10.0 dBFS, RMS -25.5), the headless attract WAV (2400
frames), the Dark Force trial (`--policy both --repeats 10`), crt_check 27,
crt_sim_check 38, patches_check 14; taken twice, identical. After the
Python and C++ changes: all identical, the trial's two files bar their line
ends (written "\r\n" before). After the move and 113's "wb" and copy_file:
the 98 PASS lines identical; of the 526 files, every PNG and save identical,
and the 196 logs and summaries each the old one with its "\r" taken out (the
old probe's run, normalised, against the new, file for file); the WAV, the
trial, crt 27 and 38, patches 14 identical; crt_ref's two reports and
crt_gpu_check's likewise. A save made with `save-here` by 113's probe is
112b's with its "\r" taken out, and CONTINUEs. The routes' files the same
run 12 at a time and one at a time. Q (qproof: `make audio_check_q`, the
attract WAV on Q, `q_demo --trim-pass`, drum_hits, five fm_probe runs):
against a baseline built with `-D_USE_MATH_DEFINES`, 25 of 26 files
identical, the 26th (the attract log) only by its own path; audio_check_q
-0.3 dB as before.

Against Linux: the same 98 routes and 526 files, audio_check's numbers, and
the Dark Force trial's 40 fights identical to build 97b's Linux run
(route_out/bt97b_after.json: every result, boss HP, survivor count and tick
count; 9 of 40 won) -- the game runs the same under MinGW as under Linux's
gcc. Files are not compared byte for byte across machines (CLAUDE.md,
"Proofs").

**Timings.** On OneDrive the routes took 31 s 12 at a time and 29 s one at a
time on their first run (about 8-10 s at 12 on later runs). Moved off it
(Defender excludes the tree; Windows Search off): 6.7 s at 12 jobs, 20.6 s
one at a time -- the same in a directory Defender scans, so OneDrive's
folder was the cost, not the scanning. `make -j12 all headless` 17 s either
way (one compiler run a target); the whole proof set 60 s, a quarter of it
route_probe's compile and a quarter the CRT checks, which task 2 can run
beside the routes.

**Not done here.** WSL (none installed) and a Linux host run (The owner's
answer 4 made both unneeded); route_probe_asan on Windows (his choice); the
window, the `display:` and `audio:` lines by his eyes (the startup lines
were read here).
**src/ changed**: `make` (gamedata.c, probe.c), and `make clean && make Q=1`
(q_synth.cpp). shaders/*.dxil changed; the .spv and .msl did not.

### Build 113a: battle.json the same every time, the Makefile's paths quoted

What 113 found and left.

**battle.json.** extract_battle.py built two dicts -- the animation
tables' scripts, loc_2601A's and every table read_anim_table reads -- by
walking `set(offsets)`, and a set of strings is ordered by the strings'
hashes, which PYTHONHASHSEED changes from run to run: three seeds gave three
battle.json, equal as data, their keys in three orders. They are walked in
the index's order now (`dict.fromkeys`), the same under any seed, and
battle.json is rewritten once in that order: equal as data to the old one,
the same size, 7217 lines moved. The game reads each script by its label
(battle.c's read_anim_table, `json_get(scripts, label)`), never by place.

**play.py.** Its `if __name__ == "__main__"` block stood at line 492, above
the four and a half thousand lines it calls into, so `python3 tools/play.py`
stopped at `PASEO` undefined on every OS. It is at the end now and plays
the new game route to Paseo (tick 5813, as the route).

**The Makefile.** The recipes quote the paths they are given -- Q_DIR in
Q_INCLUDES, PS2_ASM, PS2_CONST, ART_DIR and PCM_ASM in every extractor's
line, SHADERCROSS and DXC -- so a path with a space arrives as one argument.
`make ps2_headless_q Q_DIR="$HOME/GIT/Q"` fails on 113's
Makefile (q/support/literals.hpp not found: the -isystem split at the
space) and builds on 113a's. The constants file's default lookup beside
PS2_ASM still splits at a space, as make's functions do: with a space in
that path, name PS2_CONST too (the Makefile says so). `make clean` also
removes each binary's `.exe` and patches_check, which build 112 never added
to it; .gitignore gains /patches_check.

**Proof.** extract_battle.py under PYTHONHASHSEED 1, 2 and 3: one file.
`make extract` in a scratch copy, handed the disassembly's absolute path
(which has a space): 906 of the 907 assets byte for byte, the 907th the new
battle.json (the drums skipped: the disassembly has no sound/ PCM file, as
in 113). Then every proof file identical to 113's (the proof set at 12
jobs against 113's taken one at a time): 98 PASS lines, the 526 route
files, audio_check, the attract WAV, the Dark Force trial, crt 27, sim 38,
patches 14, probe_check 3. A Q build, `make shaders` and `make clean` run
from a scratch copy with the new Makefile. **Nothing under src/ changed**
(assets/data/battle.json did: the game reads it the same).

## Build 114: parallel runners, and the owner's answers

Task 2 of handover_113_windows.txt, and the owner's answers to
questions_113.txt (asked and answered in the session; the file is kept).

**Checked first: what two runs could share.** A probe session writes only
route_out/<name>/ (its log, its stderr capture, summary.txt, its saves), so
two runs at once need two names. boss_trials played every fight in one
session named `boss_trials`; walk_trials opened a fresh session a run, but
every one named `walk_trials_<walk>`; fuzz_levels gives each run its own
route file and output directory already; playthrough keeps a seed's all --
its session, checkpoints, saves on the way, reports -- under
route_out/playthrough_s<seed>/. Nothing else a trial's run calls writes a
file. play.py's module state that a run could leave for the next in the
same process: PIN_HEAD and HELD are set only by story.py's stage functions,
which no trial's run calls, and the map caches are keyed by level and the
items held.

**make.** Each route is a target (`route-<name>`), so `make -j12 routes`
runs twelve at once: 5.6 s here, where one at a time takes 21. As the loop
did, every route runs whatever the others do (the recipe's failure is
ignored), and `routes` then fails if any route's summary.txt is missing or
does not say `exit 0` -- each run removes its summary.txt first, so a route
that dies before writing one (a parse error) fails rather than passing on
its last run's. Checked with a route that fails and one that does not parse,
in a scratch copy: the other 98 pass, both are named at the end, make exits
non-zero. The final check is one `grep -L` over every summary: 98 greps in a
row cost 2.5 s under MSYS2, where starting a process is slow.

**tools/jobs.py** (new): `ordered(fn, tasks, jobs)` runs the tasks in spawned
worker processes -- fresh on every OS, with nothing of the parent's state
(walk_trials --setup's PIN_HEAD stays in the parent) -- and yields their
results in the tasks' order; each worker has an index (`worker()`) for a
session of its own; the workers' PYTHONHASHSEED is pinned (0 unless set, as
playthrough.py's, whose docstring names tie-breaks over sets of names). A
task's exception comes back where its result would have, and the tasks not
yet started are dropped.

**The trials.**
* boss_trials.py `--jobs N`. One session cannot be split: each fight
  started where the fight before had left the RNG, so fight k depended on
  every fight before it. Now each fight is a fresh probe -- power-on, the
  CONTINUE, then `wait ticks` of its repeat's offset (`--step`, 7), the same
  for every triple, policy and arrival, walk_trials' way since build 97 --
  in the worker's session route_out/boss_trials/w<N>. A new draw of the
  trial (Rudo+Amy+Anna 6/10 where it was 5/10, Rudo+Amy+Kain 2/10, Rudo+Anna+
  Kain 1/10, Amy+Anna+Kain 5/10: 14 of 40 where it was 9), and the policies
  now meet the same draws. 2.9 s at 12 jobs, 15.3 s at one (a probe started
  a fight), 10.5 s for the old session. `--chain` draws the old way: every
  line of its output, the table included, as 113a's.
* walk_trials.py `--jobs N`: its runs in workers, each in the worker's
  session (route_out/walk_trials_<walk>/w<N>). Two policies at the first
  seven offsets: 2.4 s at 12 jobs, 7.1 at one, the same results. Ten of
  those fourteen equal the tracked Linux runs of build 97's first round
  (route_out/walk_trials_dark_force.json); the four others differ in their
  fights, the harness having changed since 97 -- not the hash seed (seeds 0,
  1 and 2 give identical runs) and not Windows (build 113's Dark Force trial
  matched Linux's run fight for fight).
* fuzz_levels.py `--jobs N`: every route written first, the probes started
  from threads, the logs read in the old order: seeds 1-2 over all 101
  levels at 4000 ticks, the whole report byte for byte the same under 113a's
  code, at one job and at 12 (3.8 s against 28).
* playthrough.py `--seeds 1,2,5-8 --jobs N`: each seed a child process run
  as `--seed N` with the rest of the command line, under its own
  directory; its report's first line printed as it ends. Seeds 1 and 2 to
  Arima together and each alone: story.txt and the checkpoints identical,
  report.txt bar its seconds; 13.1 s for the two at once against 24.7.

**The owner's answers** (questions_113.txt). (1) The sound plays. (2) His
checks of 112, 112a and 112b all passed. (3) Sanitizers: whatever is best
here -- next build. (4) route_out/'s route and session directories go in
.gitignore: `/route_out/*/`, less the directories the uploads keep
(computer_last_run, dam_lights, mild_ground, playthrough_s*, story_*), and
/fuzz_out/; the reports beside them stay tracked. (5) shaders/ is written
here now, all of it by `make shaders` (shadercross): the .spv and .dxil came
out identical, and crt_sharp.frag.msl became 108a's (not run: no Metal
here). (6) He asked that the game not be treated as deterministic: it is not
made so -- the port keeps the ROM's GenerateRandomNumber, which starts from
the same constant at every power-on ($2A6D365A for a zero seed) and turns
every frame, so play varies with the player's timing as the original's
does; the harness repeats only because it presses on the same ticks, and a
trial varies its timing to sample. (7) PEAK above what SDL reports works for
him: dropped from the queue. Mid-build he asked for a leaner grind (NOTES,
task 8).

**Proof** (b114, at 12 jobs; b114_serial, at one: every file the same bar
the make log's order): the 98 PASS lines (now each route's summary.txt's
first line: 113a's lines less the "\r" the console added), the 526 route
files, `make audio_check`, the attract WAV, crt_check 27, crt_sim_check 38,
patches_check 14 and probe_check 3, all as 113a's; the trial the new draw
above. The proof set takes 59 s at 12 jobs and 97 s at one. **Nothing under
src/ changed**; shaders/crt_sharp.frag.msl did.

### Build 114a: ASan and UBSan on Windows, and what clang's bounds check found

The owner's answer 3: whatever is best for the sanitized fuzz here. MinGW's
GCC has no ASan or UBSan runtime (build 113). MSYS2's UCRT64 clang has none
either -- its compiler-rt is the builtins, the fuzzer and profiling; the
link asks for libclang_rt.asan_dynamic.dll.a and finds none -- so it was
removed again, its four packages exactly. MSYS2's CLANG64 environment has
both: clang 22.1.8 with compiler-rt (`pacman -S mingw-w64-clang-x86_64-clang
mingw-w64-clang-x86_64-compiler-rt`, 17 packages). A test program with a
signed overflow and a read past a stack array: UBSan reports both, ASan the
stack-buffer-overflow, each with its file and line. `make route_probe_asan`
from the CLANG64 shell (cc is clang there) builds with no warnings at
`-Wall -Wextra -Wpedantic`; the probe needs /clang64/bin on the PATH (the
ASan runtime is libclang_rt.asan_dynamic-x86_64.dll, with libc++.dll), so
fuzz_levels runs from that shell with UCRT64's Python by its full path.

**smps.c's track_byte.** The routes on the sanitized probe all passed, and
91 reports came with them, one kind on every route with music:
`smps.c:88: index 92 out of bounds for type 'u8[48]'` (92, 130, 140, 188).
track_byte addresses the ten tracks' RAM as one block on purpose (the
sequencer's cfF8 gosub with a zero stack byte and the chord-mode loops walk
off a track, as on the hardware), but formed the address as
`s->tracks[0].raw + i`: pointer arithmetic past the first track's 48-byte
array, which C leaves undefined, and which GCC's UBSan does not instrument.
It is `(u8 *)s->tracks + i` now -- the bytes of the whole array, which C
allows -- with a `_Static_assert` that a track is TRACK_STRIDE bytes (the
union of 48 bytes it is); the same byte either way. The other report,
Nuked-OPN2's `ym3438.c:660: left shift of negative value` (7 routes, those
that render sound), is the emulator's own, noted since build 112, and left
alone.

**Proof.** The sequencer's chip writes, every song (smps_trace --regs, all
23, 3600 frames each, 228299 lines): identical before and after. The
routes on the sanitized probe: 98 PASS, only Nuked's shift reported. The
fuzz sweep on it, build 63's (seeds 1-3 at 20000 ticks, all 101 levels,
--jobs 12): 303 runs in 37 s, none failed, no sanitizer report, no stderr
line. The proof set against 114's: every file identical (the attract log
bar its path; ps2_headless rebuilt). Q (qproof, its binaries rebuilt: smps.c
is in them): 25 of 26 files identical, the attract log bar its path;
audio_check_q -0.3 dB. `make ps2` builds with no warnings. **src/
changed**: `make` (smps.c).

## Build 115: COMPUTER PLAYS on Windows

Task 3 of handover_113_windows.txt: COMPUTER PLAYS, which build 105 left a
stub on Windows (POSIX ONLY), runs there now. docs/computer_plays.md, "On
Windows (build 115)", has the design; src/harness/autoplay.c was read in
full first.

**The host.** One autoplay.c for both. What differs is only finding,
starting and stopping the driver, in a section an OS: POSIX's code is the
old code moved into `spawn_driver` and `reap_driver`; Windows's starts the
driver with CreateProcess, hands it the two pipes' ends as inheritable
handles named on its command line (`--attach CMD,LOG`; a Windows child has
no descriptors past 2) and nothing else but its standard handles
(PROC_THREAD_ATTRIBUTE_HANDLE_LIST), its stdout and stderr to driver.log,
no window; 64 KB pipes; its environment the game's with PYTHONHASHSEED=0 in
its sorted place; on a hand-back a second for its report
(WaitForSingleObject), then TerminateProcess. The game's own two ends become
C descriptors (_open_osfhandle), so the command thread, host_log and the
handoff are one code. Python is `python3.exe`, `python.exe` or `py.exe`
(`-3`) on the `;` PATH; a reparse point there (the Store's stand-ins in
WindowsApps) counts only if `-c ""` exits 0, asked once a session. The
driver (tools/computer_plays.py, `attach_fds`) opens the handles with
msvcrt.open_osfhandle; play.py is unchanged.

**The lock.** The first working build played seed 12 from a new game
through Arima (596,726 ticks) in 98 s at FAST, where `playthrough.py --pace
watch` on the probe takes 13.9 s. Timed with a stand-in driver: a tick in a
long wait cost 130 us through the handoff, a command with no tick 66 us.
winpthreads' condition variable is the cause: a broadcast first waits until
the thread it woke last has run (the condition variable's gate, Terekhov's
algorithm), so every tick handed between the frame loop and the command
thread paid a full wake-up. A two-thread ping-pong here: winpthreads 123-138
us a round trip, Windows's SRW lock and condition variable 1.5-2 us. With
Windows's own the run took 37.9 s: a tick 3.6 us in a long wait, but a
command's first tick still about 100 us (a thread that has slept wakes
slowly in this VM) and the ticks of a short command 13 us. So a thread
about to sleep now spins for up to 200 us first, watching an interlocked
count of wake-ups without holding the lock (spinning on the lock itself
could keep the other thread off it): 12.2 s, a tick 1-2.4 us, a command
110-130 us (the probe's 80-220). The three runs are the same game byte for
byte (story log, both checkpoints, last.json; the report bar its seconds).
POSIX keeps pthreads, the calls wrapped one for one. ps2.exe and
ps2_headless.exe now import libwinpthread-1.dll (MSYS2's ucrt64/bin, beside
SDL3.dll); ps2_headless imported no MSYS2 DLL before.

**Tested headless through the window's path** (main.c, the Escape menu,
the reset, the thread, the child; FAST from the SPEED row; scratch
directories in MSYS2's /tmp):

| Run | Result |
|---|---|
| seeds 1, 2 from a new game to Arima | the probe's game exactly: story logs identical but for the checkpoints' directory, all four checkpoints byte for byte |
| seed 12 to Arima | the probe's game up to its first wipe-out (tick 16,085, the tick Linux recorded in 102b); they part at the CONTINUE, by design |
| hand-backs: seeds 1, 2, 3, 5, 6 at frames 600-850, seed 8 at 750 | each "handed back (EOFError)", the report written, the game on under the pad |
| the menu opened and shut mid-run, the lamp lit (seed 1) | plays on; the game byte for byte the one without it |
| QUIT mid-run | the report written, the game ends |
| SLOT 1 holding the story's `paseo` checkpoint, seed 7 | arima from there, through; the slot byte for byte as before |
| LAST RUN after seed 6's hand-back | its save on the way in arima, through |
| `--fail-once arima` (seed 1) | try 2 CONTINUEd its save on the way through the title, through |
| NORMAL, seed 1 to paseo | one tick a frame (14,392 ticks, 15,044 frames); the same checkpoint and story as FAST |
| no Python on the PATH | `no python3.exe, python.exe or py.exe on the PATH`; nothing starts |
| no tools/ | `tools/computer_plays.py is missing`; nothing starts |
| a driver that ignores the hand-back | terminated after the second; no Python left running |

A scratch unit test (autoplay.c included whole) of the Windows helpers: 17
arguments -- spaces, quotes, tabs, runs of backslashes, a trailing
backslash, empty -- quoted and read back exactly by CommandLineToArgvW; the
Python search on five PATHs (none, a missing directory first, quoted,
trailing backslash with an empty entry, forward slashes); the environment
block with PYTHONHASHSEED unset, 5 and 0 (one entry, =0, in its sorted
place, the rest untouched); python3.exe answers as a Python, where.exe and a
missing file do not, and a path is asked once. Not run: `py.exe` (none
here), a reparse-point Python end to end (making a symbolic link needs
administrator rights here), and the SDL3 window.

**POSIX.** Not compiled here (no POSIX toolchain). What a POSIX compiler
sees of autoplay.c and autoplay.h -- the `_WIN32` branches taken out --
against 114a's: the spawn and reap code moved verbatim into `spawn_driver`
and `reap_driver`, each pthread call wrapped one for one, the same NO
PYTHON3 text; that view compiles with no error under MinGW against stub
declarations for the POSIX calls MinGW lacks. The owner's Linux build is the
real check.

**Seen on the way, not fixed:** seed 4's `arima` wrote its save on the way
with Nei dead and 68 meseta; every retry resumes there, cannot revive her
and gives up (the probe's playthrough too, which got through on its fourth
try; the window's did not, the two games having parted at the first
CONTINUE). For task 8.

**Proof** (b115 against b115_base, taken at HEAD before any edit, itself
identical to 114a's): the 98 PASS lines, the 526 route files, `make
audio_check`, the attract WAV, the Dark Force trial, crt_check 27,
crt_sim_check 38, patches_check 14, probe_check 3 and the build's warnings
(none), all identical; the attract log bar its path. route_probe does not
link autoplay.c; ps2_headless does (the WAV). **src/ changed**: `make`
(autoplay.c, autoplay.h); tools/computer_plays.py too.

## Build 116: the AUDIO page two rows apart, GRAIN, and instrument 70's ring off

Task 4 of handover_113_windows.txt, the owner's answers 1 and 2 (docs/audio.md,
"Build 116"). His checks of 112-112b all passed (questions_113.txt), the
cursor in fullscreen and the title under the menu among them, so nothing
there to fix.

**Instrument 70's ring off.** assets/music/q_patches.json keeps 70's entry
with `"enabled": false`; the shipped file changes nothing now (`patches on
(0 in the file)`). Advanced 25-55 s through Q with it is Q with no patch
file byte for byte (RMS -23.8 dB against 115's ring's -23.5; the chip
-22.9). patches_check lists the file's 0 enabled; its 14 checks pass.

**The page's layout, first.** The AUDIO page's rows were three cells apart
(the value, why under it, a line of fill): eight rows took 27 of the
screen's 28. Now two apart, as the Escape menu's own (the lamp's 16-pixel
step), Q ONLY and NOT BUILT beside the value at AUDIO_WHY_COL: nine rows in
22. The CRT and HDR pages keep three apart (their reasons are long). Proved
alone before GRAIN (b116_layout): of the 526 route files exactly the eight
shots of the AUDIO page changed, everything else identical.

**GRAIN.** A ninth row, after PATCHES: OFF (the default) or ON, Q ONLY
beside it while the chip plays, two values flipped by left, right and C;
settings.json `"grain"`; `PS2_Q_GRAIN=0|1` for one run in the game, the
start-up line ending `grain on|off`. ON is build 112's diagnostic -- each
carrier into the channel's sum cut to the chip's 9-bit step and the YM2612
DAC's jump at zero, on Q's PM voices -- now synth.h's SYNTH_OPT_GRAIN, set by
Q's set_option; Q still reads PS2_Q_GRAIN at creation, for the tools. The
probe hands the row to the mixer each rendered tick, as the other three;
`expect audio grain`. Route `audio_grain` (99 routes): exit 2 on 115's
probe (no such row), PASS on 116's. On the Q build, the attract sequence
with GRAIN off is 115's WAV byte for byte; with `PS2_Q_GRAIN=1`, and with
only `"grain": true` in settings.json, the two WAVs are identical and not
off's.

**Proof** (b116 against b115): 99 PASS lines, the new one added; of 529
route files, the eight AUDIO-page shots and audio_grain's three differ,
the other 518 identical; `make audio_check`, the attract WAV, the Dark Force
trial, crt 27 and 38, patches 14, probe_check 3 and the build's warnings
identical; the attract log differs only in its start-up line (`0 in the
file`, `grain off`). Q (q_116 against q_115, the Q binaries rebuilt): 25 of
26 files identical, the attract log only in that line; audio_check_q -0.3 dB.
**src/ changed**: `make` (sysmenu.[ch], main.c, probe.c, synth.h) and `make
clean && make Q=1` (q_synth.cpp); assets/music/q_patches.json.

### Build 116a: a second chip for the effects, the design

Task 4, the owner's answer 3: extra voices on the chip, a design first --
docs/second_chip.md, nothing built. Read for it: smps.c's play_sfx,
play_spc_sfx, restore_bgm_chn, stop_sfx_tracks and the hooks; fm.c's queue,
native sample and split render; synth.c's chip backend; docs/audio.md's
builds 110-112. Found for it: the game's 56 effects take FM3, FM4 and FM5
only (FM5 in 51, FM4 in 34, FM3 in 18; every effect's header in the
sequencer's image), and one Nuked-OPN2 takes about 42 ms of CPU a second of
audio here (the headless attract, 60 s, 4.2 s rendered with the audio, 1.7 s
without).

The design: A plays the music (every write, the held-back ones too) and B
the effects, routed by the hooks as Q is; B seeded with A's registers at a
claim (the effects that never write their pitch), the music's writes after
RestoreBGMChn ending B's note as the chip's one note a channel does (111c's
lesson); EFFECTS and MUSIC as each chip's gain; VOICES on both synths. Two
questions for the owner: the chip's VOICES default (EXTRA recommended), and B
always running (recommended, about 4% of a core) or stopped while idle.
**Nothing under src/ changed.**

## Build 117: the two queued refactors

Task 5 of handover_113_windows.txt, each proved.

* **battle.c's battle music by name.** battle_enter (read in full) queued
  the fight's music as `0x93 : 0x92`; it names them now,
  `MUSIC_THE_PLACE_OF_DEATH` for a boss (a formation from $100) and
  `MUSIC_RISE_OR_FALL` otherwise. The ROM writes the bare numbers there too
  (BattleScreen, "normal battle music" / "boss battle music"); the
  constants file names them MusicID_RiseOrFall and MusicID_ThePlaceOfDeath.
* **probe.c's fuzz switch.** do_fuzz (read in full) had two stacked case
  pairs. The title and the pause each have their own body now, both calling
  `fz_start`; the menu and the event likewise, `fz_moves_press` for both,
  the menu trying `fz_battle_menu` first as before -- so the fuzzer's
  random numbers are drawn in the same order.

**Proof** (b117 against b116): the 99 PASS lines, all 529 route files
(fuzz_battle's log among them: the fuzz; every fight's `music $92`), `make
audio_check`, the attract WAV, the Dark Force trial (the boss music), crt 27
and 38, patches 14, probe_check 3 and the build's warnings, all identical.
The fuzz sweep (seeds 1-3, 20000 ticks, all 101 levels, --jobs 12) with
116's probe and with 117's: the report and all 909 files identical bar the
logs' own directory. **src/ changed**: `make` (battle.c, probe.c); nothing
anyone plays or hears.

## Build 118: squeak-by -- a leaner grind

Task 8 (The owner, during build 114): the story took 23-25 million ticks a seed,
~110 hours; cut its game time -- the best gear and the companions as early
as can be, grinds that wander a widening range -- applied where it makes
sense, measured on many seeds. Harness only: nothing under src/ changed.

**Measured first** (build 117's harness, seeds 1-12 whole games, `--seeds
1-12 --jobs 12`, scratch copies of the tree):

| | the probe's pace (playthrough's default) | the watch pace (COMPUTER PLAYS's) |
|---|---|---|
| ticks a game | 24.23 M (22.84-26.20), ~112 h | 15.26 M (14.76-16.50), ~71 h |
| the victory's pages | 9.8 M | 1.6 M |
| fights | 6,140 | 6,119 |
| failed tries, all seeds | 19 | 19 |

The ~110 hours were the probe's: it waits 240 ticks at each of the
victory's pages, 40% of its game. The window's game was 71 hours. At the
watch pace the grinds are the rest: lab_in 3.41 M, climatrol 2.26, noah_grind
1.41, skure 1.02, dezolis_grind 0.70, zema 0.60, arima 0.57, gear 0.54.

**Where a grind can be.** Every encounter group from battle.json: a
formation pays 11 EXP in the first groups, 116 on the Biosystems Lab's
ground floor (group 23, 90 HP), 444 in the Underwater Passage (31), 1,100-
2,600 on Dezolis, 4,000-13,800 on Noah; the EXP for the fighting it takes
(EXP over the enemies' HP) climbs from 1.3 at the lab to 2.6 in the passage,
5-7 on Dezolis, 11-30 on Noah. Around the towns before the Key Tube (Paseo,
Arima, Oputa: 11-32 EXP) nothing pays more than the lab. The field west of
Kueri (groups 11 and 13, a 227-cell ground) against the passage, from the
story's end-of-gum save, 1 M ticks, seeds 1-4: 4,420-4,780 EXP a member a
100 k ticks against 4,456-4,864, no wipe-out either way -- no better. And the
party reaches Climatrol with 330-340 k meseta unspent: money never runs
short; the grinds buy levels.

**One target at a time** (watch pace): LAB_TARGET 16 -- 14.43 M, failed
tries 22; 15 -- 14.36 M, 33; 13 -- four seeds lost at Uzo. Climatrol's
targets a level lower: 15.05 M, 31.

**The owner's steer, mid-build:** "I don't think you should be grinding to
assure 100% survival in 1 playthrough. That's why saves exist. Live on the
razors edge squeek by mode. The grind is to accomplish bare minimum survival
gear and levels. Getting best gear is better than level padding." And:
"sometimes grinding for chests retreat and return is almost enough to get
stats and money especially later in game."

**Squeak-by** (story.squeak, story.top_up; docs/harness.md, "Squeak-by"),
the default now for playthrough.py and COMPUTER PLAYS (`--no-squeak` for the
old targets; story.py keeps them): every level target a level or two lower,
the gear-until-bought stages as they were; a failed try retried from its
last save after a top-up of the latest grind stage's own grind, three trips
for each try already failed; six tries a stage.

| Seeds 1-12, whole games | ticks a game | endings | failed tries |
|---|---|---|---|
| before, watch pace | 15.26 M | 12/12 | 19 |
| squeak-by v1 (a 3-trip top-up), watch | 12.60 M | 12/12 | 34 |
| v2: the late grinds dropped (Skure's, Zosa's, the Prism dungeons', Kain's), watch | 13.34 M | 12/12 | 64 (Noah 18) |
| v1 at the probe's pace | -- | 10/12 | seed 4 out of tries at Skure, seed 7 at Climatrol |
| **squeak-by as built (the top-up growing), watch** | **12.82 M (-16%), ~59 h** | 12/12 | 40 |
| **as built, probe pace** | **19.83 M (-18%), ~92 h** | 12/12 | 46 |

v2 tried the owner's second point: the chest runs do pay, but skipping the
Dezolis grinds pushed the levels onto Noah at the wrong time. v1's two lost
seeds showed the top-up's flaw: every retry resumes from the same save,
taken before any top-up, so three trips a try never added up (and at L20
three trips in the passage buy no level). Now the top-up grows with the
tries: 3, 6, 9 trips.

**Found on the way, fixed:** `play.trip_save` saved with the dead in the
party. Seed 4's Paseo grind saved with Nei dead and 68 meseta; the clone lab
out of reach, every retry came back to that save and gave up (build 115's
hand-back test met it too). It never saves with anyone dead now.

**Checked:** without the flag, seeds 1 and 2 to the dams played exactly as
before (story logs, all 22 checkpoints; run before the trip_save fix,
which only acts where a save would have held the dead). Squeak-by replays
exactly: seed 5 to the end of climatrol twice, story and 18 checkpoint files
identical. COMPUTER PLAYS headless with squeak-by as its default: seed 1
through Arima, 422,663 ticks (600,153 before), the probe's squeak-by game
line for line (52 story lines, both checkpoints). The tracked reports in
route_out/playthrough_s*/ are 112b's, not rerun.

**Proof** (b118 against b117): every proof file identical -- the 99 PASS
lines, 529 route files, `make audio_check`, the attract WAV, the Dark Force
trial, crt 27 and 38, patches 14, probe_check 3. **Nothing under src/
changed** (tools/story.py, play.py, playthrough.py, computer_plays.py).

## Build 119: squeak-by pushed further, Noah's target, and a party without Kain

The owner after 118: "push squeak-by further, tune Noah's target". Mid-build:
"Why is Kain the current grind choice? Is he statistically good enough to
compete with the Rudo Amy Anna choice?" and "Add the Rudo+Amy+Anna variant
to the runs too". Harness only: nothing under src/ changed.

**Two retry fixes** (tools/story.py, playthrough.py, computer_plays.py):

* `top_up_noah` ground Rolf and Kain one level past where they stood, on
  every retry. Each retry resumes from the one save, so Dark Force's third
  try was no stronger than its second. Now it's a level for each try
  already failed: +1, +2, +3.
* A top-up could itself end wiped out, or with the dead: seed 2's Dark
  Force, at Noah 39/28, started a try with three dead after a top-up. A
  wiped-out top-up now goes back to the save, and the dead are raised
  again before the stage.

**Noah's target**, the endgame alone: `--from kain_grind` on squeak-by's
checkpoints, seeds 1-12, 101-112 and 201-212 (36 endgames, on seed 1-12's
checkpoints), with the fixes; ticks from the checkpoint to the ending at
the watch pace:

| Noah target (Rolf/Kain) | mean | median | endings | Dark Force losses |
|---|---|---|---|---|
| 39/28 (118's) | 2.29 M | 2.13 M | 36/36 | 113 |
| 37/26 | 2.03 M | 1.83 M | 36/36 | 145 |
| **35/24** | **1.96 M** | **1.70 M** | 36/36 | 152 |
| 33/22 | 1.87 M | 1.70 M | 36/36 | 161 (the worst 5.47 M) |

Below 35 the median stays flat while the tail grows. Before the fixes (12
endgames each), 39/28 lost seed 2 at Dark Force in every stage try. NOAH_TARGET
under squeak() is now Rolf 35, Kain 24.

**Lower targets elsewhere**: whole games, seeds 1-12, watch pace, scratch
copies of the tree:

| | endings | mean ticks (finished seeds) | failed tries |
|---|---|---|---|
| squeak-by 118 | 12/12 | 12.82 M | 40 |
| **v3f: the fixes, Noah 35/24 (as built)** | **12/12** | **12.41 M (~57 h), median 11.99** | 44 |
| v4: every target a level lower (Paseo 6, lab 14, Climatrol 19/19/16, dams 21/21/17/19, Skure 26, Zosa 29, Prism -4, Kain 16) | 11/12 | 12.40 M | 76 |
| v5: two levels lower (lab 13, Climatrol 18/18/15, dams 20/20/16/18, 24, 27, Prism -6, Kain 14) | 11/12 | 12.06 M | 99 |

v3f moved only Noah: Noah's grind went from 1.06 M to 0.40 M a seed. All
408 checkpoints before the Nei Sword are byte for byte 118's. v4 saved
nothing on the seeds that finished and lost seed 2 at Skure (six tries).
v5 lost seed 1 at Uzo: the lab at 13 again, as in 118. So squeak-by's
targets stay as they are, and Noah's is the one that moved.

**Kain or Rudo+Amy+Anna.** Kain's case rested on build 90's Noah trial.
There, Rudo+Anna+Kain was behind Rudo+Amy+Anna up to Kain L24 and ahead
from L28. Build 94's boss trial took Amy in for her Deban: Rudo+Amy+Kain
won Dark Force 5 of 10 with the both policy, and Rudo+Amy+Anna won 1 of 10
(4 of 10 with deban). Squeak-by's Noah brings Kain in at L24, where that
trial had him behind. So `story.no_kain()` (`playthrough.py --no-kain`)
plays the endgame with the party the Nei Sword came with: the kain stage
only Shir's thefts (Anna out for her round and back), kain_grind nothing,
Noah's grind until Rolf is where Kain's grind left him (L37,
`NOAH_KAIN_ROLF`), and no swap before the bosses. 36 endgames each, from
v3f's Nei Sword checkpoints (`--from nei_sword`):

| | mean | median | endings | Dark Force losses | failed stage tries |
|---|---|---|---|---|---|
| **Rolf+Rudo+Anna+Kain, then Amy for Anna (as built)** | **2.41 M** | **2.26 M** | 36/36 | 154 | 54 |
| Rolf+Rudo+Amy+Anna throughout | 2.63 M | 2.62 M | 35/36 | 233 | 65 |

The no-Kain party saves Kain's grind (about 0.25 M a seed), then loses
half as many fights again to Dark Force. Seed 102 ran out of six tries
with Rolf L42, Rudo L41, Amy L40 and Anna L41. Kain stays; `--no-kain`
stays as an option (playthrough.py only). (A first run with `--from kain`
was thrown away: that starts after the kain stage, with Kain already in.)
A seed resumed from a checkpoint does not replay the whole game's fights
past it (the control's seed 1 took 93,684 ticks in the kain stage against
v3f's 65,401): so both sides of this table ran from the same checkpoints,
and neither is compared with v3f's whole games.

**Checked:** COMPUTER PLAYS headless through the window's path, seed 1 to
Arima: 422,663 ticks, as 118.

**Proof** (b119b against b118): every proof file identical: the 99 PASS
lines, 529 route files, `make audio_check`, the attract WAV, the Dark Force
trial, crt 27 and 38, patches 14, probe_check 3. **Nothing under src/
changed** (tools/story.py, playthrough.py, computer_plays.py).

## Build 120: SPEED AUTO and 2X, and the sound after FAST

The owner: "add the auto FAST during grinds sure and maybe even a 2x speed mode
for general viewing". COMPUTER PLAYS is for someone to watch a seeded game,
and its long stretches are the grinds: the Biosystems Lab's and
Climatrol's, about 1.6 M ticks (~7 h at 60 Hz) each.

**SPEED** (docs/computer_plays.md, Speed): five values on the one row, C
stepping NORMAL, AUTO, 2X, AUTO 2X, FAST. The menu keeps its layout, so no
other route's shots move. 2X is two ticks a frame, sound on. AUTO and AUTO
2X play FAST while the driver grinds. The driver says when: `@grind on` /
`@grind off`, host lines on the command pipe (play.Session.in_grind, the
`play.grinding` decorator on play.py's four grind loops and story.py's
grind stages and top-up). The game's command thread keeps them from the
interpreter, and they run no tick. playthrough on the probe sends none.
`expect setting speed` takes the five names. Route escape_menu_computer
steps through all five, a shot of each; the FAST shot is byte for byte
119's.

**Checked** (headless, through the window's own path): seed 1 to Arima at
AUTO, AUTO 2X and FAST gives 119's story log and both checkpoints, 422,663
ticks in 18,437, 9,617 and 1,110 frames. To Paseo, 2X drew 7,839 frames
against NORMAL's 14,995 for the same 14,392 ticks.

**The sound after FAST** (docs/audio.md, "Muted, then back"). FAST mutes,
but the sequencer's writes still went into the 4,096-entry queue, which the
audio thread drains only as it renders. A counting build found seed 1's
game to Arima at AUTO dropping over two million writes, all while muted.
So the chips came back out of step with the music: wrong instruments or
held notes after every FAST, and with AUTO that means after every grind.
Now the game thread keeps a shadow of everything the sequencer writes
(`Audio.mirror`: FM ports, keys, each track's state, the PSG's registers
and latch). While muted it holds the writes, and when the sound comes back
it hands the shadow over in one `CMD_RESYNC`. The audio thread then sets
the PSG, and resets and replays the FM side as a synth switch does. The
audio thread's own shadows became `Audio.live` (the same `AudioShadow`
struct). A test build that compares the audio thread's state with the
mirror when the game quits: equal after AUTO, FAST and NORMAL games; with
the old dropping, Q's per-track state differed. Nobody has listened yet.

**Proof** (b120 against b119b): 99 PASS, 532 route files, 529 of them the
same bytes; the rest, and the one changed PASS line, are
escape_menu_computer's (three new shots, its log and summary). `make
audio_check`, the attract WAV, the Dark Force trial, crt 27 and 38,
patches 14 and probe_check 3 are identical. Q's checks (q_120 against
q_116): all 26 files identical but the path in attract_q.log. src/ changed:
`make` (and `make ps2_headless_q` for Q).

## Build 121: Q in by default, and `make dist`

The owner, during build 120: "I don't want it opt in. I would really like to
inline as much of the build or at least make an exe with all the .dll files
for easy distribution. Possibly a linux flat pack at some point." (Earlier he
asked why COMPUTER PLAYS was not on Windows: it has been since build 115;
and why Q was not: it builds here, but only with `make Q=1`.)

**Q by default** (Makefile, README "Build"): `Q=auto`, the default, builds
`ps2` with Q wherever Q's headers are found and a C++ compiler answers, and
says so and builds without it anywhere else; `Q=1` insists, `Q=0` leaves it
out. `Q_DIR` is looked for as `~/GIT/Q`, then on Windows `GIT/Q` under the
user's folder (USERPROFILE, or `cygpath -H` and `$USER` when a shell has no
Windows environment, as this session's has not). `make headless` takes Q
only for `Q=1`: it and `route_probe` are the proofs' binaries, and
`ps2_headless_q` is the headless game with Q. Checked by dry runs: Q found,
`ps2` with `-DPS2_HAVE_Q` and q_synth.o; `Q_DIR` wrong or no C++ compiler,
neither, and the message; `headless` neither.

**`make dist`** (tools/make_dist.py): `dist/ps2-windows/` and its zip. On
Windows the game in it is linked statically (`-static`, pkg-config's
`--static` SDL3 libraries: SDL3, the C and C++ runtimes, winpthreads,
iconv) and stripped, 5.8 MB. Its imports are Windows' own DLLs alone, and
it opens as a window with no console. Beside it: `assets/`, `shaders/`,
what COMPUTER PLAYS runs (`tools/` computer_plays, play, story,
playthrough, gen_script_ids; `routes/new_game.route`, `routes/saves/`;
`src/game/script_ids.h`), README.txt, `licenses/` (Nuked-OPN2's notice and
the LGPL-2.1, Q's Boost licence, SDL3's zlib, iconv, winpthreads, the GCC
runtime's exception), and `source.zip` (`git archive HEAD`: Nuked-OPN2 and
iconv are LGPL-2.1 and linked statically, so the source goes with them).
18.4 MB, 11.3 MB zipped, with Q. COMPUTER PLAYS still needs Python 3 on the
player's PATH. Elsewhere `make dist` links as `ps2` does; a Flatpak is for
later.

**Checked:** the packaged `ps2.exe` from a copy of the folder starts with
Direct3D 12 and the DXIL shaders, sound up, 791 asset files loaded (a
window for eight seconds, then killed). This session's shell has no
LOCALAPPDATA, so NVIDIA's driver put its shader cache in the working
directory; a normal launch puts it in the user's folder. COMPUTER PLAYS
from the package's own files (the headless game beside them), seed 1 to
Arima: through, both checkpoints as ever.

**Found on the way:** the story log from the package differed from the
scratch runs' by one line. The scratch trees (/tmp/cp115/setup.sh) never
had src/, and without src/game/script_ids.h play.py names scripts $090D and
$090E (two of Rolf's house's) the other way round. The game was the same;
only the log's names differed. So ps2-port_118_linux.txt's and
ps2-port_119_linux.txt's first story.txt hash was a scratch tree's, not a
whole tree's. The 119 list is corrected (da71437b...; step 3's story hash
dropped), and setup.sh copies the header now.

**Proof** (b121 against b120): every file identical. Nothing under src/
changed (Makefile, tools/make_dist.py, README, .gitignore, CLAUDE.md).

## Build 121a: make dist a second time

`make dist` run again refused to empty dist/ps2-windows/: copytree had
carried the tree's assets/data read-only attribute over, and Python on
Windows will not remove a read-only folder. make_dist.py's rmtree now
clears the attribute and tries again; two runs back to back go through.
Nothing under src/ changed.

## Build 122: Python in the package, and no name in it

The owner: "bundle the embeddable Python ... Probably strip mention of my name
from any of the files that would be distributed."

**Python** (tools/make_dist.py, autoplay.c): on Windows `make dist` puts
python.org's embeddable Python 3.14.7 (the version the harness runs on
here) in the package's `python/`. It is downloaded once into
`build/python/` and its SHA-256 checked against python.org's downloads API
(d297e5ff...; a file that differs is removed, not used); its LICENSE.txt
goes to `licenses/`. The game looks for `python\python.exe` in its own
folder before the PATH (`bundled_python`, GetModuleFileNameW) and says
which Python it started on stderr. The embeddable Python's `._pth` keeps
its path to itself; the driver puts `tools/` on it as it always has.
Checked from a copy of the package, with MSYS2's Python on the PATH too:
`computer plays: ...\python\python.exe`, seed 1 through Arima, both
checkpoints as ever. The story log differs from MSYS2 Python's only in two
checkpoint paths' separators (`saves\computer\...`).

**The name** (make_dist.py, `scrub`): every text file the package carries,
`source.zip`'s included, has the maintainer's name put as "the author"
("The author" where it begins a sentence or follows an opening bracket):
145 of them in this package. `source.zip` leaves out the working notes
(CLAUDE.md, NOTES_next_context.md, questions_*.txt, handover_*.txt). The
tree itself is unchanged. Checked: all 1,039 entries of the zip, and every
file inside source.zip, have no trace of the name or the e-mail address;
the rewritten tools/*.py compile; the exe carries neither (nor any user
path). The package: 43.1 MB, 23.6 MB zipped.

**Proof** (b122 against b121): every file identical. src/ changed
(autoplay.c, Windows only): `make`.

## Build 123: the pad back at the last save, saves wherever the game is, and Rolf's icon

The owner's answers after 122: "It should always load last save" (COMPUTER
PLAYS out of tries); "The Stand Alone game should have the paths for saves
both player and computer"; "if you make the game entirely inline it should
generate any necessary files and folders that don't exist on first start";
"it might be cool to make the game logo a little ROLF portrait". (And: 2X's
double-tempo music stays, "comical, I like it"; no FLACs unless something
needs his ears; VOICES for both chip and Q -- the second chip, next.)

**Out of tries** (computer_plays.py): a stage out of tries CONTINUEs the
last save -- the stage's save on the way, else the checkpoint before it --
before the driver ends, so the pad comes back in the game, not on a
wipe-out's title. Checked: `--tries 1 --fail-once arima`, "arima: out of
tries; continued ...mid_arima.json for the pad", the party standing.

**The saves** (src/platform/home.c, main.c, gamedata.c, autoplay.c):

* `home_enter`: started from another folder (a shortcut, a double-click
  elsewhere), the game goes to its own (GetModuleFileNameW; /proc/self/exe)
  when the working folder has no assets/manifest.txt.
* `home_saves`: one folder for every save -- the player's slots,
  settings.json, COMPUTER PLAYS's computer/ with its slots and checkpoints:
  `PS2_SAVES` if set; `saves` beside the game where a file can be made
  there (one made and removed: nothing is left behind); else
  %LOCALAPPDATA%\PhantasyStarII\saves on Windows ($XDG_DATA_HOME or
  ~/.local/share, phantasy-star-ii/saves, elsewhere -- the Flatpak's, later),
  made as deep as need be. `gamedata_set_save_home` (what a NULL save dir
  goes back to), `sysmenu_set_settings_path`, `autoplay_set_home` (the
  macros AUTOPLAY_DIR and AUTOPLAY_SLOTS are gone). Everything else the game
  writes it already made on first use.

Checked (headless, scratch copies): a start makes nothing; started from
another folder, "home: the game's folder, ..." and every asset loaded;
`PS2_SAVES` given, settings.json and computer/ (report, story, slots,
checkpoints, last.json, driver.log) all under it and nothing beside the
game; the game's folder denied writing (icacls) with LOCALAPPDATA in
scratch, "saves: ...\PhantasyStarII/saves (the game's own folder cannot be
written)" and COMPUTER PLAYS through Paseo into it.

**Rolf's icon** (tools/make_icon.py, assets/icon/, src/platform/ps2.rc):
the portrait's picture cut from the new_game route's shot of Rolf's house
(80 x 80: the window's interior; rolf_portrait.png's tiles are in load
order, the window's map places them), assets/icon/rolf_portrait.png, and
from it assets/icon/ps2.ico (256 the art tripled by whole pixels, 16-128
scaled smoothly). ps2.rc makes it the first icon -- Explorer's, and SDL
gives the window the first -- with version information (Phantasy Star II,
a port in C); the Makefile links it (windres) into ps2.exe and make dist's
on Windows. Checked: Windows's own icon lookup on the packaged exe gives
Rolf; its FileDescription reads Phantasy Star II.

The package's README.txt says where saves go. **Proof** (b123 against
b122): every file identical; the proof copy's headless runs left no saves/
and no probe file. src/ changed: `make`.

## Build 124: the second chip -- VOICES on the chip too

The owner: "I want the option for both chip and Q" (docs/second_chip.md's
questions). Built as the page designed it, with its two recommendations:
EXTRA the default (one VOICES setting for both synths), B always running.
docs/second_chip.md, "Built", has how and what was measured; in short:

* synth.c's chip backend: A (the one chip; all that is heard under
  LIMITED, exactly as before) and B (the effects' under EXTRA), routed by
  the hooks; the claim seeds B's channel from the one chip's registers; once
  the effect ends the music's writes land under B's note and its first
  strike ends it. audio.c replays the chip when VOICES changes; the AUDIO
  page's VOICES loses Q ONLY.
* A fight, headless, EFFECTS on and off: B's effects within ~1 dB of the one
  chip's (2-3 dB in some strikes' first 20 ms: the envelope they start from,
  not copied); LIMITED's lost music (59 dB under the music's 68) gone. The
  attract's WAV identical at both ways; the menu's sound over the title:
  EXTRA differs from the untouched run only while it plays. B: ~3.5% of a
  core.
* Route audio_voices: `expect audio voices EXTRA` (no Q ONLY), failing on
  123's build, passing now.

**Proof** (b124 against b123): 99 PASS lines identical, the attract WAV,
`make audio_check`, the Dark Force trial, crt 27 and 38, patches 14,
probe_check 3 identical; 518 of 532 route files identical. The 14 others are
the AUDIO page routes': seven shots (VOICES without Q ONLY) and seven logs
(the page's words, and the peaks of `expect sound playing`, moved a little:
the probe hands the page's VOICES, EXTRA, to the mixer at its first rendered
tick, and the chip replays onto it mid-song). Q's checks (q_124 against
q_120): identical but attract_q.log's path. src/ changed: `make`.
For the owner's ears: effects over music on the chip, EXTRA against LIMITED --
a fight, the level-up over the victory music, the heal over Step Up.

## Build 124a: the switch's replay kept the last song

The owner, listening to 124: under VOICES EXTRA the chip's crawl melody went
"higher pitched and louder, less flute like", and Q's did too whatever
VOICES said -- after a change in the Escape menu.

**Found:** audio.c's record of each track (live.track_reg, build 111),
which every switch of synth or VOICES replays, never forgot a song. The
song before the attract crawl puts tracks 2-5 on FM1; the crawl's song puts
them on FM2-FM5, and their FM1 registers stayed in the record. The replay
goes track by track, so after track 1's own FM1 registers came tracks 2-5's
stale ones: FM1's operator 4 at $27 where the crawl's melody had $14. On
the one chip (LIMITED) the switch replays the chip's own registers in order
and did not show it; the second chip's A (EXTRA) and Q take the tracks'
records. A check build compared chip A's registers with each music track's
after two VOICES flips in the crawl: FM1's $4C wrong before, all equal after.
Not new in 124: Q's switch has replayed these records since build 111.

**Fixed** (shadow_track): a track found on another channel than it was
starts its record again.

**Route** audio_records, and `expect sound-records` in the probe: no
track's record holds a register off its own channel but the pan flag's $B4
and FM3's special-mode pitches. On 124's audio.c it fails during the title
("track 2 (FM2) holds FM1's $4C" ... tracks 3-5 the same); it passes now,
at the title, in the crawl and after a switch there.

**Proof** (b124a against b124): 100 PASS (audio_records added), 534 files;
of the 532 before, all identical but audio_switch's and audio_voices' logs
(the peaks after their switches' replays). The attract WAV, `make
audio_check`, the trial, crt, patches and probe_check identical. Q's checks
(q_124a): identical but attract_q.log's path. src/ changed: `make`.

## Build 125: the assets and shaders inside the program

The owner: "Pack the assets and shaders into the exe". Earlier: "if you make the
game entirely inline it should generate any necessary files and folders that
don't exist on first start".

**The pack** (tools/make_pack.py, src/platform/pack.S, assetfs.c): every
file under assets/ (bar Tiled's .tsx and .bak), shaders/, and what COMPUTER
PLAYS runs (tools/'s five, routes/new_game.route, routes/saves/,
src/game/script_ids.h), in one blob -- 989 files, 5.0 MB -- linked into
`make dist`'s game by `.incbin` (PS2_PACKED). assetfs_preload takes the
pack's files in place of the manifest's, their bytes where they lie; the
loaders that read files themselves ask the pack first: tables, nametables,
window maps (through asset_open/asset_read, fopen's and fread's shape, so
their decoding is unchanged), the sound data, the chip's and Q's drums
(smps_set_reader, fm_set_reader, q_synth_set_reader, which audio.c points
at the pack: those files are linked into tools without assetfs too), and
the CRT shaders (crt_gpu.c). make_pack.py puts the maintainer's name as
make_dist.py does, and rewrites build/pack.bin only when its bytes change.
A tree build has no pack: every loader reads the disk exactly as before.

**COMPUTER PLAYS** from the pack (autoplay.c): its first time, the game
writes the driver's files -- tools/, routes/, src/, assets/data, levels and
script -- under <saves>/computer/harness (619 files), and again only when
the program's pack differs (pack_id.txt, a hash of it); the driver starts
there (`--home`, computer_plays.py changing folder before play is imported)
with every path it is given whole.

**The package** (make_dist.py): ps2.exe (10.9 MB), python/, README.txt,
licenses/, source.zip -- 23.3 MB zipped. `make dist_headless` builds the
headless game with the pack, for testing, not packed.

**Checked:** from a folder holding only the packed headless game and
python/: 989 files from the pack, the attract WAV byte for byte the
baseline (97141d6d...); COMPUTER PLAYS seed 1 to Arima at FAST on the
bundled Python, 619 files written out, both checkpoints as ever (the story
log differs from a tree run in its two checkpoint lines, now whole paths);
a second run writes nothing out. The packed window game from an empty
folder: Direct3D 12 with the DXIL shaders from the pack. Found on the way:
MSYS2's Python gave shaders/'s entries backslashed in the pack, so the
first packed window fell back to the placeholder; make_pack.py writes
forward slashes whatever the Python. The zip's 52 entries, source.zip's and
the exe's packed files included: no trace of the name.

**Proof** (b125 against b124a): every file identical; Q's checks (q_125)
identical but attract_q.log's path. src/ changed: `make` (and `make
ps2_headless_q` for Q). Not compiled here: the POSIX side of the harness's
writing out (make_parents, getcwd) and `.note.GNU-stack` in pack.S.

## Build 126: the Nei pictures

The owner's "message about graphics image issues.txt", item 1: after the
Neifirst fight, the picture of Nei and the small closed-eyes picture drawn
over it came out sheared diagonally and wrapped.

**Read:** loc_7E02's two picture cases (ps2.asm:11498, 11521) draw the first
with `moveq #$1B, d1` / `moveq #$D, d2` at $410C0003 and the second with 5 and
3 at $44A20003; DrawArtTiles (ps2.asm:8581) runs both loops with `dbf`, so
each count is its size less one: 28x14 and 6x4. The data agree: loc_AF4EA to
loc_AF7FA is $310 bytes, 392 words, 28x14; loc_AF7FA to loc_AF82A (the next
label) $30 bytes, 24 words, 6x4. tools/extract_nametables.py had the counts,
27x13 and 5x3. 28x14 at $410C0003 is the dream pictures' size and place too.

**Fixed** in the extractor's table and comment; `make extract-nametables`
(PS2_ASM, PS2_CONST, ART_DIR from ../ps2_disasm) rewrote only
NeiDeathTileInd.nam and NeiBodyTileInd.nam and their two entries in
nametables.json -- the other 20 tables came out byte for byte as they were.
Nothing under src/ assumed the old sizes (game.c's level_scene_picture and
its draw take them from the .nam); game.c's comment on them is corrected.

**Every table checked** against the ROM (a scratch audit: each label's bytes
to the next label, and every DrawArtTiles call that loads it, its d1/d2 +
1): no other mismatch. The title background's run reads on into data past
its 40x28 (drawn 40x28, as the table says); Tyler's monitor is five 15x12
frames kept as one 15x60 table on purpose; the epilogue portraits and the
last picture are drawn through loc_79F2's table, their sizes the data's
exactly. Looked at in the routes' shots: the title, the dream, Tyler and his
monitor, Mother Brain's machine, the ending pages and last picture all
right; Lutz (esper_b1_lutz) shows as black with his text -- the message's
item 2 asks about him, with the art dump.

**Route** nei_pictures (climatrol_nei_dies's way in): a pixel of Nei's hair
in the first picture ($804; the old table gives the red sky, $20E) and of
her closed eyelid in the second ($8AE), a shot of each. It fails on 125's
tables at the first and passes now.

**Proof** (b126 against b125): 101 PASS (nei_pictures added), 539 files;
of the 534 before, all identical but the four Nei-picture shots in
climatrol_nei_dies and climatrol_nei_wins. The WAV, the trial, crt,
patches, probe_check identical. src/ changed only in a comment; the assets
changed: `make extract-nametables`, or take the two .nam files as they are
(they are checked in, as every extracted asset).

Item 2 (the art dump) waits on its four tools (art_dump.c, adm.py,
art_dump_plan.py, art_export.py): the tarball the message names is not on
this machine; art.zip matches ../ps2_disasm/art, and ps2_art.tar.gz is the
last dump's output, not the tools.

## Build 127: the art dump in the tree, Lutz's palette, the cut-scenes' colours

The owner's "message about graphics image issues.txt", item 2, with the four
tools from ps2-port_118_artdump_tools.tar.gz; and mid-build: "what about
central tower outside 2 and teleport frame, make sure none of the others
have that issue?", "A few of the dump images have no palette applied".

**The tools** (tools/art_dump.c, adm.py, art_dump_plan.py, art_export.py),
read in full and folded in. art_dump records the battle animations by
running the port's own battle objects; it is one translation unit with
src/game/game.c, which it #includes for the game's static state. Its mkdir
is portable now (MinGW's takes no mode). `make art_dump` builds it; `make
art` runs the lot -- the seven routes whose shots it reads, the plan, the
dump, the export into ART_OUT (../ps2_art) -- and packs ps2_art.tar.gz
beside it (22 s for the dump and export here). Its first run broke on the
space in the tree's path at the packing and left the owner's own
../ps2_art.tar.gz an empty file; the step is fixed, the new dump replaces
it, and the old one's portraits and cut-scenes were kept in scratch.

**Lutz, in the game.** His picture in the Esper mansion was black, the
port's bug and not only the dump's: loc_8364 loads palette $3B, whose own
PalettePtrs entry names line 3 (its RAM destination), where
LutzPortraitTileInd draws; game.c loaded it on line 1. Fixed; route
esper_b1_lutz checks a pixel of his face ($68E; $000 on 126's game). Every
palette_load_entry in src/ checked against its entry's line the same way:
no other mismatch.

**The cut-scenes' colours.** The exporter gave each picture a palette from
a fixed table, and several were wrong: the three dream pictures all in the
waking one's cyan -- the intro loads a different palette on line 3 for
each, the battle picture sepia (new route intro_dreams: $AAC and $EC0);
the second, the "help" picture, is shown only in Tyler's ship, in
pal_id_tit_scr (loc_8326) -- what "reads oddly" was; Tyler without the $2A
Tyler's ship loads on lines 2-3 when it shows him (loc_CD50: this is
where $2A, Pal_CentTow2Port, is used); the ending's last picture grey;
Lutz black. Now each cut-scene takes its 64 colours from a route's shot of
it on screen (SHOT_PALETTE: route shots are indexed PNGs carrying the live
palette), so a picture comes out as the game shows it; the planets, the
battle floor, the Sega logo and the title's own art keep their fixed ones.
A scan of the new dump: no image draws with a palette line never loaded.

**Mother Brain.** The "machine" -- loc_AE8A0, which BattleScreen draws
across the top of plane B for her formation (ps2.asm:12092) -- is the star
field behind her: 21 tiles of her art, $8E-$A1, on line 3, the stars her
lights' chase makes twinkle. Its dark dots were right; it is not her. Her
figure is the battle object (monsters/126_mombrain). Now
cutscenes/mother_brain_starfield.png (in her lights 360 ticks after
FIGHT/STGY, mother_brain_battle's shot 04_rainbow: the chase lapped, the
glow and the rainbow up) and mother_brain_battle.png, the game's own frame
of the scene then. game.c's comment on the table said "her picture" and is
corrected.

**The portraits.** Portrait slot N takes its art and palette from
PortraitTable and its window map from PtrWin_PortraitStart. Lined up, 28
of the 31 slots pair; 25, 28 and 29 do not: 25 the tower view's art in $2A
with the teleport clerk's map, 28 and 29 the view in its third palette
with WinArt_BattleEmptySpots, a battle window's map. None is ever opened:
no door names 25, 28 or 29 (doors use 9-19, 21, 24, 30, and 55 and 62,
past the table, which load nothing), no code sets 25 or 29, and the one
that sets 28 -- entering Tyler's ship (loc_CC26) -- goes to loc_8326,
which skips LoadPortraits and opens no portrait window (said otherwise
during the build, then read). The port does the same. The dump: 22 (the
tower view with the man who runs up at "Something's wrong...", its own map
WinArt_CentTowerOutsidePortraitCopy) exported as
central_tower_outside_alarm; 25 and 28 drawn with the view's own map in
their palettes, as palette variants; index.json says which slots are
unused mismatched pairs.

**Proof** (b127 against b126): 102 PASS (intro_dreams added), 543 files;
of the 539 before, all identical but esper_b1_lutz's log, summary and shot
and the Lutz shots in esper_b1_hold and esper_lutz_nei (Lutz shown). The
WAV, the trial, crt, patches, probe_check identical; Q's checks (q_127)
identical but attract_q.log's path. src/ changed: `make`.

## Build 128: the hand-off

The owner: "I'd like to wrap up windows stuff for now. Is the recipe and all
the drivers packaged in the tar.gz for Windows Builds? Finish what needs to
be finished and prepare Tar GZ for project hand off. I need to set up
Claude on my beefier computer, but will continue with online project
Development."

**What the tarball had, and had not.** A `git archive` of the tree carries
every recipe -- the Makefile with `make dist` and `make art`, the assets,
the compiled shaders, the harness, the docs -- but three things the
Windows builds leaned on were outside it: the proof scripts (/tmp/proof.sh
and /tmp/qproof.sh, scratch files since 113), the whole-game and endgame
measurement scripts of 115-119 (/tmp/cp115), and the machine's set-up,
which only CLAUDE.md's Machine section described, as this VM's state
rather than as steps. No drivers are needed beyond the graphics card's own:
the game draws through SDL3's GPU API (Direct3D 12 by default, Vulkan on
request, SDL_Renderer without either), and `make dist`'s game links SDL3
and the C and C++ runtimes statically, needing only Windows's own DLLs.

**Into the tree:**

* tools/proofs/: `proof.sh NAME [JOBS]` and `qproof.sh NAME`, the scratch
  scripts made portable -- the tree found from the script's own place,
  `.exe` names only where the binaries have them, the job count from
  `nproc` -- and `compare.py`, which does what diff and cmp did from Git
  Bash (MSYS2 has neither): two proof sets' single-result files, the
  routes' PASS lines, and the route files by checksum, changed or on one
  side only; `--all` compares every file (Q's sets). Exit 0 when all match.
* tools/measure/: `scratch_tree.sh` (a game directory with no saves/, the
  binaries and src/game/script_ids.h), `whole_games.sh` and
  `summarize_games.py` (seeds 1-12), `endgame_setup.sh`, `endgame_run.sh`
  (JOBS=) and `summarize_endgames.py` (the 36 endgames from nei_sword),
  `computer_plays_speed.sh` (COMPUTER PLAYS headless through the Escape
  menu's own path, at any SPEED).
* tools/setup_msys2.sh: the MSYS2 packages by name (`pacman -S --needed`,
  so a second run does nothing), `--shaders` for SDL_shadercross's build,
  `--sanitizers` for CLANG64, `--q [DIR]` for Q at the commit last proved
  (6cd1aba, infra 2dff97a), in the folder `Q=auto` looks in.
* docs/windows_build.md: a Windows machine from nothing -- what the tree
  has and what each missing piece is for and where it comes from (MSYS2,
  Q, shadercross and DXC, CLANG64, the embeddable Python `make dist`
  fetches itself, and ../ps2_disasm, which is not public and only `make
  extract` needs), the drivers, the steps from MSYS2's installer to `make
  dist`, working from Claude Code there, and SDL_shadercross's build as
  this VM did it (commit 1ff05be, DXC v1.9.2607, the CMake options).

CLAUDE.md's Machine section says it is this VM's and to fill it in anew on
the next machine, with a baseline of its own before any edit; its scripts
bullet points into the tree; pkgconf is 3.0.7 (the section said 3.4.16,
SDL3's number). README: the Windows page, docs/computer_plays.md and two
measurement pages in the docs table (second_chip.md is built, since 124);
the new scripts and seventeen files the "What is here" table had never
listed (the trial and check tools, drum_hits, make_dist, make_pack,
make_icon, the art dump's four, home.c, pack.S, ps2.rc, script.c); a note
that "Not done yet" is older than much of what it lists. tools/art_dump.c's
comment named its plan and image tools by names they never had.

**Tested:** proof.sh against the old scratch script's b127: every file the
same (compare.py exit 0); against b126, the files 127 changed and no others
-- the three Lutz shots, esper_b1_lutz's log and summary, and intro_dreams'
four. qproof.sh (q_128) as q_127 but attract_q.log's path. setup_msys2.sh
with --shaders and --sanitizers on this VM: nothing to do; an unknown
option refused; --q into scratch: Q cloned from GitHub at 6cd1aba with
infra at 2dff97a, as this VM's own, in a minute. computer_plays_speed.sh,
seed 1 to Arima at FAST: the three hashes of 120 on (da71437b, 637c2276,
4e25e54b), "finished: through arima", 9.1 s. The summarizers on 119's runs:
v3f 12.41 M, 12 of 12; nk 2.63 M, 35 of 36; kc 2.41 M, 36 of 36.
endgame_setup.sh and endgame_run.sh from v3f's checkpoints again (285 s at
12 jobs): 36 of 36, 2.41 M, 154 Dark Force losses and 54 failed tries, as
kc; each seed's report and story the same as kc's but for wall-clock
seconds and script names (kc's tree had no script_ids.h, 121's finding),
and the checkpoints byte for byte.

**Proof:** nothing under src/ changed; b128, and b128c on the finished
tree, are every file as b127. art_dump built again (its comment): no
warning.

**The hand-off**, beside the tree: ps2-port_128.tar.gz (git archive of this
commit, rooted at ps2/); ps2-port_128.bundle (git bundle of main, builds
112b-128 with their messages: this repository has no remote, and a tarball
carries no history); ps2-port_128_routes_windows.sha (b128's route files,
for `sha256sum -c` elsewhere); ps2-port_128_handoff.txt (what they are,
what changed since 124a's Linux list, what this VM could not compile, the
numbers to expect, what wants the owner's eyes and ears); and
ps2-windows_128.zip (`make dist`: the first package with Lutz's palette and
the Nei pictures).

## Build 129: build 128 on Linux, window.c's stacked cases, README's "Not done yet"

The owner, after 128's hand-off: "check in on things, anything undone, maybe
verify the sprite and palette stuff is all fixed", then "look at
outstanding jobs". Made in the claude.ai chat sandbox, not on his machines:
Ubuntu 24.04, gcc 13.3.0, Python 3 with Pillow 12.1.1, one core, no SDL3,
no Q, no network, no ../ps2_disasm. A tool call there stops at 300 s; a run
started with `setsid` keeps going past it.

**Build 128 on Linux** (NOTES, after 128, item 2), from ps2-port_128.tar.gz
(its SHA-256 the hand-off's):

- `make headless route_probe patches_check crt_ref art_dump
  dist_headless`: no warning under `-Wall -Wextra -Wpedantic`. The first
  Linux compile of home.c's POSIX half, autoplay.c's POSIX spawn, pack.S's
  ELF note and art_dump's mkdir with a mode.
- `make routes`: 102 PASS, 49 s one at a time. `sha256sum -c` against
  ps2-port_128_routes_windows.sha: all 543 files the same bytes as the
  Windows VM's.
- The attract WAV (PS2_FRAMES=2400) 97141d6d..., Windows's. crt_check 27 of
  27, crt_sim_check 38 of 38, patches_check 14 of 14, probe_check 3 of 3,
  audio_check PASS (peak -10.0 dBFS, RMS -25.5 dBFS).
- COMPUTER PLAYS, `tools/measure/computer_plays_speed.sh /tmp/cp 4 "--to
  arima --seed 1"`: 422,663 ticks, 6.3 s; story.txt da71437b...,
  story_paseo.json 637c2276..., story_arima.json 4e25e54b..., "finished:
  through arima" -- the hand-off's numbers.
- home.c: the scratch tree read-only, run as another user (`runuser -u
  nobody`) from another directory with XDG_DATA_HOME set: "home: the game's
  folder, /tmp/ro", then saves in $XDG_DATA_HOME/phantasy-star-ii/saves,
  made, and the same two checkpoints written there. Not run: the
  ~/.local/share branch (XDG_DATA_HOME unset).
- `make dist_headless`: the pack 989 files; `readelf -l` shows GNU_STACK
  RW, so the `.note.GNU-stack` section took, and the link gave no warning.
  From an otherwise empty folder: the attract WAV byte for byte; COMPUTER
  PLAYS wrote its 619 files out under saves/computer/harness and gave the
  same checkpoints, story.txt differing only in the two checkpoint lines'
  paths -- as on Windows.
- `make art`: 45 s with its routes; ../ps2_art, 1365 files, 882 PNGs, and
  ps2_art.tar.gz. The plan skips seven enemies no formation uses (40-42,
  55-57 and 89, the Van Leader), so the dump has no animation of them.
- Not run on Linux: the windowed `ps2` and `make dist` itself (no SDL3
  here).

**The pictures, checked.** The shots of nei_pictures, esper_b1_lutz,
esper_b1_hold, esper_lutz_nei and intro_dreams looked at: Nei's face on the
red, her closed eyes over the stars, Lutz in colour in all three, the
dream battle sepia and the waking dream cyan. The dump scanned for a
picture drawn black or flat (a palette line never loaded): none. The
mostly black ones are black by nature -- Mother Brain's star field, the
waking dream, the walkers' dark outlines -- and Darkside, Mr. Death, Shadow
and the lava monsters, nearly empty in their first idle frame, fill in over
the later ones. Contact sheets of the cut-scenes, the 30 portraits, all
119 monsters and the field sprites: every one in colour. For the emulator:
the first Nei picture's white streak across the red backdrop (three rows,
pink either side) looks drawn, not a wrong tile.

**window.c's stacked cases.** Seven groups, not the five NOTES counted:
the window builder's battle stats slots, enemy names and enemy infos, and
the handler's stats slots, empty spots, name/item/technique-used windows
and enemy boxes. Each case has its own body now, as the menu's stats
windows already did; the two with real logic call named helpers,
`battle_char_stats_handler` (a slot past the party becomes the empty-spot
art, loc_10946) and `enemy_box_handler` (drawn at tile base $500, then the
font base back). The tree's only stacked labels left are
src/audio/ym3438.c's: Nuked-OPN2, vendored, left as it is.

**README's "Not done yet"** gone through against this file, the docs/
pages and the source. Done, and moved to a list at its end with the build
that did it: the window palettes, the dams' panels (80), the title
objects, music, `{enemy}`/`{tech}`, the buildings and all eleven demo
walks, the fight, the level screen's events, the screen fades (game.c's
`goto_screen`; not checked change by change against the ROM), CRT phases 2
and 3. "Window widths" and "Selection cursor" are reference and went to
Window maps, the title objects' paragraph to Screen sequence, each without
its stale last sentence. Found on the way, and left open: `\I` in the
scripts. README's Window maps section says it is the double quote, but
`win_charset` has no case for the backslash, so the port leaves that cell
and draws a letter I -- 61 places, 55 in the level events. It waits on
ps2.asm's character map.

**Proof** (b129 against b128, both taken here with `tools/proofs/proof.sh
NAME 1`, 89 s each): compare.py -- every single-result file the same (the
PASS lines, crt, sim, patches, audio, probe_check, the WAV, the Dark Force
trial 25d3ca5f..., the build's warnings, none), and 543 of 543 route
files. 26 routes fight; `fuzz_battle` begins an attack, a technique, an
item and a defend. (The tree's own route_out/boss_trials_dark_force.* are
an older run of every policy, not this command's.) src/ changed:
window.c; `make`.

## Build 129a: the quote in the scripts

README's open item from 129: `\I` in the script text. The owner uploaded the
disassembly (ps2.asm and its constants, macros and macro set-up files) and
Cycfi's Q (q.tar.gz, not used this build).

**Read:** the script text (GameScriptPtrs, ps2.asm:32266 on) is assembled
under the charset of ps2.asm:32183-32195, in force until 38664, where the
double quote is glyph $42. `\I` is AS's escape for that character, so a
quote inside a `dc.b` string does not end it -- the disassembly says so
itself (`; \I is the " character`, ps2.asm:29633), and
tools/extract_winart.py already mapped it so. LoadScript (ps2.asm:25476)
copies any byte below $BB into the text buffer as a glyph: one cell.
tools/extract_script.py kept the two characters, and the window's charset
has no backslash, so the port drew a cell left alone and a letter I. Every
quoted line was one cell too long: Neifirst's `"But I escaped,and stole`
fills the box's 24 columns in the ROM, and the port lost its final "e".

**Fixed** in the extractor: `\I` becomes `"`, and any other backslash in a
script string stops the extraction (none occurs). `make extract-script`
from the uploaded ps2.asm rewrote Script_LevelEvents (55 quotes),
Script_ItemAction, Script_People and Script_CloneLabs (2 each), each
difference a `\I` become `"`; script.json the same. gen_script_ids.py run
again: script_ids.h's names and values the same, only its comments' text
excerpts changed. `make manifest` was not kept: it now adds
assets/icon/ps2.ico and rolf_portrait.png, which the checked-in manifest
has never listed (they came in build 123); 128's manifest is kept.

**Route** script_quotes (climatrol_nei_dies's way to Neifirst): a pixel of
the quote opening line 3 of $180A and line 1 of $180B, a shot of each. It
fails on 129's scripts (a blank cell, $800) and passes now ($EEE).

**What moved.** A quoted line now prints one glyph fewer per quote, so the
text ends sooner and every tick after it moves, and with them what the
routes met after it: the ride's encounters, a fight's draws, the lights'
phase (the random-number routine's callers were not read this build).
Twelve routes pass through
quoted text; ten passed as they were, with new tick counts and new shots
(the quotes right, "ROLF wait!" Lutz's voice, `...said, "Just as I
thought`; the shake and explosion frames a step apart; mother_brain_won's
fight ending on other HP). Two were aimed again:

- jet_scooter_dam: the first boarding plays the Mota people's note
  ($1686, two quotes). The encounter the route waits for no longer comes
  on the step up from 85,108; it comes on the second step left from
  85,107, and the scooter is parked at 84,107 -- the cell the step left,
  the behaviour the route is there for. A second encounter now comes on
  the way up to the dam, at 81,84: fought, the scooter parked there, and
  on up into Red Dam F1 as before.
- mother_brain_battle: the rainbow pixel 360 ticks after FIGHT/STGY is
  $048, a step on from $06A. Read in loc_67FE: her lights wait on
  $FFFFF634, the level's palette-cycle counter, which no screen resets
  (the port hands it into the battle, as it does for the Neisword's
  light), so their phase depends on every tick before the battle; the
  confrontation's text is shorter now. A scratch build printed the
  counter at the lights' first call: 5 on 129's scripts, 1 on 129a's.

The art dump's cutscenes/mother_brain_starfield.png takes its palette from
that route's shot 04_rainbow, so `make art` gives the rainbow a step on.

**Proof** (b129a2 against b129, this sandbox): 103 PASS of 103, 548 files
(script_quotes' five new); 61 of 543 differ, all in the twelve routes
above; the attract WAV, audio_check, the Dark Force trial, crt, sim,
patches, probe_check and the build's warnings the same. COMPUTER PLAYS
seed 1 to Arima: 422,663 ticks, story.txt and both checkpoints as 128's
(its story reaches no quoted line). A whole game, seed 1 at the probe
pace (playthrough.py --seeds 1, a scratch tree): the ending, 41
checkpoints, 19.60 M ticks, no port stderr, 102 s. src/ changed: script_ids.h's comments
only; assets/script changed (`make extract-script`, or take the four files
as they are); nothing to rebuild for the game but `make` if you like.

## Build 129b: the steal split

NOTES' queued "fm_probe --steal split into the music's loss and the
effect's cut", with the Q the owner uploaded in 129a's session (q.tar.gz,
Q_DIR=its Q/). The owner: "not even sure what the --steal effect is but go
for it" -- an effect on the Mega Drive plays on one of the music's
channels and silences the music's part there; `--steal` measures that.

**Read:** tools/fm_probe.cpp's steal case and its Synth; synth.h, synth.c's
chip backend (LIMITED marks each channel's owner from the hooks, EXTRA
routes the effects to a second chip), fm.c's fm_mark_channel and
fm_render_split (a mark takes no sample slot; equal gains are fm_render).
fm_probe drove its own Fm with no hooks, so its chip could neither be split
nor run EXTRA.

**Changed:**

- fm_probe's chip is the game's chip backend (synth.c, linked in:
  FM_PROBE_C); the hooks go to it as audio.c sends them. A new path,
  chip_extra (VOICES EXTRA); `--chip limited|extra|both` puts the one chip,
  the second, or both first in the list (limited by default, as before).
  `--music-only` now reaches the chip. Render takes the two buses' levels
  (Mix).
- `--steal` keeps its line and adds the split: the together rendered again
  with EFFECTS at 0 and with MUSIC at 0, the music's part against the song
  alone (20 ms energy, mean / worst), the effect's part against the effect
  alone (where it starts, where it ends in the mix and alone, the energy it
  keeps, the windows while it plays), and the parts' sum against the
  together, sample by sample. A per-window mean alone was no measure of a
  cut tail (past the cut every window reads -60 dB or worse): hence the end
  and the energy.
- synth.c: `synth_chip_pending_writes`, the chip backend's queued writes,
  for fm_probe's settle (it read the Fm's queue before). The game never
  calls it.
- tools/proofs/qproof.sh: `fm_probe --steal B5 --over 92 --chip both`
  (fm_steal_B5.txt).

**Measured** over the battle music ($92), docs/audio.md, Build 129b: under
EXTRA the chip's music loses nothing, as Q's; every path cuts the
effect's tail where the music takes the channel back (800-1300 ms alone,
320-660 ms in the mix); the one chip alters the effect most while it
plays. The parts add up on every path. Open: Whip and Deban keep 0.3 dB
less energy on all four paths alike (the sequencer's side; not traced).

**Proof:** Q's set (qproof.sh, q_before against q_after, this sandbox, g++
13.3, the uploaded Q): every file the same -- the Q attract WAV, audio_check_q,
the trim pass, drum_hits, every fm_probe WAV and report -- but
attract_q.log's own path line and the new fm_steal_B5.txt. The game's set
(b129b against b129a2): identical, 103 PASS, 548 files. `make headless`,
`route_probe` and `patches_check` build with cc alone, as before (the
windowed `ps2` not built here: no SDL3); fm_probe, q_demo, drum_hits and
ps2_headless_q with Q, no warnings. src/ changed: synth.c and synth.h, the
new call alone; `make`.

## Build 129c: the pause flag as the ROM has it

The owner, on the Mega Drive and in emulators: after an effect interrupts
the music, part of the instruments can drop out for the rest of a fight or
a walk, often after pausing and unpausing. Asked whether the port should
keep the original's behaviour under the one chip: "yah at least for the
chip audio".

**Read** (ps2.asm): the driver's frame (SoundDriverInput: nothing at all
runs while paused_mode is set); DoSoundQueue and PlaySoundID (one queue
slot the game writes, priorities); PlaySFX (each effect track lends its
music track -- bit 2 -- and takes the effect slot $90 past it); the stop
of every playing effect a new one begins with (loc_BA656: three slots of
the four); RestoreBGMChn (the lent bit cleared, the music's instrument
uploaded); cfF2_TrkEnd (the only other caller); cfE8_MusPause, in full;
the game's pause (CheckGamePause, GamePausedLoop: the pause and unpause
sounds written straight into the queue, paused_mode cleared by the game).
A music track lent to an effect gets its channel back only when that
effect's track ends or a new effect stops it; if neither happens, it stays
silent until the next song reloads the tracks -- the shape of the dropout.
Which path leaves one lent was not found this build.

**Fixed toward the ROM:** cfE8_MusPause. Its pause walk (nine tracks from
the second music track) tests for an unused track with a branch into the
unpause walk's step (loc_BA0FA): from the first unused track on the rest
are turned back up, the special track is given back instead of lent, and
the flag returns without ending the pause track (its next byte, $F2, does).
The port skipped the unused track and went on turning down (and walked
eight, the special track apart). src/audio/smps.c's `mus_pause` now does
the ROM's two walks, the branch included. Both backends hear it -- the
sequencer is shared -- but on Motavia under its music the pause stays
silent either way (measured; which track is the first unused there was
not looked at).

**Route** pause_driver (a pause on Motavia, 90 ticks in: silent, and the
sequencer's writes): 2642 writes both, hash $F1E4E5B7 on 129b and
$E34E7B33 now; it fails on 129b and passes.

**Proof** (b129c against b129b): every file the same -- no other route
pauses in the game -- 103 PASS then, 104 with pause_driver (4 files).
Q's checks not rerun: fm_probe, q_demo and the Q attract never pause.
src/ changed: smps.c; `make`.

## Build 129d: a check for a lost music part; Q's LIMITED shares it

The owner: "I think sometimes menu sounds cause it", and "it might be cool to
have Q limited do it too if it's not going to create too much work".

**Q's LIMITED needs nothing.** Read in q_synth.cpp (its write queue's
drain): with VOICES LIMITED (take_channels) Q drops a music track's writes
whenever the sequencer did not send them to the chip (`to_chip` false) --
the sequencer's own lent bit, the one that silences the chip. A music part
the driver leaves lent is therefore silent on Q's LIMITED as on the chip,
and turned-down volumes reach Q as the chip's own register writes. EXTRA,
on both, plays the music from the hooks and keeps it.

**The check.** smps.c's `smps_stuck_tracks` (audio.c's
`audio_stuck_tracks`): the music tracks lent (SMPS_F_OVERRIDDEN) while no
effect track that lends them is in use -- PlaySFX lends track 3 for
channels 2 and 3, 4 for 4, 5 for 5; PlaySpcSFX lends 3 to the special
track. The route language's `expect stuck none|$MASK` (docs/harness.md).

**The hunt, first pass** (tools/stuck_hunt.py): from Motavia under its
music, the C menu opened, a cursor moved, a choice made, each followed by
a pause and an unpause at every tick offset 0-25, and the menu played just
after an unpause: 104 orders, none left a part lent. Not tried yet: a
pause's frame-advance (B held, C) with sounds queued in it, fights (their
effects on channels 4-5), the jet scooter's special track, and a part left
turned down (the pause's walk) rather than lent -- `expect stuck` sees only
the lent bit.

**Route** menu_pause_stuck: menu sounds, a pause over them, the unpause;
`expect stuck none` and the music playing again. A guard, not a fix: it
passes on 129c too.

**Proof** (b129d against b129c): every file the same; 105 PASS, 555 files, the
new ones pause_driver's (129c's route, first in a proof set here) and
menu_pause_stuck's. src/ changed:
smps.c/.h, audio.c/.h and probe.c, additions only; `make`.

## Build 129e: the lost-part hunt in the fights

The owner: "In the battle screens the Advanced music would drop instruments."
(In the port's tables a fight plays Rise or Fall, $92, and a boss The Place
of Death, $93 -- battle.c, from BattleScreen; Advanced is $87.)

tools/stuck_hunt.py gained 120 orders in a fight under Rise or Fall
(formation 1, Rolf alone at level 1 so the rounds go on): FIGHT, then at
every sixth tick through the round a pause and an unpause, a pause with
three frame-advances by C, or a pause with four B presses (slow motion),
each ending `expect stuck none` at the next FIGHT/STGY. With the 104 from
129d, 224 orders: none left a music part lent.

So the port does not reproduce it yet. What the port does not model is
the likeliest place: on the Mega Drive the driver runs inside VBlank, which
can fall anywhere in the game's frame -- in a lag frame (a busy fight) the
driver runs again before the game's frame ends, and a sound the game
queues can meet the driver mid-frame -- where the port runs the driver once
after each game tick, always in the same order. Not read: the battle's
frame, whether it lags, and where VBlank can land in it. And a part left
turned down by the pause's walk, rather than lent, is still unchecked.

Nothing under src/ changed.


## Build 130: the manifest's icon, the drum names

The owner chose the queued cleanups: the manifest and the icon, the drum names,
the asset round-trip. This build is the first two. Made in the claude.ai
chat sandbox from the uploaded tree (129e), with the disassembly, art/ and
Q uploaded: Ubuntu 24.04, gcc 13.3.0, Python 3.12 with Pillow 12.1.1, one
core, no SDL3.

**The manifest and the icon** (NOTES, after 129a, item 3). `make manifest`
added `assets/icon/ps2.ico` and `rolf_portrait.png`, which the checked-in
list has never had. The game reads neither: windres takes the icon into the
Windows program (src/platform/ps2.rc) and tools/make_icon.py makes it. So
tools/make_manifest.py leaves `assets/icon/` out (`NOT_READ_BY_GAME`), and
tools/make_pack.py, which took assets/ whole, uses the same test. `make
manifest` now writes the checked-in list byte for byte (791 files), and
`make dist`'s pack is 987 files, not 989: every other entry the same bytes,
51 KB lighter.

**The drum names.** The disassembly names two of the eight drum bytes, in
the drum routine's comments (UpdateDrumTrack, ps2.asm:91039-91041): the one
it pans left "High Tom", the one it pans right "Low Tom"; the three toms are
one sample at three rates, so the byte between them is the middle tom. The
other samples were measured from PCMDrums.bin (the whole sample through one
Hann window, halved as the Z80 halves it; docs/audio.md, "Drums"): a body
near 200 Hz under noise at 2-6 kHz, 189 ms, the snare; 33 ms with 86% of
its energy below 200 Hz, the kick (build 110b's voicing, from the owner's ear);
noise centred near 3.2 and 3.4 kHz, 125 and 42 ms, the open and the closed
hi-hat. The eighth plays past its 18 bytes and no song sends it: "unused".
Measured, not heard.

`src/audio/drums.h` (new): `DRUM_HIGH_TOM` ... `DRUM_CLOSED_HAT` and
`drum_name()`. They name the sequencer's pan choice (smps.c's drum
routine), Q's drum table (q_synth.cpp), drum_hits' report (110b's "short
hard (kick?)" is "kick"), and the `DAC <-` lines of the game's sound log
(PS2_SNDLOG) and of smps_trace --regs, which now end with the name.
`tools/extract_drums.py` carries the names beside its table and `--wav`
writes `drum_84_snare.wav` and so on. The Z80 drum file is not in this
upload, so the extractor was run on a stand-in written from the checked-in
image's six samples at their addresses: PCMDrums.bin the same bytes.

**Proof.** b130 against b129e, both taken here (`tools/proofs/proof.sh
NAME 1`): every file the same -- 104 PASS lines, 552 of 552 route files,
crt, sim, patches, audio, probe_check, the attract WAV 97141d6d..., the
Dark Force trial 25d3ca5f..., no warnings. Q's (`tools/proofs/qproof.sh`,
the Q objects rebuilt here on both sides): 25 of 27 files the same;
drum_hits.txt differs in the kick's label alone and attract_q.log in its
own output path alone. src/ changed: smps.c, audio.c, q_synth.cpp and the
new drums.h; `make`.

## Build 130a: the asset round-trip, and a dead sprite sheet

The third of the queued cleanups (README, "Not done yet": round-trip
verification). `make roundtrip` runs tools/roundtrip.py (new): report on
stdout and in route_out/roundtrip/report.txt, exit 1 on any failure.

**Level 1, re-extraction.** Every extractor of `make extract` runs again,
in the one process, into a scratch tree -- the list and each one's
arguments read from the Makefile's EXTRACTORS and recipes, so the two
cannot drift -- and every file written is compared with assets/ byte for
byte. On this disassembly: 903 of 903 the same. Four files under assets/
come from no extractor: the icon's two, the hand-written patch file
(music/q_patches.json), and PCMDrums.bin, whose Z80 drum file this upload
lacks (the drums' extractor is skipped when it is missing, as `make
extract` does). The manifest is checked against make_manifest.py's rule
applied to assets/ itself.

**Level 2, packing back.** While the extractors run, their writers are
wrapped, so each converted file is paired with the ROM bytes it was made
from: tile sheets and flat pictures (ps2png's write_tile_sheet and
write_flat), palettes (write_palette), block definitions and Tiled maps
(extract_levels' write_blocks_text and write_tmx), collision tables
(extract_collision's write_table). Each checked-in file is then read as
the game reads it -- the PNGs by a reader of its own that takes what
core/png.c takes (colour type 3, depths 1-8, not interlaced), a palette as
palette_load does (the PLTE's first height x 16 entries, each to the
nearest DAC level), a script entry as script.c and window.c do -- packed
back into the ROM's layout and compared. The scripts' ROM side is not
their extractor's: the text is assembled from ps2.asm under the character
map in force at its line (every CHARSET directive, in order). Result: tile
sheets 156, flat pictures 6, palettes 77, block definitions 6, Tiled maps
188, collision tables 102, scripts 611 -- every one the ROM's bytes.

Two differences came up on the way, both read in the ROM:

- The ending's fifth page palette, colour 13, is $05CC: blue 2, green 6,
  red 6 and bit 8, which colour RAM does not hold. The PNG cannot carry
  it. It would matter under a fade -- PaletteFadeIn (ps2.asm:9034)
  compares whole words and steps blue by $200, green by $20 (which carries
  into bit 8), red by 2, so the bit turns a 14-step fade into 22, more
  than the 21 frames the fade runs -- but nothing fades it: the page
  routine puts it straight into the live palette (PaletteLoad2,
  ps2.asm:10907 and 11064) and the sixth page's replaces that line before
  the fade to white. So the bit is never seen. roundtrip.py compares
  palettes under the colour bits and lists such a word as a note only when
  it is in its STRAY_BITS_CHECKED table (this one); any other stray bit
  fails until someone has checked how its palette is faded.
- The miscellaneous block's 19th script ("Remove the Nei-sword from this
  box.") had a zero byte after its end code: the first `dc.b 0` of the
  free space after the last script (`rept $2A8`), which the assembler
  here read as text. It stops at the filler now. The script's own bytes
  match.

**Proved against deliberate edits** (a scratch copy of assets/, one of
each): a tile pixel, a tile sheet re-saved as RGB, a palette colour moved a
level, a script letter, a map block, a collision quadrant and a block
definition word each fail at both levels; a palette colour nudged off the
DAC ladder but nearest the same level fails level 1 only -- the file
changed, the game's data did not.

**A dead sprite sheet.** Level 1 found `assets/tiles/sprite_noah_f1_object_28.png`
written by no extractor. It is the Earthmen leader's art under its old
name: byte-identical to `sprite_earthman_leader.png`, left behind when that
was named, and swept into the manifest by build 112's `make manifest`.
Nothing reads it. Removed; the manifest is 790 files (the start-up line
`assets: 790 files`).

Not covered yet: the JSON tables (level 1 only), and the containers that
hold the ROM's bytes behind a header (.nam, .map, tables/*.bin,
music/*.bin), where level 1 is the whole of it. The compressed art is
compared decompressed; nothing packs it back into the ROM's compressed
form.

**Proof.** b130a against b130 (`tools/proofs/proof.sh NAME 1`): every file
the same -- 104 PASS lines, 552 of 552 route files, crt, sim, patches,
audio, probe_check, the attract WAV, the Dark Force trial. Nothing under
src/ changed. The owner: delete `assets/tiles/sprite_noah_f1_object_28.png`
by hand (a tarball cannot remove it).

## Build 131: the JSON tables packed back (first checkpoint), six lost stairs

The round-trip's rest (NOTES, after 130a): level 2 for the JSON tables,
extractor by extractor. Made in the claude.ai chat sandbox from the
uploaded tree (130 with 130a applied, the dead sprite sheet deleted), with
the disassembly and art/ uploaded. This checkpoint has the stores, the
techniques, gamedata, buildings and interactions; npcs, the level
records, battle.json, the level animations and the map patches follow.

**The ROM side, assembled fresh** (tools/asmdata.py, new). The data tables
are written with symbols -- `dc.b ItemID_Knife`, `nametxt "KNIFE"`,
`dc.l (pal_id_nei_port<<$18)|NeiPortraitArt` -- so reading their bytes
means evaluating the constants as the assembler does, not trusting an
extractor's reading of them. asmdata.Rom reads the constants file lazily;
`id(ptr)` is worked out from the table's own layout (the byte distance
from the `offset` label to `ptr`, over `ptrsize`, plus `idstart`, with the
`:=` values in force on the constant's line), not from the `; $56`
comment or the order of the lines, which is what the extractors use --
the two agree on every item id. It assembles dc.b/w/l, dcb, nametxt with
`length`, cursorbox, border, `even` and the `charset` in force at the line
(the item table sets its own below its label). An address that is not a
constant stays symbolic, one cell a byte of its field, so a pointer
compares by the label it names; a layout distance needs only sizes, so
`dc.w z80ptr(...)` counts without being evaluated. A line it cannot
assemble ends a region read to the first instruction, and fails
anywhere else.

**The packers** (tools/roundtrip_json.py, new; `make roundtrip` runs it as
the last part of level 2). Each reads the checked-in file the way its C
loader does -- json.c's first key of two, a loader's fallback for a
missing or mistyped member, a value cut to the loader's width, a string
cut by snprintf, an item or technique looked up by key in the list the
game looks it up in (the first match, else 0) -- and lays what the game
then holds out in the ROM table's layout:

| Table (ROM label) | File(s) | Loader |
|---|---|---|
| the shop stock table (StoreEquipItemArray) | stores.json | window.c load_stores: six ids a store, at most 32 |
| Shir's steal lists (StealItemArray) | steal.json | building.c steal_item: weapon, armor, item, room, eight each |
| the technique table (TechniqueData) | techniques.json | gamedata_load_tables: name as nametxt (TechNameLength), tp, flags, rate |
| the techniques each character learns (CharAvailableTechniques) | techniques.json | 16 map and 16 battle keys a character, by `key` |
| the item table (InventoryData) | items.json | name (ItemNameLength), price word, action flags, equip mask, attack, defense |
| the characters' names (CharNames) | characters.json | name_from_ascii, four codes |
| the starting equipment and items (CharInitialSetup) | characters.json, items.json | five slots, the count, eight bag slots |
| the level tables (CharExperiencePtrs) | characters.json | 50 rows: level and 24-bit experience, nine stats, the technique nibbles |
| the footwear agility bonuses (AgilityIncreasesArray) | agility_bonus.json | by key; Shoes..GardaBoots, the range the equip routine reads (loc_AD4C) |
| the portrait table (PortraitTable) | portraits.json | game.c load_building_tables: by index, the palette in the top byte, the art as the label its PNG was made from |
| the building music (BuildingMusicPtrs) | buildings.json | by index |
| the portrait windows (WindowArtLayoutPtrs from PtrWin_PortraitStart) | portrait_windows.json | window.c gather_descriptors: position, size and art; the first entry's position and size, the last's art, as the loader's shared buffer leaves them |
| the doors and entrances (LevelData +14's lists) | levels/*.doors.json | walk.c walk_load_doors, each level's file by levels.json's name, the `$FF` after |
| the edge exits (loc_28546) | exits.json | walk_load_exits, three bytes an entry |

The report lists, per file, the fields the loaders read and the ones no
loader reads: `source`, `units`, `id`, `index` where the place is the
index, the store and building names, `music_name`, `to_name`,
`from_level`, `from_name`, `name` on the portraits. levels.json is marked
partly packed (only the index and name, for each level's doors file).
flag_names.json is the port's own names for the story flags, not ROM
data: level 1 only, said so in the report. The JSON files not packed yet
are listed at its end.

A ROM byte the game never reads and the JSON does not carry is *not
packed*, and listed: in the starting equipment table, bytes 5 and 6 of
each character, which SetCharInitialStats (ps2.asm:12475) writes the level
table's technique counts over (character +$25 and +$26), 16 bytes. A
value the ROM's layout cannot hold (a name longer than its field,
experience past 24 bits, a technique count past a nibble, an agility
bonus outside Shoes..GardaBoots, a door at y $FF, which would end the
ROM's list) fails.

**What the ROM said about each difference.**

- *The Control Tower's ground floor: six stairs lost -- fixed.* The doors
  packer came out 66 bytes short. ControlTowerGroundF's interaction list
  (loc_2937C) has 70 entries -- the ROM's lists have no length, only the
  `$FF` after -- and walk.h kept 64 a level, so the loader dropped the
  last six: the stairs up at y 43, x 69, 73, 77, 81, 85 and 89. A
  step into one found nothing, and in a dungeon nothing is the way out
  (loc_6F56 through the exit table): the party left the tower for
  Piata. `WALK_DOORS_MAX` is 128 now, and walk_load_doors says on stderr
  when a file has more than it keeps. Route `tower_ground_stairs`: from
  cell 69,45 up into the stair; on 130a's route_probe it ends in Piata at
  9,24, on 131's on ControlTowerF1 at 70,43.
- *Portraits 28 and 29, palette 44 for 45 -- checked, harmless.*
  PtrPortrait_CentralTowerOutside3 and its copy name pal_id 45
  (PtrPal_CTwOut3Port); portraits.json says 44 (PtrPal_RadarPort), because
  extract_buildings.py maps a palette symbol to its first PalettePtrs
  entry and both entries point at Pal_RadarPortrait. The two entries are
  the same eight bytes (ps2.asm:26919-26922: Pal_RadarPortrait, $FB40,
  eight longs), palette.c loads either into line 2, and
  portrait_radar.png and portrait_radar_copy_1.png are the same bytes.
  In roundtrip_json.CHECKED; printed as a note.
- *AC SLASHR's trailing space -- still a difference.* The item table is
  one byte out: the ROM's name is `nametxt "AC SLASHR "` (ps2.asm:28392,
  "Should be LAC SLASHR"), ten codes, the last a space;
  extract_gamedata.py's decode_name strips trailing spaces, so items.json
  has nine and the packer puts $C4 where the ROM has $00. Not harmless:
  Script_ProcessItemEnemyNames (ps2.asm:25624) copies up to ten codes,
  stopping only at $C4, so the ROM's dialogue prints the space ("AC
  SLASHR ." where the item ends a sentence) and window.c's text_put_item
  does not. The shop and bag lists are the same either way (put_item_name
  draws a blank past the name). `make roundtrip` fails on it until 131a,
  which keeps the space and adds a route command that can read a
  window's text.

**Proved against edits** (`make roundtrip-edits`, new: one field a table
changed on a scratch copy of assets/): a store's item, a steal pick, a
technique's TP, a learned technique, an item's price, a character's name,
a starting slot, a level row's luck, a boots bonus, a portrait's palette,
a building's music, a portrait window's width, a door's f764 and an exit's
arrival x each fail at both levels -- level 1 by the table's extractor run
fresh, level 2 by a difference the unedited table does not have.

**Proof.** b131 against b130a, both taken here (`tools/proofs/proof.sh
NAME 1`): 104 of the 105 PASS lines as 130a's, 552 of 552 of 130a's route
files the same, and the new route's four files; crt, sim, patches, audio,
probe_check, the attract WAV 97141d6d..., the Dark Force trial
25d3ca5f..., no warnings. src/ changed: game/walk.h (WALK_DOORS_MAX) and
game/walk.c (the warning); `make`.

## Build 131a: AC SLASHR's space, `expect text`, the townspeople's tables

The round-trip's second checkpoint (after 131): the difference 131 left
open, and extract_npcs.py's tables.

**AC SLASHR -- fixed.** extract_gamedata.py's decode_name no longer strips
a name's trailing spaces. One name has one: AC SLASHR (`nametxt "AC SLASHR
"`, ps2.asm:28392). `make extract-gamedata` changes that line of items.json
alone -- characters.json and techniques.json, which share decode_name,
come out the same. The dialogue now prints the space the ROM prints
(Script_ProcessItemEnemyNames copies up to ten codes, stopping only at
$C4); the store and bag lists look as before (put_item_name draws a blank
past a name).

**`expect text "..."`** (route_probe, new). No route command could read a
window's text. window.c's win_debug_text copies the codes load_script
last expanded, names and all, up to the end code; game.c's
game_debug_text hands them to the probe, and `expect text` looks for its
string in them, written as the script files write text -- a character by
win_charset, `{XX}` a code in hex (the port puts two `{C1}` a line break).
A failure prints the window's text the same way. docs/harness.md has it
in the command table.

Route `ac_slasher_name`: Aukba's weapon store (store 18), the fourth item.
On the old items.json the window read `AC SLASHR{47}{47}{47}ok.` (FAIL);
on the new, `AC SLASHR {47}{47}{47}ok.` (PASS, 161 ticks, one more than
before: the space is printed).

**The ROM reader** (tools/asmdata.py): expressions in AS's order, not C's
-- the bit operators bind tighter than `+ -` and `* /` -- which
LevelSpriteData's `(ObjID_X-$14)<<2+1` needs (the shifted id plus one);
symbols matched without regard to case when the exact one is missing, as
AS does (ItemID_Crystanish is `id(Item_crystanish)`, the label
Item_Crystanish); `bra.w` and `bsr.w` sized (four bytes) so the ObjID_
constants' id() can be measured over ObjectTable; `cells_from`, so a
mapping frame reads only its own bytes. Every table of 131 packs as
before.

**The townspeople's tables** (roundtrip_json.py, 131's shape):

| Table (ROM label) | File | Loader |
|---|---|---|
| the chest contents (TreasureChestContentArray) | chests.json `contents` | npc.c load_chests: by index, $8000 \| the item's id, meseta, or 0 for garbage |
| who stands where (LevelSpriteData) | level_sprites.json `levels` | npc_load_tables: per level, (object - $14) << 2 \| interaction, index, x, y, the zero word after; at most 32 a level, a second entry for a level adding to the first |
| each level's art list (LevelData +18 bits 0-6) | level_sprites.json `art_list` | the list npc_spawn takes; bit 7 is the parallax flag, the level records' |
| the people's art lists (loc_283EE) | level_sprites.json `art_lists` | the count less one (LoadLevelData's dbf, ps2.asm:13305), then each art's label and VRAM address; an entry whose PNG will not load is left out, as art_index does |
| the townspeople's dialogue numbers (loc_E6B0, loc_E70C, loc_E730) | people_dialogue.json | read_bytes, 256 a table |
| the townspeople's types | npc_types.json | the first 16, per object the first: tile word, first wait, the wandering routine, 20 frames of up to two pieces |
| the scene objects | scene_objects.json | the first 32: tile word, box, front, 12 frames of up to 47 pieces |
| the chest objects | chests.json `types` | tile word, box, two frames, and the Motavian chest's level-type-1 frames |

The last three are not one ROM table each. A type's sprite mappings are
(each frame's piece count and five bytes a piece, in the order the
mapping's offset words give them; the offsets themselves are layout, not
packed), but its tile word, first wait, hit-box row and front bit are
operands of its init's instructions, and its wander the routine its
second branch names. The packer reads those fresh from ps2.asm -- the
object's ObjectTable branch, its `jsr X(pc,d0.w)`, the first branch of X,
the init to the next label, bra or rts -- and lays them out with the
mappings as a table of their own. The tile word packs as palette_line <<
13 | tile, so a priority or flip bit the JSON cannot carry would show:
none has one. Nor has any mapping piece a palette or priority bit, nor a
size bit outside the width and height. "wander" packs as the ROM routine
npc.c ports under that name: vertical loc_3C06, horizontal loc_3DCA,
four loc_42F8, dezolian loc_402C, muskcat loc_45A0.

Every one the same: `make roundtrip` has 22 of 22 JSON tables, 0
failures. The caps held: no level has more than 32 people, no
type more frames or pieces than npc.h keeps. Not packed (annotations):
the types' `name`, `source` on the levels and art lists, the levels'
`name`, the dialogue's `bank`.

**Proved against edits** (`make roundtrip-edits`): a chest's meseta, a
person's x, a level's art list, an art list's tile, a dialogue number, a
townsperson's frame piece, Darum's tile and the Motavian chest's
level-type-1 frame each fail at both levels -- with 131's fourteen, 22
of 22.

**Proof.** b131a against b131, both taken here (`tools/proofs/proof.sh
NAME 1`): every file the same -- 105 PASS lines, 556 of 556 route files,
crt, sim, patches, audio, probe_check, the attract WAV 97141d6d..., the
Dark Force trial 25d3ca5f..., no warnings -- and the new route's PASS line
and three files. No other route shows AC SLASHR's name in a window. src/
changed: game/window.[ch] (win_debug_text), game/game.[ch]
(game_debug_text), harness/probe.[ch] (`expect text`); assets/: items.json
(`make extract-gamedata`, shipped); `make`.

Not ROM data, listed as level 1 only: flag_names.json (131) and
music/q_patches.json, Q's hand-voiced patch file (build 112). JSON files
outside handover_131's list, not packed yet and named at the end of the
report: songs.json, nametables.json, paletteptrs.json, palettes.json,
script.json, winart.json.

## Build 131b: the level records packed back

The round-trip's third checkpoint (handover_131b, task 1): the level
records (LevelData, 20 bytes a level). Made in the claude.ai chat sandbox
from the uploaded tree (131a), with the disassembly and art/ uploaded.
Nothing under src/ changed.

**Where the game reads a level record.** Not from
`levels/<nn>_<name>.json`. LoadLevelData (ps2.asm:13229) reads the
record's 20 bytes: +0 the planet (bit 7) and the tileset (bits 0-3, also
the level type), +1 the height and width nibbles ($100 pixels each), +2
and +6 the plane B and plane A block maps, each under the enemy group byte
the random battles roll from before and after Climatrol, +10 the collision
table under the flag byte (bits 0-3 the camera clamp, 2 and 3 the backdrop
planes, 4-6 the collision plane), +14 the interaction list under the
palette byte, +18 the extra-art byte (bit 7 the parallax flag, bits 0-6
the people's art list), +19 the music. Nothing else reads the record's
flag or palette bytes later: the collision pointer is only ever used as an
address (the walk's collision test, loc_6DA4 onward), and the jet scooter
swaps its own table in by address (ObjJetSctr_Init, loc_D9DA). The
port's loaders split the same fields:

| Field | Read by | From |
|---|---|---|
| planet, the tileset's two files, the stem | game.c load_level | levels.json: the last entry with the level's `index`, its `planet`, `name`, `files.blocks`, `files.tiles` |
| tileset, palette, music, flags, both enemy groups, size, index | level.c level_load | the level's `.tmx` properties, by strstr and atoi (a scanner, not XML) |
| the two block maps | level_load (tmx_layer) | the `.tmx`'s planeB and planeA layers, or `<stem>_A.tmx` / `_B.tmx` when the flag byte makes that plane a backdrop |
| bit 7 of the extra-art byte | walk.c walk_begin | the `.tmx`'s `extra_art` (its bits 0-6 are read and not used) |
| bits 0-6 of the extra-art byte | npc.c npc_spawn | level_sprites.json's `art_list` |
| the collision table | walk.c walk_load_collision | `<stem>.collision.txt` |
| the interaction list | walk.c walk_load_doors | `<stem>.doors.json` |

The per-level `.json` is read only when a `.tmx` has no `extra_art`
property (a map from before build 46), and every one has it: the file is
the extractor's description of the record -- labels, sizes in pixels, the
music's name -- and is now said to be that in the report (level 1 only),
no longer listed as not packed yet.

**The packer** (roundtrip_json.py, `level_records`): every level in the
ROM's order, read as those loaders read it, packed to 20 bytes and
compared with LevelData assembled fresh by asmdata.Rom. A `.tmx` is read
by TmxDoc, which is level.c's scanner in Python -- the first `<property
name="X"`, the first `value="` after it, atoi; the first `name="planeB"`,
its width and height, the numbers after `<data...>`, each gid less one
with 0 left 0 and Tiled's flip bits masked -- and records the properties
and layers asked for, so the report lists them beside the JSON files. The
collision file is read as walk.c's load_table reads it (sscanf's `%x %4s
%4s`, 256 blocks).

A pointer packs as the ROM label whose data is what the game reads from
the level's own file: the block map whose DecompressArt2 output starts
with the layer's blocks, the collision table the file loads as (nothing
set past the ROM table's end), the interaction list the doors file loads
as. Where two labels hold the same data the record's own is taken; none
of the 108 maps, six collision tables or 99 interaction lists has a twin.
The data themselves are compared at level 2 already (the Tiled maps, the
collision tables, the doors); this is which one each level points at, and
each pointer compares with its flag byte in its top byte, as the records
write `$41000000|loc_4A370`.

It fails, besides a difference: a `.tmx` index that is not the level's
place (the ROM's index is the place), a size that is not a nibble's (8 to
120 blocks by 8), a planet past one bit, a field past its byte or nibble,
tileset files that are another tileset's than the level type (the record's
one nibble picks both), a plane of another size than the record's (16 x 8
for a backdrop), a missing `.tmx` or layer (the level does not load), or a
file whose data is none of the ROM's.

**What the ROM said.** 2020 of 2020 bytes the same, no difference to read.
The bits LoadLevelData never reads -- bits 4-6 of the first byte (it
takes bit 7 and bits 0-3) and bit 7 of the flag byte (it takes $0F and
$70) -- are clear in all 101 records, so none needed a hole.

**Per field, ROM data or the port's own.** levels.json: `index` is the
record's place; `name` the port's (the ROM label less `Level_`), which
names every file of the level; `planet` ROM data; `files.blocks` and
`files.tiles` the port's file names, checked to be the level type's
tileset (asset_names.TILESET_NAMES, which the extractor names them by);
every other field of a level is a copy of what the `.tmx` holds, or a map
file name the game builds from the stem, and not read; the `tilesets`
block describes the tileset pointers (loc_9342) -- labels, counts,
compressed sizes -- and is not read (the tile sheets and block
definitions themselves are level 2's). The `.tmx` properties are ROM data
bar `index` (the place), all read. A backdrop's `_A.tmx` or `_B.tmx`
carries its level's properties too; level_load reads only its layer, and
the report says so.

**Proved against edits** (`make roundtrip-edits`): Paseo's `.tmx`
palette 5 -> 6 fails at level 1 (the extractor writes 5) and at level 2
(the interaction pointer's top byte); with the others, 23 of 23 edits
caught. The extractor behind it, `make extract-levels`, is now in
roundtrip_json.py's extractor_cmd.

**The report's rest.** roundtrip.py's end list now names only battle.json,
level_anims.json, map_patches.json and the six files outside
handover_131's list. roundtrip_json.py gained FIELD_NOTES (why a file's
unread fields are unread, where the list would not say) and NOT_READ
(files that describe ROM data the game reads elsewhere); the doors
packer's list reading is its own function (game_door_list), shared with
the records' interaction pointer.

**Also.** the owner's uploaded tree still had
`assets/tiles/sprite_noah_f1_object_28.png`, which build 130a retired:
`make roundtrip`'s manifest check fails on it (790 listed, 791 found).
It is dead -- no extractor writes it, the game does not list it -- and a
tarball cannot delete it: remove it by hand.

**Proof.** b131b against b131a, both taken here (`tools/proofs/proof.sh
NAME 1`): every file the same -- 106 PASS lines, 559 of 559 route
files, crt, sim, patches, audio, probe_check, the attract WAV
97141d6d..., the Dark Force trial 25d3ca5f..., no warnings. Nothing
under src/ changed: tools/roundtrip_json.py, the docs.

## Build 131c: battle.json, the level animations, map patches, palette entries and script banks packed back; two bosses' pictures and sixteen-piece frames

The round-trip's fourth checkpoint (handover_131b, task 2): battle.json,
the largest table file, read as battle.c's battle_tables_load reads it and
packed into the ROM's tables. Made in the claude.ai chat sandbox.

**The packers** (roundtrip_json.py, fourteen tables):

| Table (ROM label) | Loader's rule | Result |
|---|---|---|
| the enemies (EnemyData) | g_enemy by `id` (i + 1 if absent), 64 bytes as loc_8D4 reads them: 15 palette words (hex strings by strtoul), the art (the Battle_*Art label its `art_file` PNG was written from), EXP, meseta, attack, defense, type, agility, HP, technique, rate, success index, escape, object ($F if absent), duration, four animation bytes, two sounds, the map set by label, `anim` | 7938 of 8064; the 64th byte of each record not packed (loc_8D4 reads 63; nothing reads the id's copy) |
| the enemies' names (EnemyNames) | ten codes by win_charset, $C4 after a short name | 1260 of 1270; entry 0 not packed (no enemy 0: battle_enemy_name(0) is the port's own blank) |
| the enemy formations (EnemyBattleFormationData) | five bytes, at most 260 | 1295 of 1295 |
| the encounter groups (EnemyFormationTable) | eight bytes, at most 96 | 672 of 672 |
| the encounter regions (loc_23C3A-loc_23DBA) | read_bytes, 96, 96, 96, 96, 64 (the two late tables loc_23CFA's halves) | 448 of 448 |
| the enemies' x positions (EnemyXPosArray) | five words a layout, at most 16 | 100 of 100 |
| the party's battle positions (Battle_CharCoordinates) | eight words | 16 of 16 |
| the party's battle sprites (Battle_CharObjectPtrs) | the map set with the palette line in bits 29-30, the art label | 64 of 64 |
| the battle mappings (every Battle_*Map, Map_*Battle, Map_Weapon*, Map_Tech*) | read_maps over maps, effect_maps, sprites (96 sets, the first with a label): a frame of pieces (count, then y, size, tile word, x) or a nametable block (x, y, w - 1, h - 1, the cells), in the order of the table's offset words | 90017 of 90017 (after the fixes below) |
| the battle animation scripts (the eight tables Battle_AnimateSprites runs) | each table's offset words (a key names a label or label+N), then each entry's script to its end code | 13143 of 25497; 12354 not packed (past each end code) |
| the weapons' effects (WeaponProperties) | three bytes, then two effect longs (art in seven bits, palette flag in bit 7, over the map set) | 407 of 444; the fourth byte not packed (loc_1F30 steps over it with `addq.w #1, a6`, and the port's loader reads three) |
| the techniques' effects (TechniqueProperties) | two bytes, then the two longs | 520 of 520 |
| the effects' art lists (WeaponTechArtPtrs' lists) | loc_608A's list: count less one, then art label and VRAM address each, two a list at most | 280 of 280 |
| the fight's small byte tables (EnemyTechSuccessRate, Battle_CharTargetChances, loc_34EE) | read_bytes, 32, 8, 28 | 68 of 68 |

Where the ROM table's shape is the file's -- a mapping frame's length, a
script's -- the ROM side is read at the packed side's length
(Packed.rom_side, new): a frame of the wrong kind or a count that differs
shows as a difference, and bytes the reader cannot assemble read as None,
never as a shorter table.

**Where a script ends.** The extractor keeps 256 bytes from each script's
label, which runs on into the next scripts and, for the last of a table,
into whatever follows (effect script loc_27656 runs into
WeaponProperties, whose symbolic longs the extractor's reader skips, so
its held bytes past 114 are not the ROM's). Battle_AnimateSprites
(ps2.asm:4727) reads a script a byte a step: below $80 a frame, $F7 with
one argument byte, $F6 and $F8-$FD and $80-$F5 on their own; $FF resets
the step to 0 (loc_32C2) and never reads on; $FE sets bit 0 of the
object's byte 2 (loc_32EE), which RunObject answers by clearing the
object's 64 bytes (loc_3DC, loc_6D38) -- battle_obj.c's R_DESTROY. So an
entry's script is packed to its first $FF or $FE at a command position;
what the game holds after that is not packed. 334 of the 335 distinct
scripts end on their label's last byte; loc_270E8 (the enemies' second
overlay, entry 126) ends on a $FE with bytes after it that no entry
points at.

**Found and fixed: the bottom of Dark Force's and Mother Brain's
pictures.** A boss's picture is a nametable block (loc_572): Dark
Force's 40 x 15, 1206 bytes a frame, Mother Brain's 40 x 14.
extract_battle.py read at most $400 bytes of a frame, so the cells past
that came out 0 -- 10 in each of Dark Force's four frames (rows 13-14,
the tip of his jaw) and 43 of Mother Brain's (rows 12-13, the waves
along her base). read_enemy_maps now reads a block as long as its size
says. `make extract-battle` changes those two maps in battle.json alone
(shipped).

**Found and fixed: frames of more than sixteen pieces.** battle.h kept
16 pieces a frame (PIECES_MAX); seventeen frames have more -- Dark Force's
frame 28 has 36 and his 26-27 have 20, Gra's strike 24 to 28, and frames
of the Fire Eye, Gen, Deban, Zan, Tsu, Gra's cast and Gaj's cast 17 to 22
-- and read_frame dropped the rest. BuildSprites takes a frame's whole
count; the 80-sprite cap is the screen's (video.c). PIECES_MAX is 40 now,
and read_frame says on stderr when a frame has more than it keeps.

Route `battle_big_frames`: Rolf at L40 against Dark Force (formation
$102). The first shot checks the tip of his jaw (169,109, $220); then Gra
from the second page of his list, and 60 ticks after FIGHT a bolt at
245,131 ($C00) that the dropped pieces carried. On 131b's route_probe and
battle.json it fails at the jaw; on 131b's route_probe with the new
battle.json, at the bolt; on 131c's it passes (219 ticks).

**The report.** battle.json's keyed members are named once (`<map set>`,
`<script>`, DICT_PATHS). Packed: every field the loader reads. Not packed:
`anim` (the loader takes the idle scripts from `anim_tables.idle`), the
`label` of every enemy, weapon, technique and art list, `enemies[].art`
(the label beside `art_file`), `enemies[].id_copy`, `parts[].art`,
`inline_art` and `effect_art_inline` (the extractor's lists for
extract_inline_art.py).

**Proved against edits** (`make roundtrip-edits`): an enemy's HP, a name,
a formation's count, a group's formation, a region byte, an x position, a
battle position, a palette line, a mapping piece's x, a script byte, a
weapon's sound, a technique's art, an art list's VRAM address and a
target chance each fail at both levels.

**The level animations and the map patches** (handover_131b, task 3).
Neither is a ROM table: each is the operands of one routine, which
extract_levels.py writes out as constants. As 131a's object types, the
ROM side is read fresh from the routine's lines and laid out beside what
the file packs to.

- *The level animations* (level_anims.json; game.c tile_anims_load).
  loc_118F4, the level screen's VBlank, read branch by branch: the level
  (a `bne` on level_index is level 0; `cmpi.w #$13, d0`), the wait
  (`move.w #$1F, (a2)`: a period of $20), the step mask (`andi.w #1` /
  `#3, d1`) and for the Underwater Passage the step table loc_11978
  ($00 $02 $04 $02 -- the one ROM table here), `lsl.w #8` making each
  step a byte offset into the art, the art's label, the VRAM address
  from the VDP command ($0020 and $21C0: tiles 1 and $10E) and the
  frame's longs from the dbf ($40 and $80: 8 and 16 tiles). Packed from
  the file: level, period, tile, the frame's bytes (its PNG's tiles by
  32, as tileset_load counts them), the art (the label the frame files
  were named from, asset_names.LEVEL_ANIM_NAMES, frame k the art's k-th)
  and each step's offset (the frame number by the frame's bytes). 34 of
  34 bytes. `source` is not read. The frames' pixels are level 2's tile
  sheets.
- *The map patches* (map_patches.json; game.c level_map_patches).
  loc_9258-loc_9300 read instruction by instruction: the level tests,
  the flag tests (the first under a level holds for the rest of it:
  Motavia's all sit under $C735, loc_92CA being the section's rts), the
  Prism test (`moveq #ItemID_Prism, d2`), the replace loop ($8B to $89
  over $1800 bytes of plane B), each `move.b` into a plane (the place
  its address falls in: $FF9000 plane B, $FFA800 plane A), each run list
  (`lea (L).l, a2`), with loc_9302's row stride ($60) and block ($87).
  Laid out per patch: level, the flags, the item, the plane, then the
  replace, the set or the run list (its label and its entries to the
  $FF, which are a ROM table). The file's runs are matched to the run
  list whose bytes they are (`source` is not read); a flag the loader's
  sscanf cannot read or outside $C700-$C7FF fails (the patch would never
  apply). 465 of 465 bytes.

**The JSON files outside handover_131's list** (task 4), each read or
not by the game:

- *palettes/paletteptrs.json* -- read (palette.c read_entry_table: each
  entry's `index` and `file`; palette_load_entry loads the file into the
  line its caller names). Packed as PalettePtrs: the palette's address as
  the ROM label whose colours the file holds (palette_load's reading, the
  colours' bits only -- ending page 5's stray bit is level 2's note; the
  entry's own label where several palettes are the same), the count of
  longs less one from the file's colours; the colour RAM address is a
  hole (the port's callers pass the line). 462 of 616, 154 not packed.
  `symbol`, `dest`, `words`, `line`, `entry`, `name` are not read.
- *script/script.json* -- read (game.c script_bank: each member's `bank`
  and `file`). Packed as GameScriptPtrs: each bank's pointer as the ROM
  label its file is named by (extract_script.py writes a block under its
  label). 104 of 104. `count` is not read; the text is level 2's scripts.
- *music/songs.json, nametables/nametables.json, palettes/palettes.json,
  winart.json* -- read by no loader: decoded listings for reading (the
  songs' headers, the nametables' sizes, the named palettes' words, the
  window maps as rows); the game reads music/*.bin, the .nam and .map
  files and the palette PNGs, which levels 1 and 2 cover. Listed in the
  report as level 1 only (NOT_READ). All four are in the manifest and
  preloaded all the same.

With these the report's closing list names no JSON file: every one is
packed or said to be level 1 only, and a new one would be named there.

**Proved against edits**, all together: the fourteen battle edits, a
level animation's step, a Dezolis door's place in the map patches, a
palette entry's file and two script banks swapped each fail at both
levels -- with the 23 before them, 41 of 41.

**Proof.** b131c against b131b, both taken here: 106 of 106 PASS lines
the same, and the new route's; route files 559 and 564 -- the new route's
five, and five changed, each by the fix: noah_box's 04_dark_force.png
(474 pixels, his jaw) and mother_brain_battle's 02-05 (her base's waves;
2338 pixels in 03). crt, sim, patches, audio, probe_check, the attract
WAV 97141d6d..., the Dark Force trial 25d3ca5f... (its fights take the
same turns: the pieces only draw) the same, no warnings. src/ changed:
game/battle.h (PIECES_MAX), game/battle.c (the warning); tools:
extract_battle.py, roundtrip_json.py, roundtrip.py (the closing line);
assets/data/battle.json; `make`. The packers for tasks 3 and 4 came
after that proof and are tools only.

## Build 132: a note sounding on through a pause

The owner, on 131c: "a lot of synth note sustain when I push pause now on both
q and opn synth" (voices extra, tails fade); then, on 132's first report,
"it still happens with extra or limited".

**Read** (ps2.asm): the driver's frame (SoundDriverInput), UpdateTracks, the
four FM track paths (ProcFMTrack and its slide and raw-frequency variants),
DoNoteStop, the pause flag (cfE8_MusPause), the track end (cfF2_TrkEnd), the
sound queue (DoSoundQueue), PlaySoundID, PlaySFX, the effect stops (loc_BA656,
StopSFX), the hand-back (RestoreBGMChn), SetMaxRelRate, SilenceFM, SetFMIns,
RefreshVolume, DoNoteOn_A/DoNoteOff_A/FMNoteOff and the write routines
(WriteFMIorII, WriteFMIIPart, WriteFM1Main); the game's pause (CheckGamePause,
GamePausedLoop); the pause and unpause jingles' data (SFX_Pause,
SFX_Unpause). Where the port matches: the track end clears the flags before
its note-off, so the key-off always reaches the chip; the hand-back clears
the lent bit and uploads the music part's instrument with no in-use test;
the upload writes the same 26 registers in the same order (loc_BA818),
keeps the same six, skips the chip while lent and refreshes the volume when
it is not zero; the pause flag's walks are 129c's.

**Measured** (route_probe with a temporary per-channel tap in fm.c, removed):
under VOICES EXTRA the tail is on the *music* chip's FM4. On the tick before
the pause takes hold, music slot 3's chord strikes a pad note across FM3,
FM4 and FM5 -- a slow attack, a decay over seconds. The pause jingle has
lent FM4 and FM5, so the one chip never gets those two key-ons; EXTRA's
music chip does (the hooks carry a lent part's writes there, the design:
the music loses no note under an effect), and so do Q's voices. The pause's
walk turns down only parts in use and not lent, as the ROM's does -- on the
Mega Drive a lent part is silent, its channel the effect's -- so on the music
chip the pad rang on: -31.4 dBFS 30 ticks into the pause, -36.0 at 90. Under
LIMITED (the one chip, the ROM's order) the same pause is -45.3 at 30 ticks
(the jingle's own release) and silent from 60, at four points in the song.
Before the fix, EXTRA, 30/60/90 ticks in: Shure -31.4/-32.6/-36.0, Aukba
-33.3/-38.2/-42.6, the dams -41.0/-49.0/-48.1 (Aukba and the dams are under the
probe's -40 line, so only Shure's route can fail).

**Fixed** (the port's own, not the ROM's): the pause flag first keys off
every music part lent to an effect (smps.c `mus_pause`). The part is lent,
so the key-off goes to the hooks alone (`to_chip` false): the one chip's
writes, LIMITED and the write hash are as before, and EXTRA's music chip and
Q hear the pause the Mega Drive plays. The part's next strike after the
pause sounds as ever. After, EXTRA, 30/60/90 ticks in: -47.6/-142/-258 in all
three places (the jingle's release, then nothing); LIMITED unchanged to the
tenth. Q (a Q build of route_probe, the route with `synth q`): -39.6 dBFS 90
ticks in before, -180 after.

**Read, not ported -- for the owner.** SetMaxRelRate (called only from PlaySFX,
with the effect's header in place of a track) never reloads its register:
WriteFMIorII adds the channel's offset into it on every pass. On FM1 and FM4
(offset 0) it writes the four release rates; on FM5 it writes FM5 op1's
release rate, FM6 op3's, an unused slot, and `$FF` into FM4 op1's SSG-EG
register ($90, part II); on FM3, FM3 op1's, FM1 op2's, FM3 op4's and FM1
op3's SSG-EG ($94). Nothing else in the driver writes $90-$9E (SetFMIns's list
stops at $8C). So on the Mega Drive, from the first effect on FM5 (the pause
jingle's echo is one), FM4's operator 1 runs with SSG-EG $F for the rest of
the session, FM1's operator 3 likewise after an effect on FM3. The port
writes the four release rates alone. Not the pause's tail (LIMITED is silent
without it); porting it would change FM1's and FM4's timbre for the whole
game, his call. The release rate on the effect's own channel changes nothing
lasting: the effect's first act uploads its instrument before its first
key-off.

**Also found:** 129d's check -- `smps_stuck_tracks`, `expect stuck`, route
menu_pause_stuck -- is not in this tree; the proofs show it lost between
129e and 130 (104 PASS at 130). tools/stuck_hunt.py is still here and needs
it.

**Route** pause_tail (Shure, EXTRA, `expect sound silent` 90 ticks into the
pause): -35.5 dBFS and a FAIL on 131c, PASS on 132.

**Proof** (b132 against b131c, both taken here, `tools/proofs/proof.sh NAME
1`): every file the same -- 107 PASS lines, 564 route files (pause_driver's
2642 writes and hash $E34E7B33 among them: the new key-offs are hooks, not
chip writes), crt, sim, patches, audio, probe_check, the attract WAV
97141d6d..., the Dark Force trial 25d3ca5f... -- and the new route's PASS line
and three files. `make roundtrip` not rerun: nothing it reads changed.
src/ changed: smps.c; `make`.

## Build 132a: the lost-part check back, the townspeople and the mid-step read, the JSON listings

The owner: Rise or Fall is "definitely the primary song" for the lost music
part; restore the check; the release-rate quirk "would be cool"; are there
townspeople who start scenes; what is the mid-step about; fix the JSON
listings.

**The lost-part check, restored** as Build 129d describes it (the code was
not in the tree since 130): smps.c `smps_stuck_tracks` (the music tracks
lent to an effect while no effect track that lends them is in use: tracks
6-8 lend 3-5, the special track lends 3), audio.c `audio_stuck_tracks`, the
route language's `expect stuck none|$MASK` (probe.c, `C_EXPECT_SOUND` sub 3),
route menu_pause_stuck. A guard, not a fix: it passes; `expect stuck $08`
on a clean pause fails reporting $00. tools/stuck_hunt.py runs again.

**Townspeople who start scenes** (read: ObjType_People): four dialogue
numbers go to scenes of their own -- $85 Roron's cake (loc_E5FE), $87 the jet
scooter's maker's gift (loc_E690), $88 the Esper mansion's door Espers
(loc_E05A), $8F the two Espers on B1 (loc_E07A) -- and the rest from $80 up to
loc_E5D0. All five are in game.c (builds 74, 85-87). README's item was stale:
moved to its done list.

**The mid-step** (read: LevelScreenLoop, ProcessAButtonPress,
Level_ChkTargetInteract): the level loop calls ProcessAButtonPress every
frame, with no test of the party's step, and its target test is a box ahead
of the leader's pixel position. On the Mega Drive A answers mid-step; the
port drops it until the step ends. README's item now says so; not changed
yet (the port's hit test would have to work from the pixel position).

**The JSON listings**: songs.json, nametables.json, palettes.json and
winart.json are read by no loader (src/ names none); extract_art.py reads
palettes.json. Kept in assets/; make_manifest.py's NOT_READ_FILES leaves them
out of the preload and, through the same rule, of `make dist`'s pack and of
`make roundtrip`'s manifest check. Manifest 790 -> 786.

**Proof** (b132a against b132): every file the same, and the new route's
PASS line and three files (109 PASS). src/ changed: smps.c/.h, audio.c/.h,
probe.c; `make`. assets/manifest.txt regenerated (`make manifest` gives the
same).

## Build 132b: SetMaxRelRate as the ROM writes it

The owner: the release-rate quirk "would be cool if you figure it out".

**Read** (ps2.asm): SetMaxRelRate, WriteFMIorII, WriteFMIIPart, WriteFMI,
WriteFMII, StopZ80 (it leaves d0 alone), PlaySFX's call (a3 at the effect's
header), SetFMIns's register list (it stops at $8C), SilenceFM (key-offs and
total levels only). The loop adds 4 to d0 and never reloads it; the write
routine adds the channel's offset into d0 on each pass. So on FM1 and FM4
the four release rates; on FM5, FM5 op1's release rate, FM6 op3's, a slot no
channel has, then $FF into FM4 op1's SSG-EG ($90, part II); on FM3, FM3 op1's,
FM1 op2's, FM3 op4's, then FM1 op3's SSG-EG ($94). Nothing else writes
$90-$9E, so on the Mega Drive FM4's operator 1 keeps SSG-EG $F (hold,
alternate, inverted) from the first effect on FM5 -- 51 of the 56 effects,
the menu cursor's among them -- for the rest of the session, FM1's operator 3
from the first of the 18 on FM3.

**Ported** in smps.c `set_max_rel_rate_channel`, drift and all; the hooks
get each write where it lands. The chip plays it under either VOICES (under
EXTRA the SSG-EG lands on the music chip's channel, and an effect claiming
FM4 later is seeded with it from the shadow). Q is unchanged: Rise or Fall
with the menu sound at 1 s and an FM3 effect at 2 s, Q's PM voicing, before
and after, bit for bit the same -- Q voices neither SSG-EG nor those stray
release rates.

**Heard** (fm_probe, the chip LIMITED, 24 s, the menu sound at 1 s and
effect $A3 at 2 s): the level the same (-22.1 dB), the difference after the
effects -31.1 dB in Rise or Fall (9 dB under the music: a timbre change on
its FM4 and FM1 parts) and -47.4 dB in Advanced (24 dB under). Listening:
rise_or_fall_chip_before_132b.flac / _after_, advanced_shure_chip_before_132b
.flac / _after_.

**Routes.** pause_driver's record of the sequencer's writes: the same 2642,
the hash $E34E7B33 -> $1DF0A957 (the jingle's echo on FM5 now writes $81, $86,
$8B, $90 for $81, $85, $89, $8D); its expectation updated, PASS. Five more
pass with their logs moved: audio_effects, audio_oversample, audio_patches,
audio_voices and title_under_menu each measure the title theme after an
Escape-menu sound (an effect on FM5 or FM3), and its peak moves 0.1-0.6 dB
(e.g. audio_voices -17.0 -> -16.4, title_under_menu -15.7 -> -15.1).

**Proof** (b132b against b132a): every other file the same -- crt, sim,
patches, audio_check, probe_check, the attract WAV 97141d6d... (no effect in
it), the Dark Force trial 25d3ca5f... -- and the files above. src/ changed:
smps.c; `make`.

## Build 132c: the lost music part, found -- the unpause's walk taken over

The owner: the FLACs of 132b sound much the same; "the thing that triggers the
dropped instruments most frequently is pausing and unpausing".

**Read** (ps2.asm): cfE8_MusPause in full, CheckGamePause and GamePausedLoop,
and both jingles' bytes (from the sequencer's image). The pause jingle
($D5): FM4 plays the phrase, then E8 01 (pause) and F2; FM5 echoes it two
ticks behind. The unpause jingle ($D6): FM4 echoes; FM5 plays the phrase,
then E8 00 (unpause) and F2. The pause's walk turns each music part in use
and not lent down ($FF - volume onto its carriers); the parts' stored volumes
are untouched, so a part stays down until something refreshes it at its own
volume -- the unpause's walk, an instrument upload, a volume command. The
unpause's walk is in one place only: the end of the unpause jingle's FM5
phrase, about 30 ticks after Start. GamePausedLoop clears paused_mode
itself, so the game runs on at once.

**The mechanism**: an effect on FM5 started inside that window (51 of the 56
effects are on FM5, the selection sound A and the menus play among them; in
a fight, the battle's own) takes the unpause jingle's FM5 track over, and
the walk never runs. The parts the pause turned down stay down -- in a
looping song, often for the rest of the fight or walk. The ROM's own; the
port has always done it, as faithful sequencers do. 129d's check looked for
a part left *lent*, not left *down*, so its 224 orders never saw it.

**The check, widened** (port-only, a diagnostic): Smps.quiet, the tracks the
pause's walk turned down that nothing has turned back up (set in mus_pause's
walk down; cleared by refresh_volume at the track's own volume and by an
instrument upload, each while the track holds its channel; cleared for a new
song). smps_stuck_tracks returns it in its high byte for music tracks in
use and holding their channel, the driver running again; `expect stuck`
takes a 16-bit mask.

**Found** (a hunt: pause, 40 ticks, unpause, d ticks, a sound, 150 ticks):
on the field, A or C at d = 0 to 24 leaves music tracks 1-3 down ($0E00),
from 27 on none; in the formation fight under Rise or Fall, the battle's
effects at d = 0 and 3 leave tracks 1 and 2 ($0600). tools/stuck_hunt.py has
the field orders now.

**Kept**, as the owner decided for the chip ("at least for the chip audio"):
route unpause_lost_part (Motavia, pause, unpause, A three ticks later,
`expect stuck $0E00`). EXTRA and Q inherit it through the shared
sequencer; the owner: leave that so (no fix for them).

**Proof** (b132c against b132b, both here): every file the same but
pause_driver's (its expectation updated in 132b after b132b was taken: now
PASS) and the new route's PASS line and three files (110 PASS). The
diagnostic writes nothing to the chip: every sound-write hash unchanged.
src/ changed: smps.c/.h, audio.c/.h, probe.c; `make`.

## Build 133: A answers mid-step; a window waits for the camera

handover_133: port the A press mid-step from the leader's pixel position and
drop the port's own rule; then, read only, why notes ring under the Escape
menu while COMPUTER PLAYS runs.

**Read** (ps2.asm): the target test (Level_ChkTargetInteract) to its end and
its box table (loc_7278); ProcessAButAction; RunObject's tail (loc_3E4); the
leader's routine (ObjCL_Main); the camera's mover (loc_8BBA); the head of
ProcessWindows.

**Where the port and the ROM parted.** The target test puts the box table's
row for the leader's facing (his $2A / 3: rows 0-3 are a 16x16 box centred
16 px ahead -- up, down, left, right) about his screen position, and overlaps
it, edges included, with each person's own box (the row in their $2C, row 4
for townspeople), taking the first in slot order that was drawn this frame
(bit 7 of 2(a1)). The port's hit test, npc_in_the_way, was already that: BOX
is the table row for row, box_hit the same compare. It reads level positions
where the ROM reads screen ones, and they agree: RunObject's tail writes a
screen position after the frame's velocity, as (position - camera, wrapped) +
$80, and the camera moves inside ObjCL_Main before the leader's tail runs, so
the leader and every person after him are placed against one camera. At rest
the leader stands on a cell's centre and the box ahead is that cell; the two
parted only mid-step, where level_talk's own rule dropped the A. The facing
does not part: ObjCL_Main sets $2A when it tries a direction and reads the
pad again only when the step ends, as walk.c does.

**The fix** (game.c, level_talk): the rule is gone; the demo-walk test,
ProcessAButtonPress's own, stays. g_walk.x/y after walk_tick are the ROM's
position with the frame's pixel in. The scooter is as it was (it never had
the rule). A person who is mid-step still gets the selection sound only
(ProcessAButAction's $18/$14 test).

**ProcessWindows' camera test**, which the fix brought into play. Before it
sets a new window up, ProcessWindows tests the main camera's x and y against
the 16 px grid (`andi.w #$F`) and returns while either is off it (loc_9478, an
rts): the window word waits and is tried every frame. The camera moves only
under a step, a pixel a frame (two on the scooter), and stands on the grid
between steps, so the port had never asked for a window off it; with A
answered mid-step it can, and the port drew the box at once, sliding with
plane A for the rest of the step (game.c's window render assumed the test).
window.c's win_process has it now (WinSys.camera_off_grid, set by the level
screen around its call). A close, a window already opening and the script's
typing are not tested, as in the ROM. The port keeps no main camera on the
other screens, which leave the flag false; in the ROM the test runs on every
screen, against whatever the camera words hold there -- not read further.

**Routes.** talk_mid_step: Aukba 11,9, a step up toward the Dezolian at
11,7, A five pixels in: $1651, the step running on to 11,8 (fails on 132d,
"ignored: mid-step" under PS2_ATRACE). window_mid_scroll: Aukba 12,12, the
step down that scrolls the screen, A four pixels in at nothing: five ticks
later the frame at 160,196 is still the map ($888) -- without the camera
test the box is opening there (index 11, $800) -- then $1708 as the step
ends (fails on 132d at its script; on the fix without the test, at the
pixel).

**Proof** (b133 against b132d): 112 PASS of 112; the 573 earlier route files
all the same, the two new routes' seven files new; crt, sim, patches, audio,
attract.wav, the boss trials, probe_check and the build's warnings the same.
`make roundtrip`: 0 failures. src/ changed: game.c, window.c, window.h.

**Read only: notes ringing under the Escape menu while COMPUTER PLAYS runs.**
The sequencer is game logic: tick_screens calls audio_music_tick once a tick,
the only caller of smps_tick, and the register writes it makes cross to SDL's
audio thread, which renders the chips continuously from what it has been
sent. A player's Escape menu is held inside the tick: tick_screens runs
audio_music_tick before sysmenu_tick freezes the screen ("only the music runs
on"), so the song goes on and its notes end on time. Under COMPUTER PLAYS the
menu is main.c's instead, outside the game: computer_draw draws the held game
and runs no tick, with the output unmuted (audio_set_muted false). Nothing
advances the sequencer -- no note is keyed off, none of the driver's per-tick
work steps on -- while the chips keep sounding what was keyed on when Escape
was pressed: FM notes held in their sustain until the chip's own decay takes
them, PSG tones at the volume last written (the PSG has no envelope of its
own), for as long as the menu is up. With the lamp still lit at the close,
the ticks resume and the song carries on from the tick it stopped at. The
Escape menu is the port's, so there is no ROM behaviour to match; left as is
(The owner likes it). Not read: smps_play, so whether the menu's own clicks
(menu_sound, audio_play_sfx) sound before the next tick.

## Build 133a: the two reads 133 left open

Docs only; nothing in src/ changed.

**ProcessWindows' camera test off the level screen.** In the ROM it runs on
every screen; the port applies it on the level screen only (133). Read: every
reference to the main camera words by name. The level's own writes are the
camera mover's helpers (loc_8BBA's loc_8C42, loc_8CBA, loc_8D2E, loc_8DA6)
and two in LoadLevelData (loc_91AA, loc_91DA); BuildingScreen (loc_839A) and
BattleScreen, before their loops, write 0 to plane A's camera and the main
camera alike; the rest read them (RunObject's tail, the map's row and column
streaming at loc_8E18 and loc_9002, ProcessWindows). So a fight begun under a
scrolling step starts with the camera at 0,0, on the grid, and the test only
ever holds a window on the level screen: the port's level-only flag is the
ROM's test. Nothing to change. docs/engine.md says so.

**The Escape menu's own sounds under COMPUTER PLAYS.** Read: smps_play, the
sequencer's queue reader (do_sound_queue) and its caller (driver_frame, from
smps_tick, whose only caller is audio_music_tick), and the Escape menu's
sounds (sysmenu.c). smps_play writes the id to queue slot 0 and nothing else,
as the ROM's `move.b #id, (sound_queue).w` does; the driver reads and clears
it on its next frame. Every sound the menu makes is the selection sound. Under
COMPUTER PLAYS the menu runs in main.c with no tick, so each click overwrites
the slot, none sounds while the menu is up, and one selection sound plays on
the first tick after it closes, lamp lit or put out. A sound the computer's
last tick queued and the driver has not yet read is replaced by the menu's
first click. A player's own menu is held inside the tick, after the music
has run, so neither happens there. Not tested by ear or by a route (the
headless path to COMPUTER PLAYS was not run); the port's own menu, so no ROM
behaviour to match, and not changed: The owner's call. docs/computer_plays.md
("Handing back") has both this and 133's ring.

## Build 134: the window maps as Tiled maps

The owner: ".nam .map for sure"; the compressed art packed back into the ROM's
own format is not wanted (README and NOTES no longer list it -- it had come
in as a reading of the round-trip's own limit, "never packed back", and was
carried in every "Next" since); baking the rectangles is the pictures
(portraits, the title, the ending's pages), not the walking sprites, which
are the atlas item. This build: the .map half.

**Read**: extract_winart.py, winmap.c, window.c's loading (win_load_assets),
art_export.py's portraits, roundtrip.py's level 2 and make_manifest.py; a
survey of all 84 maps and of the sheets they index.

**The format.** assets/winart/<name>.tmx, a CSV layer named `window` over
embedded tilesets that are the tile sheets themselves (assets/tiles/*.png,
8x8 tiles in tile order). Each tileset has a `byte_base` property; a cell's
byte is (gid - firstgid) + byte_base. Firstgids are read, not assumed, since
Tiled renumbers them on save. A window map is drawn as tile $500 + byte
(loc_980A, $F72C = $8500), so its one tileset is FontsIconsArt (192 tiles;
the maps use $12-$BE). A portrait map (the 25 named ...Portrait) has a frame
row top and bottom ($B9, $BE: FontsIconsArt) and its picture between, from
the art of the first portrait slot that uses the map and is its own (the
three slots build 127 found mismatched, 25, 28 and 29, are passed over):
byte_base $A0 for Rolf's and the library graph's (art at $4A0), 0 for the
rest. The extractor refuses a byte outside its row's sheet; none is.

**The game.** src/core/tmx.[ch] (new): embedded tilesets with up to eight
integer properties, one CSV layer by name, gids with Tiled's flip flags kept
(the nametables will want them); an external tileset, a non-CSV layer or a
wrong cell count is refused with the reason. winmap_load reads the .tmx and
refuses an empty, flipped or stray cell, naming it. window.c asks for .tmx.
level.c's own reader is untouched.

**The tools.** extract_winart.py writes the .tmx (write_winart_tmx) and
deletes stale .map files; it reads the sheets and the portrait tables, so
`winart` moves after `art` in the Makefile's EXTRACTORS (nothing between
reads its output). roundtrip.py wraps write_winart_tmx and packs each .tmx
back as winmap_load reads it (pack_winart_tmx): a "window maps" row at level
2. art_export.py's load_winmap reads the .tmx. make_manifest.py: the window
maps are read through the store now, so they are listed (manifest 786 ->
870); `.map` is gone from its skip list and from the round-trip's mirror of
it. intro_probe and building_probe link tmx.c.

**Proof** (b134 against b133): every file the same -- 112 PASS, the 580
route files byte for byte (every window and portrait the routes show), crt,
sim, patches, audio, attract.wav, the boss trials, probe_check, the build's
warnings. `make roundtrip`: level 1 903 of 903 files as assets/; window maps
84 of 84 the same; 0 failures. Every new .tmx decoded independently against
the uploaded .map: 84 of 84 byte for byte, and art_export's reader the same.

**Open**: the .nam half (next); Tiled shows the sheets in grey (their PNGs
carry a grey ramp) -- coloured views like blocks_*.png are possible if
The owner wants colour when editing.

## Build 135: the screen nametables as Tiled maps

The owner, on grey sheets or coloured view sheets in Tiled, asked whether the
coloured ones would be editable: the map is editable either way -- the game
reads gids and the tilesets' properties, never the image -- and a view sheet
is only a rendering that goes stale when its sheet is edited. Grey, the
handover's default, went ahead; his choice is still open. This build: the
.nam half of README's last-container item, after 134's .map half.

**Read**: tmx.[ch], winmap.c and nametable.[ch] in full; extract_nametables.py;
art_export.py's SCENES, Scene, load_nam and the two pictures it places
itself (Tyler's monitor, Mother Brain's star field); roundtrip.py's level 2
and run_extractors; make_manifest.py; the callers (game.c's load_assets,
level_scene_picture, enter_battle and Mother Brain's battle; ending.c's
loader). In ps2.asm: the title's two dissolves and FadeArtTiles
(ps2.asm:9953, 10733-10760).

**The survey, finished.** Build 134 had checked 20 of the 22 tables against
art_export's placements. The other two: Tyler's monitor is clean (its 104
empty cells exactly $0000, the rest in the spaceship art from tile 1).
Mother Brain's star field is not: 238 cells are $6000, tile 0 in palette
line 3, the line all her stars use -- the ROM's way of storing the gaps.
Tile 0 is blank in her battle, so they draw nothing, but the word is not
$0000. And one cell of the title's plane A (column 13, row 11) is tile $200,
where two sheets meet: the figures' dissolve writes 287 tiles from $E1
(FadeArtTiles with $11E, counted by dbf), so $200 is the logo's first tile;
the figures sheet's 288th tile is only its padding. No cell anywhere uses
priority; each table's art is in one palette line.

**The format.** assets/nametables/<n>.tmx: one CSV layer, `nametable`;
embedded tilesets that are the art sheets themselves (assets/tiles/*.png),
one a sheet, palette line and priority the table uses, each with the
integer properties vram_base, palette_line and priority. The word is
priority << 15 | palette_line << 13 | V << 12 | H << 11 | (vram_base +
local), V and H from Tiled's flip flags ($40000000, $80000000). An empty
cell (gid 0) is tile 0 in the map's own property empty_palette_line, written
in every map: 0 (the word $0000) in 21, 3 in Mother Brain's. Firstgids are
read, never assumed.

**The extractor** (extract_nametables.py, write_nametable_tmx) knows where
each table's tiles come from -- SHEETS, the sheets in video memory when the
game shows the table, in load order, from game.c's and ending.c's places
and art_export's -- and resolves a tile to the last-loaded sheet that covers
it, as video memory does (the title's $200 to the logo). It refuses a word
it cannot write exactly: a tile in no sheet that is not an empty cell (tile
0, one palette line, nothing else set), or two different empty words in one
table. It deletes stale .nam files. It reads the sheets art and inline_art
write, so `nametables` moves after `art` in the Makefile's EXTRACTORS
(nothing between reads its output; a comment says why the order matters).

**The game.** tmx.[ch]: the map's own integer properties (the block after
<map>, before the first tileset or layer; tmx_map_int), the property reader
shared with the tilesets' (TmxProps). nametable_load reads the .tmx and
refuses, naming the cell, a diagonal or hexagonal flip, an empty cell with
flips, a tile in no tileset or past $7FF, a tileset without vram_base or with
a palette line or priority out of range. Its callers (game.c, ending.c) ask
for .tmx, and the messages that name a missing file name the .tmx.

**The tools.** roundtrip.py wraps write_nametable_tmx and packs each .tmx
back as nametable_load reads it (pack_nametable_tmx): a "nametables" row at
level 2; .nam is out of the "containers" sentence and the skip list.
art_export.py's load_nam reads the .tmx; (a find) its title scene placed
the figures after the logo, so the figures sheet's padding tile covered the
logo's first at $200 in the dump's title_screen.png -- the order is the
game's now. make_manifest.py lists the nametables (manifest 870 -> 892);
nametables.json stays level 1 only (roundtrip_json's note names the .tmx).

**Proof** (b135 against b134): every file the same -- 112 PASS, the 580
route files byte for byte (the title, the dreams, Nei's pictures, Lutz,
Tyler's portrait and monitor, the planet, the battle floor, Mother Brain,
the ending), crt, sim, patches, audio, attract.wav, the boss trials,
probe_check, the build's warnings. `make roundtrip`: level 1 903 of 903,
manifest 892 as checked in; nametables 22 of 22 the same; 0 failures. Every
.tmx decoded independently (a reader written for the check) against the
uploaded .nam: 22 of 22 word for word; nametable_load (a standalone test
program) and art_export's reader the same. The routes read the maps: in a
scratch copy, Lutz's tileset moved to line 2 fails esper_b1_lutz's pixel
check, and Mother Brain's vram_base moved by one changes four of
mother_brain_battle's five shots. Refusals tried on edited copies of her
map: renumbered firstgids load the same words; a diagonal flip, a stray
gid, a missing vram_base, a line of 4, a flipped empty cell and a tile past
$7FF are each refused with the cell named, by nametable_load and by the
round-trip's packer alike; without empty_palette_line her 238 gaps come
back $0000.

**Open**: coloured view sheets for Tiled, if the owner wants them later (the
format would not change: only the tilesets' images). The nametables keep
their 97b-era names (`TylerSpcshp_MonitorTileInd`, `DreamBattleTileInd`)
-- ROM labels, where the README's naming rule wants friendly ones.

## Build 135a: building on macOS, written down

The owner asked why the tables are binaries, and for instructions to build on a
Mac. No Mac here: docs/macos_build.md is written from the Makefile and the
source, read for what each does on macOS, and says so.

**Read**: the Makefile's toolchain, Q, game, dist and clean rules;
platform_sdl3.c's SDR-white branch; crt_gpu.c's shader-format choice (SPIR-V,
then DXIL, then MSL, entry point main0 for MSL); home.c in full; pack.S;
autoplay.c's includes and spawning; a search of src/ for Linux-only APIs and
x86-only code (none: timespec_get is in macOS's C library, the POSIX calls
autoplay.c and home.c make under _POSIX_C_SOURCE are macOS's too).

**The page**: Xcode's command line tools; Homebrew's sdl3, pkgconf, python,
pillow (extractors and art only; COMPUTER PLAYS imports none) and GNU make
(macOS's make is 3.81); Q cloned to ~/GIT/Q; shaders/ copied from his own
machines (Metal reads the .msl). Known gaps: the game must be started from
the tree's top (home_enter finds its folder through /proc/self/exe, which
macOS lacks); `make dist` does not build (pack.S is ELF/COFF syntax; Mach-O
wants `__DATA,__const` and underscored symbols); HDR untested (SDL3 reports
SDR white as 1.0 on Apple's EDR; the port's 203-nit assumption off Windows
unmeasured there); proof.sh needs bash 5, GNU xargs and sha256sum.

**The Makefile**: Q_LDLIBS defaults to -lc++ when `uname -s` says Darwin
(Apple's clang has no libstdc++; with Q=auto finding Q, plain `make` would
not have linked); -lstdc++ elsewhere as before, and the command line still
wins (checked with a stand-in uname: Darwin gives -lc++, Linux -lstdc++).
`make clean` removes clang's .dSYM folders. Nothing else changes on Linux
or Windows: no proof run for a variable that is unchanged there.

**README**: a pointer to the page in Build and in the docs index; and a
correction to 135's line -- "no custom container is left" was wrong: the
.bin files under tables/ and music/ are the ROM's bytes behind a tag
(PS2W, PS2B, PS2I, PS2S, PS2D). It now says no tilemap container is left
and names the .bin files.

## Build 136: the colour ramps as pictures

The owner picked handover_136's task A, the `.bin` files made readable, one
kind at a time. First kind: the two colour ramps. (Asked mid-job: building
on an Intel Mac now that Homebrew has dropped it -- docs/macos_build.md,
"Intel Macs", below.)

**Read**: extract_tables.py, table.[ch], ps2png.py, palette.c's
palette_load and cram_from_rgb8, png.h, game.c's load_assets and
palette_cycle_sega, ending.c's ending_load_assets and its credits
(credits_start, credits_write_row, credits_palette_fade, credits_step),
roundtrip.py's wrapping, run_extractors, the palette packer, level2 and
main, make_manifest.py. In ps2.asm: PaletteCycle and CyclingPal_SegaLogo
(9235-9271), the credits' palette fade and its ramp (loc_6AE0, loc_6B10:
9823-9852), EndingScreen's credits loop (loc_7824 to loc_785C) and the
credits writer (loc_787E), VBlankSub_14.

**The ramps.** Two tables of CRAM words a routine copies into the live
palette a piece at a time: the Sega logo's sweep (CyclingPal_SegaLogo, 31
words; PaletteCycle slides an 11-word window down it into line 0, entries
2-12, a word every four frames) and the credits' fade (loc_6B10, nine rows
of four; loc_6AE0 puts one row into line 3, entries 9-12, every eighth step
from 3600 steps in, the last row -- black -- first). Each is now an indexed
PNG under assets/palettes/, as wide as its rows: `sega_logo_sweep.png`
31x1, `ending_credits_fade.png` 4x9, brightest row on top. Pixel i holds
PLTE index i, whose colour is word i through the DAC ladder, so the picture
is the ramp. Names in asset_names.RAMP_NAMES (label -> name, row width).

**One stray bit.** The fade's eighth row is `$0200 $0100 $0000 $0000`:
`$0100` is bit 8 over black, not a colour a PNG can carry. Read where it
goes: loc_6AE0 copies the row straight into the live palette and the next
row replaces it eight steps (24 frames) later; nothing in the credits' loop
(RunObjects, BuildSprites, the fade, the scroll, the writer) or in its
vertical blank (VBlankSub_14: the pads, the DMA, ProcessWindows) fades or
compares a palette word, and the fade always ends before the credits can
(its last row starts the UFO, and loc_6AE0 runs only inside the credits'
loop). So the word is only ever shown, and colour RAM shows it black either
way. The PNG has `$0000`; roundtrip's STRAY_BITS_CHECKED lists it, as 130a
did ending page 5's `$05CC`. The sweep has no stray bits.

**The extractor** (extract_tables.extract_ramps) writes the two PNGs
through ps2png.write_ramp -- its own writer, not write_palette, which
roundtrip records as a 16-wide strip -- into palettes/ beside its outdir
(`--palettes` to say otherwise), and deletes `CyclingPal_SegaLogo.bin` and
`EndingCreditsFadeRamp.bin`. The other four tables come out byte for byte
as before.

**The game.** table.[ch]: `table_load_ramp`, each pixel in reading order to
a word by its PLTE colour's nearest DAC level (cram_from_rgb8), so an
editor that reorders the PLTE changes nothing. It refuses, naming it, a
pixel whose index is past the PLTE, and more than TABLE_MAX pixels.
game.c's load_assets and ending.c's ending_load_assets load the PNGs.

**The tools.** roundtrip.py: `write_ramp` wrapped (kind "ramp", the ROM's
words), `pack_ramp` reads each file as table_load_ramp does, a "colour
ramps" row at level 2 sharing the palettes' stray-bit check. The
"containers" sentence keeps tables/*.bin and music/*.bin: four tables and
four music files are left. The manifest lists the two PNGs (892 -> 894).

**Proof** (b136 against b135a): every file the same -- 112 PASS, the 580
route files byte for byte, crt, sim, patches, audio, attract.wav, the boss
trials, probe_check, the build's warnings. `make roundtrip`: level 1 903 of
903 (the two PNGs in place of the two containers), manifest 894 as checked
in, colour ramps 2 of 2 the same with the one stray noted, 0 failures.
Decoded independently twice against the uploaded `.bin` files: a standalone
program around table_load_ramp, and PIL's pixels through palette.c's ladder
-- 31 of 31 words, and 36 of 36 with `$0100` as `$0000`. table_load_ramp
refused a pixel past a 4-entry PLTE (naming pixel (2, 1)) and a 600-pixel
image. The game reads the pictures: in a scratch copy, one word of each made
green changes the ending route's last two shots (the UFO, THE END), and a
throwaway route shooting the Sega screen every 20 ticks shows the sweep's
green word at 80 and 100; no checked-in route shoots the Sega screen's
sweep.

**docs/macos_build.md**: an "Intel Macs" section. Homebrew 7.0.0 (September
2026) moved Intel Macs to its lowest tier -- no new bottles, an Apple
Silicon installer, support ending September 2027 -- and points them at
MacPorts. Two ways, neither tried: MacPorts (`SDL3`, `pkgconfig`, `gmake`,
Python and Pillow ports; the SDL3 port's name checked, the others not), or
no package manager at all: SDL's release framework in ~/Library/Frameworks
and the Makefile's SDL_CFLAGS / SDL_LIBS given on the make line (they come
from pkg-config with `:=`, and the command line wins), python.org's Python
with pip's Pillow.

Delete on the owner's side: `assets/tables/CyclingPal_SegaLogo.bin`,
`assets/tables/EndingCreditsFadeRamp.bin`.

## Build 136a: the dissolve order as JSON

Task A's second kind. The owner sent the drums' Z80 file
(`ps2_pcm_drums.asm`, laid out as `sound/ps2.pcm_drums.asm`) and the SMPS
notes he has (the music and track headers, the instrument formats, the
driver's RAM); with the drum file, `make roundtrip` runs extract_drums too
(level 1 904 of 904: PCMDrums.bin is no longer "not written by any
extractor").

**Read**: FadeArtTiles, FadeArtFill, the dissolve out (loc_6C54, loc_6CA4)
and FadeEffectArray (ps2.asm:9953-10063), and the routines' callers' lines
(9938, 10735, 10759, 12210; 12259, 18758, 18783, 19567); game.c's
load_assets, the Tyler monitor's step and dream_erase_step; battle.c's
battle_set_fade_order and its dissolve phase; tileset_reveal_pixel;
json.[ch]; roundtrip_json.py's Table, Docs, the JSON accessors and the
fight's small byte tables as the model.

**What the bytes are.** FadeArtFill takes an entry's bit 0 to pick the
first or second pixel of a pair and bit 1 the pair in the word, and the
word at byte offset (entry >> 2) * 2 in the tile -- so the entry is the
pixel's place in the tile, y * 8 + x, as tileset_reveal_pixel already took
it. The dissolve out ignores bit 0 and clears the byte (two pixels) the
entry falls in, for the first 32 entries.

**The file.** `assets/tables/dissolve_order.json`: "source", "about" (the
above, for whoever opens it) and "order", 64 `[x, y]` pairs eight to a line
-- which shows the table for the ordered dither it is: [0, 0], [4, 4],
[4, 0], [0, 4], [2, 2], [6, 6], ... The name is in asset_names.TABLE_FILES,
which the later tables join.

**The game.** table.[ch]: `table_load_dissolve` -- "order" must hold
exactly DISSOLVE_STEPS (64) positions, each an array of two whole numbers
0-7; anything else is refused, naming the position. game.c's load_assets
calls it; g_fade_order is the same ByteTable as before.

**The tools.** extract_tables writes it (write_dissolve) and deletes
`FadeEffectArray.bin`. roundtrip_json.py: a "dissolve_order" table, the ROM
side FadeEffectArray assembled by asmdata, pack_dissolve reading the file as
table_load_dissolve does (refusing what it refuses), edit_dissolve for
--prove-edits, and extract-tables in extractor_cmd (the first run of
--prove-edits stopped on it with a KeyError). Manifest 894 -> 895.

**Proof** (b136a against b136): every file the same -- 112 PASS, the 580
route files byte for byte, crt, sim, patches, audio, attract.wav, the boss
trials, probe_check, the build's warnings. `make roundtrip`: level 1 904 of
904, manifest 895, the dissolve order 64 of 64 bytes, 0 failures. A
standalone program around table_load_dissolve: 64 of 64 bytes as the
uploaded FadeEffectArray.bin; refused, naming the position, 63 positions,
an x of 8, a y of 1.5, a one-number position and a missing "order"; loaded
the same with the keys reordered. pack_dissolve refuses the same five.
`roundtrip_json.py --prove-edits`: order[1]'s x +1 fails both levels;
every edit caught. The
game reads the file: with the order reversed in a scratch copy no
checked-in route changes (they shoot dissolves only once they are over,
and both halves of the order cover every byte the dissolve out clears), but
a throwaway route shooting the title differs at 192 ticks, mid-dissolve.

**Open**: battle.c's FADE_ORDER_FALLBACK, used only until
battle_set_fade_order is called (enter_battle always calls it), is a
bit-reversed 0-63, not the ROM's order. Harmless as it stands; noted.

Delete on the owner's side: `assets/tables/FadeEffectArray.bin`.

## Build 136b: the drums as WAVs and a JSON

Task A's third kind, with the Z80 drum file the owner sent.

**Read**: extract_drums.py in full; fm.c's fm_load_drums and its reader
(src_open, fm_set_reader); fm.h's FmDrum; q_synth.cpp's QBank::load_drums,
QSource and SampleDrum; synth.h's load_drums; drums.h; audio.c's
audio_load_music; the drum paths in drum_hits.cpp, q_demo.cpp and
fm_probe.cpp; the Makefile's fm_demo, q_demo, drum_hits and fm_probe rules;
roundtrip.py's wrapping and level 2, roundtrip_json.py's file lists;
docs/audio.md's "Drums".

**What the container held.** A table of eight drums (byte, delay, Z80
address, length) and the 8 KB Z80 RAM image the six samples sit in, from
$118 (past the program) to $1AAE, zero elsewhere. The samples are
contiguous and each drum plays its sample's exact extent -- the three toms
one sample at delays 2, 4 and 8 -- except $90, 513 bytes of an 18-byte
sample, on into the zeroed RAM.

**The files.** `assets/music/drums/<sample>.wav`, six (kick, snare, tom,
open_hat, closed_hat, unused): the ROM's bytes as 8-bit unsigned mono PCM,
unhalved (the Z80 halves each as it plays), at the first drum's rate that
plays them, rounded -- a label only, since a drum's delay sets its rate.
`assets/music/drums.json`: "samples" (name, file, z80_address) and "drums"
(byte, name, sample, length, delay), one a line, and an "about" for whoever
opens it. Names in asset_names.DRUM_SAMPLE_FILES, by the sample's label.

**The game.** src/audio/drums.c: `drums_load`, which rebuilds the table and
image from the JSON and its WAVs (the WAV's path the JSON's folder plus its
"file"), through the pack reader first, as before, else the disk. It
refuses, naming it, a file that does not read, a WAV that is not 8-bit mono
PCM, a sample below $118, past $2000 or over another, a drum byte outside
$80-$FF or given twice, a drum naming no sample, a delay outside 1-255 and
a drum playing past RAM. drums.h gives it its own DrumEntry (and extern
"C"): including fm.h there clashed with smps.c's own static fm_note_off,
smps.c including drums.h for the names. fm_load_drums and Q's load_drums
both call it, so the chip and Q play from one reader. The paths: audio.c,
drum_hits, q_demo, fm_probe. Makefile: fm_demo, q_demo, drum_hits and
fm_probe link drums.c, json.c and assetfs.c.

**The tools.** extract_drums writes the WAVs and drums.json
(write_drum_set) and deletes PCMDrums.bin; its --wav listening copies are
unchanged. roundtrip.py: write_drum_set wrapped (the image and table the
Z80 file makes), `pack_drums` reading the files as drums_load does, a "drum
sets" row at level 2. roundtrip_json.py: PACKED_ELSEWHERE names drums.json
as packed there. Manifest 895 -> 902.

**Proof** (b136b against b136a): every file the same -- 112 PASS, the 580
route files byte for byte, crt, sim, patches, audio_check, attract.wav, the
boss trials, probe_check, the build's warnings. `make roundtrip`: level 1
910 of 910 (the JSON and six WAVs in place of the container), manifest 902,
drum sets 1 of 1 the same, 0 failures. A standalone program around
drums_load against the uploaded PCMDrums.bin: 8 of 8 table entries and 8192
of 8192 RAM bytes the same; refused, each named, an overlapping sample, one
at $C8, one past $2000, a byte of $7F, a byte given twice, a missing sample,
a delay of 0, a 9000-byte drum, nine drums, a missing WAV and a stereo one.
pack_drums: the image and table the same as the container's. With Q
(the uploaded Q, Q_DIR): drum_hits' report on all eight drums, chip and Q,
identical before (the container) and after (the files); q_demo and fm_probe
build with no warnings. The game reads the WAVs: the snare's first 400
bytes silenced in a scratch copy change audio_records' and pause_driver's
logs. Not tried: `make dist`'s pack (no SDL3 here); the reader is the same
pack-first path fm_load_drums took, and make_pack takes every asset.

Delete on the owner's side: `assets/music/PCMDrums.bin`.

## Build 136c: the note frequencies as JSON

Task A's fourth kind. The owner, on the song format to come: actual note names,
not hex bytes; then, could the transpose and the envelopes be shown
conveniently (the design is in NOTES, "Next (after 136c)").

**Read**: GetFMFreq and the note path above it (ps2.asm, from loc_BA30C;
FMFreqs at 92537, 85 dc.w, then a track's dc.b with no label between);
extract_music.py's label_bytes and main; smps.c's get_fm_freq, read_file,
smps_load; smps.h; smps_trace.c's note_name and roll; the FMFreqs.bin paths
in audio.c, patches_check.c, smps_trace.c, q_demo.cpp and fm_probe.cpp;
docs/audio.md's "Pitch" (the 25-27 cents, measured in build 111).

**What the table is.** GetFMFreq adds the track's transpose to the note
byte (add.b), keeps six bits (andi.w #$3F) and reads that word of FMFreqs:
entries 0-63 are reachable; 0 is silence; the ROM's 64-84 are never read.
Each word is block << 11 | F-number. On the chip (F-number x 2^(block - 1)
x 7670454/144 / 2^20) entry 1 is 31.34 Hz, B0 + 26 cents, and every entry
is 25-27 cents above a concert note.

**The file.** `assets/music/note_frequencies.json`: "entries", one a line,
`{"note": "C#5", "block": 5, "fnum": 692, "hz": 562.45, "cents": 25}`;
"note" is the nearest concert note (entry 1 B0, each entry a semitone up),
"hz" and "cents" the extractor's arithmetic -- for reading; the game reads
block and fnum. Entry 0 is `{"note": "silence", "block": 0, "fnum": 0}`.
Name in asset_names.MUSIC_FILES. (A first try wrote "cents": +26 -- not
JSON; caught by reading it back.)

**The game.** smps.c's load_freqs, from smps_load: "entries" must hold
SMPS_FREQS_MIN (64) to SMPS_FREQS_MAX (128); each block a whole number 0-7
and fnum 0-2047, else refused naming the entry. smps_trace links json.c and
assetfs.c now; the other users of smps.c already did.

**smps_trace's names** follow: entry 1 is B0, not C0 -- a semitone lower
and an octave higher than it printed, so they are what the chip plays (a
quarter of a semitone flat of it). The title's melody now opens C#5, C5,
C#5, G#4 (it printed D-4, C#4, D-4, A-3). The octave is taken % 100 so gcc
can see the name fits its buffer.

**The tools.** extract_music writes the JSON (write_note_frequencies) and
deletes FMFreqs.bin; SoundData.bin comes out byte for byte as before.
roundtrip_json.py: a "note_frequencies" table (FMFreqs' 85 words from
asmdata, pack_freqs as load_freqs reads, edit_freqs), and extract-music in
extractor_cmd. Manifest 902 -> 903.

**Proof** (b136c against b136b): every file the same -- 112 PASS, the 580
route files byte for byte, crt, sim, patches, audio_check, attract.wav, the
boss trials, probe_check, the build's warnings. `make roundtrip`: level 1
910 of 910, manifest 903, the note frequencies 170 of 170 bytes, 0 failures;
`--prove-edits`: entries[5].fnum +1 fails both levels, every edit caught. A
standalone program around smps_load: 85 of 85 words as the uploaded
FMFreqs.bin; refused, each named, 63 entries, a block of 8, an fnum of 2048,
an fnum of 600.5 and an entry with no fnum; loaded 64 entries, and a file
with every note/hz/cents removed. pack_freqs refuses the same. With Q
(Q_DIR): q_demo, fm_probe and smps_trace build with no warnings;
patches_check PASS. The game reads the file: C#5's fnum +20 in a scratch
copy fails audio_switch and pause_driver (their sound-write hashes) and
changes audio_records' log.

Delete on the owner's side: `assets/music/FMFreqs.bin`.

## Build 136d: the FM instruments as JSON

Task A's fifth kind: the FM instruments and the order they are uploaded in,
two `.bin` containers (`tables/FMInstruments.bin`, "PS2I", and
`tables/FMRegisterOrder.bin`, "PS2B"), made one readable file with each
register's fields named. The owner's handover asked for named operator fields
and for instrument names to be offered for the song files.

**Read**: the instrument command (cfEF_SetFMIns, ps2.asm 91753) and the
upload routine (SetFMIns, after loc_BA818's 26-byte register table at
92306), RestoreBGMChn, RefreshVolume (the carriers by algorithm, 92380 on),
the pointer reader (SndDrv_ReadPtr, 92125) and the pointer table
(FMInsPtrs, 89852, 105 entries, nine naming the same instrument);
extract_tables.py in full; extract_music.py's build_image, label_bytes,
pointer_names and main; fm.c's fm_load_instruments, fm_set_instrument and
the container reader; drums.c and json.h (the loader's model); fm_demo.c,
q_demo.cpp's instrument paths, fm_probe.cpp's comment; q_synth.cpp's
register decode and patch matching; roundtrip_json.py's Table, the dissolve
and frequency tables; asmdata's Rom; make_manifest.py; the Makefile's tool
rules; smps.c's coordination flags and note parse (for the names below).

**What the upload is.** SetFMIns writes 26 registers to the track's
channel: the register numbers from loc_BA818 -- $B0 $B4, then $40 $48 $44
$4C, $30 $38 $34 $3C, $50.., $60.., $70.., $80.. (The owner's PS2_Notes: the
US/EU order) -- and the values from the instrument, byte i to register i.
Operators in the chip's numbering sit at register offsets +0, +8, +4, +C,
so each group goes operator 1, 2, 3, 4. Then, if the track's volume is not
0, RefreshVolume adds it to the carriers' total level: algorithms 0-3
operator 4; 4 operators 2 and 4; 5-6 operators 2-4; 7 all four.

**Bits the chip does not read.** 38 of the 105 instruments set some: bits
6-7 of feedback/algorithm ($F5, $F6), bit 7 of a detune/multiple, bit 5 of
a rate-scaling/attack, bits 5-6 of an AM/decay, bits 5-7 of a sustain rate
($23, $71...). Nuked-OPN2 masks them all; so does Q's decode. They are kept
(`"ignored_bits"`) so the bytes stay the ROM's, which Q's patch file
matches on.

**The file.** `assets/music/fm_instruments.json` (asset_names.MUSIC_FILES
"FMInsPtrs"), written by extract_music (`write_fm_instruments`), the
instruments read out of the sound driver's image through FMInsPtrs as
SetFMIns reads them (`instrument_bank`; the two whose last four bytes are
the next one's first come out whole, the build 111 lesson) and the order
by label_bytes. `"upload_order"`: the 26 registers by name --
`feedback_algorithm`, `pan_lfo`, `opN.total_level`, `opN.detune_multiple`,
`opN.rate_scaling_attack`, `opN.am_decay`, `opN.sustain_rate`,
`opN.sustain_level_release`. `"instruments"`: each `number` (its place),
`algorithm`, `feedback`, `pan` (center/left/right/none: $B4 bits 7-6),
`lfo_am_sensitivity`, `lfo_fm_sensitivity`, then a row of four per
operator field -- `multiple`, `detune`, `total_level`, `attack_rate`,
`decay_rate`, `sustain_level`, `sustain_rate`, `release_rate`,
`rate_scaling`, `lfo_am` -- and `"ignored_bits"`, register name to bit
numbers, where there are any. An "about" says what each field is and its
range. extract_tables no longer writes either container and deletes them
(STALE); its bytes_from and extract_instruments are gone.

**The loader.** src/audio/instruments.[ch], `instruments_load`: the order
as register numbers and the bank as 26 bytes an instrument in that order,
through the pack reader or the disk. Refuses, naming it: an upload_order
not the 26 names once each; none or more than 256 instruments; a `number`
not its place; a field missing, not whole or out of range (`lfo_am` true
or false, `pan` one of four); a row not four; an ignored bit on no
register, past 7 or one the chip reads. fm.c's `fm_load_instruments(Fm *,
path)` calls it (the order kept unless it loads); fm.c's FmSource reader,
used only by the containers, is gone. fm_demo and q_demo read the new
path; the Makefile links instruments.c into fm_demo, q_demo's objects and
drum_hits (the game takes it with src/audio/*.c).

**The game does not read it.** Its instruments come out of SoundData.bin's
image (smps.c's set_fm_ins), as before; only fm_demo and q_demo read the
file, until the song format builds the image from it. So it is out of the
manifest (make_manifest.NOT_READ_FILES, as songs.json): 903, unchanged.

**Names offered.** `docs/instrument_names.md`: a proposed name for each of
the 105, for the song files, with who plays it and where its notes sit --
from a one-off run of every song (180 s) and effect (20 s) through smps.c
with the instrument hook (not shipped). Effects' instruments by their
effect; songs' by the first song and the part (`chords` for FM3-FM5 in
chord mode, else bass/low/mid/high); 64, the bass under 21 songs, `bass`;
eleven that no song or effect keys a note with, `unused_<n>`. A first try
walked the streams in Python and attributed some wrongly (raw-frequency and
pitch-slide notes take more bytes); the sequencer's run is the one used.
Not in the JSON until the owner agrees them.

**Proof** (b136d against b136c): every file the same -- 112 PASS, the 580
route files byte for byte, crt, sim, patches, audio_check, attract.wav, the
boss trials, probe_check, the build's warnings. Decoded independently
against the uploaded containers: a standalone program around
instruments_load, 26 of 26 register numbers and 2730 of 2730 instrument
bytes; a separate Python decoder, the same. Refused, each named, 21 kinds
of bad edit; roundtrip_json's packer accepts and refuses the same 24 test
files. Re-extracting changes nothing else (SoundData, the frequencies,
songs.json, the credits, the dissolve order, the ramps). fm_demo's WAVs
for all 105 instruments and the smoke test, and q_demo `--instrument`'s Q
and chip WAVs for twelve, identical to the old build's on the old files;
q_demo, drum_hits and fm_probe build with Q, no warnings. A reader takes
an edit: instrument 65's attack rates set to 8 in a scratch copy change
fm_demo's survey from 64 from 1.90 s, where 65 starts, to 4.13 s. `make
roundtrip` (with art.zip and the drum file, sent after the first pack):
level 1 909 of 909 (910 less the two containers, plus the JSON), manifest
903, every level 2 kind the same, JSON tables 44 of 44 (the FM instruments
2756 of 2756 bytes), 0 failures; `--prove-edits`: 44 edits,
instruments[65].total_level[0] +1 failing both levels, every one caught.

Delete on the owner's side: `assets/tables/FMInstruments.bin`,
`assets/tables/FMRegisterOrder.bin` (and, if still there, 136-136c's:
`tables/CyclingPal_SegaLogo.bin`, `EndingCreditsFadeRamp.bin`,
`FadeEffectArray.bin`, `music/PCMDrums.bin`, `FMFreqs.bin`).

**After the build** (The owner): the instrument names agreed (a song is not an
instrument, being several; he likes that the effects are instruments) --
docs/instrument_names.md says so, the names to go into the JSON with the
song format. And a direction: palettes go. He never wanted to keep them;
the art goes full colour all the way through with every palette effect
rebuilt as a colour effect (Mother Brain's palette swap among them), the
palette path kept callable as a reference and unused once replaced. Task A
first (136e the credits, 137 on the songs), then the palette survey;
README's "Not done yet" and NOTES "Next (after 136d)" have the order.

## Build 136e: the credits as text

Task A's sixth kind: the ending's credits, the last table container
(`tables/EndingCredits_Script.bin`, "PS2B"), made text beside the window
text, as the handover asked ("text like the scripts").

**Read**: the ending's credits loop and button wait (loc_7824, loc_785C)
and the credits writer (loc_787E, ps2.asm 10953-11013) in full, and the
script itself (EndingCredits_Script, 11173-11331); ending.c's credits code
(credits_start, credits_byte, credits_write_row, credits_step) and loads;
table.[ch]; extract_tables.py; extract_script.py and script.c (the
scripts' text format); gen_script_ids.py's file loop (it reads every .txt
under assets/script with a "bank 0x" header line); make_manifest.py;
roundtrip.py's wrapping, level 2, report and the scripts' check;
assetfs_next_line; the ending route.

**What the writer does**, beyond build 136's summary. It is called once
the scroll reaches a new row (every eighth step) and writes the row 256 px
below the top of the view. A counter ($F770) gives each text line two
blank rows after it: a line closed by $C3 has one, a line closed by $C4
or $C5 has 28. In a line, a byte under $14 is a blank run of n + 1 cells,
$20 one blank cell, anything else a letter: tile $4BF + the byte with bit
5 cleared, palette line 1 if bit 5 was set. The credits' alphabet (loc_B6C2C,
over the font's first 26 tiles) has capitals only, so the ROM's lower case
is a colour, not a spelling: lower case shows in the titles' blue ($A44),
capitals in white. Three things the port had not written down:

- The writer counts the row's 40 cells down with dbf. A line of exactly
  40 cells runs the count out on its 40th: the code falls into the space
  path, writes a 41st cell and carries on with the count below zero, and
  the end code's fill then loops some 65 thousand times, writing on through
  video memory. 39 cells is the true limit; the widest ROM line is 32
  ("produced and programmed", indent included). ending.c's writer stops at 40 instead, which was harmless
  only because no line reaches it.
- The end code ($C5) is never read by the writer: the loop stops when the
  next byte to read is $C5, straight after the row before it is written.
- THE END is followed by seven empty lines ($C1 alone): with its $C3 they
  scroll it 22 rows up before the credits hold for a button.

**The file.** `assets/script/ending_credits.txt` (asset_names.SCRIPT_FILES,
new: the containers that are text beside the window text). A header in #
lines says how to read it; then a line of the file is a line of the
credits as it lands on the screen. Its leading spaces are its indent, the
ROM's blank run (one byte for up to 20 cells); every other space is $20,
one cell -- THE  END keeps its two. Letters are their ASCII bytes, so the
case is the colour, as the ROM has it ("written and directed" in blue,
"CHIEMSHI" in white). A line ends $C1 (two blank rows) unless it ends
`{close}` ($C3, one) or `{section}` ($C4, 28); `{empty}` alone is $C1 with
nothing before it, `{end}` alone is $C5. # lines and empty lines are
skipped; the extractor puts an empty line after each `{section}` for the
eye. The first lines:

                      STAFF{section}

             written and directed{close}
                       by
                    CHIEMSHI{section}

The other ways considered: a line of the file a plane row (28 empty lines
a section, and a gap's meaning by counting them), and a section a
paragraph with its end implied (deleting an empty line would merge two
sections unseen). Tags at a line's end say what the ROM's code says.

**The game.** src/core/credits.[ch]: `credits_load` into a CreditsScript
(4096 bytes; TABLE_MAX's 512 would have left an editor 71). It refuses,
naming the line: a character that is not a letter or a space (the byte,
or the character when printable, and its column), a tag it does not know
or one out of its place (`{empty}` and `{end}` stand alone), a line wider
than 39 cells, a line after `{end}`, no `{end}`, and more than 4096 bytes.
A carriage return anywhere is dropped (assetfs_next_line), so a Windows
checkout reads the same. The byte codes moved from ending.c to credits.h,
which both use; ending.c's g_credits is a CreditsScript and its writer
reads the same bytes as before. table.c's `table_load_bytes` and
`table_load_words` (the "PS2B"/"PS2W" container readers) had no caller
left and are gone.

**The tools.** extract_tables writes the text (`write_credits`: the bytes
to lines, `credits_text`; refused, raising, if the text would not give
the ROM's bytes back through `credits_bytes`, which reads as credits_load
does), into script/ beside its outdir (`--script` to say otherwise), and
deletes the `.bin`. Its stale files are deleted last now (`delete_stale`),
once everything is written. roundtrip.py: `write_credits` wrapped (kind
"credits", the label's bytes), `pack_credits` reading the file as
credits_load does (its own reading of the bytes: newline splits, carriage
returns dropped, the 255-byte line), a "credits" row at level 2; the
report's "containers" sentence names SoundData.bin alone. Manifest 903 ->
904.

**Proof** (b136e against b136d): every file the same -- 112 PASS, the 580
route files byte for byte, crt, sim, patches, audio_check, attract.wav,
the boss trials, probe_check, the build's warnings. Decoded independently:
a standalone program around credits_load gives 441 of 441 bytes as the
uploaded container. Twenty test files through credits_load and
pack_credits, which agree on every one: refused, each named, a digit, a
full stop, a tab, a UTF-8 letter, a misspelt tag, a tag mid-line, an
indented `{empty}`, text before `{end}`, a 40-cell line, a line after
`{end}`, no `{end}`, a stray `}` and 5841 bytes; the same 441 bytes from
the file with CRLF line ends, with comments and empty lines added, and
with no final newline; a 39-cell line (indent $13 $0A), a 25-cell indent
($13 $04) and trailing spaces loaded as written. The extractor's encoder
agrees with both. Re-extracting writes the same file. The game reads it: in
a scratch copy, "mechanical design" made "MECHANICAL design" fails the
ending route at its colour check (line 209, expect pixel 98 36 $A44: index
1, $EEE -- the M is white), the shot showing MECHANICAL white and DESIGN
blue; and the headless game started with a digit in the file names line 28,
column 24. `make roundtrip`: level 1 909 of 909 (the text in place of the
container), manifest 904 as checked in, credits 1 of 1 and every other kind
the same, JSON tables 44 of 44, 0 failures; run on a scratch copy with the
capitalised title it fails both levels, the second naming byte 126 ($4D,
the ROM's $6D). `--prove-edits`: 44 edits, every one caught (the JSON
tables, unchanged here).

Delete on the owner's side: `assets/tables/EndingCredits_Script.bin` (and,
if still there, 136-136d's: `tables/CyclingPal_SegaLogo.bin`,
`EndingCreditsFadeRamp.bin`, `FadeEffectArray.bin`, `FMInstruments.bin`,
`FMRegisterOrder.bin`, `music/PCMDrums.bin`, `FMFreqs.bin` -- his upload
still had all seven).

## Build 137: the songs and effects as text

Task A's last kind: `music/SoundData.bin`, the sound driver's data as the
ROM's bytes behind a "PS2S" tag, made text. The owner (after 136): "hex bytes
for notes is not human accessible"; the design agreed after 136c-136d
(NOTES, "Next (after 136c)" (3)): notes by concert name, each track's
starting state on its header line, state changes as words, `heard`
comments where the transpose moves a note, `tie` for cfE6 and a length,
drums and instruments and envelopes by name.

**The record corrected.** NOTES and this file (Build 136a on) said the owner
sent three files of SMPS notes (ConvNotes.txt, PS2_Notes.txt,
PS2_SmpsRAM.txt) and 136d credited SetFMIns' register order to them. He
has no such files and does not know where the names came from; 136d read
the order from the ROM's own table (loc_BA818). The reference is ps2.asm
and smps.c. NOTES "Next (after 136e)"'s "ask for them" is withdrawn.

**Read**: the driver's input and track loop (SoundDriverInput,
UpdateTracks, UpdateTrack, UpdateDrumTrack), the four ways a note is read
(ProcFMTrack, ProcFMTrack_Sld, ProcFMTrack_Raw, ProcFMTrack_RS) and
TickMultiplier, FinishTrkUpdate, DoNoteStop; the envelope routine (DoModEnv)
and its four tables; every coordination flag (cfE0_PlayBGM to cfFF_Pan);
GetFrequency_A, GetFMFreq, ReadRawFreq, DoPitchSlide; the pointer reader;
PlayMusic and PlaySFX (the headers) and PlaySpcSFX; DoSoundQueue, DoTempo,
SetFMIns, RefreshVolume, DoNoteOn_A, DoNoteOff_A; the tables at the image's
start; smps.c's loader, note parse, get_frequency_a and flag handlers;
extract_music.py in full.

**What the reading added.**

- A header is nine bytes: flags, channel, tick multiplier, the stream's
  offset, transpose, envelope, instrument, volume. Its flags can start a
  track in a mode: 59 effect tracks start in pitch-slide mode ($A0), 8 in
  raw-frequency and pitch-slide ($A8); two set bit 6, which nothing reads.
- A note is read by the mode: raw-frequency mode takes a word then a length
  (always a length, any value); pitch-slide mode a note (or rest), a byte
  the driver skips (ProcFMTrack_Sld's "dummy" -- 44 of them are not 0), a
  speed and a length; chord mode one more note byte for each extra part,
  read raw (GetFMFreq's six bits make $C1 the same as $81 plus 64).
  Raw-frequency mode is the one fixed when the read begins:
  ProcFMTrack_Raw never looks again, so `raw_freq on` just before a note
  takes effect at the next one; smps.c looks after the flags. No stream
  does it -- the walk checks -- so nothing differs.
- cfE6 sets the hold bit only when a length follows (a tie); before a note
  it does nothing (46 times). cfE7 lets the note go and holds silence for
  the length (105 times, always with a length).
- The vibrato envelopes 3 and 4 are named by no header and no stream
  (docs/audio.md's "the effects'" was wrong).
- Ten loops land on a note's length byte: the note plays once, then each
  time round the loop reads the length alone, as `again`.
- 77 bytes are played by two songs or effects: Rise or Fall and The Place
  of Death (39), Pause and Unpause (22), Exciting Town and Pleasure (8),
  Advanced and Mystery (8). Each pair is one file.
- The image: the tables (root words, priorities, tempos, envelopes, the PAL
  tables at $100 and $120, read there by address, the pointer tables), the
  song headers, the instrument pointers and instruments -- 14 and 15 are
  the same bytes stored twice, 56-64 one copy, two pairs overlapping by
  four bytes -- the effects, each header followed by its streams, the
  driver's code (zeros), the frequency table, then the song streams, not
  song by song: the short streams of Pressure, Dream, My Home, Exclaim,
  The Place of Death and Power sit interleaved. 61 bytes are reached by no
  stream (48 after the frequency table, 8 among the streams, Zan's 4,
  Slasher's second header -- its count is 1 -- and five padding bytes).

**The format** (docs/audio.md, "The song text", in full; README, "Asset
formats"): `assets/music/sounds.txt` (the ids, priorities, the song files,
the instruments' storage, the driver's code, the order of the song
blocks), `envelopes.txt`, `effects.txt`, `songs/<name>.txt` (20 files, 23
songs). Instruments by docs/instrument_names.md's names, now each
instrument's `"name"` in fm_instruments.json (asset_names.INSTRUMENT_NAMES);
the envelopes named in asset_names.ENVELOPE_NAMES. Other ways were
considered: one file a song with the shared passages called across files
(needs labels across files and a file order for the reader), and the
blocks named by address (not readable).

**The tools.** `tools/song_text.py`: Names (note_frequencies.json's names,
checked a semitone a step; drums.json's; the instruments' and envelopes'),
`walk` (a track as the sequencer reads it: flags, the modes, loops with
their counters, calls with their returns), `disassemble` (the image and
the ROM's labels to the files; a byte read two ways stops it), `assemble`
(the files to the image, every error as file:line), `annotate` (the heard
notes, by walking the built image), and `check` / `annotate` on the
command line. extract_music writes the text (`write_song_text`) after
building it back and finding the image's bytes, and puts the names into
fm_instruments.json. roundtrip.py: kind "songs" at level 2
(`pack_songs`, assemble), "songs and effects 1 of 1"; make_manifest left
the text out (the game did not read it yet).

**Proof**: the text built back into the image byte for byte (23593 of
23593); `make roundtrip`: level 1 932 of 932, manifest 904 as checked in,
songs and effects 1 of 1, 0 failures. The heard notes agree with
smps_trace's names (the title's melody heard C#5 C5 C#5 G#4, as 136c
found). Nothing under src/ changed for 137; it was not handed over on its
own (137a follows in the same pack).

## Build 137a: the game builds its sound image from the text

**The loader.** `src/audio/songtext.[ch]`, `songtext_build(music_dir,
reader, SmpsData *)`: the note names, the drum names, the instruments
(`instruments_load_named`), sounds.txt, envelopes.txt, effects.txt and
each `song_file`, laid out as assemble lays them (docs/audio.md, "Build
137a"). It refuses, naming the file and line, a word or a name it does not
know, a number out of range, a label, block, song or effect given twice, a
block not placed, ids out of order, the envelopes past $100, an image past
64 KB. `smps_load(SmpsData *, music_dir)` reads the frequencies then calls
it; SoundData.bin's reader is gone. Callers: audio.c, patches_check,
smps_trace, q_demo (2), fm_probe (2); q_patch_template.py builds the image
with song_text.py (`--music`). The Makefile links songtext.c and
instruments.c into smps_trace, patches_check and Q_DEMO_C.

**The instruments' names** (137b's part): `instruments_load` refuses a
name that is not one word (letters, digits, `_`, at most 31), a name
given twice, and names on some instruments but not all;
`instruments_load_named` returns them.

**The last container.** extract_music no longer writes SoundData.bin and
deletes it; `.bin` is off make_manifest's skip list and roundtrip.py's
listed-files filter; roundtrip's report says no container is left.
fm_instruments.json and the song text are in the manifest: 904 -> 928.

**Proof** (b137a against b136e): every file the same -- 112 PASS, the 580
route files byte for byte, crt, sim, patches, audio_check, attract.wav, the
boss trials, probe_check, the build's warnings (none new). Decoded
independently: the C loader's image under ASan and UBSan, 23593 bytes, the
same as SoundData.bin's image and its header's fields (special table at
494, 31 song ids, 56 effect ids). The game reads the text: in a scratch
copy, the title melody's first note made D6 fails audio_switch at its
sound-write hash (line 27: 2143 writes, $9E449769 for $15673FE1). Refused,
naming file and line, in both readers: a note `H6`, a jump to a label no
file has, a length of 300, two instruments with one name. `make roundtrip`:
level 1 931 of 931 (932 less SoundData.bin), manifest 928 as checked in,
songs and effects 1 of 1, every other kind the same, "no container ... is
left", 0 failures; `--prove-edits`: 44 edits, every one caught. With Q
(Q_DIR, q_tar.gz): q_demo, fm_probe and drum_hits build with no warnings;
against the b136e tree's build on SoundData.bin, q_demo's title theme (20
s) and My Home (15 s), Q's and the chip's WAVs, and `--instrument 66` both
ways, byte for byte the same; fm_probe's Step Up melody (`--solo fm1`) the
same measurements. (fm_probe's first build failed: the new `music` string
shadowed a `u8 music` in the same function; renamed `music_dir`.)

Delete on the owner's side: `assets/music/SoundData.bin` (and, if still there,
136-136e's: `tables/EndingCredits_Script.bin`, `CyclingPal_SegaLogo.bin`,
`EndingCreditsFadeRamp.bin`, `FadeEffectArray.bin`, `FMInstruments.bin`,
`FMRegisterOrder.bin`, `music/PCMDrums.bin`, `music/FMFreqs.bin`).

## Build 138: the palette survey (read-only)

**Nothing under src/ or assets/ changed in this build.** It is the first
step of full colour (The owner's direction after build 136d; NOTES "Next
(after 136d)" (3)): a survey of every routine in the ROM that writes the
palette, written up as docs/palette_effects.md.

**Read**, from the code: every use of the live palette (`palette_table`,
$FFFFFB00) and the fade target ($FFFFFB80) by label or raw address,
PalettePtrs' destinations and the 26 calls of PaletteLoad1/PaletteLoad2,
the three writes to colour RAM itself, and every caller of each palette
routine: the four fades (PaletteFadeFrom/Out/DecreaseColor, PaletteFadeTo/
In/AddColor, loc_634E/loc_6382, loc_62CC/loc_631E) and their callers in
SegaScreen, TitleScreen, LevelScreen, BuildingScreen, BattleScreen,
GameOverScreen, IntroScreen, EndingScreen, the roof and flight (loc_C572,
loc_C5EA), Ustvestia's lesson (loc_C7AC), Gaira's control panel (loc_CC10)
and the dream (loc_D59C, loc_61F4); PaletteCycle; the level cycles
(loc_6430, loc_64E8 and their tables); the building cycles (loc_66F6); the
battle's lights (loc_67B8 and its tables), the hit flash (Obj_CharactersBattle,
loc_1BA8/loc_1BE4) and the Nei Sword's cure (loc_1C1C); the enemy colours
(loc_796, loc_8D4); the credits' fade (loc_6AE0); the damage floor
(loc_7108) and the approach to Mother Brain (loc_70D6); the library's
overlay (loc_C4B2); Tyler's palette (loc_CD50); LoadPortraits and its
callers; LoadLevelData's loads and patch; VBlankSub_1C_24, the vertical
blank's table, the pause loop and VDPSetupArray. On the port's side:
palette.[ch], video.h, every caller of palette.c, game.c's fades and
`level_palette_cycle`, `building_palette_cycle`, `palette_cycle_sega`, the
dream's effects, the damage floor; battle.c's loads, fade and
`palette_effects` / `mother_brain_lights`; battle_obj.c's flash; ending.c's
page palettes and credits fade; building.c's library.

**Found.** One palette a frame (the live palette DMA'd each vertical blank,
no raster changes, no shadow/highlight, the backdrop always line 0 entry
0), and the fades block. Four kinds: loads (the picture's own colours),
whole-palette operations (the four fades, the dream's line-3 fade, the
damage floor's white field: colour maths on the final pixel), index-
dependent operations (the Sega sweep, the level cycles by tileset, Gaira's
panel, Tyler's monitor, the Nei Sword's light, Mother Brain's chase, glow
and rainbow, the hit flash, the credits' fade, the library's overlay: a
slot map with the picture), and the backdrop. Mother Brain's "palette
swap" is her three cycles of lights. The dissolves have no palette side.
The ROM recolours shapes: 126 enemies from 42 shapes, Motavia's dungeon
tiles under 11 palettes (24 tileset-palette pairs in all), scene objects
in lines 2-3 under each screen's colours.

**Four differences from the ROM** (not fixed; docs/palette_effects.md,
"What the port does today"): the fades to and from black move every
channel at once where the ROM moves one channel a frame (done in 7 frames
of 21); the arrival at Mother Brain fades through black where the ROM goes
through white; the damage floor restores the shown palette where the ROM
restores the level as loaded; the library's overlay palette is not loaded
(building.c took PaletteLoad2 for a target-only load), so the graph's
hidden lines never appear.

**Proposed** (The owner to see first): 138b the reference fixed; 138c a colour
plane beside the index plane, every route drawn both ways; 138d the effects
on it; then the assets a kind at a time. Questions in the page's last
section.

**Asked mid-job** (The owner): an edit to "the song .json for Phantasy" --
notes dropped a couple of octaves, C6 to C3 -- changed nothing he could
hear. `assets/music/songs.json` is a listing of the ROM's sound offsets that
no loader reads (make_manifest's NOT_READ_FILES since build 132a); the notes
are in `assets/music/songs/phantasy.txt`. Checked in a scratch copy with
`smps_trace --song 0 --frames 3000`: two `C6` notes on the fm1 line
(transpose -12) changed to `C3` move the traced notes from C5 to C2 at ticks
15 and 1052 and nothing else, so an edit to the text reaches the sequencer.
A `make dist` game reads its built-in pack, not assets/, until `make dist`
is run again; the plain `ps2` reads assets/ at each start. A `# heard:`
comment is never read.

## Build 138a: `make dist` in a tree outside git

The owner ran `make dist` on his Linux tree and it stopped:
`make_dist: git archive failed: fatal: not a git repository`, after
`make_dist: no LGPL-2.1 text found for licenses/; add one`. Nothing under
src/ or assets/ changed; tools/make_dist.py only.

**Read**: tools/make_dist.py in full, the Makefile's pack and dist rules,
.gitignore. The package's `source.zip` (the port's source, which the LGPL
asks to go with a program linked statically with Nuked-OPN2) came only from
`git archive HEAD`, and the README's commit line from `git rev-parse`; a
tree the tarballs are unpacked over has no git, so `make dist` could not
finish there. The LGPL-2.1 text was looked for only in MSYS2's
share/licenses, so off Windows it was never found (a warning, not the stop).

**Changed** (make_dist.py): `source_zip` takes `git archive` of HEAD in a
git repository as before; outside one (`in_git`), the tree's own files
(`tree_files`): everything but NOT_TREE_DIRS (.git, build, dist, saves,
route_out, fuzz_out, listen, __pycache__), NOT_TREE_FILES (objects,
libraries, .exe/.dll, .pyc, archives, WAV/FLAC) and programs by their first
bytes (ELF, PE, Mach-O), less NOT_SOURCE as before, every text file
scrubbed as before (`put_scrubbed`, shared by both). README.txt says "Built
from a tree outside git" there, and the run says so on stderr. Off Windows,
`licenses/LGPL-2.1.txt` is the system's copy (LGPL_SYSTEM: Arch's licenses
package, `/usr/share/licenses/spdx/LGPL-2.1-only.txt` and the older
`common/LGPL2.1/license.txt`; Debian's and Ubuntu's
`/usr/share/common-licenses/LGPL-2.1`); without one the run says where it
looked and makes the package anyway.

**Checked** (here, with the uploaded tree's own `ps2` binary as the game,
since this sandbox has no SDL3 to link one): outside git, make_dist writes
dist/ps2-linux/ and its zip, `licenses/` with Nuked-OPN2.txt and
LGPL-2.1.txt (Ubuntu's copy), README.txt "Built from a tree outside git";
source.zip 1279 files -- assets 938, routes 112, src 105, tools 89, docs 20,
shaders 12, the Makefile, README.md, .gitignore -- and none of saves/,
build/, route_out/, listen/, fuzz_out/, the binaries, NOTES, CLAUDE.md, the
handover and questions files; the name put as "the author" 217 times. In a
copy made a git repository and committed, the git path as before: 1874
files from `git archive` (it carries route_out's tracked reports and story
checkpoints, which the tree-files path leaves out with the rest of
route_out/), README.txt naming the commit.

**Also seen**: his `make dist` printed no `python3 tools/make_pack.py` line
(the pack rule is phony and always prints) and failed at Makefile line 163,
where the uploaded tree's dist recipe is at line 188 -- the Linux tree he
ran it in may be older than the one uploaded.

The plan's letters move on one: the reference fixes are 138b, the colour
plane 138c, the effects on it 138d (docs/palette_effects.md).

## Build 138b: the mods folder

The owner, after 138a: "shouldn't the notes be changeable by people without
source code?" -- the packaged game (`make dist`, build 125) carried every
file inside itself and read nothing beside it. Agreed design (docs/mods.md):
a `mods/` folder whose files are read in place of the game's own, the game's
own written out to `mods/originals/` on first start to copy from, and a
refused music mod set aside for the game's own music.

**Read**: assetfs.[ch] in full, home.[ch], main.c's start, audio.c's
`audio_load_music`, autoplay.c's harness write-out (`write_harness`, its
pack_id stamp), make_pack.py's head, platform_headless.c's WAV, proof.sh.

**Changed**: src/core/assetfs.[ch] (`assetfs_set_mods`, the mods read once
per pack file under assets/, `assetfs_pack_find` and the preload giving a
mod's bytes, `assetfs_mods_count` / `_path` / `_drop`), src/platform/home.[ch]
(`home_mods`; `home_write_originals`; the user data folder taken out of
`home_saves` as `user_folder`, the same for both), src/main.c (with a pack:
the folder set before the preload, the originals written and each mod named
after it), src/core/audio.c (the image built again without music/'s mods
when the song text refuses one; the drums likewise), tools/make_dist.py
(the player's README.txt points at mods/README.txt). Not guarded yet: a
refused picture, map or JSON mod (docs/mods.md, "When a mod is refused").

**Checked** with the packed headless game (`make dist_headless`) alone in an
empty folder: 938 files and pack_id.txt written out on first start, nothing
on the second; the attract WAV (2400 frames) byte for byte the tree build's
97141d6d...; phantasy.txt copied to mods/ with two C6 notes made C3 -- named
at start, the WAV changed; the same with a misspelt instrument -- songtext's
refusal at line 16, the mod set aside, the WAV the original's byte for
byte; two palettes inverted in mods/palettes -- both named, the last frame
changed.

**Proof** (b138b against b138a): every file the same -- 112 PASS, the 580
route files, crt, sim, patches, audio_check, attract.wav, the boss trials,
probe_check, the build's warnings (none). The routes run the tree build,
which has no pack, so none of the new code runs in them. A first b138b had
12 `-Wnonnull` warnings in assetfs.c: in a tree build the pack table is
empty, so GCC saw its paths as NULL; each loop now says a path is not NULL
before comparing it, and the proof was taken again.

The full-colour plan's letters move on one again: 138c the reference fixes,
138d the colour plane, 138e the effects (docs/palette_effects.md).

## Build 138c: the mods folder in every build

The owner, trying 138b: no mods folder after building and running -- he had run
the game built in the tree, and 138b gave only the packaged game one. "Why
would it just be in the distro?" Fair: it split the game's behaviour by how
it was built and hid the folder in the build he runs. Every build has it
now (docs/mods.md).

**Changed**: assetfs.c -- the mods looked up per asset path the first time
each is asked for (`mod_find`, a table of paths and bytes, a miss kept
too), used by the cache in both builds (`store`, `store_packed`) and by
`assetfs_pack_find`, now asked for a mod first in a tree build as well (the
song text, instruments, drums and tables ask it before the disk);
`assetfs_mods_drop` puts back the pack's bytes or, in a tree build, the
file under assets/. home.c -- `home_write_originals` with no pack writes
README.txt alone when it is missing; the README says where the game's own
files are in either build. main.c -- the folder set in every build, and
every start says where it is and how many mods are in use. .gitignore
`/mods/`; make_dist.py leaves mods/ out of source.zip; make_pack.py packs
without shaders/ when there is none, said, instead of a traceback (The owner's
`make dist` in the complete 138b tree, which travels without shaders/).

**The owner's `make shaders` on Arch**: SPIR-V and MSL written, every DXIL
failed inside Arch's directx-shader-compiler (an LLVM assertion,
"Error in initializing hung off uses for User"), its own fault, not the
shaders'. Linux draws with SPIR-V; DXIL is Direct3D 12's, for Windows. On
Linux: `make shaders SHADER_FORMATS="spv msl"`; the DXIL on his Windows
machine, where it has built before.

**Checked**: the tree build in a copy -- no mods: README.txt written,
`mods: mods, 0 in use`, the attract WAV 97141d6d... as before; the two-note
mod: 1e15a626..., the same as the packaged game's with it; the misspelt
instrument: set aside, the original's WAV. The packaged game from an empty
folder: the originals, the same three. make_pack.py with no shaders/: the
warning, 1006 files.

**Proof** (b138c against b138a): every file the same -- 112 PASS, 580
route files, crt, sim, patches, audio_check, attract.wav, the boss trials,
probe_check, no warnings. A first b138c run overlapped one started a turn
earlier in the same tree (each `make routes` clearing the other's
summaries); both stopped and the proof taken once, alone.

docs/palette_effects.md's order is in steps now, not build letters, so a
build between them renames nothing.

## Build 138d: the palette code against the ROM (full colour, step 1)

The owner took docs/palette_effects.md's picks ("go with your picks"): the
reference fixed first, the stepped fades kept, one full-colour picture per
use, the GPU path's shaders with the effects. This is step 1: the four
places build 138's survey found the port's palette path differing from the
ROM, each fixed and pinned by a route that fails on 138c. Read in build
138 and again for this: PaletteFadeTo / PaletteFadeIn / PaletteAddColor,
PaletteFadeFrom / PaletteFadeOut / PaletteDecreaseColor, loc_634E /
loc_6382, loc_62CC / loc_631E, LevelScreen's start and end, loc_70D6,
loc_7108 / loc_7140, loc_C4B2, PaletteLoad1 and PaletteLoad2; on the
port's side palette.c, game.c's fades, the walk's level change, the screen
entry, `level_dangerous_floor`, building.c's library.

**1. The fades to and from black.** palette.c `palette_fade_out_step`
(PaletteDecreaseColor: a word that is 0 stays; red down a level while it
has any, then green, then blue) and `palette_fade_in_step` (PaletteAddColor:
blue up while the word stays at or under the target's, then green, then
red, the comparisons the words', as `cmp.w`/`bhi` make them), in place of
`palette_add(-1)` and `palette_fade_step`, which stepped every channel at
once (done in 7 frames of 21) and are gone. Every caller moved: game.c's
`fade_out_step` / `fade_in_step` (every black screen change, Ustvestia's
lesson, Gaira's panel), game over's fade in, the dream's line-3 fade.
Route `fade_channel_order`: the Sega logo's white `$600` at the third frame
(`$666` before), `$E60` at the tenth, `$EEE` at the 21st; after Start
`$EEC` (red first; `$CCC` before), `$EC0`, `$000`.

**2. The arrival at Mother Brain through white.** game.c: the walk's level
change with the arrival scene sets `g_level_through_white` and leaves with
`goto_screen_white`; the screen's entry takes it and starts
`begin_fade_in_white` (every entry white, the loaded palette as the target,
21 frames of `palette_fade_from_white_step`, a new FADE_IN_WHITE) -- what
LevelScreen does when event_flags is $E00. Route `mother_brain_white`: a
black corner `$008` three frames in (red first; `$000` before), white at
the 21st, the reload `$CEE` then `$CAC` (blue first), then `$1813`.

**3. The damage floor's restore.** `level_dangerous_floor` no longer saves
the shown palette; the white field's end copies `g->fade_target` back --
loc_7140's copy of $FFFFFB80, the level as loaded -- so the cycling entries
show their loaded colours until loc_6430 writes them. Route
`damage_floor_restore`: Gaira's floor at 3,6, the white field, line 2 entry
14 at `$44E` (level_gaira.png's (255,87,87); the pulse's `$004` before),
then `$008` at the cycle's next step.

**4. The library's overlay.** building.c's `LB_GRAPH_PALETTE` called
PaletteLoad2 $32 a target-only load and left it out; PaletteLoad2 writes
the live palette (PaletteLoad1 adds the $80). It now sets
`graph_overlay`, which game.c applies: `library_graph_overlay.png` into
the live line 1. `library()` takes the building state for it. Route
`library_graph_overlay`: a hidden line at 211,66 `$A22`, then `$EEE` once
$1012 has played (296 pixels change: the graph's lines in white, cyan
and red).

**Found while finding the pixels:** the four routes' checks were chosen by
shooting both programs frame by frame (the Sega fade in and out, the
arrival, the floor, the library) and reading where they part; each expected
value is the ROM's rule worked by hand (the fades' channel orders, the
PNG's loaded colour, the overlay's palette), not read off the new program.

**Proof** (b138d against b138a): 116 PASS of 116; the 112 old routes'
580 files the same byte for byte -- no checked-in shot falls inside a fade
-- and 9 new files, the four routes' logs and summaries (and the library
route's save); crt, sim, patches, audio_check, attract.wav (97141d6d...),
the boss trials, probe_check, the build's warnings (none) the same.

**The owner, after 138c**: the song text's words approved as they are (no
renames); he has edited songs -- told to keep edits in mods/, where no
tarball or `make extract-music` reaches.

For the owner to check in the emulator: the Sega screen's fade (white goes
through deep blue, then cyan, to white, and leaves through yellow-white and
cyan to blue, then black), the arrival at Mother Brain (through white), a
damage floor's flash in a dungeon with lights, the library's graph after
"overlap the graphs".

## Build 138e: the port's saves in the emulator

The owner checks the port against the ROM in RetroArch, and 138d's scenes are
deep in the game. `tools/to_srm.py` writes a port save into the cartridge's
battery RAM as the emulator keeps it, so the ROM CONTINUEs where the save
stands, and reads a slot back into the port's format; docs/emulator_saves.md
has the layout and the commands. Nothing under src/.

**Read for it** (in full): the save (SaveData), the checksum test
(loc_D676), the copyright test (loc_D6AA), the repair copy (loc_D6D4), the
power-on set-up (loc_D700), the load (LoadSavedData), the power-on check
"Let me check the backup data" (loc_D1E0 / loc_D20E), CONTINUE (loc_D3B6),
the Data Memory's save (loc_B5A4), the slot list (loc_104EC), the name board's
write (loc_B36C), SetCharNames, SetCharInitialStats (the record's offsets)
and VDPCharacterMaps; on the port's side gamedata.[ch] and json.c's
accessors.

**What the ROM keeps.** Eight 1024-byte blocks on the odd addresses from
$200001, $800 apart: slots 1-4 and a mirror copy of each $2000 above. A
block: the eight records, the flags, $C600-$C69D, a checksum word (the
16-bit sum of the 926 bytes before it), the 96-byte copyright string. The
power-on check repairs a bad copy from the good one and, with both bad,
writes RAM over the slot. CONTINUE tests $C69C's first byte and loads the
main copy whole.

**Found:** the last eight bytes of each character's record, which the
port's save has no field for, are the name as the window tiles draw it
(SetCharNames: four upper, four lower tiles, through VDPCharacterMaps).
CONTINUE does not rebuild them, so a block without them would show blank
names. The writer makes them as SetCharNames does; the owner's .srm has them so
in all eight blocks. VDPCharacterMaps' 208 pairs equal window.c's win_glyph
pair for pair.

**The converter.** `load_port_save` is gamedata_load field for field (raw_c600
first, the named fields winning; a JSON null told from a missing member as
json_get tells them; the first of two equal keys), `dump_port_save` is
gamedata_save's text byte for byte. `write` puts the block in a slot and its
mirror with $C69C = $FFFF (as every Data Memory save sets it), the file's
layout and size kept (found by where the copyright sits: odd bytes, even
bytes, or the 8 KB alone), and checks the result before keeping it: the
slot passes the power-on check, CONTINUE's RAM is the block, the block reads
back as the same save, no byte outside the slot moved. `read` reports what
the port's format cannot carry. `check` lists every slot as the ROM sees it.

**Proof of the converter** (`selftest`, 34 s; `make save_resave`, a small C
tool -- gamedata_load then gamedata_save, or gamedata_new_game -- so the
Python is held to the port's own C): the 61 saves under routes/saves read as
the C reads them, and each written into each slot passes, CONTINUEs to its
block and reads back as the same save; 4746 edited saves (every field of
three bases -- one with raw_c600 full of values no game writes -- removed or
given a string, a number, a boolean, null, an array or an object) read as the
C reads them; the owner's .srm (16 KB, data on the odd bytes, $FF between):
every block reads and writes back to the same 1024 bytes, slot 1 included
(ASS, level 2), and **the port's new game equals the ROM's to the byte** --
his empty slots hold the RAM the title's new game leaves (loc_D700 saved it
at the first power-on), which checks every record offset, the party block
and the flags against the ROM's own writing; a changed main copy is repaired
from the mirror and two bad copies are a lost slot, in all three layouts.
Mutations caught: the name tiles' order, the checksum's length, the
experience word, a fallback (max_hp's) that no route's save exercises -- the
last only once the edited saves were added.

**The scenes** (`tools/emulator_scenes.py`; the file
`Phantasy_Star_II__USA__Europe_.srm` handed over beside the tarball): slot
1 kept, slot 2 LIB (paseo_recorder.json, library_graph_overlay's start),
slot 3 LAB (biolab_recorder.json: the Biosystems Lab B1 at 79,20, damage
floors two left -- Gaira's 138d floor would lose the gear on release, the
prison's copy living outside the battery RAM), slot 4 NOAH
(emu_mother_brain.json, new: walk_mother_brain.json walked to the foot of
NoahF1's corridor, 33,40, as story.py walks it -- four fights -- mended to
0.9, then save-here). Routes `emulator_lab_damage_floor` (the white field,
line 2 entry 15 at its loaded `$08E` after it, loc_65D8's `$006` at the next
step) and `emulator_mother_brain_white` (the arrival through white from the
real save, then `$1813`) pin that each save reaches its scene in the port;
both pass on 138d as well. The port's frames of all four scenes (the Sega
fade too) in `ps2-port_138e_scenes.zip`: a video a scene, real speed then
slowed eight times, frames numbered with their ticks and a marked pixel's
word, and a sheet of the frames that matter.

**Not done**: the emulator side itself (no 68000 here): the check is the
ROM's routines worked through in Python, held to the owner's ROM-written file.
His core's name was not given; the file's layout settled what the writer
needs. The owner's first sight of the scenes in RetroArch is the check.

**Proof** (b138e against b138d): 118 PASS of 118; the 116 old routes' 589
files the same byte for byte, 6 new (the two routes' logs, summaries and
saves); crt, sim, patches, audio_check, attract.wav (97141d6d...), the boss
trials (25d3ca5f...), probe_check and the build's warnings (none) the same.

## Build 139: the songs as a tracker (the format, the writer, the proof)

The owner chose the tracker before full colour's step 2 (handover_139): the
songs laid out like FastTracker II, rows of ticks, a column group a
channel, so the parts can be read against each other and edited by
position. This build is the format, `tools/tracker.py` (export, a reader,
the proof) and docs/audio.md's "The tracker format"; nothing under `src/`
or `assets/`, and the game reads no `.trk` file yet.

**Read first** (ps2.asm, in full): SoundDriverInput, DoTempo, PlayMusic,
UpdateTrack and its FM and drum halves, ProcFMTrack and its slide, raw and
raw-slide variants, TickMultiplier, FinishTrkUpdate, StartPanAnim and
ExecPanAnim, DoNoteStop, ExecModEnv and DoModEnv, GetTrkPointer, cfHandler
and all 32 flags, GetFrequency_A and GetFMFreq, DoSoundQueue, PlaySoundID,
PlaySFX, StopAllSound, SilenceAll, FadeOutMusic, DoFading, RestoreBGMChn,
SetFMIns, RefreshVolume, DoNoteOn_A and DoNoteOff_A.

**Found in the reading.** DoNoteStop lets a note go 3 ticks before the next
event unless the byte stored right after it is a hold ($E6): so the song
text's bare `hold` (commented "does nothing") holds the note before it up
to the next one where it is that byte (32 places), and 54 ties follow a
note already let go (a change or a jump between them; the tie then holds
silence). A rest and a release, `again` and the note it repeats, make the
same chip writes. cfE5 copies transpose, vibrato and volume to the chord's
parts, cfEF the instrument and volume; `volume` alone changes FM3 only.
The order of the changes before a note matters in a few cells. cfF8's
stack is at $2C, $28, $24 and cfF7's counters at $22 + N, so a third nested
call overwrites counters 2 to 5, not counter 1 as smps.h and audio.md said
(audio.md corrected; smps.h's comment for the next build under src/). The
handover's "24 tempo uses" counted the header lines: one mid-song (Phantasy).

**Measured** (the timed walk, every track of the 23 songs): each song's
parts repeat after one length but Over's drums (1/12) and Movement Ver2's
(1/2); Movement ends; the parts go back to different places (Mystery's fm2
at 780, its others at 12 and 72). Phantasy's channels end within 12 ticks
of each other once each goes back to the latest place it can.

**The format** (docs/audio.md, "The tracker format"): `song`, `channel`
lines, then sections of `N rows x T ticks` with every channel side by side;
a column headed `fm1 = 03` plays section 03's again and is left blank (or,
`export --full`, written out and checked); cells are notes as heard (FT2's
`C-5`), drum names, `===`, `tie`, `...`, then `@N`, `hold`/`cut` where the
3-tick rule goes the other way, and the changes in the stream text's words in
the driver's order (instruments by name -- the owner's "real names, not hex");
`goto SS.RR` / `end`. Section lengths 96-384 ticks, chosen per song for the
least writing; rows mostly 6 or 12 ticks, finer where a section needs it.
Phantasy: 94 sections, 1,342 lines, 193 of 376 columns repeats.

**Proof** (`python3 tools/tracker.py prove`): every song exported, read
back, built with song_text.assemble and played by `smps_trace --regs` beside
the stream text: 23 of 23 PASS, every chip write equal frame for frame for
the written span plus two turns of the longest loop (182 to 28,920 frames).
Image 31,836 bytes (everything written out) against 23,593. Refusals tried:
an edited `= 00` column, a misspelt instrument, a note one tick off a
multiple of the tick, a chord one part short, a note past C#6, a `cut`
taken away before a tie with a change -- each names its file and line; a
note edited changes the chip writes from that frame. `--full` reads to the
same stream text as the default export.

**Not done**: the C reader (src/audio/songtext.c reading `.trk`, so a mod
edits a song in the packaged game), which file wins when both exist, and
where the `.trk` files live -- after the owner has looked at the format. No
proof.sh run: nothing the game builds or reads changed.

## Build 139a: the art dump completed (levels, windows, palettes)

The owner: "I want a PNG dump of all assets". The art dump (build 127, `make
art`) had the battle animations, portraits, cut-scenes and field sprites;
tools/art_export.py now writes the rest the game draws: `levels/`, every
level as one picture (plane B, plane A over it where it is a map, the
field palette on lines 0-1 and the level's on 2-3, as load_level loads
them; a plane that is a wrapping backdrop -- every dungeon's plane A --
beside it as `_backdrop_A`/`_B`, since it scrolls on its own), the water's
animated tiles on the line the blocks draw them with; `windows/`, the 59
text windows with the font at $500 on line 0 (WIN_BASE_FONT), the battle's
in the battle palette, plus the font sheet and the credits' capitals;
`palettes/`, every palette as swatches. 1,215 PNGs (882 before). Checked
against the game: Paseo's picture against new_game's 07_walked shot, the
player menu, a Shure floor. The dump is still an export: editing it does
not change the game. Nothing under src/ or assets/.

## Build 139b: the game reads the tracker files

(Its code did not reach the tree -- Build 157d, "Found"; build 168 made
it again from this section.)

The owner took the defaults for build 139's format: the layout as built
(sections side by side, `= NN` copies, `goto SS.RR`, `hold` / `cut`), the
files as `assets/music/songs/<song>.trk` beside the stream text, and the
game reading the tracker file where there is one, the stream text the
fallback. Read for it: tools/tracker.py whole, src/audio/songtext.c whole,
song_text.py's reader, docs/mods.md, assetfs's mods, audio.c's loading;
from ps2.asm only the gosub and loop routines again (cfF8_GoSub, cfF9_Return,
cfF7_Loop), for smps.h's comment -- the rest is build 139's reading.

**The files.** `make extract-music` (extract_music's new write_trackers)
writes every song's `.trk` beside its `.txt`, exported from the stream text
it has just checked against the ROM's image, and deletes a `.trk` for a song
that is gone. The manifest lists them (`make manifest`: 928 -> 951 files).

**The reader** (src/audio/songtext.c, "Tracker files"): tracker.py's
read_tracker, channel_events and channel_stream in C, writing straight
into the image builder the bytes song_text.assemble makes of the stream
text tracker.py would write -- docs/audio.md, "Build 139b", has the rules.
songtext_build finds each song file's `song` and `block` names first
(scan_songs), looks for each song's `.trk`, reads the song files that still
have a song of their own (passing over a replaced song's header lines),
then the tracker files in song order, and lays the image out with a
dropped file's blocks left out, the `bytes` lines kept only while a song
file is read, and the tracker blocks after the streams. next_line's word
splitting is its own function now (split_words), shared with the grid
lines, which are split at `|` first.

**Which file wins.** A `.trk` in the mods folder, a `.txt` there, the game's
`.trk`, the game's `.txt`: the second so a stream text mod (The owner's
phantasy.txt edits were made that way) is not hidden behind the game's new
`.trk`. songtext.c asks through songtext_set_mod_test, which audio.c sets to
assetfs's new assetfs_is_mod; the tools set none. A refused tracker mod:
the refusal by file and line, every music mod set aside, the game's own
music (docs/mods.md's rule, unchanged).

**The Python twin.** tracker.py's `game_music(music_dir, out_dir)` writes
the same choice out as stream text (the kept song files less the replaced
songs' header lines, `trk_<song>.txt` a tracker song, sounds.txt's song
files and streams to match); `music_from_trackers`, prove's, is it with
every song required; `tracker.py image OUT` assembles it. `smps_trace
--image FILE` writes the image the C builds. stream_text takes a label
prefix. `song_span` and `timeline` / `timeline_difference`: what a song
plays, as the tracker sees it, unrolled through each channel's loop over
the song's span -- song_frames uses song_span now.

**Also**: song_text.py writes "holds the note before up to the next" beside
a bare hold that is the byte right after a note, a rest or a length (29
bytes; build 139's "32 places" was a miscount -- counted again by the
disassembler and by the timed walk, per song and per track, 29 each time),
"does nothing" beside the other 17 bare holds and releases; `make
extract-music` rewrote seven song files' comments, nothing else. smps.h's
comment: a third nested gosub overwrites loop counters 2-5, not counter 1.
make_manifest.py's comment, the README's rows, docs/audio.md, docs/mods.md.

**Proof.**
- The image: smps_trace --image against tracker.py image, the same bytes in
  25 cases -- every song from its tracker (31,836), none (23,593, the
  ROM's), each of the 23 songs alone (23,624 to 25,390), and the largest
  mix (every song but exciting_town, mystery and the_place_of_death:
  33,234). Build 139's handover guessed 55 KB for a mix; dropping a file
  whose songs are all trackers keeps it near the all-tracker size.
- The chip writes: smps_trace --regs, built through songtext.c, the music
  directory with the 23 tracker files against the stream text alone: 23 of
  23 songs the same, every write, frame for frame, over song_frames (182 to
  28,920 frames, 674 to 82,393 writes).
- The refusals: 34 edits read by both readers -- 31 the same message, file
  and line (an edited `= 00` column, a misspelt instrument, a length off
  the tick, a chord one part short, a note past C#6, a `cut` taken from
  before a tie with changes, an unknown word, `transpose`, `loop`, a pitch
  slide, a `goto` to no cell, to no section, misformed, a row past its
  section, sections out of order, a misformed section line, a channel twice,
  a channel's transpose, not a channel, a heading not a channel, a second
  song line, a missing tempo, a stray line, changes in an empty cell, a
  held drum, `hold` before a tie, `chord 1`, a cell past the columns, `@12`
  in a 12-tick row, words after `end`, `goto` in a note's cell); one edit
  was no error to either; the `song` line naming another song (the C reader
  refuses it, as game_music does; `tracker.py streams` does not ask); a
  header's unknown instrument carries songtext.c's "(fm_instruments.json)".
- In the game: the headless game reads the `.trk` (a length edited off the
  tick in the copy's phantasy.trk stops its music with tracker.py's message).
  Mods, the attract WAV (2400 frames): none 97141d6d...; a phantasy.txt mod
  read over the game's .trk (said on the console), a different WAV; an
  unedited .trk mod beside it wins, 97141d6d...; an edited .trk mod, a
  different WAV; a refused .trk mod set aside, 97141d6d....
- `make roundtrip`: level 1 954 of 954 (the 23 `.trk` re-extracted the same),
  level 2 "tracker songs" 23 of 23; 0 failures after `make manifest`.
- proof.sh b139b against b139a (both taken here, the routes and the game
  reading the tracker files): every file the same -- 118 PASS, 595 route
  files, crt 27, sim 38, patches 14, audio_check, attract.wav 97141d6d...,
  the Dark Force trial 25d3ca5f..., probe_check, no build warnings.
- q_demo (it plays phantasy from the tracker files), ps2_headless_q and
  fm_probe build without warnings with Q.

**On the owner's side**: `make extract-music` (writes the 23 `.trk` files and
the corrected comments) and `make manifest`. Then a song is edited in its
`.trk`; a `.txt` edit plays only for a song with no `.trk`, or as a mod.

## Build 139c: the asset format, written for the owner (docs only)

Job B of handover_139b, first step: the format of the art as the game will
read it, written before any code -- docs/asset_format.md. Nothing under
src/ or assets/. Read for it: docs/palette_effects.md, the README's asset
sections, tools/art_export.py, docs/battle.md's drawing, battle.json's
animation scripts, battle_obj.c's script interpreter and child objects,
and in ps2.asm the enemy overlay objects ($10 loc_3166, $11 loc_31BC).

Found while writing it: no enemy's idle loop launches a second object;
183 of the enemies' attacks and techniques launch one or two overlays
(the Locusta's arm, claws, beams) -- separate objects with the enemy's own
art and map and their own frame scripts. The first draft argued the
overlays had to stay apart because they draw over the enemy boxes and the
body under them; build 100b's reading says nothing of the boxes is above
either, so the draft was corrected before it was handed over: they stay
their own frame sets because the game moves and times them as their own
objects.

The owner's first answers (after 139b): HD is a bonus feature (native the
game, HD optional, 4x default); HD timing may be finer -- in-betweens
inside a native frame's ticks, the game's timing unchanged. He asked what
"painted levels", "glow mask" and "overlays" meant and for the folder
layout as a question; the doc now explains each and puts the layout as
three choices. Still open: painted HD levels, glow masks or animations,
overlays as their own frame sets, the layout (and one description a thing
or a kind), the `source` notes.

## Build 139d: Q 1.5 (its new header layout, and the drums' noise kept)

The owner updated his Q clone ("q lib was updated so needs a fix"). The Q he
had (master of late September, before 1.5) and the one he updated to (1.5.1,
dated 7 October) were both uploaded.

**What broke.** Q 1.5 moved the generators (envelope, ramps, noise) into
`q/synth/gen/` and the band-limited oscillators (saw, square, pulse) into
`q/synth/va/`; the names inside are the same. q_synth.cpp no longer
compiled, and -- the quiet half -- the Makefile knew a Q clone by
`q/synth/envelope_gen.hpp`, so `Q=auto` no longer found his clone and built
the game without Q, saying only "(no Q in ...)". Checked: with the old
Makefile and Q 1.5 in `~/GIT/Q`, GAME_Q=0.

**The fix.** q_synth.cpp takes either layout (`__has_include` of the new
envelope generator); the Makefile's `Q_HAS` is a shell test of a directory,
true for either, used by the clone search, `Q=auto` and the Q object's
recipe. With each Q in `~/GIT/Q`, GAME_Q=1; with none, 0, as before.

**What else Q 1.5 changed, measured.** Q's own checks
(`tools/proofs/qproof.sh`) were taken with both Qs on this sandbox's g++
13.3 from the same source: every FM file the same (the PM and subtractive
voicings, every fm_probe note, effect and alias), the drums not. Q's
`white_noise_gen` read its 32 bits unsigned before 1.5, so its noise ran
from 0 to 2 (white noise plus a constant 1, against its own comment's -1 to
1); 1.5 reads them signed. Through the drums' band-pass and high-pass
filters most of the constant goes again, but not all: built with Q 1.5,
drum_hits moved every drum by up to a decibel in a band or at its peak
(the snare's peak -13.5 to -14.4 dB, the open hat's lows -74.8 to -72.8),
q_demo's trim pass moved the songs' peaks by up to 0.6 dB, and the Q attract
WAV's peak by 0.4 dB. The drums were voiced by ear on the old noise
(110b-111), so q_synth.cpp now has its own: `DrumNoise`, the old generator
line for line (the same seeds, the same unsigned read). Nothing else in
what the port uses changed its output: `svf`, the envelopes (whose copies
now clone instead of sharing -- the port builds its voices in place, so
nothing copied one), `dc_block`, `cubic_clip`, the phase and the
oscillators, `hermite_interpolate` (1.5 only adds a six-point Lagrange
beside it). Q's licence is MIT from 1.5 (Boost before); README and
make_dist.py's notes say so (`make dist` copies the clone's LICENSE, as
before).

**Proof.** Q's set: q_old (the old Q, the old source) against q_new2 (Q
1.5, this source) and q_old2 (the old Q, this source): every file the same
-- the Q attract WAV, audio_check_q (peak -11.9 dBFS, -0.3 dB against the
chip), the trim pass, drum_hits, every fm_probe WAV and report, the steal
report -- but attract_q.log's line naming its own path. (q_new, Q 1.5 with
Q's noise, is the measurement above.) The game's set, b139d (proof.sh, one
job, 82 s): 118 PASS, 595 route files, crt 27, sim 38, patches 14,
audio_check -10.0 dBFS, attract.wav 97141d6d..., the Dark Force trial
25d3ca5f..., probe_check 3 ok, no build warnings -- the plain targets
compile none of what changed. No warnings from the port's code with
either Q (Q 1.5's own headers warn under -Wextra when not -isystem, as
before: `fast_inverse`'s pun, an unused parameter in `hold_line_gen`).

**Not changed:** tools/setup_msys2.sh still checks Q out at 6cd1aba (the
commit last proved on Windows, build 128); the port builds with it and
with 1.5 alike. Changed: src/audio/q/q_synth.cpp, Makefile, README.md,
tools/make_dist.py; `make clean` then `make`.

## Build 139e: the Q toms' membrane and room

The owner, on the Q toms (139d's listening files): something wrong with them,
"maybe the note decays a little too fast or needs a tiny reverb? How are
the pitches?" Measured each tom struck alone through the chip and through
Q (docs/audio.md, "Build 139e"): the pitch right from 40 ms but 1-2
semitones sharp for the first 20-30 ms; the body falling about 1.5 times as
fast as the chip's; and after 60 ms a pure tone where the chip's keeps a
noisy band. Five versions (ps2-port_toms_q.zip): as now, a longer body,
that with a shell noise, a small room, both. The owner: the room "the best but
there's something radically different about the timbre of the tom that's
hard to place". Found: the ROM's tom is fourteen inharmonic membrane modes
dying at their own rates, the strongest overtone at 4.24 times the pitch;
111's was four overtones at near one level dying together, heard as a
chord; and the room held the sharp opening pitch as a faint second note.
Three more (ps2-port_toms_q2.zip): the chip's opening pitch, that with the
membrane, the ROM's sample in Q. The owner: "I like 7".

**Changed** (src/audio/q/q_synth.cpp only): `kTomModes`, the fourteen
modes measured in the ROM's tom sample, in place of 111's four overtones
(`DrumPatch.membrane`; `over_ratio`, `over_level`, `over_decay_s` gone);
the toms' starts 262 / 238 / 202 Hz (were 320 / 290 / 250); `TomRoom`, a
small Schroeder room behind the toms alone, running only from a tom to a
second after it, emptied by the bank's reset. The scratch build the owner
heard and this one: the toms struck alone the same WAVs (the PM voicing;
the scratch's room was one for every bank in a process, so its
subtractive WAV and the song carried the PM bank's tail); Secret Ways 42 s
one sample apart by 1 LSB, where this room empties after a second.

**Proof.** The game's set, b139e (proof.sh, one job) against b139d:
identical, 118 PASS, 595 route files, attract.wav 97141d6d..., the Dark
Force trial 25d3ca5f... -- nothing the plain targets build changed. Q's
set, q_139e against q_new2 (139d, Q 1.5): every FM file the same, the Q
attract WAV and audio_check_q the same (no tom in the title's 40 s), the
steal report the same; drum_hits' toms, fm_probe's drum $80 and the trim
pass moved, as docs/audio.md says (the songs' RMS within 0.1 dB, their
peaks up with the toms' attack; the trim left at 0.804). ASan and UBSan on
q_demo through Secret Ways: nothing but ym3438.c:660's known shift. No
warnings with Q 1.5.

## Build 139f: the music after a fight on the jet scooter

The owner: "I don't know if this is normal game behavior on the scooter, but
I noticed the fight victory music keeps playing when you're riding the
scooter after you win a fight." Not the ROM's: the port's.

**Read**: UpdateSoundQueue (the latch at $F640, then the one-byte
`sound_queue`), BattleScreen's music (UpdateSoundQueue $92/$93, so the
latch holds the battle's), the victory (`loc_F27E`: Movement written
straight to `sound_queue`, the latch left), LevelScreen from the top (the
mask at $2500 after ClearSpriteAndScroll), LoadLevelData (UpdateSoundQueue
with the level's music, ps2.asm:13296, then its art loads), the masks in
LoadSpritesInLevel, loc_7E02 and loc_9002 (only around their plane
writes), ObjJetSctr_Init ($D8 into `sound_queue` when the flag is set,
6797), VBlank and VBlankExit (SoundDriverInput every field, also with
vblank_routine 0), SoundDriverInput, DoSoundQueue and PlaySoundID ($D8-$DF
special effects).

**Found**: after a fight on the water, LoadLevelData queues Motavia's
music (not the latch's $92) and the scooter's init queues its engine into
the same byte. In the ROM the load spans fields with the vertical blank
on, so the driver has started the music before RunObjects runs the
scooter; the port's load is one tick, and the engine overwrote the music,
so Movement (going on as Movement Ver.2, $96) played on. The port's sound
log of jet_scooter_dam showed both queued at tick 1620. The same out of a
dam riding: Mystery played on over Motavia.

**Changed**: enter_level_walk lets the driver take the level's music at
once when it queued it (`audio_take_queue`: smps_take_queue, DoSoundQueue
and PlaySoundID without the tempo, fade or track step -- what a field in
the middle of the ROM's load does). `audio_play_sound` says whether it
queued. The sequencer keeps the music it last started (`music_started`,
the port's own), `audio_music_started`, and the route language has
`expect song $XX` for it -- `expect music` is what was queued, which an
overwrite cannot show. Route `scooter_music`: jet_scooter_dam's ride with
`expect song` at each level change; on 139e's game it fails at the first
fight ("the song playing is $96").

**Proof**: b139f against b139e (proof.sh, one job): every one of the 595
route files the same and the new route's 7 added, 119 PASS, attract.wav
97141d6d..., the Dark Force trial 25d3ca5f..., crt, sim, patches, audio,
probe_check the same, no warnings -- a load that changes the music takes it
a tick sooner, and no route's files showed it. ps2_headless_q, art_dump,
the probes, save_resave, smps_trace and fm_demo build without warnings.
The windowed `ps2` is not built here (no SDL3). Changed: src/game/game.c,
src/core/audio.[ch], src/audio/smps.[ch], src/harness/probe.[ch],
routes/scooter_music.route, docs.

## Build 140: the colour layer (full colour, step 2)

docs/palette_effects.md's step 2 and docs/asset_format.md's order 1, after
The owner's answers to the format (the folder layout A, glow masks): every
frame composed as colour beside its indices, every route drawn both ways
and the two the same; the palette path still the one shown.

**Changed.** `Framebuffer` has a colour plane, `rgb` (Rgb8 a pixel), beside
`px`. video_compose fills both: a pixel's colour is its index's in the
palette the frame is composed with, through the DAC ladder (cram_to_rgb8),
looked up once a frame; the backdrop likewise. The Escape menu's overlay
(sysmenu.c blit_tile) writes its colours from `overlay` beside its
indices. `fb_draw_from_env` reads `PS2_DRAW` (`colour`, else the palette);
`fb_palette_colour` and `fb_colour_differs` give and check the palette
path's colour. The headless platform's PPM and the SDL_Renderer
placeholder draw from either plane (`PS2_DRAW=colour`); the GPU chain
still draws the index plane -- its shaders take colour with the effects
(The owner's answer after 138c), and the startup line says so. main.c's frame
is static (280 KB with the plane, a quarter of Windows's main stack).
route_probe: `PS2_DRAW=colour` shots as RGB PNGs; `PS2_COLOUR_CHECK=DIR`
every tick's frame composed and held both ways, each shot written again
from the colour plane, a report a route (docs/harness.md);
tools/colour_check.py runs them and reads both PNGs of each shot back with
Pillow. The slot plane is not made: while the art is indexed it would be
the index plane again; with glow masks it is the composed mask, step 3's.

**Proof.** colour_check over every route (119): 223,755 frames composed
both ways and 330 shots read back both ways, none different; the route
files after those runs the same as b139f's (so a render every tick
changed no game state). The headless attract frame (2400 frames) and two
earlier ones (600, 1300) the same PPM from either plane; the attract WAV
97141d6d.... proof.sh b140 against b139f: identical in every file, 119
PASS, 602 route files, the Dark Force trial 25d3ca5f.... No warnings in
`make headless route_probe crt_ref patches_check ps2_headless_q art_dump`
and the probes. Not built here: the windowed `ps2` (no SDL3) -- its edit
is the placeholder's loop and a startup line; the owner's build is its
check. Changed: src/core/video.[ch], src/game/sysmenu.c, src/main.c,
src/platform/platform_headless.c, src/platform/platform_sdl3.c,
tools/route_probe.c, tools/colour_check.py (new), docs.

## Build 141: the effects on the colour plane (full colour, step 3)

docs/palette_effects.md's step 3: the colour plane made from a colour path
of its own -- never read from the live palette -- and shown, the palette path
kept beside it as the reference.

**Changed.** `Video` has `colours` (VideoColours, video.h): the pictures'
colours (a colour per palette entry while the art is indexed, written by
every load into the live palette or a fade target), the glow (the colours
the index-dependent effects write, each entry lit until its picture next
loads; entry 0 is the backdrop, the frame's own colour), and one whole-frame
effect -- a fade to or from black or white, or the damage floor's white
field -- with its step. video_compose takes each pixel's colour from the
picture colours or, where lit, the glow, then applies the effect pixel by
pixel (frame_effect): every channel's level on the DAC ladder
(colour_level, level_colour in palette.c; exact on the ladder, read between
its points off it) stepped one level a frame toward black or white, one
channel after another in the ROM's order (red first going up to white and
down to black, blue first coming down from white and up from black), a
channel taking as many frames as whole levels it has to go. A fade out
starts from what is shown: the frame, a screen already one colour (a fade to
black after one leaves it black, as the palette's black entries stay
black), or an unfinished effect under it. A fade in comes up to the
pictures' colours and ends at its 21st step. The dream's fade applies to
its picture's palette line only.

Every palette operation is one call in video.c that makes the palette
path's write as before and the colour path's: loads (`video_colours_load`,
`_clear`, `_copy` for the ending's preloaded palettes, `_set` for one entry:
the enemies' colours, Mother Brain's backdrop, the credits' lower case), the
effects (`video_glow`: the level and building cycles, the Sega logo's
sweep, Mother Brain's lights, the credits' fade; `video_glow_load`: the
library's overlay; `video_backdrop`: the hit flash, the Nei Sword's light,
Gaira's alarm), the buffers (`video_target_from_shown`, the fade target set
from the live palette; `video_shown_from_target`, the damage floor's restore
and the battle's fade's end), and the fades (`video_fade_begin` for the
fades in, which zero or whiten the live palette as before; `video_fade_step`
for every step; `video_flash_white`). game.c, battle.c, battle_obj.c and
ending.c call them; battle_enter, battle_tick and bobj_run take the Video
(the battle's loaded palette stays the palette path's target); Mother
Brain's chase reads the backdrop through `video_backdrop_is`. art_dump.c
gives the battle objects a scratch Video, as it gave them a scratch palette.

`fb_draw_from_env`: the colour plane unless `PS2_DRAW=palette`; the headless
platform and the SDL_Renderer placeholder say so when it is the palette.
route_probe's shots stay the palette path's indexed PNGs unless
`PS2_DRAW=colour`. The GPU chain still draws the index plane through the
palette (141a moves its shaders). `u64` in ps2_types.h.

**Proof.** colour_check over every route (119): 223,755 frames and 330
shots, none different, every route passing. The check catches a break: the
fades in stepped red first (new_game, 120 frames differ) and an effect's
glow not lit (damage_floor_restore and library_graph_overlay, 169 frames)
were both caught, and put back. No `video:` warning (a fade in stepped with
no begin, a fade out over two unfinished effects) on the ending, the
dreams, both roof routes, game over, the lesson, the Army Eyes' capture,
Mother Brain's battle and white arrival, the damage floor or a new game.
proof.sh b141 against b140: identical in every file -- 119 PASS, 602 route
files, the attract WAV 97141d6d..., the Dark Force trial 25d3ca5f..., the
headless attract frame (now from the colour plane) the same bytes as b140's
from the palette, and frames 600 and 1300 the same both ways. No warnings in
`make headless route_probe crt_ref patches_check art_dump` and the probes.
Not built here: the windowed `ps2` (no SDL3; platform_sdl3.c's edit is a
startup line). Not run: `make art` (art_dump's change is the scratch
palette's shape, the same values).

**Found, not fixed.** LoadLevelData on the Skure, cave and Dezolis dungeon
tilesets (1, 3, 6) sets the fade target's line 1 entry 7 to $ECA and loads
loc_412F8's 23 tiles to VRAM $7020 (tile $381 -- wrong: VRAM $3020, tiles
$181-$197, Build 141b; ps2.asm:13381); the port does neither. The tiles use only that entry and line 1's white. No pixel the port
draws on those tilesets uses line 1 entry 7 (counted over eleven routes:
Skure, the Dezolis towns' dungeons, the Esper mansion, Ikuto, the Prism),
and nothing found in ps2.asm names tile $381-$397 -- what draws them is
open.

Changed: src/core/ps2_types.h, src/core/palette.[ch], src/core/video.[ch],
src/game/game.c, src/game/battle.[ch], src/game/battle_obj.c,
src/game/ending.c, src/platform/platform_headless.c,
src/platform/platform_sdl3.c, tools/route_probe.c, tools/art_dump.c, docs.

## Build 141a: the CRT chain on the colour frame

docs/palette_effects.md's point 5: the CRT chain draws the colour path's
frame, the palette gone from it.

**Changed.** The model's source is a colour plane (`crt_source_colour`:
each gun's 8-bit value through `crt_value_linear`, CRT_GAMMA; crt_cram_linear
is that on the ladder's values). The beam table has a row for each 8-bit
value, CRT_BEAM_ROWS 256 (by DAC level, 8 rows, until now): the ladder's
eight values have the rows the eight levels had, the same doubles, so a
frame of the ROM's colours sums the same floats. `crt_tables_render` takes
the colour plane. crt_ref reads a route PNG's colour plane (each pixel's
PLTE colour through CRAM and back) for both the model and the mirror, and
its sim check has a third crop, `every_value`, a made-up frame with every
8-bit value in each channel in the crop -- most of them off the ladder, as
full-colour art will have them.

The shaders: `crt_down_rgb.frag.hlsl` (the frame RGBA8, each gun's value
the beam table's row; Frame and Beam, two textures) and
`crt_sharp_rgb.frag.hlsl` (the frame's colour as it is; Frame alone, and the
ladder uniform gone from its block) replace `crt_down.frag.hlsl` and
`crt_sharp.frag.hlsl` -- new names, so a tree with the old compiled shaders
says which file is missing and draws the placeholder rather than binding a
colour frame to a shader that wants indices. crt_gpu.c uploads the colour
plane (RGBA8, 320x224) and no palette texture; the beam texture is 256
rows. platform_sdl3.c hands the chain `fb->rgb`, or with PS2_DRAW=palette
the palette path's colours pixel by pixel. crt_gpu_check.c takes colour
frames too and checks the every-value crop on the GPU. Makefile's
SHADER_STEMS names the new two.

**Proof here.** `make crt_check` 27 of 27, its report byte for byte b140's;
`make crt_sim_check` 40 of 40 -- the title and fight crops' lines byte for
byte b140's, the every-value crop within one code of the model as they are
(SDR worst 1, 27 of 1,992,840 codes; HDR worst 1, 251 of 494,160).
proof.sh b141a against b141: the same in every file but crt_sim_check.txt
(its two new checks). Not built or run here: crt_gpu.c, platform_sdl3.c,
crt_gpu_check.c and the HLSL (no SDL3, no shader compiler) -- the owner's
`make shaders`, `make crt_gpu_check` and a look at the window.

Changed: src/core/crt.[ch], src/platform/crt_gpu.[ch],
src/platform/platform_sdl3.c, src/shaders/crt_down_rgb.frag.hlsl and
crt_sharp_rgb.frag.hlsl (new), src/shaders/crt_down.frag.hlsl and
crt_sharp.frag.hlsl (deleted), tools/crt_ref.c, tools/crt_gpu_check.c,
Makefile, CLAUDE.md, docs.

## Build 141b: the mist on the Skure, cave and Dezolis dungeon levels

The owner, looking at the 23 tiles: "a mist effect for cold areas", and from
memory, the mountain with the Maruera gum has it.

**Found.** LoadLevelData (ps2.asm:13374-13384) on level types 1, 3 and 6 --
the tileset number -- sets line 1 entry 7 of the fade target to $ECA and
decompresses loc_412F8 with the VDP command $70200000: VRAM $3020, tiles
$181-$197, not $7020 as build 141 (and engine.md since the extractor's
first note) read it -- the command's address bits are $3020. Those are the
tiles the three tilesets' block definitions reach past their own art (their
highest tile 407 = $197, art 317, 270 and 241 tiles): blocks 101-104 of
tilesets 1 and 3 and 64-67 of tileset 6, in line 1, high priority. Every
level on those tilesets but Noah's two and the underwater passage has a
plane A backdrop (the wrapping 16x8-block layer) made all of them -- the
mist. The port drew a block's tiles past the tileset's art as nothing, so
the backdrop was empty, and entry 7 stayed field_common's brown ($048).

**Changed.** extract_levels.py writes loc_412F8 as assets/tiles/
level_mist.png (the grey sheet, as the tilesets'; every other output the
same bytes); the manifest lists it. game.c's `level_mist`, at a level's
entry after its palettes and people art (and in the opening), on level
types 1, 3 and 6: `video_colours_set` line 1 entry 7 to $ECA (both paths),
and the 23 tiles placed at tile $181 (the sheet's padding left out).

**Proof.** uzo_maruera's new `expect pixel 5 8 $ECA` at the Maruera tree:
FAIL on b141a (the sky, index 49, $E80), PASS now. proof.sh b141b against
b141a: 49 route files differ, every one a shot on tilesets 1, 3 or 6 --
the mist over Mount Uzo, Skure, the Esper mansion, Ikuto, Menobe, Guaron and
Naval; the Noah and underwater-passage shots differ only in their PNG
palette's entry 23 ($048 to $ECA), no pixel drawn with it. Everything else
the same. colour_check on uzo_maruera, esper_door, ikuto_ground,
dezolis_prism_doors, gum_way_out, mother_brain_no and skure_newspaper:
3,428 frames, 18 shots, none different.

Changed: src/game/game.c, tools/extract_levels.py, assets/tiles/
level_mist.png (new), assets/manifest.txt, routes/uzo_maruera.route, docs.

## Build 141c: the menu says when the CRT chain is not running

The owner ran 141a without `make shaders`: 141a renamed the down and flat
shaders (crt_down_rgb, crt_sharp_rgb), his shaders/ held only the old
names, so crt_gpu_create found no `crt_down_rgb.frag.spv` and the platform
fell back to the SDL_Renderer placeholder (its startup lines said so). The
placeholder draws the flat picture only -- but the Escape menu went on
showing scanlines and the masks as available on his 3840x2400 panel, so
turning them off and on again looked like the CRT had broken.

**Changed.** CrtDisplay has `no_chain`: platform_sdl3.c's display_state
sets it when the GPU chain is not running (`p->crt_gpu == NULL`: no
shaders, no SDL_GPU device, no pipelines). crt_scanlines_why, crt_mask_why
(a mask other than OFF) and crt_hdr_why give CRT_NO_SHADERS for it, shown
as "NO CRT SHADERS", and the summary row reads OFF -- what is drawn. The
headless platform and the route probe keep the chain as present (the
default), so every other route reads as before; the probe's `display`
command takes `no-chain`.

**Proof.** routes/crt_no_shaders.route: a 3840x2400 fullscreen display
with `no-chain`, the CRT page, scanlines ON / NO CRT SHADERS, APERTURE
GRILLE / NO CRT SHADERS, summary OFF -- PASS (on b141b the probe does not
know `no-chain`, and the menu had no such reason to show). proof.sh b141c
against b141b: the same in every file but routes_pass.txt and the new
route's two files. Not built here: platform_sdl3.c's line.

Changed: src/core/crt.[ch], src/platform/platform_sdl3.c,
routes/crt_no_shaders.route, docs.

## Build 141d: `make` compiles the shaders it needs

The owner, after 141c's NO CRT SHADERS: shouldn't `make clean && make` just
make them? It should. The compiled shaders were only ever made by `make
shaders`, by hand, so 141a's rename left a built game with nothing for
the CRT chain to load.

**Changed.** The game's target has an order-only prerequisite,
`shaders-here`: every shaders/<stem>.<fmt> for this machine's format
(SHADER_FORMATS_HERE: DXIL on Windows, MSL on macOS, SPIR-V elsewhere)
that is missing or older than its src/shaders/<stem>.hlsl is compiled with
SHADERCROSS by a pattern rule, and the compiled files 141a's rename left
(crt_down.frag.*, crt_sharp.frag.*) are removed. Without shadercross, or
when a shader fails to compile, the game still builds and a WARNING says
which shader and that the game will draw without the CRT. `make shaders`
still makes every format for the checked-in set; `make clean` still keeps
shaders/ (checked in for builders without shadercross).

**Proof here** (no shadercross: a stand-in script that writes its output):
the four SPIR-V shaders made with the right stages (vertex for
crt_fullscreen.vert, fragment for the rest), nothing made on a second run,
only crt_across remade after touching its source, a failing compile
reported and the build going on, `SHADER_FORMATS_HERE="spv msl"` making
both, the six obsolete files removed; without shadercross, the WARNING for
the two missing ones. Not run here: the real shadercross on the new HLSL.

Changed: Makefile, README.md, docs.


## Build 142: the portraits drawn from their pictures (full colour, asset order 2)

docs/asset_format.md's order 2, the first part: the portraits as full-colour
pictures (tools/extract_pictures.py, `make extract-pictures`, written in
142wip: assets/art/portraits/, 27 things and portraits.json, window number
to thing) and the compositor drawing them -- step 1 of NOTES' plan for 142,
with step 2, its proof.

**Read first.** BuildingScreen (ps2.asm:11838): RolfPortraitArt to VRAM
$9400 (tile $4A0) and Pal_RolfPortrait into line 1 of the fade target in
every building but four -- the library (LibrGraphPortArt and the graph's
palette at the same tiles, loc_8304), the roof (loc_8318, neither),
Tyler's spaceship (loc_8326, VRAM filled with 0, LoadPortraits skipped)
and the Esper mansion (loc_8364, Lutz at $0020) -- then LoadPortraits
(11969): portrait_index 1-$1E's art to $8000 (tile $400), its palette by
PaletteLoad1 and PaletteLoad2, so line 2 of both the target and the live
palette. Win_RolfPortrait and Win_Portraits (24656) set $FFFFF72C to $A400
and $C400 for the draw: loc_980A (13918) writes each interior byte as that
word's high byte over the byte, tile $400 + byte, the frame's border cells
the font's (loc_978C). The library's graph window is WinID_LibraryGraph-
Portrait ($55, WinArt_LibraryGraphPortrait) through Win_RolfPortrait's
base: Rolf's tiles, the graph's art; Rolf's two windows ($3B, and $72,
which his house and the Governor open) draw WinArt_RolfPortrait. The tower
redraws its outside portrait's art (21) through the copy window (22,
another map), both PalettePtrs 38. Gaira's control panel cycles line 2
entry 5 -- the radar portrait's line -- as glow (building_palette_cycle).
Not acted on: the port's load_portrait loads its palette into the live
palette only, where LoadPortraits loads the target too; nothing in the
routes shows a difference (open).

**Changed.** src/core/art.[ch] (new): `picture_load` reads a picture's
description and frame "1" (png_load_rgba), refusing, naming the file, a
missing `size` or `at`, a frame that is missing or will not load, and a
PNG whose size is not the description's; `art_portraits_load` reads
portraits.json and each thing once (shared between windows), `art_portrait
(n)` window n's picture or NULL. load_assets loads them (required, as the
rest of the assets).

video.h: `VideoPicture` {picture, first_tile, count, layer, the plane cell
of its top left, the picture cells drawn this frame}, up to eight on the
Video (`video_pictures_clear`, `video_picture_bind`, which clips the cells
to the picture). draw_plane, for a cell whose tile is in a binding's range
and whose place is in the binding's cells (the latest binding first), takes
the colour from the picture at the pixel's place -- alpha 0 draws nothing,
part alpha blends over what is under -- or the glow's where an effect has
lit the tile's palette entry, and the index from the tile as before; every
other pixel as before. The Framebuffer's `pic` plane says which binding
each pixel's colour came from (0: none; sprites, planes drawn over and the
Escape overlay put it back to 0), `pictures` which picture each binding
was, `picture_pixels` how many: the route probe's report, and the plane
the HD compose (NOTES' step 3) will read.

window.c: WinSys's `picture` hook, asked by draw_entry for every window it
draws; a picture named binds over the interior cells this draw puts down
(frame columns lo + 1 .. hi - 1, so a window opening a column at a time
binds only what is drawn, and rows 1 .. rows - 2), its top left at the
picture's `at` cell. game.c's `window_picture` answers from what was
loaded: `g_portrait_loaded` (the portrait load_portrait put at $400 with
its line-2 palette; 0 after enter_building's VRAM clear, after Tyler's
second tower palette and after the flight's ship goes to $400) and
`g_rolf_portrait_loaded` (enter_building: not the library, roof, Tyler's
spaceship or Esper mansion). Window n's picture applies when the loaded
portrait is n or has n's art and palette (so the tower's copy window gets
its own picture over 21's art); Rolf's two windows get his only where his
art is. Only on the building screen. game_render clears the bindings with
the planes. sysmenu.c's overlay clears `pic` under it.

route_probe: the colour check's report adds, after its counts (the words
colour_check.py reads stay where they were), `N pixels from pictures in F
frames (things)`; colour_check.py totals them. probe.c: `expect picture X Y
THING|none` (not a player's command).

**Found and fixed: the tower's copy portrait.** The third route, for the
one window whose picture is not the loaded portrait's number, found the
port drawing the wrong map. At the Central Tower's desk with Nei gone,
"Something's wrong..." is followed by loc_C10A's `$0600 | $51`: the copy
window redrawn in place, instantly, and not pushed (ProcessWindows,
ps2.asm:13502: bit 2 of the word skips the background save, loc_99A8,
and the count; loc_978C writes it into the nametable over the outside
portrait's window). Its map is another picture from the same art -- the
guard running in, shouting "Omigosh! The lake is overflowing!" -- 100 of
its 120 cells different. The port keeps such a window as a painted entry,
and win_render drew every painted entry first, under the whole stack: the
outside portrait (still open on the stack) covered it, and the port showed
the empty plaza all through the alarm. Now each painted entry keeps
`over`, the stack windows open when it was drawn (WinEntry.over), and
win_render draws in the order the nametable was written: the painted
entries drawn over d stack windows after stack window d - 1, before stack
window d; when stack windows close beneath one, it sits on what is left
(`painted_settle`), so a window opened later draws over it. Not kept: the
ROM's close puts back, inside the closed frame, what was under it before
it opened, which would take a painted entry's cells there; no route paints
over a window that then closes beneath it where the two meet (counted with
a temporary log over every route: the battle's boxes painted over its
command and message windows, which sit apart); every battle route's files
are unchanged.

**Proof.** proof.sh b142a against b141d (taken before any edit, this
machine): the same in every file -- 120 routes' 604 files byte for byte
(every shot's index plane and every log), crt_check 27, crt_sim_check 40,
patches 14, probe_check 3, the attract WAV 97141d6d..., the Dark Force
trial 25d3ca5f... -- but routes_pass.txt and the three new routes' ten
files. colour_check over every route (123): 226,150 frames composed both
ways and 334 shots read back, none different; 160,659,834 pixels drawn from
15 pictures in 24,641 frames on 46 routes -- among them the radar portrait
under Gaira's panel's cycling entry (the glow), Ustvestia's through her
lesson's fades, the roof's spaceship into the flight, Shir's and Rolf's in
the house. Twelve pictures no route draws yet (Amy, Anna, Rudo, Hugh, Kain,
three of the four Dezolians, the doctor, the armour seller, the item
keeper, the teleport clerk): the same binding as the rest.
Mutations, each put back: Rolf's picture bound for the library's graph
window, and Rolf's windows bound whatever is loaded -- library_graph_overlay
218 frames differ (the graph's blue #3434AC shown as Rolf's grey
#CECECE), rolf_picture_gate fails (`expect picture 40 40 none`: rolf) and
42 of its 64 frames differ. tower_alarm_picture on b141d: `expect pixel
130 70 $4A0` fails at $888 (the plaza), and on 142 before the draw order:
`expect picture` central_tower_outside. No warnings in `make headless
route_probe crt_ref patches_check art_dump intro_probe building_probe
level_probe menu_probe pause_probe`. Not built here: the windowed `ps2`
(no SDL3; it compiles video.c, window.c, game.c and art.c as the headless
game does, and its platform code is unchanged).

Routes: `portrait_pictures` (the Governor at the start of the game, Rolf's
house, Nei arriving: `expect picture` on each, and on the black room
beside them), `rolf_picture_gate` (the library with portrait 0 at its door:
Rolf's window over the graph's art at $4A0 is the tiles', not his picture
-- a case the game never makes, made to hold the gate),
`tower_alarm_picture` (the copy portrait over the outside one: the guard).
All three fail on b141d.

**Also.** The Makefile's `make shaders` defaults to `spv msl` on Linux
(The owner: Arch's shadercross asserts on DXIL for every shader, old and new;
Windows and macOS keep all three; `SHADER_FORMATS="spv dxil msl"` asks for
them anywhere). crt_gpu_check (and crt_ref the same way) says which route
makes a route PNG it cannot read: `cannot read route_out/climatrol_nei_dies/
05_party_fights.png (make route-climatrol_nei_dies makes it, from the
tree's top)`; crt_ref's checked here, crt_gpu_check's not compiled here
(SDL3).

The owner's results on 141a-141d: `make shaders SHADER_FORMATS="spv msl"`
compiled the four shaders; crt_gpu_check on Vulkan 105 of 105 (the HDR10
and scRGB crops, the every-value crop and the flat picture, every worst 1
code) once climatrol_nei_dies' PNG was there; `./ps2` on the P16 drew the
chain (SPIR-V, scanlines and the aperture grille through HDR10, WHITE 250,
PEAK 500; SDL's AUTO 330 nits, Wayland's headroom 1.624 over 203).

Changed: src/core/art.[ch] (new), src/core/video.[ch], src/game/window.[ch],
src/game/game.c, src/game/sysmenu.c, src/harness/probe.[ch],
tools/route_probe.c, tools/colour_check.py, tools/crt_gpu_check.c,
tools/crt_ref.c, Makefile, routes/portrait_pictures.route,
routes/rolf_picture_gate.route and routes/tower_alarm_picture.route (new),
README.md, docs.

## Build 142a: the proof on a headless server

The owner is moving the work to his OVH server (headless: no display, no GPU;
his private repo) and a Windows machine, so builds are tested on several
machines, and asked for the work that does not need him first. Tools and
docs only; nothing under src/.

proof.sh: `PROOF_COLOUR=1` adds a stage after the routes are summed --
tools/colour_check.py over every route, JOBS at a time, its verdict alone
in colour_check.txt (`SAME: 0 routes failed, 0 frames differ, 0 shots
differ`, so a new route does not make two runs differ) and every route's
line in colour_check_full.txt; the closing lines say `colour: ...`. Off by
default (six minutes on one core). compare.py compares
colour_check.txt with the other single-result files (the same when neither
run took it).

tools/proofs/server_proof.sh [REF] (new): one commit's whole proof,
unattended. REF's files out of git (`git archive`) into a scratch tree --
the clone is never built in -- proof.sh there with PROOF_COLOUR=1, the set
kept in $PS2_PROOFS/<commit>/ (default ~/ps2_proofs), proof.sh's lines and
compare.py against the last finished run in <commit>.txt, one line a run in
summary.txt, the commit in `last`; one run at a time (flock: proof.sh's
/tmp directories are shared); git's hook variables cleared, so a
post-receive hook can start it. docs/machines.md (new): what runs on which
machine (the server: everything on the CPU; the desktop and Windows: the
windowed game, the shaders, crt_gpu_check), what the server needs (no
SDL3, no Q), the bare repo and the hook, reading the results over ssh;
crt_gpu_check under Mesa's lavapipe noted as untried.

Proof: a clone of the 142 tree as one commit, server_proof.sh with one job:
eight minutes (built 33 s, routes 56 s, colour_check 345 s, the rest 52 s);
123 PASS of 123, 614 files, colour `SAME: 0 routes failed, 0 frames
differ, 0 shots differ`, the attract WAV 97141d6d... and the Dark Force
trial 25d3ca5f...; against b142a every file the same but colour_check.txt
(b142a did not take it), the 614 route files identical. Its branches with
a stand-in proof.sh over five commits: the first run; a commit the same as
the last ("same", exit 0); a new route ("DIFFERENT", exit 1, compare's
lines in <commit>.txt); a commit that does not build (summary's FAILED
line, exit 2, `last` left on the last finished); a ref that is not a
commit (exit 2). Nothing under src/: no proof.sh baseline needed, the
script's own run is the proof.

Changed: tools/proofs/proof.sh, tools/proofs/compare.py,
tools/proofs/server_proof.sh (new), docs/machines.md (new), README.md,
CLAUDE.md, docs.

## Build 142b: the tree cleaned for the owner's private repository

The owner's upload of his built 142a tree, returned as the repository's first
commit: what git tracks and nothing else, nothing under src/ changed.

**Removed** (each the game's or the build's output, or the tree's
`.gitignore` already keeps it out): the game `ps2` (3.3 MB), `build/`
(Q's object), `tools/__pycache__/` (Python 3.14's), `saves/` (The owner's
slot, settings and a COMPUTER PLAYS session), `mods/` (the README the game
writes), and eight routes' `route_out/<route>/` with crt_gpu_check's
report (4.1 MB, written again by `make routes` and crt_gpu_check): 9.1 MB
in all, the tree 40.5 MB to 31.4. The checkpoints, playthroughs and trial
reports kept under route_out/ stay (21.7 MB of it; the playthrough, ground
map and measure tools read them).
And four empty `shaders/*.dxil`: Arch's shadercross wrote each file and
then asserted (its DXC), and `make shaders` left them -- make took them
for up to date, and the D3D12 game would have refused them and drawn
without the CRT. On Windows plain `make` now compiles the DXIL there
(141d's shaders-here), or `make shaders` / `make shaders_dxc`.

**Moved**: handover_113_windows.txt and questions_113.txt from the tree's
top to docs/old/ (history.md cites them by name); make_dist.py's NOT_SOURCE
leaves docs/old/ out of source.zip, as it did the two at the top.

**Added**: `.gitattributes` -- text LF in the repository and every
checkout (`* text=auto eol=lf`; on Windows git would otherwise check text
out with CRLF, which MSYS2's bash refuses in a script), PNG, WAV, ICO,
SPIR-V, DXIL and the like binary. None of the tree's text was CRLF (git's
`ls-files --eol`: 1,266 LF, 617 binary), so adding it changed no file.

**Makefile**: `make shaders` and `make shaders_dxc` remove a format that
fails (shaders-here already did) instead of leaving an empty file behind.
Checked with a stand-in shadercross that writes an empty file and fails
for DXIL: `FAILED shaders/<stem>.dxil (removed)` for each, the SPIR-V
written, the real shaders put back after.

**Proof**: the cleaned tree committed in a fresh repository (1,883 files)
and server_proof.sh run on that commit (one job, eight minutes): 123
PASS of 123, 614 files, colour `SAME: 0 routes failed, 0 frames differ, 0
shots differ`, crt 27, sim 40, patches 14, probe_check 3, the attract WAV
97141d6d... and the Dark Force trial 25d3ca5f...; against 142a's run every
file the same, the 614 route files identical, no build warnings.

Changed: .gitattributes (new), Makefile, tools/make_dist.py, docs/old/
(moved), README.md, docs.

## Build 142c: the DXIL from the Windows desktop

The tree built, tested and proved on the owner's Windows desktop (Ryzen 9
5950X, RTX 3080 Ti, MSYS2 UCRT64), and the four DXIL shaders 142b
removed made there and checked in. Nothing under src/ changed.

**The DXIL**: plain `make` (141d's shaders-here) wrote
`shaders/crt_fullscreen.vert.dxil`, `crt_down_rgb.frag.dxil`,
`crt_sharp_rgb.frag.dxil` and `crt_across.frag.dxil` with SDL_shadercross
1ff05bec and DXC 1.9.2609 (113's VM had 1.9.2607), dxil.dll beside it:
2,860 to 6,756 bytes, each a DXBC container that `dxc -dumpbin` reads back
with its shader hash and its `main`. Not drawn yet: The owner was on the
machine over remote desktop (Remmina, RDP), where the session draws to
Microsoft's Remote Display Adapter, so crt_gpu_check on Direct3D 12 and
the window with the CRT on wait for him at the machine. Should Direct3D 12
refuse them, `SDL_GPU_DRIVER=vulkan` takes the SPIR-V.

**No ROM**: the build, the routes and the proofs read only the tree (the
assets are checked in). The disassembly is in the repository beside it,
`../PSII_Disasm`, for `make extract PS2_DIR=../PSII_Disasm` (not run).

**CLAUDE.md's Machine section** is this desktop's now: cores, paths, GPU
and display, the toolchain's versions, the command from Git Bash into
MSYS2's shell with USERPROFILE passed on (MSYS2's login shell drops it,
and the Makefile looks for Q under it), Q v1.5.4, shadercross and DXC,
CLANG64. The VM's section moved unchanged to docs/machines.md, "The P16's
Windows VM (builds 113-128)"; docs/machines.md's table and
docs/windows_build.md point there. Noted, not changed:
tools/setup_msys2.sh's Q_COMMIT (6cd1aba, 2026-09-26) is older than Q 1.5
(139d); the Makefile takes either.

**Proof**: this machine's baseline, `PROOF_COLOUR=1 proof.sh w142b 32`
(/tmp/proofs/w142b), on 142b's tree, 106 s (built 14 s, routes 14 s,
colour_check 30 s, crt and patches 27 s, the rest 22 s): 123 PASS of 123,
614 files, colour `SAME: 0 routes failed, 0 frames differ, 0 shots
differ`, crt 27, sim 40, patches 14, probe_check 3, the audio check's
PASS (peak -10.0 dBFS), no build warnings, the attract WAV 97141d6d... and
the Dark Force trial 25d3ca5f... -- the hashes 142b's server run recorded
(its route files were not kept to compare one by one). `make -j32 all
headless route_probe patches_check` built the game with Q, no warnings.

Changed: shaders/*.dxil (new), CLAUDE.md, docs/machines.md,
docs/windows_build.md, docs.

## Build 142d: crt_gpu_check on the Windows desktop (docs only)

142c's DXIL drawn: The owner back at the machine (the console session,
no remote desktop adapter), `make crt_gpu_check` and `./crt_gpu_check` from
MSYS2's UCRT64 shell on the RTX 3080 Ti.

| Driver | Shaders | Result |
|---|---|---|
| Direct3D 12 (the default) | 142c's DXIL | 105 checks passed, 0 failed |
| Vulkan (`SDL_GPU_DRIVER=vulkan`) | the checked-in SPIR-V | 105 checks passed, 0 failed |

SDL 3.4.18, swapchain format 12 on both. The two reports differ in 7 of
their 105 lines, each by a few codes of millions (the title whole against
the mirror, all worst 1). `make crt_gpu_check` from Git Bash fails ("SDL3
not found by pkg-config": Git Bash's make has no pkg-config or SDL3); build
and run it from MSYS2's shell, CLAUDE.md, Machine. Still for the owner's
eyes: the window with the CRT on.

Nothing under src/; no rebuild needed. Changed: CLAUDE.md, docs.

## Build 143: the click at an effect's end under EXTRA

The owner played 142c at the machine -- it looked and sounded right --
but heard "a slight click after pushing the continue button on dialog
boxes", and wondered about the sound card (never noticed on Linux). It was
the game's own, on every machine, and the port's: docs/audio.md, "Build
143", has the whole of it.

Each stop plays the selection sound on FM5; at its end RestoreBGMChn
uploads the music track's instrument -- its carriers at the instrument's
own levels, then the track's volume added. On the Mega Drive that lands on
the channel the effect was sounding. Under VOICES EXTRA, the default, the
music's FM5 part plays on under the effect (chip A; the track's own voice
on Q), and the upload landed on its sounding note: two carriers up to 27
dB too loud for a few write slots, a burst well under a millisecond, about
a tick after the effect's end, wherever the part held a note through it
(17 of the new game's 39 measured presses on Q, 16 on the chip; none
under LIMITED). Not the sound card, and not an underrun: rendering took at
most 0.8 ms of a 512-frame chunk's 10.7 ms.

**The change**: smps.c's restore_bgm_chn brackets the upload for the hooks
(SMPS_HOOK_RESTORE, smps.h: 1 before, 0 after; no chip register, the
chip's writes unchanged). Under EXTRA the chip (synth.c) sends the upload
to B alone and Q (q_synth.cpp) keeps it off the track's own voice -- its
instrument, patch and volume kept -- while the effect's releasing voice
takes it as before (docs/second_chip.md's third rule; Q's 111c). The
bracket takes no write slot of Q's. LIMITED as before; audio.c's record of
the tracks keeps no bracket. Measured again with the build: 0 of 39 on Q
and on the chip, each within 1 dB (36 and 39 of 39) of the same render
with the selection sound taken out.

**The probe**: `expect sound clicks below DB` -- the sharpest bend (second
difference, either side) over the last four ticks rendered (docs/
harness.md). Route effect_end_click: the Governor's briefing, three
selection ends held under -28 dBFS: -32.4, -36.6, -37.0 now; on 142d's
sound code with this probe -18.7, -18.8, -16.0, a FAIL at the first.

**Proof**: w143 against w142b (this machine, 32 jobs, PROOF_COLOUR=1): 124
PASS of 124, 616 files; the attract WAV, the Dark Force trial, colour,
crt 27, sim 40, patches 14, audio_check, probe_check 3 and the build's
warnings (none) the same. The route files: the new route's two, and six
logs whose `expect sound playing` peaks moved, in audio_effects,
audio_grain, audio_oversample, audio_patches, audio_tails and
audio_voices -- each four ticks after an Escape menu press, whose
selection sound ends there: 0.9-5.8 dB lower (audio_oversample -10.8 to
-16.6 dBFS), the restore's burst having been the loudest sample in them.
Every other route file the same. Q's set (q_143 against q_142d, taken
before the edit): 25 of 27 files the same -- the attract WAV,
audio_check_q, the trim pass, drum_hits and every fm_probe WAV;
attract_q.log by the line naming its own path; and the steal report
($B5 over song $92), whose effect's track ends before its sound does: the
three EXTRA voicings' partials at 0.30 s by a dB or two at most (q_subtractive
one -52 dB partial more), their pitch swings by tenths of a cent, the
music's loss under the effect +0.0 where it was -0.0; the LIMITED chip's
lines the same. The sanitized probe (CLANG64's `route_probe_asan`) over
effect_end_click, the six audio routes, audio_switch, new_game,
menu_pause_stuck and unpause_lost_part: 11 PASS, nothing reported but
Nuked-OPN2's own ym3438.c:660 shift (left alone since 112).

`make` (src/audio, src/core, src/harness and Q's file changed). Changed:
src/audio/smps.h, src/audio/smps.c, src/audio/synth.c,
src/audio/q/q_synth.cpp, src/core/audio.c, src/harness/probe.c,
routes/effect_end_click.route (new), docs.

## Build 144: LoadPortraits' fade target

142's open item: load_portrait (game.c) loaded the portrait's palette into
the live palette only, where LoadPortraits (ps2.asm:11963) loads the fade
target first (PaletteLoad1, the target address + $80) and then the live
palette (PaletteLoad2) -- and load_portrait's own comment said both. Now
both, in that order.

It was expected to show in a fade in after a mid-scene portrait load. None
exists. LoadPortraits runs when BuildingScreen starts (ps2.asm:11928) and
three times mid-scene, all in Rolf's house: Nei's portrait at the start
(loc_AFA4), a member's on SEE STRENGTH (loc_B118), a new member's
introduction (loc_B282); Rolf's house has no fade and no other palette
load, and leaving it fades out and loads the street's palettes. The
port's building fades take their target from what is shown
(begin_fade_in, the lesson, the roof's flight), so the live load had
always carried the portrait into them. The colour path gets the same
colours from either load (video_colours_load), so the frame cannot change
either.

**Proof**, as for a refactor: w144 against w143 (this machine, 32 jobs,
PROOF_COLOUR=1): every file the same -- 124 PASS of 124, the 616 route
files (new_game and portrait_pictures load Nei mid-scene), colour SAME,
the attract WAV, the Dark Force trial, crt 27, sim 40, patches 14,
audio_check, probe_check 3, no warnings. No route can fail on 143: nothing
a route reads moved, and the probe does not read the fade target.

`make` (src/game). Changed: src/game/game.c, docs.

## Build 145: a route for every portrait picture

142 drew the portraits from their pictures and held them to their tiles
on every route, but twelve of the 27 were on no route: Rudo, Amy, Hugh,
Anna, Kain, the doctor, the armour seller, three of the four Dezolians,
the item keeper and the teleport clerk. Their binding is the same as the
rest's, so they were taken on trust. Build 146 deletes the tiles' loader,
and its proof can only hold what the routes draw: eight routes draw the
twelve now, each with `expect picture` on its portrait and a fade out with
the portrait up.

**The routes.** Paseo's hospital, armour store and teleport station from
their doors' own bytes (`start building`), as paseo_shop's way; the item
keeper the way a player reaches him, the tower's desk and ROOM ("Where
to?", TO_CHOSEN: building 9, portrait $14); the Dezolians on Aukba's and
Zosa's streets, in at the door, greeted in Dezolian without the translator
($0810). The four Dezolian portraits are one drawing
(dezolian_portrait.png through WinArt_DezolianPortrait) in four palettes,
PalettePtrs 33-36, so three routes for the three pictures.

The recruits come through Rolf's house, both of its portrait loads in the
middle of a scene: a newcomer at the door (RolfHouse_Arrival, loc_B282)
and SEE STRENGTH (loc_B118). recruit_pictures sets the counters as they
stand when Kain knocks -- Anna the last to join (`members Rolf Rudo
Anna`), Kain sent -- so the knock ($090C) and Kain's introduction ($0923)
beside Rolf's window, NO to renaming him, the roster ($090E), then SEE
STRENGTH for each of the five, then OUTSIDE ("There doesn't seem to be
any problems!", $0909) and the street.

**The probe.** `join-next N` (new, not a player's command): sets
party_member_join_next ($C606), the last recruit sent, which `expect
join-next` could read and nothing could set. While it is ahead of
party_members_joined, each visit to Rolf's house brings the next recruit
to the door (loc_B04E): the knock, joined + 1, the introduction.

**Proof.** proof.sh w145 against w144 (this machine, 32 jobs,
PROOF_COLOUR=1): every file the same -- the 616 route files of 124 routes,
crt 27, sim 40, patches 14, probe_check 3, audio_check, the attract WAV
97141d6d..., the Dark Force trial 25d3ca5f..., no warnings -- but
routes_pass.txt (132 PASS: the eight added) and the eight routes' 29 new
files. colour_check over the 132 routes: 230,301 frames composed both
ways and 347 shots read back, none different; 184,326,007 pixels from 27
pictures in 27,976 frames -- every picture there is. The routes pass on
144's game too: they add coverage and fix nothing. That they would catch a
wrong picture was checked by mutation, put back after: portraits.json
naming dezolian4 for window 18 fails zosa_item_picture (`expect picture
56 64 dezolian3`: dezolian4) and 47 of its first 128 frames differ in
colour_check; naming anna for window 6 fails recruit_pictures (`expect
picture 56 64 kain`: anna) and 112 of its first 249 frames differ.

**Also.** video.h's colour-plane comment still said the palette path is
the one shown: since 141 the platforms show the colour plane
(fb_draw_from_env), PS2_DRAW=palette the palette path; said there now.
asset_format.md: The owner's answers after 144 -- no HD check until
there is painted HD art (no upscaled stand-in), and HD drawn without the
CRT model but in HDR, whose encode is today the CRT chain's last stage --
and order 2's state. The status line no longer says nothing follows the
format.

`make route_probe` (src/harness changed; src/core/video.h a comment).
Changed: src/harness/probe.[ch], src/core/video.h, routes/ (eight new:
paseo_hospital_picture, paseo_armor_picture, paseo_teleport_picture,
tower_room_picture, aukba_hospital_picture, aukba_item_picture,
zosa_item_picture, recruit_pictures), docs/harness.md,
docs/computer_plays.md, docs/asset_format.md, docs/history.md,
NOTES_next_context.md, CLAUDE.md.

## Build 146: the library graph's picture and the slot maps

The first half of removing the portraits' old loader (NOTES' step 4,
146a the second): what a picture needs before there are no tiles behind
it. Two things still came from the tiles. The library graph had no
picture: LibrGraphPortArt sits at Rolf's $4A0 in the library (loc_8304)
and WinID_LibraryGraphPortrait drew it from there. And an effect's colour
on a picture came through the tile's index: a pixel took the glow where
the effect had lit its tile's palette entry.

**Which effects touch a picture.** Counted over every route, with a
temporary log of each picture and entry the glow was taken at (taken out
after): one, the radar's line 2 entry 5 -- Gaira's control panel (loc_66F6,
ps2.asm:9527, through loc_6790's four colours), which flashes the
portrait's DANGER and its arrow. By reading, the library's overlay is the
other (loc_C4B2, ps2.asm:18022: PaletteLoad2 $32, all of line 1 under the
graph), on a window that had no picture yet. Tyler's monitor (line 3) has
no portrait window; the level cycles stop at the building screen's
palette clear.

**Changed.** tools/extract_pictures.py: the per-window body is
make_picture, which the library graph goes through too (LIBRARY_GRAPH:
graph_portrait in Pal_GraphPortrait, line 1, its window's map, the art at
byte $A0) -> assets/art/portraits/library_graph/, named in portraits.json
as "library_graph". SLOTS: for the radar entry 5, for the graph every
entry 1-15; such a picture gets `<thing>_slots.png`, an indexed PNG of
the same size whose index at a pixel is the entry the effect writes there
(0 elsewhere, a grey ramp only so it can be looked at), named by the
frame's "slots" (docs/asset_format.md). JSON written with LF on any
system (Windows's write_text wrote CRLF; the 27 pictures are the same
bytes as before). `make manifest` for the four new files.

src/core/art.[ch]: a picture's `slots` (refused, naming it, when it does
not load, is another size or holds an index past 15); art_library_graph().
video.c's picture_pixel takes a pixel's glow entry from the slot map, in
the cell's palette line, where until 146 it was the tile's index; a
picture without a map, or a pixel its map leaves 0, no effect recolours.
game.c's window_picture: the graph's window draws the graph's picture in
the library, where the graph's art is.

**The proof tool.** route_probe's colour check hashes every tick's colour
plane, what the game shows, into one FNV-1a value a route, at the end of
its report (`the frames' colours $...`); tools/proofs/compare.py compares
them route by route when both runs have them. It is how 146a, which
changes the index plane and the shots' form under the portraits, shows no
pixel the game shows has moved.

**Proof.** The colour-check baseline with the hash, on 145's game: 132
routes. proof.sh w146 against w145 (this machine, 32 jobs, PROOF_COLOUR=1):
every file the same -- 132 PASS, the 645 route files, crt 27, sim 40,
patches 14, probe_check 3, audio_check, the attract WAV 97141d6d..., the
Dark Force trial 25d3ca5f..., no warnings. colour_check SAME: 230,301
frames, 347 shots, 28 pictures, the graph among them on
library_graph_overlay (through its overlay); each route's frames' colours
the baseline's, all 132. Mutations, put back after: the radar's "slots"
taken out -- gaira_dead_member and army_eye_capture 333 frames differ (the
DANGER no longer flashes), army_eye_capture's two panel shots 104 pixels
each; the graph's taken out -- library_graph_overlay 122 frames differ
(the hidden lines do not appear). The first run of w146 found every
route's log changed: assetfs warning that the four new files were not
preloaded -- `make manifest`, then the same.

`make` (src/core, src/game; tools/route_probe.c). Changed: src/core/art.[ch],
src/core/video.c, src/game/game.c, tools/extract_pictures.py,
tools/route_probe.c, tools/proofs/compare.py,
assets/art/portraits/ (library_graph/ new, radar's slot map,
portraits.json), assets/manifest.txt, README.md, docs/asset_format.md,
docs/palette_effects.md, docs/harness.md, docs/history.md,
NOTES_next_context.md, CLAUDE.md.

## Build 146a: the portraits' old loader out

Step 4's second half (docs/asset_format.md, order 2): the portraits are
their pictures alone. Until now LoadPortraits' art went to tile $400 and
its palette to line 2, Rolf's art to $4A0 and his palette to line 1 (the
graph's in the library), as the ROM's do, and the pictures stood in for
those tiles; now nothing of a portrait goes to VRAM or the palette.

**Changed.** game.c: load_portrait only notes the portrait loaded (it
took a Game for the palette loads; 144's fade-target line goes with
them); enter_building places no Rolf or graph art and loads no line 1
(Lutz's art and line 3 in the Esper mansion stay: he is a nametable
picture, not one of these); the three portrait tile sets and Rolf's sheet
at start-up are gone; PortraitTable's art is kept as a name, for the
tower's copy window. window_picture binds by tile range alone:
LoadPortraits' $400 to Rolf's $4A0, Rolf's and the graph's $4A0 to $500
-- every portrait window's interior bytes inside them (counted from the
maps: the tower's copy reaches 101 over $400, Rolf's and the graph's
$A0-$FF). video.c: a picture's pixel puts its line and slot in the index
plane (no tile under it; the dream's fade reads the line); the palette
path, fb_palette_colour, gives a picture's pixel the colour plane's
colour, and fb_colour_differs passes those by -- there are no tiles to
hold a picture to any more, which the frames' colours hash did last
(146). The SDL software path with PS2_DRAW=palette and building_probe's
dumps likewise. The probe: a route's shot with a picture in it is an RGB
PNG (its pixels have no index); `expect pixel` on a picture's pixel
compares the colour the CRAM word shows.

Not changed: the portrait tile sheets stay in assets/tiles -- the
extractor's input (`make extract-pictures`) -- and in the manifest, read
by nothing at run time. Dropping them from the tree means the extractor
reading the disassembly's art itself; not done here.

**Proof.** proof.sh w146a against w146 (this machine, 32 jobs,
PROOF_COLOUR=1): the same 132 PASS, crt 27, sim 40, patches 14,
probe_check 3, audio_check, the attract WAV, the Dark Force trial, no
warnings; 49 route files differ, all shots. The frames' colours, route by
route: 131 of 132 the same -- every frame of every route shows what 146
showed -- and rolf_picture_gate not: the route opens Rolf's window in the
library (the game never does), where 146 drew the graph's tiles through
his map and 146a has no tiles, so the window is empty; `expect picture
... none` holds as before. colour_check SAME (230,301 frames, 347 shots,
28 pictures). Then build 146 itself, from its commit in a scratch folder,
run the same 132 routes, and every route file compared: of the 347 shots
298 the same bytes, 48 indexed before and RGB now with every pixel the
same colour, and rolf_picture_gate's; every log, summary and save the
same. ASan and UBSan (CLANG64) on ten picture routes -- the graph's
overlay, Gaira's panel, Rolf's house, the recruits, the gate, the tower's
alarm and room, the roof's flight, Ustvestia's lesson, Zosa's Dezolian:
nothing reported. Built with no warnings: `make all headless route_probe
building_probe intro_probe level_probe` -- the windowed ps2 with SDL3 and
Q compiled here, not run (its PS2_DRAW=palette software path changed).

`make` (src/core, src/game, src/harness, src/platform). Changed:
src/game/game.c, src/core/video.[ch], src/platform/platform_sdl3.c,
src/harness/probe.c, tools/route_probe.c, tools/building_probe.c,
routes/rolf_picture_gate.route, README.md, docs/asset_format.md,
docs/palette_effects.md, docs/harness.md, docs/history.md,
NOTES_next_context.md, CLAUDE.md.

## Build 147: Lutz, the first cut-scene picture

docs/asset_format.md's order 2, the cut-scene pictures, begun with the
simplest. A survey of the 22 nametable pictures after 146a (each table
drawn every frame through nametable_draw or _draw_rows, but the ending's,
which copies its tables into its own plane-A buffer) sorted them by what
they need: nothing but fades; a glow, so a slot map; or tiles changed
pixel by pixel (the title's dissolve, the dreams' reveal and erase,
Tyler's reveal, Mother Brain's machine), which a picture cannot follow
until the compositor has a reveal mask. NOTES' "Next" keeps the list.
Lutz is the first of the first kind: the Esper mansion (BuildingScreen's
loc_8364, ps2.asm:11914) decompresses LutzPortraitArt to VRAM $0020, draws
LutzPortraitTileInd (30x14) into plane A at $C10A -- column 5, row 2 --
and loads Pal_LutzPortrait ($3B) for line 3 with PaletteLoad1; nothing
else touches his tiles or his line but the screen's fades.

**Changed.** tools/extract_pictures.py: SCENES, a cut-scene picture's
nametable and the PalettePtrs entry for each line it uses; scene_picture
draws the table whole (each cell its tileset's sheet tile, line and
flips; pixel 0 transparent), cut to the cells that draw something ->
assets/art/scenes/lutz/ (240x112: his green ground fills the table, "at"
its cell in the table, [0, 0]). src/core/art.[ch]: art_scene(thing),
loaded once, at startup. src/core/nametable.[ch]: nametable_tiles (the
range a table's cells name) and nametable_picture, a picture bound over
a table drawn at a cell, by that range. game.c: Lutz's picture loaded
with the assets and bound where render_building draws his table; then
his art (VRAM $0020 and LutzPortraitArt.png at startup) and his line-3
palette no longer loaded -- the table still puts down the cells the
picture colours.

**Proof**, in two steps. The picture bound with his tiles and palette
still there: colour_check SAME on the seven Esper routes, Lutz drawn from
his picture on three (esper_b1_hold, esper_b1_lutz, esper_lutz_nei), each
route's frames' colours 146a's. Then the loader out: the same hashes.
proof.sh w147 against w146a (this machine, 32 jobs, PROOF_COLOUR=1): the
same 132 PASS, crt 27, sim 40, patches 14, probe_check 3, audio_check,
the attract WAV, the Dark Force trial, no warnings; colour_check SAME
(29 pictures); every route's frames' colours the same, all 132; four route
files differ -- the three Lutz shots, RGB now (a picture in the frame),
and rolf_picture_gate's log, whose line numbers moved by the six comment
lines 146a added to the route after w146a had run it (the committed 146a
gives w147's). Against the step-one tree's route files, saved: all 347
shots the same bytes. A mutation, put back after: the binding a cell to
the right -- esper_lutz_nei's colours hash changed, and colour_check
still said SAME: a picture's pixels have nothing to be held to since
146a, so the hash is the proof for the pictures from here on.

`make` (src/core, src/game). Changed: tools/extract_pictures.py,
src/core/art.[ch], src/core/nametable.[ch], src/game/game.c,
assets/art/scenes/lutz/ (new), assets/manifest.txt, README.md,
docs/asset_format.md, docs/history.md, NOTES_next_context.md, CLAUDE.md.

## Build 148: the scenes only fades touch

The second group of 147's survey, done as Lutz was: the roof's flight's
planet, Nei's two pictures after Neifirst, the battle floor, the
ending's six pages and its last picture -- thirteen pictures, each a
table nothing touches but the screen's fades.

**The pictures** (tools/extract_pictures.py's SCENES, assets/art/scenes/):
flight_planet_to_dezolis and flight_planet_to_motavia (SpaceshipPlanetTileInd
in PalettePtrs $39 or $3A: the flight loads $39 when the destination is
DezolisSkure -- the blue and white planet -- and $3A otherwise, the green
one); nei_face (NeiDeathTileInd in $35: loc_7E02 case 1, the red ground),
nei_face_starry (the same table in $36, case 2) and nei_eyes_closed
(NeiBodyTileInd in $36: her closed eyes, 6x4, which case 2 draws over the
face at row 9, column 17); battle_floor (BattleFloorTileInd in line 0 of
the battle's four-line $2F); ending_rudo, _amy, _kain, _shir, _hugh and
_anna (the six pages' tables, each in its page's palette, $44-$49) and
ending_rolf (the last picture, in the wipe's $4A). The extractor now reads
any line of a palette that loads several (its PNG's colour k is RAM entry
line * 16 + entry + k) and refuses a cell drawing a colour its palette
does not load; and a picture keeps every cell that names a tile, drawn or
not, so that with its art gone no cell of the table can show what else
VRAM holds there (the Nei scenes' art goes over the level's tiles).

**The game.** Each bound where its table is drawn (nametable_picture):
the planet in render_flight, the one flight_begin chose by destination;
Nei's face by the case and her eyes over it (bound after, so taken first
where they overlap); the floor in render_battle. The ending copies its
tables into its own plane-A buffer, so it keeps which picture is there
and where (plane_picture, at each page's draw and at the wipe's start:
the bands put the table's row t at plane row 7 + t), forgets it with the
plane (clear_plane_a), and ending_render binds it. Then the art goes:
loc_71362 (the planet), loc_AED00 (Nei's), the floor's at $F020 and
EndingPicturesArt are not loaded; nor the palettes only they use -- the
flight's line 3, Nei's line 1, the pages' and the wipe's line 2 (the text
over the stars, which has no picture, still loads its two lines). The
battle's $2F stays: the windows (the command boxes, the stat boxes) draw in line 0 too.

**Proof**, in two steps. The pictures bound with their art still loaded:
colour_check SAME, 42 pictures drawn, every route's frames' colours
147's, all 132. Then the loaders out: the same but the ending's, by one
frame of 21,809. Put back one at a time: with the page palettes the hash
stayed changed, with EndingPicturesArt it came back; the frame is tick
21787, the press after "The End": ending_tick's last tick sets
SCREEN_SEGA with no fade and the render draws the Sega logo's cells (from
column 14) over the ending's VRAM and palette before enter_sega places
its art -- a strip of grey and olive from the last page's art for one
frame, black now that the art is not loaded. A frame the port drew wrong
before: the ROM's SegaScreen begins from a display it blanks (NOTES).
Every route's colour-plane shots the same between the two steps (347).
proof.sh w148 against w147 (this machine, 32 jobs, PROOF_COLOUR=1): the
same 132 PASS, crt 27, sim 40, patches 14, probe_check 3, audio_check,
the attract WAV, the Dark Force trial, no warnings; 52 route files
differ, all shots with one of the new pictures in them, RGB now. The
first run's crt_sim_check failed: crt_ref reads climatrol_nei_dies's
05_party_fights.png, a battle shot, which the floor's picture made RGB,
and it read route PNGs as indexed only. crt_ref's png_colour_plane and
crt_gpu_check's frame reader now read either (png_load_rgba, each pixel
through CRAM and back, as before): 40 of 40 again. crt_gpu_check is
compiled here, not run (it wants the owner at the machine).

**Also.** asset_format.md says what transparent pixels are: the ROM's
colour 0, where what is behind shows -- the owner saw them white in a
photo viewer, which paints transparency white. They stay transparent: the
battle's flashes show through the floor and the stars through the pages.

`make` (src/core, src/game; tools/crt_ref.c, tools/crt_gpu_check.c).
Changed: tools/extract_pictures.py, src/game/game.c, src/game/ending.c,
tools/crt_ref.c, tools/crt_gpu_check.c, assets/art/scenes/ (thirteen
new), assets/manifest.txt, README.md, docs/asset_format.md,
docs/history.md, NOTES_next_context.md, CLAUDE.md.

## Build 149: the colour cycles

The third group of 147's survey: the pictures a colour cycle runs over --
the SEGA logo's sweep, the ending's star field under the credits' fade,
Tyler's monitor -- each with the glow mask 146 made for the radar and the
library graph.

**The pictures** (tools/extract_pictures.py's SCENES and SLOTS,
assets/art/scenes/): sega_logo (SegaLogoTileInd in Pal_Sega, line 0; its
slot map entries 2-12, where PaletteCycle slides CyclingPal_SegaLogo --
entry 1, the white, stays); ending_stars (EndingStarfieldTileInd in line 3
of $4B; slots 9-12, the credits' fade, which are black in the picture as
they are in $4B: the fade lights them from black to the ramp's first row);
tyler_monitor, the first thing of several frames -- TylerSpcshp_MonitorTileInd
stacks five 15x12 frames, so the extractor takes a table's "frames" (the
game's number -> its first row) and "rows", and writes one picture a frame
(tyler_monitor_0.png ... _4.png: the grid, Palma, the white flash, the red,
the explosion), all cut to the cells any of them names so they share
"size" and "at", each with its slot map (line 3 entries 12-15, which only
the red and the explosion draw). The description's "frames" are keyed as
the game numbers them (loc_CDDE's 0-4), as asset_format.md has it.

**The loader and the binding.** picture_load_frame reads a description's
frame by number (picture_load is frame 1; Picture.frame says which);
art_scene_frame caches a scene's frames one picture each.
nametable_picture_rows binds a frame's picture over the rows
nametable_draw_rows drew (nametable_picture is the whole table).

**The game.** The logo is bound from the frame after enter_sega
(screen_entered): the frame drawn between a switch and the setup -- the
ending's hand-over, which has no fade (148's note) -- still shows what VRAM
held. SegaLogoArt and Pal_Sega are not loaded; enter_sega clears VRAM
instead, which keeps tile 0 blank as the art's blank first tile did (every
empty cell names it). The star field is drawn, and its picture bound, once
line 3 holds its colours (the PaletteLoad1 $4B after loc_634E's fade to
white, ps2.asm:10939): until then line 3 is cleared and the stars were
black on the black backdrop, so nothing is drawn. Its art stays loaded: the
ending's UFO is a sprite from the same sheet. The monitor's frame is bound
once the dissolve that brings it and Tyler in has finished: the dissolve
is the tiles' until the compositor can reveal a picture (the next group),
so TylerSpaceshipArt and palette $2A stay, for it and for Tyler's portrait.

**Proof**, in two steps, against w148 (this machine, 32 jobs,
PROOF_COLOUR=1). The pictures bound, their art still loaded (w149s1): the
same 132 PASS, crt 27, sim 40, patches 14, probe_check 3, audio_check, the
attract WAV, the Dark Force trial, no warnings; every route's frames'
colours 148's, all 132; 12 route files differ, the shots with one of the
new pictures in them, RGB now (army_eye_capture's eight of Tyler's
spaceship from the monitor on, the ending's four from the final days'
page). The logo is drawn on 22 routes, the stars on the ending's, the
monitor on army_eye_capture's and gaira_dead_member's. Then the logo's art
and palette out and the stars not drawn before their palette (w149): all
the same again, the 132 hashes 148's; two shots differ, the ending's fade
in from white (ending's 00_fading_in, mother_brain_won's 09_ending), where
the star field under the white is not drawn now -- each the same colours
as its frame's colour plane, as colour_check holds every shot.
Mutations, each in a scratch copy: the logo without its slot map changes
new_game's, title_under_menu's and intro_dreams' hashes; the stars without
theirs, the ending's; the monitor's explosion without its, both Tyler
routes'; the monitor's frames 2 and 3 swapped fails army_eye_capture (its
`expect pixel` on frame 2's white, which found the red) and changes
gaira_dead_member's. route_probe_asan (CLANG64, ASan and UBSan) on the six
routes that draw the three: all PASS, nothing reported.

**Also.** 148 said the stars show through the ending's pages: they are
black there (line 3 cleared until the final days' page); asset_format.md
corrected. The tables left are seven, not 147's "8": the title's two
planes, the dreams' battle and help, Tyler's portrait, Mother Brain's
machine, and DreamWaking -- the intro's second dream, which comes in by a
line-3 palette fade only and was missed in 148's group.

`make` (src/core, src/game). Changed: tools/extract_pictures.py,
src/core/art.[ch], src/core/nametable.[ch], src/game/game.c,
src/game/ending.c, assets/art/scenes/ (sega_logo, ending_stars,
tyler_monitor: three new), assets/manifest.txt, README.md,
docs/asset_format.md, docs/history.md, NOTES_next_context.md, CLAUDE.md.

## Build 149a: built on a Mac

The first build on a Mac, and what it took: an Intel MacBook Pro (Core
i7-6820HQ, 8 threads), macOS 15.7.7, Apple clang 17.0.0, MacPorts' SDL3
3.4.16, gmake 4.4.1 and pkgconfig, Q v1.5.4 (efe2611); Apple's Python 3.9.6.
Build 135a's page held: plain `gmake -j8` built ps2 with Q against libc++
and MacPorts' SDL3 as it was. The edits were made on build 148's tree and
brought onto 149.

**home.c** (macOS only, `__APPLE__`): `home_enter` finds the game's folder
through dyld (`_NSGetExecutablePath`, then `realpath`), as Linux does
through /proc/self/exe; `user_folder` is ~/Library/Application
Support/PhantasyStarII/<leaf> there, the Mac's place for it, in place of
~/.local/share. **pack.S**: Mach-O's spelling beside ELF's and COFF's
(`__TEXT,__const`, `_ps2_pack`), so build/dist/ps2 and dist_headless link
on a Mac. Neither reaches Linux or Windows: both branches are the
preprocessor's, and the other spellings are as they were.

**The Makefile**: `DIST_NAME` ps2-macos on Darwin. Q_DIR is looked for
last at `<GIT>/Q` for a tree at `<GIT>/Claude/phantasy-star-2/ps2`
(on the Mac, ~/Documents/GIT), after the places it looked before. With no
shadercross, a shader that is as committed beside a source that is as
committed is taken as up to date and touched: a fresh clone dates files in
the order git wrote them, and the Mac's first build said four times that
the game "will draw without the CRT" with the four .msl there. One older
for real still says so, now that the game draws with the old one; a
missing one says what it said. `*.dSYM` ignored by git.

**Proofs** (the Mac; on build 148's tree, before the edits and after): the
132 routes' PASS lines and the 645 files they write, every one the same;
patches_check PASS. Then crt_check 27 of 27, crt_sim_check 40 of 40,
`./crt_gpu_check` 105 of 105 on Metal with the checked-in MSL. `./ps2`:
`display: video cocoa, SDL_GPU metal, MSL shaders, 60.00 Hz, vsync on`,
HDR on, headroom 2.000, SDR white 1.000 (203 nits). Started from another
folder it says `home: the game's folder` and plays from there; the packed
headless game in a read-only folder, HOME a scratch folder, reads 1098
files from its pack and writes its saves and mods/originals under
Library/Application Support. On 149 with the edits: 132 routes
PASS (645 files), patches_check PASS, crt_check 27, crt_sim_check 40.
Not run: `make dist` itself
(tools/make_dist.py needs Python 3.12; the Mac has 3.9), proof.sh (bash
5, GNU tools), audio_check, the attract WAV and boss_trials (home.c is in
main.c's build only, and only on macOS; route_probe has neither file).

**Not done**: `make dist`'s package for a bare Mac (SDL3's dylib beside the
game, or an .app), its README's Mac wording; clang's two
`-Wsometimes-uninitialized` in songtext.c, left as they are. **Not seen or
heard**: the game on the Mac -- the picture, the CRT, HDR on its panel,
the sound, the pacing.

`make` (src/platform). Changed: src/platform/home.c, src/platform/pack.S,
Makefile, .gitignore, docs/macos_build.md, README.md, docs/history.md,
NOTES_next_context.md.

## Build 150: the dream's waking

The last of 147's first group, which 148 missed: the dream's second
picture, Rolf waking. loc_D59C (ps2.asm:19577) puts it up whole under a
blacked-out line 3 -- DreamWakingTileInd (28x14) into plane A at $C10C
(column 6, row 2), loc_6FF22 decompressed to VRAM $0020 (tile 1),
PaletteLoad1 $30 (line 3 alone) into the fade target -- and loc_61F4
fades entries $60-$6F, line 3 alone, up to it. Then script $1504's stops
and loc_D5EA's hand-over to the walk to the tower: nothing else touches
its tiles or its line.

**Changed.** tools/extract_pictures.py: SCENES' dream_waking
(DreamWakingTileInd in line 3 of $30) -> assets/art/scenes/dream_waking/
(224x112: the whole table, no pixel transparent; the same colours, pixel
for pixel, as intro_dreams' 02_dream_waking shot from w149's run). The
extractor's other 17 pictures came out the same bytes. game.c: the
picture loaded with the other scenes' (load_scene_pictures) and bound in
render_intro while the dream shows its second picture. Then the loader
out: dream_effect_begin no longer places the art at tile 1 nor loads $30
into the target. The target is still taken from what is shown, so the
palette path's step walks line 3 from black to black, and the colour
path's fade by line (VideoFade.lines, line 3) brings the picture up.
DreamWakingArt.png is not loaded at startup (g_dream_art is the battle's
art alone; the sheet stays in assets/tiles, the extractor's and the art
dump's input). VRAM from tile 1 keeps what the dissolve out left of the
battle's art: every cell the table names is the picture's, and a bound
cell draws nothing of its tile, so none of it shows.

**Proof**, in two steps against w149 (this machine, 32 jobs,
PROOF_COLOUR=1). The picture bound, its art and palette still loaded
(w150s1): the same 132 PASS, crt 27, sim 40, patches 14, probe_check 3,
audio_check, the attract WAV, the Dark Force trial, no warnings;
colour_check SAME; every route's frames' colours 149's, all 132; one
route file differs, intro_dreams' 02_dream_waking, RGB now (every pixel
the colour of 149's shot). The picture is drawn on intro_dreams and
new_game, in 671 frames of each. Then the loader out (w150): every file
w150s1's, the 132 hashes 149's. Mutations, each in a scratch copy: the
binding left out, or a cell to the right, fails intro_dreams (its `expect
pixel` on the cyan) and changes new_game's hash; one pixel of the
picture changed leaves both routes PASS and colour_check SAME and changes
both hashes. route_probe_asan (CLANG64, ASan and UBSan) on intro_dreams
and new_game: both PASS, nothing reported. Made on 149; build 149a (the
Mac's, pushed meanwhile) came in under it, and the proof on the rebased
tree (w150r) is w150's, every file and every hash.

`make` (src/game). Changed: tools/extract_pictures.py, src/game/game.c,
assets/art/scenes/dream_waking/ (new), assets/manifest.txt, README.md,
docs/asset_format.md, docs/history.md, NOTES_next_context.md, CLAUDE.md.

## Build 151: the reveal mask, and the dream's dissolves

The first of the tables a picture could not follow: tiles changed pixel by
pixel. A dissolve in (loc_6BBE's FadeArtFill) puts one pixel position of
every tile in its range in at a time, in FadeEffectArray's order; a
dissolve out (loc_6C54's loc_6CA4) takes a two-pixel byte of every tile
out each frame. Every tile of the range is at the same step, so what a
dissolve has done is one set of a tile's 64 places -- a reveal mask --
and a picture can be drawn through it.

**The reveal mask.** VideoPicture's `hidden` (src/core/video.h): bit
y * 8 + x is a tile's pixel [x, y] as the tile holds it, before the
cell's flip; draw_plane draws nothing of a bound cell's pixel whose place
is hidden (as alpha 0), mirrored as the cell is flipped, so a flipped cell
hides what its tile would. 0, every binding until now, shows the whole
picture. nametable_picture_hidden binds a table's picture with one.

**The pictures** (tools/extract_pictures.py's SCENES): dream_battle, the
dream's fight (loc_70A32 over loc_6D7BA: the intro's first picture,
loc_D534, and Tyler's spaceship's first), and dream_help, Tyler's "Help!!!"
(loc_70D42 over loc_6F040), both in line 3 of the title's palette
(Pal_TitleScr), which IntroScreen and Tyler's spaceship (loc_8326) both
load. Each the same colours, pixel for pixel, as the route shots of its
tiles (intro_dreams' 01_dream_battle, army_eye_capture's 09a_dream_fight
and 09b_help); the help has transparent pixels where its tiles are 0.

**The game.** game.c keeps the mask of the tiles the dissolves write from
tile 1 (g_dissolve_hidden), beside the writes: all hidden as a dissolve in
starts (the tiles are blank then: the intro and the building clear VRAM,
and a dissolve out's 32 bytes are all 64 places, checked against
dissolve_order.json), FadeArtFill's pixel clearing its bit, loc_6CA4's
byte setting its two. The two tables name tiles 1-248 and 1-148, inside
the $FA and $96 tiles the ROM dissolves, so the mask holds for every cell.
render_intro binds the fight with it while the dream shows its first
picture, render_tyler the fight and the help. Then the loader out:
DreamBattleArt.png and DreamHelpArt.png are not loaded, the dissolves write
no tiles for them -- the dream's dissolve in and out and Tyler's are the
mask's alone (dream_erase_step lost its tile writes and its count) -- and
Tyler's own art, the monitor view's, is still dissolved into tiles.

**Proof**, in two steps against w150 (this machine, 32 jobs,
PROOF_COLOUR=1). The pictures bound through the mask, the tiles still
written (w151s1): the same 132 PASS, crt 27, sim 40, patches 14,
probe_check 3, audio_check, the attract WAV, the Dark Force trial, no
warnings; colour_check SAME; every route's frames' colours 150's, all 132
-- the dissolves' frames included, so the mask is the tiles' pixel for
pixel, flipped cells too (28 in the fight, 124 in the help); three shots
RGB now (intro_dreams' 01_dream_battle, army_eye_capture's 09a_dream_fight
and 09b_help). The pictures are drawn on intro_dreams, new_game,
army_eye_capture and gaira_dead_member. Then the loader out (w151): every
file w151s1's, the 132 hashes 150's. Mutations, each in a scratch copy:
the mask read without the cell's flip, the mask ignored (the picture whole
through the dissolves), and the erase hiding one pixel of its two each
change all four routes' hashes, every route still PASS and colour_check
SAME -- the hash is what holds the pictures. route_probe_asan (CLANG64,
ASan and UBSan) on the four: all PASS, nothing reported.

`make` (src/core, src/game). Changed: src/core/video.[ch],
src/core/nametable.[ch], src/game/game.c, tools/extract_pictures.py,
assets/art/scenes/ (dream_battle, dream_help: two new),
assets/manifest.txt, README.md, docs/asset_format.md, docs/history.md,
NOTES_next_context.md, CLAUDE.md.

## Build 152: Tyler through the dissolve

Tyler's spaceship's last view (event_routine 7, loc_CD50): palette $2A
(lines 2-3), TylerPortraitTileInd 13x12 into plane A at column 6, row 3
(line 2), the monitor's frame 0 at column 19 (line 3), and
TylerSpaceshipArt dissolved in from tile 1 on $B4 tiles (loc_6BBE). The
portrait is a picture now, and both are drawn through 151's reveal mask
from the dissolve's first frame.

**The picture.** tools/extract_pictures.py's SCENES: tyler_portrait
(TylerPortraitTileInd in line 2 of $2A) -> assets/art/scenes/
tyler_portrait/ (104x96; the same colours, pixel for pixel, as
army_eye_capture's 09_tyler shot of its tiles). The two tables name tiles
1-107 and 2-167 (all five monitor frames), inside the $B4 the dissolve
writes, and their rectangles do not meet.

**The game.** render_tyler binds the portrait through g_dissolve_hidden,
and the monitor's frame through it too, from the dissolve's start (149
bound the frame only once the dissolve was done).
nametable_picture_rows takes a mask (the monitor is its one caller).
TylerSpaceshipArt and $2A stay loaded: the monitor's explosion
(ObjID_MonitorExplosion, tile 0 on, line 3) is a sprite from the same
tiles, in $2A's line 3 -- as the ending's stars' art stays for its UFO.
So nothing is taken out this build, and the tiles under the pictures
draw the same thing.

**Proof**, against w151 (this machine, 32 jobs, PROOF_COLOUR=1): the same
132 PASS, crt 27, sim 40, patches 14, probe_check 3, audio_check, the
attract WAV, the Dark Force trial, no warnings; colour_check SAME; every
route file the same (the Tyler shots were RGB from 149's monitor on);
every route's frames' colours 151's, all 132. The pictures are drawn in
252 more frames of army_eye_capture and of gaira_dead_member, the
dissolve's. With the tiles still there, the mutations say the pictures
are what is drawn: two pixels of tyler_portrait.png changed, the portrait
bound without the mask, and the monitor bound without it each change both
routes' hashes. route_probe_asan (CLANG64, ASan and UBSan) on the two: both
PASS, nothing reported.

`make` (src/core, src/game). Changed: src/core/nametable.[ch],
src/game/game.c, tools/extract_pictures.py,
assets/art/scenes/tyler_portrait/ (new), assets/manifest.txt, README.md,
docs/asset_format.md, docs/history.md, NOTES_next_context.md, CLAUDE.md.

## Build 153: game over's own background

Found reading GameOverScreen (ps2.asm:12255) in full for the title's
pictures: its plane B is DrawArtTiles 40x28 from loc_6C5DA, not from
TitleScrBGTileInd. In the ROM the two are one run of bytes -- the title's
table is a 160-byte binclude (art/tit_scr_background_tile_ind.bin)
straight on into loc_6C5DA's 2240, contiguous and split only by the
disassembler, which is why extract_nametables.py reads the title's
through both -- so game over draws from 160 bytes, two rows, further in:
the title's background two rows up, and under it loc_6C5DA's last two
rows, which the title never draws. Rebuilt from the disassembly's bytes:
game over's row r is the title's row r + 2 for every r below 26, and none
of its 28 rows is the title's row in the same place. Both screens clear
the scroll (ClearSpriteAndScroll), and the table names only the
background's tiles (1-$67, line 1). The port had drawn the title's table
there: the planets 16 pixels low and the bottom two rows the title's.

**Changed.** tools/extract_nametables.py: loc_6C5DA as a table of its own,
assets/nametables/GameOverBGTileInd.tmx (40x28, over the title's
background sheet at tile 1); every other table came out the same bytes.
game.c: g_gameover_b loaded with the title's and drawn by render_gameover.
Route gameover_message: its planet's pixel moved from 190,112 to 190,96
($260), 190,112 black now, and 1,208 ($420) in the two rows only game
over draws -- the route fails on 152 (at 190,96 it finds $6A0) and
passes now.

**Proof** against w152 (this machine, 32 jobs, PROOF_COLOUR=1): the same
132 PASS but gameover_message's line (15 commands: one `expect` made
three), crt 27, sim 40, patches
14, probe_check 3, audio_check, the attract WAV, the Dark Force trial, no
warnings, colour_check SAME; route files: gameover_message's shot, log
and summary differ, nothing else; colours hashes: gameover_message's
differs, the other 131 152's. The shot: every pixel above row 26 outside
the message is 152's 16 pixels lower.

**For the owner's eyes**: game over in the emulator (a level-1 Rolf
against Mother Brain, the route's way) beside the port: the middle planet
16 pixels higher than on the title, the message over its top half.

`make` (src/game). Changed: tools/extract_nametables.py,
assets/nametables/GameOverBGTileInd.tmx (new), assets/nametables/
nametables.json, assets/manifest.txt, src/game/game.c,
routes/gameover_message.route, docs/history.md, NOTES_next_context.md,
CLAUDE.md.

## Build 154: the title's pictures

TitleScreen (ps2.asm:10632), read in full: after the VRAM fill (tiles 0
to $5FF blank), TitleScrBGArt at tile 1 under TitleScrBGTileInd (40x28,
plane B, line 1) and TitleScrPlaneATileInd (40x28, plane A) drawn before
any of its art is there; Pal_TitleScr; 180 frames; then FadeArtTiles
brings TitleScrWomenArt in on 287 tiles from $E1, one FadeEffectArray
place every 4 frames, and TitScrPhantasyStarLogoArt on 256 from $200 the
same way (FadeArtFill ORs a pixel into each tile).

**The pictures** (tools/extract_pictures.py's SCENES, all in
Pal_TitleScr): title, plane A -- the figures (tiles $E1-$1FF) and the
logo ($200-$288), both line 2, one picture as asset_format.md has a plane;
title_background, plane B (tiles 1-$67, line 1); gameover_background,
153's GameOverBGTileInd over the same art. The title the same colours,
pixel for pixel, as new_game's 01_title shot; each background the same
wherever nothing is drawn over it (the copyright sprite on the title, the
message's letters on game over).

**The binding.** The figures and the logo dissolve one after the other,
so the one picture is bound twice, once a tile range, each with its own
reveal mask: nametable_picture_tiles binds a table's picture over the
cells naming one range. game.c keeps g_figures_hidden and g_logo_hidden:
all hidden from enter_title (the VRAM fill), and from begin_fade (the
range blanked), FadeArtFill's place clearing a bit each step.
render_title binds the background and the two ranges; render_gameover
its background. Then the loader out: title_background_art (the title's
and game over's), title_figures_art and title_logo_art are not loaded;
fade_step and begin_fade write no tiles, only the masks. Pal_TitleScr
stays (the copyright and PUSH START sprites, TitleScrCopyrightArt's from
$300, and the font draw in it), and so does the copyright art.

**Proof**, in two steps against w153 (this machine, 32 jobs,
PROOF_COLOUR=1). Bound, the art still loaded (w154s1): the same 132 PASS,
crt 27, sim 40, patches 14, probe_check 3, audio_check, the attract WAV,
the Dark Force trial, no warnings; colour_check SAME; every route's
frames' colours 153's, all 132 -- the dissolves' frames too; 44 route files
differ, every one a shot with the title or game over in it, RGB now (most
are the Escape menu's and the CRT pages' routes, which shoot over the
title). Then the loader out (w154): every file w154s1's, the 132 hashes
153's. Mutations, each in a scratch copy: the figures bound without their
mask, and the logo through the figures' mask, change new_game's and
intro_dreams' hashes (the first title_under_menu's too; the logo is under
the Escape menu's box there all the way); a background pixel changed, the
three title routes'; game over's background unbound fails
gameover_message and changes its hash. route_probe_asan (CLANG64, ASan
and UBSan) on new_game, title_under_menu, intro_dreams and
gameover_message: all PASS, nothing reported.

`make` (src/core, src/game). Changed: src/core/nametable.[ch],
src/game/game.c, tools/extract_pictures.py, assets/art/scenes/ (title,
title_background, gameover_background: three new), assets/manifest.txt,
README.md, docs/asset_format.md, docs/history.md, NOTES_next_context.md,
CLAUDE.md.

## Build 155: the stars behind Mother Brain

The last nametable on 147's list, "Mother Brain's machine": since build
127 the port has known it as the star field behind her (her figure is the
battle object). BattleScreen (ps2.asm:12047), read in full: for formation
$103 it clears the loaded palette's entry 0 ($FB80) and draws loc_AE8A0
(MotherBrainMachineTileInd) 40x14 into plane B from row 0, over the
floor's first two rows; its tiles are 21 of her art's ($8E-$A1), in line
3. After the fade from white, FadeArtTiles reveals all the enemy art --
$200 tiles from 0, hers among them -- one place a frame; then her lights
(loc_67FE): the chase along line 3's entries 8-15, every fourth frame,
paused while a hit has the backdrop red. Game over's loc_6C54 takes tiles
0-$5BF out again under the battle screen. Nothing else writes her tiles
in the battle: the effects' art goes to $400 and $480, the party's from
$200.

**The picture.** tools/extract_pictures.py: mother_brain_stars
(MotherBrainMachineTileInd in line 3 of the battle's $2F -- her one
group's colours, all zero, go to line 2) with its slot map, entries 8-15
(SLOTS). Every drawn pixel of it is on one of those entries, and in $2F
they are black: on her black backdrop the stars show only where the chase
lights them.

**The game.** Battle keeps art_hidden, the enemy art's reveal mask: all
hidden as battle_enter blanks the tiles, a place cleared each dissolve
step; game.c's gameover_erase_step sets its bytes back as it erases.
render_battle binds the stars through it. Her art stays loaded: her
figure is drawn from it. So nothing is taken out this build.

**Proof** against w154 (this machine, 32 jobs, PROOF_COLOUR=1): the same
132 PASS, crt 27, sim 40, patches 14, probe_check 3, audio_check, the
attract WAV, the Dark Force trial, no warnings, colour_check SAME; every
route file the same (the battle shots were RGB from 148's floor on); every
route's frames' colours 154's, all 132. The stars are drawn on
mother_brain_battle, mother_brain_won, gameover_message and ending.
Mutations, each in a scratch copy: the slot map removed, and three
picture pixels changed, each change all three of mother_brain_battle's,
mother_brain_won's and gameover_message's hashes; the binding without the
mask, and game over's erase left off it, change gameover_message's only
-- the dissolve in cannot be seen (the stars are black until the chase
starts, after it), the erase can. route_probe_asan (CLANG64, ASan and
UBSan) on the four routes: all PASS, nothing reported.

`make` (src/game). Changed: src/game/battle.[ch], src/game/game.c,
tools/extract_pictures.py, assets/art/scenes/mother_brain_stars/ (new),
assets/manifest.txt, README.md, docs/asset_format.md, docs/history.md,
NOTES_next_context.md, CLAUDE.md.

## Build 156: the extractors read the disassembly's art

The portraits' and the scenes' tile sheets stayed in assets/tiles after
their loaders went (146a-154), as the picture extractor's input -- read by
nothing at run time, but in the manifest, so preloaded at every start.
Now the extractors read that art from the disassembly, and the sheets
leave the tree.

**Which sheets.** Every sheet a run-time loader reads, by the loaders'
own sources: game.c's and ending.c's literal paths, battle.c's enemy art
(battle.json's art_file), effect art and the party's battle_<name>_art,
each level's tiles and its animations' frames (level_anims.json), and
npc.c's sprite_<label> for every label the object data names. Of 157
sheets, 121 are read; the other 36 are exactly the art the pictures
replaced -- the 24 portraits' (the library graph's among them) and the 12
scenes' (Lutz, the flight's planet, Nei, the battle floor, the ending's
pictures, the Sega logo, the three dreams', the title's three) --
asset_names.PICTURE_ART. The art still loaded under a picture, for a
sprite from the same tiles, stays: Tyler's (the monitor's explosion), the
ending's star field (the UFO) and Mother Brain's (her figure).

**tools/disasm_art.py.** DisasmArt(asm, art_dir) knows every sheet the two
art extractors make -- an art/<stem>.bin by asset_names.art_name, an inline
dc.b block by extract_inline_art.output_name -- and gives a sheet's tiles,
decompressed (ps2art), padded to the grid its PNG has
(ps2png.write_tile_sheet's: extract_art.guess_width for a .bin, 16 across
inline), and that grid. `python3 tools/disasm_art.py PS2_ASM ART_DIR`
checks every sheet under assets/tiles it can make against it: before the
change, all 121 it makes the same tiles and grid as their PNGs (the 36
among them).

**The extractors.** extract_pictures.py takes the disassembly (its first
argument, so tools/roundtrip.py runs it -- below) and reads every sheet's
tiles through DisasmArt, a scene's tilesets by name. extract_nametables.py
and extract_winart.py take a tileset's size from DisasmArt (winart gains
an ART_DIR argument); a tileset of picture art still names
../tiles/<sheet>.png, which the tree no longer holds: Tiled shows those
cells empty (the picture is the file to edit), and the game reads only the
counts and properties. extract_art.py and extract_inline_art.py write no
sheet of picture art and delete an old one. art_export.py (the art dump)
reads a sheet with no PNG from the disassembly (PS2_ASM, ART_DIR; `make
art` passes them). Every picture, window map and nametable came out the
same bytes with the sheets gone; every other sheet the same bytes.

**Roundtrip.** It ran each extractor only when the recipe's first argument
was a path that exists, so it had never run the picture extractor
(`--assets` first): "skipped pictures", every file under assets/art "not
written by any extractor", and a PASS that held nothing of them -- the
`make roundtrip` with assets/art still open from 142a. With the disassembly
first, the pictures are extracted at level 1 too. Before (w155's tree):
PASS, 933 of 933 files the same, tile sheets 157 of 157 at level 2. After:
PASS, 1018 of 1018 files the same (the 121 picture files among them;
only the icon and q_patches.json written by no extractor), tile sheets 121
of 121, window maps 84, nametables 23.

**Proof.** proof.sh w156 against w155 (this machine, 32 jobs,
PROOF_COLOUR=1): the same 132 PASS, crt 27, sim 40, patches 14,
probe_check 3, audio_check, the attract WAV, the Dark Force trial, no
warnings, colour_check SAME; every route file the same; every route's
frames' colours the same, all 132; no route's log names an asset it could
not load. The manifest: 1015 files, 3.7 MB (1051 and 3.8 before). The art
dump's load_tiles gives the same tiles for all 36 from the disassembly as
from the last commit's PNGs; `make art` itself was not run (it needs
art_dump's captures).

**Found, not fixed.** A control for the proof -- a scratch copy with a
sheet the game does read at start (title_copyright_art.png) deleted --
made route_probe die before game_init could fail cleanly: a segmentation
fault, nothing in the route's log (under ASan, exit 1 and no report).
game_init's own message ("game_init failed ... see the log") is never
reached. Nothing in the tree is missing, so no route meets it.

`make extract` (tools only: no src/ change; run `make extract
PS2_DIR=...`, or `make extract-pictures extract-nametables extract-winart
extract-art extract-inline_art`, to see the same files). Changed:
tools/disasm_art.py (new), tools/asset_names.py, tools/extract_art.py,
tools/extract_inline_art.py, tools/extract_pictures.py,
tools/extract_nametables.py, tools/extract_winart.py, tools/art_export.py,
Makefile, assets/tiles/ (36 sheets gone), assets/manifest.txt, README.md,
docs/asset_format.md, docs/history.md, NOTES_next_context.md, CLAUDE.md.

## Build 157: route_probe says which asset is missing

156's found crash: with a sheet the game loads at start left out of a
scratch tree (title_copyright_art.png), route_probe died with a
segmentation fault, its log empty, before game_init's own message.

**Why.** route_probe sends the game's stderr to a file and hands each line
to the interpreter (probe_stderr_line), which logs it through its host
(probe_log, `g_host.log`). The host was attached after game_init, so a
line written during game_init -- the PNG loader's "png: cannot read ..."
-- reached the interpreter while `g_host.log` was still NULL, when
game_init's failure made capture_end read the file back: a call through
NULL. Nothing in the tree is missing, so no route had met it. Now the host
is built and attached before assetfs_preload and game_init (nothing in
them uses it but the log; probe_attach only copies the pointers and
clears the interpreter's state), and a failed start writes `game_init
failed` and `exit 2` at the end of the log, beside the message on stderr:

    route routes/new_game.route -> route_out/new_game
       tick
          0      |     assetfs: 1 files in assets/manifest.txt could not be read; ...
          0      |     png: cannot read assets/tiles/title_copyright_art.png

    game_init failed
    exit 2

**Check.** tools/probe_check.py's `missing_asset`: assets/ copied to a
scratch folder without title_copyright_art.png, new_game run there; exit
2, game_init's message on stderr, the file named in the log. On 156's
route_probe it fails (exit 3221225477, Windows's access violation; nothing
said, nothing named); now it passes. probe_check 4 of 4.

**Every file.** A scratch sweep (not in the tree): each of the manifest's
1015 files, and the manifest itself, left out in turn of a tree of hard
links, route_probe through the SEGA screen to the finished title, 32 at
once. 788 runs PASS (a file the start does not need, or one read later),
228 stop at game_init with exit 2 and the file named in the log; none
crashed.

**Proof.** proof.sh w157 against w156 (this machine, 32 jobs,
PROOF_COLOUR=1): the same 132 PASS, every route file (645) and every
route's colours hash (132) the same, crt 27, sim 40, patches 14,
audio_check, the attract WAV, the Dark Force trial, no new warnings,
colour_check SAME; probe_check 4 ok where it had 3 (missing_asset).
route_probe_asan (CLANG64, ASan and UBSan): the missing sheet's start
exits 2 with the same log, new_game PASS, nothing reported.

`make route_probe` (tools only: no src/ change). Changed:
tools/route_probe.c, tools/probe_check.py, README.md, docs/harness.md,
docs/history.md, NOTES_next_context.md, CLAUDE.md.

## Build 157a: SegaScreen's fade, after the ending and after game over

"Found in 148": the ending's last button set SCREEN_SEGA with no fade,
and that tick's render drew the logo's cells over the ending's VRAM for
a frame. Read for it: SegaScreen (ps2.asm:10591), EndingScreen's end
(loc_785C), GameOverScreen and its loop (12255), MainGameLoop, every
`move.b #ScreenID_Sega` and the two loops that test for it (the level's
loc_7DEC, the battle's 12250), PaletteFadeFrom, VBlank's routine $24,
and game.c's every direct `g->screen =`.

**What the ROM does.** SegaScreen begins `move.b #$E0, d0; bsr
UpdateSoundQueue` (the music's fade) and then PaletteFadeFrom -- 21
frames, one channel of every entry a frame, red first -- over whatever
the screen before left showing, and only then turns the display off and
sets up the logo. The ending's button (loc_785C) writes `$9001`, the
planes back to 64x32, sets ScreenID_Sega and returns; game over's loop
returns on B, C or its timer with ScreenID_Sega already set by the
battle. So both fade out first, with the music. The other screen
routines (title, intro, level, building, battle, ending) send nothing
before their opening fade: SegaScreen's `$E0` is the only one.

**What the port did.** goto_screen's fade-out is the next screen's
PaletteFadeFrom, and enter_sega sent `$E0` -- after that fade, 21 frames
late, on every way to the SEGA screen. The ending and game over set the
screen directly, with no fade at all: the logo's setup cut in, and the
frame between drew the logo's cells over the old screen's VRAM and
palette (black since 148, its art not loaded).

**The change.** goto_sega (game.c): `$E0`, then goto_screen(SCREEN_SEGA);
enter_sega sends `$E0` only when nothing did (power-on, a route's `start
new`, and Data Memory's reset, which is a console reset -- `jmp
EntryPoint` -- and stays a plain goto_screen). The attract crawl's end,
the ending's button and game over's end go through it. ending_render
draws only the rows the VDP reads: once the button's `$9001` has made
the planes 64x32, plane A's first 32 rows (a row's cells are where they
were, 64 a row; only the wrap is shorter). Drawing all 64 into a 32-row
plane wrapped rows 32-63 over 0-31: through the fade the port would have
shown PRESENTED BY SEGA where THE END stays (a scratch build without the
limit, shot ten frames in).

**Also: the battle's hand-over to game over.** BATTLE_GAME_OVER sets
SCREEN_GAMEOVER directly too; the tick's render runs before
enter_gameover, and render_gameover took g_go_phase as the last game
over left it. In the first game over of a process that is GO_DISSOLVE
(the battle drawn, right); after one, GO_WAIT -- one frame of game
over's background under the battle's windows. render_gameover draws the
battle until the screen is entered.

**Found, not fixed: the party wiped out on the map.** loc_E01A (the
dead-member event, ObjType_Events $30) sets ScreenID_Sega, and the level
loop's loc_7DEC sends that to GameOverScreen when the controls are not
locked ("branch if all characters are dead"): the game over music, the
erase over the level, the message, then SegaScreen. The port's
level_floor_event goes straight to the SEGA screen -- no game over at
all. 157c (157b came first: the damage floor's order). (The attract crawl's end, with the controls locked, skips the
test and goes to SegaScreen, as the port does.)

**Not changed, for the owner's emulator.** At power-on SegaScreen
runs the same PaletteFadeFrom, over the black palette, before its setup:
21 frames the port does not spend (the README's "frame 321"). Every
route's ticks and draws would move with it; it waits for an emulator's
count of the frames from power-on to the logo's first light.

**Routes.** ending: ten frames after the button, THE END's white at
$E60 (eleven steps: the red gone, four of the green) and PRESENTED's P
(130, 88) black; fails on 157 (the logo's setup: black) and on a scratch
build without the row limit (THE END not there). gameover_to_sega (new):
C at game over's message, three steps in its white $EE8, seven $EE0,
then the logo; fails on 157 (black). probe_check's gameover_twice: two
lost fights in one session, the hand-over frame shot both times -- the
battle's floor (a picture pixel) both times and the same bytes; on 156's
probe the second is game over's background.

**Proof.** proof.sh w157a against w157 (this machine, 32 jobs,
PROOF_COLOUR=1): 133 PASS (gameover_to_sega new), crt 27, sim 40,
patches 14, audio_check, the attract WAV (2400 frames: the crawl's end is
not reached) and the Dark Force trial the same, no new warnings,
colour_check SAME; probe_check 5 ok. Route files: ending's log and
summary changed (the `$E0` on the press's tick, the SEGA screen 21 ticks
later) and its new 14_fading; its thirteen other shots the same;
gameover_to_sega's six new; every other route file and every other
route's colours hash the same. route_probe_asan (CLANG64, ASan and
UBSan) on ending, gameover_to_sega and gameover_message: PASS, nothing
reported.

For the owner: the ending's last button and game over's C in the
emulator beside the port -- the fade, and THE END held through it.

`make` (src/game). Changed: src/game/game.c, src/game/ending.c,
routes/ending.route, routes/gameover_to_sega.route (new),
tools/probe_check.py, README.md, docs/game.md, docs/harness.md,
docs/history.md, NOTES_next_context.md, CLAUDE.md.

## Build 157b: the damage floor's field, then the rest of the iteration

Found making 157c's route (the party wiped out on Gaira's damage floor):
colour_check DIFF, 19 frames, one a step -- each damage floor's white
field, where the palette path showed line 3 entry 10 (a star) at $CAA
and the colour plane white. Read for it: loc_7108 (ps2.asm:10430) and
its five callers' `beq.w loc_7108`, ObjCL_Main (5186), LevelScreenLoop
(11443), VBlankSub_10 and VBlankSub_1C_24; game.c's tick_level_walk in
full, level_dangerous_floor, level_palette_cycle, video_flash_white.

**What the ROM does.** loc_7108 runs inside ObjCL_Main, the leader's
object, at a step's end: all 64 entries to $EEE (32 longs), the sound,
WaitForVBlank (the white field), the palette back from $FB80 (loc_7140),
seven more WaitForVBlanks, a point of HP off everyone standing
(loc_7162, which sets $DE70/$DE6E for ObjType_Events $30 when someone
drops to 0), and only then rts. The rest of the iteration -- the other
objects (followers, townspeople), BuildSprites, ProcessPlayerMenu,
ProcessAButtonPress, ProcessRandomBattle, loc_6430's colour cycles, the
backdrop's drift -- runs after the eight fields, and shows at the ninth.
The fields are VBlank $10's, so the tile animations go on through them
(loc_118F4), as the port's stall already had them.

**What the port did.** level_dangerous_floor flashed the palette, took
the HP, and returned into the rest of tick_level_walk: the iteration's
rest ran in the white field's own tick, and the stall's eight ticks ran
nothing after it. So on the palette path loc_6430 wrote its cycling
entries over the white (the colour plane's whole-frame white hid it:
colour_check found it as soon as a route stood a star in view there);
the cycle's step was then put back by loc_7140 one tick later, where the
ROM shows it at the stall's end; ProcessRandomBattle rolled eight ticks
early, on another tick's RNG; ProcessAButtonPress opened "... is dead!"
eight ticks early; and the HP fell before the fields, not after them.

**The change.** level_dangerous_floor is the flash alone; the step's
result and the windows' state are kept (g_floor_rest), and the tick ends
there. The stall's last tick takes the HP (level_floor_hp, loc_7162) and
runs the rest of the iteration (level_walk_rest, the tail of
tick_level_walk moved into its own function). Gaira's ring swap stays in
the flash's tick: it is ObjCL_Main's first act, before the step.

**Routes.** damage_floor_field (new): Gaira from the open cell above the
floor, the second step (off 3,6): the white field at tick 38 with the
star white (on 157a $CAA) and Rolf still at 19 HP, seven ticks of the
star as loaded ($000), then at the stall's end loc_6430's step ($CAA)
and 18 HP. damage_floor_restore and emulator_lab_damage_floor, the two
other routes that step on a damage floor (a scratch build counting
level_dangerous_floor's calls found no others), pass unchanged.

**Proof.** proof.sh w157b against w157a (this machine, 32 jobs,
PROOF_COLOUR=1): 134 PASS (damage_floor_field new), crt 27, sim 40,
patches 14, probe_check 5, audio_check, the attract WAV and the Dark
Force trial the same, no new warnings, colour_check SAME; route files:
damage_floor_field's four new, every other the same; colours hashes the
same but the new route's -- damage_floor_restore's and
emulator_lab_damage_floor's too (no cycling pixel in view at their
deferred step, no roll that came out otherwise). route_probe_asan
(CLANG64, ASan and UBSan) on the three damage-floor routes: PASS,
nothing reported.

`make` (src/game). Changed: src/game/game.c,
routes/damage_floor_field.route (new), docs/game.md, docs/harness.md,
docs/history.md, NOTES_next_context.md, CLAUDE.md.

## Build 157c: the party wiped out on the map, through GameOverScreen

Found reading every `move.b #ScreenID_Sega` for 157a. loc_E01A (the
dead-member event, ObjType_Events $30) sets ScreenID_Sega once
"... is dead!" has played out and nobody is left standing, and
LevelScreenLoop's end (loc_7DCE, ps2.asm:11473) sends that on:
screen_changed_flag clear and the controls unlocked, loc_7DEC compares
the screen with ScreenID_Sega and branches to GameOverScreen ("branch
if all characters are dead"). With the controls locked -- the attract
crawl, whose end sets ScreenID_Sega too -- that test is skipped, and
MainGameLoop runs SegaScreen. Read for it besides 157a's list:
loc_DFFE-loc_E03C, LevelScreenLoop in full, VBlankSub_10 (the party's
art: 32 tiles from $FFD600 to tile $300, loc_5B6E; loc_118F4 is the
tile animation) and VBlankSub_1C_24 (the palette and the sprite table
only), GameOverScreen's setup (ClearSpriteAndScroll empties both
planes; the DMA after it puts the font at $500).

**What the port did.** level_floor_event went straight to the SEGA
screen through a plain fade: no game over music, no erase, no message.
It also put g_level_walk out first, so that fade drew the level through
the attract crawl's render and camera. And on both ways in, game over
lost the windows at once: enter_gameover reset the window system as the
screen began, where in the ROM the windows are plane A cells that stay
through the erase and the fade, their tiles (the font from $500) going
with the rest until the setup clears the planes.

**The change.** begin_gameover(g, over): the screen GameOverScreen
starts over (g_go_over), used by the battle and now by
level_floor_event; render_gameover draws that screen (render_level or
render_battle) until the setup. g_level_walk is left on (the title's
crawl and every start set it again). render_walker sends the party's
frames to tile $300 only on the level screen -- VBlank $10's DMA, which
GameOverScreen's VBlank $24 does not do -- so the erase takes the
party's tiles too instead of the render putting them back each frame.
The window system is set up for the message at the setup
(gameover_setup_windows, after the fade), not as the screen begins.

**Routes.** map_wipeout (new): a level-1 Rolf on Gaira's damage floor,
eighteen steps between 3,6 and 3,7 to "ROLF is dead!", C; then the
game over music, sixteen frames into the erase Rolf's blue still in at
54,89 ($C00) and the window's fill at 100,177 ($800), fourteen more and
Rolf's pixel gone ($000), the message, C, the SEGA screen. Fails on
157b at `expect screen gameover` (the fade to SEGA). gameover_to_sega:
sixteen frames into the battle's erase, the party window's fill at
20,177 ($800). Mutations, each a scratch build: the party's art sent
under game over (map_wipeout fails: Rolf's pixel $C00 at the erase's
end), the windows reset as the screen begins (both routes fail: the
fills gone), the battle drawn under the level's game over (map_wipeout
fails: Rolf not there).

**Proof.** proof.sh w157c against w157b (this machine, 32 jobs,
PROOF_COLOUR=1): 135 PASS (map_wipeout new), crt 27, sim 40, patches
14, probe_check 5, audio_check, the attract WAV and the Dark Force trial
the same, no new warnings, colour_check SAME. Route files:
gameover_to_sega's log and summary changed and its 00_erasing new,
map_wipeout's six new, every other the same; colours hashes changed:
gameover_message and gameover_to_sega (the windows through the erase),
map_wipeout new, every other the same. route_probe_asan (CLANG64, ASan
and UBSan) on map_wipeout, gameover_to_sega, gameover_message and
ending: PASS, nothing reported.

For the owner: a party wiped out on a damage floor in the emulator
beside the port (the erase over the level, Rolf and the window going
with it), and a lost battle's windows through its erase.

`make` (src/game). Changed: src/game/game.c, routes/map_wipeout.route
(new), routes/gameover_to_sega.route, docs/game.md, docs/harness.md,
docs/history.md, NOTES_next_context.md, CLAUDE.md.

## Build 157d: a refused mod of any kind is set aside

The owner's list after 142a, "the mods' fallbacks (PNG, map, JSON)":
docs/mods.md said "Other kinds are not guarded yet" -- a picture, map or
JSON file in mods/ that its loader refused behaved as the same broken
file would in assets/. Read for it: assetfs.c whole (the mods, the cache,
mods_drop), main.c's mods setup, and every loader of an asset in full --
png.c's two decoders, tmx_load, json_parse_file, tileset_load,
palette_load, table.c, nametable_load, winmap_load, script_load,
credits_load, level_blocks_load and level_load, walk.c's four, art.c
(picture_load_frame, the portraits, the scenes) -- and the ~30 JSON
consumers for whether they refuse or fall back.

**The change.** assetfs_mod_refused(path): when the refused bytes are a
mod's, set it aside (mods_drop's work, now set_aside(), shared), say so,
and return true; the loader then reads again, the game's own this time.
assetfs_mods_refused_under(folder) for a picture, whose description,
picture and slot map are one thing. The loaders: the formats --
png_load_indexed, png_load_rgba, tmx_load, json_parse_file (asset paths)
-- and each loader's own rules over a file that reads -- tileset_load
(whole tiles), table_load_ramp (a pixel past the palette),
table_load_dissolve (64 places of 0-7), nametable_load, winmap_load,
script_load (no entries; what it allocated freed first), credits_load,
level_load (the map and its backdrop planes' maps), picture_load_frame
(the thing's folder). Each is the old function made static (`*_read`)
and a short wrapper with the old name. palette_load, the walk tables,
level_blocks_load and the JSON consumers refuse nothing beyond a missing
file or what json_parse_file and the PNG decoder already guard; a JSON
value that reads but is odd is used, as before. The probe and the tools
set no mods folder, so nothing changes for them.

**Check.** tools/mods_check.py (new; in proof.sh as mods_check.txt, and
in compare.py's list): the headless game, 3600 frames from power-on into
the attract crawl on Motavia, with no mods and then with one broken mod
a case -- a PNG that is not one, a tile sheet of 7x7 pixels, a palette
PNG cut short, a ramp with a pixel past its palette, the dissolve order
of 63 places, items.json unclosed, a nametable .tmx cut in half, a
nametable cell flipped diagonally, a window map cell flipped, the Sega
logo's description with the wrong size, Rolf's portrait an 8x8 picture,
Script_Opening.txt with no entries, the credits with no {end}, Motavia's
map without its plane A -- each must say the mod was set aside and give
the same WAV and last frame as no mods; and one edit the loaders take
(Motavia's palette inverted) must be in use and change the last frame.
157d: 16 ok. On 157c's game: ten stop it at its start (exit 1), three
run on with the broken file's effect (no palette, no crawl text, no
level), the flipped window map is taken silently (the attract never
opens it); the control the same. A sanitized headless game (CLANG64,
ASan and UBSan, built in scratch from the Makefile's recipe): 16 ok,
nothing reported from the new code -- each run's one report is
ym3438.c:660's left shift of a negative value, the vendored
Nuked-OPN2's, on the audio path the probe does not render.

**Proof.** proof.sh w157d against w157c (this machine, 32 jobs,
PROOF_COLOUR=1): every file the same -- 135 PASS, 663 route files, the
colours hashes, crt 27, sim 40, patches 14, probe_check 5, audio_check,
the attract WAV, the Dark Force trial, no new warnings, colour_check
SAME -- and mods_check.txt new, 16 ok.

**Found, not fixed: build 139b is not in this tree.** Looking for 139b's
song_text.py comment (the two music-tool comments of 142a's list): none
of build 139b's code is here. songtext.c reads no `.trk` (scan_songs,
split_words, songtext_set_mod_test absent), assetfs has no
assetfs_is_mod, extract_music no write_trackers, tracker.py is 139's (no
game_music, image, song_span, timeline), smps_trace no --image,
assets/music/songs holds no `.trk`, song_text.py still writes "does
nothing" beside every bare hold, smps.h still says counter 1, and
docs/audio.md and docs/mods.md have no 139b text. README.md, this file
and NOTES describe it as done: later builds' tarballs carried those three
whole, and the code did not come with them. Which way is the owner's
(NOTES, "Next").

`make` (src/core). Changed: src/core/assetfs.[ch], src/core/png.c,
src/core/tmx.c, src/core/json.c, src/core/tileset.c, src/core/table.c,
src/core/nametable.c, src/core/winmap.c, src/core/script.c,
src/core/credits.c, src/core/level.c, src/core/art.c,
tools/mods_check.py (new), tools/proofs/proof.sh,
tools/proofs/compare.py, README.md, docs/mods.md, docs/history.md,
NOTES_next_context.md, CLAUDE.md.

## Build 157e: the index-plane shaders' sources out

Build 141a asked the owner to delete src/shaders/crt_down.frag.hlsl and
crt_sharp.frag.hlsl, the index-plane shaders crt_down_rgb and
crt_sharp_rgb replaced; from the chat sandbox that was his to do, and it
was never done: both were in the repository's first commit and are
still tracked. Nothing reads them -- the Makefile's SHADER_STEMS and
crt_gpu.c name only the four `_rgb`, `across` and `fullscreen` shaders,
and SHADERS_GONE already removed their compiled files. Removed with git
rm (git's history keeps them; the two `_rgb` headers and docs/crt.md
still name them as what came before).

**Check.** `make` here: no shader rebuilt, no warning. The four sources
left compiled with shadercross in all three formats: SPIR-V, DXIL and
MSL the same bytes as shaders/, twelve of twelve. No src/ C change, so
no proof.sh run: the game, the probe and the routes are the same
programs.

Nothing to build (`make` is a no-op beyond its usual check). Changed:
src/shaders/crt_down.frag.hlsl and crt_sharp.frag.hlsl (removed),
Makefile (a comment), docs/history.md, NOTES_next_context.md.

## Build 158: HD art from the game's pictures -- the tools, and the art dump again

The owner, after 157e: an image model trained on the series' art --
this game's, its sprites among it, and as much of Phantasy Star I, III,
IV and the original artists' work as can be had -- that draws each of the
game's pictures again in high resolution from the picture itself, at an
exact whole-number scale: 9x, the game in its TV shape on a UHD
television (224 x 9 = 2016 of 2160 lines). docs/hd_art.md (new) has it
whole: the machine's C:\AI (ComfyUI, OneTrainer, the models, the data,
all outside the repositories), the tools, the first results.

**The tools** (tools/, Python and Pillow, no part of the game):
hd_dataset.py (the training set from the art dump, masked and captioned,
and OneTrainer's settings), hd_generate.py (an HD picture through
ComfyUI, exactly k times the native size, the native transparency) and
hd_check.py (each k x k block against its native pixel, Delta E, and the
transparency exact).

**The art dump, again.** `make art` stopped at Mother Brain's frame:
art_export.py drew the cut-scenes from their tiles under a palette read
from a route's shot, and a shot with a picture in it has been RGB since
146a -- no palette (the scenes it did draw came out under a palette of
zeros: black), and the RGB crop refused `transparency=0`. The cut-scenes
are pictures now (147-155), so pictures() copies assets/art/scenes as it
is, each thing's frames, slot maps and description, and keeps Mother
Brain's mid-fight frame from its shot (RGB). SCENES, SHOT_PALETTE,
from_shot, render_layers, load_nam and draw_words, which only that drew
with, are gone. `make art ART_OUT=...` now runs to its end: 1226 PNGs
(cut-scenes 38 -- every scene's picture, frames and slot maps -- effects
88, field sprites 345, levels 193, monsters 304, palettes 79, party
battle 88, portraits 30, windows 61).

**Checked.** The training set from the dump: 4808 images (portraits 30,
cut-scenes 30, monster frames 2394, battle actions 945, field sprites
288, effects 629, windows 58, level pieces 434). A one-epoch OneTrainer
run on 50 of them (1.55 s a step, batch 2, 1024, bf16, masked) wrote a
LoRA that ComfyUI loads with no unmatched keys. Rolf's portrait at 9x
through hd_generate.py: 720 x 720 exactly, the transparency exact (base
model: Delta E mean 7.98).

No src/ change: the game, the probe and the routes are the same programs
(no proof.sh run). Changed: tools/hd_dataset.py, tools/hd_generate.py,
tools/hd_check.py (new), tools/art_export.py, docs/hd_art.md (new),
README.md, docs/history.md, NOTES_next_context.md.

## Build 158a: no giant pixels -- the smooth guide and edge, the series' set

The owner on the first HD test (Rolf through the base model, the guide
the native picture enlarged as blocks): "definitely don't want it to look
like giant pixels", "true to the spirit of the original art not just a
pixelated upscale", and of the blocky transparency, whether it was there
to match the geometry.

**hd_generate.py.** The starting image and the ControlNet's guide are the
native picture enlarged smoothly (Lanczos, `--guide lanczos`, the
default): the blocks' edges were what the model kept. The output is still
exactly k times the native size. Defaults: denoise 0.7, the ControlNet
0.45 until 75% of the steps (`--control-end`); the negative prompt names
`mosaic, blocky, dithering, sprite`. The transparency: by default a
smooth outline (`--edge smooth`): the native coverage enlarged smoothly,
ramped to an anti-aliased edge an eighth of a native pixel wide, held
inside the blocks (ImageChops.darker): the geometry kept -- nothing drawn
where the game draws nothing -- without its stair-steps. `--edge exact`
keeps the blocks.

**hd_check.py.** The transparency must be inside the native one (it was:
equal to it); the report says exact or smooth edge and how much of the
coverage is drawn, and exits 1 only for a pixel drawn outside.

**hd_dataset.py.** The whole series: `--dump GAME:DIR` and `--images
GAME:KIND:DIR` (ingame, map, official), any number; captions `psclassic`
and the game's tag; a concept a game and kind; at most three frames of an
animation (`--max-frames`): PS2's set 1828 (monster frames 726 of 2394),
PS1's 71 (its 44 screens and the overworld's pieces, from
C:\AI\tools\ps1_art_export.py and its tree's render_maps.py, both reading
a snapshot of that tree's last commit).

Results in docs/hd_art.md, "First results". Tools only: no src/ change.
Changed: tools/hd_generate.py, tools/hd_check.py, tools/hd_dataset.py,
docs/hd_art.md, docs/history.md, NOTES_next_context.md.

## Build 158b: the series' training set, and v0

The tools' side of gathering the series' art and the first LoRA trained
on the games' own images (docs/hd_art.md, "The series' set and v0", has
the sources, the curation and the results). hd_dataset.py: a game's
in-game images capped an epoch (`--ingame-cap`, OneTrainer's SAMPLES
balancing), the illustrations twice an epoch, a tagged image's caption
its tagger's tags rather than its path's words. hd_generate.py:
`--style`, `--avoid`, `--guide soft`. The gathering and curation scripts
live in C:\AI\tools, beside the data, outside the repository.

Tools only: no src/ change. Changed: tools/hd_dataset.py,
tools/hd_generate.py, docs/hd_art.md, docs/history.md,
NOTES_next_context.md.

## Build 158c: III and IV's own art; names in the captions

The owner owns the four games: III's and IV's own art exported from
Phantasy Star Cave's disassemblies into C:\AI, outside the repository
(docs/hd_art.md, "III and IV from their disassemblies; names", has what
and how, and the checks against the games' frames). hd_dataset.py: a
path's party and cast names found as whole words, III's and IV's casts
known by name; the training's three sample prompts (Rolf, a monster,
Chaz), against text and logos.

Tools only: no src/ change. Changed: tools/hd_dataset.py,
docs/hd_art.md, docs/history.md, NOTES_next_context.md.

## Build 158d: only clean pictures in the training set

v1's first epoch learnt blur drawn in blocks: half its in-game pictures
were video frames and resized screenshots, enlarged by whole numbers.
v1 stopped; the frames snapped to their console's colours (in C:\AI);
hd_dataset.py brings an enlarged picture back to its own pixels, leaves
out an in-game picture of more than 256 colours and an illustration
under 512 px, takes a few-colour "illustration" as in-game, and shows
the illustrations three times an epoch against 600 of a game's in-game
pictures. v2 started on the cleaned set (docs/hd_art.md, "v1 stopped;
only clean pictures").

Tools only: no src/ change. Changed: tools/hd_dataset.py,
docs/hd_art.md, docs/history.md, NOTES_next_context.md.

## Build 159: interlaced scanlines (2026-10-15)

The owner: "try implementing an interlaced scanline mode to simulate
old televisions alternating every other scanline", in this port,
Phantasy Star's and Herzog Zwei's; "I'm assuming its not really possible
to simulate horizontal sweep rate" (it is not: docs/crt.md, "What it
cannot do"). The CRT page's SCANLINES gains INTERLACED: each line's beam
a quarter line up on one frame and down on the next, the gaps filling in
between frames and horizontal edges twitching at 30 Hz as on an
interlaced set (docs/crt.md, "Interlaced scanlines"). This port first, as
the chain's own; PS1's build 55 and Herzog Zwei's session 53 carry the
same change to their index-plane chain.

Changed: src/core/crt.c and crt.h (CrtSettings.interlaced,
CrtPlan.field, crt_field_shift, beam_row's shift, the beam table's two
fields, the mask a quarter line down, crt_tables_render's field,
crt_tables_field_row), src/shaders/crt_down_rgb.frag.hlsl (beam_row0)
and shaders/crt_down_rgb.frag.* (`make shaders`: SPIR-V, DXIL, MSL; the
other shaders unchanged), src/platform/crt_gpu.c and .h
(DownParams.beam_row0, the beam texture's rows, the field to draw),
src/platform/platform_sdl3.c (the other field each frame; the `crt:`
line says "interlaced scanlines"), src/game/sysmenu.c (the row's three
values, the menu's CRT row, settings.json's `"interlaced"`),
tools/crt_ref.c and tools/crt_gpu_check.c (the checks), routes/
crt_interlaced.route (new), docs/crt.md, README.md, CLAUDE.md,
NOTES_next_context.md. `make` (the game), `make crt_ref crt_gpu_check`.

Proof (32 jobs, PROOF_COLOUR=1), w159 against w158d, taken before the
first edit (w158d matched w157d whole): the routes 136 PASS (135 and the
new crt_interlaced), the 663 route files byte for byte and 3 new, the
colour hashes the same (and crt_interlaced's); audio_check, the attract
WAV, the boss trials, patches_check, probe_check and mods_check the same,
no warning. crt_check 30 of 30 (27), crt_sim_check 44 of 44 (40); their
reports byte for byte build 158d's crt_ref's (built from HEAD in a
scratch folder, run on the same frames) once the interlaced lines are
taken out. settings.json read and written back for true, false,
"interlaced", none and a word not known (a scratch program on
sysmenu.c's load and save). Found on the way, in PS1's check: the
mask's quarter line rounded down (k / 4) left a slot bridge a quarter
row from a field's beam at odd k, so `(k + 2) / 4`, here too; the proof
is after it.

**Not seen.** Made over remote desktop, so the window, crt_gpu_check
(the interlaced crops in both fields, on Direct3D 12 and Vulkan) and the
look on the panel wait for the owner at the machine: `make
crt_gpu_check && ./crt_gpu_check`, then `SDL_GPU_DRIVER=vulkan
./crt_gpu_check route_out/crt_gpu_check_vulkan`; in the game, Escape,
CRT, SCANLINES to INTERLACED, fullscreen.

## Build 159a: the field before held at a third (2026-10-15)

The owner, at build 159's INTERLACED: "I actually find the interlace
as it is a bit disorienting"; told why (a desk's distance, where every
line can be told from the next; pixel art's hard edges) and offered the
field before held under each field, "try the 1/3 dim and see how it
feels". Each interlaced field now holds the other under it at
CRT_FIELD_HOLD, a third of its light (docs/crt.md, "The field before"):
the twitter half as deep, the light's centres a quarter line apart. With
PS1's build 55a and Herzog Zwei's session 53, the same lines.

Changed: src/core/crt.h (CRT_FIELD_HOLD), src/core/crt.c (`beam_row` the
field's beams plus a third of the other's, over 1 1/3; the old body
`beam_line`; progressive returns `beam_line`'s numbers), tools/crt_ref.c
(the fields' centres held to (1 - hold) / (1 + hold) of half a line
apart), docs/crt.md, README.md, NOTES_next_context.md. No shader: the
blend is in the beam table. `make` (the game), `make crt_ref
crt_gpu_check`.

Proof (32 jobs, PROOF_COLOUR=1), w159a against w159: every stage the
same -- the routes 136 PASS and their 666 files, the colour hashes,
audio_check, the attract WAV, the boss trials, patches_check,
probe_check, mods_check, no warning. crt_check 30 of 30, crt_sim_check
44 of 44; their reports against build 159's crt_ref (built from HEAD in a
scratch folder, run on the same frames) differ only in the interlaced
lines: the interlace table's field rows (centres a quarter line apart,
the SDR white field 0.887 at 1440p and 0.886 at 4K, 0.798 and 0.796 in
159), the check's wording, and the interlaced crops' counts of codes one
off the model (each within a code, as before).

**Not seen.** Made over remote desktop: how it feels is the owner's to
say, at the machine (Escape, CRT, SCANLINES to INTERLACED, fullscreen),
and `./crt_gpu_check` on Direct3D 12 and Vulkan waits with 159's.

## Build 159b: FLICKER, FULL, SOFT or NONE (2026-10-16)

The owner, after 159a's third: "add the FULL / SOFT / NONE setting".
The CRT page's FLICKER row, under SCANLINES while SCANLINES is INTERLACED
(docs/crt.md, "FLICKER"): FULL, every row all and nothing in turn
(build 159's numbers); SOFT, the default, the field before held at a
third (159a's); NONE, both fields one still picture. With SCANLINES OFF
or ON the row is not on the page, and the page is as it was. With PS1's
build 55b and Herzog Zwei's session 53, the same lines.

Changed: src/core/crt.h (CrtFlicker, CrtSettings.flicker,
crt_field_hold), src/core/crt.c (SOFT by default; `beam_row`,
`white_beam_peak`, crt_render and the beam tables take the hold, and at
0 give `beam_line`'s own numbers), src/platform/crt_gpu.c (another
FLICKER is other tables), src/platform/platform_sdl3.c (the `crt:` line
says the flicker; its buffer 256), src/game/sysmenu.c and .h (the row,
on the page only while INTERLACED, the cursor past it, the rows closed
up; settings.json's `"flicker"`), src/harness/probe.c (`expect crt
flicker`), tools/crt_ref.c (each FLICKER in the interlace checks; NONE's
fields' beam tables the same, a new check; the scanlines' crops at each
FLICKER), tools/crt_gpu_check.c (the same crops on the GPU),
routes/crt_flicker.route (new), routes/crt_interlaced.route (down past
FLICKER to MASK), docs/crt.md, README.md, CLAUDE.md,
NOTES_next_context.md. No shader. `make` (the game), `make crt_ref
crt_gpu_check`.

Proof (32 jobs, PROOF_COLOUR=1), w159b against w159a: crt_check 31 of 31
(30); the routes 137 PASS, crt_flicker new and crt_interlaced two
commands longer, its shot with the FLICKER row; 671 route files (666:
crt_flicker's 5 new, crt_interlaced's 3 changed); the colour hashes the
same but those two routes'; crt_sim_check 44 of 44, audio_check, the
attract WAV, the boss trials, patches_check, probe_check and mods_check
the same, no warning. crt_ref's reports against 159a's and 159's (from
those builds' crt_ref, on the same frames): SOFT's rows and crops 159a's
value for value, FULL's 159's, NONE's two fields centred on the
progressive beam, every other line the same but the checks' wording.
settings.json read and written back by sysmenu.c's own load and save
for each FLICKER, none, and words not known (a scratch program, 11
cases).

**Not seen.** Made over remote desktop: the three on the panel are
The owner's to look at (Escape, CRT, SCANLINES to INTERLACED, down to
FLICKER, fullscreen), and `./crt_gpu_check` on Direct3D 12 and Vulkan
waits with 159's, with the scanlines' crops at FULL and NONE now too.

## Build 160: gamepads, the rumble and the controls (2026-10-16)

The owner, with a DualShock 3 on the Linux desktop: "I'd like to add an
option in the menu to select controller in herzog zwei and phantasy-star
one and two. It has the rumble pack ... Bonus to make the buzzer work
douring fights in phantasy star 1 & 2", then "ohh phantasy star 2 when you
walk over danger tiles". With Herzog Zwei's session 54 (its rumble module
and GAMEPAD page are this build's pattern) and PS1's build of the same.

**Gamepads.** The port read the keyboard only. platform_sdl3.c now opens up
to two gamepads (SDL_INIT_GAMEPAD; hotplugged, slots in the order they
came, the next moving up when one goes, as Herzog Zwei's): the d-pad or
the left stick past half its travel the directions, the left, bottom and
right face buttons A, B and C (the Mega Drive pad's left to right), Start,
and Back the Escape key. Presses are latched as the keys' are (a stick
crossing half its travel is a press), and held buttons OR into `held`,
alongside the keys of either layout. platform.h: PLATFORM_PADS,
platform_rumble, platform_pad_name.

**The GAMEPAD row and page.** The Escape menu's ninth row, above QUIT (so
COMPUTER PLAYS and SPEED stay where routes find them); its value NONE, 1
PAD or 2 PADS. The page: PAD 1 and PAD 2 -- the name each gives itself,
upper case, a character the font has not got a space, cut to 19 (main.c
hands the names over each frame) -- C on one buzzes that gamepad, to tell
which is which; RUMBLE, OFF, LOW, MEDIUM (the default) or HIGH, left and
right stopping at the ends, C round, every gamepad buzzing at the new level
as it changes; under them "C ON A PAD: IT BUZZES" or "NO GAMEPAD
CONNECTED". settings.json: `"rumble"`, a file without it (or a word not
known) MEDIUM.

**The danger tiles.** loc_6F56 (walk.c's check_underfoot), on the cell
underfoot at each rest, by the level type ($FFFFF710, the tileset's
number): type 5 (the tubes, Shure, Nido, Roron, the dams, the Biosystems
Lab, Climatrol and the other towers) blocks $1A, $46, $77 and $78; type 3
(Uzo, the Underwater Passage, the Crevice) block $63 -- a damage floor,
loc_7108, only on arriving by a step ($30(a0)): the whole palette to $EEE
for one field with SXFID_DangerousFloor ($B0), the palette back, seven
fields more (the level loop held inside ObjCL_Main), then loc_7162: one HP
off everyone standing, "... is dead!" for whoever it leaves at 0, the game
over when no one is left (builds 157b, 157c). Gaira is the routes' floor.

**The rumble** (src/game/rumble.[ch]). Nothing in the ported game changed:
a watcher main.c runs after every tick reads the screen, each party
member's HP (game_debug_data) and the ids the game asked the sound for that
tick -- core/audio.c records each one audio_play_sound and audio_play_sfx
are given (`Audio.asked`, `audio_take_asked`), whether or not the
sequencer can play it; nothing in the sound reads it. What it finds starts
voices (a peak on each motor, held, then down in a straight line); the
motors are the loudest voice's, scaled by RUMBLE (LOW a half, MEDIUM three
quarters, HIGH all), sent to every gamepad (one player):

| Felt | When | Shake at HIGH, in ticks |
|---|---|---|
| hurt | in a fight, a member's HP down, felt as the blow lands -- SXFID_Sword ($B3, loc_3308's hit_target: the target blinks) -- not as loc_2AA8 reckons it, about 20 ticks before; on the map, HP lost any other way than a floor's point (found: Roron's cake, ten HP off everyone, loc_E5FE) | big motor 35% + 65% by the share of max HP (all of it at half), held 4, down over 8-20 by the same; small 50% at a quarter or more |
| fell | a member to 0 HP, in a fight or on the map | both full: small 10, big 16 + 32 |
| lost | the game over screen begins | both full: small 24, big 60 + 90 |
| floor | SXFID_DangerousFloor on the map: with the white field; its point of HP eight ticks on is the same bite, not felt again (but as a fall when it is the last) | small 60%, big 30%, 6 + 6 |

A blow reckoned but never landing would be felt after 120 ticks
(RUMBLE_PENDING_MAX, counted as late): none ever was. HP lower on any other
screen is no blow -- found: a NEW GAME after a game over takes a level-6
Rolf from 46 HP to 19 on the intro screen -- and nothing is felt. main.c:
the watcher after each player's tick (play_tick); each frame the motors
(rumble_frame); the Escape menu stills the game's shakes as it opens
(rumble_quiet) and shows only its own test buzz (big 80%, small 60%, 10 +
14); COMPUTER PLAYS shakes no one, and the watcher looks afresh when the pad
comes back. The SDL3 platform sends a change at once (to a 32nd) and a
level held every 6 frames, each SDL_RumbleGamepad good for 250 ms, so the
motors stop by themselves if the frames do; it stops them when a gamepad
goes and at the end.

**Harness.** The headless platform: PS2_GAMEPADS=N connects N ("HEADLESS
PAD 1" and on), PS2_RUMBLE=FILE logs each motor change ("FRAME PAD LOW
HIGH"). The route language (src/harness/probe.c): `expect rumble KIND N`,
`expect rumble going|still`, `expect setting rumble LEVEL`, `rumble-log
NAME LEVEL` (the headless log's lines, ticks for frames); ProbeHost.rumble
(route_probe's watcher, run in its after_tick; NULL in COMPUTER PLAYS).
route_probe's PS2_RUMBLE_CHECK=FILE holds the watcher to the game every
tick, both ways (tools/route_probe.c, docs/harness.md). tools/
rumble_check.py: every route and 160 fuzz runs (formations 1-259 a stride
of 6 and the four from $100, three parties each; Gaira's and the
Biosystems Lab's floors) under the rules; the headless game's GAMEPAD page
(RUMBLE's four steps buzzing CCCC/9999, 9999/7332, 6666/4CCC and nothing,
settings.json's "off" written; PAD 2 buzzing pad 2 alone; nothing with no
gamepad); and Mother Brain against a level-1 Rolf in the headless game
against the same fight in route_probe, the PS2_RUMBLE log the rumble-log
line for line (133 lines) at MEDIUM, from a "low" settings.json at LOW,
from "loud" at MEDIUM, nothing at "off", and the Escape menu opened four
frames into Rolf's fall writing 0000 0000 as it opens and nothing while
open.

**Routes.** rumble_fight (formation 1: Rolf's first blow reckoned at tick
249, 19 HP to 17, motors still until 269, as it lands; eight blows), rumble_lost
(Mother Brain: hurt and fell in one tick, then lost, still 160 ticks on),
rumble_floor (Gaira: each bite felt with its white field, its point not
again; nineteen bites, the fall, lost), rumble_menu (the GAMEPAD row and
page, RUMBLE's steps; three shots). Each fails on 159b (the commands are
new).

Changed: src/core/audio.c and .h (the asked record, audio_take_asked),
src/game/rumble.c and .h (new), src/game/sysmenu.c and .h (the GAMEPAD row
and page, "rumble", sysmenu_set_pad_name, sysmenu_pads),
src/platform/platform.h, src/platform/platform_sdl3.c (gamepads, rumble),
src/platform/platform_headless.c (PS2_GAMEPADS, PS2_RUMBLE), src/main.c
(play_tick, rumble_frame, the menu's quiet, COMPUTER PLAYS still),
src/harness/probe.c and .h, tools/route_probe.c (its watcher,
PS2_RUMBLE_CHECK), tools/rumble_check.py (new), routes/rumble_fight,
rumble_lost, rumble_floor, rumble_menu (new), README.md, docs/harness.md,
CLAUDE.md, NOTES_next_context.md. `make` (the game), `make headless
route_probe`.

Proof (this Linux machine, 10 jobs, PROOF_COLOUR=1; its own baseline
b159b, taken first from 159b's files), w160 against b159b: build warnings
none; crt_check 31 of 31, crt_sim_check 44 of 44, patches_check 14 of 14,
audio_check, the attract WAV (97141d6d...), the Dark Force trials
(25d3ca5f...), probe_check 5, mods_check 16, colour_check SAME (0 frames, 0
shots differ) -- each the same; the routes 141 PASS (137 and the four new);
685 route files (671): the four routes' 14 new, and 15 changed, every one a
shot of the Escape menu's main page, two rows taller with GAMEPAD (audio_menu
1, crt_escape_back 1, crt_menu 2, escape_menu_computer 6,
escape_menu_fullscreen 3, title_under_menu 2), no log; the colour hashes
the same but the 18 routes that open that page and the four new.
rumble_check PASS: 141 routes and 160 fuzz runs, 1,053,036 ticks, hurt
1139, fell 199, lost 101, floor 547, none late, no rule broken, one HP drop
elsewhere (the NEW GAME above, not felt); every headless case. Six
mutations, each caught by rumble_check and restored: a blow felt as
reckoned (rules, 1095 broken, rumble_fight FAIL), a floor's point felt again
(rules, rumble_floor FAIL), lost felt every tick of the game over (rules,
rumble_lost and rumble_floor FAIL), LOW at three quarters (the menu's LOW
step), the menu not stilling the game's shakes (the Escape menu case), RUMBLE
not written to settings.json (the menu case). ASan and UBSan (route_probe_asan)
through the four routes, roron_cake and fuzz_battle under the rules: no
report but route_probe's own route list, never freed at exit (71,168 bytes,
probe_parse_route; there before this build). `make ps2 headless route_probe
patches_check` with no C++ compiler and no Q: built, no warning. The SDL3
game started with SDL's offscreen video driver (no window) opened "gamepad
1: PS3 Controller" and wrote nothing to its saves folder.

**Not seen or felt.** The rumble on a real gamepad: The owner's, with the
DualShock 3 (SDL 3.4.16 calls it "PS3 Controller"; its big left motor takes
a strength, its small right one is on or off): a fight's blows, a fall, the
game over, a floor (Gaira, or any tower's), the cake on Roron F4, the
GAMEPAD page's steps and PAD rows, the menu stilling a shake; whether the
strengths and lengths feel right. The pad's buttons in the window (the
mapping is read off SDL's, not pressed here). On Windows, a DualShock 3
needs a driver that gives it to Windows as a gamepad (DsHidMini is one).

**Then the controls, any gamepad's and the keys'.** the owner, having
felt the rumble: "yes I felt all three buzzes in herzog zwei.  I think you
need to make it controller agnostic so you can re-assign controller
buttons.  I should be like that for the keyboard in PS1 and PS2 as well."
So, in this build still: every one of the pad's eight inputs (the four
directions, A, B, C, Start) has two keys and two gamepad inputs, any of
them re-assigned in the Escape menu, any gamepad's, shown under the names
printed on the pad in hand -- the same work as Herzog Zwei's session 54 and
PS1's build 56.

*The core* (src/core/keys.[ch], new): Herzog Zwei's port's keys.c and
keys.h, with this game's actions (UP DOWN LEFT RIGHT A B C START), its
defaults and its reserved keys -- Escape (the Escape menu) and Backspace
(a slot emptied); the port keeps no other key for itself (Herzog Zwei's
F10 and F11 are not here). Everything else as Herzog Zwei's: the gamepad
inputs' numbers (0-25 SDL3's SDL_GamepadButton, 32-39 the sticks'
directions, 40 and 41 the triggers, 64-95 a mapless joystick's buttons 1
to 32), their names in settings.json (SOUTH, DUP, LSUP, L2, JOY1 ...),
the labels by pad (pad_input_label, ten characters at most: CROSS on a
PlayStation pad, A on an Xbox pad, B on a Switch pad, for the one bottom
button), a bind taking the input off every other slot, Back never bound, a
stick or trigger pressed at half its travel, and controls_pad_actions,
controls_key_actions, pad_nav_actions, pad_held_axes, pad_held_hat and
pad_held_joy_axes. One set of bindings for every gamepad. One difference:
five keys' names are spelt out where Herzog Zwei's are signs -- MINUS,
EQUALS, LBRACKET, RBRACKET, KPMINUS for - = [ ] KP- -- because this game's
font (win_charset) has no = [ ], and "-" is an empty slot's (as a key's
name it read back as none). The apostrophe is QUOTE, as in Herzog Zwei's,
so settings.json names it alike in the ports (the font has it; the page
shows QUOTE).

The defaults are what the port did before, both key sets live at once
(builds up to 159 picked one on the CONTROLS row) and the gamepad as this
build first read it (platform_sdl3.c's KEYMAP and pad_button, checked):

| | KEY 1 | KEY 2 | 1ST | 2ND |
|---|---|---|---|---|
| UP DOWN LEFT RIGHT | the arrows | W S A D | the d-pad | the left stick |
| A | Z | L | WEST | -- |
| B | X | ; | SOUTH | -- |
| C | C | ' (QUOTE) | EAST | -- |
| START | RETURN | -- | START | -- |

ControlScheme (platform.h, CONTROLS_ARROWS and CONTROLS_WASD) is gone; a
`Controls` (keys.h) is the menu's, settings.json's and
`platform_set_controls(Platform *, const Controls *)`'s.

*The menu* (src/game/sysmenu.[ch]). The CONTROLS row, second as before,
opens the CONTROLS page and shows no value. The page: CONTROLS, KEY 1 and
KEY 2, a row an action, then DEFAULTS (the keys only). Up and down pick the
row, left and right the slot; C, A, Start or Enter on a slot listens, the
slot blinking 16 ticks on and 16 off, "PRESS A KEY" and "ESC: KEEP
BACKSPC: NONE" under it: a key binds (Escape and Backspace are never
bound), Escape or a gamepad's Back keeps the slot, Backspace empties it,
anything else is not heard. The GAMEPAD page gains BUTTONS, its last row,
which opens the BUTTONS page: GAMEPAD BUTTONS, 1ST and 2ND, a row an
action, DEFAULTS (the gamepads only), "PRESS A PAD BUTTON"; the first
gamepad input to go down on any connected gamepad binds (a button, the
d-pad, a stick or trigger passing half its travel, a mapless joystick's
button, hat or axis), Back or Escape keeps, Backspace empties, other keys
are not heard. Its slots are drawn as PAD 1 labels them (its style, which
main.c hands over from the platform every frame, as it does the names;
positional names with none connected). The longest labels (DPAD RIGHT, AXIS
RIGHT) fit both columns of the 40-column screen. Whatever is bound, the
arrows and Enter and every gamepad's d-pad and left stick work the menu
(Input's nav), and Escape and Back go back a page and close it; choosing
and going back otherwise use the bound buttons, as before. Back from
BUTTONS is the GAMEPAD page.

*settings.json*: `"keys": {"up": ["UP", "W"], ..., "start": ["RETURN",
"-"]}` and `"pad": {"up": ["DUP", "LSUP"], ..., "start": ["START", "-"]}`,
last in the file, "-" an empty slot; a name nothing has keeps that slot's
default and says so on stderr ("sysmenu: no key NOSUCH; up keeps its
own"); a file from before, with `"controls": "arrows"` or `"wasd"`, reads
as the defaults without a word and is written in the new form from then
on. The game still never writes saves/ in a check: every headless run had
its own PS2_SAVES.

*The platforms.* platform_sdl3.c: a slot holds an SDL_Gamepad, or an
SDL_Joystick for a joystick SDL has no mapping for (opened on
SDL_EVENT_JOYSTICK_ADDED when SDL_IsGamepad says no; the gamepad replaces
it should SDL_EVENT_GAMEPAD_ADDED come for it); slots in the order they
came, the next moving up, the rumble's state moving with its slot, as
before. Each pump, each slot's inputs held (a gamepad's buttons and six
axes; a joystick's buttons, first hat and first two axes) OR'd with what
went down since the last pump, through controls_pad_actions; the keys held
and gone down through controls_key_actions. Input gains `nav_held` and
`nav_pressed` (the arrows, Enter as Start, every gamepad's d-pad and left
stick), `back` (Backspace), `key` (the first key down since the last
pump) and `pad_input` (the first gamepad input, a stick, trigger or hat
as it crosses into pressed); input_clear makes one of nothing. A
gamepad's Back is the menu key; a mapless joystick has none.
platform_pad_style (SDL_GetGamepadType: the Xbox 360 and One, PS3, PS4,
PS5 their own; Switch Pro and the Joy-Cons SWITCH; GameCube; STANDARD and
UNKNOWN STANDARD; a mapless joystick JOYSTICK; none NONE); names and the
rumble by SDL_GetJoystickName and SDL_RumbleJoystick for a joystick; and
gamecontrollerdb.txt beside the game (SDL_GetBasePath) read after
SDL_Init, "gamecontrollerdb.txt: N mappings" on stderr. main.c: the
presses carried to the next tick are the whole Input's now (the key, the
gamepad input, Backspace, the nav), not the buttons alone.
platform_headless.c: `PS2_RAW="N:NAME,N:PAD:NAME,..."`, keys by keys.c's
names and gamepad inputs by their settings.json names pressed at frame N,
as Herzog Zwei's HZ_RAW: `key` and `pad_input`, and a one-frame press of
what the controls make them (a gamepad input's only while PS2_GAMEPADS
connects one) and of the menu's own; `PS2_PAD_STYLE=ps3` (any of keys.h's
style keys), every connected gamepad's style, none by default; either
unreadable stops the game with a message. The scripted pads (PS2_PAD)
stay game buttons, no controls between. `PS2_FRAME_HZ=N`: the frames come
N times a second by the virtual clock, so main.c's pacing runs free (at
120 every other frame runs no tick, at 30 each runs two), as the window's
does on a 120 Hz display or a slow machine. The route language: `key NAME
[TICKS]` and `pad NAME [TICKS]` (held TICKS ticks, 1 by default),
`pad-style STYLE`, `expect keys|buttons ACTION SLOT1 / SLOT2`
(src/harness/probe.c, docs/harness.md).

*Checks.* `make controls_check` (tools/controls_check.c, Herzog Zwei's
hz_controls.c with this game's defaults, and every key's name held to this
game's font): PASS -- names, the labels of 10 styles, key names, the
defaults, 200 random binding runs against a model, the axes, hats and the
menu's directions at their edges. `python3 tools/controls_check.py` (the
headless game, each run in a scratch folder with its own saves): PASS, 18
checks -- START's KEY 1 set to Q through the CONTROLS page by raw keys,
then Enter does nothing on the title and Q is Start, settings.json
`"start": ["Q", "-"]` and no "controls", read back alone; START's 1ST set
to R2 through the BUTTONS page by raw gamepad inputs alone (Back, the
d-pad, the stick, East), then the gamepad's Start does nothing and R2 is
Start, `"start": ["R2", "-"]` under "pad", read back, nothing with no
gamepad connected; the BUTTONS page drawn four ways apart under none, ps3,
xboxone and switch, and ps3 with no gamepad as none; a file with
`"controls": "wasd"` the defaults, no message, rewritten without it; bad
names (NOSUCH, =, BOGUS, JOY40) a message each and those slots their
defaults; a PS2_RAW or PS2_PAD_STYLE it cannot read, a message and no
game; and at PS2_FRAME_HZ 30 and 120 (twice, a frame apart), UP's KEY 2
set to Q by raw keys -- a key pressed on a frame that runs no tick still
binds, and Enter, which starts the listening, is not bound by its frame's
second tick (main.c carries the key to the one tick that takes it). Three
routes: controls_keys (the CONTROLS page: a bind taking Z off A, the
blink, Escape and Back keeping, Backspace emptying, gamepad inputs not
heard, the unbound arrow and the d-pad still moving the cursor, the
unbound arrow held repeating, DEFAULTS the keys alone), controls_buttons
(the BUTTONS page: the labels under none, ps3, xboxone, switch; NORTH,
RSLEFT, L2 and JOY3 bound; a bind taking SOUTH off B; Back and Escape
keeping; keys not heard; Backspace; the unbound d-pad held repeating;
DEFAULTS the gamepad alone), controls_rebind (end to end: START to Q and
DOWN's 1ST to RSDOWN through the menu, then Enter and the d-pad down reach
nothing and Q and the right stick down do, the down arrow still). Each
fails on the build before (its commands are new).

*Mutations*, 39, each on a copy of the tree (never the tree), the binaries
rebuilt and controls_check, the three controls routes and rumble_menu
(their PASS lines and their files against the copy's own unmutated run)
and controls_check.py run; every one caught. keys.c, 13, each caught by
controls_check (some by the routes too): a bound key, or a bound gamepad
input, left in its old slot; a stick pressed only past half, not at it;
the triggers swapped; the hat's left read as right; CROSS and CIRCLE
swapped on a PS3 pad; Backspace bindable; Back bindable; B's second key '
not ;; the menu's directions without the left stick; JOY names one off; an
action's second gamepad slot not read; the default A East. The menu,
main.c, the platform and the probe, 26: a key binding on BUTTONS; a
gamepad input on CONTROLS; BUTTONS going back to the menu; either page's
DEFAULTS putting back the other half too; the pad's second slot written as
its first; BUTTONS without PAD 1's labels; Escape not keeping while
listening; Backspace emptying nothing on BUTTONS; the GAMEPAD page's
BUTTONS opening nothing; the menu's held, or pressed, directions not
merged (the routes; the held one only since they hold the unbound arrow
and d-pad, which a first round of 38 mutations showed they needed); the
slot not blinking (the routes' shots); "keys" not read; a bad gamepad name
emptying its slot; the old "controls" complained of; PAD 1's style not
handed over; a tick's key not carried from the frame (missed in the first
round, every headless frame being one tick; caught once controls_check.py
ran at PS2_FRAME_HZ 30 and 120); the controls not handed to the platform
at start; the headless platform's raw key or gamepad input not through the
controls, its controls not kept, PS2_PAD_STYLE ignored, a raw gamepad
input reaching the game with no gamepad connected; the probe's held press
held one tick; its raw gamepad press not the menu's nav.

*Proof* (this Linux machine, 12 jobs, PROOF_COLOUR=1). A restart had
emptied /tmp, b159b_basec and b160_after with it: the baseline was taken
again from 159b's files (`git archive HEAD ps2` into scratch space),
b159b_re, its attract WAV (97141d6d...) and Dark Force trials
(25d3ca5f...) b159b_basec's. b160_controls2 (the rumble and the controls)
against it: build warnings none; crt_check 31 of 31, crt_sim_check 44 of
44, patches_check 14 of 14, audio_check, the attract WAV, the Dark Force
trials, probe_check 5, mods_check 16, colour_check SAME -- each the same;
the routes 144 PASS (137, the rumble's four, the controls' three); 707
route files (671): 36 new (the rumble's 14, the controls' 22) and 15
changed, the same 15 the rumble changed, every one a shot of the Escape
menu's main page (audio_menu 1, crt_escape_back 1, crt_menu 2,
escape_menu_computer 6, escape_menu_fullscreen 3, title_under_menu 2): two
rows taller with GAMEPAD (the rumble's) and now no value beside CONTROLS
-- held against 159b's shots a row apart, the rows from CONTINUE to SPEED
the same but CONTROLS's value (ARROWS ZXC gone), QUIT the same; no log
changed. The colour hashes the same but the 18 routes that open that page
and the seven new. rumble_menu's three shots, new in this build and so not
in the baseline, changed from the rumble's own run as they should: the
main page without CONTROLS's value, the GAMEPAD page a row taller with
BUTTONS (looked at). rumble_check PASS again (144 routes, the three new
among them, and 160 fuzz runs, 1,054,950 ticks; hurt 1139, fell 199, lost
101, floor 547, none late, no rule broken; every headless case). With
-Werror: every C target (ps2 with SDL3 and Q, ps2_headless, route_probe,
route_probe_asan, patches_check, controls_check, crt_ref, crt_gpu_check,
display_probe and the other probes) built, no warning; and `make ps2
headless route_probe patches_check controls_check` with no C++ compiler
and no Q. Sanitizers (ASan and UBSan): route_probe_asan through the three
controls routes and rumble_menu, the headless game built with them through
controls_check.py (PASS) and controls_check (PASS): no report but
route_probe's own route list, never freed at exit, as before. The SDL3
game under SDL's offscreen video and dummy audio drivers (no window), its
saves and mods in scratch folders: it started, said "gamecontrollerdb.txt:
2 mappings" for a file of two made-up mappings beside it, and wrote
nothing to its saves folder. (One slip: a one-frame headless run to see
PS2_FRAME_HZ refuse 0 was started at the tree's top, so home.c's check
that saves/ and mods/ can be written made and removed its probe file in
each, and frame.ppm, git-ignored, was written there and deleted;
settings.json, slot1.json and mods/README.txt untouched.)

*Changed* (the controls): src/core/keys.c and keys.h (new),
src/platform/platform.h (Input's nav, back, key, pad_input; Controls;
platform_pad_style), platform_sdl3.c, platform_headless.c (PS2_RAW,
PS2_PAD_STYLE, PS2_FRAME_HZ), src/game/sysmenu.c and .h (the CONTROLS and
BUTTONS pages, "keys" and "pad"), src/main.c (the whole Input pending; PAD
1's style), src/harness/probe.c and .h (key, pad, held or not, pad-style,
expect keys|buttons), tools/route_probe.c (its head),
tools/controls_check.c and .py (new), tools/art_dump.c, building_probe.c,
intro_probe.c, menu_probe.c and pause_probe.c (an Input of nothing by
input_clear: key and gamepad input none, not 0), tools/make_dist.py (the
package's README), routes/controls_keys, controls_buttons, controls_rebind
(new), Makefile (`controls_check`), .gitignore, README.md,
docs/harness.md, CLAUDE.md, NOTES_next_context.md. `make` (the game),
`make headless route_probe controls_check`.

**Not tried on a gamepad** (none was connected while the controls were
made: every check is the headless game's scripted presses and
route_probe's): the BUTTONS page with the DualShock 3 in hand (it should
read CROSS, CIRCLE, SQUARE, TRIANGLE, SELECT, START), binding its buttons,
d-pad, sticks and triggers (half travel), the window's keys rebound (the
SDL3 platform's keyboard and gamepad path is compiled and started under
SDL's offscreen driver, not pressed), a joystick SDL has no mapping for,
and gamecontrollerdb.txt with a real pad (a file of two made-up mappings
beside the game read as "gamecontrollerdb.txt: 2 mappings").

## Build 160a: a DualShock 3 on Windows, through DsHidMini's SXS mode (2026-10-17)

The owner plugged the DualShock 3 into the Windows PC (at the machine,
not over remote desktop). What a scratch probe on SDL 3.4.18 found (the
joysticks SDL lists, a gamepad's mapping and rumble, its events):

- **Windows's own driver** shows it (USB 054C:0268, "HID-compliant game
  controller"), but a DualShock 3 sends nothing until it is asked (a
  feature report, $F2), which Windows's HID class doesn't pass: SDL's
  own PS3 driver (SDL_HINT_JOYSTICK_HIDAPI_PS3) logged "Couldn't read
  feature report 0xf2" and left it to the generic path, which got no
  input. Nothing a game can do there.
- **DsHidMini** (Nefarius's driver, v3.20.1, signed; its control app
  wants Microsoft's .NET 10 Desktop and ASP.NET Core 10 runtimes): its
  default mode, **XInput**, makes it an Xbox pad -- SDL's "Xbox One
  Controller", its buttons, sticks and triggers and the rumble, with the
  Xbox names. Its PS button is then the Guide button, which Steam takes
  for itself unless Steam's "Guide Button Focuses Steam" is off.
- Its **SXS** mode (Sony's sixaxis.sys's protocol; the control app's
  Configure, Settings mode Custom, SXS, Apply changes): through Windows's
  generic path SDL named it "Nefarius Software Solutions e.U. DS3
  Compatible HID Device", a PS3 type, but its rumble failed (DirectInput's
  force feedback, 0xC0110004) and no input reached the probe, though its
  raw reports came 200 a second; with
  SDL_HINT_JOYSTICK_HIDAPI_PS3_SIXAXIS_DRIVER, SDL's own PS3 driver took
  it: "PS3 Controller", its axes at once, and the rumble (The owner
  felt it, both motors full for 3 s).

So the game sets that hint on Windows before SDL_Init (`platform_sdl3.c`,
platform_create): PS3 CONTROLLER on the GAMEPAD page, the PlayStation
names on BUTTONS, the rumble. SDL reads it on Windows only, and only for a
pad speaking the Sony driver's protocol (DsHidMini's SXS, or Sony's own
sixaxis.sys); XInput mode and every other pad are as before. The README's
gamepad paragraph says how to set DsHidMini up.

Checked: `make` (the game) with no warnings. Only the SDL3 platform
changes, which nothing headless builds: the routes, the proofs and
route_probe run the same code as build 160. The game in the window with
the pad in SXS mode: The owner tried it, "works".

## Build 161: the rumble follows the sounds -- the blasts, the shakes, the flights, the holes (2026-10-17)

The owner, after builds 160 and 160a: "I was thinking for phantasy star
and phantasy star 2 the dual shock should rumble not only when a
character dies, but when they get hitt. perhaps the vibration should be
informed by the durration of the sound effect of the strike? There should
also be vabration from walking on dangerous tiles. Vibration from
explosion events. Maybe special slow rumble during space flight." (The
PS1 port's is its build 57.)

**Why a blow was hard to feel.** Build 160 shook a blow on the big motor
alone, 35% + 65% by the share of the member's max HP it took, held 4
ticks and down over 8 to 20: at MEDIUM 26% to 75% for about a fifth of a
second, the small motor (50%) only for a quarter of the HP or more. A
DualShock 3's big motor is a weight on a shaft, the only one of its two
it drives at a strength; at a quarter of it for 4 ticks it hardly turns.
A fall or the game over shook both motors full for much longer: that was
what was felt.

**A shake that follows a sound.** The sound driver plays an id the game
asked for at its next frame -- at the next tick's start, before the game
runs -- if its priority lets it, and PlaySFX stops every effect track
before it loads the new one. So the driver now keeps, as the port's own
diagnostics beside `music_started` (no ROM byte, nothing in the driver
reading them), the effect it last started and a count of the starts
(`Smps.sfx_started`, `sfx_starts`), and walks an effect's header for the
tracks it takes without playing it (`smps_sfx_tracks`). The rumble reads
them each tick: an effect started cuts the shake that followed the one
before, and a followed one is followed -- each motor its peak by the
effect's loudness while its tracks play (its volume against where it
began, SMPS's 0.75 dB a step, 30 dB to nothing; a track at rest
silent), then a 6-tick tail. The game's motion comes from `game_motion`
(game.c, for the rumble alone): the spaceship's flight and which part of
it has the screen, the pause, the building, the scene's shake.

- **A blow** in a fight: as it lands (SXFID_Sword asked, where build 160
  felt it) the small motor full 4 ticks -- on or off on a DualShock 3,
  which it turns on at any strength -- then, at the next tick, the sword's
  sound followed (13 ticks): the big motor 50% + 50% by the share of the
  member's max HP (all of it at half), the small one along for a quarter
  or more; then the red flash's sound (SXFID_DamageRedScreen, from 16
  ticks after the blow, each cutting the last) followed at 40%, a fainter
  echo, when it comes within RUMBLE_RED_SPAN (64) ticks of a blow -- a
  blow that took no HP flashes too, and was never a blow felt. At MEDIUM
  a blow is 37% to 75% for 13 ticks, the echo 30% to about tick 70. A
  sword that never starts (the priorities: a louder effect playing) is
  shaken its 13 ticks unfollowed, counted. On the map (Roron's cake) no
  sword is heard: the same peaks held 13 ticks.
- **A blast**: SXFID_Explosion followed -- the Darum scene, the
  Climatrol's sixteen bursts (event $29, restarting it again and again),
  Gaira's control panel, Gaira's prison blast -- the big motor 85%, the
  small its first 8 ticks; its 49 ticks fall 6 dB every 8 and the shake
  with them.
- **The shake**: while the scene's camera shakes the planes (the Climatrol
  "shaking terrifically", CAMERA_JITTER, 0-1 pixels: the big motor 50%;
  then exploding, CAMERA_SHAKE_THEN_RELOAD, 0-7 pixels: the big 90% and
  the small full), the pause aside.
- **The floors**: a damage floor's bite firmer (the small motor full, the
  big 60%, 8 + 6; build 160's 60% and 30%, 6 + 6); and a fall through a
  hole or a crevice (SXFID_FellInHole) followed: the big 70%, the small
  its first 6.
- **The flights**: the spaceship from the Central Tower's roof -- its
  departure's sound followed as the ship fades in (the big 90%, the small
  its first 10), then while it flies (fading in, SpaceShipLoop) the big
  motor 50% swelling 15% either way over 128 ticks (a sine from a table
  of integers); the pause stills it, the Pause sound cutting the
  departure's; landed as SpaceShipLoop gives way to the level (both full,
  4 + 24), the landing's sound followed (the big 80%) till the level's
  music cuts it. Tyler's ship (BUILDING_TYLER_SHIP), the escape from
  Gaira, throbs too, lower: 40% swelling 10%.

The fall and the game over are as build 160 had them; LOW, MEDIUM and
HIGH scale everything as before; the Escape menu stills every shake as
it opens, a flight's throb coming back when it closes. Felt kinds now
hurt, fell, lost, floor and (new) blast, shake, flight, landed, hole.

**The checks.** route_probe's `PS2_RUMBLE_CHECK` (docs/harness.md)
gains, read apart from the watcher from the driver's starts and
`game_motion`: blast and hole felt exactly as SXFID_Explosion and
SXFID_FellInHole start; flight exactly on the ticks a ship flies, shake
on those the camera shakes, the pause aside, the motors going on each,
and still once a flight is paused 40 ticks; landed exactly as
SpaceShipLoop ends; a blow's small motor going as it is felt; and from a
followed effect's start, while the tracks in use then play: at every
tick it is heard the big motor at 40% of full by its loudness or more;
where nothing but sounds was felt for RUMBLE_LONGEST ticks and nothing
flies or shakes, the big motor no higher than the loudness, and, the
sound over more than the tail and a tick (and a blow's kick done), both
still. Its report gains a line: the ticks a followed sound was heard, the
blows whose sword never started. tools/rumble_check.py reads the nine
kinds and wants each felt somewhere. Two routes are new: rumble_hole (a
level-5 Rolf walked into Crevice's crevice: the fall's sound followed
55-138, still at 139, Crevice B1) and rumble_flight (roof_yes's flight:
the departure, the throb, the pause in SpaceShipLoop stilling it 45
ticks, Start bringing it back, the landing at 580, still on Skure).
rumble_fight and rumble_floor take the new lengths: the blow's kick at
269, its sword followed 270-282, the red flash's sound from 286 to 333,
still at 340; the floor's buzz 8 + 6.

Found on the way: a red flash's sound with no blow near it -- a hit that
took no HP flashes the member too -- in eijia_far_column at tick 994 (the
last blow at 857): followed, it shook with nothing felt for 150 ticks.
Hence RUMBLE_RED_SPAN.

Changed: src/game/rumble.c and .h, src/audio/smps.c and .h
(`sfx_started`, `sfx_starts`, `smps_sfx_tracks`), src/game/game.c and .h
(`game_motion`), src/main.c and tools/route_probe.c (the watcher given
the motion; the rules), tools/rumble_check.py, routes/rumble_fight,
rumble_floor, rumble_hole (new), rumble_flight (new), README.md,
docs/harness.md, CLAUDE.md, NOTES_next_context.md.

`python3 tools/rumble_check.py` PASS: 146 routes and 160 fuzz runs,
1,055,711 ticks; felt hurt 1,139, fell 199, lost 101, floor 547 (build
160's counts), blast 259, shake 1,758 ticks, flight 6,621 ticks, landed
3, hole 1; none late, no rule broken, one HP drop elsewhere (as build
160's); every headless case as before.

**Faults by hand**, 16, each in a copy of the tree, route_probe built
there and the rumble's routes (rumble_fight, rumble_floor, rumble_lost,
rumble_hole, rumble_flight, roof_yes, darum_teim, climatrol_nei_wins,
army_eye_capture, fight, eijia_far_column) and two fuzz fights run under
the rules: each caught, a copy with none passing. Build 160's blow back
(rumble_fight FAIL; a blow felt, the small motor still); a followed shake
never ending (a followed sound over, motors going: darum_teim,
climatrol, Gaira); a new effect not cutting the followed one
(rumble_fight still going at 340); the loudness ignored (the big motor
louder than its sound: the explosions); the throb on in the pause
(rumble_flight FAIL, flight felt in the pause); Tyler's ship no flight;
the explosion not a blast; the hole not felt (rumble_hole FAIL); the
jitter not felt (the Climatrol); no landing felt (rumble_flight,
roof_yes); every sword followed, the party's blows too (a followed sound
over with nothing felt); a red flash followed with no blow near
(eijia_far_column's tick 994, a fuzz fight); the driver's starts not
counted (rumble_hole FAIL); an effect's tracks one channel off (the
followed sound under 40%: the sword, the landing); the flight's fade-in
not the ship's (motors going, nothing felt); the pause not told
(rumble_flight FAIL).

**Proofs** (this machine; build 160a's tree from git beside it,
`tools/proofs/proof.sh` on both, 16 jobs, PROOF_COLOUR=1: b160a and
w161). The same in both: build warnings none, crt_check, crt_sim_check,
patches_check, audio_check, the attract WAV (97141d6d...), the Dark
Force trials (25d3ca5f...), probe_check 5, mods_check 16, colour_check
SAME; the routes 146 PASS (144 and the two new), 713 route files (707):
the two new routes' 6, and 6 changed, every one a rumble route's --
rumble_fight's log, motors and summary (its new lines), rumble_floor's
log and motors, rumble_lost's motors (its blow's kick and sword); the
colour hashes the same but the two new routes'. So the sound driver
plays as before with its two new diagnostics, and the game as before
with game_motion. `make` (the SDL3 game, Q in it) and `make headless
route_probe` with no warning. Under ASan and UBSan (CLANG64,
route_probe_asan): the six rumble routes, darum_teim, climatrol_nei_wins,
army_eye_capture, roron_cake, fuzz_battle and a fuzz fight under the
rules: every one PASS, no rule broken, no report; clang's warnings the
tree's own (songtext.c, battle.c, route_probe.c's g_cap_off), none new.
Not run: the build with no C++ compiler and no Q (only C files changed).

**Not felt.** Nobody has felt any of it: the strengths and lengths are
first guesses at what a DualShock 3 gives -- a blow's 37% to 75% at
MEDIUM and its echo's 30%, a flight's 26% to 49% (Tyler's ship 22% to
37%), which may be under what turns its big motor. The game in the
window with the pad: a fight's blows, small and big, the Darum and Teim
scene, the Climatrol's shaking and exploding, Gaira's blasts, a damage
floor, a crevice, the roof's flight from takeoff to landing and the pause
in it, Tyler's ship, at each RUMBLE. The SDL3 platform is as build
160a's.

## Build 161a: the package's zips dated in UTC (2026-10-18)

The owner's task for the download page (the three ports' Windows
packages and Herzog Zwei's disassembly, on one page): the packages online
gave away the builder's UTC offset, and are made again. (The PS1 port's
is its build 57b, which mends its empty source.zip too; Herzog Zwei's
package gains a source.zip.)

**The times.** A zip entry's time is a local time with no zone.
make_dist.py's entries took each file's own time (`z.write`,
`ZipInfo.from_file`), local, and source.zip's took what git archive put
on them; the exe's link stamp is UTC. Side by side the two gave away the
UTC offset of the machine that made the package, which the commits'
TZ=UTC keeps out of git. Now every entry of both zips, the package's and
source.zip's, is dated HEAD's commit time in UTC (`zip_stamp`), the way
herzog-zwei's tools/hz_dist.py dates its zip; an entry keeps its name and
mode, not its own time or its extra fields (git archive's carry the time
again). Outside git (build 138a's tree) the time is
1980's first second, a zip's earliest. The embeddable Python's own
python314.zip, in python/, keeps python.org's times.

Checked: `make dist` (dist/ps2-windows.zip, 24.4 MB): the zip's 52
entries and source.zip's 2070 all at the commit's time, UTC; no
maintainer's name or machine path in either; the server's check of a
package before it is published passed it ("one fixed time for every
file", source.zip 1948 files). Nothing under src/ changes: the game and
the headless game are build 161's.

## Build 162: COMPUTER PLAYS on an older Python, and AUTO FAST but for the story (2026-10-19)

The owner, on the Windows desktop: "When I recently tried it on fast
mode, it randomly released computer play to player controll after it went
to save in data memory. Also when in auto mode it decided to drop out of
fast mode to show me the team selling gear in the item shop. It's only
supposed to drop out of fast mode when it's attempting a story beat."

**The hand-back.** The game took the first Python on the PATH. The tree's
ps2.exe has no bundled Python (only `make dist`'s package carries one,
3.14), and the desktop's PATH has a Python 3.8 first. Build 113 made every
text file the tools write LF-only with `Path.write_text(...,
newline="\n")`, a keyword Python 3.10 added. So under 3.8 the driver ran
until it first noted a save: a stage's checkpoint at a Data Memory
(`note_last`, outside the stage's own try) or a save on the way
(`trip_save`'s note). There it died on a TypeError, its report's write in
`finally` failed the same way, and the game, seeing the driver gone, handed
the pad back with no word. The run's saves/computer/driver.log (an AUTO run
from slot 1 at the gear stage, which replaced the FAST run's) ends with
that traceback. Reproduced: build 161 in a scratch tree with that Python
3.8 alone on the PATH, NEW GAME, FAST, `--to paseo --seed 12`: "the pad is
yours again after 28592 ticks", Paseo's checkpoint written and last.json
not.

* tools: `play.write_lf(path, text)` (open with newline="\n", 3.8's) for
  the seven writes in the driver's modules (computer_plays.py, play.py's
  trip-save note, story.py's and playthrough.py's reports). The driver
  says so and stops on a Python older than 3.8 (driver.log).
* autoplay.c (Windows): every Python found on the PATH is asked whether it
  is 3.8 or newer (`-c` with a check of sys.version_info, once a session;
  until 162 only the Store's reparse points were asked, `-c ""`), and an
  older one is passed over for the next; with none, NO PYTHON3.
* tools/probe_check.py `driver_on_python38`: the driver's five modules
  parse as Python 3.8 (ast's feature_version) and call none of the newer
  Pythons' methods and keywords it lists. It FAILs on 161's tools (the
  seven writes) and passes now; probe_check 6 of 6.

**AUTO.** Build 120 made AUTO the other way round: NORMAL, FAST only while
the driver ground (its `@grind on`/`@grind off` host lines), so all between
the grinds went at 60 Hz -- the gear stage's Paseo shopping between its
rounds in Arima's field, the walks, the chests. Now AUTO and AUTO 2X are
FAST but for the story's beats, which the game itself reads off the screen
(game.c `game_story_beat`, called once a tick by main.c, read by nothing
in the game): the ending; a boss (formation $100 and up, The Place of
Death's music); a scene on the map (the story's level events, the Teim and
Darum scene, the camera cases but the gum's way out, Nei's death's
pictures, a demo walk, the party gone or held); four buildings door to
door (Rolf's house at the start, the Governor, Gaira's control panel,
Tyler's ship); and the plot's scripts (`story_script`) in the text window,
each the first time it shows since the console's last reset, and on the
intro screen between the dream's pages. NORMAL from the beat's first tick
-- AUTO's FAST frame ends there -- until 60 ticks after it ends. stderr
names each beat as it begins (`computer plays: story beat at tick N: the
story's words ($1801 I've seen that face! ...)`) and says when the game
goes fast again. The driver's grind lines are taken and not read;
autoplay's `grinding` flag is gone.

A first cut also held the roof's spaceship, every visit to the Esper
mansion and Shir's return as beats: seed 12's whole game had 105 beats, 55
of them those -- 34 mansion visits (Lutz sends the party to Noah there on
every trip of the Noah grind), 7 flights, Shir back 14 times. They are
travel and thefts, not story; Lutz's first meeting is still the B1 Espers'
scene and his own words.

**Checked** (headless, the window's own path, scratch trees, slot 1
copied in, never saves/):
* The reported case: slot 1 at gear, AUTO, Python 3.8: through Shure, then
  through Nido -- every gear round's shopping FAST; the beats the Shure F3
  pocket (the Small Key and the Letter), the walk in at Nido's door after
  the Dynamite, and Teim; no harness error.
* Seed 12, NEW GAME, FAST, to the ending under that Python 3.8 (273 s) and
  MSYS2's 3.14 (207 s): the same game -- the 37 checkpoints byte for byte,
  the story logs differing only in the checkpoint lines' path separators
  (Windows's Python writes backslashes).
* The same seed on build 161 (git archive HEAD, built in scratch) and on
  162: the story log and checkpoints identical.
* AUTO against FAST on 162: the story log and checkpoints identical (speed
  changes nothing the game decides); 55 beats, 153,348 of 11,875,022 ticks
  at NORMAL (43 minutes at 60 Hz): Dark Force's five tries 65,344, Mother
  Brain's confrontation through the ending 39,501.
* The harness error in seed 12's game, `esper try 1: KeyError None at
  play.py:999` (991 on 161), is build 161's too; the retry plays it.

**Proofs** (w162b against w161, this machine, 32 jobs, PROOF_COLOUR=1):
the same as 161's but probe_check's new line: build warnings none, the
routes 146 PASS and their 713 files byte for byte, crt_check 31,
crt_sim_check 44, patches_check 14, audio_check, the attract WAV, the
Dark Force trials, mods_check 16, colour_check SAME (146 routes);
probe_check 6 of 6. rumble_check.py PASS, `make controls_check` and
controls_check.py PASS. `make` (the SDL3 game, Q in it), `make headless
route_probe patches_check` with no warning. Only C and Python changed;
not built: the game with no C++ compiler and no Q.

Not seen: the window. The owner, at AUTO from a slot or a new game: the
game should race but for the scenes, the plot's conversations and the
bosses, with the music back at each.

## Build 162a: AUTO shows the gear bought (2026-10-19)

The owner, on 162's AUTO: "I actually don't mind it showing normal speed
purchasing gear, but it's sort of tedius watching the game sell a bunch of
loot." 162 played every shop FAST. Now a visit to a weapon or armour store
where the computer buys gear plays at NORMAL (AUTO 2X: 2X) from inside the
door to the street, and a second after; the walk to the door, the item
store -- where the old pieces are sold, and the Monomates, Trimates and
Escapipes bought -- and the rest stay FAST.

The game cannot tell such a visit at the door: the shir stage walks in and
out of stores buying nothing, for her thefts. So the driver says: gear_up
brackets its purchases with `@gear on` and `@gear off`
(`play.Session.buying_gear`), host lines as the grinds' were; autoplay.c
keeps the flag (`autoplay_buying_gear`, read by the frame loop while it
holds the game, as the command thread sets it under the lock), and main.c's
`beat_watch` holds NORMAL while it is on and the screen is a building.
stderr: `story beat at tick N: buying gear (the driver's @gear on)`.

**Checked** (headless, the window's own path, scratch trees): slot 1 at
gear, Python 3.8, `--seed 7 --to nido`, AUTO against FAST: the same story
log; 11 purchases at NORMAL, each about 450 ticks (7.5 s), the sales FAST,
then the three beats of 162 (the Shure F3 pocket, Nido's door, Teim).
Seed 12's whole game, AUTO against FAST and against 162's FAST: the same
story log and checkpoints; 97 beats, 42 of them purchases (24,630 ticks),
177,978 of 11,875,022 ticks at NORMAL (49 minutes at 60 Hz).

**Proofs** (w162c against w162b): every file the same as 162's
(compare.py exit 0): build warnings none, the routes 146 PASS and their 713
files, crt_check 31, crt_sim_check 44, patches_check 14, audio_check, the
attract WAV, the Dark Force trials, probe_check 6 of 6, mods_check 16,
colour_check SAME. `make` and `make headless route_probe patches_check`
with no warning. Not seen: the window.

## Build 163: languages, and the retranslation's text in its own face (2026-10-19)

The owner: language options for the port, one of them Excalibur_Z's
English retranslation "using his new font", Japanese later, "selectable in
a languages menu"; asked how much of the retranslation to bring in, "Text
and font only, keep the port's gameplay for now". docs/languages.md is the
reference; this is what 163 did -- the first of the plan's builds, every
message window. 164 takes the rest of the windows.

**The languages** (src/game/lang.[ch], new). `assets/lang/languages.json`
lists them; the first is the cartridge's, the game's own files; each other
a folder `assets/lang/<key>/` whose files stand in for the game's own of
the same path (`lang_path`; the script banks and the crawl's script are
read through it), plus its `lang.json` (the face and its advances),
`names.json` and `LICENSE.txt`. `assetfs_exists` (new): whether a file is
there to read -- cache, mods folder, pack or disk -- saying nothing when
not. The Escape menu's LANGUAGE (above QUIT, as GAMEPAD; `step_language`),
settings.json's `"language"`, the game taking a change up at its next tick
(`take_language`: lang_select, the script banks dropped, the party's default
names its own); main.c writes the file when `language_changed`.

**His text** (tools/import_retranslation.py, new; assets/lang/retranslation,
new). Read as data from his repository (commit 061e1f5): dialogue.json's
`en` into the port's notation, 611 script ids in the 26 banks; his seven
added messages left out (his engine's). `--check`: his `us` text through the
same conversion is the port's own 26 files byte for byte. For the original
game (ORIGINAL_GAME): 0604 and 0704, which he reworded for his shop flow,
written for the port from the JP ("Then who'll carry it?", "Then who's
gonna carry it?" -- not his); the rebalance's three items as the glossary's
Heilsam Boots, Whip, Bowgun; for 164, the main menu's labels in the
original's order and Amia's profile's whips. The face: vwfmixed.py's table
(parsed with ast) and diafont.py's two glyphs at its advances, the same as
his proofreader's copy; `font.png` a tile a script byte.

**The face, drawn** (window.c). A proportional language types each line
into a run (WinRun, kept with the window entry: glyph codes, row, column,
cells), a glyph a frame; scroll and clear move and drop runs with their
cells. win_render draws each run every frame into the text pool -- `g_vram`
grown to 3072 tiles, $800-$BFF past the VDP's 2048, reset in game_render
-- on its cells' own tiles (the paper) in ink 1, only the cells its glyphs
reach, and points the cells there. A glyph that would cross the line's end
is dropped with the rest (his engine's rule). The attract crawl's lines
the same way (draw_message_run, `win_text_draw`). His battle box
(`battle_box`): 24 cells at columns 7-32, two lines when a queued script
breaks its line (looked ahead with the new `script_text` hook, which has
none of `script_lookup`'s side effects: the probe's script log, the story's
beats), a two-line `{C6}` held 120 ticks.

**Names in messages.** `{item}`, `{tech}` (`gamedata_item_text`,
`gamedata_tech_text`) and `{enemy}` (WinSys's `enemy_text`, by
`battle_enemy_label`) whole in the language's letters (`lang_codes`). The
party's names are up to six letters (`GD_NAME_MAX`; the raw `$C600` block
keeps four, a four-letter raw name ending there); `gamedata_language_names`
at a new game, a load and a change of LANGUAGE makes any language's default
name the chosen one's. A name typed on the name board (four letters, as
the cartridge's) is the whole name: the new game's `commit_name` and the
renaming in Rolf's house end it after the four, where a six-letter
default's fifth letter stayed behind ("ROLFs", lang_new_name, which fails
without the two lines); intro_select's `subst_name` is six bytes, as
`{char}` reads six in a proportional language.

**The probe:** `language KEY` (not a player's command) and `expect setting
language KEY`. **Routes** (eight new, 154 in all): lang_menu, lang_shop,
lang_battle, lang_names, lang_gameover, lang_crawl, lang_ending,
lang_new_name.
**tools/lang_check.py** (new): settings.json through the headless game,
five checks, PASS; controls_check.py's `Headless.run` takes `mods`.
Makefile: lang.c in intro_probe, building_probe and save_resave.

**Proofs** (b163 against b162a_base, this machine, 20 jobs,
PROOF_COLOUR=1): build warnings none; the routes 154 PASS (146 and the
eight new), 739 files, of the 713 before all the same but 17 shots of the
Escape menu's main page (the LANGUAGE row; the frame two rows taller);
crt_check 31, crt_sim_check 44, patches_check 14, audio_check, the attract
WAV (97141d6d...), the Dark Force trials, probe_check 6 of 6 and
mods_check 16 the same; colour_check SAME, its frames' colours the same
for every route but the 22 that open the Escape menu's main page. rumble_check PASS
(every route, the 154, and the fuzz), controls_check.py PASS (its
`Headless.run` now taking `mods`), lang_check.py PASS (5 of 5).
`make` (SDL3, Q), `make headless route_probe patches_check`, intro_probe,
building_probe and save_resave with no warning. Not seen: the window, the
face at the panel's size.

## Build 164: the retranslation in every window (2026-10-19)

The second build of the languages' plan (docs/languages.md): his words and
face in the menus, lists and labels, which 163 left in the stock font.

**Names and labels as runs** (window.c). In a proportional language each
writer of a name or label puts a run where the stock writes glyph halves
(`put_run`, `put_run2`, `put_name_run`): items (ten cells, whole), the
party's names (four cells, up to six letters: his 32 px plate), techniques
(six cells), enemies (by `enemy_group_id`, new in WinSys, set beside the
names in battle.c), towns, songs, jobs, Heal and Miss, and the party
list's WHO?/ON? as his "Who?" and "To whom?" on its bottom border -- a run
on a border row is drawn as the border's cells are (draw_runs takes
`font_first`). The window maps' labels from names.json's `windows`
(`lang_labels`): a map row with letters is a label, his row of the same
place drawn from its first letter's cell to the row's next thing; the
counts must agree. The profiles' five lines; the name board's FWD, DEL,
END at its words' places. Left stock: NEXT and hp/tp/lv in small
capitals, the battle boxes' HP/TP (status icons go over them), the name
board's letters.

**His layout where his words need it** (`widen_for_language`): the
technique lists and the casting plate a cell wider, STRNG's two lists two
wider at columns 8 and 24 (his `wide_techs`; only Saschnella, 46 px, and
Nasaresta, 44, pass the stock 40), and the main menu a cell wider
("Techniques", 46 px; his own menu is Phantasy Star IV's, wider still).

**Six letters on the name board** (his `long_names`): `WIN_NAME_MAX`, the
board's `name_entry` and the slot label (`save_name`, `SlotSummary`,
`WinSlotInfo`) six bytes; `gamedata_set_name` (new) makes a name of what
was typed -- the hero, a renamed recruit, a Data Memory label -- in place
of 163's two copies.

**Routes** (five new, 159 in all): lang_menu_pages, lang_battle_lists,
lang_house, lang_places, lang_six_letters; lang_shop's shot now shows the
store's list in his names. **Proofs** (b164 against b163, this machine, 20
jobs, PROOF_COLOUR=1): every route of the cartridge's language and every
other proof the same as 163's -- build warnings none, crt_check 31,
crt_sim_check 44, patches_check 14, audio_check, the attract WAV
(97141d6d...), the Dark Force trials, probe_check 6 of 6, mods_check 16,
colour_check SAME; the routes 159 PASS, 766 files: of 163's 739 only three
of the language routes' shots differ (lang_battle's two: the party boxes'
names in his face; lang_shop's: the store's list in his names), and the
frames' colours of five language routes (lang_battle, lang_gameover,
lang_names, lang_new_name, lang_shop: his names in the boxes and lists
they show). rumble_check, controls_check.py and lang_check.py PASS. `make`
(SDL3, Q), `make headless route_probe patches_check` and the three tool
targets with no warning. Not seen: the window.

## Build 164a: the credits (2026-10-19)

The owner: "Give credits in the documentation if you didn't already."
The README has a Credits section now (the disassembly, lory90's; the
retranslation, Excalibur_Z's, with its own credit line quoted; the
libraries; SEGA's game), and docs/languages.md one for the retranslation:
what is his, what is the port's (the code drawing it, the import, the two
shop lines for the original flow), and that he built over the same
disassembly the port reads (its README and macros are byte for byte the
port's copy's). His notice in `assets/lang/retranslation/LICENSE.txt` says
where the text comes from, quotes his credit and names the port's two
lines (tools/import_retranslation.py writes it; the import again changed
that file alone). `make dist` puts it in the package as
`licenses/retranslation-LICENSE.txt` -- his MIT licence asks that it go
with his text, which is in the pack -- and the player's README.txt names
LANGUAGE and the retranslation, with the Escape menu's rows. Checked:
`make dist` on this machine, the notice and the paragraph in
dist/ps2-linux. Nothing under src/ changes.

## Build 165: Japanese (2026-10-19)

The owner put the Japanese cartridge's image in ~/GIT/PSII (ps2jp.zip:
PS2J.BIN, a 1 MB overdump whose first 768 KB are CRC32 BEC8EB5A, the image
Excalibur_Z's tools were checked against). LANGUAGE has JAPANESE now: every
word the game draws the cartridge's, in its font. docs/languages.md,
"Japanese", is the reference.

**The import** (tools/import_japanese.py, new; assets/lang/ja, new): the
font (FontsIconsArt at $26800 in the JP image, found by its bytes and by
the five loads of it that sit where the US's do), the character map
(VDPCharacterMaps at $129C8: 187 codes are characters, with his
tile-to-kana table), the text from his `jp` (the US-split messages' second
halves empty, 0D05 ended before its run-on bytes, the library's title row
dropped), the name board's six rows from its art (window $24 in the JP
table at $13568). The image stays outside the tree.

**A face of two rows** (lang.c `tall`, window.c `draw_upper`,
`win_text_draw_marks`): a code's letter and, in the row above, its
dakuten or handakuten, eight pixels a glyph; the marks drawn only where
they have ink. The JP sheet is not put in the US sheet's place -- its kana
are where the US has its status icons and window digits -- so the US sheet
stays for every icon, digit and frame, and the kana are runs, as his face
is. The crawl two rows a line in it.

**A charset of its own** (`charset_full`): every character the cartridge's
code, ASCII too; `load_script` and the probe's `expect text` read UTF-8
through it.

**Layout by flags** (lang.json `layout`, `lang_layout`): the wide battle
box, the wider technique windows, the wider main menu and six-letter names
are the retranslation's flags now, not every proportional language's; the
Japanese keeps the stock windows. The retranslation's lang.json gains the
four (its import writes them).

**The Japanese name board** (window.c `build_board`, `board_move`): six
rows of katakana at their art's places, every cell a stop but the blanks,
ススム/モドル/オワル advance, back, end; the cursor the port's own (the
cartridge's movement not read); the name as typed in the face.

**Names across languages** (gamedata `name_to_text`, `name_from_text`,
`gamedata_names_text`, `gamedata_names_set_text`): a name crosses LANGUAGE
as text, a default of any language becoming the new one's on the text (ROLF,
Eusis, ユーシス); saves keep the language's text. Every language but the
Japanese keeps the ASCII conversions it had, byte for byte.

**Routes** (five new, 164 in all): lang_ja_shop, lang_ja_crawl,
lang_ja_pages, lang_ja_name, lang_ja_names; lang_menu round three
languages. lang_check.py: a sixth check, settings.json's "ja".
**Proofs** (b165 against b164, this machine, 20 jobs, PROOF_COLOUR=1):
every route of 164 and every other proof the same -- the cartridge's
language and the retranslation's routes byte for byte (his layout moved to
flags changes nothing he shows) -- but lang_menu (its new presses through
JAPANESE: its log, summary, a third shot, its frames' colours); the routes
164 PASS, 784 files; crt_check 31, crt_sim_check 44, patches_check 14,
audio_check, the attract WAV (97141d6d...), the Dark Force trials,
probe_check 6 of 6, mods_check 16, colour_check SAME; rumble_check,
controls_check.py and lang_check.py (6 of 6) PASS. `make`, `make headless
route_probe` with no warning. Not seen: the window.

## Build 166: changing language with things on the screen (2026-10-19)

The owner: "It does have some glitches when switching between
languages." Reproduced with LANGUAGE changed in the C menu, in a shop
mid-message, in a fight and during the crawl: what was on the screen
stayed in the old language and was drawn with the new one's face -- a
message went on typing the old codes (US English's in the Japanese face,
nonsense kana), open windows kept their words (his names blank in another
language's), the crawl's box vanished. docs/languages.md, "Changing
language".

`win_language_changed` (window.c, new), at the end of `take_language`:
every open and painted window built again in place (`rebuild_entry`), the
text window's script copied again in the new language (`expand_script`,
split from load_script, through the side-effect-free `script_text`; the
id kept in WinSys's new `text_script`) and typed again from its start, or
shown whole when finished; the battle box's lines its language's; an open
name board the new language's. The crawl reads its script again when a
change dropped it (`g_opening_tried`, once a language).

**Routes** (five new, 169 in all), each failing on 165: lang_switch_menu
(the C menu through the three and back: on 165 his I of Items not there),
lang_switch_message (mid-message to Japanese: on 165 "あう てしあてくつ
だだだ..."), lang_switch_battle (the enemy's box and the party's: on 165 the
stock FIRE ANT left), lang_switch_crawl (on 165 no box), lang_switch_board
(the US board to the Japanese mid-name: on 165 the US board's codes in the
name). Not run: his two-line battle box changed to the stock one.
**Proofs** (b166 against b165, this machine, 20 jobs, PROOF_COLOUR=1):
every route's files the same as 165's (the 806 are 165's 784 and the new
routes' 22); the frames' colours the same but the new routes' and
lang_names' and lang_ja_names' (both change language as their fight
starts: the boxes drawn again at once now); crt_check 31, crt_sim_check
44, patches_check 14, audio_check, the attract WAV (97141d6d...), the Dark
Force trials, probe_check 6 of 6, mods_check 16, colour_check SAME;
rumble_check, controls_check.py and lang_check.py PASS. `make`, `make
headless route_probe` with no warning. Not seen: the window.

## Build 167: INTERLACED taken out (2026-10-19)

The owner, having read that the Mega Drive normally draws only "240p":
"Remove the interlaced effect please". This game sends 240p: 262 lines a
field, every field on the same lines (where SCANLINES ON's dark gaps come
from), register 12 set to $81 at boot and never changed (H40, no
interlace). Builds 159 to 159b's INTERLACED drew a picture it never sent.

Taken out whole: SCANLINES is OFF or ON again; the FLICKER row,
`CrtFlicker`, `crt_field_shift`, `crt_field_hold`, the two-field beam
table, `beam_row0` and DownParams' padding, `crt_gpu_draw_crt`'s field and
the platform's `field`, crt_ref's and crt_gpu_check's interlaced checks,
probe.c's `expect crt flicker`, the routes crt_interlaced and crt_flicker.
`crt.c`, `crt.h`, `crt_gpu.c`, `crt_gpu.h`, the down shader, crt_ref.c and
crt_gpu_check.c are build 158d's byte for byte; `make shaders` gives
158d's crt_down_rgb outputs byte for byte again. sysmenu.c and
platform_sdl3.c lose 159's parts and keep everything since. A
settings.json from 159-166 with `"scanlines": "interlaced"` reads as ON
(scanlines were what it drew), `"flicker"` passed over and not written
again: tools/controls_check.py's part 4 holds it (on 166's headless game
it fails: the file written back still "interlaced", with "flicker").

**Proofs** (b167 against b166, this machine, 32 jobs): 167 of 167 routes
PASS, their 798 files the same as 166's (the 806 less crt_interlaced's 3
and crt_flicker's 5); crt_check 27 (31), crt_sim_check 40 (44), as before
build 159; patches_check 14, audio_check, the attract WAV (97141d6d...),
the Dark Force trials, probe_check 6 of 6, mods_check 16 the same;
controls_check.py, lang_check.py and rumble_check.py PASS. `make all
crt_gpu_check` with no warning; `./crt_gpu_check` 105 of 105 on Direct3D
12 and on Vulkan (CLAUDE.md's count from build 142d). Not seen: the
window.

## Build 167a: the README on Windows's warnings (2026-10-25)

The owner, after Herzog Zwei's package said how to get past them: "add
the unblock note to the PS1 and PS2 readmes too". ps2.exe isn't signed,
and every way to sign it shows a verified legal name or doesn't fit a
port of a cartridge (herzog-zwei's docs/windows_build.md, "Unsigned").
So the Windows package's README says what to do when Windows stops it
(tools/make_dist.py's `UNSIGNED`, after the README's first paragraph,
only for a package made on Windows): "Windows protected your PC", More
info, then Run anyway; Unblock on the zip's Properties before unpacking,
and Windows asks nothing; Windows 11's Smart App Control, which blocks it
with no Run anyway, turned off in Windows Security. The download page's
"Before you play" says the same for all three games.

Checked: `make dist` (dist/ps2-windows.zip, 24.6 MB): the README
with the paragraph after its first; the server's check of a package
before it is published passed it. Nothing under src/ changes: the game
and the headless game are build 167's.

## Build 168: build 139b made again -- the game reads the tracker files (2026-10-28)

The owner, after build 157d found none of 139b's code in the tree
(Build 157d, "Found"): "redo 139b from the history notes". So build 139b
is made again from its section above, part for part, and named 139b in
the code and docs (README, this file and NOTES already said 139b). Read
for it: tools/tracker.py, src/audio/songtext.c, song_text.py's reader and
disassembler, tools/extract_music.py, tools/roundtrip.py, smps_trace.c,
assetfs.c's mods, audio.c's audio_load_music, docs/mods.md and
docs/audio.md's tracker format; from ps2.asm cfF7_Loop, cfF8_GoSub and
cfF9_Return again, for smps.h's comment.

**What was made**, as 139b's section says:

- extract_music's `write_trackers`: the 23 `.trk` files, beside the song
  files, and the manifest (`make manifest`: 1079 -> 1102 files).
- songtext.c's "Tracker files": the reader, `scan_songs`, `split_words`,
  `songtext_set_mod_test`.
- assetfs's `assetfs_is_mod`, which audio.c hands the song text.
- tracker.py's `game_music`, `image`, `song_span`, `timeline` and
  `timeline_difference`, and stream_text's label prefix.
- smps_trace's `--image FILE`.
- song_text.py's hold comments (`make extract-music` rewrote seven song
  files' comments, nothing else).
- smps.h's comment and make_manifest.py's.
- roundtrip.py's level 2 "tracker songs".
- docs/audio.md's "Build 139b" (written now: 139b's text was not in the
  tree), docs/mods.md's "A song's two files", and the README's rows.

**Where it differs from the notes, or what they left open:**

- smps.h: the stack is four bytes a gosub from $30 ($2C, $28, $24) and
  counter N is at $22+N. So the first gosub's return address already lies
  over counters 10-13, a second's over 6-9, a third's over 2-5. 139b's
  "counters 2-5" was the third's alone.
- extract_music's write_trackers assembles the text with the drum names
  it writes (`drum_names()`), not drums.json: `make extract` writes the
  music before the drums, so a fresh tree has no drums.json yet.
- game_music's dropped blocks are a dropped file's blocks less any a file
  still read also has, which is songtext.c's rule. Each tracker's stream
  text is made as soon as it is read, so of two broken trackers both
  readers refuse the same one first.
- The level 2 check: what a `.trk` plays (tracker.py's `timeline`) against
  what the ROM's song plays. It failed 12 songs until `timeline` left out
  `nop`. stream_text puts a `nop` between a note marked `cut` and its plain
  `tie`, where the ROM's stream lets the note go with a jump (Violation's
  subroutine ends `tie 48`, `return`, then `tie 6`). The chip writes are
  the same (below); the `nop` plays nothing.
- A tracker song's name is 47 letters at most in the C reader (its labels,
  `trk_<song>_drums_loop`, are words of 63).
- 139b counted "25 cases" for the image; every song, none, each of the 23
  alone and the mix make 26.

**Proof** (on this machine):
- The image: smps_trace --image against `tracker.py image`, the same bytes
  in 26 cases. With every song from its tracker, 31,836 bytes; with none,
  23,593 (the ROM's); each of the 23 songs alone, 23,624 to 25,390; the
  largest mix (every song but exciting_town, mystery and
  the_place_of_death), 33,234. These are 139b's sizes.
- The chip writes: smps_trace --regs, built through songtext.c, the music
  directory with the 23 tracker files against the stream text alone: 23 of
  23 songs the same, every write, frame for frame, over song_frames (182 to
  28,920 frames, 674 to 82,393 writes).
- The refusals: 34 edits to phantasy.trk read by both readers (the 31 of
  139b's list, a comment, the `song` line naming another song, a header's
  unknown instrument). 33 are the same message, file and line: the comment
  no error to either, and the song line refused by both. The header's
  instrument carries songtext.c's "(fm_instruments.json)", tracker.py's
  header reader does not.
- In the game: the headless game in a copy of the tree, the attract WAV
  (2400 frames):
  - no mods: 97141d6d...;
  - a phantasy.txt mod (its first note an octave down) read over the game's
    .trk: 6a4341fc..., the same as the same edit in a phantasy.trk mod;
  - an unedited phantasy.trk mod beside the .txt mod wins: 97141d6d...;
  - a .trk mod with a length off the tick: refused by file and line in
    tracker.py's words, set aside, the game's own music, 97141d6d...;
  - the copy's own phantasy.trk broken the same way: the refusal said, and
    no music.
- `make roundtrip`: PASS, 0 failures -- level 1 1041 of 1041 (the 23 `.trk`
  re-extracted the same), the manifest's 1102 files; level 2 "tracker
  songs" 23 of 23. And the check can fail: a note moved in a copy's
  phantasy.trk is named at its tick; a `cut` taken away is refused.
- proof.sh r168 against r167a (build 167a's, taken this session), the
  routes and the game reading the tracker files now: every file the same --
  167 PASS, 798 route files, crt 27, sim 40, patches 14, audio_check,
  attract.wav 97141d6d..., the Dark Force trial 25d3ca5f..., probe_check 6,
  mods_check 16, no build warnings.
- rumble_check, controls_check (both) and lang_check: PASS. `tracker.py
  prove`: 0 failures. Plain `make` (the game with Q), q_demo,
  ps2_headless_q and fm_probe with Q, and `make ps2 headless route_probe
  patches_check` with no C++ compiler and no Q (in a copy of the tree):
  built, no warning.

**Not run**: crt_gpu_check (an RDP session is on), PROOF_COLOUR. Nothing
was listened to: the WAVs and chip writes are compared, not heard.

**On the owner's side**: a song is edited in its `.trk` now; a `.txt`
edit plays only for a song with no `.trk`, or as a mod.

## Build 168a: FULLSCREEN on a Mac (2026-10-28)

The owner, playing the Mac build: "full screen doesn't appear to work on
mac". SDL3 on macOS makes fullscreen a Space of its own by default, and once
the GPU chain has drawn into the window that switch is taken and never
made: SDL_SetWindowFullscreen and SDL_SyncWindow both say yes, the window
stays as it was and SDL_WINDOW_FULLSCREEN stays off, with nothing in the
log. Fullscreen from settings.json at the start got there, being before
the first frame. A test of SDL alone (SDL 3.4.18 on an Intel MacBook Pro,
macOS 15) showed it in three runs of three: a window a GPU device has
drawn into, never; the same window with no GPU device, always; the GPU
window with SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES set to "0", always. So
platform_sdl3.c sets that hint on macOS before SDL_Init, and fullscreen
there is the borderless window over the display, as on Windows and
Linux, with no Space of its own and no slide.
(docs/macos_build.md, "Fullscreen"). The PS1 port (its build 71a) and
Herzog Zwei have the same line.

Checked: `make ps2` on the Mac, universal (x86_64 and arm64), the two
known clang warnings (songtext.c) for each; the game starts as before.
The switch with the game running is the SDL test's; for the owner at the
Mac to try in the game. Under src/ only platform_sdl3.c changes, and only
on macOS: `make ps2` there.

## Build 168b: the history merges from two machines (2026-10-28)

The owner: "We're going to need to figure out how to coordinate build
cycles between windows and mac". Builds are now pushed from more than one
machine (Windows, the Mac, the server), each adding its entry where the
last one went, so the second push's pull --rebase stopped on a conflict
in the notes (as Herzog Zwei's HANDOVER.md did on the Mac's fullscreen
fix). .gitattributes now merges docs/history.md with git's union merge: both
sides' new lines are kept, no conflict.
Only where entries are only ever added: tried on that HANDOVER.md
conflict, union kept the server's edited line and its old one too, with
nothing said. So NOTES_next_context.md, whose "Next" paragraph each build rewrites,
stays a plain merge: a conflict there is resolved by hand, both entries,
the newest last, and one "Next".

Nothing under src/ changes.

## Build 168c: the LoRA runs v3 to v7 written up (2026-10-28)

The owner: "record the LoRA runs v3 to v7". The HD art's runs after
build 158d (2026-10-13 to 10-17) were made in sessions outside this tree,
which kept every set, tool, log and LoRA in C:\AI and wrote nothing here:
docs/hd_art.md stopped at v2 and NOTES at "v2 finishes". Now hd_art.md's
"v3 to v7" (a table of the five runs, then each run's set, captions,
steps, restarts, results and the owner's verdicts) and "After v7"
(psclassic_v7 in use, the monsters on a photographic model with v7 at
half strength, the animation work, IP-Adapter turned down), its C:\AI
table two rows longer (C:\AI\tools, C:\AI\out\review), and the README's
row.

Read for it: each run's train.json and concepts.json (C:\AI\datasets\
train_v3 ... train_v7), the set builders' logs (anime_set_v4,
subjects_set_v5, v5_set, build_v6, build_v7, after_commons3), the
trainings' logs (their steps an epoch, saves and restarts), the files in
C:\AI\models\loras, the new scripts' own descriptions in C:\AI\tools, and
the three sessions' transcripts for the reasons and the verdicts.

Checked from the files, not taken from the sessions:
- Every train.json is v2's but for its paths and its sample prompts
  (v4's train_resume.json adds `continue_last_backup`); only the concepts
  and captions changed from run to run.
- The concepts' sizes and weights as the table and the runs' bullets give
  them, from concepts.json, the set builders' logs and the sets' folders
  (v3's four 5,407, 156, 582 and 6,968 pictures; v7's favoured in-game
  5,448, v6's 5,407 and the 41 of I's).
- The steps an epoch, the hours and which runs kept _e1 and _e2, from the
  training logs and the LoRA files.
- v5's own renders were about 21% of an epoch's samples (1,872 of 8,977,
  v5_set.log), not the session's 40%, which counted the renders' mask
  files as pictures.
- The owner's words quoted are word for word from the transcripts,
  spelling and all; three the sessions' notes gave that the transcripts
  did not hold word for word are left out.
- The monsters' model and strength from v5_pool.py (MON_CHECKPOINT,
  MON_LORA_STRENGTH).

Left as the sessions said, not checked again here: the lightness gaps
(v3, v4), the pickers' sizes, why v6's and v7's caching stalled (not
found then either). No picture was looked at again, and none was
measured for v5 to v7 (the sessions measured none).

Nothing under src/ or tools/ changes; no make target to run. The C:\AI
tools stay out of the tree (hd_dataset.py, hd_generate.py and hd_check.py
are still 158d's); bringing any in is the owner's call.

## Build 168d: the select screen's menu at a change of language (2026-10-29)

The owner, at the machine: "For the language switch on the data memory
first screen the lines don't update quite right. The flashing red cursor
floats over text with no perimeter box." Also: "full screen appears to
work fully on ac" (168a's FULLSCREEN on the Mac), and the DualShock 3
"good" so far, to be tried again; the GPU checks to come.

**Reproduced** headless, LANGUAGE changed both ways -- the route
language's `language` and the Escape menu's row (Escape, down nine, C,
Escape) -- through the three languages:
- A town's Data Memory (building 2, its attendant's portrait), at each of
  its stops: the welcome, each member's report, the question, YES/NO, the
  slot list. Every window built again and every message typed again, as
  build 166 has it. Clean.
- The select screen after the title (the backup check's messages, then NEW
  GAME / CONTINUE / ERASE GAME): the check's last message, "Data check
  complete." in the new language, typed into the menu's first two rows,
  over their words and their lamps; the third row the new language's; the
  cursor lit on a blank row. This is the screen the owner meant.

**The cause.** The text's place in the stack (`text_target`) stays where
the backup check's message window was when it closes, as the ROM's does,
and the menu opens in that place. Build 166's `retype_text` found a script
(`text_script`, the check's last) and a window shown there, and typed the
script into it. **Now** the window system's close step (`win_process`,
window.c) clears `text_script` when the window closing is the text's: the
script goes with its window. `text_target` is left as it was, so nothing
the game does changes; `text_script` is read only by build 166's change of
language.

Also tried, not kept as a route: a game saved in slot 1 at the Data
Memory (the route's own save directory), the reset to the title, then the
menu and CONTINUE's slot list through the three languages. Clean.

**Route** lang_switch_select (168 in all): the menu after the backup
check, to the retranslation, Japanese and back, CONTINUE's lamp at 65,56
and ERASE GAME's at 65,72. On 168c it fails at the first: index 11, $800,
the window's blue (the message over the lamp).

**Proofs** (b168d against r168, build 168's, this machine, 32 jobs; 168a
to 168c changed nothing the proofs build): every file the same but the
new route's six -- 168 PASS, 804 route files (798 and lang_switch_select's
4 shots, log and summary), crt 27, sim 40, patches 14, audio_check, the
attract WAV 97141d6d..., the Dark Force trial 25d3ca5f..., probe_check 6,
mods_check 16, no build warnings. lang_check.py, controls_check (both) and
rumble_check.py PASS; plain `make` (the game with Q) built, no warning.

**Not run**: crt_gpu_check (an RDP session is on), PROOF_COLOUR. Not seen:
the window.

## Build 168e: COMPUTER PLAYS's new game in every language (2026-10-29)

The owner, trying AUTO: "when starting new computer play with auto on it
gets stuck talking to your boss and won't advance the dialogue", and "I
was on retranslated".

**The cause.** The driver's first stage (story.py `stage_paseo`) played
routes/new_game.route, whose C presses are counted in the cartridge's
English: nine for the dream, twelve for the Governor, twelve for Rolf's
house. The retranslation's Governor has more pages (his line breaks), so
after the twelfth C he was still talking, and the route's `wait screen
level` waited its 3,600 ticks and failed; its later presses then landed on
whatever came. At AUTO the Governor is a story beat, played at 60 Hz, so
each of those waits was a minute of the Governor standing there. On the
probe the route in the retranslation reached the street at tick 13,131
with the C menu open (a stray C), against 5,813 in English; headless at
AUTO (seed 12) the opening's beat ran from tick 1,596 to 13,032, against
English's 1,564 to 5,820. In JAPANESE it was worse: the route typed ROLF
with the stock board's moves on the cartridge's board (build 165), and the
game never left the intro screen; the run gave up after six tries.

**Now** `play.Session.new_game` plays the opening: the route's commands
in its order, with no player's pauses (as inside a .route file), but the
pages answered until the street -- `wait idle`, then C while the game
waits, until the map waits on the pad -- instead of counted. The name:
ROLF typed on the stock board as the route types it (ENGLISH 1989 and the
retranslation share that board); on the Japanese board a blank name ended
at オワル, so his default stands. `play.japanese_board` asks the game with
`expect setting language ja` -- a read of what the Escape menu shows, which
the game's host allows, as it allows `expect flag` -- and `type_end` (the
name at a Data Memory save) takes the Japanese board's way too: until
168e the stock moves typed a letter there and the board stayed up. In
ENGLISH 1989 nothing changes: the route's game tick for tick. The route
file stays, as a route and among the files the game asks COMPUTER PLAYS
for; the driver no longer reads it.

**Checked:**
- tools/probe_check.py `new_game_languages` (new; 7 of 7): ENGLISH 1989's
  new_game() the route's tick (5,813) and state at the street; the
  retranslation's (6,135) and the Japanese (5,263) on the street with Rolf
  and Nei, the Governor's mission and the house's last words shown. The
  168d way, the same sessions: the retranslation at 13,131 with the C menu
  open, the Japanese on the intro screen at 19,494 with Rolf alone.
- COMPUTER PLAYS headless, the window's own path (the Escape menu by
  PS2_PAD, settings.json's language, seed 12), `--to paseo` at AUTO:
  ENGLISH 1989 the same story log and checkpoint as on 168d's driver, the
  same beats; the retranslation's opening beat 1,596 to 6,142; JAPANESE
  through paseo.
- The whole game at FAST, seed 12: ENGLISH 1989 to the ending, its story
  log and all 37 checkpoints byte for byte those of 168d's driver (a
  scratch tree with 168d's play.py and story.py); the retranslation to the
  ending; JAPANESE as far as hugh (below).

**Found, not fixed here** (for the owner to choose):
- JAPANESE shows two messages twice. Where the US split one message in
  two, the Japanese is on the first id and the second is empty (build
  165). Twice the US ends a message at the empty id, and the event code
  asks for the next separately: the Governor's ($1101 runs on through
  [3], the empty [4], into the mission at [5], which $1105 then shows
  again) and Lutz's ($190B on through [12], the empty [13], into [14],
  shown again as $190E). Seen on the Governor (twelve stops, the last
  three the mission again). The other twenty pairs end where the US does.
- COMPUTER PLAYS in JAPANESE stops at hugh: the stage's check looks for
  the library's $1014, which the Japanese leaves empty (its words end in
  $1013), so the stage counts as failed though the game went on. Other
  checks keyed to script ids may meet the same; not audited.

**Proofs** (r168e against r168d, this machine, 32 jobs): every file the
same but probe_check's new line -- 168 PASS, the 804 route files, crt 27,
sim 40, patches 14, audio_check, the attract WAV 97141d6d..., the Dark
Force trial 25d3ca5f..., probe_check 7 of 7, mods_check 16, no build
warnings. Only tools/ changed (play.py, story.py, probe_check.py): no
make target; the game's C is 168d's.

**Not run**: crt_gpu_check, PROOF_COLOUR. Not seen: the window.

## Build 168f: the whole game in each language (2026-10-29)

The owner: "You need to grind the whole game in both language modes to
spot errors", and after the first grind's report, "fix the Zosa Magic Cap
shopping too". The whole game played by the driver in RETRANSLATED and
JAPANESE, twelve seeds each, with ENGLISH 1989's twelve as the control,
and the game itself checking every line it draws.

**The tools.**
- `PS2_TEXT_CHECK` (window.c, new): in a proportional language, after
  every tick, each run of text in every window shown or painted held to
  the cells it is drawn in, by lang.c's own rule (the first glyph whose
  ink would cross the end is dropped, and all after it); a run that loses
  ink so is said on stderr once. And a message the game asks for whose
  entry is empty in the language. Off, nothing runs; on, the game does
  nothing differently. Proved to fire with a build whose room was cut by
  a cell: 40 lines in the opening alone (the Japanese lines fill their
  192 px to the pixel).
- `playthrough.py --language KEY`: the probe told the language at each
  start, the output under `route_out/playthrough_s<N>_<KEY>/`.
- `tools/script_ends.py --check` (below).

**The grind** (scratch trees, PS2_TEXT_CHECK=1, seeds 1-12, squeak-by,
the three languages at once, 11 games at a time each). Three rounds: the
first on 168e's driver and data, a second with the fixes below but Zosa's,
the last on this build:

| | to the ending | tries not finished | harness errors | text check |
|---|---|---|---|---|
| first: ENGLISH 1989 | 12 of 12 | 56 | 0 | -- |
| first: RETRANSLATED | 12 of 12 | 43 | 3 | 0 lines |
| first: JAPANESE | 11 of 12 (seed 10: Skure) | 46 | 1 | 0 lines |
| last: ENGLISH 1989 | 12 of 12 | 48 | 0 | -- |
| last: RETRANSLATED | 12 of 12 | 38 | 0 | 0 lines |
| last: JAPANESE | 12 of 12 | 45 | 0 | 0 lines |

- The text check said nothing in any of the 72 games in the two
  proportional languages: no line, name or label too wide for its window,
  no empty message asked for.
- The scripts reached, a round's twelve games together (the first):
  ENGLISH 1989 312 distinct, the retranslation 313, JAPANESE 297. The
  Japanese 15 fewer are the second halves of messages the US split, which
  the Japanese leaves empty and ends before (build 165), and chance (an
  item that cannot be carried, a cat met without the cap); nothing the
  game asks for is missing in either language.

**Found by the grind, fixed:**
- JAPANESE showed two messages twice: the Governor's mission and Lutz's
  second half (168e's "found, not fixed"). The event code asks for two
  ids in a row there (building.c's `win_script2`, `win_script_set2`:
  $1101 then $1105; $190B then $190E), and the Japanese ran the first on
  through the second's empty entry into it. `tools/script_ends.py` is the
  rule -- a language's message ends where the US one does -- applied by
  both imports and by hand to the two entries (Script_Governor [3],
  Script_Miscellaneous [12]: a trailing page break made the US end code).
  `script_ends.py --check`: PASS; on 168e's files FAIL, 2. One run-on is
  the languages' own and stays: $010B, the healing technique on a
  poisoned member, which the event code asks for alone (TechEffect_Res,
  ps2.asm:15266) -- the US ends it and loses $010C's line, the Japanese
  and his text run on into it (languages.md, build 163). The rule leaves
  it (`RUN_ON`); a first try of 168f ended it too, read against the ROM
  and taken back before anything was committed (the first grind's tree
  had it: only that message's text).
- COMPUTER PLAYS in JAPANESE stopped at hugh (168e): the stage looked for
  the library's $1014, which the Japanese never reaches (its words end in
  $1013). It looks for $1013, reached in every language.
- RETRANSLATED's hero was ROLF until the first CONTINUE and Eusis after
  it -- in seed 1's shots, "ROLF called Nei's name once more" in the dream
  and "Eusis" in the Mother Brain fight. The driver's new game typed ROLF
  on the stock board, the cartridge's default, and a loaded save makes a
  default name the language's (gamedata.c's read_char, build 165). Now
  the new game types ROLF in ENGLISH 1989 alone and ends a blank name
  elsewhere (`play.language_is`), so his Eusis stands from the start, as
  ユーシス does in Japanese: all twelve of the last round's Dark Force
  saves say Eusis. ENGLISH 1989 and JAPANESE play the same game as before
  (seed 1 to paseo: the checkpoint byte for byte the first round's).
- Zosa's shopping without a cap (every language's, the driver's). Zosa's
  shops, hospital and Data Memory answer only to a live Rolf in a Magic
  or Mogic Cap (CheckPlanetAndCaps), and two ways led there bare-headed.
  A Skure retry starts from the roof's save, before Skure's chests, so the
  squeak-by top-up went to Zosa with no cap in the bags ("Rolf in the
  wrong Cap"), bought nothing ("not bought?" for every piece, every trip),
  healed nowhere and sent the party back to Skure hurt: JAPANESE seed 10
  lost all six of its Skure tries so. And in the stage a Magic Cap handed
  to a dead Rolf stayed with Rudo; the retranslation's seed 10 then ground
  315 empty trips with Rudo at 5 HP. Now `story.cap_for_shops` starts
  every Zosa and Dezolis town errand -- Rolf alive (the clone lab), then
  the cap fetched to him from whoever carries it and put on -- and Zosa's
  top-up opens Skure B1's Magic Cap chest first when the party carries no
  cap, Rolf's head pinned for it as the Dezolis stages pin it. From the
  first round's roof saves, the top-up alone: JAPANESE seed 10 and
  ENGLISH 1989 seed 7 take the cap, buy their Zosa gear (about 290,000
  meseta at the cap's double prices) and heal; on 168e's story.py both
  bought nothing. "Not bought?" in the last round: 33 lines, none in Zosa
  (the first: 1,248, nearly all there); "wrong Cap": none.
- The Esper stage: a wipe-out on the way back from the Prism left no
  level, and `heal_here`'s door lookup raised (KeyError None) -- four
  tries in three of the first round's games. Now a try that did not
  finish, as any wipe.

**Checked beside the grind** (scratch measurements, not in the tree):
- Every character of both languages' scripts and names.json has a code
  in its charset and a glyph with ink (the misses are only in names.json's
  `source` note, which is never drawn).
- Every script line with its inserts at their widest: the longest
  default name, item, enemy and technique names, a name typed of the
  board's widest letter at the board's length (six in the retranslation,
  its long_names; four in Japanese), held to 24 cells, and the battle's
  own scripts to the battle box (his 24 cells; the stock 20 in Japanese).
  The retranslation: none over. Japanese: only the hospital's and the
  teleport's prices at six digits, which they never reach (a few hundred
  meseta at most: a price a head times four, doubled in the Magic Cap).

**Left:** the last round's 33 "not bought?" lines are the gear planner's
elsewhere, in ENGLISH 1989 too (a few pieces at Zema, Kain's Garda Boots
on Noah); no try failed for them.

**Proofs** (r168f against r168e, this machine, 32 jobs): every file the same but
probe_check's new_game_languages line, still ok -- the retranslation's
new game on the street at tick 6,097 (6,135: a blank name, not ROLF
typed), the Japanese at 5,021 (5,263: the Governor's mission said once);
168 PASS, the 804 route files, crt 27, sim 40, patches 14, audio_check,
the attract WAV, the Dark Force trial, probe_check 7 of 7, mods_check 16,
no build warnings; lang_check, controls_check and rumble_check PASS;
script_ends --check PASS. With PS2_TEXT_CHECK unset nothing of the check
runs. `make` to take the text check; the rest is tools/ and the two
Japanese script files.

**Not run**: crt_gpu_check, PROOF_COLOUR. Not seen: the window. For
The owner: COMPUTER PLAYS in RETRANSLATED from a new game (the hero
Eusis now); the Governor in JAPANESE, his mission said once.

## Build 169: the bags' room, the music back after FAST (2026-10-29)

The owner, after 168f: "fix the 33 not bought line. Translation makes it
though past Darum so far. I noticed some weird lag in the music not sure if
it's because the fast play messes with the amount of work the CPU was doing
so it fails timing? Also in the home a pitch ringswhen it exits the fast
playy something was a little off about the home music as well. Also just
curious why is the computer play logic writen in python instead of C?"

**The 33 "not bought?"** (docs/harness.md, "Bag room"). All 33 were a full
bag. An equipment store answers a purchase for a bag of sixteen with "no
room" ($0609, $0709) and shuts (building.c, `equipment_store`'s
EQ_CHOSEN), so everything after it on the driver's list went unbought:
- Zema (four games): Rolf's two Laser Knives filled his bag, and his Boots
  then shut the armour store on Hugh's and Rudo's too.
- Ryuon (three games, every visit): Kain's Garda Boots, his bag full of
  Trimates.
- Amy's second Lacon Emel, and Rolf's Titanium Chest at Kueri.

Now `play.plan_gear` counts each member's room. A purchase is planned only
into a bag with a slot free when it is made, and gear_up GIVEs before it
buys, so a piece given away frees its slot. A buyer still full first hands
one consumable to the living member with the most room (a "stow",
`play.STOWABLE`: the mates, Antidote, Star Mist and the pipes; never a key
item, gear or a Moon Dew). From the two checkpoints alone (scratch):
- Zema, seed 1 RETRANSLATED: a Star Mist and an Antidote off Rolf, and all
  twelve pieces bought.
- Ryuon, seed 5 ENGLISH 1989: `gear: Amy Hidapipe (from Kain, for room),
  Kain GardaBoots`, the Garda Boots on.

The grind, seeds 1-12 in the three languages at once as 168f's
(PS2_TEXT_CHECK=1):

| | to the ending | tries not finished | harness errors | text check |
|---|---|---|---|---|
| ENGLISH 1989 | 12 of 12 | 48 | 0 | -- |
| RETRANSLATED | 12 of 12 | 44 | 0 | 0 lines |
| JAPANESE | 12 of 12 | 43 | 0 | 0 lines |

"Not bought?": none (168f's last round, 33). Stows: 12, and no GIVE
refused. No probe stalls. The games are not 168f's: a slot freed or a piece
bought a trip sooner moves every seed's game from there on.

**The pitch in the home** (docs/audio.md, "Muted, then back"). Reproduced
headless: COMPUTER PLAYS at AUTO in RETRANSLATED, seed 1, the sound to a
WAV. FAST mutes; at a story beat the sound comes back, and the chips are
reset and the music's state replayed into them. Under VOICES EXTRA (the
default) and Q that replay keys each track from its own record, and a
record kept its keys down until its own track wrote again. A temporary
check in audio_resync, set against the chip's own key state, named the
stale ones:
- At the knock in Rolf's house (Rudo, tick 19,347), track 4's FM4 was down
  in the record and up on the chip. The street's music had held a note
  there when the house's music stopped it, and SilenceFM's key-offs are
  the driver's own writes (`SMPS_TRACK_DRIVER`), no track's. The house's
  music never writes FM4, so the note rang under it.
- An old effect's note (track 6, FM3) came back the same way on B.
- At later beats, the drum track's key-off came back: $28 = $80
  (cfF2_TrkEnd's FMNoteOff on the DAC channel, as the ROM writes it) keys
  FM1's operator 4, which FM1's next key write ends on the chip and never
  did in the record.

Seed 1's knock beat, the old build's WAV against the fixed one tick for
tick: 1.8 dB louder for five seconds, the difference -28 dBFS against the
music's -25 (the FM4 note, nearly as loud as the music), then -50 (the
chips' free-running phase, run to run). Fixed in audio.c's `shadow_track`:
a key write ends every other track's keys down on its channel among the
writes that reach the same chip, the music's and the driver's on A, the
effects' and the driver's on B. Rerun, the check found none stale at any
beat. Nothing played live changes; only a replay reads the records. The
synth switches replay the same records, so the same notes would have
struck at a switch after a song change. Route `audio_replay_silenced`
(new): the title's theme stopped with $FE, the driver's silence, then
`synth chip`. 168's route_probe fails it, the theme's chord struck again
at -16.9 dBFS; 169 passes, silent.

**The lag.** Not found here. At NORMAL and 2X the computer's frame waits
TICK_WAIT (12 ms) for the driver's tick, and a frame that gets none holds
the game and the music for a frame. That was the likely cause, so it was
measured. `PS2_TRACE` now prints the computer's frames (`computer rate R
ticks T (tick N)`, `<-- short` on such a frame), and `PS2_PACE` counts them
every five seconds. Seed 1 at AUTO to Arima, headless: 0 of 8,690 frames
at NORMAL short, idle, and 0 again under the 33-game grind. The return
from FAST: the level back on the first frame, a dB or two up for a frame
or two as the held notes strike again (by design, docs/audio.md), and the
stale notes above, gone now. If it is still heard: `PS2_PACE=1` in the
window says whether frames go short there.

**Python** (docs/computer_plays.md, "The shape"): the driver is the
harness. play.py and story.py played the whole game against route_probe
before build 67, and COMPUTER PLAYS put that player into the window rather
than write a second one in C. So the window plays the game the probe
proves; the C side is the interpreter both share.

**Proofs** (r169 against r168f, this machine, 32 jobs): every file the same
but routes_pass.txt, which has the new route's PASS line, and that route's
two files. 169 PASS of 169, 806 route files (804 and the new route's log
and summary); crt 27, sim 40, patches 14, audio_check, the attract WAV,
the Dark Force trial, probe_check 7 of 7, mods_check 16, no build
warnings. lang_check, controls_check, rumble_check and script_ends --check
PASS, and plain `make` builds `ps2` with Q. Q's own checks (qproof.sh,
q169 against q168f, 168f's binaries built from `git archive` in scratch
space): every file the same but attract_q.log, which names its own output
path; audio_check_q PASS. The switch routes (`audio_switch`,
`audio_voices`) and the WAV replay nothing a song has silenced, so they
did not move.

`make` to take the audio fix and the trace lines; the rest is tools/ (the
driver) and the new route.

**Not run**: crt_gpu_check, PROOF_COLOUR. Not heard: the window. For
The owner: COMPUTER PLAYS at AUTO into Rolf's house (the knock, Rudo's
and the others'); nothing should ring there now. And the lag: where it was
heard, which machine, at which SPEED; `PS2_PACE=1` prints the frames that
went short.

## Build 169a: AUTO's grind at 32 ticks a frame (2026-10-29)

The owner, after 169: "I think the fast part on auto might be too fast
Interesting that it shows weapons purchases. I don't get to see a blur of
grind fighting it's almost like black outs between weapons purchases and
story points."

(The purchases are 162a's beat: the gear bought is played at NORMAL,
The owner's "buying gear can be watched".)

**Why it was dark.** Between the beats AUTO ran FAST: ticks back to back,
a frame drawn when 15 ms of them have run (or 4096). Measured with a
scratch build that, at each drawn frame, logged the screen, the fade and
how much of the frame was dark (and with `FV_EVERY=2`, every other tick of
FAST too). Seed 1, ENGLISH 1989, AUTO to Arima, headless on the owner's
desktop:
- 227 FAST frames for the stretches between the beats, about 1,650 ticks
  each: 27 s of the game a frame.
- A fight in Arima's grind is about 1,000 ticks on the battle screen (the
  median of 199; 720 to 1,558 for the middle 80%). So no fight lasted a
  frame, and each frame was a sample of wherever the game had got to.
- Of the 227: 98 a fight, whose screen is four-fifths black; 84 the map;
  21 the map fading in; 20 a building, a portrait on black; 4 the rest.
  At 60 Hz, a dark strobe about four seconds long between the beats.
- FAST's ticks themselves, every other one: 52% on the battle screen, 42%
  the map (4 points of it fading), 6% in buildings.

**The change** (main.c): `AUTO_GRIND`, 32 ticks a frame between the beats
at AUTO and twice it at AUTO 2X, every frame drawn and the sound muted
(`computer_muted`: the sound plays at one and two ticks a frame only).
The owner chose 32 from 16, 32, 64 and 128 offered, and twice AUTO's for
AUTO 2X. FAST is as it was. A grind frame ends at a beat's first tick, as
FAST's frame did, so the beat plays from there at its own speed.
PS2_TRACE's `<-- short` and PS2_PACE's count take only the frames with the
sound on (169's measure of the music standing still).

**Measured**, seed 1 to Arima (`computer_plays_speed.sh`, PS2_TRACE=1):

| SPEED | frames between the beats | frames in the beats | at 60 Hz |
|---|---|---|---|
| AUTO, 169 | (FAST) | | 9,419 frames in all |
| AUTO | 13,029 at 32 (95 fewer) | 8,526 at NORMAL | 6 minutes |
| AUTO 2X | 6,560 at 64 (98 fewer) | 4,265 at 2X | 3 minutes |
| FAST | 350 for all 422,663 ticks | | |

The 95 frames under 32 were the driver still thinking at TICK_WAIT or a
beat beginning. A fight at 32 is about 31 frames, half a second. All three
play 422,663 ticks with the same story log, checkpoints and slots as 169's
AUTO; only report.txt's and driver.log's wall-clock seconds differ. A
whole game at AUTO now: seed 12's 11.7 million ticks between the beats are
about 1.7 hours at 32 (AUTO 2X 50 minutes), besides the beats' 49 minutes
(162a).

No route: the routes run through route_probe, not main.c's COMPUTER PLAYS
loop. The measurement above, before and after, is the proof, as for 120's
and 162's speeds.

**Proofs** (r169a against r169, this machine, 32 jobs): every file the
same. 169 PASS of 169, 806 route files; crt 27, sim 40, patches 14,
audio_check, the attract WAV, the Dark Force trial, probe_check 7 of 7,
mods_check 16, no build warnings. lang_check, controls_check, rumble_check
and script_ends --check PASS, and plain `make` builds `ps2` with Q. Q's
(q169a against q169): every file the same but attract_q.log, which names
its own output path.

`make` for it: main.c alone (and sysmenu.h's comment).

**Not seen**: the window. For the owner: AUTO, and whether 32 reads as
the blur wanted; `AUTO_GRIND` is the one number to change.

## Build 170: COMPUTER PLAYS in C, step 0 -- the Python's games made portable (2026-10-30)

The owner, after 169a: "Take inspiration from the PS1 build and re-write
computer play in C". Then the plan's two questions: the C player is proven
by playing the Python's own games, command for command ("Same games as
Python"), and the Python stays afterwards as the reference and for the
trial tools ("Keep as reference"). And on the PS1 port's way: "PS1's
COMPUTER PLAYS is all C. Its player, tools/grind.c, is built into the game
with -DGRIND_NO_MAIN ... make computer_check checks that the game in the
window plays the same game as build/grind."

docs/c_player.md is the plan: the PS1 port's way (its grind.c in the game
and in the tool, one side running at a time, the window proved against the
tool), what this port keeps (the probe's commands as the player's verbs,
given in-process, and the hand-over of ticks it has), the proof, and the
builds in order. This build is its step 0, and changes nothing the game
does but the probe's `start new` (below).

**Two things in the Python's games C could not do the same.**
- The pauses' stream: `random.Random(f"{seed}/{stage}/{try}")`, CPython's
  Mersenne Twister seeded through a SHA-512 of the name. Now `play.Jitter`:
  FNV-1a (64-bit) of the name seeds a SplitMix64; `random()` is the top 53
  bits over 2^53, `randint(a, b)` a draw under the largest multiple of the
  count, mod it. Only those two are drawn (Session._pause, answer_page);
  computer_plays.py, playthrough.py and grind_trials.py seed it.
- Set order. An audit of the four files (every function read: play.py,
  story.py, computer_plays.py, playthrough.py) found four places where a
  set's order -- CPython's table's, by the hash seed and the version for
  strings, by the table's layout for numbers -- could choose:
  - plan_gear's one-handed weapons: equal scores in the set's order
    (FiberEmel and Dagger, 17; MirEmel, LacDagger and PoisonShot, 30;
    MirShield and Aegis, 32; Slasher and SonicGun, 34), which decided the
    six kept and the pair taken. Now by name among equals.
  - The holes plan() takes: `_holes_one` went through the floor's special
    cells in the set's order, and plan() takes the first hole to a
    landing. Forty floors have holes to two or more landings; on all but
    CreviceGroundF the set's order is not the map's (NavalF4 to NavalF1:
    the first hole (14,24) in the set's order, (33,12) in the map's). Now
    row by row.
  - Ground's anchors: the nearest cell, the first in the set's order among
    equals (the mild ground's (73,46) has two); now the lesser (x, y),
    which is the cell taken before.
  - story.nearest_town's start: the set's first cell; now its least (each
    caller's set has one).
  None prints a set. The audit's notes for the port (stable sorts, the
  first of equals, the path's heap, the floats) are in docs/c_player.md.

**The probe's `start new`** (probe.c, do_start). The first baseline (below)
had one game short of the ending: seed 5 in JAPANESE. Nei died on the walk
to Arima, Rolf had 11 meseta, and six tries never left Paseo. A try at
Paseo starts with stage_paseo's new game, and the probe's `start new` did
nothing once a tick had run (it presses the console's reset only for
COMPUTER PLAYS, `player_only`): the second try went on with the first
try's game, Nei dead, where the window's starts anew. Now the probe resets
too once a tick has run. Every route's `start new` is its first command,
at tick 0 (a route allows one start), so no route moves; probe_check's
`start_new_again` (new, 8 checks now) fails on 169a's probe ("intro, then
intro after the second start new") and passes on 170's.

**The games.** Seeds 1-12 at the watch pace (COMPUTER PLAYS's), the three
languages at once, PS2_TEXT_CHECK=1, on 170's probe:

| | to the ending | tries not finished | harness errors | text check |
|---|---|---|---|---|
| ENGLISH 1989 | 12 of 12 | 48 | 0 | -- |
| RETRANSLATED | 12 of 12 | 51 | 0 | 0 lines |
| JAPANESE | 12 of 12 | 66 | 0 | 0 lines |

No probe stalls, no "not bought?", 6 stows. JAPANESE's seed 5 took a second
try at Paseo, a new game now, and went on to the ending.

Every seed's game is new from 170 -- another stream of pauses, and the
holes -- so none is 169's. ENGLISH 1989's twelve again under hash seed 1
(playthrough's pin set to 1 in a scratch tree) and under Python 3.8.1
(Windows' python.org build), on 169a's probe (no Paseo try in them):
the same twelve games each, the story logs (but Python 3.8's paths in
the checkpoint lines, written with `\`) and every seed's 39 checkpoints;
and ENGLISH 1989 on 170's probe is that first round's again, 12 of 12.
Before the set fixes, hash seeds 0 and 1 played the same twelve games
too: the string sets' ties did not come up in them.

**Proofs** (r170 against r169a, this machine, 32 jobs): every file the
same. 169 PASS of 169, 806 route files; crt 27, sim 40, patches 14,
audio_check, the attract WAV, the Dark Force trial, probe_check 7 of 7
(8 of 8 with start_new_again, run after), mods_check 16, no build
warnings. lang_check, controls_check, rumble_check and script_ends --check
PASS; plain `make` builds `ps2` with Q. Q's (q170 against q169a): every
file the same but attract_q.log, which names its own output path.

`make route_probe` (and `make`) for the probe's `start new`; the rest is
tools/ and docs.

**Next**: build 171, the C player's foundation with Paseo and Arima
(docs/c_player.md).

## Build 171: COMPUTER PLAYS in C, step 1 -- the foundation, Paseo and Arima (2026-10-30)

The C player's first stages (docs/c_player.md, "Step 1"). src/player/,
about 9,200 lines in twelve files, ports what Paseo and Arima use of
play.py, story.py and playthrough.py, under the Python's names: the
session (send, the state line, the pauses, the downs and revivals, the
victory pages, the new game), the maps (LevelMap's path, goto, the doors,
holes and ways out, plan, travel, the towns' exits), the towns' buildings
and the C menu, the gear (plan_gear, gear_up, surplus, the mates, the map
techniques, revive, retreat), fight_smart and its planners, the grounds
(Ground, roam, roam_and_gear, the saves on the way, lean_short), the
script names, the two stages and playthrough's loop of tries, checkpoints
and report. It is built into route_probe: `route_probe --play` with
playthrough.py's arguments (`--base NAME` for the directory) gives each
command to the probe's interpreter in-process and reads the log back as
-i's stdout carried it to the Python. A later stage is a harness error,
`NotPorted: stage gear`.

The proof, the owner's choice: the Python's own games.
`tools/c_player_check.py --scratch DIR` plays seeds 1-12 in us,
retranslation and ja to Arima at the watch pace with both players and
compares their files: 36 of 36 the same -- the probe's log.txt line for
line (every command at the same tick, 60,166 to 72,843 lines a game, 6,991
fights in all), every checkpoint byte for byte, and story.txt, the summary
and the report but for its seconds. The story log was not to be a target
(build 170's plan); it matched, so it is held to it. Six of the games
under ASan and UBSan (CLANG64's route_probe_asan): nothing reported, the
same logs. A game to Arima takes the C player 3-9 s and the Python 67-69
s, 32 at once on this machine.

Seed 1 went first, a stage at a time: Paseo matched to line 1,865 (the
first `bags` of gear_up, then not ported), then to the end once the gear,
the fights and the ground were in. Of the 36 after that every log.txt
matched; what differed was the printing -- the report's revival tally
("2 x clone lab") and the seconds of a try that did not finish, travel's
"off the map" with the whole state as Python prints a dict, and the
script names in the story log, which script_ids.h names only in part
(gen_script_ids.py names the rest from the script's text; names.c does
the same).

What the port found in the Python, kept in docs/c_player.md for the stages
to come: `s.state` is a new dict at each send, so a function holding it
reads the state as it was (fight_smart's round, plan_gear's party before
its `bags`); `for ... in enumerate(known)` walks the list it began with
when the body rebinds `known`; weapon_blow's bags cache lasts across
tries; CPython 3.12's float `sum()` is compensated, which only
plan_type_tech (not yet reached) uses.

A slip, put right before the commit: c_player_check's first version ran
playthrough.py in the tree, which writes route_out/playthrough_s<seed>/
-- where the uploads' whole games are tracked -- so its run deleted and
rewrote those files. They were restored from git (`git status` showed
only this build's files after), and the tool now runs both players in a
scratch tree it makes (assets/, tools/, routes/, script_ids.h, the probe,
as tools/measure/scratch_tree.sh makes one) and refuses one inside the
tree.

Recorded from 169a's checks, as promised then: The owner's
crt_gpu_check at the machine, 105 of 105 on Direct3D 12 and on Vulkan
(the worst difference one code), and PROOF_COLOUR=1 proof.sh r169a_c,
colour SAME, no frame differing.

**Proofs** (r171 against r170, this machine, 32 jobs; without
PROOF_COLOUR, though this line said otherwise until build 172 found it):
every file the same but probe_check.txt, which has 170's eighth check
(start_new_again, run after r170 was taken). 169 PASS of 169, 806 route
files; crt 27, sim 40, patches 14, audio_check, the
attract WAV, the Dark Force trial, probe_check 8 of 8, mods_check 16, no
build warnings. lang_check, controls_check, rumble_check and script_ends
--check PASS; plain `make` builds `ps2` with Q. c_player_check 36 of 36
again on the final probe. Q's: not run (nothing under src/audio/q/).

`make route_probe` for `--play`; the game (`ps2`, `ps2_headless`) is
unchanged.

**Not seen**: nothing in the window changes.

**Next**: build 172, the next stage group in C -- gear, shure, nido,
darum, oputa, amy (docs/c_player.md).

## Build 172: COMPUTER PLAYS in C, step 2 -- the gear to Amy (2026-10-30)

The C player's next six stages (docs/c_player.md, "Step 2"): gear
(Arima's field a round at a time, then Paseo's stores, until neither
town's shelf holds an upgrade), shure (F3's body twice, then every
floor's chests, the Small Key on the locked ones), nido (the gear again,
the Dynamite on the door, F1's chests, Teim's scene on F2), darum (the
tube's scene), oputa and amy (the fourth member from Rolf's house, then
Oputa's grind until its shelves hold nothing better). What they stand
on, under the Python's names: src/player/things.c, new -- talk,
neighbours, search, level_objects (level_sprites.json), chests and
chest_item (chests.json), flag_byte and chest_shut, make_room; in
ground.c open_walk, wander and grind_and_gear, and roam_and_gear's
`until_geared`; `geared`, plan_gear's `unlimited` (every upgrade whatever
the money or the room, no log line); toss_item; and in story.c
shut_again, loot, the six stages and the top-ups of gear (Arima's
ground) and of oputa and amy (Oputa's grind). The player is about 10,400
lines. A stage past amy is `NotPorted: stage lab_in`.

The proof: `c_player_check.py --scratch DIR --to amy`, seeds 1-12 in us,
retranslation and ja, 36 of 36 the Python's on the first run -- seed 1
alone, then the 36: the probe's log.txt line for line (226,735 to 284,280
lines a game, 31,312 fights in all), every checkpoint byte for byte,
story.txt, the summary and the report but for its seconds. Six tries
failed and were retried as the Python's were (one at paseo, three at
arima, two at nido), the arima and nido retries after their top-ups.
Seeds 13-24 in us as well: 12 of 12. Six games under ASan and UBSan
(seeds 1 and 4 in us, 7 and 10 in retranslation, 3 and 12 in ja): nothing
reported, each log.txt the plain build's. Seed 1 to Amy takes the C
player 12 s and the Python 37 s side by side; 36 of each on 32 jobs,
21-31 s and 49-86 s.

Not proved, because no game reached them: make_room and toss_item (a
chest's find with no room, $1704), shut_again and loot's CONTINUE (a
wipe-out among a floor's chests), loot's retreats and its `tired`,
Oputa's top-up, amy's "four in the party already", grind_and_gear's
`spot` and `lowest`. Each is written from the Python line for line; the
stages after (the lab's chests, the Prism dungeons' Nei items) are where
they run, and their checks will show them.

**Proofs** (r172 against r171, this machine, 32 jobs, PROOF_COLOUR=1):
every file the same. 169 PASS of 169, 806 route files; crt 27, sim 40,
patches 14, audio_check, the attract WAV, the Dark Force trial,
probe_check 8 of 8, mods_check 16, no build warnings. Colour SAME (0
routes failed, 0 frames differ). r171 and r170 were taken without
PROOF_COLOUR -- build 171's record said otherwise, now corrected -- so
the colour hashes were set against r169a_c's, the last with them: 169
routes of 169 the same (and every other file but probe_check.txt, which
170's eighth check changed). lang_check, controls_check (both), rumble_check
and script_ends --check PASS; plain `make` builds `ps2` with Q, no
warnings. c_player_check 36 of 36 again on the final probe. Q's: not run
(nothing under src/audio/q/).

`make route_probe` for `--play`; the game (`ps2`, `ps2_headless`) is
unchanged.

**Not seen**: nothing in the window changes.

**Next**: build 173, the lab to the gum in C -- lab_in, lab, lab_b1,
hugh, key_tube, zema, roron, uzo, gum (docs/c_player.md).

## Build 173: COMPUTER PLAYS in C, step 3 -- the lab to the gum (2026-10-30)

The C player's next nine stages (docs/c_player.md, "Step 3"): lab_in
(the lab's door blown, then the grind on its ground floor from Oputa),
lab and lab_b1 (the floors' chests, F2's door, the Recorder), hugh
(Rolf's house for the fifth, the Recorder to the Governor for the Key
Tube), key_tube (the party chosen, the tube's door), zema (Zema's
shelves and its field), roron (every floor's chests, the scooter's
maker), uzo (the jet scooter to Uzo's island and the Maruera tree, the
inventor in Kueri for the gum) and gum (the gum on the ocean floor's
spot, the Underwater Passage, Climatrol's ground floor). What they stand
on, under the Python's names: src/player/party.c, new -- roster,
member_value, choose_party, pickable, regroup_stops, pick_party;
src/player/scooter.c, new -- the scooter's own map, walk_region and
landing_region, ride, ride_ashore, ride_to_face, board_scooter;
dungeon_grind in ground.c; in things.c the level's objects in the
file's order, event_block, facing_cells, search_event, objects (the
probe's live slots) and talk_to; rolf_house_regroup; travel and retreat
while riding (ashore first); and in story.c the nine stages and the
top-ups of lab_in (the lab's ground) and zema (Zema's field). The player
is about 12,450 lines. A stage past gum is `NotPorted: stage climatrol`.

Found on the way, in the C: pick_party's `same_names` compared the
sorted names as 16-byte blocks, past each name's NUL whatever the stack
held, so the party it already had could read as another -- seed 1's gum
went to Paseo to regroup where the Python went to the scooter. It
compares the names now. And c_player_check's mask of the report's
seconds took a stage's name as letters only: lab_b1's row is masked too.

The proof: `c_player_check.py --scratch DIR --to gum`, seeds 1-12 in us,
retranslation and ja, 36 of 36 the Python's, every game through gum:
log.txt line for line (609,960 to 739,265 lines a game, 74,087 fights in
all), every checkpoint byte for byte, story.txt, the summary and the
report but for its seconds. Seed 1 alone first (the same through roron,
then the regroup above), then the 36 on their first run with the fix. 27
tries failed and were retried as the Python's were, 17 of them at uzo,
where Anna comes along at L1 to catch up (wiped out on Uzo's island, on
the water or short of Kueri; seed 6 in retranslation four times
running); the others 3 at arima, 2 at nido, 2 at key_tube, one each at
paseo, lab and gum. The retries ran the new top-ups, Zema's field 18
times and the lab's ground 3. Seeds 13-24 in us as well: 12 of 12. Six
games under ASan and UBSan (seeds 1 and 4 in us, 7 and 10 in
retranslation, 3 and 12 in ja): nothing reported, each log.txt the plain
build's. The 36 on 32 jobs: the C player 53-93 s a game, the Python
125-197 s.

Not proved, because no game reached them: travel while riding, retreat's
"could not get ashore", ride's give-ups, blow_door without the Dynamite
or short of the door, B1's chests left unreached, the maker's second try,
talk_to's "not here", uzo's and gum's own give-ups, board_scooter's "not
known" and "could not reach", dungeon_grind's "could not reach",
pick_party's "could not get to Paseo", the lab's and Zema's grinds giving
up. Build 172's loot CONTINUE, shut_again, loot's retreat on the way and
grind_and_gear's `lowest` ran in these games; make_room, toss_item,
loot's `tired`, Oputa's top-up, amy's "four already" and
grind_and_gear's `spot` still have not.

**Proofs** (r173 against r172, this machine, 32 jobs, PROOF_COLOUR=1):
every file the same. 169 PASS of 169, 806 route files; crt 27, sim 40,
patches 14, audio_check, the attract WAV, the Dark Force trial,
probe_check 8 of 8, mods_check 16, no build warnings; colour SAME, 169
routes' hashes of 169 the same. lang_check, controls_check (both),
rumble_check and script_ends --check PASS; plain `make` builds `ps2`
with Q, no warnings. The sanitized probe has CLANG64's three warnings
from before (route_probe.c, songtext.c, battle.c), none in src/player/.
Q's: not run (nothing under src/audio/q/).

`make route_probe` for `--play`; the game (`ps2`, `ps2_headless`) is
unchanged.

**Not seen**: nothing in the window changes.

**Next**: build 174, Climatrol to the roof in C -- climatrol, dam_party,
cards, dam_grind, dams, roof (docs/c_player.md).

## Build 174: COMPUTER PLAYS in C, step 4 -- Climatrol to the roof (2026-10-30)

The C player's next six stages (docs/c_player.md, "Step 4"): climatrol
(the passage from Kueri by the scooter and the gum each time, four
floors' chests once, the grind on the passage's landing by its pipe with
a CONTINUE of Kueri's save after a wipe-out, F7, Neifirst's two fights
and the death of Nei to Paseo's clone lab), dam_party (the Governor's
flags, Rolf's house for Rudo, Amy and Anna), cards (Ustvestia's Musik in
Oputa, Piata by the scooter, Control Tower F1's keyboard and four
boxes), dam_grind (Piata's field), dams (each dam by the scooter, its
card on the door and the panel, out by Escapipe when hurt; the Army
Eyes, Gaira's prison with RUN alone, home) and roof (the Central
Tower's ROOF, the flight to Dezolis). What they stand on, under the
Python's names: pick_party's `want`; ride_into; stock_mates' `want` and
`trimates`, and stock_item; wander and trip_party public; the run's
unwind drops s.fight_opts as escape_gaira's `finally` does; and in
story.c the six stages with their pieces (to_climatrol,
back_to_climatrol, climatrol_grind, face_neifirst, to_piata,
learn_musik, play_keyboard, open_dam, leave_dam, out_of_dam,
army_eyes_to_gaira, escape_gaira, piata_supply), squeak-by's
CLIMATROL_TARGET and DAM_GRIND_TARGET, and the top-ups of climatrol and
dam_grind. The player is about 13,700 lines. A stage past roof is
`NotPorted: stage skure`.

The proof: `c_player_check.py --scratch DIR --to roof`, seeds 1-12 in us,
retranslation and ja, 36 of 36 the Python's, every game through roof, on
the first run (seed 1 alone, then the 36): log.txt line for line
(1,082,360 to 1,424,841 lines a game, 113,213 fights in all), every
checkpoint byte for byte, story.txt, the summary and the report but for
its seconds. 55 tries failed and were retried as the Python's were: 26
at climatrol (23 lost to Neifirst on F7), 17 at uzo, 3 at arima, 2 each
at nido, key_tube and dams, one each at paseo, lab and gum; the new
top-ups ran before the climatrol and dams retries. Seeds 13-24 in us as
well: 12 of 12. Six games under ASan and UBSan (seeds 1 and 4 in us, 7
and 10 in retranslation, 3 and 12 in ja): nothing reported, each log.txt
the plain build's. The 36 on 32 jobs: the C player 98-176 s a game, the Python 267-482 s.

Reached in these games: climatrol_grind's CONTINUE, its "could not reach
the landing" and its lean; to_climatrol's "no gum"; Neifirst with no
fight offered; retreat's walk out failing; cards gone before a door or
a panel; dams' trips hurt and out by Escapipe, and their CONTINUEs;
leave_dam failing; dam_grind's lean. Not reached, so unproved:
climatrol_grind's "no way back in", "lost" and "stuck", Nei winning, the
debrief not done, learn_musik, play_keyboard, the cards and to_piata
failing, open_dam's "not carried" and its boarding or riding in
failing, a door that stays shut, leave_dam succeeding, the Army Eyes not
carrying the party off, Gaira's panel unreached, dam_grind's "could
not"s, the roof not landing.

**Proofs** (r174 against r173, this machine, 32 jobs, PROOF_COLOUR=1):
every file the same. 169 PASS of 169, 806 route files; crt 27, sim 40,
patches 14, audio_check, the attract WAV, the Dark Force trial,
probe_check 8 of 8, mods_check 16, no build warnings; colour SAME, 169
routes' hashes of 169 the same. lang_check, controls_check (both),
rumble_check and script_ends --check PASS; plain `make` builds `ps2`
with Q, no warnings. The sanitized probe has CLANG64's three warnings
from before, none in src/player/. Q's: not run (nothing under
src/audio/q/).

`make route_probe` for `--play`; the game (`ps2`, `ps2_headless`) is
unchanged.

**Not seen**: nothing in the window changes.

**Next**: build 175, Skure and the Prism's dungeons in C -- skure,
dezolis_towns, esper, dezolis_grind, motavia, shir, menobe, ikuto,
guaron, naval (docs/c_player.md), perhaps in two steps.

## Build 175: COMPUTER PLAYS in C, step 5 -- Skure to Zosa's grind (2026-10-30)

The C player's next four stages (docs/c_player.md, "Step 5"): skure
(Skure B1's chests from where the flight lands, the Magic Cap on Rolf,
B1's musk cats, Zosa's shelves and a grind, B2's chests and cats, the
shelves again in the Mogic Cap, every Zosa Dezolian), dezolis_towns
(Aukba and Ryuon as the standing rules visit a Dezolis town: the Mogic
Cap for the shops, gear, mates, Trimates, Escapipes, the hospital, every
Dezolian, the Magic Cap back on, Data Memory), esper (the door Espers
with Rolf alive, B1's Esper twice and the scene, Lutz, F1's chest and
the Prism, the nearest town) and dezolis_grind (Zosa's field from open
ground by its gate). Skure and the Prism's dungeons were to be one step;
Shir and the four dungeons are the next. What these stand on, under the
Python's names: PIN_HEAD (Rolf's head left alone by plan_gear once a
stage pins it); gear_up's return, the plan it put on, printed as Python
prints the list; ready's `limit`; person_name public; field_spot; and in
story.c wear, fetch_to, cap_for_shops, caps_carried, talk_round,
people_here, zosa_visit, dezolis_town_visit, nearest_town, squeak-by's
DEZOLIS_GRIND and DEZOLIS_GRIND_2, and the top-up of skure and
dezolis_grind (Zosa's field, B1's Magic Cap first when no cap is
carried). The player is about 14,400 lines. A stage past dezolis_grind
is `NotPorted: stage motavia`.

The proof: `c_player_check.py --scratch DIR --to dezolis_grind`, seeds
1-12 in us, retranslation and ja, 36 of 36 the Python's, every game
through dezolis_grind: log.txt line for line (1,445,299 to 1,841,694
lines a game, 129,780 fights in all), every checkpoint byte for byte,
story.txt, the summary and the report but for its seconds. Seed 1 alone
first found one C bug: dezolis_town_visit's "could not reach" line left
the town's name out (level_name hands back its own string, not always
the buffer it is given). Then the 36, each on its first run. 73 tries
failed and were retried as the Python's were: 26 at climatrol, 17 at
uzo, 10 at skure, 6 at esper, 3 at arima, 2 each at nido, key_tube and
dams, one each at paseo, lab, gum, dezolis_towns and dezolis_grind;
Zosa's top-up ran before 18 of the retries. Seeds 13-24 in us: 12 of 12.
Six games under ASan and UBSan (seeds 1 and 4 in us, 7 and 10 in
retranslation, 3 and 12 in ja): nothing reported, each log.txt
the plain build's. The 36 on 16 jobs: the C player
136-201 s a game, the Python 256-329 s.

Reached in these games: the top-up's B1 Magic Cap (ten retries from
story_roof.json), B1 wiped out before the cap, Zosa and the Esper
mansion out of reach after wipe-outs, Rolf dead at the mansion's door
and revive_party at nearest_town's town, a wipe-out between the Prism
and a town, talk_to's "not here" in a round, dezolis_grind short of its
level, cap_for_shops' Mogic Cap and its fall back to the Magic Cap, and
grind_and_gear's `spot` (ported in 171, first reached here). Not
reached, so unproved: fetch_to's GIVE (the caps were in Rolf's own bag
every time), a visit with no cap to wear, the top-up's chest unreached,
Aukba or Ryuon out of reach, B1 out of reach, no scene, no Prism in the
chest.

Two of the Python's own ways, kept as the proof requires: a talk round
names the level it began on, so a talk that walks the party out of town
(Ryuon onto DezolisSkure, seed 1 in retranslation: dezolis_towns' one
failed try) leaves the rest of the round "not here"; and skure's B1
cats are asked for wherever loot's retreat left the party (Zosa, four
times).

**Proofs** (r175 against r174, this machine, 32 jobs, PROOF_COLOUR=1):
every file the same. 169 PASS of 169, 806 route files; crt 27, sim 40,
patches 14, audio_check, the attract WAV, the Dark Force trial,
probe_check 8 of 8, mods_check 16, no build warnings; colour SAME, 169
routes' hashes of 169 the same. lang_check, controls_check (both),
rumble_check and script_ends --check PASS; plain `make` builds `ps2`
with Q, no warnings. The sanitized probe has CLANG64's three warnings
from before, none in src/player/. Q's: not run (nothing under
src/audio/q/).

`make route_probe` for `--play`; the game (`ps2`, `ps2_headless`) is
unchanged.

**Not seen**: nothing in the window changes.

**Next**: build 176, Shir and the Prism's dungeons in C -- motavia,
shir, menobe, ikuto, guaron, naval (docs/c_player.md).

## Build 176: COMPUTER PLAYS in C, step 6 -- Shir and the Prism's dungeons (2026-10-30)

The C player's next six stages (docs/c_player.md, "Step 6"): motavia
(the Prism and the Mogic Cap to Rolf, home to Paseo, Shir from Rolf's
house into the party in place of its weakest and raised to level 10
beside Paseo for the Central Tower's room), shir (Shir's thefts from
Paseo's item store and the tower's room until the party carries a
Visiphone, a Star Mist and a Moon Dew, up to 400 exits; what she stole
out of her bag; the party for Menobe; the roof to Dezolis) and the four
Prism dungeons, menobe, ikuto, guaron and naval: the nearest Dezolis
town, a grind from open ground by its gate until the leader reaches
L32, 34, 36 and 36 (squeak-by's two lower), every floor's chests, back
to the town when worn out, and its two Nei items. What these stand on,
under the Python's names: dungeon_mix and tech_power (fight.c: the
living and robot shares of the enemies ahead, and a member's best attack
technique against them), member_value's tech_power, choose_party's mix,
pick_party's core and catch_up (party.c), held_add (map.c), a new
shir.c (the steal tables, shir_join, raise_member, shir_thefts), and in
story.c keep_on_rolf, empty_shir, worn_out and prism_dungeon. The
dungeons' retries take dezolis_grind's top-up, Zosa's field, as the
Python's do. The player is about 15,400 lines. A stage past naval is
`NotPorted: stage nei_sword`.

One change to the Python, which the proof found. Seed 1 alone, first:
the C saved shir's checkpoint and the Python logged "no save written for
shir (slot 1 is stale)" and ran the stage again. playthrough's
`checkpoint` read `time.time()` before the save and called slot 1 stale
when its time was not later. Windows stamps a file from its coarse clock
(a tick of up to 15.6 ms) and Python 3.14's time.time() is precise, so
a save written within the tick of the read looked older than it. The
C read the coarse clock too and never raced. Both now take slot 1's own
time before the save and call it stale only when the time after is no
later (tools/story.py's checkpoint, run.c's). The genuine stale saves
of builds 171 to 175 (key_tube's and nido's, a save that did not
happen) still are. The tracked games in route_out/playthrough_s*/ were
not made again. COMPUTER PLAYS in the window calls the same checkpoint
(computer_plays.py) but never in the Skure field (its SKURE_FIELD
rule), the one place the checkpoint is save-here's immediate save; its
others walk to a Data Memory first, so the window did not race.

The proof: `c_player_check.py --scratch DIR --to naval`, seeds 1-12 in
us, retranslation and ja, 36 of 36 the Python's, every game through
naval: log.txt line for line (1,732,161 to 2,517,678 lines a game,
145,030 fights in all), every checkpoint byte for byte, story.txt, the
summary and the report but for its seconds. After seed 1's race, the 36
on their first run. 99 tries failed and were retried as the Python's
were: 26 at climatrol, 17 at uzo, 15 at ikuto, 10 at skure, 7 at
motavia, 6 at esper, 3 at arima, 2 each at nido, naval, key_tube and
dams, one each at paseo, menobe, lab, gum, guaron, dezolis_towns and
dezolis_grind. Seeds 13-24 in us: 12 of 12. Six games under ASan and UBSan
(seeds 1 and 4 in us, 7 and 10 in retranslation, 3 and 12 in ja):
nothing reported, each log.txt the plain build's.
The 36 on 16 jobs: the C player 101-144 s a game, the Python 232-324 s.

Reached in these games: Shir not raised to 10 (four games), her bag full
and what she stole handed on (eight), "nobody has room" (nine),
make_room's tosses when a chest finds every bag full (eleven games;
make_room first reached), the shir stage's lean (one), worn_out's
retreat (35 games; loot's `tired` first reached), a door shut again
after a CONTINUE (21), a dungeon short of its Nei items (eight), Zosa,
Aukba or Ryuon out of reach, "lost" in a dungeon's grind, and fetch_to's
GIVE (the Prism from Rudo with Rolf's bag full, one game; unreached in
175). Not reached, so unproved: motavia's way home failing, Shir not in
the roster, raise_member failing outright, the thefts' "her bag is
full" stop, fetch_shir not back, a theft of nothing known, empty_shir's
swap "for room", and the shir stage's "not all stolen yet" without lean.

One of the Python's own ways, kept as the proof requires: raise_member
never revives a dead member, so when Shir dies at a low level beside
Paseo the trips go on without her and motavia fails, to be retried. And
one thing found and left for a build of its own: computer_plays.py's
STORY_ITEMS spells "NeiMet" where the item's name is "Neimet", so the
helmet is never among the marks it reads from a save (`signature`),
nor in menobe's own marks.

**Proofs** (r176 against r175, this machine, 32 jobs, PROOF_COLOUR=1):
every file the same. 169 PASS of 169, 806 route files; crt 27, sim 40,
patches 14, audio_check, the attract WAV, the Dark Force trial,
probe_check 8 of 8, mods_check 16, no build warnings; colour SAME, 169
routes' hashes of 169 the same. lang_check, controls_check (both),
rumble_check and script_ends --check PASS; plain `make` builds `ps2`
with Q, no warnings. The sanitized probe has CLANG64's three warnings
from before, none in src/player/. Q's: not run (nothing under
src/audio/q/). No route calls tools/story.py's checkpoint.

`make route_probe` for `--play`; the game (`ps2`, `ps2_headless`) is
unchanged.

**Not seen**: nothing in the window changes.

**Next**: build 177, Nei's sword to Noah in C -- nei_sword, kain,
kain_grind, noah_grind (docs/c_player.md).

## Build 177: COMPUTER PLAYS in C, step 7 -- the Nei Sword to Noah's grind (2026-10-30)

The C player's next four stages (docs/c_player.md, "Step 7"): nei_sword
(the Esper mansion with the eight Nei items, Lutz's $1690-$1694, F1's
chest and the Nei Sword, a slot made in Rolf's bag, the sword to him and
into his hand, Lutz's NO and the free HEAL, the nearest town), kain
(home to Paseo; whoever the Kain party leaves out hands over what Kain
or Shir could use; Shir's thefts for two Moon Dews and four Star Mists;
Rolf, Rudo, Anna and Kain, geared; the roof back to Zosa), kain_grind
(Kain to L20, squeak-by's 18, on Zosa's field by the party's lowest
level) and noah_grind (Lutz's YES onto NoahGroundF, 20 fights a trip or
until worn out, the Nei Sword out to the mansion, the clone lab or
Lutz's NO, every fifth trip the nearest town, until Rolf is L41 and Kain
L30 -- squeak-by's 35 and 24 -- with a Moon Dew kept off the sword's
holder and Shir sent home for one when there is none). What these stand
on, under the Python's names: revive's `spend_reserve`, sold_anywhere,
char_id and wearable public, one bag's repr (gear.c); shir_bag public
and shir_thefts' tally of what she took (shir.c); in story.c lutz, the
story's own make_room (make_room_keep: a slot made by a hand-over, not
play.py's toss), wear_hand, lutz_heal, keep_from, nei_sword_out,
noah_trip, mansion_mend, sword_holder, reserve_dews, dews_off_holder,
restock_dews, KAIN_TARGET and NOAH_TARGET with squeak-by's, the top-ups
of kain_grind (Zosa's field) and noah_grind (Noah's own: a level past
where Rolf and Kain stand for each three trips, the target passed in,
not the Python's global swapped and restored), and no_kain. run.c takes
playthrough's `--no-kain` (Noah and the bosses with Rolf, Rudo, Amy and
Anna) and its report says ", no Kain"; tools/c_player_check.py passes
`--no-kain` to both players. The player is about 16,500 lines. A stage
past noah_grind is `NotPorted: stage boss_party`.

The proof: `c_player_check.py --scratch DIR --to noah_grind`, seeds 1-12
in us, retranslation and ja, 36 of 36 the Python's, every game through
noah_grind: log.txt line for line (1,931,853 to 2,754,218 lines a game,
154,616 fights in all), every checkpoint byte for byte, story.txt, the
summary and the report but for its seconds. Seed 1 alone first, then
the 36, each on its first run. 112 tries failed and were retried as the
Python's were: 26 at climatrol, 17 at uzo, 15 at ikuto, 12 at
noah_grind, 10 at skure, 7 at motavia, 6 at esper, 3 at arima, 2 each at
nido, key_tube, dams and naval, one each at paseo, lab, gum,
dezolis_towns, dezolis_grind, menobe, guaron and nei_sword; Noah's own
top-up ran before the twelve noah_grind retries. With `--no-kain`:
seeds 1-12 in us, 12 of 12 the Python's (the kain stage
Shir's round alone, the last member but Rolf out for it and back;
kain_grind nothing; Noah until Rolf is L37; the restock's member
keeping consumables, reached only here). Seeds 13-24 in us: 12 of 12. Six games under ASan and UBSan (seeds 1
and 4 in us, 7 and 10 in retranslation, 3 and 12 in ja): nothing
reported, each log.txt the plain build's. The 36 on
16 jobs: the C player 119-181 s a game, the Python 297-409 s.

Reached in these games: make_room_keep's hand-over from Rolf's full bag
(every game), the sword handed to a dead Rolf and nei_sword failing (one
game), keep_from's "no room" and "keeps" (ten games), the Kain stage's
thefts (all 36), the sword's holder dead on Noah (34 times in 19 games),
revived with the reserve or not ("no Nei Sword", six games), Lutz out of
reach from Noah, "could not mend" (ten games), the dews off the holder
(14), the restock home for Shir (30 times in 18 games) and its
empty-handed end (two), empty_shir's swap "for room" and the thefts'
"her bag is full" (both unreached in 176), four wipe-outs on Noah and
their CONTINUE. Not reached, so unproved: Lutz finding nobody,
make_room_keep with nothing to move, nei_sword's mansion out of reach,
"not all eight", "no Nei Sword", the sword in Rolf's second hand, the
kain stage's "could not get home" and "the party is", kain_grind short
of its level and its top-up, the sword not taking the party to the
mansion, noah_grind's lean and "out of trips", the restock with no
Shir, its way home failing, and a wipe-out with no save.

Two of the Python's own ways, kept as the proof requires: fetch_to hands
the Nei Sword to a Rolf dead at the mansion (TECH_IS_DEAD) and the stage
fails, to be retried; and when the sword's holder dies on Noah with no
Moon Dew outside his bag and nobody with Rever, no living member holds
the sword, the party stays on Noah, Lutz is out of reach, and the grind
fails "could not mend". Every one of noah_grind's twelve failed tries
ended "could not mend": eight this way, with Rolf dead, and four with
the party wiped out after a trip.

**Proofs** (r177 against r176, this machine, 32 jobs, PROOF_COLOUR=1):
every file the same. 169 PASS of 169, 806 route files; crt 27, sim 40,
patches 14, audio_check, the attract WAV, the Dark Force trial,
probe_check 8 of 8, mods_check 16, no build warnings; colour SAME, 169
routes' hashes of 169 the same. lang_check, controls_check (both),
rumble_check and script_ends --check PASS; plain `make` builds `ps2`
with Q, no warnings. The sanitized probe has CLANG64's three warnings
from before, none in src/player/. Q's: not run (nothing under
src/audio/q/).

`make route_probe` for `--play`; the game (`ps2`, `ps2_headless`) is
unchanged.

**Not seen**: nothing in the window changes.

**Next**: build 178, the bosses in C -- boss_party, dark_force,
mother_brain -- and with them the C player's whole game
(docs/c_player.md).

## Build 178: COMPUTER PLAYS in C, step 8 -- the bosses and the ending (2026-10-30)

The C player's last three stages (docs/c_player.md, "Step 8"):
boss_party (home to Paseo, keep_from for Amy, Rolf, Rudo, Amy and Kain
geared, the roof back to the mansion's town), dark_force (Ryuon's
Trimates, the walk to Pandora's box on weapons and mates alone with the
dead revived and everyone mended to half after each fight, the box
opened only in shape -- 85% HP, 60% TP -- the fight on fight_smart's
boss policy, the Nei Sword out; four fights a try) and mother_brain (the
same walk to her corridor, YES and NO into her fight, then the
aftermath, YES three times and the ending played to the Sega screen).
With them the C player plays the whole game, and story.STAGES has no
stage left unported. What these stand on, under the Python's names:
tp_line, box_ready, boss_supplies, dark_force_once, mother_brain_once,
answer, BOSS_PARTY (NO_KAIN_PARTY under no_kain) and the two walks' loop
(boss_walk) in story.c; a fight's record keeps its last eight scripts
(fight.c; the Dark Force line prints them); and the unwind after a
harness error puts s.walk_mend back to None, as the walks' `finally`
does. The player is about 17,000 lines.

The proof: `c_player_check.py --scratch DIR --to mother_brain`, seeds
1-12 in us, retranslation and ja, 36 of 36 the Python's, every game
from a new game to the Sega screen: log.txt line for line (2,053,273 to
3,678,220 lines a game, 169,294 fights in all), every checkpoint byte
for byte, story.txt, the summary and the report but for its seconds.
Seed 1 alone first, then the 36, each on its first run. 158 tries
failed and were retried as the Python's were: 37 at dark_force, 26 at
climatrol, 17 at uzo, 15 at ikuto, 12 at noah_grind, 10 at skure, 9 at
mother_brain, 7 at motavia, 6 at esper, 3 at arima, 2 each at nido,
key_tube, dams and naval, one each at paseo, lab, gum, dezolis_towns,
dezolis_grind, menobe, guaron and nei_sword; Noah's top-up before 58 of
the retries. With `--no-kain`, seeds 1-12 in us: 12 of 12, every one to
the Sega screen. Seeds 13-24 in us: 12 of 12. Six games under ASan and
UBSan (seeds 1 and 4 in us, 7 and 10 in retranslation, 3 and 12 in ja):
every one to the Sega screen, log.txt the unsanitized player's, no
report. The 36 on 16 jobs, beside the 24 other games: the C player 216-356
s a game, the Python 386-630 s; seed 1 alone, 113 s and 220 s.

Reached in these games: the box and her corridor out of reach and the
walk again, a second walk to be in shape, a boss lost and the CONTINUE
(176 of the stages' own tries lost at Dark Force, 72 at Mother Brain),
all four of a dark_force try's fights lost (18 games), Dark Force
beaten with no way off Noah (one), a fight in her corridor that was not
hers (ten games), and the ending to the Sega screen in every game. Not
reached, so unproved: the boss party's "could not get home", "keeps"
and "the party is", never in shape at the box or her corridor, no fight
at either, no ending, and the ending stopping short.

Two of the Python's own ways, kept as the proof requires: after Dark
Force, mansion_mend walks a party with anyone dead across DezolisSkure
to the town's clone lab rather than Lutz's HEAL, and six times that
walk wiped them out, the win lost with no save; and mother_brain walks
her corridor with the encounters on, so a fight that is not hers
(formations $D7, $DF, $E3, $E4, $F7) ends the try (15 times).

**Proofs** (r178 against r177, this machine, 32 jobs, PROOF_COLOUR=1):
every file the same. 169 PASS of 169, 806 route files; crt 27, sim 40,
patches 14, audio_check, the attract WAV, the Dark Force trial,
probe_check 8 of 8, mods_check 16, no build warnings; colour SAME, 169
routes' hashes of 169 the same. lang_check, controls_check (both),
rumble_check and script_ends --check PASS; plain `make` builds `ps2`
with Q, no warnings. The sanitized probe has CLANG64's three warnings
from before, none in src/player/. Q's: not run (nothing under
src/audio/q/).

`make route_probe` for `--play`; the game (`ps2`, `ps2_headless`) is
unchanged.

**Not seen**: nothing in the window changes.

**Next**: build 179, the window on the C player -- COMPUTER PLAYS in the
game without the Python, `make computer_check` (a seed through the
window's own path playing the standalone tool's game), and `make dist`
without its Python (docs/c_player.md, "The builds after", 3).

## Build 179: COMPUTER PLAYS in C, step 9 -- the window on the C player (2026-11-01)

COMPUTER PLAYS plays with the C player now (docs/c_player.md, "Step 9";
docs/computer_plays.md, "The C player in the window"): no Python, no
child process, no pipes. computer_plays.py's own loop is
src/player/computer.c -- its arguments, the start from a new game, LAST
RUN or a player's save (stage_of_save: the story flags as the Python's
JSON search sees them, the recruits, the items, NO_PICKUP), squeak-by's
tries and top-ups, the Skure field's stages played again before the next
one's retry, last.json at every save (written as json.dumps writes it),
the report and story.txt, `--classify`. The window (autoplay.c) runs it
on its command thread with a 16 MB stack: each command the player sends
is run there and then by the probe's interpreter under the lock, the log
lines go straight back to its session, and between commands the state is
PIPE, so the frame loop draws while it thinks. The menu closed with the
lamp out makes the player jump to its run's end (player_hand_back),
which writes the report ("finished: handed back"). Its arguments are
parsed before the last run is cleared, so ones that do not parse leave
it. NO TOOLS when assets/data/items.json, routes/saves/story_paseo.json
or src/game/script_ids.h is missing (from the pack or the tree);
PS2_COMPUTER_LOG=FILE writes every interpreter log line;
PS2_COMPUTER_DRIVER=python starts the Python driver as builds 100b-178
did, the reference. To share computer.c's needs: run.c's stage table,
raise_dead, resume_point, stage_begins, party_levels and tick_totals out
of static, its unwind as stage_unwind, the story's lines to an echo file
(driver.log); story_reset (every target squeak-by lowers back, Kain's
and the boss party's choice, the held items and the pinned heads) for a
second run in one game; a trip's save tells last.json ("mid").

`route_probe --computer [--language KEY] ARGS` is the same run on the
window's host (`player_only`: `start new` the console's reset, `start
slot` a CONTINUE through the title), its files cleared as the window
clears them, the probe's in OUT/probe, the language in its own
settings.json; `--classify` touches nothing. `tools/computer_check.py`
(`make computer_check`: seed 1 in us to the ending at FAST, AUTO and AUTO
2X) plays a seed through the headless window's own path -- the Escape
menu, COMPUTER PLAYS from NEW GAME at a SPEED -- against it: every
command at the same tick, story.txt, the report less its seconds,
last.json, every checkpoint and slot. The Makefile links the player into
every game target. `make dist` carries no Python now (make_dist.py's
download, its licence and --no-python gone), and the pack no Python
driver (make_pack.py: routes/saves/ and script_ids.h stay).

The proof, `tools/computer_check.py`: seeds 1-12 in us, retranslation
and ja at FAST, each through the window with the C player and with the
Python driver: 72 of 72 runs the tool's game, every command at the same
tick to the end and every file byte for byte. The window writes a shot as
"(not drawn here)", which the check reads as the probe's line, and the
Python on Windows (the PATH's 3.8 here) writes its paths with
backslashes, which the check takes as slashes for the Python's runs
only. 33 of the 36 games reach the ending; seeds 7 and 10 in us and 11
in ja stop at Skure, the Python's driver exactly as the C player (below).
AUTO and AUTO 2X, seeds 1 and 4 in the three languages: 6 of 6, every
one to the ending. `make computer_check`: 1 of 1. Through the window's
other ways in: LAST RUN after a run to Paseo, SLOT 1 from Paseo's
checkpoint and from Gaira's save with Anna dead (the slot untouched),
`--fail-once arima` at FAST and AUTO on four games (the retry's CONTINUE
through the title), with `--no-swap` too, the Escape menu opened and shut
mid-run at FAST and at AUTO -- each the tool's game exactly, with both
drivers; a hand-back and QUIT mid-run, with both (exit 0, the report
"finished: handed back", the Python's "(EOFError)"); NO TOOLS with
story_paseo.json taken away; arguments that do not parse and an unknown
`--to` (the usage on stderr, the last run kept). `--classify`: 173 saves
(routes/saves/ and three games' checkpoints) the Python's, nothing
written. The packed game (`make dist_headless`) from an empty folder:
seed 1 to Arima, the tree's game byte for byte; `make dist` builds
(dist/ps2-windows.zip 12.6 MB, no python/). Six runs of `route_probe
--computer` under ASan and UBSan (seeds 1 us, 5 retranslation, 9 ja to
the ending; the two `--fail-once` games; Gaira's slot): clean, the plain
build's logs and stories. c_player_check, seeds 2 and 7 in us and ja to
mother_brain: 4 of 4 (run.c changed). Times, 48 runs at once on 16 jobs:
the window with the C player 54-126 s a whole game at FAST, with the
Python 144-323 s, the tool 91-194 s; AUTO 636-800 s, AUTO 2X 380-494 s.

**Found, not changed**: the three games that stop at Skure. Skure's
first try wipes out on SkureB2 after "no path on SkureB2 from (79, 52)"
to the stairs, the CONTINUE is Paseo's, and every retry -- roof again
from dams' checkpoint, since the Skure field gives none -- plays the same
try again, squeak-by's top-up gaining no level, six times. The Python
driver plays the same games, so it is the Python harness's, come in
after build 118 (when all twelve seeds, in us then, finished); playthrough.py, which saves
in the Skure field, finishes all 36 (Build 178). And the NeiMet spelling
in STORY_ITEMS (items.json's Neimet), mirrored by the C as before.

**Proofs** (r179 against r178, this machine, 32 jobs, PROOF_COLOUR=1):
every file the same. 169 PASS of 169, 806 route files; crt 27, sim 40,
patches 14, audio_check, the attract WAV, the Dark Force trial,
probe_check 8 of 8, mods_check 16, no build warnings; colour SAME, 169
routes' hashes of 169 the same. lang_check, controls_check (both),
rumble_check and script_ends --check PASS; `make -j32 all headless
route_probe` (`ps2` with Q) no warnings. The sanitized probe has
CLANG64's four warnings from older code (songtext.c twice, battle.c,
route_probe.c's g_cap_off; build 178 counted three), none in
src/player/. Q's: not run (nothing under src/audio/q/).

`make` (the game: `ps2`, `ps2_headless`) and `make route_probe`; `make
dist` for the package.

**Not seen**: the real window. COMPUTER PLAYS now plays with the C
player there, so for the owner at the machine: NEW GAME at each
SPEED (NORMAL, AUTO, 2X, AUTO 2X, FAST) -- the pacing, the music at the
beats, AUTO's grind; the Escape menu over the computer's game and the
hand-back with the lamp out; SLOT and LAST RUN; QUIT while it plays; and
`make dist`'s ps2.exe from the zip on a machine with no Python. Not run
here: the headless game under ASan (only `route_probe --computer`), and
a C player that thinks without end, which the window cannot stop (the
Python driver could be killed after a second): the window waits for it.

**Next**: the Skure failures in COMPUTER PLAYS (both drivers) -- a save
in the Skure field the way a player makes one, or a retry that does
not replay the same try -- and whether the Python driver and
playthrough.py stay as references now that the window needs neither.

## Build 180: COMPUTER PLAYS's Skure retries, the Neimet (2026-11-07)

The three games build 179 found stopping at Skure (seeds 7 and 10 in us,
11 in ja) reach the ending now, with both drivers.

**Why they stopped.** The roof stage ends in the Skure field, where a
player cannot save yet (the Visiphone comes from Shir's thefts, after
Skure), so COMPUTER PLAYS takes no checkpoint there and a retry of Skure
CONTINUEs dams' checkpoint, in Paseo, and flies the roof again. But the
retry's squeak-by top-up ran first: Zosa's grind (top_up_zosa), on
Dezolis, from Paseo -- "no way from Paseo to SkureB1", "could not reach
Zosa" -- so it gained nothing, and every try was the first try's party
at L23 again. Most games got through on a later try's luck anyway; these
three lost all six. playthrough.py never met it: its `save-here` makes a
checkpoint in the Skure field, so its top-up starts there. menobe after
shir is the same case (shir ends in the Skure field too; menobe's top-up
is dezolis_grind's, Zosa's again).

**The change** (tools/computer_plays.py and src/player/computer.c, the
same): a retry plays the stages lost since the checkpoint first, then
the top-up, so the top-up starts in the Skure field as playthrough's
does. A top-up wiped out CONTINUEs the checkpoint and plays them again
(`.../again` in the pauses' key). Nothing else moves: a game that
never retries skure or menobe plays as it did, pause for pause (the
pauses' stream is only drawn afresh after a replay).

And the helmet: STORY_ITEMS spelt it "NeiMet", items.json's "Neimet",
so no save ever showed it and menobe's signature (its checkpoint the
first with the helmet) lacked it. Spelt as the item now, in both.
`--classify` on 127 saves (routes/saves/ and the three games'
checkpoints): every stage the one build 179 gave, the C the Python's.

And tools/computer_check.py and c_player_check.py start the scratch
tree's own programs by their full paths: on Windows a relative program
is looked for from the parent's folder, not `cwd`, so the checks ran the
tree's ps2_headless and route_probe (the same files) and held them open,
and a build in the tree could not write route_probe.exe while one ran
(this build's first proof run stopped on it).

**The games** (`route_probe --computer`, seeds 1-12 in the three
languages to the ending, build 179's tool from `git archive` against
this one's): 36 of 36 to the ending, 33 before. 12 games' stories are
179's line for line; the other 24 part exactly at their first retry of
skure (23 games) or menobe (seed 8 in us), the new order's first line.
Skure's first try still fails in 22 games; their retries now come after
Zosa's grind, and Rolf leaves Skure at L25-28 (the stuck games' L23 on
every try): 20 through on the second try, 2 on the third (14 games
through on the first). Skure's failed tries
60 -> 24, every stage's 184 -> 162; menobe's 2 -> 4. The 33 games both
finish: 401.9 M ticks against 406.9 M. A top-up wiped out and the roof
flown again (`/again`): seed 9 in us and seed 6 in retranslation, once
each.

**The proof** (`tools/computer_check.py`, seeds 1-12 in us, retranslation
and ja at FAST, the window with the C player and with the Python driver
against the tool): 36 of 36, all 72 window runs the tool's game to the
ending -- every command at the same tick, story.txt, the report less its
seconds, last.json, every checkpoint and slot. The three games alone
first: 3 of 3. The window with the C player 104-330 s a whole game, with
the Python 261-711 s, the tool 166-279 s (16 jobs, beside the 72 runs of
the old-and-new comparison). c_player_check, seeds 2 and 7 in us and ja
to mother_brain, its probe now by full path: 4 of 4 (nothing it plays
changed). `make computer_check` (seed 1 in us at FAST, AUTO and AUTO
2X): 1 of 1.

**Proofs** (r180 against r179, this machine, 32 jobs, PROOF_COLOUR=1):
every file the same. 169 PASS of 169, 806 route files; crt 27, sim 40,
patches 14, audio_check, the attract WAV, the Dark Force trial,
probe_check 8 of 8, mods_check 16, no build warnings; colour SAME, 169
routes' hashes the same. Q's: not run (nothing under src/audio/q/).

`make` (the game: `ps2`, `ps2_headless`) and `make route_probe`.

**Not seen**: the real window (as after 179; nothing in it changes but
the computer's game after a failed Skure or Menobe try).

**Next**: The owner at the machine, as after 179; whether the Python
driver and playthrough.py stay as references.

## Build 180a: the Python references kept (2026-11-07)

The owner, after 180, asked whether the Python driver and
playthrough.py stay as references now that the window needs neither:
"keep both as references".

So the computer's game keeps two players: the C player (src/player/),
which the window runs, and the Python -- tools/computer_plays.py, which
the window still starts under `PS2_COMPUTER_DRIVER=python`, playing
tools/playthrough.py's stages. A change to how the computer plays goes
into both in the same build, as build 180's retry order did, and
`tools/c_player_check.py` (the C player against playthrough.py) and
`tools/computer_check.py --python` (the window with each driver against
`route_probe --computer`) hold them to the same commands at the same
ticks. They stay the only check on the C player made apart from it;
playthrough.py also carries story.py, probe_check.py, the grind tools
and tools/measure/'s whole-game scripts. The rule is in CLAUDE.md
(Proofs); docs/c_player.md and docs/computer_plays.md say so where
they name the reference.

Docs only: nothing under src/ or tools/ changed, so no proofs were run
and no make target is needed.

**Next**: The owner at the machine, the window checks listed after
179.

## Build 180b: the C player builds on Linux and macOS (2026-11-07)

The owner, `make` on Linux (Arch, GCC 16): `ps2` stopped at
src/player/run.c -- clock_gettime and CLOCK_REALTIME undeclared, struct
stat with no st_mtim -- and GCC warned at src/player/gear.c's
hands_score1 (-Wformat-truncation: `"%s"` of a name into 24 bytes).
"you trying to go windows centric on me? you should know better"

**Why.** The C player (builds 171-180) was compiled only on the Windows
desktop, where run.c's two clocks -- pl_now, the time now, and pl_mtime,
a file's time, both for the checkpoints' "written since" -- take their
`_WIN32` side. Their POSIX side had never been built. Under `-std=c11`
glibc declares nothing of POSIX's past the C library unless a file asks:
home.c, probe.c and autoplay.c ask with `_POSIX_C_SOURCE 200809L`;
run.c did not. gear.c's warning is GCC 16's on Linux only: MinGW's GCC
16.2 here never gave it, nor does GCC 13.

**The change.**
- run.c: `_POSIX_C_SOURCE 200809L`, and `_DARWIN_C_SOURCE` for macOS,
  whose struct stat names the time st_mtimespec (pl_mtime's `__APPLE__`
  branch).
- gear.c: hands_score1 scores its one item from the item itself, not
  through a copy of its name into a `[24]` for hands_score. The longest
  item name is 10 characters (MARUERAGUM), so the score is the same.

**Checked.** On a Linux machine with GCC 13.3 (glibc, Ubuntu 24.04, no
SDL3), `-std=c11 -Wall -Wextra -Wpedantic -O2`: 180a's src/player gives
run.c's errors as the owner's; this build's `make -k Q=0 headless
route_probe` with `-Werror=implicit-function-declaration` builds both,
no warnings. Windows: `make ps2 headless route_probe`, no warnings.
On MinGW `_POSIX_C_SOURCE` can turn run.c's printing over to MinGW's own
printf, so `python3 tools/c_player_check.py --to mother_brain`, seeds
1-12 in the three languages: 36 of 36 the Python's games, line for line.
Not run: the proofs and `make computer_check` (nothing outside
src/player/ changed, and c_player_check plays every game run.c and
gear.c take part in).

**Not compiled**: macOS (no Mac here: pl_mtime's `__APPLE__` branch is
the SDK's name, unbuilt); GCC 16 on Linux; `ps2` itself on Linux (SDL3:
platform_sdl3.c and crt_gpu.c have not changed since 170, and
The owner's build reached them). The rule that every build builds on
all three is in CLAUDE.md (Proofs).

`make` (the game: `ps2`, `ps2_headless`) and `make route_probe`.

**Next**: The owner, `make` again on the Linux machine; then the window
checks listed after 179.

## Build 181: how much of the ROM is left, counted again (2026-11-09)

The owner: "do the new count of what's left of the ROM" (README, "Not
done yet": not measured since the early count of ~18k instruction lines,
which had no method recorded).

**The count.** tools/rom_left.py reads ps2.asm as AS does: every 68000
instruction line, the nameless labels (`++` is the second `+` on), control
followed from the vectors and from every code label data or code takes the
address of, branch tables entered at every line. It splits the code into
routines, each an entry up to the next, and holds each against src/: a
routine is cited when src/ names one of its code labels, or cites a
`ps2.asm:N` line inside it (smps.c's way), on a line that does not say
"not ported". 19,169 instruction lines in 1,055 routines; control reaches
all but 34. src/ cites 841 routines, 16,682 lines.

**The reading.** The 214 routines src/ does not cite (2,487 lines) were
read in full, five regions by five readers, each routine against the C it
maps to, and the seven item actions behind `bne ++` once the tool learned
`++`. Every one has a line in tools/rom_left_review.tsv: 160 PORTED (1,816
lines; a jump table's cases ported as a switch under its dispatcher's
name, every window under its WIN_* id, smps.c's snake_case names), 41
REPLACED (616: the VDP, DMA and scroll-edge redraws, the joypad, the
decompressors at extraction, the battery RAM, the Z80's bus), 13 never run
(55: the crash trap, the line interrupt, objects 9 and $34-$36, the unused
Nemesis writers), none MISSING. Inside the cited routines, 6,524 lines sit
in stretches whose own label src/ never names; 40 drawn at random by size
(506 lines) were all in the port (348 ported, 158 replaced), which bounds
the skipped share under 7% at 95%. The early "~18k" was most likely the
whole: 19,169 is all of the 68000 code by this count. docs/rom_left.md has
the table, the method, the limits and every finding.

**What is left.**
- The dead still walk in the party's line. RenderCharSprites' per-slot step
  (loc_11868) skips a member at 0 HP and packs the next living one in; the
  port's map draw takes `party_member_id[i]` whatever the HP. Seen in the
  port: paseo_rolf_dead's 02_still_outside.png, Rolf at 0 HP at the door
  where the ROM would show Nei. Not seen in an emulator. Pictures only.
- The music fade-out's overrun is not reproduced. FadeOutMusic copies
  FM3's chord voice into `count` tracks from the one after FM3, one past
  the chord, onto the first effect track; smps.c stops at the chord. It
  could be heard at Gaira's control panel, once a game; not measured.
- Read and the same: the Magic Cap's mask (the head slot holds the plain
  id), the EXP share's overflow, window $54's palette line (nothing opens
  it). By design: the effect art placed at once, no mirror for a save.
- Not settled: sound commands $E2 and $E3 past PlaySoundID's two-entry
  table (nothing queues them by number; the enemies' sound bytes not
  checked).

**Stale comments.** Four in src/ still called ported things not ported:
game.c's window reveal (the crawl's box opens by MSG_STEP, window.c's
reveal does the rest), BuildingScreen's four special cases (all ported
since builds 84-87) and Tyler's spaceship's pictures (build 82); and
script.h's substitutions (window.c resolves them). Comments only.

**Checked.** `make -j32 headless route_probe` and `make -j32 routes`: exit
0, 169 PASS lines. The comments change no code, so the proofs and the
other checks were not run, and nothing needs compiling against glibc.
`python3 tools/rom_left.py` exits 0 (the review in step with src/), about
two seconds.

Nothing under src/ changed but comments: no make target is needed.
route_out/rom_left/ is kept in the tree (.gitignore), the count docs cite.

**Next**: the dead out of the walking line (a route with a dead member
that fails on 181 and passes after); the owner's call on the fade-out's
overrun; then the window checks listed after 179, and `make` again on the
Linux machine.

## Build 182: the dead out of the walking line (2026-11-09)

The owner: "do build 182, take the dead out of the walking line" (build
181's count, docs/rom_left.md, "What is left").

**The ROM.** RenderCharSprites (ps2.asm:26657) runs once each pass of the
level loop and calls its per-slot step (loc_11868) for the leader's slot
and the three followers', with a count of party_members_num + 1 ids. Each
call takes the next id of party_member_id and, if that character's HP is
0, goes round again with the next -- so the living close up behind the
dead -- and when the ids run out it hides the slot (2(a0) = 2, which
RunObject reads as "leave out of the sprite table"). The slots' positions
and frames are their own: only who is drawn in them moves up. Nothing in
the leader's or the followers' object code writes that byte back.

**What the port did.** render_level_walk drew `party_member_id[i]` in
slot i whatever the HP: with Rolf dead, Rolf still led the line and all
four walked.

**The change.** game.c's line_pack does loc_11868's step over a list of
ids; the walking line's draw takes slot i's member from it and draws only
as many slots as there are living members. The ending's cast (loc_E398)
goes through it too: loc_1183A packs its seven slots from the leader's,
where the port had always drawn `party_member_id[0]` in the leader's slot
and packed only the six; and from then on RenderCharSprites packs the
first four ids every field, so each dead one among them hides one of
those four slots, from the last. The aftermath has just brought the party to full HP,
so only a party of under four with someone dead at home could show
either. (With someone dead the ROM's seventh slot would read one word
past the list; the port draws nobody there.)

**map_wipeout.** Build 157c's route pinned Rolf on the map through the
game over erase (his blue at 54,89, sixteen frames in), but the port then
drew everyone. The ROM hides him the frame after the damage floor takes
his last point, before "ROLF is dead!": no party sprite is left in a
wipe-out. Its checks now see the floor there, in the message's frame and
in the erase; the erase's end and the message are byte for byte as
before.

**Routes.** walk_line_dead (new): paseo_rolf_dead's save (Rolf at 0 HP,
Nei, Rudo, Amy), Nei's hair ($E0A) leading at Rolf's door; then a step
down and four left, the line spread: Nei, Rudo ($2EE), Amy ($00E), and the
ground ($640) where a fourth would walk. On 181 (its sources built on
their own) it fails at the door, Rolf's blue ($C00), and each of the four
line checks fails alone -- the fourth finds Amy. map_wipeout fails on 181
at its new check.

**Checked.** `make headless route_probe`, no warnings; `make routes`:
170 PASS (map_wipeout once more after its edit). Every route on 181's
probe beside 182's: 811 files, 797 the same; the 14 that differ are the
five routes that show someone at 0 HP on the map -- walk_line_dead and
map_wipeout (their logs and summaries too), paseo_rolf_dead's two shots,
esper_door_rolf_dead's and gaira_dead_member's prison shot. colour_check
on those five: SAME. probe_check PASS. Against glibc (GCC 13.3, Ubuntu
24.04): `make -k Q=0 headless route_probe` with
`-Werror=implicit-function-declaration`, no warnings. `python3
tools/rom_left.py` exit 0 once loc_1183A's line left the review (its
comment now cites it: 842 routines and 16,700 lines cited). Not run: the
proofs, c_player_check and computer_check -- only what the map draws
changed, which neither player reads. Not compiled: macOS, GCC 16, `ps2`
on Linux. Not seen in an emulator: the ROM side is from reading.

`make` (the game: `ps2`, `ps2_headless`) and `make route_probe`.

**Next**: The owner's call on the fade-out's overrun (docs/rom_left.md,
"What is left"); then `make` again on the Linux machine and the window
checks listed after 179.

## Build 183: the music fade-out's overrun, measured and ported (2026-11-10)

The owner, to build 182's "Next" (his call on the fade-out's overrun)
and the advice with it -- measure it with a register trace, port it if it
changes anything; trim NOTES_next_context.md; bring TODO.md up to date --
"do all three?"

**The ROM** (read in full: FadeOutMusic, DoFading, SendFMFreq_A,
RefreshVolume, SetFMIns, PlaySFX, cfE5_ChordMode, cfEF_SetFMIns,
RestoreBGMChn, UpdateSoundQueue, PlaySnd_Command; and the three places
that send the fade, SegaScreen, the ending at ps2.asm:10936,
Building_GairaControlPanel). FadeOutMusic (ps2.asm:92206) takes the chord
count from FM3's track, subtracts 1 for `dbf`, and copies the track's
algorithm and four level bytes into that many + 1 tracks from
music_tracks + $C0 -- the track after FM3's. The chord's own loops
(SendFMFreq_A, the key routines, cfEF) run the same count from FM3's track
itself. So the copy is one track long: with the chord of three every one
of the 21 songs that has a chord uses, tracks 4, 5 and 6, and track 6 is
the first effect track, where PlaySFX puts an effect on FM3. The copies
to 4 and 5 are what the loop is for; the copy to 6 is the overrun.
docs/rom_left.md had that from reading; nothing had measured it.

**The measurement** (a scratch copy of the tree in MSYS2's /tmp/b183,
never the repository: smps.c with the ROM's loop behind an environment
variable, a log of every fade and of every volume refresh from a
written-over track, a log of every chip write, and the probe's `sound
render` written out).
- Where. The 170 routes: 42 fades (35 at power-on, 2 at game overs, 3
  around the ending, 2 at Gaira's control panel -- army_eye_capture and
  gaira_dead_member), and only Gaira's two with an effect track in use.
  Twelve whole games by the C player (`route_probe --play`, seeds 1-12 at
  the probe's pace, eleven to the ending): 147 fades -- 12 at power-on,
  102 at game overs under Over, 11 before the ending's white, 11 at the
  SEGA screen after it, 11 at Gaira -- and in each game that got to Gaira
  that one fade alone had an effect on the first effect track: the
  explosion ($A3), started 22 frames before. Four volume refreshes a game
  changed, nothing else anywhere.
- What. The explosion's FM3 part (instrument explosion: algorithm 5,
  feedback 6, operator levels 1, 0, 0, 0; FM3's special mode) steps its
  volume up 8 at its frames 17, 25, 33, 41 and 49. From frame 22 its track
  holds Mystery's chord voice, and Mystery has two: pleasure_chords_3
  (algorithm 5, levels $1F $12 $27 $04) for frames 0-1535 of its 2304,
  mystery_chords (algorithm 0, levels $20 $11 $21 $00) for 1536-2303.
  Under the first the three carriers' registers take volume + $27, $12,
  $04 where they took the volume alone: twelve writes differ (`$46`: $37
  $3F $47 $4F for $10 $18 $20 $28; `$4A`: $22 $2A $32 $3A; `$4E`: $14 $1C
  $24 $2C). Under the second only operator 4 is a carrier by the copied
  algorithm, so `$4E` takes the volume as before and `$46` and `$4A` are
  not written again: eight writes fewer, two carriers left at the level
  of the step at frame 17. Ten of the eleven games met the first voice,
  one the second.
- How much (the whole mix through the game's mixer, rendered from the
  song's start so the chip holds the music's instruments; the 24 ticks
  from the first changed write, the port's old loop against the ROM's):
  the first voice -23.5 to -26.2 dBFS, 2.7 dB quieter, the difference
  signal -30.5 dBFS (2.8 dB on the long route, VOICES EXTRA and LIMITED
  alike); the second voice -23.7 to -19.1 dBFS, 4.6 dB louder, up to 9.9
  dB in a 4-tick window, the difference -19.4 dBFS. From the explosion's
  end on the two renders are within 0.1 dB, the difference under -60 dBFS
  and falling.
  A first render started `sound render` at the panel and was thrown away:
  the probe drops the chip's queue until then, so the music's channels had
  no instruments.
- So: on the Mega Drive the explosion's last 0.4 s is a little quieter two
  times in three and rings on one time in three. It changes the sound,
  and the owner's rule was to port it if it does.

**The change.** smps.c's fade_out_music copies `count` times from the
track after FM3's, as the ROM does, through track_byte (a count past the
tracks lands in the RAM after them, as on the hardware, and no further;
the old loop indexed the tracks unchecked). audio.[ch]:
audio_register_written, a YM2612 register as the sequencer last wrote it
(the game thread's mirror). The probe: `expect sound-register PART $REG
$VALUE` (docs/harness.md). Nothing else under src/.

**Routes** (new, both from `start level` on Gaira's cell below the panel,
540 and 1,740 ticks). fade_out_overrun: FM3's three level registers hold
$08 after the explosion's step at frame 17, then $37 $22 $14, $3F $2A $1C
and $47 $32 $24 after the steps at 25, 33 and 41.
fade_out_overrun_second_chord: 1,200 frames on Gaira first; $08 $08 $08,
then `$4E` alone stepping, $10, $18, $20. On 182's fade (the same probe,
smps.c as it was) each passes its three checks before the fade and fails
at the first after it: "the sequencer last wrote $10 there" at tick 524
and at tick 1724.

**Checked** (the Windows desktop of CLAUDE.md's Machine section; baselines
r182 and q182 taken there before the first edit).
- The sequencer's whole write stream through the scene, count and hash
  (`expect sound-writes`), build 183 against the measuring build with the
  ROM's loop: 4,250 writes $8E5A8DE4 under the first voice, 8,395 writes
  $C3E46BEB under the second, the same in both; 182 and the measuring
  build's old loop the same as each other too (4,250 $20549BC0; 8,403
  $4C1BB6D3). So the measurements above are build 183's.
- `proof.sh r183 32` with PROOF_COLOUR=1 against r182: 172 PASS; every
  one of 182's 811 route files the same, the two routes' four files new;
  the attract WAV, the Dark Force trial, audio_check, crt 27, sim 40,
  patches 14, probe_check 8, mods_check 16 the same; colour SAME.
- Q (`make ps2_headless ps2_headless_q q_demo fm_probe drum_hits`, then
  `qproof.sh q183` against q182): the 27 files the same but attract_q.log's
  line naming its own folder -- audio_check_q, the Q attract WAV, the trim
  pass, drum_hits, every fm_probe file.
- Twelve whole games by the C player (`route_probe --play`, seeds 1-12,
  the probe's pace) on 182's probe and on 183's, each in a scratch tree of
  its own: 729 files, every one the same -- the probes' logs line for line
  (every command at the same tick), the stories, checkpoints and shots,
  the reports less their seconds. Eleven to the ending (17.3 to 21.1
  million ticks), seed 6 to its give-up at uzo (below). The game is the
  same; only the chip's writes at Gaira's panel moved.
- `python3 tools/rumble_check.py`: PASS (172 routes and 160 fuzz runs,
  1,102,566 ticks; no rule broken, none late, every kind felt). The rumble
  follows an effect by its track's volume byte, which the copy leaves
  alone.
- Against glibc (the server: GCC 13.3, Ubuntu 24.04): `make -k Q=0
  headless route_probe` with `-Werror=implicit-function-declaration`, no
  warnings.
- `python3 tools/rom_left.py` exit 0, once FadeOutMusic, DoFading and
  SendFMFreq_A left the review (the fade's comment cites them): 845
  routines and 16,745 lines cited, 210 and 2,424 not.
- Not run: c_player_check and computer_check (each holds two players to
  one probe, so neither can see a change in the probe; the whole games
  above are the check that the game is the same), controls_check,
  lang_check, script_ends (nothing of theirs changed), crt_gpu_check and
  the window (The owner at the machine). Not compiled: macOS, GCC 16,
  `ps2` on Linux. Not heard on a Mega Drive or an emulator: the ROM side
  is from reading, the port's from the chip's writes.

**Docs.** docs/audio.md, "The fade-out's overrun" (and its volume
paragraph: the fade steps every 5 ticks, 39 times, and silences at the
199th tick -- it said every 4 for 40); docs/rom_left.md; docs/harness.md
(the command, the two routes); README's "Not done yet" (the item moved to
"Done since"); CLAUDE.md's counts. NOTES_next_context.md trimmed, at
The owner's word: 3,647 lines of every build's summary since 68 and
every "Next" list since 92 down to his standing notes, the entries of
builds 173 on and one "Next"; the file as it was is `git show
244892c:ps2/NOTES_next_context.md`. Outside the repository: TODO.md's
Phantasy Star II list rewritten (it stopped at build 161), and four FLACs
of the scene, both voices on 182 and 183, in `listen/` on that machine.

**Seen on the way, not changed.**
- `route_probe --play --seed 6` at the probe's pace gives up at uzo: six
  tries, each wiped out on Uzo's island after the Maruera leaf (Rolf L16).
  The same story, byte for byte, on build 181's probe and 182's, and on
  183's: older than this build, and the harness's loss, not the port's.
  c_player_check and computer_check play at the watch pace, where seed 6
  reaches the ending (build 180).
- songtext.c takes `chord N` up to 255 from a song's or an effect's text,
  and the chord loops in smps.c (the keys, the frequencies, cfE5, cfEF)
  index `ti + i` unchecked: a mod's song with a chord past the tracks
  would read and write outside the track array. The game's own songs are
  all chords of three on FM3. For the owner: refuse a chord that runs
  past the music tracks in songtext, if wanted.

`make` (the game: `ps2`, `ps2_headless`) and `make route_probe`.

**Next**: nothing is queued; NOTES_next_context.md's "Next" lists what
waits for the owner at the machine, on the other machines and in an
emulator.

## Build 184: mods' chords past the chip (2026-11-10)

The owner, to build 183's note that a mod's song could take a chord past
the track array ("say if you want oversized chords refused"): "yes lets
allow larger chords on mods". Asked how large, and where the notes past
the Mega Drive's chip go, he chose "Past the chip": any size up to the 16
notes a tracker cell holds; the notes past the chip's five music channels
on voices of their own -- extra chips on the chip synth, extra voices in
Q, made only when a mod's chord needs them; the game's own music
unchanged.

**The chord as the driver has it** (smps.c's ports of cfE5_ChordMode,
DoNoteOn_A, DoNoteOff_A, SendFMFreq_A, GetFMFreq, cfEF_SetFMIns, the pan,
FinishTrkUpdate, FadeOutMusic, DoFading and cfE8_MusPause, each read in
full before the change): N notes play one stream on the lead's track
block and the N-1 after it, whatever they hold. The game's chords are of
three on FM3 in songs of four tracks, so FM4's and FM5's blocks, which
PlayMusic gives their channels.

**Before.** Build 183's smps_trace, built from the commit in MSYS2's
/tmp/b183, on Phantasy with its chord made six notes and sixteen (each
note's octaves added; the first 3,000 frames): six put 942 more writes on
the chip than the song's own, its parts on the effects' blocks and their
channels; sixteen 4,082 more -- 936 of them key writes on FM1, the
melody's channel (a zeroed block's channel id is FM1's), others on FM6,
the drums' -- and its parts past the tenth block wrote outside the track
array.

**The change** (docs/audio.md, "Chords past the chip").
- smps.h/smps.c: a chord is at most SMPS_CHORD_MAX, 16 notes. A lead's
  parts are given at cfE5 (give_parts): first the music tracks after it
  that the song's header leaves out and no other chord holds, then chord
  voices -- track blocks of the port's own (Smps.voices, numbered from
  SMPS_TRACKS), whose writes go to the hooks alone, never to the chip or
  its write hash. Every chord loop goes through part_track; the fade steps
  the music's voices, its copy goes to the parts as they are (so the
  game's chord of three still copies one block past, 183's overrun); the
  pause's walk turns a lead's voices down and up with it; a song's start
  and cfE0 give the music's parts back and silence its voices, PlaySFX an
  effect track's. smps_load walks every stream as the sequencer reads it
  and counts the voices the music can need (SmpsData.chord_music,
  chord_effects): 0 and 0 for the game's.
- songtext.c and tools/song_text.py: `chord N` up to 16 (it took 255);
  the tracker's cell already held 16 notes.
- synth.[ch], fm.[ch]: SynthBackend.set_chord_voices. The chip: a chord
  chip (a Nuked-OPN2 of its own) for every six voices, the music's apart
  from the effects'; fm_settle primes a new chip's DC blocker, fm_idle
  lets a silent chip go unrendered. Q (q_synth.cpp): its virtual channels
  a vector, a channel and a voice for each chord voice, the PM pool grown
  by as many; chord_write_now.
- audio.[ch]: each voice's writes kept (AudioVoiceShadow, 40 bytes) and
  replayed on a switch of synth and after FAST; set_chord_voices from
  audio_load_music.
- tools/smps_trace.c: `--chords`, `--voices`, `--play ID@FRAME`,
  `--resume FRAME`. tools/chords_check.py (new; docs/harness.md).

**Checked** (the Windows desktop; baselines r183 and q183).
- `python3 tools/chords_check.py`: 7 of 7. The game: 0 and 0 voices.
  chord6 and chord16: 3 and 13 voices; the 8,021 chip, DAC and PSG
  writes of 3,000 frames the song's own, line for line; each voice struck with the lead,
  24 times, its first pitch its added note; the fade stepping each voice
  41 times to FM5's 39, its end keying them off; the pause turning each
  voice to FM3's levels ($48=$EB $4C=$E3) at frame 628 and the unpause
  back ($24 $1C) at 728. chord17: both refusals, file and line named.
  The level-change effect made a chord of three: 2 effect voices, each
  struck four times. Heard through the headless game, chord6 against the
  game's own: the same to the bit until the first chord (frame 344); from
  it their difference is the three added notes and their overtones,
  99.9% of its energy on the chip (-30.7 dBFS) and 100.0% on Q (-32.7).
- The voices after a switch (a scratch program on audio.c, MSYS2's
  /tmp/cc184/replay, not in the tree: Phantasy with chord6 from its start,
  a switch to the same synth at tick 60 inside the held first chord, the
  voices' records kept or wiped just before it): kept less wiped is the
  added notes, 99.9%, at -30.9 dBFS on the chip and -33.1 on Q, against
  -31.4 and -33.7 for the voices with no switch. FAST's resync replays the
  same way.
- tracker.py against the game's reader: `tracker.py prove` 0 failures;
  `tracker.py image` and `smps_trace --image` the same bytes for the
  game's music and for the chord6, chord16 and effect mods; both refuse
  the 17.
- `proof.sh r184 32` with PROOF_COLOUR=1 against r183: 172 PASS; all 815
  route files, the attract WAV, the Dark Force trial, audio_check, crt 27,
  sim 40, patches 14, probe_check 8, mods_check 16 the same; colour SAME.
- Q (`make ps2_headless ps2_headless_q q_demo fm_probe drum_hits`, then
  `qproof.sh q184` against q183): the 27 files the same but
  attract_q.log's line naming its own folder.
- `python3 tools/rumble_check.py`: PASS.
- ASan and UBSan (CLANG64's clang 23.1.3, smps_trace): the chord mods
  with the fade, the pause and the effect, and every one of the game's 23
  songs for 4,000 frames -- 30 runs, no report.
- `python3 tools/rom_left.py` exit 0, its counts as 183's.
- Against glibc (the server: GCC 13.3, Ubuntu 24.04): `make -k Q=0
  headless route_probe smps_trace` with
  `-Werror=implicit-function-declaration`, no warnings.
- What a chord chip costs: the headless attract (40 s, Phantasy from 5.7
  s), best of four, 10.1 s for the game's own on the chip, 14.2 with the
  six-note chord (one chord chip), 17.9 with sixteen notes (three); Q 6.7,
  7.7 and 12.1. The chips rest while silent, so a song without big chords
  costs nothing more.
- Not run: c_player_check and computer_check (the game is not touched: the
  routes' files and the attract are byte for byte 183's), controls_check,
  lang_check, script_ends, crt_gpu_check and the window. Not compiled:
  macOS, GCC 16 and `ps2` on Linux, Q's C++ anywhere but here (GCC
  16.2). Not heard by the owner: the six FLACs in `listen/` on this
  machine -- Phantasy from the title's start, the game's own, its chord
  of six and of sixteen, chip and Q.

**Seen on the way, not changed.** CLANG64's clang warns at songtext.c:1142
that `v` may be used unset (the `loop` flag: a label refused leaves it
so); the byte it goes into is thrown away with the refusal. The first
commit's.

**Docs.** docs/audio.md, "Chords past the chip"; docs/mods.md, "Chords of
up to 16 notes"; docs/harness.md (smps_trace's options, chords_check);
README (the tools, "Done since"); CLAUDE.md (the check, the baselines).

`make` (the game: `ps2`, `ps2_headless`), `make route_probe smps_trace`.

**Next**: nothing is queued; NOTES_next_context.md's "Next" lists what
waits for the owner.

## Build 185: the extractors read the cartridge image (2026-11-21)

Asked why the extractors needed the disassembly, the owner: "do the
rewrite to read the ROM directly". The bar set for it: every asset the
same, byte for byte, from the image alone.

**Before.** The 21 extractors (`EXTRACTORS` in the Makefile) parsed
ps2.asm -- its `dc.b`/`dc.w` lines, labels, comments and `binclude`s --
with ps2.constants.asm, art/ and the drums' ps2.pcm_drums.asm, through
tools/ps2asm.py and asmdata: `make extract PS2_DIR=../PSII_Disasm`.

**The change.** Nothing under src/ changed.
- tools/rom_map.json (new, 209 KB): where things are in the ROM and what
  the disassembly calls them -- ps2.asm's 7,450 labels in the order of its
  lines with their addresses, the drum file's 25 (Z80 addresses), the 56
  `binclude`d files with address and length, 884 constants with their
  values (not the `:=` and `set` ones, which change as the assembly goes),
  and the image's size and SHA-1. tools/make_rom_map.py makes it from
  ps2.asm and AS's listing of it; tools/make_rom_map.sh (new) does the lot
  from a copy of the disassembly -- `cpu 68020`, `asl -L`, ps2p2bin, the
  output checked against the ROM byte for byte -- and wrote the same map.
  The listing's symbol table has AS 1.42's integers typed `-`, not `I`.
- tools/rommap.py (new): RomMap checks an image's size and SHA-1 and reads
  it by name: a label's blob runs to the next label in line order (as
  ps2asm's did), constants, files, words. With PS2_ROMMAP_USED=FILE it
  writes the names a run used.
- Every extractor takes the image first: `extract_X.py ROM -o OUT`; the
  Makefile's rules pass `ROM=`, checked by the new `extract-rom`. What the
  disassembly's text gave and the bytes now give:
  - the drums' dispatch is decoded from the Z80 program (zloc_68), not its
    lines; DRUMS is (byte, name);
  - a store's name is its door's: the level holding the one door that
    opens it and the door's building ("Paseo Item Store"), from each
    LevelData record's door list (+14), not the disassembly's comment;
  - buildings' palettes through PalettePtrs' pointers, not
    paletteptrs.json (a dependency the Makefile never declared);
  - the characters' names in extract_techs from `<Who>_TechLevel`, and
    extract_gamedata's CharID list gone;
  - extract_npcs decodes the objects' inits with a small 68000 decoder,
    each routine the ObjectTable `bra.w` for its id (MotaYoungMan3's
    special case gone); extract_flag_names finds the routines that name a
    flag byte by decoding every label's block that is clean 68000 code to
    the next label, and keeps the absolute operands in $FFC700-$FFC7FF but
    `event_flags` and `treasure_chest_flags` themselves (the old pattern
    skipped them as ps2.asm writes them by name): its 71 references, in
    the same order;
  - the script: an entry to the next label, and what follows a script's
    last end code dropped when it is all zero (`even`'s pad, 15 of them,
    and the `rept $2A8` space after the last);
  - winart from WindowArtLayoutPtrs' records and the RAM-to-ROM rule;
    FMFreqs' length (85 words) as song_text.py has it.
  What the code holds is the format, as the old code had it -- record
  layouts, the text's codes, the 68000's and Z80's encodings, TILE_ANIMS
  and the map patches -- and no address in the ROM.
- tools/roundtrip.py and roundtrip_json.py: `--rom`; prove_edits runs the
  extractors on the image. The round-trip's packing back still reads
  ps2.asm, as do tools/door_audit.py and tools/rom_left.py.
- `make art ROM=...`: art_export.py and disasm_art.py read the image.

**Two files changed**, in bytes nothing reads.
- data/battle.json: the tails of 18 battle animation scripts (idle and
  anim's loc_270BC, enemy_attack, enemy_tech, both enemy_child tables,
  two of chars', seven of effect's, two of effect2's). The old reader
  kept a line's `$` numbers and read on to the first line that was not
  data, so a run with no `even` after it went over a table of `dc.w
  Label-Table` lines (no `$`, nothing taken) and joined the run after it,
  and into WeaponProperties it took `dc.b $00, $00, SXFID_Boomerang, $00`
  as three bytes and `dc.l $04000000|Map_WeaponBar` as four -- bytes, past
  112, that are not the ROM's in any order. Now each script is the ROM's
  bytes from its label to the next, at most 256. A scratch walk of
  battle_obj.c's animate() over every reading position ($FF back to the
  start, $FE the end, $F7's operand): the furthest byte any of the 18 can
  reach is 1 to 43, the first difference at 10 to 124. The round-trip
  counts the same 13,143 bytes packed; past an end mark, 12,943 not packed
  where it was 12,354.
- data/flag_names.json: two notes without the disassembly's comments,
  which the ROM does not have: `$C716` (loc_E690, "events after Roron
  (Jet Scooter and other stuff activated)") and `$C790` (loc_8A08, "this
  makes the treasure chests in Shure locked"). The game reads a flag's
  addr and name (gamedata.c, load_flag_names), never its notes.

**Checked** (the Windows desktop).
- `make extract` from the image alone, in a scratch tree with no
  disassembly (`PS2_DIR=/nonexistent`) and assets/ emptied but the 67
  files no extractor makes: 1,042 files made, and the 1,109 against the
  committed: 1,107 the same, the two above. Each extractor apart against
  the old extractors' own run (1,041 files, each the work of one): the
  same but those two.
- `make routes`: 172 PASS, the 1,244 files left in route_out/ the same as
  with the two files as build 184 had them. The Dark Force trial (`boss_trials.py --boss
  dark_force --policy both --repeats 10 --jobs 12`): the table the same.
- `make roundtrip` and `make roundtrip-edits`, on a tree of the old
  extractors and on this one: PASS, 0 failures, every edit caught; the
  reports the same but the battle scripts' line above.
- `song_text.py check` passes, and song_text's instrument_bank (which
  tracker.py uses) runs on extract_music as rewritten. `make extract`
  with no `ROM=`: refused, the image named.
- Not run: proof.sh (no file under src/ changed; the routes and the trial
  are the assets' check), glibc (no C changed). The extractors have run
  only here, on MSYS2's Python 3.14; not on Linux or the Mac.

**Docs.** README ("Build", the tools' rows, "Done since");
docs/windows_build.md (what needs the image, what the disassembly);
docs/game.md; CLAUDE.md (Machine); the Makefile's comments.

`make extract ROM=...`; nothing to build.

**Next**: nothing is queued; NOTES_next_context.md's "Next" lists what
waits for the owner.

## Build 186: the first start makes the game's files (2026-11-21)

Asked why the game itself could not take the cartridge on its first start
-- and the Japanese one -- so that no one has to run a script,
The owner: "yes, start step 1" (step 1: the window, with the extraction
still the Python's; step 2, later, the extraction in C in the game).

**Before.** A copy of the game without `assets/` failed at game_init with
the loaders' messages. The files came only from `make extract ROM=...`
(make and Python), and the JAPANESE language from `import_japanese.py`
by hand; with assets/lang/ja/ missing, choosing it set the language aside
("does not parse") and went back to the stock one.

**The change** (docs/first_start.md is the whole of it):
- tools/make_assets.py (new): `make extract ROM=` with no make -- the
  Makefile's EXTRACTORS and their commands parsed, each run on a copy of
  the image in a temporary folder, then the preload list. Its files are
  told apart by what they are: the US image (rom_map.json's size and
  SHA-1; .smd, copier headers and zips taken), the Japanese one (CRC32
  BEC8EB5A in its first 768 KB), Excalibur_Z's retranslation (a folder or
  GitHub's zip, by work/dialogue.json and work/script.json). The Japanese
  pair makes assets/lang/ja/. Where tools/assets.sha256 is, every file is
  checked against it (a PNG by its picture); this tree has none. For the
  window: `[K/N] WHAT` lines, the steps' whole output in make_assets.log
  (git-ignored), exit 0 done, 1 a step failed, 2 nothing to make the
  files from, 3 made but differing, 4 no Pillow. Steps start with
  CREATE_NO_WINDOW on Windows.
- tools/import_japanese.py: `--commit SHA` for a retranslation that is not
  a git checkout (GitHub's zip names its commit in its comment), so
  lang.json's "source" is the same as a checkout's.
- src/platform/first_start_sdl3.c (new; `ps2` and `make dist` link it):
  `platform_make_assets` and `platform_add_language` (platform.h). A window
  of its own -- SDL_Renderer, SDL's 8x8 debug face, 640x360 at whole
  multiples -- asks for the files (dropped, chosen in the system's dialog
  on Enter, Space or a click, or named on the command line), runs the
  script through SDL_CreateProcessWithProperties (its output piped and
  read each frame, never blocking), shows the steps on a bar, the script's
  words when it refuses, and asks again. The Python: PS2_PYTHON, one
  packaged beside the game, then the PATH's, each asked to import PIL; on
  Windows the windowless one first (pythonw.exe beside python.exe; pythonw,
  pyw -3), as a console Python started by a game with no console opened a
  console window over it here. The command line's files read again as
  UTF-8 from the wide command line on Windows.
- src/platform/platform_headless.c: the two say how to make the files and
  return false (`ps2_headless` exits 1 without them).
- src/main.c: no assets/manifest.txt and no pack -> platform_make_assets
  with the command line's files, before the preload; offer_language for
  `game.language_wanted`, at the start and after the ticks: made, the new
  files preloaded, the languages' names read again and the language
  chosen; left, settings.json keeps the one the game went on in.
- src/game/lang.c: `lang_missing` (a language's folder with no lang.json),
  `lang_rescan`. src/game/game.c: game_init offers a missing language
  settings.json names instead of loading it; take_language shows it on the
  menu while the menu is up and offers it when the menu closes on it, so
  passing it on the way round offers nothing. `Game.language_wanted` (-1
  for none).

**Checked.** In a scratch copy with assets/ emptied to the files no
extractor makes (C:/msys64/tmp/b186):
- make_assets.py on the image, 5.8 s: 1,075 files the same bytes as the
  tree's; manifest.txt the same but for the Japanese lines (not made);
  icon/rolf_portrait.png not made (no extractor's).
- The window, pictures taken of each (b186_shot.ps1, the window's own
  rectangle): asking (no files); a file that is not a cartridge (refused,
  its SHA-1, both cartridges described, asked again); the image on the
  command line (the bar at step 20 of 22, then the game: "assets: 1070
  files", the SEGA screen); settings.json naming JAPANESE without its
  files (the language's window at the start); a retranslation folder and
  a wrong cartridge on the command line (put in their slots, refused).
  MSYS2's python3.exe named, pythonw.exe found and used: no console
  window. One run stopped by the test itself mid-step failed its step (the
  game killed under it); left to finish, every run ended 0.
- The retranslation's GitHub zip at 061e1f5 (fetched to scratch): told
  apart, unpacked, the commit read from its comment; a folder holding it
  as GitHub's unzips; the refusals (the retranslation alone, the image
  twice, a file that is no cartridge, the Japanese before the game's
  files): 10 of 10.
- tools/lang_check.py PASS, with a sixth part: assets/lang/ja taken from
  its scratch copy, JAPANESE named by settings.json (offered, the stock
  crawl), chosen on the menu (offered, the retranslation's crawl stays,
  settings.json keeps it) and passed on the way round (nothing offered).
- r186 (PROOF_COLOUR=1, 32 jobs) against r185 (taken before the edit,
  itself r184's): every file the same -- 172 PASS, 815 route files, the
  attract WAV, the Dark Force trial, crt, patches, probe, mods, colour.
  controls_check PASS; `make dist` builds (the packed game never asks).
- glibc (the git server, GCC 13): `make -k Q=0 headless route_probe` with
  -Werror=implicit-function-declaration builds with no warning, and
  first_start_sdl3.c compiles against SDL3's headers with none.
- Not run: drag and drop and the file dialog by a person; the JAPANESE
  language made end to end (no Japanese cartridge on this machine); the
  window on Linux or the Mac, GCC 16, a Linux link with SDL3;
  computer_check (main.c's COMPUTER PLAYS path is not touched).

**Docs.** docs/first_start.md (new); README (the docs and tools rows, the
extract section); docs/languages.md (Japanese); CLAUDE.md (lang_check's
sixth part).

`make` (the game), `make headless`.

**Next**: step 2, the extraction in C, when the owner asks;
NOTES_next_context.md's "Next" lists what waits for him.

## Build 187: the extractors in C, begun (2026-11-22)

Build 186's first start runs the Python extractors, so the game needs a
Python with Pillow: carried in the Windows package, installed elsewhere.
Asked whether C was not good enough, the owner answered: "python is fine
for some things but keep in mind some of my original intentions was
maximum cross platform compatibility. Minimizing special or OS dependent
libraries is part of that goal", and then "unless there is something
superior you can't have with bog standard C minimal C++ Then avoid it.
You get to interpret what Superior is." Read as: SDL3 earns its place (a
window, input, sound, the GPU and pads on three systems, which C has no
way to give), Q stays the optional C++ it is, and the extraction -- bytes
in, files out -- is C11 and the tree's own code, nothing else.

So step 2 (docs/first_start.md, "In C"): the 21 extractors, about 10,400
lines of Python with their helpers, ported to C a few at a time, each step
the twin of a Python one and held to its files.

### The framework (src/extract/)

* `extract.h`, `exbase.c`: an Extract is the image, its map and an arena.
  Everything a step allocates comes from the arena, and `ex_fail` ends the
  step as `sys.exit` ends a Python extractor: a longjmp back to `ex_run`,
  which lets the arena go -- so the game can run the steps in its own
  process and live on after one refuses. Text built a piece at a time
  (ExStr), paths joined with '/', folders made with `_mkdir` on Windows
  and POSIX's `mkdir` elsewhere (the one call no C standard has; home.c
  and gamedata.c do the same).
* `exmap.c`: tools/rom_map.json read with src/core/json.c, the image
  checked by its size and SHA-1 (`sha1.c`, FIPS 180-4), and rommap.py's
  lookups with its exact rules: a name exactly, then in any case, the
  first label in line order before any drum; a constant's case-blind
  match the last in the map's order (rommap.py builds that dict with a
  comprehension, so a later key wins); `blob` as Python's slice, nothing
  when the end lies before the start. The indexes are sorted arrays of
  (key, rank), so no sort needs a global.
* `jout.c`: JSON written as json.dumps writes it -- with an indent, ","
  and ": " and a line a member; without, ", " and ": "; "[]" and "{}";
  ensure_ascii's escapes (\uXXXX in small hex, surrogate pairs past
  U+FFFF); a dict's keys in the order first set, a key set again keeping
  its place; sort_keys by code point, which for UTF-8 is strcmp's order.
  Integers only: no extractor writes a float.
* `excommon.c`: extract_gamedata.py's helpers (the name codes,
  constant_names, id_keys, run_of, block_at, label_at, file_at) and
  ps2png.py's palette strip, drawn with src/core/pngwrite.c (its own
  zlib stream: a PNG is the same picture as Pillow's, not the same
  bytes, which builds of Pillow do not agree on either; make_assets.py
  checks PNGs by picture since build 186).
* `asset_names.c`: tools/asset_names.py's tables, written by the new
  tools/gen_asset_names.py (`--check` says whether it is current), so the
  names have one source.
* `tools/ps2_extract.c`: `./ps2_extract NAME IMAGE [-o OUT] [--assets DIR]
  [--map MAP]`, each step as its Python twin is run; `--list` names the
  steps in C. `make ps2_extract`.

MinGW's GCC checks printf formats as Microsoft's old msvcrt takes them;
the tree builds against UCRT, which is C99's, so the format attribute is
gnu_printf there (`EX_PRINTF`) and %zu, %lld pass as on glibc.

### The first eight steps

palettes (the Pal_* labels' palettes.json), paletteptrs (every PalettePtrs
entry as a palette strip, paletteptrs.json), collision (each level's
blocks' quadrants as text, the jet scooter's table), interactions (the
doors and the town-edge exits), buildings (portraits, the buildings'
music, the portraits' windows), stores (the stock, named by the door that
opens each store, and Shir's lists), techs (the techniques and who learns
which), gamedata (items, characters, the footwear's agility). Each was
ported line for line from its Python, Python's floor division and modulo
kept (ex_floordiv, ex_mod) where the Python divides a difference that
could be negative.

### The check

tools/extract_c_check.py IMAGE: two whole extractions in scratch folders,
every Makefile step in order -- one all Python, one with each step
`ps2_extract --list` names run in C -- the folder looked at around each
step. Each C step must write and take away the same files as its twin,
and the two folders end the same: every file the same bytes, a PNG the
same picture (make_assets.py's file_digest). On the Windows desktop:

    extract_c: 8 steps in C (palettes, paletteptrs, collision,
    interactions, buildings, stores, techs, gamedata): their 292 files the
    Python's (77 PNGs the same picture, 215 the same bytes); the whole
    extraction 1041 files, 0 differ
    extract_c: PASS

(17 s for both extractions.) Every JSON file the C wrote was the
Python's byte for byte at the first run.

Nothing the game builds changed: src/extract/ is ps2_extract's alone
until the game runs the steps itself. `make -j32 all headless route_probe
patches_check ps2_extract` builds; the 172 routes PASS. glibc (the
server, GCC 13.3): `make -k Q=0 headless route_probe ps2_extract` with
-Werror=implicit-function-declaration, no warning. Not run on Linux or the
Mac (no image there), nor compiled on the Mac or with Linux's GCC 16.

Left for the next builds: levels, script, npcs, flag_names, battle;
inline_art, nametables, winart, art, pictures (the decompression and the
tile sheets); tables, music, drums (song_text.py and tracker.py, the
biggest part); then the Japanese importer, make_assets.py's own part (the
image's kinds, the zip, the SHA-256 check) and the game running the steps
itself, with no Python in the packages.

## Build 188: the levels, the script, the people and the flags in C (2026-11-22)

Step 2 goes on (docs/first_start.md, "In C"): four more extractors in C,
and what they share with the art steps still to come.

### Shared

* `exart.c`: the cartridge's two compressions -- DecompressArt's format
  (tools/ps2art.py: a count byte, then groups of a value and a 32-bit mask
  of where it goes, then a literal for each place no mask covered) and
  Nemesis (tools/nemesis.py: the code table, the 16-bit window, the inline
  symbols past $FC, the XOR mode) -- each with a `_try` form that hands
  back the Python's error to a caller that goes on; and ps2png.py's tile
  sheet, flat picture and colour ramp, with the grey ramp that art with
  no known palette is drawn in.
* `ex68k.c`: extract_npcs.py's 68000 decoder -- every instruction the
  game uses, its length and its operands, the same opcodes refused -- so
  a walk through the code stays on instruction boundaries.
* `excommon.c` takes extract_paletteptrs.py's table reader and its label
  rule (where two labels share an address, the one that does not end the
  block before it), which levels reads too; paletteptrs reads the whole
  table first now, as the Python does, the same files.

### The four steps

* levels (tools/extract_levels.py): each LevelData record's two planes,
  collision table and interactions; the tilesets out of loc_9342 (the
  tiles in Nemesis, the blocks in DecompressArt's format) as sheets, the
  blocks as text and drawn 32x32 in the first level's palette; each
  level's Tiled map (a backdrop plane in a map of its own) and JSON,
  levels.json, the two tile animations, the map patches LoadLevelData
  makes, and the mist. 316 files.
* script (extract_script.py): the window text, each block's deltas
  walked as LoadScript walks them and decoded by the script's charset.
* npcs (extract_npcs.py): who stands where, the art lists and their
  sheets, the townspeople's types, the chests and the 25 scene objects,
  each object read from its init in the 68000 code.
* flag_names (extract_flag_names.py): a name for each flag byte, every
  label's block decoded to find the routines that name one. Its
  hand-checked table (57 entries) was written into the C from the
  Python's by a script, not retyped.

### The check

    extract_c: 12 steps in C (palettes, paletteptrs, levels, collision,
    interactions, script, buildings, stores, techs, npcs, gamedata,
    flag_names): their 665 files the Python's (119 PNGs the same picture,
    546 the same bytes); the whole extraction 1041 files, 0 differ
    extract_c: PASS

(14 s for both extractions.) Each new step wrote the Python's files at
its first run. The C is quicker, though not by much (levels 0.24 s
against the Python's 0.69 s, flag_names 0.08 s against 0.26 s).

The Makefile's ps2_extract rule depends on src/extract's headers now,
so changing one rebuilds it.

Nothing the game builds changed. `make -j32 all headless route_probe
patches_check ps2_extract` builds with no warning; the 172 routes PASS.
glibc (the server, GCC 13.3): `make -k Q=0 headless route_probe
ps2_extract` with -Werror=implicit-function-declaration, no warning. Not
run on Linux or the Mac, nor compiled on the Mac or with Linux's GCC 16.

Left: battle; inline_art, art, nametables, winart, pictures (their
decompressors and sheets are in now); tables, music, drums (song_text.py
and tracker.py, the biggest part); then the Japanese importer,
make_assets.py's own part and the game running the steps itself, with no
Python in the packages.

## Build 189: the battle, the art, the screens and the pictures in C (2026-11-22)

Step 2 goes on (docs/first_start.md, "In C"): six more extractors in C,
18 of the 21 now, so everything but the music has its twin.

### Shared

* `exsheets.c`: the art sheets as the cartridge holds them, which
  tools/disasm_art.py (DisasmArt) gives the Python steps -- each
  art/<stem>.bin the disassembly includes, decompressed and laid out at
  the width extract_art.py guesses for it, and each art block inline in
  ps2.asm, 16 tiles across, padded to whole rows as the sheet PNGs are.
  Read once a run and kept. The inline labels (70) and their output
  names (11) were written into the C from extract_inline_art.py by a
  script, not retyped.
* `exbase.c`: `ex_remove` (a file, if it is one) and `ex_remove_ending`
  (a folder's files with one ending: the steps clear what an older build
  wrote). It reads the folder with dirent.h, which the tree already uses
  (autoplay.c) and every compiler it builds with has.
* `jout.c`: `jo_from`, a parsed JSON value as one to write, for a step
  that merges into a file already there (nametables.json).
* `png.c`: the indexed reader's decoding is `png_decode_indexed` now, from
  bytes the caller holds, so the extractors can read the palette PNGs an
  earlier step wrote past the asset store's cache and the mods. The game
  reads through `png_load_indexed` as before, and the routes show the
  split changed nothing. This is the one change to what the game builds.

### The six steps

* battle (tools/extract_battle.py): the enemies, the formations, their
  groups and regions, the characters' and the enemies' sprites (the
  animation tables, the frames, the sprite maps), the props and the
  effects' art, into battle.json.
* inline_art (extract_inline_art.py): each inline block of art as a grey
  sheet in assets/tiles.
* art (extract_art.py): each art/<stem>.bin as a sheet in its palette (the
  palettes the palettes step wrote, by name), grey where none is known.
* nametables (extract_nametables.py): the screens' 23 nametables, the
  words the game writes to the VDP, as Tiled maps, each cell drawn from
  the art video memory holds at its tile, in a tileset a sheet, palette
  line and priority, flipped by Tiled's own flags; and nametables.json.
  Its three tables were written in from the Python's by a script.
* winart (extract_winart.py): the 84 window maps, each its size from the
  window records that draw it, into winart.json and a Tiled map each,
  the portraits' interiors drawn from their sheets.
* pictures (extract_pictures.py): the 27 portraits and the library
  graph, and the 25 cut-scene pictures, as RGBA PNGs with their JSON and,
  where an effect recolours them, their slot maps. Its tables (the
  scenes, the slot maps, Rolf's and the graph's art, the windows the game
  never opens) were written in from the Python's by a script.

### The check

    extract_c: 18 steps in C (palettes, paletteptrs, levels, collision,
    interactions, script, buildings, stores, techs, npcs, gamedata, battle,
    inline_art, art, nametables, winart, flag_names, pictures): their 981
    files the Python's (271 PNGs the same picture, 710 the same bytes); the
    whole extraction 1041 files, 0 differ
    extract_c: PASS

Each new step wrote the Python's files at its first run. pictures takes
0.20 s in C against the Python's 0.90 s.

`make -j32 all headless route_probe patches_check ps2_extract` builds
with no warning; the 172 routes PASS. glibc (the server, GCC 13.3): `make
-k Q=0 headless route_probe ps2_extract` with
-Werror=implicit-function-declaration, no warning. Not run on Linux or
the Mac, nor compiled on the Mac or with Linux's GCC 16. `make` (png.c)
and `make ps2_extract`.

Left: tables, music, drums (song_text.py and tracker.py, the biggest
part); then the Japanese importer, make_assets.py's own part and the
game running the steps itself, with no Python in the packages.

## Build 189a: Q beside ps2/ (2026-11-23)

The owner asked whether Q could come with a copy of the repository
rather than be cloned on its own. A git submodule would bring Q's whole
tree (119 MB, and 259 MB of history) for the 2.4 MB of headers the game
reads; a link in the repository points at one machine's folder. So a
copy of the repository can carry Q's headers themselves, in `Q/` at its
top, beside `ps2/`, and the Makefile now looks there first of all
(`ps2/../Q`), before `~/GIT/Q` and the others: such a copy builds the
game with Q, at the headers' version, with plain `make`, no `Q_DIR` and
nothing else to fetch. This repository carries no Q (CLAUDE.md: Q is
never committed here), so here nothing changes: `Q=auto` still finds
`~/GIT/Q` (GAME_Q 1).

The Makefile only; README's build section and docs/macos_build.md list
the new place. Nothing under src/ changed, and nothing the routes build
looks for Q.
