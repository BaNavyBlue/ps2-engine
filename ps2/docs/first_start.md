# The first start: the game's files from the player's own cartridge

(build 186) A copy of the game without its files -- a source checkout
whose `assets/` was never extracted, or a package that carries none --
makes them the first time it starts, from the player's own cartridge
image, with nothing to type. The same window adds the JAPANESE language
later, from the Japanese cartridge and Excalibur_Z's retranslation.

This tree's own `assets/` are checked in, so here none of this shows; a
scratch copy with `assets/` emptied is where it was tried (below).

## What the player sees

The game starts with no `assets/manifest.txt` beside it and nothing packed
into it (`make dist`'s game has its files inside and never asks). Before
the game's window opens, a window of its own (src/platform/
first_start_sdl3.c) says what is wanted -- *Phantasy Star II* for the
USA/Europe, revision 02, "Phantasy Star II (UE) (REV02)", a .bin, .md,
.gen or .smd file or a .zip holding one -- and takes it:

- dropped onto the window;
- chosen: Enter, Space or a click opens the system's file dialog;
- dropped onto the program itself, or named on its command line
  (`ps2 path/to/image.bin`): used at once.

The window then shows each step as `tools/make_assets.py` runs it (a bar
and "step 9 of 22: techs"), and the game's window opens the moment the
files are made -- about six seconds here. When the script refuses (a file
that is not the cartridge, an earlier revision) or a step fails, its words
are shown under "That did not work:" and the window asks again. When the
files are made but some are not as a proved extraction made them (exit
3, below), it says so: Enter plays on, Escape quits. Escape quits while it
asks; closing the window stops a run (the preload list is written last,
so a half-made `assets/` is never taken for a whole one, and the next
start asks again).

The game's own font is one of the files to be made, so the window writes
in SDL's 8x8 debug face (`SDL_RenderDebugText`) on an SDL_Renderer of its
own, 640x360 drawn at whole multiples.

## The JAPANESE language

The menu lists JAPANESE whether or not its files are there
(assets/lang/languages.json). Its files (assets/lang/ja/) are made from
two things of the player's: the Japanese cartridge (*Kaerazaru Toki no
Owari ni*; its first 768 KB CRC32 BEC8EB5A) and Excalibur_Z's
retranslation (github.com/ExcalZ/ps2-retranslation: a checkout, its
"Download ZIP", or that zip unpacked), whose work/dialogue.json and
work/script.json carry the cartridge's text matched to the US script ids
(docs/languages.md, Japanese).

`lang_missing` (src/game/lang.c) says a language's folder has no lang.json.
Such a language is offered, not loaded:

- chosen on LANGUAGE, it shows on the menu while the menu is up and is
  offered when the menu closes on it -- passing it on the way round, Enter
  after Enter, offers nothing (game.c, take_language);
- named by settings.json at the start, it is offered before the game's
  window opens (game_init).

`game.language_wanted` carries it to main.c (offer_language), which asks
the platform (`platform_add_language`). The SDL window shows two slots,
cartridge and retranslation; files dropped or chosen fill them (a folder,
or a zip naming work/dialogue.json, is the retranslation; anything else
the cartridge), and when both are filled the script runs. Made, the new
files are preloaded, the languages' default names read again
(`lang_rescan`) and the language chosen; the game takes it up at its next
tick. Left, the game goes on in the language it had, and settings.json
keeps that one.

## tools/make_assets.py

`make extract ROM=...` with no make (it parses the Makefile's
`EXTRACTORS` and each rule's command, as `make extract` runs them, then
the preload list):

    python3 tools/make_assets.py FILE... [--keep-going]
    python3 tools/make_assets.py IMAGE [--ja JP_IMAGE --retranslation R]

Each FILE is told apart by what it is, never by its name or place: the US
image by size and SHA-1 (tools/rom_map.json's `rom`; .smd de-interleaved,
a copier's 512-byte header dropped, a zip opened), the Japanese one by its
CRC, the retranslation by its two files. The US image runs every
extractor on a copy in a temporary folder; the Japanese pair runs
tools/import_japanese.py (with `--commit`, new in 186, the commit a
GitHub zip's comment names, so its lang.json names the same source as a
checkout's). The JAPANESE language alone needs the game's files there
already.

Where `tools/assets.sha256` is (sections `us` and `ja`: the files a proved
extraction made; a PNG by its picture, as Pillow builds differ in zlib),
every file is checked against it. This tree has none.

For the window: a line `[K/N] WHAT` as each step starts, sentences for the
player otherwise, every step's whole output in `make_assets.log` beside the
game (git-ignored). Exit 0 done; 1 a step failed; 2 nothing to make the
files from (an unknown file, one of the Japanese pair alone, the
Japanese before the game's files); 3 made, but some differ from the
list; 4 no Pillow.

## Which Python

Found once a run (first_start_sdl3.c, find_python), each asked to `import
PIL` (15 s at most): `PS2_PYTHON`; a Python packaged beside the game
(`python/` in the folder above the game's or in its own); then the PATH's.
On Windows the windowless one first -- pythonw.exe beside python.exe, then
`pythonw` and `pyw -3` before `python3`, `python` and `py -3` -- since a
console Python started by a game with no console opens a console window
over it (seen here: the game started from Git Bash). make_assets.py starts
its own steps with CREATE_NO_WINDOW for the same reason. On Windows the
command line's files are read again from the wide command line
(CommandLineToArgvW's rules), as main()'s are in the ANSI code page.

None found: the window says Python 3 with Pillow is needed, and how to
install it there.

## Headless

`ps2_headless` has no window: `platform_make_assets` prints how to make
the files (`python3 tools/make_assets.py IMAGE`, or `make extract`) and the
game exits 1; `platform_add_language` prints how to add the language and
the game goes on in the one it had. tools/lang_check.py checks the latter
(its sixth part).

## How it was tried (build 186, the Windows desktop)

In a scratch copy of the tree with `assets/` emptied to the files no
extractor makes:

- make_assets.py on the image: 1,075 files the same bytes as the tree's,
  the preload list the same but for the Japanese lines, the icon's
  portrait (not an extractor's) not made; 5.8 s.
- The window: with no files given (the picture of what it asks), a file
  that is not a cartridge (its refusal shown, asked again), the image on
  the command line (the steps shown, then the game's window, 1,070 files
  loaded), and with settings.json naming JAPANESE (the language's window
  at the start; a retranslation folder and a wrong cartridge given: told
  apart, refused). MSYS2's python3.exe named, pythonw.exe used: no
  console window.
- The retranslation's GitHub zip (commit 061e1f5): told apart, unpacked,
  its comment the commit.

Not tried: drag and drop and the file dialog by hand (they need a
person); the Japanese language made end to end (no Japanese cartridge on
this machine); the window on Linux and macOS (the dialogs there go through
xdg-desktop-portal or zenity, and macOS's own panel).

## In C (step 2, from build 187)

The owner, asked whether the game should need Python at all: "python is
fine for some things but keep in mind some of my original intentions was
maximum cross platform compatibility. Minimizing special or OS dependent
libraries is part of that goal", and "unless there is something superior
you can't have with bog standard C minimal C++ Then avoid it." So the
extractors are being ported to C, into the game, a few builds at a time:
C11 and the tree's own code (src/core/json.c reads JSON, src/core/
pngwrite.c writes PNGs, src/extract/jout.c writes JSON), nothing else.
Making a folder is the one call no C standard has (`_mkdir` on Windows,
POSIX's `mkdir` elsewhere, as home.c and gamedata.c do already).

- `src/extract/`: each Python extractor's twin, a step by the Makefile's
  EXTRACTORS name (`ex_steps.c`). They read the image through the same
  map (`exmap.c`, tools/rommap.py's lookups: a name exactly, then in any
  case), share extract_gamedata.py's helpers (`excommon.c`) and take the
  asset names tools/gen_asset_names.py writes from tools/asset_names.py
  (`asset_names.c`, checked in). A step that cannot go on stops as the
  Python's `sys.exit` does: `ex_fail`, which ends the step and lets its
  memory go (an arena), so the game can run them in its own process.
- `jout.c` writes JSON as Python's `json.dumps` does, byte for byte:
  indent, separators, ensure_ascii's escapes, a dict's order.
- `./ps2_extract NAME IMAGE -o OUT` (`make ps2_extract`) runs one step
  from a command line, as its Python twin is run.
- `python3 tools/extract_c_check.py ROM` holds them to the Python ones:
  two whole extractions in scratch folders, one all Python, one with every
  step that has a C twin in C; each C step must write (and take away) the
  same files as its twin, and the two folders end the same, every file
  the same bytes, a PNG the same picture (the C writes its own zlib
  stream, as Pillow builds differ in theirs anyway). (build 190) `--only
  NAME,...` runs only those steps in C, the rest in Python.

| Build | In C |
|---|---|
| 187 | the framework; palettes, paletteptrs, collision, interactions, buildings, stores, techs, gamedata |
| 188 | the two decompressors and the tile sheets (`exart.c`), a 68000 decoder (`ex68k.c`); levels, script, npcs, flag_names |
| 189 | the art sheets as the cartridge holds them (`exsheets.c`); battle, inline_art, art, nametables, winart, pictures |
| 190 | song_text.py's vocabulary and walk (`exsong.c`), its disassembly and annotate (`ex_songtext.c`), tracker.py's export (`ex_tracker.c`); tables, music, drums -- all 21 |
| 191 | zip files as Python's zipfile reads them (`exzip.c`); the Japanese importer, tools/import_japanese.py (`ex_japanese.c`, `ps2_extract japanese`) |
| 192 | tools/make_assets.py itself (`exmake.c`, `ps2_extract assets`): the image as players keep it, the files told apart, every step, the preload list (make_manifest.py), the check against the proved files with its digest (`exdigest.c`: SHA-256, a PNG's picture as Pillow opens it; `ps2_extract digest`) |

(build 190) The music step builds the song text it made back into the
driver's image with the game's own song builder (src/audio/songtext.c,
through a reader that serves the files from memory, so nothing on a disk
is read), and refuses to write anything unless it gives the image again
byte for byte; then once more with each song read from its .trk, as the
game reads them, which the Python never checked.

(build 191) The JAPANESE language in C: `./ps2_extract japanese JP_IMAGE
RETRANSLATION [-o OUT] [--assets DIR] [--commit C]` is
tools/import_japanese.py's twin, the same arguments, the same files (the
face, the character map, the script, the names, the name board,
lang.json, NOTICE.txt) and the same lines said. The cartridge may come in
a zip, read as Python's zipfile reads it (`exzip.c`: the end record found
where zipfile looks, stored and deflated entries -- the inflate is the
PNG reader's, src/core/png.c -- each checked by its CRC). The text is
Unicode in the Python and UTF-8 here; what the Python does to a string
is done to its code points (str.isspace, re's `\d`, str.splitlines, by
tables of Python 3.14's). src/core/json.c reads `\u` escapes as UTF-8
now, and `\b`, `\f` as their bytes (each `\u` was a `?` until 191:
nothing in the tree is escaped so, but another commit of the
retranslation may be).

`python3 tools/japanese_c_check.py RETRANSLATION --ja JP_IMAGE` holds it
to the Python: both write a scratch folder each from the same files, and
must write the same files and say the same lines; both must take the
image in a zip and refuse a zip of two images; with the retranslation's
proved commit, every file must be assets/lang/ja's. With no Japanese
cartridge at hand, `--stand-in US_IMAGE` makes one for the check from the
US image -- its own font art where the Japanese cartridge's is, a
character map and a name board's art built back from assets/lang/ja's
lang.json and names.json, four bytes at its end set so its CRC32 is the
one both importers ask for -- and holds all but the face (font.png,
font_upper.png) to assets/lang/ja too.

(build 192) make_assets.py in C: `./ps2_extract assets FILE...
[--ja JP_IMAGE] [--retranslation R] [--keep-going] [--game DIR]` takes
what make_assets.py takes and does what it does, in one process:

- each file told apart by what it is -- the US image by the map's size
  and SHA-1 (raw; a .smd by its name or its header's AA BB, put in
  order; a copier's 512-byte header dropped; a zip holding one image),
  the Japanese one by its CRC, the retranslation by its two files (a
  checkout, GitHub's zip, or that zip unpacked); every refusal in the
  Python's words, its header's title and version quoted as Python's
  repr() quotes them;
- every step run where it lies: the US image read once, the 21 steps on
  it in the Makefile's order (`ex_steps.c`), the JAPANESE language with
  the retranslation's two files read out of its zip where they lie
  (`ExJapanese`'s dialogue and script) rather than unpacked to a folder,
  then the preload list, make_manifest.py's walk of assets/ and its file
  byte for byte;
- `[K/N] STEP` as each starts, the end of a failed step's output, every
  step's lines in `make_assets.log`, "FAILED: ..." with --keep-going's
  list as the Python's;
- then, where `tools/assets.sha256` is, every file of its sections held
  to it, the Python's lines and its exit 3. The digest is file_digest's
  (`exdigest.c`): the SHA-256 of a file's bytes, a PNG's of its picture
  as Pillow opens it -- mode, size, PLTE's own entries, tRNS as Pillow's
  reader takes it, the pixels a channel to a byte. The kinds Pillow
  opens without converting them are read here (indexed at 1-8 bits, grey,
  grey and alpha, RGB, RGBA at 8); any other PNG (16 bits, grey below 8,
  interlaced, broken) is "png:unreadable", which no list holds, so it
  counts as differing. `./ps2_extract digest FILE...` prints them.

What is not the Python's, on purpose: the US image is not copied to a
temporary folder first; the log names each step where the Python's gave
each tool's command line; a failed step's tail is its lines and its
reason where the Python's was the tool's output and a traceback; there
is no exit 4 (no Pillow to need). The API (extract.h's ex_make_*) runs it
a step at a time, for a window that draws between steps.

`python3 tools/make_assets_c_check.py IMAGE [RETRANSLATION (--ja JP_IMAGE
| --stand-in)]` holds it to the Python: each run is make_assets.py in a
scratch copy of the game (Makefile, tools/, assets/ holding only the
files no extractor makes) beside `ps2_extract assets` in a copy holding
only the map, on the same files, and both must end with the same exit,
the same lines (the log's path aside) and the same files: the refusals,
the image in each of its forms (each a whole extraction), the list as
made and the list changed, a step that fails (with and without
--keep-going), the JAPANESE language after the game's files from a
folder, its root, GitHub's zip, a zip of another commit and one with
its files at the top, and all three files at once. And the digests:
every file under assets/ and PNGs of each kind Pillow writes, the C's
the Python's, the kinds it does not read "png:unreadable".

The Python extractors stay, as the reference the C ones are checked
against (as the C player and the Python one both stay, build 180a). Every
step has its twin now (build 190), the Japanese importer (191) and
make_assets.py (192); next, the game runs them itself in its first-start
window, and the packages carry no Python.

## Later
- A game folder that cannot be written (an installed copy): the files in
  the user's data folder, as the saves already go (home.c).
