# The port's saves in the emulator (build 138e)

The port is checked against the ROM in RetroArch, and the scenes worth
checking are deep in the game. `tools/to_srm.py` writes a port save into
the cartridge's battery RAM as the emulator keeps it (RetroArch's `.srm`),
so the ROM CONTINUEs exactly where the save stands; and reads a slot back
into the port's save format. `tools/emulator_scenes.py` makes the battery
file for the palette scenes of builds 138-138d.

## The scenes, ready made

`Phantasy_Star_II__USA__Europe_.srm`, handed over with build 138e beside
the tarball, is the owner's own file with slot 1 untouched (ASS, level 2) and
three scenes in slots 2-4:

| Slot | Shows | CONTINUE puts the party | Then | The port's route |
|---|---|---|---|---|
| 1 | (yours) | | | |
| 2 | `LIB 18` | Paseo, below the Central Tower's door (32,17), the Recorder in the bag | Up two into the tower; C through the desk, the Governor and the library; "Please look -- overlap the graphs": the graph's hidden lines show in white, cyan and red | `library_graph_overlay` |
| 3 | `LAB 12` | The Biosystems Lab B1, 79,20 | Left twice: the second step is a damage floor -- one white frame, then the lights at their loaded colours until the next cycle step (the orange light at $08E, then $006) | `emulator_lab_damage_floor` |
| 4 | `NOAH 41` | Noah's first floor at the foot of Mother Brain's corridor (33,40) | Up the corridor: at row 33 the screen goes into white (red first, then green, then blue) and comes back from white in her chamber (blue first), then her first words | `emulator_mother_brain_white` |

The Sega screen's fade needs no save: power on (the logo's white through
deep blue and cyan to white, blue first), then Start (out red first: through
cyan and blue to black).

Each scene's frames as the port draws them are in
`ps2-port_138e_scenes.zip` (handed over with the build): a video a scene,
real speed then slowed eight times, every frame numbered with its tick and
the colour of a marked pixel as the hardware's word, and a sheet of the
frames that matter. RetroArch's frame advance (pause, then the frame-advance
key; `K` by default) steps the ROM a frame at a time to compare.

Slot 3's lab stands in for Gaira's floor of 138d's check: continuing a save
made inside Gaira would lose the party's gear when Tyler frees it (the gear
the prison takes is kept outside the battery RAM, as the ROM does), and the
lab's basement cycles more of its lights than any other floor with damage
blocks on Motavia.

## Putting a file in RetroArch

1. Close the game in RetroArch first (Quick Menu, Close Content): RetroArch
   writes the battery file when the game closes (and at its SaveRAM
   autosave interval, if one is set), and would write its own copy over the
   new one.
2. In the folder the `.srm` came from (RetroArch's saves folder; with "Sort
   saves into folders by core name" on, its `Genesis Plus GX` folder under
   it), keep a copy of the old `Phantasy_Star_II__USA__Europe_.srm`, then
   put the new file there under the same name -- the name of the ROM.
3. Start the game: "Let me check the backup data" checks every slot (all
   pass), then CONTINUE and the slot.

## Commands

From the tree's top (the script finds the tree from where it is):

    python3 tools/to_srm.py check YOUR.srm
    python3 tools/to_srm.py write routes/saves/story_dams.json --slot 2 --srm YOUR.srm -o NEW.srm --label DAMS
    python3 tools/to_srm.py write saves/slot1.json --slot 1 --new -o NEW.srm
    python3 tools/to_srm.py read YOUR.srm --slot 1 -o slot1.json
    python3 tools/emulator_scenes.py YOUR.srm -o NEW.srm
    make save_resave && python3 tools/to_srm.py selftest --srm YOUR.srm

* `check`: the ROM's power-on check and CONTINUE on every slot -- good,
  empty, repaired from the mirror, or lost -- with what the slot list shows
  and where CONTINUE puts the party.
* `write`: a port save into a slot and its mirror copy. `--srm` keeps that
  file's layout, size and every other byte; without `-o` it rewrites the
  file in place after copying it to `.bak`. `--new` makes a 16 KB file in
  Genesis Plus GX's layout with the other slots empty. `--label` sets the
  slot list's four letters (the harness's saves have none). The written
  file is checked before it is kept: the slot passes the power-on check,
  CONTINUE fills RAM with exactly the block written, the block reads back
  as the same save, nothing outside the slot moved.
* `read`: a slot as the port's save file, written as the game writes one.
  It says anything the port's format cannot carry (a name letter outside
  its alphabet, bag slots past the count, ...); a save the ROM wrote reads
  and writes back to the same 1024 bytes.
* `emulator_scenes.py`: slots 2-4 as above, slot 1 kept; `--new` for a
  file with slot 1 empty; `--setup` remakes `routes/saves/emu_mother_brain.json`
  (from `walk_mother_brain.json`, walked to the corridor's foot as story.py
  walks it, then the probe's `save-here`).
* `selftest` (34 s): every save under `routes/saves` read as the port's C
  reads it (`save_resave`, `make save_resave`: gamedata_load then
  gamedata_save) and written into each slot and back; 4746 edited saves
  (each field removed or given another kind of value) read as the C reads
  them; with `--srm`, every block of a file the ROM wrote read and written
  back; the port's new game against the ROM's; and the check's own faults
  caught.

## The battery RAM

Read in ps2.asm: the save (SaveData), the checksum test (loc_D676), the
copyright test (loc_D6AA), the repair copy (loc_D6D4), the power-on set-up
(loc_D700), the load (LoadSavedData), the power-on check (loc_D1E0,
loc_D20E), CONTINUE (loc_D3B6), the Data Memory's save (loc_B5A4), the slot
list (loc_104EC).

* **Where.** $200001-$203FFF, odd addresses: 8 KB. Eight blocks of 1024
  bytes, $800 addresses apart: slots 1-4 from $200001, and each slot's
  mirror copy $2000 above it. A save writes both the same.
* **A block** is the RAM a save copies, in this order: the eight
  characters' records ($C000-$C1FF), the flags ($C700-$C7FF), the party
  block ($C600-$C69D), a checksum word (big-endian: the 16-bit sum of the 926
  bytes before it; SaveData stores it at $C69E too), and $C6A0-$C6FF, which
  holds the 96-byte copyright string a new game copies there.
* **In use**: $C69C is $FFFF once the Data Memory has saved; the slot list
  and CONTINUE test its first byte, in the slot itself.
* **The slot list** shows $C680-$C683 (the letters typed at the Data Memory,
  else the slot's old name) and $C684 (Rolf's level).
* **At power-on**, the first time the select-game screen opens: a slot whose
  copies both lack the copyright is written from RAM (the new game the title
  has just made -- the empty slots hold exactly that); then "Let me check
  the backup data": a slot in use in either copy has both checksums tested,
  a bad copy is repaired from the good one ("restored"), and both bad means
  SaveData writes RAM over the slot -- the save is lost.
* **CONTINUE** tests the slot's in-use byte, loads the main copy whole
  (LoadSavedData reads 256 bytes from $C600, the checksum and copyright with
  it) and opens the Data Memory: Motavia's attendant, or Dezolis's when the
  last save was made there and not by Visiphone. In the port, leaving it
  puts the party on the saved map at the saved place, as the scenes' routes
  show (the building's exit was not read for this build: the emulator check
  is a check of it too).
* **The names' tiles.** A character's record ends in eight bytes the port's
  save has no field for: the name as the windows draw it, an upper and a
  lower tile a letter (+$38-+$3B uppers, +$3C-+$3F lowers; SetCharNames,
  through VDPCharacterMaps). A new game and the name board write them;
  CONTINUE does not, so the ROM shows names from the save. The writer makes
  them as SetCharNames does; the owner's file has them so in every block
  (slot 1's Rolf spells ASS, the empty slots' ROLF).

**The port's side** (src/game/gamedata.c, unchanged): gamedata_load reads
`raw_c600` first and the named fields after, which win where the two
disagree; the converter's reading is that function's, field for field, and
`selftest` holds it to the C. A port save always lands in use ($C69C =
$FFFF, as every Data Memory save sets it). Saves the harness made carry no
slot-list name; `--label` gives one.

**The file's layout.** Genesis Plus GX writes the 16 KB the cartridge's
range covers, the data on the odd bytes and $FF on the even ones. A file
some core has cut short of 16 KB grows back to it when a block past its end
is written.
`to_srm.py` finds the layout by where the copyright string is -- odd bytes,
even bytes, or the 8 KB of data alone -- and keeps it.
