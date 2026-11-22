# Phantasy Star II — cross-platform C port

A reimplementation of Phantasy Star II in portable C, working from the
Macro Assembler AS disassembly. It does not emulate the 68000, the Z80 or
the VDP.

## Where the rest is

This file is the reference a session starts from: what is here, how to
build, the design, the formats, controls, and what is not done. The rest
is in `docs/`, one file a subsystem, and NOTES_next_context.md says which
parts the next task needs.

| File | What |
|---|---|
| `docs/engine.md` | Level format, the Motavia opening, frame pacing, text |
| `docs/game.md` | Game start, saves, the dream to Rolf, shops and every other building, pause and the C menu, townspeople, chests, Darum and Teim, Shure's bodies, Pandora's box, the jet scooter, the level screen with the party |
| `docs/audio.md` | FM, the SMPS sequencer, the drums |
| `docs/instrument_names.md` | (build 136d) The names for the 105 FM instruments, agreed, for the song format, with who plays each and where its notes sit |
| `docs/second_chip.md` | (build 116a) A second Nuked-OPN2 for the effects: the design; (build 124) built, VOICES EXTRA and LIMITED |
| `docs/windows_build.md` | (build 128) A Windows machine from nothing: MSYS2's packages (`tools/setup_msys2.sh`), Q, the shaders' compiler, the sanitizers; what the tree carries and what is fetched; drivers |
| `docs/macos_build.md` | (build 135a) A Mac from nothing: Xcode's tools, Homebrew's SDL3 and Python, Q against libc++, the Metal shaders; the known gaps (`make dist`'s package, HDR, the proof scripts' GNU tools). Built and checked on an Intel Mac in build 149a (MacPorts) |
| `docs/computer_plays.md` | (build 100b) COMPUTER PLAYS: the design, speed and pace, the player's saves, (115) on Windows, (118) squeak-by, (179) the C player in the window |
| `docs/c_player.md` | (builds 170-179) The player in C (`src/player/`): play.py, story.py, playthrough.py and computer_plays.py ported step by step, each held to the Python's games; (179) what COMPUTER PLAYS runs |
| `docs/battle.md` | Random encounters, the battle screen, the fight |
| `docs/crt.md` | (build 105) The CRT simulation: the settings, what a display can have, the model, the reference and its checks, the display probe, phases 2 and 3 |
| `docs/palette_effects.md` | (build 138) The palette survey: every routine that writes the palette, by kind (loads, whole-palette fades and flashes, index-dependent cycles and lights, the backdrop), what the port does for each, how full colour does it, the order from 138c and the questions |
| `docs/emulator_saves.md` | (build 138e) The port's saves in the emulator: the battery RAM's layout as the ROM reads and writes it, `tools/to_srm.py` (a port save into an `.srm` slot and back, the ROM's power-on check and CONTINUE worked through), the scenes' file for RetroArch |
| `docs/mods.md` | (builds 138b-c) The mods folder, every build's: a file there read in place of the game's own, the packaged game's originals written out on first start, the music's fallback when a mod is refused |
| `docs/languages.md` | (builds 163-165) The languages: LANGUAGE on the Escape menu, a language's folder (`assets/lang/<key>/`) standing in for the game's own files, Excalibur_Z's retranslation converted (`tools/import_retranslation.py`) and drawn in his proportional face, what is his and what was written for the original game, Japanese later |
| `docs/first_start.md` | (build 186) A copy of the game without its files makes them on its first start, from the player's own cartridge: the window that asks for the image (dropped, chosen, or on the command line) and shows `tools/make_assets.py`'s steps; the JAPANESE language offered the same way; which Python; how it was tried |
| `docs/harness.md` | `route_probe` and the route language, `fuzz`, `route_probe -i`, `play.py`, `fuzz_levels.py`, the route list |
| `docs/old/` | (build 142b) The old handover and questions files of builds 112-113 (`handover_113_windows.txt`, `questions_113.txt`), moved from the tree's top; history.md cites them by name |
| `docs/machines.md` | (build 142a) What runs where: the headless server's unattended proof of every commit (`server_proof.sh`, a git hook), the Linux desktop and the Windows machine's windowed builds, shaders and GPU check |
| `docs/hd_art.md` | (build 158) HD art from the game's pictures: an SDXL model taught the series' style (a LoRA), each picture drawn again at exactly 9x; the machine's C:\AI, the tools, the first results; (168c) the runs v3 to v7 and what came after |
| `docs/rom_left.md` | (build 181) How much of the ROM is left: every 68000 routine counted against what src/ cites, the uncited ones read (ported, replaced, never run), the sample inside the cited ones, and what is left |
| `docs/history.md` | Builds 67 on, one section each: what changed, what the ROM said, which routes |
| `docs/party_trials_build79.md`, `docs/dam_trials_build80.md`, `docs/type_trials_build88.md`, `docs/noah_trials_build90.md`, `docs/walk_trials_build97.md`, `docs/boss_arrival_trials_build97b.md` | Measurements |

## Credits

* **The disassembly** the port is read from: lory90's (lorenzo)
  *Phantasy Star II Disassembly for Mega Drive/Genesis* (`PSII_Disasm`,
  distributed by Phantasy Star Cave), assembled with Alfred Arnold's
  Macro Assembler AS.
* **The retranslation** (LANGUAGE: RETRANSLATED, builds 163-164;
  docs/languages.md): Excalibur_Z's *Phantasy Star II: English
  Retranslation* (github.com/ExcalZ/ps2-retranslation) -- the English
  script made anew from the Japanese one, its names and glossary, and its
  proportional font (the mixed-case face of his Phantasy Star III and IV
  retranslations); the port follows its layout for the longer words (the
  wide battle box, the wider technique windows, six-letter names). Its own
  credit: "Translation, hacking and testing by Excalibur_Z, with Claude
  (Anthropic) for translation and technical work." MIT; his notice is
  `assets/lang/retranslation/LICENSE.txt`, and the package's `licenses/`
  carries it. Two of its lines are the port's own, for the original
  game's shop flow (docs/languages.md, "For the original game"). It is
  built over the same disassembly.
* **The Japanese** (LANGUAGE: JAPANESE, build 165): SEGA's -- the Japanese
  cartridge's (*Kaerazaru Toki no Owari ni*) text and font; the text as
  Excalibur_Z's tools decoded it and matched it to the US script ids (his
  retranslation's `jp` fields), the font and character map read from the
  cartridge by `tools/import_japanese.py` (docs/languages.md, "Japanese").
* **The libraries:** Nuked-OPN2 by Alexey Khokholov (Nuke.YKT), the chip
  (LGPL-2.1; `src/audio/LICENSE.ym3438`); Cycfi's Q by Joel de Guzman, the
  re-voiced synth (MIT); SDL3. Their licences go in the package's
  `licenses/` (`make dist`).
* Phantasy Star II is copyright SEGA. The tree has no ROM.

## What is here

| Path | Purpose |
|---|---|
| `tools/extract_palettes.py` | Pulls named palettes out of `ps2.asm` into JSON and palette PNGs (the PNG's PLTE is the palette) |
| `tools/ps2png.py` | Writes the indexed PNGs every pixel asset is stored as |
| `tools/ps2art.py` | Decoder for the `DecompressArt` compression format |
| `tools/extract_art.py` | Batch-decompresses `art/*.bin` into indexed tile-sheet PNGs under `assets/tiles/`; (156) not the art the pictures replaced (`asset_names.PICTURE_ART`) |
| `tools/disasm_art.py` | (build 156) Any tile sheet's tiles and grid, by name, straight from the cartridge image (build 185; the disassembly's `art/*.bin` or an inline block before): what the picture, window-map and nametable extractors and the art dump read; `python3 tools/disasm_art.py ROM` checks `assets/tiles` against it |
| `tools/rom_map.json`, `tools/rommap.py` | (build 185) Where the disassembly's labels, files and constants are in the cartridge image, and the reader every extractor takes the image through (it checks the image's size and SHA-1) |
| `tools/make_rom_map.sh`, `tools/make_rom_map.py` | (build 185) rom_map.json made again from the disassembly: assembled with AS for its listing, the output checked against the ROM byte for byte, the labels' addresses read off the listing |
| `tools/make_assets.py` | (build 186) `make extract ROM=...` with no make, for the game's first start (docs/first_start.md): the files given told apart by what they are (the US image, the Japanese one, the retranslation), every extractor the Makefile names run, the JAPANESE language made, checked against `tools/assets.sha256` where there is one; `[K/N]` lines for the window, exit 0-4 |
| `tools/extract_script.py` | Every block of window text (`GameScriptPtrs`, 26 blocks, 611 scripts) into `assets/script/` |
| `tools/extract_winart.py` | Pulls the 84 `WinArt_*` tilemaps out of `ps2.asm` |
| `tools/extract_inline_art.py` | Decompresses art stored as inline `dc.b` (FontsIconsArt, SegaLogoArt); (156) not the art the pictures replaced |
| `tools/nemesis.py` | Decoder for the `NemDec` format the level tilesets use |
| `tools/gen_script_ids.py` | (build 96a) `src/game/script_ids.h`: a `SCRIPT_*` name for every script id game.c, building.c, npc.c and (97b) menu.c use, from `assets/script/`; rerun after using a new one (`--apply` names menu.c's literals) |
| `tools/gen_rom_ids.py` | (builds 96a-96b) `src/game/rom_ids.h`: `MUSIC_*`, `SFX_*`, `LEVEL_*`, `ITEM_*` from the constants file (run it with the constants file's path); `FLAG_*` from `flag_names.json` plus its own names for bytes the save knows only by a routine; the A-button `EVENT_*`, `EVENT_KIND_*`, `TEIM_DARUM_*`, the one-shot `SCENE_*`, `DEMO_INDEX_*` and (97a-97b) the level screen's eight `CAMERA_*` cases, each read in ps2.asm; (97b) `PALETTE_*` from `tools/asset_names.py` |
| `tools/asset_names.py` | (build 97b) The friendly names of the assets whose ROM label or index said nothing -- palettes, tilesets, level sprites, tile animations, abbreviated art files -- read by every extractor that writes them and by `gen_rom_ids.py` |
| `tools/ps2asm.py` | Shared reader for labelled inline data in `ps2.asm` |
| `tools/extract_levels.py` | Level tilesets, block definitions and block maps, all 101; the tile animations and the maps' patches by flag |
| `tools/extract_paletteptrs.py` | Every `PalettePtrs` entry by index, named or not |
| `tools/extract_gamedata.py` | Items, starting equipment and the level tables, as JSON |
| `tools/extract_buildings.py` | `PortraitTable` and `BuildingMusicPtrs`, as JSON |
| `tools/extract_techs.py` | `TechniqueData` and `CharAvailableTechniques`, as JSON |
| `tools/extract_stores.py` | Every shop's stock (`StoreEquipItemArray`) and Shir's steal lists, as JSON |
| `tools/extract_flag_names.py` | A name for each byte of `$C700-$C7FF`, `assets/data/flag_names.json`: the save's flag keys; (97f) every event byte named for what reads or writes it, `_unused` where nothing does. Only a key's `c7XX` is read back, so older saves load unchanged |
| `tools/extract_battle.py` | `EnemyData`, formations, encounter groups and region tables, enemy maps, the eight animation-script tables, the party's battle mappings, `WeaponProperties`, `TechniqueProperties`, `WeaponTechArtPtrs` and the effect maps, as JSON |
| `tools/building_probe.c` | Diagnostic: every ported building, scripted, with script trail and party state |
| `tools/extract_npcs.py` | Townspeople, chests, the Darum/Teim scene objects, Shure's dead bodies and the jet scooter: placement, art, types, dialogue tables and chest contents |
| `tools/extract_collision.py` | Per-block collision tables, as text, and the jet scooter's (`loc_29E16`) |
| `tools/extract_interactions.py` | Doors, town entrances and edge exits, as JSON |
| `tools/door_audit.py` | Diagnostic: every door record against the cells that can reach it; the ones nothing can |
| `tools/roundtrip.py` | (build 130a) `make roundtrip`: every extractor run again into a scratch tree and compared with assets/, and each converted file (tile sheets, pictures, palettes, block definitions, Tiled maps, collision tables, scripts; (136e) the credits) read as the game reads it, packed back and compared with the ROM's bytes (docs/history.md, Build 130a); (131) the JSON tables too, by roundtrip_json.py; `make roundtrip-edits` proves an edit to each is caught; (156) the pictures re-extracted at level 1 too (until 156 it skipped their extractor); (139b) each song's `.trk` read as the game reads it and played, cell by cell through its loop, against the ROM's song |
| `tools/roundtrip_json.py` | (build 131) level 2 for the JSON tables: each file read as its C loader reads it and packed into the ROM table's layout (stores, steal lists, techniques, items, the characters' tables, agility, portraits, building music, portrait windows, doors, exits; (131a) the townspeople, their art lists and dialogue numbers, chests, the object types' inits and mappings; (131b) the level records, each `.tmx` read by level.c's own scanner, each pointer resolved to the ROM label holding what the level's file loads as; (131c) battle.json's fourteen tables, the animation scripts to their end codes, the level animations and map patches from their routines' operands, PalettePtrs, GameScriptPtrs); fields packed and not packed listed, harmless differences in its CHECKED table (docs/history.md, Build 131) |
| `tools/asmdata.py` | (build 131) ps2.asm's data tables assembled with their symbols evaluated -- the constants, `id()` from the tables' layout, nametxt, charset -- pointers kept as the labels they name: the JSON round-trip's ROM side |
| `tools/make_manifest.py` | The list of files the game preloads; (build 130) `assets/icon/`, which only the build reads, left out (and of `make dist`'s pack) |
| `tools/intro_probe.c` | Diagnostic: new game / continue / erase, scripted, against real saves |
| `tools/to_srm.py` | (build 138e) A port save into a slot of an emulator's battery file (`.srm`) and back, in the file's own layout; `check` works through the ROM's power-on check and CONTINUE; `selftest` against the port's C (docs/emulator_saves.md) |
| `tools/tracker.py` | (build 139) The songs laid out as a tracker, `<song>.trk`: sections of rows with every channel side by side, notes as heard, a repeated column written once (`fm1 = 03`), each channel's `goto`; `export`, `streams` (the stream text a file reads as), `prove` (the files play the same as the stream text, chip write for chip write, `smps_trace --regs`). (139b) The game reads them, `assets/music/songs/<song>.trk`, in place of the stream text; `game_music` and `image` build what the game builds (docs/audio.md, "The tracker format") |
| `tools/save_resave.c` | (build 138e) `make save_resave`: gamedata_load then gamedata_save on one slot file, or a new game -- what to_srm.py's reading is held to |
| `tools/emulator_scenes.py` | (build 138e) The battery file for the palette scenes: slot 1 kept, slots 2-4 the library, the lab's damage floor, the foot of Mother Brain's corridor (`--setup` remakes `routes/saves/emu_mother_brain.json`) |
| `tools/level_probe.c` | Diagnostic: one level frame at any camera position |
| `tools/extract_nametables.py` | Extracts the VDP nametables DrawArtTiles feeds to the data port, as Tiled maps (build 135) |
| `tools/extract_tables.py` | Extracts the dissolve order (FadeEffectArray) and the credits; (build 136) the Sega logo's sweep and the credits' fade as colour-ramp pictures; (build 136d) the FM instruments moved to extract_music |
| `tools/extract_music.py` | Pulls the sound driver's data region out as one flat image; (build 136c) the note frequencies as `music/note_frequencies.json`; (build 136d) the FM instruments and their upload order as `music/fm_instruments.json` |
| `tools/extract_drums.py` | The Z80 program's drum table and the six PCM samples: (build 136b) `music/drums.json` and one WAV a sample |
| `tools/smps_trace.c` | Diagnostic: piano roll or chip-write log from the sequencer; (build 136c) notes by their concert names; (139b) `--image FILE` writes the image it built (a song's `.trk` read in place of its stream text); (build 184) `--chords`, `--voices`, `--play ID@FRAME`, `--resume FRAME` (docs/harness.md) |
| `tools/chords_check.py` | (build 184) A mod's chords past the chip: the voices counted, the chip's writes the song's own, each voice struck at its note, the fade, the pause, a 17th note refused, an effect's chord, and a chord of six heard through the headless game on the chip and on Q (docs/harness.md) |
| `tools/pause_probe.c` | Diagnostic: the game through Start and Start again, every chip write and the track blocks |
| `tools/menu_probe.c` | Diagnostic: the game through a button script in the C menu, the window stack and the party |
| `tools/route_probe.c` | Testing harness: the whole game headless through a route script, waits on game state, PNGs on request, a log by tick |
| `tools/colour_check.py` | (build 140) Every route's frames drawn both ways: route_probe with `PS2_COLOUR_CHECK` composes every tick's frame and holds its colour plane to the palette path, writes each shot again from the colour plane, and the shots are read back with Pillow and compared (docs/harness.md); (142) each route's pixels drawn from pictures counted and named |
| `routes/*.route` | The routes `make routes` runs: new game to control, a Paseo shop, Darum, Shure F3's bodies, a fight, the jet scooter into a dam and off it, Nido's door and Teim, Blastoids copying themselves, Teim taken to Darum, the towns' control towers and Arima's blown doors, Ustvestia's songs and lesson, a Skure newspaper, the Recorder, the Maruera tree, the tower's cards, keyboard and Musik, the Hidapipe, an Archdragon's blow, Roron's cake and the jet scooter's maker, the battle lists' NEXT, Kueri's gum inventor, A at a Biosystems Lab capsule, the gum's way out of the Underwater Passage, Neifirst both ways with a shot a reload, the Recorder for the Key Tube, a dam's panel, the Army Eyes' capture and the Gaira prison through Tyler's spaceship to the Governor, the roof's YES and NO and the flight both ways, the Dezolis towns' people and Skure's musk cats with and without a cap, Motavia's map under the dams' patches, the rocky mountain on Uzo's island, the Earthmen's leader on Noah, the C menu shut through the Esper B1 hold and the Motavia views, the one-shot cases $D and past $10 at a load, a hand-edited save's Telepipe landing, the planet set by a level load, (131c) Dark Force's jaw and Gra's full strike |
| `tools/fuzz_levels.py` | Grinding: the fuzz from every level under ASan, failures, sanitizer reports and stderr lines summarised; (build 114) `--jobs N` runs at once, the report the same |
| `tools/play.py` | Playing it like a person, fast: a driver on `route_probe -i` -- sync on `state`, fight through, find the way on any level's own collision, gear up in every town, find a wanderer, choose the party (`choose_party`), ride the jet scooter (board, ride, get off, face something on the water) |
| `tools/dam_trials.py` | Every party of Rolf and three through a dam floor's fights: fights per trip by grind cost (build 80) |
| `tools/story.py` | The story played with no level cheats: the walk to Arima as the first grind (build 101), Rudo, grind and gear in Paseo, Arima's weapons before Shure, Shure, Nido to Teim, Darum, Oputa, Amy, the Biosystems Lab, the Recorder, Hugh and the Key Tube, Zema, Roron and the jet scooter, Uzo's leaf and Kueri's gum, the Underwater Passage to Climatrol, the dams, the roof's spaceship to Skure, and on through Dezolis, the Prism, the Nei Sword and Noah to Dark Force, Mother Brain and the ending (build 94); checkpoints in `routes/saves/story_<stage>.json` and a report |
| `tools/boss_trials.py` | Dark Force or Mother Brain with each party of Rolf and three from a save, under fight_smart's boss policies: wins, the boss's HP left, each member's TP, curses and techniques (builds 91, 94); (97b) `--arrival` / `--walk-policies`: the fight as a walk to it leaves the party, its mates and TP; (build 114) each fight a fresh probe at its repeat's offset, `--jobs N` at once with the same results, `--chain` for the one-session draw up to 113 |
| `tools/party_trials.py` | Neifirst against every pair the house allows beside Rolf and Nei, to choose whom to grind (build 79) |
| `tools/shir_trials.py` | (build 87a) Shir's thefts from Paseo's shops and the Central Tower, measured against ProcessStealItem's one in sixteen |
| `tools/grind_rate.py` | The EXP a Climatrol fight pays each member with Nei alive or at 0 HP (the victory's split, loc_F2A4) |
| `tools/plan_check.py` | (build 89) plan()'s checks: the timed plans from Zosa, and `--equiv` reaches() against path() on every level |
| `tools/probe_check.py` | (build 91) Checks of route_probe a .route cannot express (a session's own slot as its start, the wipe screens; (157) an asset missing at start: exit 2, the file named in the log; (157a) two game overs in one session, the battle drawn at both hand-overs; (162) COMPUTER PLAYS's driver on Python 3.8, without a probe; (168e) the driver's new game in each language, ENGLISH 1989 the new_game route tick for tick) |
| `tools/rumble_check.py` | (build 160) The rumble's check: every route and 160 fuzz runs (fights from many formations, Gaira's and the lab's damage floors) through route_probe with `PS2_RUMBLE_CHECK`, the watcher held to the game's own sounds and HP both ways, every kind felt ((build 161) blast, shake, flight, landed and hole too, and each followed sound's shake against its loudness); the headless game's GAMEPAD page and settings.json's `"rumble"`; and a fight in the headless game against the same fight in route_probe, its `PS2_RUMBLE` log line for line at MEDIUM, LOW and OFF, the Escape menu stilling it |
| `tools/import_retranslation.py` | (build 163) Excalibur_Z's retranslation as a language: his script, names and face from a checkout of his repository, read as data, into `assets/lang/retranslation/`; `--check` proves the notation on his 1989 text against `assets/script/` (docs/languages.md) |
| `tools/import_japanese.py` | (build 165) The Japanese cartridge as a language: its font and character map from the image (a zip of it will do; CRC32 BEC8EB5A checked), its text from the retranslation's `jp` fields, its name board's art, into `assets/lang/ja/` (docs/languages.md, "Japanese") |
| `tools/lang_check.py` | (build 163) LANGUAGE through the headless game: chosen on the menu and written to settings.json, read back, a key no language has, a broken mod of a language's file set aside (docs/languages.md); `make headless` first |
| `tools/script_ends.py --check` | (build 168f) every language's messages end where the US ones do, so none runs on into a message the game then shows again (docs/languages.md, "Where a message ends"); no build needed |
| `tools/controls_check.py` | (build 160) The controls through the headless game (`PS2_RAW`'s raw presses, `PS2_PAD_STYLE`): a key and a gamepad input bound on the CONTROLS and BUTTONS pages reaching the game and the old ones not, settings.json's `"keys"` and `"pad"` written and read back, the BUTTONS page's labels by pad, a file from before 160 (`"controls": "wasd"`) read as the defaults with no message, names nothing has kept as defaults with one; a bind on frames worth no tick or two (`PS2_FRAME_HZ`); `make headless` first |
| `tools/mods_check.py` | (build 157d) The headless game with one broken mod of each kind (a PNG, a tile sheet, a ramp, the dissolve order, a JSON file, a Tiled map, a nametable, a window map, two pictures, a script, the credits, a level map) in a scratch mods folder: each set aside and the game's own read, the WAV and last frame the same as with none; a mod the loaders take still read (docs/mods.md); in proof.sh |
| `tools/hd_dataset.py` | (build 158) The training set for the HD art's LoRA (docs/hd_art.md): the art dump's every image once, cropped, enlarged by a whole number, masked and captioned; an official-art folder beside it; OneTrainer's settings; (158a-158c) the whole series' folders, a game's in-game images capped an epoch, the party's and III's and IV's casts named from the paths |
| `tools/hd_generate.py` | (build 158) An HD picture from a native one through a local ComfyUI, exactly a whole number of times its size (9x by default): the enlarged picture as start and tile-ControlNet guide, the native transparency, hd_check's measure (docs/hd_art.md) |
| `tools/hd_check.py` | (build 158) How faithful an HD picture is to its native one: each block averaged against its pixel (Delta E), the transparency exact; refuses a size that is not a whole-number scale |
| `src/harness/probe.[ch]` | (build 100b) The route language's interpreter, shared by `route_probe` and the game's COMPUTER PLAYS |
| `src/harness/autoplay.[ch]` | (build 100b) COMPUTER PLAYS: the interpreter on a thread, each tick handed to the frame loop; (build 179) the C player on that thread, each of its commands run in the game; `PS2_COMPUTER_DRIVER=python` the Python driver as a child process, as builds 100b-178 did (POSIX; (build 115) Windows too) |
| `src/player/` | (builds 170-179) The player in C: the harness's player (play.py, story.py's stages, playthrough.py's loop) and computer_plays.py's, hosted by `route_probe --play`, `route_probe --computer` and (179) the game's COMPUTER PLAYS (docs/c_player.md) |
| `tools/computer_plays.py` | (build 100b) COMPUTER PLAYS's driver until build 178: playthrough's loop over story.py's stages, attached to the game's pipes; `--classify SAVE` says where it would pick up; (build 118) squeak-by, as playthrough's; (179) the reference `src/player/computer.c` is held to |
| `tools/c_player_check.py` | (build 172) The C player (`route_probe --play`) against playthrough.py: seeds in each language, the story logs and saves the same |
| `tools/computer_check.py` | (build 179) COMPUTER PLAYS through the headless window's own path at each SPEED against `route_probe --computer`: every command at the same tick, the story, report, checkpoints and slots; `--python` the Python driver in the window too; `make computer_check` |
| `tools/playthrough.py` | (build 98) The whole game from a new game to the ending by story.py's stages: a seed (a player's pauses), lean grinding, a stage retried from its checkpoint; a report per seed under `route_out/playthrough_s<N>/`; (build 114) `--seeds 1-12 --jobs 12`: each seed its own run, as `--seed` plays it; (build 118) squeak-by by default -- every level target a level or two lower, a failed try's retry after a top-up of the latest grind (docs/harness.md, "Squeak-by"); `--no-squeak` for the old targets; (build 168f) `--language KEY` in that language, into `route_out/playthrough_s<N>_<KEY>/`, and with `PS2_TEXT_CHECK=1` the report lists each line too wide for its window (docs/harness.md) |
| `tools/walk_trials.py` | The walks across Noah from Lutz's YES to Dark Force's box and Mother Brain's corridor, many seeds per fight policy: reached, wiped out, fit for the boss, TP and mates left (build 97); (build 114) `--jobs N` runs at once, the results the same |
| `tools/jobs.py` | (build 114) The harness's runs in worker processes, their results in order: what makes `--jobs` change nothing but the time |
| `tools/proofs/` | (build 128) `proof.sh NAME [JOBS]`: CLAUDE.md's whole proof set into /tmp/proofs/NAME ((142a) `PROOF_COLOUR=1` adds colour_check over every route); `qproof.sh NAME`: Q's checks; `compare.py OLD NEW`: two sets compared, what `diff` and `cmp` did; (142a) `server_proof.sh [REF]`: a commit's whole proof, unattended, on a machine with no display or GPU, against the last commit's (docs/machines.md) |
| `tools/measure/` | (build 128) The whole-game and endgame measurements of builds 115-119: `scratch_tree.sh`, `whole_games.sh` and `summarize_games.py`, `endgame_setup.sh`, `endgame_run.sh` and `summarize_endgames.py`, `computer_plays_speed.sh` |
| `tools/setup_msys2.sh` | (build 128) A Windows machine's MSYS2 packages, and Q at the commit last proved (docs/windows_build.md) |
| `tools/make_dist.py` | (build 121) `make dist`: the game to hand on, dist/ps2-windows/ and its zip (the Build section) |
| `tools/make_pack.py` | (build 125) The pack `make dist` links into the game: assets/, shaders/, what COMPUTER PLAYS reads ((179) routes/saves/ and script_ids.h; the Python driver until 178) |
| `tools/make_icon.py` | (build 123) The game's icon, Rolf's portrait: assets/icon/rolf_portrait.png from the new_game route's shot, then ps2.ico |
| `tools/art_dump.c` | (build 127) The art dump's recorder: every battle animation played by the port's own battle objects (one translation unit with game.c); `make art_dump` |
| `tools/art_dump_plan.py`, `tools/adm.py`, `tools/art_export.py` | (build 127) The art dump's plan (every enemy, weapon and battle technique), its captures read, and the export: every animation, portrait, cut-scene and field sprite with its palette; `make art` runs the lot; (158) the cut-scenes are assets/art/scenes' pictures, copied (the shots of pictures are RGB since 146a, with no palette to draw tiles under) |
| `tools/ground_map.py` | (build 102a) Motavia's map with each encounter region's group and the Paseo/Arima grinds' ground dotted (`route_out/mild_ground/`) |
| `tools/grind_ledger.py` | (build 103) playthrough.py with a ledger of ticks and levels after every fight (`route_out/playthrough_s<N>/ledger.json`); `--summary` gives each grind's levels a 100 k ticks and its dry tail |
| `tools/grind_trials.py` | (build 103) One grind policy (Oputa's field, the lab's entrance cell, the lab's long walks) from a save for a tick budget: EXP a member a 100 k ticks, ticks a fight, wipe-outs |
| `tools/callgraph.py` | Static control-flow map: calls, screen transitions, dispatch tables (writes `route_out/callgraph.json`) |
| `tools/rom_left.py`, `tools/rom_left_review.tsv` | (build 181) ps2.asm's 68000 code split into routines, followed from the vectors and every address taken, each held against what src/ cites; the review is the reading of every routine src/ does not cite; `route_out/rom_left/` (docs/rom_left.md) |
| `tools/crt_ref.c` | (build 105) The CRT chain's CPU reference: `make crt_check` checks it by numbers (route_out/crt_reference/checks.txt); `crop` renders a route PNG at 1080p, 1440p, 4K and 3840x2400 (docs/crt.md); (107) `make crt_sim_check`: the GPU passes' CPU mirror against the model (sim_check.txt) |
| `tools/crt_gpu_check.c` | (build 107) SDL3, `make crt_gpu_check`: the read-back check -- the SDL_GPU chain drawn on this machine's GPU, read back and held against the model and the mirror, every crop and the flat picture (route_out/crt_gpu_check/report.txt; docs/crt.md); (108) the HDR10 and scRGB crops and flat picture, and where a FAIL's codes lie |
| `src/shaders/*.hlsl` | (build 107) The CRT chain's shaders: one triangle, the beams down, across with the mask and the encode, the flat picture; (build 141a) the down and flat ones (`crt_down_rgb`, `crt_sharp_rgb`) take the frame's colour plane. `make shaders` compiles them with SDL_shadercross into `shaders/` (SPIR-V, DXIL, MSL), which is checked in |
| `src/platform/crt_gpu.[ch]` | (build 107) The chain on SDL_GPU: the frame and palette as textures, crt.c's tables, the passes; shared by the game and `crt_gpu_check` |
| `tools/display_probe.c` | (build 105) SDL3 only, `make display_probe`: what SDL says about the window, the displays, the GPU devices and HDR, windowed and fullscreen, as `PS2_DISPLAY` words too (docs/crt.md) |
| `tools/fm_demo.c` | Diagnostic: writes a WAV of instruments from the game's FM bank (build 136d: `music/fm_instruments.json`) |
| `tools/fm_probe.cpp` | (build 111) One sound through the chip and through Q in each voicing: pitch tracks, partials, envelopes, spectra over time, the click at a strike under a tail, folded images, what the music loses under an effect (`make fm_probe`, Q_DIR; docs/audio.md, "Build 111"); (129b) the chip as the game's chip backend, `--chip limited|extra|both`, and `--steal` split into the music's loss and the effect's cut (docs/audio.md, "Build 129b") |
| `tools/drum_hits.cpp` | (build 110b) Each drum struck alone through the chip and through Q's drums: level, fall time, energy in four bands |
| `tools/psg_demo.c` | Diagnostic: writes a WAV exercising every PSG channel |
| `tools/audio_level_check.py` | (build 109a) `make audio_check`: the mix's level through the game's own mixer -- the headless title theme's peak between -12 and -1 dBFS, nothing at full scale (docs/audio.md, "The mix's level"); (110) the chip by name, and `make audio_check_q`: Q's the same and within 3 dB of the chip's |
| `tools/q_demo.cpp` | (build 109) The preliminary test of Cycfi's Q, C++20 and optional (`make q_demo`, `Q_DIR`): one song's notes through Nuked-OPN2 and re-voiced with Q, two WAVs side by side; `--solo`, `--instrument`, `--bench` (docs/audio.md, "The Q test"); (110) links the Q backend; `--drums synth\|samples`, `--trim-pass` |
| `src/core/ps2_types.h` | Fixed-width typedefs, screen constants, tick rate |
| `src/core/palette.[ch]` | CRAM words, measured DAC ladder, fades, colour cycling |
| `src/core/video.[ch]` | Tile planes, sprites, indexed compositor; (build 140) the colour plane; (build 141) the colour path -- the pictures' colours, the glow, the fades as colour maths -- and the `video_*` palette operations, each made on both paths; (build 142) pictures bound to the tiles they were made from (`VideoPicture`): a bound pixel's colour from the picture, its index from the tile, and the frame's `pic` plane saying which; (151) a binding's reveal mask, the pixels a dissolve has not put in |
| `src/core/art.[ch]` | (build 142) The pictures (docs/asset_format.md): a picture's description and its full-colour PNG, and the portraits' index (`assets/art/portraits/portraits.json`), loaded with the rest at startup; (146) the library graph's picture, and a picture's slot map where an effect recolours it; (147) the cut-scene pictures (`assets/art/scenes`); (149) a thing's frames by number |
| `src/core/tileset.[ch]` | Indexed-PNG tile-sheet loader |
| `src/core/rng.[ch]` | Direct port of `GenerateRandomNumber` |
| `src/core/winmap.[ch]` | window and portrait maps: read from their Tiled `.tmx` (build 134) and drawn |
| `src/core/tmx.[ch]` | (build 134) Tiled maps: embedded tilesets and their integer properties, a CSV layer, gids with the flip flags |
| `src/core/png.[ch]` | Inflate and indexed-PNG reading, no dependencies |
| `src/core/pngwrite.[ch]` | (build 105) PNG writing: the route probe's indexed frames (moved from route_probe.c, the same bytes) and `crt_ref`'s RGB crops, 8-bit or 16-bit with a cICP tag |
| `src/core/crt.[ch]` | (build 105) The CRT simulation's model: the CRT page's settings, what a display can have (CrtDisplay, the availability rules), and the CPU reference of the whole chain phase 2's shaders must match (docs/crt.md) |
| `src/core/level.[ch]` | Block maps, block definitions, filling the planes |
| `src/core/nametable.[ch]` | VDP nametable loader (the `.tmx`, build 135) and drawing; (147) a cut-scene picture bound over a table's cells; (149) over one frame of a stacked table; (151, 152) through a reveal mask; (154) over one of a table's tile ranges |
| `src/core/table.[ch]` | Small inline data tables that drive screen effects |
| `src/core/script.[ch]` | A block of window text as extract_script.py writes it, by script id |
| `src/core/credits.[ch]` | (build 136e) `credits_load`: the ending's credits, `assets/script/ending_credits.txt`, back into the credits writer's bytes |
| `src/core/assetfs.[ch]` | Every asset file, preloaded into RAM |
| `src/core/json.[ch]` | Small JSON reader for saves and data tables |
| `src/core/keys.[ch]` | (build 160) The controls: two keys and two gamepad inputs for each of the pad's eight inputs, their names (settings.json's) and labels (as each pad prints them), the defaults, binding, and the keys and gamepad inputs held made the pad's buttons -- Herzog Zwei's port's, with this game's actions and defaults |
| `src/core/psg.[ch]` | SN76489: 3 tone channels + noise, 4-bit attenuation |
| `src/audio/fm.[ch]` | YM2612 backend around Nuked-OPN2 |
| `src/audio/drums.h` | (build 130) The eight DAC drum bytes by name (`DRUM_SNARE`, ...) and `drum_name()` |
| `src/audio/instruments.[ch]` | (build 136d) `instruments_load`: `assets/music/fm_instruments.json` read back into the 26 bytes a piece SetFMIns sends, for `fm_load_instruments` (fm_demo, q_demo) |
| `src/audio/synth.[ch]` | (build 110) The synth backends behind one table -- CHIP (fm.c), Q when built -- chosen at start-up and switched between ticks from the AUDIO page (docs/audio.md, "The Q backend") |
| `src/audio/patches.[ch]` | (build 112) The patch file's reader: `assets/music/q_patches.json`, hand-voiced instruments for Q by the bank's number, each resolved against the ROM's (docs/audio.md, "The patch file") |
| `tools/q_patch_template.py` | (build 112) An instrument's ROM values in the patch file's shape, to start a patch from |
| `tools/patches_check.c` | (build 112) `make patches_check`: the patch file's reader on its edge cases (`tools/patches_test.json`), and the game's file listed |
| `tools/controls_check.c` | (build 160) `make controls_check`: src/core/keys.c on its own (Herzog Zwei's hz_controls.c with this game's defaults): every gamepad input's name and every pad style's labels, every key's name in the game's font, the defaults, random bindings against a model, the sticks, triggers, hats and the menu's directions at their edges |
| `src/audio/q/q_synth.[ch]` | (build 110) The Q backend: the sequencer's writes re-voiced with Cycfi's Q, C++20 behind extern "C", (build 121) in the game wherever Q is found (`Q=auto`); drums synthesised or the ROM's samples |
| `src/audio/smps.[ch]` | The SMPS68k sequencer: streams, tick, coordination flags |
| `src/core/audio.[ch]` | Mixer with separate music and SFX buses; (build 109a) its gains and soft knee in `src/audio/mix.h`; (110) the backends, the register shadow and the switch, the AUDIO page's volumes |
| `src/game/ui.[ch]` | The opening crawl's message box and text |
| `src/game/lang.[ch]` | (build 163) The languages (docs/languages.md): the list, the chosen one's folder in place of the game's files, its names and its proportional face (measured and drawn into tiles) |
| `src/game/window.[ch]` | The window system: queues, stack, scripts, cursors |
| `src/game/intro_select.[ch]` | NEW GAME / CONTINUE / ERASE GAME |
| `src/game/intro_start.[ch]` | The dream that opens a new game |
| `src/game/walk.[ch]` | The party on the map: grid steps, collision, doors, camera, demo input, followers; the jet scooter's main routine |
| `src/game/npc.[ch]` | Townspeople (Motavians, Roron's Motas, Dezolians, musk cats): placement, wandering, hit test, talking |
| `src/game/menu.[ch]` | `ProcessPlayerMenu`: the C menu on the map -- ITEM, STATE, TECH, STRNG, EQP |
| `src/game/battle.[ch]` | `ProcessRandomBattle`, `BattleScreen`, `BattleScreen_CheckRoutine` (standby, fighting, run), EXP and level-ups, the battle render |
| `src/game/battle_obj.c` | The fight's objects: `Obj_CharactersBattle`, `Obj_EnemiesBattle`, the effect objects and `Battle_AnimateSprites` |
| `src/game/battle_tables.h` | The animation, weapon and technique tables the two share |
| `src/game/sysmenu.[ch]` | The port's own Escape menu: continue, controls, display, fullscreen, (build 105) the CRT and HDR pages, (110) the AUDIO page, COMPUTER PLAYS, (160) the GAMEPAD page and the CONTROLS and BUTTONS pages (the keys and gamepad inputs bound), (163) LANGUAGE, quit |
| `src/game/rumble.[ch]` | (build 160) The rumble: a watcher main.c runs after every tick -- the screen, the party's HP, the ids the game asked the sound for (`audio_take_asked`) -- and the shakes it starts, scaled by RUMBLE; the gamepads' motors through `platform_rumble`. (build 161) A shake that follows the effect the driver started (`Smps.sfx_started`, `sfx_starts`, `smps_sfx_tracks`) by its loudness while its tracks play -- a blow's sword and red flash, an explosion, a hole, the ship's departure and landing; the game's motion (`game_motion`: the flight, the pause, the building, the scene's shake) |
| `src/game/ending.[ch]` | The ending screen: the epilogue pages, the strip wipe and the last picture, the credits and their palette fade, the ending UFO (build 93a) |
| `src/game/building.[ch]` | `Building_CheckRoutine` and the buildings: the roof and its spaceship, Governor, Rolf's house at game start, shops, hospital, clone lab, Data Memory, teleport station, Central Tower desk, Ustvestia's house, Kueri's gum inventor |
| `src/game/gamedata.[ch]` | Save state, new-game reset, JSON save files; (build 104) the `$C600` block's bytes by name (`GD_*`, docs/game.md) |
| `src/game/game.[ch]` | State machine and per-tick logic |
| `src/platform/platform.h` | The only OS-facing interface |
| `src/platform/platform_sdl3.c` | SDL3 window; (build 107) drawn through SDL_GPU (crt_gpu.c) when the shaders are in `shaders/`, else the SDL_Renderer SDR placeholder; (build 105) high pixel density, `platform_display_info` |
| `src/platform/platform_headless.c` | No display; scripted input, PPM output |
| `src/platform/home.[ch]` | (build 123) Where the game's files are, and where every save goes (`saves` beside the game, else the user's data folder; PS2_SAVES) |
| `src/platform/pack.S` | (build 125) The pack linked into `make dist`'s game (`.incbin`), read by assetfs.c |
| `src/platform/ps2.rc` | (build 123) The Windows game's icon (windres) |

## Build

```sh
make            # needs SDL3 via pkg-config; (141d) also compiles this
                # machine's CRT shaders when missing or stale (shadercross)
make headless   # core + game only, builds anywhere
make shaders    # (build 107) the CRT shaders, every format, SDL_shadercross
make shaders_dxc  # (build 113) the DXIL alone, with Microsoft's DXC
make q_demo     # (build 109) optional, C++20 and Cycfi's Q (Q_DIR, ~/GIT/Q)
make Q=1        # (build 110) the game with the Q backend (C++20, Q_DIR)
make dist       # (build 121) dist/ps2-windows/ and its zip, to hand on
make art ROM=ps2.bin  # (build 127) the art dump: ../ps2_art and ps2_art.tar.gz
make roundtrip ROM=ps2.bin  # (build 130a) assets/ against the ROM; (185) ROM and PS2_DIR
make roundtrip-edits ROM=ps2.bin  # (build 131) each JSON table's edit caught at both levels
```

(build 110) `make Q=1` (and `make headless Q=1`) link the Q backend
(`src/audio/q/`, C++20, `Q_DIR` as for q_demo) and the C++ runtime
(`Q_LDLIBS`, `-lstdc++`; `-lc++` with clang's libc++). (build 121) The game
takes Q by default: `Q=auto` builds `ps2` with it wherever Q is found
(`Q_DIR`, else `~/GIT/Q`, else on Windows `GIT/Q` in the user's folder) and
a C++ compiler answers, and says so and builds without it anywhere else;
`Q=0` leaves it out. `make headless` takes Q only for `Q=1`, and `make
route_probe` never: they and plain `make` still build with neither a C++
compiler nor Q. Switching Q= does not rebuild by itself: `make clean`
between.

(build 125) `make dist`'s game carries its assets, shaders and COMPUTER
PLAYS's files inside itself (tools/make_pack.py, the pack; docs/history.md,
Build 125), so the package below is only the game and the papers (and
python/ in builds 122-178); what follows about assets/, shaders/ and
tools/ beside it was before 125.

(build 121) `make dist` (tools/make_dist.py): `dist/ps2-windows/` and
`dist/ps2-windows.zip`, the game to hand to someone. On Windows the game in
it is linked statically (SDL3, the C and C++ runtimes, winpthreads, iconv),
one `ps2.exe` with no DLL beside it and no console window, with Q where
`Q=auto` finds it. Beside it: `assets/`, `shaders/`, what COMPUTER PLAYS
runs (five of `tools/`, `routes/new_game.route`, `routes/saves/`,
`src/game/script_ids.h` for the logs' names), a README.txt, and
`licenses/` for what is linked in (Nuked-OPN2 and iconv are LGPL-2.1, so the
source goes with it; SDL3 zlib, Q its own (Boost before Q 1.5, MIT from 1.5:
the clone's LICENSE, copied), the GCC runtime and winpthreads
theirs). (builds 122-178) On Windows it carried python.org's embeddable
Python in `python/`, which COMPUTER PLAYS ran before any on the PATH;
(build 179) COMPUTER PLAYS is the C player inside the game, and the
package carries no Python and needs none installed. The
package's text files, `source.zip`'s too, have the maintainer's name put as
"the author", and `source.zip` leaves out the working notes (CLAUDE.md,
NOTES, and the old questions and handover files in `docs/old/`). (build 161a) Every entry of both zips is dated HEAD's commit time in UTC. Elsewhere `make dist` links as `ps2`
does (a Linux Flatpak is for later). (build 138a) `source.zip` is `git
archive` of HEAD in a git repository; in a tree the tarballs are unpacked
over it is the tree's own files, less build/, dist/, saves/, route_out/,
objects and programs. Off Windows `licenses/LGPL-2.1.txt` is the system's
copy (Arch's licenses package, Debian's common-licenses), and a package
without one is still made. (build 138b) The packaged game reads a file in
`mods/` beside it (or in the user's data folder) in place of its own, and
writes its own out to `mods/originals/` on first start to copy from:
docs/mods.md.
`make audio_check_q` builds `ps2_headless_q` (always with Q) and checks its
level.

(build 107) The game reads its compiled shaders from `shaders/` at startup
(run from the tree's top, as always). Until `make shaders` has written them
-- or with no SDL_GPU device, or `PS2_GPU=0` -- it draws through
SDL_Renderer as before, and the CRT page's settings show but are not drawn;
the startup line `display: ...` says which. `make shaders` needs
SDL_shadercross (`SHADERCROSS=/path/to/shadercross` if it is not on the
path; `SHADER_FORMATS="spv msl"` to leave out DXIL where the tool cannot
write it -- (build 142) the default on Linux, where Arch's shadercross
asserts on DXIL; `SHADER_FORMATS="spv dxil msl"` asks for all three). The files it writes are checked in, so a build never needs a
shader compiler. `PS2_GPU_DEBUG=1` asks SDL_GPU for its debug layer.
(build 113) `make shaders_dxc DXC=/path/to/dxc.exe` writes the DXIL alone
with Microsoft's DXC, for a Windows machine with no shadercross: its release
zip, with dxcompiler.dll and dxil.dll beside dxc.exe so the DXIL is signed.
The HLSL names SDL_GPU's register spaces itself, so DXC needs nothing else.
Its DXIL is not shadercross's byte for byte (the two drive DXC differently)
but passes the same checks. On Windows SDL_shadercross itself builds from
its source with MSYS2's CMake, Ninja and SPIRV-Cross (`pacman -S
mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
mingw-w64-ucrt-x86_64-spirv-cross`) and DXC's release zip unpacked as its
`external/DirectXShaderCompiler-binaries`: `cmake -S . -B build -G Ninja
-DCMAKE_BUILD_TYPE=Release && cmake --build build`, with dxcompiler.dll and
dxil.dll beside the shadercross.exe it makes (build 113).

(build 128) Windows from nothing -- MSYS2's packages, Q, the shaders'
compiler, the sanitizers, and what the tree carries and what it fetches:
docs/windows_build.md (`tools/setup_msys2.sh` installs the packages).
(build 135a) macOS: docs/macos_build.md -- Homebrew's SDL3, Q against
libc++ (the Makefile's default there), Metal reading `shaders/*.msl`;
built and checked on an Intel Mac with MacPorts in build 149a (the
routes, the CRT checks, 105 of 105 on Metal), `make dist`'s package not yet.

(build 100b) Both link `src/harness/` and `-lpthread`: COMPUTER PLAYS is
POSIX (posix_spawn, pipes, pthreads). (build 179) And `src/player/`: the
C player runs on the command thread inside the game, and the child process
and its pipes are only the Python driver's (`PS2_COMPUTER_DRIVER=python`). (build 115) And Windows: the driver
started with CreateProcess, its pipes handed over by handle, Python found on
the PATH as `python3.exe`, `python.exe` or `py.exe`, the thread winpthreads'
and its lock Windows's own (docs/computer_plays.md, "On Windows"); ps2.exe
and ps2_headless.exe then import libwinpthread-1.dll, beside SDL3.dll in
MSYS2's ucrt64/bin. (build 105) On Windows `autoplay.c` compiled to a stub
instead -- COMPUTER PLAYS showed POSIX ONLY and never started, until build
115 -- and main.c no longer calls `access()`, so the game builds with
MSYS2's UCRT64 GCC and SDL3 (`pacman -S mingw-w64-ucrt-x86_64-gcc
mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-pkgconf make`, then `make`
from the UCRT64 shell). Built and run on Windows by the owner (build 105).
(build 113) `route_probe`, `crt_ref`, `crt_gpu_check`, `patches_check` and
the Python harness run there too, with MSYS2's own Python (`pacman -S
mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-python-pillow`, run as
`python3` from the UCRT64 shell): every route passes and the proofs hold
(docs/history.md, Build 113). The files the game, `route_probe`, `crt_ref`,
`crt_gpu_check` and the Python tools write are the same bytes on every OS
("\n", never "\r\n"); stdout is the console's, "\r\n" on Windows. (113a)
The Makefile quotes the paths it is given (`Q_DIR`, `PS2_ASM`, `PS2_CONST`,
`ART_DIR`, `PCM_ASM`, `SHADERCROSS`, `DXC`), so one with a space works as it
is -- except that the constants file's lookup beside `PS2_ASM` splits at a
space, so name `PS2_CONST` too -- and `make clean` removes the `.exe` files.
(114a) `route_probe_asan` does not build with MinGW's GCC, which has no
sanitizer runtime; it builds with MSYS2's CLANG64 clang (`pacman -S
mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-compiler-rt`, then `make
route_probe_asan` from the CLANG64 shell) and runs with `/clang64/bin` on
the PATH, where its runtime DLLs are.

`make extract` (or `make assets`) regenerates every extracted asset and
the manifest from the cartridge image; `make extract-<name>` reruns one
extractor -- `make extract-npcs`, `make extract-battle`, ... (the names
are `EXTRACTORS` in the Makefile) -- after which `make manifest`. They
are always rerun, so no `-B` is needed. (build 185) They read the image
itself, *Phantasy Star II (USA, Europe)* Rev 02, raw (786,432 bytes, SHA-1
`0711080e968490a6b8c5fafbb9db3e62ba597231`), and no disassembly:

```sh
make extract ROM=path/to/ps2.bin
make extract-npcs ROM=path/to/ps2.bin
```

Where everything is in the image, and the disassembly's name for it, is
`tools/rom_map.json`: ps2.asm's labels in the order of its lines with
their addresses, the drum file's, the `binclude`d art files and the
constants (`tools/rommap.py` reads the image through it). When the
disassembly changes, `tools/make_rom_map.sh PS2_DIR ROM WORK_DIR` assembles
it for its listing, checks the result is the ROM byte for byte, and
writes the map again. The extractors read the disassembly until build
184; `make roundtrip`'s packing back, `tools/door_audit.py` and
`tools/rom_left.py` still do (`make art` reads the image, `ROM=` as for
extract).

(build 186) `python3 tools/make_assets.py IMAGE` does what `make extract`
does with no make, and a game started without its files runs it itself:
a window asks for the image -- dropped on it, chosen, or named on the
command line -- and shows the steps (docs/first_start.md). The JAPANESE
language is added the same way, from the Japanese cartridge and
Excalibur_Z's retranslation, when LANGUAGE is closed on it.

Before build 55 there was no `assets` target at all: `make assets` found
the `assets/` directory, had no rule for it and did nothing, even with
`-B`.

```sh
PS2_FRAMES=90 PS2_OUT=frame.ppm ./ps2_headless
```

(build 160) `PS2_GAMEPADS=N` connects N gamepads to the headless game
(their buttons are `PS2_PAD`'s, the one script) and `PS2_RUMBLE=FILE` logs
their motors, a line `FRAME PAD LOW HIGH` a change -- what
`tools/rumble_check.py` reads. `PS2_RAW="N:NAME,N:PAD:NAME,..."` presses
keys (by keys.c's names: Q, SPACE, BACKSPC ...) and gamepad inputs (PAD:L2,
PAD:LSUP, PAD:JOY3 ...) as they are at frame N, through the Escape menu's
controls as the window's are, and `PS2_PAD_STYLE=ps3` (or xboxone, switch
...) is whose labels the gamepads wear -- what `tools/controls_check.py`
drives the CONTROLS and BUTTONS pages with; `PS2_FRAME_HZ=120` (or 30)
runs the frames at another rate than the game's, so a frame is worth no
tick or two (docs/harness.md).

## Design decisions

**No VDP emulation, but an indexed pipeline.** There are no VRAM addresses,
control-port write pairs or DMA queues anywhere. What is kept is 8×8 tiles
of 4-bit palette indices with four palette lines, because the original
implements every fade, flash and colour cycle as arithmetic on palette
entries — those routines port directly, and the CRT chain needs indices
plus a palette to recover phosphor levels. (build 140) The frame is also
composed as colour (the Framebuffer's `rgb` plane, written beside every
index), the first step toward full-colour art (docs/palette_effects.md,
docs/asset_format.md). (build 141) The colour plane is the colour path's
own -- the pictures' colours, the effects' glow, the fades as colour maths
on each pixel -- and the one shown; the palette path is kept beside it as
the reference (`PS2_DRAW=palette`), every route's frames drawn both ways
and the same.

**Everything resident.** The whole game's art is a few MB uncompressed, so
assets load once at startup. The original's VRAM juggling, the art upload
sequencing and `DecompressArt`/`DecompressArt2` all disappear.

**Fixed 60 Hz logic tick.** `GenerateRandomNumber` is called from
`VBlankExit` every frame, so the RNG sequence is a function of elapsed
frames. Every animation counter also assumes one update per field. The tick
rate is load-bearing, not cosmetic.

**16-bit arithmetic preserved.** Game logic uses `u16`/`s16` typedefs from
`ps2_types.h` rather than native `int`. Some formulas depend on wrapping at
word boundaries; widening is a decision to make deliberately, in one place.

**The headless target is a test, not a curiosity.** If it stops building,
something platform-specific has leaked into the core. It also gives
deterministic frame output for diffing against a reference.

## Assets in RAM

Nothing is read from `assets/` after startup. `core/assetfs.[ch]` reads
every file named in `assets/manifest.txt` into memory before anything else
runs -- about 700 files, 2 MB -- and the loaders that used to `fopen`
whenever a screen, a level or a door asked for something (PNG tiles and
palettes, Tiled maps, block sets, JSON, scripts, collision tables) now
take their bytes from that store. Entering a level is a decode out of
memory; it does not wait on a disk.

This was not true before build 29. Title, font, dream and window art was
loaded once at startup, but every screen entry reloaded its palettes from
PNG, every level change read a manifest, a map, a block set, a tile sheet,
a collision table and a door list, and portraits and scripts were read the
first time they were wanted.

`make manifest` (also the last step of `make extract`) regenerates the
list; run it after adding or renaming an asset. A file that is asked for
and was not listed still works -- it is read on the spot, kept, and
reported once on stderr -- so a stale manifest costs a disk read, not a
crash. The startup line `assets: 713 files, 2.1 MB held in RAM` says what
was loaded.

One format is left out of the manifest because its loaders run once at
startup and decode straight into their final structures: `.bin` (tables,
sound data). The window maps' `.map` (until build 134) and the nametables'
`.nam` (until build 135) were left out the same way; both are Tiled maps
now, listed like the rest. Save
files under `saves/` are not assets and are always read from disk.

The store holds files, not decoded objects, so the assets stay what they
are on disk -- edit a PNG or a `.tmx`, restart, and it is in the game.
(build 138b) A copy in the `mods/` folder is read in place of the game's
own -- (138c) in every build, the packaged game's (whose files are inside
it) and the tree's: docs/mods.md.

## Asset formats

Everything made of pixels is an **indexed PNG**. PNG colour type 3 stores
an index plane plus a palette, which is what the hardware stored: one
file carries both, it opens in any image editor, the palette is editable
in the same window as the art, and the index plane loads with no
unpacking and no colour conversion -- it is already one byte per pixel,
which is what `Tile` wants.

| What | How |
|---|---|
| Tilesets | indexed PNG, a grid of 8x8 tiles 16 across, in tileset order |
| Art that is a rectangle | indexed PNG at its real size |
| Palettes | indexed PNG, 16 wide, one row per line; the PLTE *is* the palette |
| Colour ramps | (build 136) indexed PNG as wide as the table's rows; each pixel's colour, in reading order, is a word |
| The dissolve order | (build 136a) JSON, `tables/dissolve_order.json`: 64 pixel positions `[x, y]` |
| The drums | (build 136b) `music/drums.json` and `music/drums/<sample>.wav`, 8-bit mono PCM, the ROM's bytes |
| The note frequencies | (build 136c) JSON, `music/note_frequencies.json`: each entry's block and F-number, named |
| The FM instruments | (build 136d) JSON, `music/fm_instruments.json`: each register's fields named, operators as rows of four; the upload order |
| The credits | (build 136e) text, `script/ending_credits.txt`: a line of the file a line on the screen, case for colour, tags for the gaps |
| The songs and effects | (build 137) text, `music/sounds.txt`, `envelopes.txt`, `effects.txt`, `songs/<name>.txt`: notes by concert name, state as words, labels for jumps; the game builds the driver's image from them (137a). (139b) Each song also as a tracker, `songs/<song>.trk`, which the game reads in its stream text's place |
| Block definitions | `blocks_<tileset>.txt`, one block per line, 16 hex entries |
| Block maps | Tiled `.tmx`, CSV layers, gid = block id + 1 |
| Window maps | (build 134) Tiled `.tmx` over the tile sheets themselves; a cell's byte is its tile in its tileset plus the tileset's `byte_base` |
| Screen nametables | (build 135) Tiled `.tmx` over the art sheets themselves; a cell's word from its tileset's `vram_base`, `palette_line` and `priority` and Tiled's flip flags |

**Names (build 97b).** A file is named for what it is, never by a ROM
label or a table index: the palettes by what loads them
(`assets/palettes/level_motavia_towns.png`, `portrait_lutz.png`,
`battle.png`, `ending_page_3.png`), the level tilesets by the levels on
them (`tileset_towns.png`, `blocks_motavia_dungeons.txt`), the level
sprites' art by the objects drawn from it (`sprite_motavian_old_man.png`,
`sprite_dezolis_chest.png`), the tile animations by level, and the art
files whose ROM names were abbreviations (`armor_seller_portrait.png`,
`title_logo_art.png`). The names live in one table,
`tools/asset_names.py`, which every extractor that writes one of these
reads; the JSON beside each keeps the ROM's label (`"source"`,
`"symbol"`) for checking against ps2.asm. Code loads a palette by its
PalettePtrs index through `palette_load_entry` and a `PALETTE_*` name
(`src/game/rom_ids.h`), and a level's tileset by the file names its record
in `levels.json` gives.

`src/core/png.[ch]` is a self-contained inflate plus an indexed-PNG
reader, about 500 lines and no dependencies, so the headless target still
builds anywhere. It handles colour type 3 at bit depths 1, 2, 4 and 8,
non-interlaced, which is what editors write for indexed images. Verified
against PIL on filtered and stored streams at every depth.

A CRAM word is three bits per channel, and what lands in the PNG is that
run through the measured DAC ladder. `cram_from_rgb8` inverts it by
nearest level; the eight levels are far enough apart that the round trip
is exact, and a colour someone picks that the hardware cannot make snaps
to the nearest one it can.

Block definitions are deliberately **not** an image. A block is 4x4 VDP
nametable entries, and priority, palette line and the two flips are not
things a picture can carry. `blocks_<tileset>.png` is the same data rendered at
32x32, which is what Tiled lays maps out with and what makes a level
editable by dragging forests around; it is a view, not the source.

No container is left: the window maps (build 134) and the screen
nametables (build 135) are Tiled maps, and the `.bin` files that held the
ROM's bytes behind a four-letter tag were made readable one kind at a time
(build 136 on): the colour ramps, the dissolve order, the drums, the note
frequencies, the FM instruments, the credits, then (build 137) the sound
driver's data, every song and effect, as text (`music/SoundData.bin`, the
last, is gone in 137a).

**The songs and effects (build 137, 137a).** `assets/music/sounds.txt` (the
sound ids, their priorities, the song files to read, how the instruments
are stored, where the song streams sit), `envelopes.txt` (the four vibrato
envelopes by name), `effects.txt` (every effect: its tracks' headers, then
its streams) and `songs/<name>.txt` (a song's tempo line, its tracks'
headers, its streams in blocks; songs that share a passage are one file).
A stream reads `instrument phantasy_mid`, `C#6 6  C6  C#6  G#5 30`, `tie
24`, `C5+E5+G5 12` (chord mode), `C5 12 slide +3`, `kick 6`, `loop L 4
times`, `goto L`; notes are concert names from note_frequencies.json (byte
$81 is B0), and a `# heard:` comment follows a line whose notes the
transpose (or GetFMFreq's six-bit wrap) moves, written by `python3
tools/song_text.py annotate` and never read. The streams are read as the
sequencer reads them -- raw-frequency, pitch-slide and chord modes,
loops with their counters -- so every byte has one meaning; bytes no stream
reaches are `bytes`. The game builds the driver's image from them
(`src/audio/songtext.[ch]`, `smps_load(d, music_dir)`), the instruments
from fm_instruments.json by name; the unedited text gives the ROM's image
byte for byte, 23593 bytes. It refuses, naming the file and line, a word
or name it does not know, a number out of range, a label given twice or
missing, a block not placed. `make extract-music` writes them, checked to
build back to the image first; `make roundtrip` packs them at level 2
(tools/song_text.py's assemble). docs/audio.md, "The song text", has the
format in full.

(build 139b) **The songs as trackers.** `make extract-music` also writes
every song laid out as a tracker, `songs/<song>.trk` (sections of rows,
every channel side by side, notes as heard; docs/audio.md, "The tracker
format"), and the game reads a song's `.trk` in place of its stream text:
edit the `.trk`. Which file a song comes from, first found: a `.trk` in the
mods folder, a `.txt` there, the game's `.trk`, the game's `.txt`. With
every song from its tracker the image is 31,836 bytes (every repeat written
out); the chip writes are the stream text's, every song, frame for frame.
`make roundtrip` checks at level 2 that each plays as the ROM's song.

**The credits (build 136e).** `assets/script/ending_credits.txt`, beside
the window text: what the credits writer (loc_787E) scrolls up over the
stars, a row every eight steps. A line of the file is a line of the
credits as it lands on the screen, 40 cells across -- its leading spaces
the indent (the ROM's blank runs, a byte for up to 20 cells), every other
space one blank cell, so the text sits where it shows. Capitals are drawn
in white (palette line 0) and lower case in the titles' blue (line 1): the
alphabet has capitals only, so the case is the colour, as in the ROM's
bytes. Two blank rows follow a line; `{close}` at a line's end leaves one
and `{section}` 28, the gap between sections; `{empty}` alone is a line
with nothing on it and `{end}` alone stops the credits, held until a
button. The file's header says the same for whoever opens it. `credits_load`
(src/core/credits.[ch]) turns it back into the writer's bytes, which
ending.c's writer reads as before, and refuses, naming the line, a
character that is not a letter or a space, a tag it does not know or out
of its place, a line wider than 39 cells (the writer counts 40 a row and
a 40th runs its count out, writing on past the row), a line after `{end}`,
no `{end}`, and more than 4096 bytes. `make extract-tables` writes it
(checked to give the ROM's 441 bytes back first) and deletes
`EndingCredits_Script.bin`; `make roundtrip` packs it back at level 2. The
table containers' readers (`table_load_bytes`, `table_load_words`) went
with it.

**The FM instruments (build 136d).** `assets/music/fm_instruments.json`,
the instruments FMInsPtrs points at and the order the upload routine
(SetFMIns) writes their 26 registers in (the table at loc_BA818).
`"upload_order"` names each register by the fields it holds --
`feedback_algorithm`, `pan_lfo`, and an operator's `opN.detune_multiple`,
`total_level`, `rate_scaling_attack`, `am_decay`, `sustain_rate`,
`sustain_level_release`. `"instruments"`, 105, each `number` (its place:
the number a song's header or instrument command names), `algorithm`,
`feedback`, `pan` (center, left, right, none), `lfo_am_sensitivity`,
`lfo_fm_sensitivity`, then the operator fields as rows of four, operators
1-4 in the chip's numbering (register offsets +0, +8, +4, +C): `multiple`,
`detune`, `total_level`, `attack_rate`, `decay_rate`, `sustain_level`,
`sustain_rate`, `release_rate`, `rate_scaling`, `lfo_am` (true or false).
38 instruments set bits the chip does not read (bits 6-7 of
feedback/algorithm, the top three of a sustain rate, ...); they are kept
as `"ignored_bits"`, register name to bit numbers, so the bytes are the
ROM's, and make no sound. Nine of FMInsPtrs' entries name one instrument
and two instruments share their last four bytes with the next: the file
has one instrument a number, read from the image as SetFMIns reads it.
(build 137) Each has a `"name"` (docs/instrument_names.md's), what the song
text calls it. (build 137a) The game reads it: the song text's instruments
come from here (`instruments_load_named`), as do fm_demo's and q_demo's
(`fm_load_instruments`, src/audio/instruments.c's `instruments_load`). It
refuses, a name that is not one word or is given twice, names on some
instruments but not all,
naming it, an `upload_order` that is not the 26 register names once each,
no instruments or more than 256, a `number` that is not its place, a field
missing, not a whole number or out of range (`lfo_am` true or false, `pan`
one of the four), a row that is not four, and an ignored bit on no
register, past 7 or one the chip reads. `make extract-music` writes it;
`make extract-tables` deletes `FMInstruments.bin` and
`FMRegisterOrder.bin`; `make roundtrip` packs it back with the JSON
tables. In the manifest since build 137a.

**The note frequencies (build 136c).** `assets/music/note_frequencies.json`,
FMFreqs: `"entries"`, 85 of them, each a YM2612 frequency word as `block`
(the octave) and `fnum` (the F-number), which are what `smps_load` reads,
and for reading only `note`, `hz` and `cents`. GetFMFreq adds the track's
transpose to a note byte, keeps the low six bits and reads that entry, so
entries 0-63 are reachable; 0 is silence and 64-84 are in the ROM but never
read. `note` is the nearest concert note (A440): the table is
equal-tempered about a quarter of a semitone sharp, entry 1 being B0 + 26
cents, and smps_trace names notes the same way (until 136c it called entry
1 C0, a semitone high and an octave low). `smps_load` refuses, naming the
entry, fewer than 64 or more than 128 entries and a block past 7 or an
F-number past 2047. `make extract-music` writes it and deletes
`FMFreqs.bin`; `make roundtrip` packs it back with the JSON tables.

**The drums (build 136b).** `assets/music/drums.json` and six WAVs in
`assets/music/drums/` (kick, snare, tom, open_hat, closed_hat, unused): each
WAV is a sample's bytes as the ROM stores them, 8-bit unsigned mono PCM,
unhalved (the Z80 halves each as it plays); its own rate is a label, the
first drum's that plays it. drums.json gives each sample's file and its
`z80_address` in the 8 KB of Z80 RAM (from $118, past the program), and
each drum its `byte`, `name`, `sample`, `length` (the bytes played) and
`delay` (the Z80's djnz count: a byte every 240 + 13 x delay cycles of
3579545 Hz) -- the three toms are the tom sample at delays 2, 4 and 8.
`drums_load` (src/audio/drums.c) rebuilds the drum table and RAM image every
backend plays from, so the chip and Q play exactly what they did; $90 still
reads 513 bytes of its 18-byte sample, into RAM nothing wrote, as in the
ROM. It refuses, naming it, a WAV that is not 8-bit mono PCM, a sample over
the program, past RAM or over another, a drum byte outside $80-$FF or given
twice, a drum naming no sample, a delay outside 1-255 and a drum playing
past RAM. `make extract-drums` writes them (from the ROM's Z80 block since
build 185; `PCM_ASM`, the Z80 file, before) and deletes `PCMDrums.bin`;
`make roundtrip` packs them back.

**The dissolve order (build 136a).** `assets/tables/dissolve_order.json`,
FadeEffectArray: `"order"` is 64 pixel positions `[x, y]`, eight to a line,
the order a dissolve in reveals each tile's pixels (FadeArtFill takes the
ROM's byte as y * 8 + x: the word byte >> 2, the pixel byte & 3 in it),
all the range's tiles at once a step -- the title's figures and logo, the
dream pictures on Tyler's monitor, a battle's enemies. The dissolve out
(loc_6C54) steps the first 32 and clears the two-pixel byte each falls in.
`table_load_dissolve` refuses, naming it, a count other than 64 and a
position that is not two whole numbers 0-7. `make extract-tables` writes
it and deletes `FadeEffectArray.bin`; `make roundtrip` packs it back with
the JSON tables (roundtrip_json.py).

**Colour ramps (build 136).** `assets/palettes/sega_logo_sweep.png` (31x1)
and `ending_credits_fade.png` (4x9): tables of CRAM words a routine copies
into the live palette a piece at a time -- the Sega logo's sweep slides an
11-word window along its one row (`PaletteCycle`), the credits' fade puts
one row of four into line 3, entries 9-12, a step (loc_6AE0), the last row
(black) first. The picture is the ramp: `table_load_ramp` reads each
pixel's colour in reading order, to a word by the nearest DAC level, so
editing a pixel's colour, or the PLTE entry it points at, edits the word.
It refuses, naming it, a pixel whose index is past the PLTE. One ROM word
is not a colour a PNG can carry: the fade's eighth row holds `$0100`, bit
8 over black, written as black -- it is in the live palette for eight
steps, only shown, never faded (roundtrip's STRAY_BITS_CHECKED). `make
extract-tables` writes them (names in `tools/asset_names.py`'s
RAMP_NAMES) and deletes the two `.bin` files they were; `make roundtrip`
packs them back.

**Window maps (build 134).** `assets/winart/<name>.tmx`, one a window,
menu or portrait (`.map` containers until then). Each tileset is one of the
tile sheets under `assets/tiles/` itself -- 8x8 tiles in tile order, which is
how Tiled lays a tileset out -- with a `byte_base` property: a cell's map byte
is its tile's index in that sheet plus `byte_base`. Read through each
tileset's own firstgid, so the map survives Tiled renumbering its tilesets
when it saves. A window's tileset is `FontsIconsArt` (`loc_980A` draws a byte
as tile `$500` + byte, where the sheet sits); a portrait's frame rows use it
too and its picture the portrait's own art, from `$A0` for Rolf and the
library's graph (their art sits at `$4A0`), from 0 for the rest. (build
156) The portraits' sheets are not kept: Tiled shows a portrait window's
frame and leaves its picture empty (the portrait is
`assets/art/portraits`). A cell must
be set, unflipped and inside a tileset; `winmap_load` says which cell when
one is not. The sheets carry a grey ramp, not a palette, so Tiled shows the
windows and portraits in grey. `make extract-winart` writes them (after
`art`, whose sheets they name); `make roundtrip` packs them back.

**Screen nametables (build 135).** `assets/nametables/<n>.tmx`, one a
full-screen or framed picture: the Sega logo, the title's two planes, the
three dream pictures, Nei's two, Lutz, Tyler and his monitor (five 12-row
frames stacked in one map), the flight's planet, the battle floor, Mother
Brain's star field, and the ending's star field, six pages and last
picture (`.nam` containers until then). One CSV layer, `nametable`; each
tileset is an art sheet under `assets/tiles/`, the one the picture's tiles
come from in video memory, with three integer properties. (build 156) The
sheets of the art the pictures replaced are not kept: those tilesets name
an image the tree does not hold, so Tiled shows their cells empty -- the
picture under `assets/art/scenes` is the file to look at and edit. A cell's VDP word
is `priority << 15 | palette_line << 13 | V << 12 | H << 11 | (vram_base +
the tile's index in its sheet)`, H and V from Tiled's flip flags, so
flipping a tile in Tiled flips it in the game. A tileset is one sheet in one
palette line (and priority); a picture using a sheet in two lines would
carry it twice. An empty cell is tile 0 in the map's own
`empty_palette_line` -- 0, the word `$0000`, everywhere but Mother Brain's
star field, whose gaps the ROM stores as tile 0 in line 3 (`$6000`); tile 0
is blank, so both draw nothing. Firstgids are read, never assumed.
`nametable_load` refuses, naming the cell, a diagonal flip (the VDP has
none), a tile in no tileset or past `$7FF`, and a tileset without
`vram_base` or with a line or priority out of range. Tiled shows them in
grey, the sheets themselves. `make extract-nametables` writes them (after
`art` and `inline_art`, whose sheets they name; it needs `ART_DIR`), and
deletes stale `.nam` files; `make roundtrip` packs them back.

## Art format

`DecompressArt` (ps2.asm:8600) and `DecompressArt2` (8658) share one
format, decoded in `tools/ps2art.py`. One 32-byte tile per iteration:

```
count : u8      0x00        whole tile is literal bytes
                0x01..0x7F  that many run groups follow
                0x80..0xFF  end of stream
run group, x count:
    value : u8      byte to store
    mask  : u32be   one bit per tile byte, MSB first; set bit stores value
literals:
    one byte for every bit still clear in the OR of all masks
```

Groups may overlap and a later one wins. Masks and literals together cover
all 32 positions exactly once, so the tile buffer is never cleared. The two
routines differ only in destination: `DecompressArt` writes to the VDP data
port, `DecompressArt2` to `(a4)+`.

All 52 art blobs decode with zero trailing bytes.

## Reading the control flow first

`tools/callgraph.py` extracts what is statically recoverable from
`ps2.asm` before any porting: 7445 routines, 856 direct `bsr`/`jsr`
edges, and the 32 `move.b #ScreenID_X, (game_screen).w` writes that are
the real screen state machine.

    python3 tools/callgraph.py ps2.asm --screens
    python3 tools/callgraph.py ps2.asm --calls TitleScreen

What it cannot follow is the 85 computed jumps -- `jsr
GameScreenTable(pc,d0.w)`, `ObjCB_Index`, `BattleScreen_EventIndex` and
the rest. Those name a table, not a target, so the edges that carry the
most structure are exactly the ones a call scan loses. The tables are
adjacent data and mostly recoverable by hand, but each needs its extent
and its index source worked out.

And structure is not behaviour. The graph says SegaScreen calls
PaletteCycle; it does not say PaletteCycle slides a window over a ramp
rather than rotating entries. That still has to be read.

## Screen sequence

`GameInit` sets `game_screen` to `ScreenID_Sega` and `MainGameLoop`
dispatches through `GameScreenTable` (ps2.asm:151). The port follows the
same shape: each screen has an entry step (the part the routine does once,
before its wait loop) and a per-tick step.

`SCREEN_SEGA` places SegaLogoArt at VRAM tile 0, draws SegaLogoTileInd
12x4 at the position `$461C0003` encodes, runs `demo_timer` down from
`$12C` frames and cycles the palette every `$28`. Start skips it.

Screen entry clears all four scroll registers and the sprite table
(`ClearSpriteAndScroll`, ps2.asm:5D90) and the VRAM tile array first, matching the DMA fill to
zero each screen routine opens with (ps2.asm:10640). Skipping it leaves
art revealed on a previous visit resident, so a re-entered title screen
shows its figures and logo already drawn until the dissolve blanks them.

Every screen transition fades: 21 frames of `PaletteFadeOut` (one
intensity step per entry per frame) before the next screen loads, then 21
frames of `PaletteFadeIn` up to its palette. Because the fade-in runs
before the screen's own timer starts, the SEGA screen's `$12C` frames
begin around frame 321 rather than 300.

`SCREEN_TITLE` is a four-phase sequence, not one state. It places
TitleScrBGArt at tile 1, TitleScrCopyrightArt at `$300` and FontsIconsArt
at `$500`, draws both 40x28 nametables, and runs `demo_timer` down from
`$B4`. Then the figures dissolve in (287 tiles at `$E1`), then the logo
(256 tiles at `$200`), each over 64 steps of 4 frames. Finally it waits
`$6CC` frames: Start at any point goes to `ScreenID_Intro`, timing out
goes to `ScreenID_Level` and the Motavia opening.

**Two title objects are ported directly**, `ObjID_CopyrightText` and
`ObjID_PushStartButton5`, as sprites with fixed mappings. Both are
spawned at `TitleScreenLoop` (ps2.asm:10765), which is entered only
after the two dissolves, so neither is on screen for the title's first
692 frames. The prompt is hidden until any button other than Start is
held -- pressing Start leaves the screen -- which is why it is so
rarely seen.

Both dissolves are `FadeArtTiles`: `FadeEffectArray` is a permutation of
0..63 giving the order in which pixel positions of every tile are
revealed, so stepping it 64 times fills the art in.

The SEGA logo's sweep is `PaletteCycle` (ps2.asm:9226) -- a sliding window
over `CyclingPal_SegaLogo` (`assets/palettes/sega_logo_sweep.png`), not a rotation. Every four frames the byte
offset drops by 2 and 11 words land in CRAM entries 2..12; the sweep stops
when the offset passes zero, which is why it finishes well before the
screen's 300 frames are up.

The tileset mirrors VRAM tile space, so nametable entries index it
directly and each screen places its art at the index its load address
implies -- the same arrangement the original relies on.

## Controls

Genesis three-button pad: A `$40`, B `$10`, C `$20`, Start `$80` -- four
distinct masks, nothing redundant. (build 160) Every one of the pad's
eight inputs -- the four directions, A, B, C and Start -- has two keys and
two gamepad inputs, each re-assigned in the Escape menu (CONTROLS for the
keys, GAMEPAD's BUTTONS for the gamepads) and kept in
`saves/settings.json`. The defaults:

| | Up Down Left Right | A | B | C | Start |
|---|---|---|---|---|---|
| KEY 1 | arrow keys | Z | X | C | Enter |
| KEY 2 | W S A D | L | ; | ' | -- |
| gamepad, 1ST | d-pad | West (Square; X on an Xbox pad) | South (Cross; A) | East (Circle; B) | Start |
| gamepad, 2ND | the left stick past half its travel | -- | -- | -- | -- |

Until build 160 the CONTROLS row picked one of two key sets, ARROWS (the
first line) or WASD (the second, the D-pad under the left hand and the
three buttons under the right, the way the pad sits); both are live at
once now. The gamepads' A, B and C are the left, bottom and right face
buttons, as the Mega Drive pad's run left to right. Escape, and a
gamepad's Back (Select, Share, View), always open the Escape menu and are
never bound; Backspace neither (it empties a slot being set).

(build 160) **Gamepads** drive the pad alongside the keys: up to two, in
the order they were connected, both through the one set of bindings --
any gamepad SDL3 knows (a DualShock 3 or 4, a DualSense, an Xbox or Switch
pad), and the buttons, first hat (as the d-pad) and first two axes (as the
left stick) of a joystick it has no mapping for (an arcade stick, an old
USB pad). `gamecontrollerdb.txt` beside the game (SDL's community file of
mappings, or a line made for one pad) adds mappings, and the game says on
stderr how many it read. Any of a pad's buttons, its d-pad's directions,
its sticks' directions (past half their travel) and its triggers (past
half) can be bound; the BUTTONS page draws each as the pad in hand labels
it (CROSS on a DualShock, A on an Xbox pad, B on a Switch pad, for the
same bottom button), positional names (SOUTH, EAST ...) with none
connected.

**The CONTROLS and BUTTONS pages.** A row an action, two slots in each
(KEY 1 and KEY 2; 1ST and 2ND), then DEFAULTS, which sets that page's half
back (the keys, or the gamepads). Up and down pick the row, left and right
the slot; C, A, Start or Enter on a slot listens, the slot blinking: the
next key (CONTROLS) or the next gamepad input on any gamepad (BUTTONS)
goes there, and off any other slot it was in; Escape or Back keeps the slot
as it was, Backspace empties it, anything else is not heard. The arrow
keys, Enter, and every gamepad's d-pad and left stick always work the
menu, whatever is bound; B, as bound, and Escape and Back go back.
`saves/settings.json` keeps them by name: `"keys": {"up": ["UP", "W"],
..., "start": ["RETURN", "-"]}` and `"pad": {"up": ["DUP", "LSUP"], ...,
"start": ["START", "-"]}` (`"-"` an empty slot; a name nothing has keeps
that slot's default, saying so on stderr). A file from before build 160,
with `"controls": "arrows"` or `"wasd"`, reads as the defaults and is
written in the new form from then on.

Not yet tried on a real gamepad (none was connected while it was made):
the bindings are proved through the headless game's scripted presses.

**The rumble** (src/game/rumble.h): a gamepad with motors shakes for what
happens to the party -- a member hurt in a fight, felt as the blow lands
(the hurt sound, the target blinking), harder for a bigger share of the
member's HP; HP lost on the map any other way (Roron's cake); a member
falling; the party lost; a damage floor's bite (a buzz with its white
field). (Build 161) A blow lands with a quick buzz of the small motor, then
shakes as long as its sound plays, following its loudness, with a fainter
echo through the red flashes; so do the explosions (the Darum scene, the
Climatrol, Gaira) and a fall through a hole; the Climatrol's shaking
screen shakes the pad; the spaceship's flight, and Tyler's ship, throb
slowly, stilled by the pause, with a jolt at the landing. The GAMEPAD page
sets how strongly. A DualShock 3 on Windows needs
a driver that gives it to Windows as a gamepad: DsHidMini (Nefarius's,
free; its control app wants Microsoft's .NET 10 Desktop and ASP.NET Core 10
runtimes). In the control app, the pad's Configure, Settings mode Custom,
then SXS: the game takes it through SDL's own PS3 driver (build 160a),
with its PlayStation names and its rumble. DsHidMini's default, XInput,
works too, as an Xbox pad with the Xbox names (its PS button is then the
Guide button, which Steam takes for itself unless Steam's "Guide Button
Focuses Steam" is off). On Linux it works as it is.

**Escape** opens the system menu (`src/game/sysmenu.[ch]`) rather than
quitting. Its options, top to bottom:

| Option | C, A or Enter | `saves/settings.json` |
|---|---|---|
| CONTINUE | closes the menu | |
| CONTROLS | (build 160) opens the CONTROLS page, the keys (Controls, above), and shows no value; until 160 it toggled between two key sets | `"keys"` (until 160 `"controls"`) |
| DISPLAY | steps the pixel aspect (Display aspect, below) | `"display"` |
| FULLSCREEN | (build 100) borderless fullscreen on the display the window is on, or back to the window; the frame placed as in any window (docs/engine.md, Fullscreen); (112a) the mouse cursor hidden while fullscreen | `"fullscreen"`, a file without it windowed |
| CRT | (build 105) opens the CRT page; the value says what will be drawn (OFF, SCANLINES, GRILLE, SLOT MASK, SHADOW MASK). The page: SCANLINES (OFF, ON; builds 159 to 166 had INTERLACED and FLICKER too, taken out in build 167: the game sends 240p, docs/crt.md "Interlaced scanlines"), MASK (OFF, APERTURE GRILLE, SLOT MASK, SHADOW MASK), SUBPIXELS (RGB, BGR, OTHER), and HDR, whose C opens the HDR page: HDR (ON, OFF), PEAK (AUTO, 400 ... 10000 nits), WHITE (the game's white, 50-1000 nits). Left and right step a value, C steps on, Escape or B goes back a page (and closes the menu from the menu itself). A value this display cannot have says why on the line under it: NEEDS FULLSCREEN, NEEDS 480P, NEEDS 1080P, NEEDS 1440P, DESKTOP SCALED, NO HDR. Phase 1 (build 105) kept and showed them; (build 107) the SDL_GPU chain draws them in SDR, each switched off where the line under it says why; (build 108) HDR ON where the window has HDR draws through an HDR10 swapchain (else scRGB; `PS2_HDR_SWAPCHAIN=pq` or `=scrgb` picks) in nits by PEAK and WHITE, the flat picture at WHITE too, and follows HDR switched on or off in the desktop (docs/crt.md, "Phase 3") | `"crt"`: `scanlines` (`true` or `false`; (build 167) builds 159-166's `"interlaced"` read as `true`), `mask`, `subpixels`, `hdr`, `peak` (`"auto"` or nits), `white` |
| AUDIO | (build 110) opens the AUDIO page; the value is the synth that plays (CHIP, Q). The page: SYNTH (CHIP, Q -- NOT BUILT under Q where the build has none, and the chip plays), MASTER and MUSIC (OFF, -30 dB ... 0 dB, 0 the default), (build 111) EFFECTS, the same: the sound effects apart from the music (docs/audio.md, "Build 111"), (111a) OVERSAMPLE (1X, 2X, 4X): the rate Q's PM voices run at, Q ONLY under it while the chip plays; (112) VOICES (EXTRA, LIMITED: effects take the music's channel as on the chip; (build 124) on the chip too, EXTRA a second chip for the effects, docs/second_chip.md), TAILS (FADE, RING: a note struck again rings out its release), PATCHES (ON, OFF: the patch file), each Q ONLY while the chip plays (docs/audio.md, "Build 112"); (116) GRAIN (OFF, ON: the chip's output grain on Q), and the page's rows two apart, Q ONLY beside the value (docs/audio.md, "Build 116"). Left and right step, C steps on and goes round, Escape or B back. A switch while a song plays hands the new synth the register shadow, so a held note keeps sounding (docs/audio.md, "The AUDIO page"). `PS2_SYNTH=chip\|q` picks one for a run | `"audio"`: `synth`, `master`, `music`, (111) `effects` (dB or `"off"`), (111a) `oversample` (1, 2, 4), (112) `voices` (`"extra"`, `"limited"`), `tails` (`"fade"`, `"ring"`), `patches` (true, false), (116) `grain` (true, false); a file without it the chip at 0 dB, 4, extra, fade, patches on, grain off |
| GAMEPAD | (build 160) its value is how many gamepads are connected (NONE, 1 PAD, 2 PADS); C opens the GAMEPAD page: PAD 1 and PAD 2, each connected gamepad's name (NONE: none) -- C on one buzzes it, to tell which is which -- and RUMBLE, OFF, LOW, MEDIUM (the default) or HIGH, every gamepad buzzing at the new level as it changes; left and right step it and stop at the ends, C goes round, Escape or B back; BUTTONS, last, opens the BUTTONS page, the gamepads' inputs (Controls, above). (Above QUIT, below SPEED, in the menu) | `"rumble"`: `"off"`, `"low"`, `"medium"`, `"high"`; a file without it MEDIUM; `"pad"`, the BUTTONS page's |
| COMPUTER PLAYS | (build 100b-100c) C lights or puts out its lamp; closing the menu with it lit, the harness plays the campaign in the window from where the value says -- left and right: NEW GAME, a slot of yours that holds a save (read, never written), or (build 103a) LAST RUN, where the computer saved last; NO TOOLS when it cannot run ((build 179) a file the C player reads is missing; NO PYTHON3 only for the Python driver, `PS2_COMPUTER_DRIVER=python`). While it plays, Escape opens this menu over the held game; closing it with the lamp out hands the pad back, and no other key does anything. Its saves and report are `saves/computer/` (docs/computer_plays.md) | |
| SPEED | (build 100b) COMPUTER PLAYS's speed: NORMAL (60 Hz) or FAST (ticks unthrottled, a frame every 4096 ticks or 15 ms, silent); (build 120) C steps NORMAL, AUTO, 2X, AUTO 2X, FAST: 2X two ticks a frame, AUTO and AUTO 2X FAST while the computer grinds; (build 162) FAST but for the story's beats -- scenes, the plot's words, bosses, the ending -- and (162a) the gear it buys (docs/computer_plays.md, Speed); (build 169a) between the beats AUTO 32 ticks a frame and AUTO 2X 64, every frame drawn, silent | `"computer_speed"`: `"normal"`, `"auto"`, `"2x"`, `"auto_2x"`, `"fast"` |
| QUIT | leaves the game | |

It is
the port's, not the ROM's, so it is drawn to pass: the game's frame
tiles, its fill, its font, the lamp cursor on the seven-frame blink
`ObjRRC_Main` uses, and the window colours (PalettePtrs entry 2, `field_common.png`, line 0). Those
colours are not written into the game's palette: the menu draws with
framebuffer indices 64-79, which `video_compose` never produces, and the
platform looks those up in `Framebuffer.overlay` (`video.h`). Up to
build 47 it swapped line 0 of the live palette instead, which recoloured
everything on screen drawn in line 0 -- on the fight screen, the floor
grid and the window greys. While it is open
nothing below it runs -- no screen logic, no fades, no pad -- but the
music plays on (build 112b: so the sequences nobody plays -- the SEGA logo,
the title, the attract crawl, the ending -- play on under it too, the pad
withheld, in time with their music): stopping the sequencer leaves whatever is keyed on
ringing (the pause jingle exists to mute first), and Start already
pauses. Escape or B closes it; C, A or Enter picks. It is drawn straight
into the composed frame after the planes and sprites (`game_overlay`),
not into a plane: on the map the planes carry the camera's pixel
scroll, a sprite does not, and build 41 drew the box in plane A and the
lamp as a sprite, so the two drifted apart by whatever the scroll was
when the menu opened. `esc` is a token the headless pad script accepts.

**Presses reach exactly one tick.** The platform reports presses per
frame; free-running (a 120/144 Hz display, vsync off) a frame can carry
two ticks or none, so a single press used to count twice -- a cursor
stepping two stops, a window confirmed and the next one with it -- or
vanish before a tick saw it. `main.c` now accumulates presses until a
tick consumes them (41a).

The title screen tests `ButtonStart_Mask` only (`andi.b
#ButtonStart_Mask, (joypad_pressed).w`), so Start is the only key that
advances it. An earlier build also accepted Z and C there; that was
invented, not from the game.

## Display aspect (build 54)

The Mega Drive's pixels are not square. In H40 (`$8C81`, ps2.asm:8480)
the VDP clocks them at the master clock / 8, about 6.71 MHz, where a
pixel square on an NTSC set would take about 6.14 MHz: on a TV each of
the 320 is narrower than it is tall, 32:35. Scaling 320x224 the same
both ways, as the port did until build 54, draws the picture at 10:7 --
about 9% too wide, round things a little flat.

DISPLAY in the Escape menu picks the pixel aspect, and
`saves/settings.json` keeps it as `"display"`:

| Menu | `settings.json` | Pixel | Picture | |
|---|---|---|---|---|
| SQUARE | `square` | 1:1 | 1.43 | the old look |
| 4:3 | `4:3` | 14:15 | 1.333 | the frame stretched to 4:3, as most emulators show it |
| TV | `tv` (default) | 32:35 | 1.306 | what the TV drew |

4:3 is a touch wide of the TV because a set's 4:3 also held the border
around the 224 lines. A file without the key -- any from before build
54 -- reads as TV.

`platform_sdl3.c` places the frame itself now, not through
`SDL_SetRenderLogicalPresentation`. Up and down it is still the largest
whole multiple of 224 that fits, so every line is the same height; across
it is that many lines at the chosen aspect, rounded to a screen pixel
(3x: 960, 896 or 878 wide; 4x: 1280, 1195 or 1170). A 32:35 pixel cannot
be drawn nearest -- some columns would come out a screen pixel wider than
others -- so the frame is scaled nearest to the next whole multiple of
320 above that width, in a render target, and linear from there down to
it: each pixel stays sharp with at most one blended column at its edge.
SQUARE skips the second step and is exactly what it was. The window
still opens at 960x672, so at TV and 4:3 it has black bars at the sides
until it is resized.

The headless platform ignores the setting: its PPM is the raw 320x224
frame. The CRT chain, when it lands, gets the aspect for free by
spreading the 320 pixels over the active line, as the TV did.

## Input and threading

Input edges are **latched from SDL events**, not sampled from
`SDL_GetKeyboardState`. Polling a snapshot loses any press shorter than a
frame, and when the loop catches up several ticks at once every tick sees
the same snapshot. The original read the joypad once per VBlank into
`joypad_pressed`, which is a latch, so latching matches it and stops
dropping taps. `Input` carries both `held` and `pressed`.

Audio runs on SDL's own thread. Register changes cross from the game
thread through a single-producer, single-consumer ring (`AUDIO_QUEUE`)
rather than being applied in place -- writing PSG state directly from the
game thread while the audio thread reads it is a data race that mostly
works and occasionally tears a period value.

Palette fades pause screen logic, matching the original's blocking
`PaletteFadeFrom` / `PaletteFadeTo`. Input keeps being latched throughout,
so a press during a fade is still delivered when logic resumes.

## Window maps

Every window and portrait is a `WinArt_*` block in `ps2.asm`: a rectangle
of tile indices, usually introduced by `border N, $B9` where N is the width
in tiles. 80 of the 84 extract cleanly. Portraits are 10x12 -- a $B9 border
row, ten rows of art, a $BE border row -- and their indices may start at a
nonzero base ($A0 for Rolf).

Tiles repeat within a map, and the compressed art stores only unique tiles.
That is why the blobs have counts like 85, 87 and 89 rather than neat
rectangles, and why the map is required to assemble them.

Window text is stored as font tile indices via the game's CHARSET
directives (ps2.asm:25327): 'A' is tile 11, 'a' is 37, '0' is 1, space is
0. `\I` inside these strings is the double-quote character (AS's
escape; tools/extract_script.py decodes it since build 129a).

**Window widths.** A WinArt map's width is the window's INTERIOR: its
rows carry no left/right border bytes, only the $B9 and $BE spans, so
the frame is drawn around the map and the box on screen is two columns
wider. CHARSET code 0 (space) is not a blank glyph either -- it means
"leave the cell alone", since glyph_base + 0 is an item icon.

**Selection cursor** uses the game's own art. `$B4`+`$B5` are a 16x8
indicator housing whose 10x4 interior is index 11, the same index as
the window fill, so an unlit lamp vanishes into the background. The lit
lamp is a separate sprite: `ObjRRC_Init` sets `move.w #$85B6, 8(a0)`
(priority 1, palette 0, tile `$5B6`), and since FontsIconsArt loads at
`$500` that is tiles `$B6`/`$B7` of the sheet. Index 8 is the lit face,
9 the shadow down the right and bottom edges, 3 the highlight in the
upper left. It blinks on the 7-frame timer `ObjRRC_Main` uses.

Verified: `rolf_portrait` and `rudo_portrait` assembled from extracted art
plus extracted maps are pixel-identical to externally produced references,
0 of 6400 palette indices differing.

## Not done yet

(build 129) Gone through against docs/history.md, the docs/ pages and the
source; what was done is at the end with the build that did it. The
working list of what comes next is NOTES_next_context.md's latest "Next".

- **Full colour (decided after build 136d).** The art moves off palettes
  and tile sheets altogether: pictures in their real colours, every
  palette effect (fades, the logo sweep and credits fade, colour cycling,
  swaps and flashes such as Mother Brain's) rebuilt as a colour effect, the
  palette path kept callable as a reference -- a frame drawn both ways and
  compared is the proof -- and unused once replaced. First a read-only
  survey of every routine that writes the palette (docs/palette_effects.md),
  then the renderer, the effects, and the assets a kind at a time, the
  baked pictures below first. The survey is done (build 138,
  docs/palette_effects.md), the owner took its picks, and step 1, the
  reference fixed -- the four places the palette path differed from the
  ROM -- is build 138d; next, step 2: the colour plane beside the index
  plane, every route's frames drawn both ways (done, build 140); step 3,
  the effects on the colour plane and the colour path shown, is build 141;
  the CRT shaders on the colour frame next (141a); (build 142) the
  portraits drawn from full-colour pictures, proved against their tiles
  (asset_format.md's order, 2); (146a) their tiles and palettes no longer
  loaded -- the portraits and the library graph are their pictures alone,
  with slot maps where an effect recolours them (146); (147) Lutz the
  first cut-scene picture, his tiles and palette not loaded; (148) the
  flight's planet, Nei's pictures, the battle floor and the ending's
  pages too; (149) the SEGA logo, the ending's stars and Tyler's monitor,
  with slot maps; (150) the dream's waking; (151) the dream's fight and
  Tyler's help, dissolved through a reveal mask; (152) Tyler and his
  monitor through theirs; (154) the title's two planes and game over's
  background; (155) the stars behind Mother Brain; (156) their sheets out
  of the tree, the extractors reading the disassembly's art. (build 139c) The format the
  art moves to -- full-colour pictures the game reads, a description a
  thing, an optional HD set -- is proposed in docs/asset_format.md, for
  The owner's answers before any code.
- **Baking the rectangles.** Portraits, battle art and the title screen
  are stored as unique tiles plus a map only because the ROM
  deduplicated repeats to fit VRAM, a budget that no longer applies.
  They can assemble flat at extraction time -- the assembly is already
  verified pixel-identical -- which would retire `winmap.[ch]` for those
  and make a portrait a picture of Rolf rather than a contact sheet.
  (The art dump, build 127, exports them flat; the game still reads the
  tiles and maps.)
- **Sprite mappings to an atlas.** `BuildSprites` reads each object's
  mapping as a count byte then that many records of y offset, size, tile
  and x offset, which is a frame atlas in 1989 vocabulary. Converting it
  to an atlas PNG plus a frames JSON is a real translation rather than a
  repackage, and it is where animation lives.
- **Round-trip verification, the rest.** `make roundtrip` (build 130a)
  packs the tile sheets, pictures, palettes, block definitions, Tiled maps,
  collision tables and scripts back and compares them with the ROM's
  bytes; (build 131, 131a) the JSON tables of the stores, techniques,
  gamedata, buildings, interactions and npcs too; (131b) the level
  records (the `.tmx` properties and levels.json, as the game reads them;
  the per-level `.json` is read by nothing and said so); (131c)
  battle.json, the level animations and map patches, the palette entries
  and the script banks -- every JSON file is packed or listed as level 1
  only (the port's own, or read by no loader: songs.json,
  nametables.json, palettes.json, winart.json, the per-level `.json`). The
  compressed art is compared decompressed. Packing it back into the ROM's
  compressed form is not wanted (The owner, build 134) and is not a goal.
- **CRT chain.** Halation, curvature and NTSC artefacts are not planned
  (docs/crt.md). Open from build 108: PEAK above what SDL reports, SDR
  white on Wayland, SUBPIXELS cut to RGB and BGR, the P16's panel.
- **Linux packaging.** `make dist` links as `ps2` does there ((build
  179) COMPUTER PLAYS needs no Python anywhere); a Flatpak when wanted.

Done since this list was written, and where:

- The first start makes the game's files (build 186): a copy without
  them opens a window that takes the player's cartridge image (dropped,
  chosen, or on the command line), runs tools/make_assets.py and starts
  the game; JAPANESE without its files is offered the same way, from the
  Japanese cartridge and the retranslation. docs/first_start.md;
  docs/history.md, Build 186.

- The extractors read the cartridge image (build 185), not the
  disassembly: `make extract ROM=...`, through tools/rom_map.json (the
  disassembly's labels, files and constants with their places in the
  ROM) and tools/rommap.py. The same assets but two files, in bytes
  nothing reads. README, "Build"; docs/history.md, Build 185.

- Mods' chords of up to 16 notes (build 184), the owner's choice:
  past the Mega Drive's five music channels, a chord's notes get voices
  of their own -- chord chips on the chip synth, voices on Q -- made only
  for the chords a mod's music has; the game's own music is unchanged, to
  the bit. A 17th note is refused. docs/audio.md, "Chords past the chip";
  docs/mods.md; tools/chords_check.py.

- The music fade-out's overrun (build 183), the last thing the ROM count
  of build 181 left: the fade copies the chord's voice one track too far,
  onto the first effect track, as FadeOutMusic does. Once a game, at
  Gaira's control panel, the explosion's last 0.4 s is 2.7 dB quieter
  (two times in three) or 4.6 dB louder (one in three, by where Mystery
  is in its loop). Routes fade_out_overrun and
  fade_out_overrun_second_chord; docs/audio.md, "The fade-out's overrun".

- The dead out of the walking line (build 182): each slot of the party's
  line on the map draws the next member above 0 HP, and the slots past the
  living are hidden, as RenderCharSprites does -- with Rolf dead, Nei
  leads and three walk. Route walk_line_dead; docs/history.md, Build 182.

- How much of the ROM is left, counted again (build 181): no routine of
  the 68000 code is left unported; of 19,169 instruction lines, src/ cites
  the routines holding 16,682, and the other 2,487 were read -- 1,816
  ported under other names, 616 replaced by the port's design, 55 never
  run. docs/rom_left.md; the early "~18k" was most likely the whole.

- The palette survey (build 138, read-only): every routine that writes the
  palette, by kind, with the port's function for each and how full colour
  does it -- docs/palette_effects.md.
- The sound driver's data, `music/SoundData.bin`, the last `.bin` (builds
  137, 137a): every song and effect and the driver's tables as text
  (`music/sounds.txt`, `envelopes.txt`, `effects.txt`, `songs/`), which the
  game builds its image from (src/audio/songtext.c) and `make roundtrip`
  packs back (23593 of 23593 bytes). Task A is done.
- The credits' `.bin` container (build 136e):
  `script/ending_credits.txt`, a line of text a line on the screen, packed
  back by `make roundtrip` (441 of 441 bytes). The last table container.
- The FM instruments' and register order's `.bin` containers (build
  136d): `music/fm_instruments.json`, the registers' fields named, packed
  back by `make roundtrip` (2756 of 2756 bytes). Read by fm_demo and
  q_demo, and (build 137a) by the game, through the song text.
- The colour ramps' `.bin` containers (build 136): the Sega logo's sweep
  and the credits' fade are pictures under `assets/palettes/`, packed
  back by `make roundtrip` (2 of 2). The first of the `.bin` files made
  readable; the rest are NOTES_next_context.md's latest "Next".
- The note frequencies' `.bin` container (build 136c):
  `music/note_frequencies.json`, packed back by `make roundtrip` (170 of
  170 bytes).
- The drums' `.bin` container (build 136b): `music/drums.json` and six
  WAVs, packed back by `make roundtrip` (the Z80 RAM image and drum table,
  the same).
- The dissolve order's `.bin` container (build 136a): `tables/
  dissolve_order.json`, 64 `[x, y]` positions, packed back by `make
  roundtrip` (64 of 64 bytes).
- The screen nametables' `.nam` containers (build 135): Tiled maps over
  the art sheets, flips as Tiled's flip flags, packed back by `make
  roundtrip` (22 of 22). The last custom container.
- The window maps' `.map` containers (build 134): Tiled maps over the tile
  sheets, packed back by `make roundtrip` (84 of 84).
- A pressed mid-step on foot (build 133): the port held it back to the end
  of the step; ProcessAButtonPress answers it every frame, against the box
  ahead of the leader's pixel position, which the port's hit test already
  was. With it, ProcessWindows' camera test: a window starts only with the
  main camera on the 16px grid, so one asked for while the screen scrolls
  opens as the step ends (routes `talk_mid_step`, `window_mid_scroll`).
- Townspeople whose line starts a scene (checked in build 132a): ObjType_People
  sends four dialogue numbers to scenes of their own, all ported in game.c --
  $85 Roron's cake (loc_E5FE), $87 the jet scooter's maker's gift (loc_E690),
  $88 the Esper mansion's door Espers (loc_E05A), $8F the two Espers on B1
  (loc_E07A); the rest from $80 up go to loc_E5D0, ported too.
- The four JSON listings no loader opens (songs.json, nametables.json,
  palettes.json, winart.json; build 132a): kept in assets/ for the tools,
  left out of the preload and of `make dist`'s pack (make_manifest.py's
  NOT_READ_FILES; manifest 786).
- Dark Force's and Mother Brain's pictures' bottom rows, and battle frames
  of more than sixteen pieces (build 131c): the extractor cut a frame at
  $400 bytes, battle.h kept 16 pieces (route `battle_big_frames`).
- AC SLASHR's trailing space (build 131a): the extractor stripped it, the
  ROM's dialogue prints it (route `ac_slasher_name`, by the new `expect
  text`).
- The Control Tower's ground floor's last six stairs up (build 131): the
  port kept 64 doors a level, the ROM's list has 70 (docs/game.md,
  "Dungeon floors"; route `tower_ground_stairs`).

- Drum names (build 130): the toms by the disassembly, the snare, kick and
  two hi-hats by what the samples measure as; `DRUM_*` in
  `src/audio/drums.h` (docs/audio.md, "Drums").

- The window palettes: every window cell carries the palette line from
  its window's tile word (window.c's `put_based`), in the palettes each
  screen loads.
- `\I` in the scripts (build 129a): AS's escape for the double quote,
  decoded by the script extractor, so a quote is one glyph as in the ROM
  (route `script_quotes`).
- Menu actions on dungeon fixtures: the Small Key (build 45), Dynamite
  and the Key Tube (67), the four cards on their dams' doors and (build
  80) on their dams' panels (route `green_dam_panel`).
- PRESS START and the copyright line: ported as the two title objects
  (Screen sequence, below).
- Palette fades: a screen change (game.c's `goto_screen`) darkens the
  palette over 21 frames and fades the next screen in, or goes through
  white where the ROM does (`goto_screen_white`, loc_634E); the title,
  the dreams, Ustvestia's lesson, the roof's flight, game over and the
  ending have their own. Not checked change by change against the ROM.
- Music and sound: Nuked-OPN2 is in `src/audio/`, every screen queues its
  music, the effects play; Q, the AUDIO page, the second chip (builds
  109-124, docs/audio.md, docs/second_chip.md).
- `{enemy}` and `{tech}`: resolved in the battle text (window.c's
  substitution table).
- The buildings: every one the story enters, the Esper mansion with
  Lutz's picture (build 87; his palette 127), the roof and its flight
  (84). All eleven demo walks are wired (walk.c, building.c, game.c).
- The fight: FIGHT, STGY, ORDR, RUN, the enemies' rounds, EXP, meseta,
  level-ups, game over (builds 41-43 on; docs/battle.md), and the
  bosses.
- The level screen's events: the dead bodies, Teim and Darum, the
  Dynamite, Key Tube and gum targets, the one-shot cases (97e), the
  approach to Mother Brain (92), what follows her battle (93) and the
  ending screen in full (93a).
- CRT phases 2 and 3: SDR through SDL_GPU (107) and HDR10/scRGB (108), run
  by the owner on the P16.
