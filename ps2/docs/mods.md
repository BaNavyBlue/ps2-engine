# The mods folder (builds 138b, 138c)

The owner, after build 138a: the notes -- and the game's files generally --
should be changeable by people without the source code. Since build 125 the
packaged game (`make dist`) carries every file it reads inside itself and
read nothing beside it, so a player could change nothing. Build 138b gave
the packaged game a mods folder; (138c, the owner: "why would it just be in
the distro?") every build has one now -- the game built in the tree too,
whose own files are its `assets/`. Only the probe and the tools never read
it, so the routes and the harness are as before.

## What a player sees

Every start says where the folder is and how many mods are in use
(`mods: mods, 1 in use`), then names each. On its first start the
packaged game writes:

    mods/README.txt          how to use the folder
    mods/originals/...       every file the game carries under assets/,
                             as it carries them: music/, palettes/,
                             nametables/, winart/, levels/, data/, script/,
                             tables/, tiles/ (938 files, about 5 MB)
    mods/originals/pack_id.txt

The game never reads `originals/`. It writes it again only when the game
itself changes (pack_id.txt, a hash of the program's pack, as COMPUTER
PLAYS's harness folder has had since build 125), and it touches nothing
else under `mods/`.

To change a file, copy it from `originals/` to the same place under
`mods/` and edit the copy:

    mods/originals/music/songs/phantasy.txt  ->  mods/music/songs/phantasy.txt

At the next start the game reads the copy in place of its own and says so
on its console (`mods: mods/music/songs/phantasy.txt in place of the
game's own`). Deleting the copy undoes it. A file that is not one the game
carries is ignored.

A game built in the tree writes only `mods/README.txt` (when it is
missing): its own files are `assets/` beside it, so a mod is a file copied
from `assets/music/songs/phantasy.txt` to `mods/music/songs/phantasy.txt`.
`mods/` is in .gitignore and left out of `make dist`'s source.zip.

The packaged game run from the tree's top (`dist/ps2-linux/ps2` from
`ps2/`) finds the tree's `assets/` there and stays, so its mods folder is
the tree's `mods/`, as its saves are the tree's `saves/` (home.c,
build 123); run from its own folder it uses `dist/ps2-linux/mods/`. `make
dist` empties `dist/<name>/`, a mods folder there included.

Where the folder is: `mods` beside the game when that folder can be
written, else beside the saves in the user's own data folder
(`%LOCALAPPDATA%\PhantasyStarII\mods` on Windows; `$XDG_DATA_HOME`, or
`~/.local/share`, then `phantasy-star-ii/mods`), the saves' rule
(home.c). `PS2_MODS` names another.

## A song's two files (build 139b)

Since build 139b a song is in two files: its tracker file,
`music/songs/<song>.trk` (docs/audio.md, "The tracker format"), and the
stream text in its song file, `music/songs/<file>.txt`. Where both are
there the game reads the tracker. The first of these found is read:

1. the song's `.trk` in the mods folder,
2. its song file in the mods folder,
3. the game's own `.trk`,
4. the game's own song file.

The second is there so a stream text mod (`phantasy.txt` copied into
`mods/` and edited, as above) is not hidden behind the game's `.trk`. A
`.trk` copied in beside it wins over it. The two songs of a pair file
(`pleasure_and_exciting_town.txt`) each go by their own `.trk`: with the
pair file as a mod and no `.trk` mod, both come from the mod.

## Chords of up to 16 notes (build 184)

A chord can be as big as a tracker cell: up to 16 notes (`chord 16`, or a
cell of 16 notes), on any channel, in a song or an effect. The Mega Drive's
chip has five music channels besides the drums, and the game's own chords
of three borrow two of them (FM4 and FM5, which its songs leave free); a
bigger chord's notes take the music channels the song leaves free after
the chord's own, then voices of the port's own: extra chips on the chip
synth (six notes each), extra voices on Q. So a song can keep its melody
and bass and still play a chord of six. They are made at start-up for the
music read, only as many as the mods' chords need, and the game's own
music needs none: it sounds as it always has. A 17th note is refused, as
any mistake in the song text is:

    songtext: assets/music/songs/phantasy.trk:136: more than 16 notes in a cell

docs/audio.md, "Chords past the chip", says how; until build 184 a chord
past the free channels played its notes on the effects' channels, and past
the sixth part on whatever the driver's memory held.

## When a mod is refused

The music is guarded: if the song text refuses a mod (a word it does not
know, a note out of range, a label given twice or missing -- songtext.c's
refusals, each naming the file and line), the game says so, sets aside
every mod under `music/`, and plays its own music:

    songtext: assets/music/songs/phantasy.txt:16: instrument: no instrument is named 'phantasy_mud' (fm_instruments.json)
    mods: mods/music/songs/phantasy.txt set aside
    mods: the music above was refused (its assets/ path is the mods folder's); the game's own played instead

The drums the same, under `music/drums`. (build 139b) A tracker file the
same, refused in tracker.py's words:

    songtext: assets/music/songs/phantasy.trk:136: fm1 lasts 13 ticks here; with tick 2 a length is a multiple of 2 up to 254
    mods: mods/music/songs/phantasy.trk set aside
    mods: the music above was refused (its assets/ path is the mods folder's); the game's own played instead

(build 157d) **Every other kind is guarded too**, a file at a time: a
picture, a map, a JSON file, a script or the credits that its loader
refuses is set aside, and the loader reads the game's own in its place:

    png: assets/tiles/title_copyright_art.png is not a PNG
    mods: mods/tiles/title_copyright_art.png set aside
    mods: it was refused (above); the game's own assets/tiles/title_copyright_art.png is read in its place

What counts as refused is what the loader itself turns down. The formats:
a PNG it cannot read (png_load_indexed, png_load_rgba), a Tiled map
(tmx_load), a JSON file that does not parse (json_parse_file). Then each
loader's own rules about a file that reads: a tile sheet not whole tiles
(tileset_load), a colour ramp with a pixel past its palette and a dissolve
order not 64 places of 0-7 (table.c), a nametable cell flipped
diagonally or in no tileset (nametable_load), a window map cell flipped,
empty or past its tileset (winmap_load), a script with no entries
(script_load), the credits' refusals (credits_load), a level map without
its planes (level_load: the map and its backdrop planes' maps), and a
picture whose description, picture and slot map do not agree
(picture_load_frame: the whole thing's folder is set aside, those three
being one). A mod that reads but means something odd -- a price of
`"ten"`, a JSON field missing that its loader has a default for -- is not
refused: the game plays what it says, as before. `tools/mods_check.py`
(in proof.sh) runs the headless game with one broken file of each kind
and holds it to the game with none: the same WAV and last frame, the mod
named as set aside. Until 157d most of these stopped the game at its
start (exit 1, the loader's message alone).

## How it is built

* **assetfs.c**: `assetfs_set_mods(dir)` before the first read. (138c)
  Each asset path is looked for under the mods folder the first time it is
  asked for (`mod_find`), and the answer kept. Every way the game reads a
  file asks it: the cache (`store`, the tree's preload and late reads;
  `store_packed`, the pack's) and `assetfs_pack_find`, the reader the song
  text, the instruments, the drums, the tables, nametables and window maps
  ask before they read the disk themselves -- in a tree build too, where
  it gives a mod or nothing. `assetfs_pack_entry` still gives the pack's
  own (the originals and COMPUTER PLAYS's harness are written from it).
  `assetfs_mods_drop(prefix)` sets a loader's mods aside, putting the
  game's own bytes back in the cache (the pack's, or the file under
  assets/). `assetfs_mods_count` and `assetfs_mods_path` for the report.
  (157d) `assetfs_mod_refused(path)`: the loaders' retry -- true when the
  refused bytes were a mod's, now set aside; `assetfs_mods_refused_under`
  for a picture's folder.
* **home.c**: `home_mods` (the folder, by the saves' rule, the user-folder
  part now one function both use) and `home_write_originals` (with no
  pack, README.txt alone).
* **main.c**: the folder set before the preload, in every build; after it
  the originals (or README) written, the folder and count said, each mod
  named.
* **audio.c**: `audio_load_music` builds the image again without the
  music's mods when the song text refused one; the drums likewise. (139b)
  It hands the song text `assetfs_is_mod` (`songtext_set_mod_test`), which
  says whether a path is a mod's (`mod_find`), for the choice between a
  song's `.trk` and its song file.
* The probe (route_probe, the routes, play.py, the trials) and the tools
  never call `assetfs_set_mods`, so nothing here runs in them.
* (138c) tools/make_pack.py packs without `shaders/` when the tree has
  none (said; the packed game then draws without the CRT chain) instead of
  stopping: a complete tree travels without it.

## Checked (build 138b)

From an empty folder holding only the packed headless game
(`make dist_headless`): the first start wrote 938 files and pack_id.txt,
and its attract WAV (2400 frames) was byte for byte the tree build's
(97141d6d...); a second start wrote nothing. `phantasy.txt` copied to
`mods/music/songs/` with two `C6` notes made `C3`: the mod named at start,
the WAV different (the title theme's notes moved). The same file with an
instrument name misspelt: the refusal by file and line, the mod set aside,
the WAV the original's byte for byte. `level_motavia_outside.png` and
`title_screen.png` with their colours inverted in `mods/palettes/`: both
named, the last frame different.

(138c) The game built in the tree, in a copy with a `mods/` folder: no
mods -- `mods: mods, 0 in use`, README.txt written, the attract WAV
97141d6d... as before; phantasy.txt from assets/ into mods/ with the two
C6 made C3 -- named, the WAV 1e15a626..., the packaged game's with the same
mod; the misspelt instrument -- set aside, the WAV the original's. The
packaged game again from an empty folder, the same three.

(build 168, 139b's code made again) The headless game in a copy of the
tree, the attract WAV (2400 frames), a song's two files: no mods,
97141d6d...; a phantasy.txt mod (the first note an octave down) read over
the game's phantasy.trk, 6a4341fc... -- the same as the same edit made in a
phantasy.trk mod, 6a4341fc...; the phantasy.txt mod with an unedited
phantasy.trk mod beside it, the `.trk` read, 97141d6d...; a phantasy.trk mod
with a length off the tick, refused as above, set aside, 97141d6d...; the
copy's own phantasy.trk broken the same way, the refusal said and no
music.
