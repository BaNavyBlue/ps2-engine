# ps2-engine: working notes

The public version of the phantasy-star-2 port: the same engine, with no
assets and no ROM data in the tree. A player supplies a cartridge image they
own: the game asks for it on its first start and makes `ps2/assets/` from it
itself, in C (`ps2/src/extract/`; `./ps2_extract assets IMAGE` does the same
from a terminal), checking every file. The extraction reads the image
itself, finding what it needs through `ps2/tools/rom_map.json`; no
disassembly is needed or shipped, and no Python. README.md is the front
page; this file holds the decisions, what is proved and what is next.

## Decisions (the owner)

* (2026-11-20) A separate repository, so the private tree keeps its assets
  and history (the private history holds every asset, so it cannot be
  published).
* It is the owner's own: github.com/BaNavyBlue/ps2-engine, committed as
  BaNavyBlue with the owner's university address (this clone's git config;
  the owner chose it over GitHub's noreply address). The private port is
  written under a pseudonym, and nothing of that alias goes into this
  repository: not in the tools, the docs, the commit messages or the
  history. `tools/sync_private.py` reads the name from the private clone's
  git config, writes it as "the owner", and refuses to finish if the name,
  an address, a machine's IP or a home folder is left. The other way holds
  too: the private tree never names this repository.
* The working copy sits outside the folder that holds the pseudonymous
  repos, so their rules and hooks never apply to it.
* The US/European cartridge is required: *Phantasy Star II (USA, Europe)*,
  Rev 02, the one the disassembly is of.
* The Japanese cartridge is optional and adds the JAPANESE language only.
* Excalibur_Z's retranslation ships as it is (`ps2/assets/lang/retranslation/`,
  his MIT work, with his notice).
* Licence: MIT for the engine's own work (`LICENSE`, "Copyright (c) 2026
  BaNavyBlue"); the third parties keep theirs (`NOTICE.md`). The
  disassembly's names in `ps2/tools/rom_map.json` are lory90's and stay
  outside the MIT grant until lory90 is asked.
* Working builds come from the repository: GitHub Actions builds Windows,
  Linux and macOS on every push (artifacts), and a `v*` tag publishes them
  as a release.
* (2026-11-21) The game's text quoted in the tree stays: `script_ids.h`'s
  comments, the routes' `expect text` lines, the docs' quotations. No asset
  file and no ROM data beyond the small tables below.
* (2026-11-21) No disassembly here. A first version shipped a template of
  it (every data literal a placeholder) that the setup filled from the
  image; asked why it was included, the owner said: "do the rewrite to read
  the ROM directly". The private tree's build 185 did that: its extractors
  read the image through `tools/rom_map.json` (the disassembly's labels in
  line order, the drums', the binclude'd files and the constants, each with
  its place in the ROM) and `tools/rommap.py`. The template, its restore
  and `make_template.sh` went.
* (2026-11-21) The history starts without the template. The first version
  had been pushed once, while the repository was private; the owner chose
  to replace the history with one commit and force-push it, so the template
  was never public.
* (2026-11-21) The game makes its files itself. Asked why the exe could not
  see it was the first start and take the ROM (and the Japanese one)
  itself, so no one runs a script, the owner said "yes, start step 1": the
  private tree's build 186. A game with no files opens a window that takes
  the image (dropped, chosen, or on the command line) and runs
  `ps2/tools/make_assets.py` with the Python it finds -- in the Windows
  package, `python\pythonw.exe` -- and JAPANESE on the LANGUAGE menu asks
  for its two files the same way. `tools/setup.py` became a wrapper around
  that script; the list of proved files moved from `tools/` to
  `overlay/ps2/tools/`, which each sync lays down as `ps2/tools/`, where the
  script looks.
* (2026-11-22) Step 2, the extraction in C, so that no Python is needed
  anywhere. The owner asked "why should I need python at all is c not good
  enough?", and set the bar: maximum cross-platform reach, as few special
  or OS-dependent libraries as possible, nothing but standard C (and a
  little C++) unless something is clearly better. So the extractors are
  C11 with the tree's own JSON and PNG code and nothing more (SDL3 stays
  the game's only library; Q stays optional). The private tree's builds
  187-189 wrote 18 of the 21 steps in C (`ps2/src/extract/`, run one with
  `./ps2_extract NAME ROM -o OUT`), each held to its Python twin by
  `ps2/tools/extract_c_check.py`. The Python extractors stay as their
  reference; the game still runs them until every step has its twin.
  (2026-11-25) The private tree's builds 190-192a finished that: the last
  three steps (tables, music, drums), the Japanese importer (`./ps2_extract
  japanese`) and make_assets.py's own part (`./ps2_extract assets IMAGE`:
  the files told apart, every step, the preload list, the check against
  the list of proved files), each held to its Python by
  `extract_c_check.py`, `japanese_c_check.py` and `make_assets_c_check.py`.
  Synced here at 192a, on the owner's word ("If we're testing the asset
  extraction obviously you need to push the engine"). The game's window
  still ran the Python; it runs the C since the private tree's build 193
  (below, 2026-11-25, later).
* (2026-11-23) Q comes with the repository. Asked whether Q could be
  linked into the repository or cloned from the README: `Q/` holds Q
  v1.5.4's headers and its infra's, unchanged (1.3 MB: everything the game
  includes; infra's two test frameworks, catch.hpp and doctest.hpp, left
  out), with Q's LICENSE and `Q/VERSION`, copied by `tools/vendor_q.py
  Q_CLONE`. The port's Makefile looks in the repository's top folder
  first (`ps2/../Q`, the private tree's build 189a), so any clone, or
  GitHub's "Download ZIP", builds the game with Q from plain `make`, and
  CI fetches nothing for it. A submodule would bring Q's whole tree (119
  MB, and 259 MB of history) for the 2.4 MB of headers; a link points at
  one machine's folder.
* (2026-11-24) The first release, v0.1.0, is Windows and Linux: the owner
  will check the Mac build later and wanted those two out now. The release
  job waits for the Windows and Linux jobs only and publishes their
  packages; the macOS job still runs on every push (its package stays the
  run's artifact). macOS joins a release once its job has been seen to
  pass: add it to the release job's `needs` and downloads, and to the
  release notes. The notes now say what README.txt says: start the game,
  it asks for the image the first time.
* (2026-11-25, later) No Python anywhere a player goes. The private tree's
  build 193 has the game's first-start window run the C extraction on a
  thread of its own, every file by its UTF-8 name (`ps2/src/extract/exfs.c`:
  wide calls on Windows), so the game needs nothing to make its files.
  Synced and pushed at the owner's word ("yes, sync the engine and push
  both"). With it the packages lost python.org's embeddable Python, Pillow,
  `setup.bat` and `setup.sh` (`tools/make_release.py`, the workflow's
  download step), and `tools/setup.py` went: from a terminal, a player gives
  the image on the game's command line, and a build from source has
  `./ps2_extract assets IMAGE`. `tools/make_expected.py` takes each file's
  digest from `ps2_extract digest` (the picture of a PNG, as the game
  checks it), so the engine's own tools need nothing but Python's own
  library; the Python extractors and their checks, in `ps2/tools/`, want
  Pillow still, as the reference the C is held to.

## How the tree is made

```sh
python3 tools/sync_private.py ~/Desktop/GIT/Claude/phantasy-star-2   # ps2/, scrubbed
(cd ps2 && make ps2_extract && ./ps2_extract assets ROM)             # ps2/assets/, checked
(cd ps2 && make -j all headless route_probe && make -j routes)       # the proofs
python3 tools/make_expected.py ~/Desktop/GIT/Claude/phantasy-star-2  # when the assets changed
python3 tools/rom_leak_check.py --rom ROM --assets PRIVATE/ps2/assets \
    --known-text PRIVATE/ps2/assets/script \
    --known-text PRIVATE/ps2/assets/lang/ja/script FILES_TO_COMMIT
```

`ps2/` is emptied and rewritten by each sync (`ps2/.synced-from-private`
marks it), so the engine's own files live outside it, or in `overlay/ps2/`
(laid over each sync: the icon, and `tools/assets.sha256`, the list of
proved files, which `make_expected.py` writes in both places). The made
assets and the builds go with each sync too: make the files again after
one (`patches_check` and the routes read them). Left behind: assets/ (but for the
retranslation, the language list and the Q patches, which are the port's or
free to pass on), route_out/ (frames and recordings of the game), every
picture and sound (docs' screenshots too), CLAUDE.md,
NOTES_next_context.md, docs/machines.md, docs/old/.

The ROM map is the private tree's: when the disassembly changes, its
`tools/make_rom_map.sh DISASM ROM WORK` assembles it for the listing,
checks the output against the ROM byte for byte and writes the map; the
next sync brings it.

On the Windows desktop: run MSYS2's Python by its full path from Git Bash.
The sync reads the private clone's `user.name` with the git on the PATH,
and MSYS2's own git does not follow that clone's identity include (Git for
Windows's does).

## What is proved (2026-11-21, the Windows desktop)

* The ROM: 786,432 bytes, SHA-1 `0711080e968490a6b8c5fafbb9db3e62ba597231`.
* The map comes from the disassembly's AS listing, and the disassembly
  assembles to exactly that ROM given two things (make_rom_map.sh does
  both):
  * `cpu 68020` in place of `cpu 68000` for the assembly. AS 1.42 Bld 306
    treats a 68000's addresses as 24-bit (`IsShortAdr` in code68k.c), so
    `btst #6, $FFFFF8` became a short address, and the sound driver's input
    code came out 160 bytes short. The change moves no line.
  * The disassembly's own linker, `AS/ps2p2bin.c`; the stock `p2bin` places
    the Z80 driver wrongly.
* The private tree's build 185, from the image alone (no disassembly
  anywhere it could reach): 1,107 of its 1,109 asset files the same as the
  old extractors made them. The two that changed are now the private
  tree's: `data/battle.json` (18 battle animation scripts' tails, past
  every byte the game's interpreter can reach; the old reader had joined
  other tables' bytes) and `data/flag_names.json` (two notes that quoted
  the disassembly's comments, which the game never reads). Its 172 routes
  pass with every route_out file the same, the Dark Force trial the same.
* `tools/setup.py` here: 1,076 of 1,076 files as proved (about 5 s); every
  one the same as the private tree's but the engine's own icon and the
  preload list, which leaves out the 32 Japanese files.
* In this repository after the sync: `make -j32 all headless route_probe
  patches_check` builds, and `make routes` passes 162 of 172. The 10 that
  fail all use the Japanese language (lang_ja_*, lang_menu, lang_switch_*),
  whose files only `setup.py --ja` makes.
* (Build 186: the first start.) The Windows package (`make_release.py`,
  the static `ps2.exe`, the same embeddable Python and Pillow) unzipped
  fresh, its game started from its folder with the image on the command
  line (as when it is dropped onto the exe), no `PS2_PYTHON` and no Python
  on the PATH: it found `..\python\pythonw.exe`, showed the steps (no
  console window), 1,076 of 1,076 files as proved, exit 0, then the game
  with 1,070 files. Started again: no window, the game. Another fresh copy
  through `setup.bat`, now the wrapper: 1,076 of 1,076. In the repository:
  `setup.py` 1,076 of 1,076; `make_expected.py` writes the same hashes;
  `make routes` 163 of 172 -- lang_menu passes now, the menu keeping
  JAPANESE shown while it is up though its files are not made, as the
  route expects; the 9 left need those files. `lang_check.py`: every part
  but the Japanese crawl (no files here), its new part with the private
  tree's frames. The leak check over the 23 files changed: the same three
  files and counts as at the last commit, nothing new.
* (2026-11-22, the private tree's build 189: the extractors in C.) After
  the sync: `setup.py` 1,076 of 1,076 files as proved (the assets did not
  change); `make -j32 all headless route_probe patches_check ps2_extract`
  with no warning; `make routes` 163 of 172, the same 9 Japanese routes
  failing as at 186. `tools/extract_c_check.py` here: 18 steps in C, their
  981 files the Python's (271 PNGs the same picture, 710 the same bytes),
  the whole extraction's 1,041 files the same. In the private tree the C
  compiled against glibc (GCC 13.3, Ubuntu) with no warning. The leak
  check over the 40 files new or changed: only docs/history.md, with the
  same 4 raw runs and 48 lines of text as the committed one.
* (2026-11-23, Q in the repository; the private tree's build 189a.) The
  static `ps2.exe` built as CI builds it, with `Q/`'s headers (the
  compile lines' -isystem are `Q/`'s), no warning, only Windows' own DLLs.
  Packaged as v0.1.0 (`make_release.py`, the embeddable Python 3.14.7 and
  Pillow 12.3.0, their SHA-256 checked): 25.7 MB. `make_release.py` had
  named a dotted version's zip wrong (`with_suffix` took `.0-windows` for
  a suffix: ps2-engine-v0.1.zip); fixed, as the tar.gz already was. The
  zip unpacked fresh, its game started from outside its folder with the
  image on the command line and nothing but Windows on the PATH: it found
  `..\python\pythonw.exe`, 1,076 of 1,076 files as proved, status 0, then
  the game (1,070 files, Direct3D 12). Started again: the game, no
  window. The leak check over the 150 files new or changed: only
  docs/history.md, as before.
* (2026-11-25, the private tree's build 192a: the whole extraction in C.)
  After the sync: `setup.py` 1,076 of 1,076 (the assets did not change);
  `make -j32 all headless route_probe patches_check ps2_extract` with no
  warning; `make routes` 163 of 172, the same 9 Japanese routes failing.
  `extract_c_check.py`: 21 steps, 1,041 files the Python's (273 PNGs the
  same picture, 768 the same bytes). `make_assets_c_check.py IMAGE`: 20
  runs, both the same, digests 1,091 of 1,095 (the 4 left unread as they
  should be). Synced at 192 first, that check stopped at once: it copied
  this tree's list of proved files into its Python games only, and the
  private tree has no list; 192a leaves it behind. Its JAPANESE part and
  `japanese_c_check.py` want the Japanese cartridge here (`--ja`): their
  `--stand-in` is built from the Japanese files, which this tree has not
  made, and exits 2 saying so. In a copy of the files to commit, `assets/`
  holding only the shipped files: `./ps2_extract assets` on the zipped
  image, 1,076 of 1,076 as proved. The leak check over the 36 files new
  or changed: docs/history.md (4 raw runs, 48 lines of text) and
  docs/languages.md (1 line), as in the committed ones.
* (2026-11-25, later, the private tree's build 193: the window runs the
  C; packages without Python.) After the sync: `./ps2_extract assets` on
  the zipped image, 1,076 of 1,076 as proved; `make -j32 all headless
  route_probe patches_check ps2_extract` with no warning; `make routes` 163
  of 172, the same 9 Japanese routes failing. `extract_c_check.py` PASS (21
  steps, 1,041 files) and `make_assets_c_check.py IMAGE` PASS (20 runs,
  digests 1,091 of 1,095). `make_expected.py` through `ps2_extract digest`
  wrote the list byte for byte as Pillow's digests had (1,076 files, 33 for
  JAPANESE), but for its reworded header. The Windows package built as CI
  builds it (static, only Windows' own DLLs), packed with no Python: 6.5 MB
  (25.7 MB with it). Unzipped fresh, its game started from another folder
  with the zipped image on its command line: the window and its steps,
  1,076 of 1,076 as proved, the files in the game's own folder and nothing
  in the folder it was started from, then the game (1,070 files, Direct3D
  12), about 1.7 s in all; started again, straight to the game. The leak
  check over the 27 files new or changed: docs/history.md (4 raw runs, 48
  lines) and docs/languages.md (1 line), as in the committed ones. In the
  private tree the window's code compiled on glibc against SDL3's headers;
  never linked or run on Linux or macOS.
* A clone of this commit checked out with git's CRLF conversion on (as on
  Windows): its text stays LF (`.gitattributes`), and its setup gives
  1,076 of 1,076. From that clone, the Windows package (`make_release.py`,
  a static `ps2.exe`: only Windows' own DLLs) through its `setup.bat`:
  1,076 of 1,076, 9 s.
* (2026-11-20, on the template version) The Windows package (a static `ps2.exe`,
  python.org's embeddable Python 3.14.7 with Pillow 12.3.0 from PyPI)
  through `setup.bat`: 1,076 of 1,076. That run first found 113 PNGs whose
  bytes differed (another zlib in PyPI's Pillow) with the same pictures;
  PNGs are checked by their picture since then. Its game, started outside
  its folder, found its folder, 1,070 asset files, Direct3D 12 with the
  DXIL shaders; `objdump -p` of the exe: only Windows' own DLLs.
* The leak check over the 572 files to commit (with the ROM, the private
  assets, the US and Japanese script):
  * no file the same as a game asset, except the shipped ones above;
  * ROM bytes: a few tables written as hex in the C source (smps.c,
    building.c, game.c, npc.c; fm_probe.cpp and q_patch_template.py, the
    instrument voices) and four short raw runs (docs/history.md,
    tools/to_srm.py, the retranslation's names.json);
  * the game's text: short lines in 93 files: `src/game/script_ids.h`'s
    comments, the routes' `expect text` lines, the docs' quotations;
  * the map and the new tools: nothing.
* No alias, email, machine name, account name, IP address or home path in
  the files to commit.

## Not yet run or seen

* Until 2026-11-24 the workflow was not valid YAML (an unquoted step name
  with ": " in it, `Build (static: ...)`), so runs #1-#4 failed without
  running a job. Check the file with a YAML parser before pushing a change
  to it. Runs #5 (tag v0.1.0) and #6 (main, 05d3077), as the owner read
  them: completed successfully, every job, macOS included -- so the port
  compiles and links on macOS (clang, universal), and release v0.1.0
  (Windows, Linux) is published. Nobody has run the macOS package yet,
  nor the Linux package (the owner's own Linux build works).
* The extraction on Linux or macOS: it has run only on Windows (the C
  ones' digests and refusals also on Linux, in the private tree). Since
  the game links it (2026-11-25, later), CI compiles it with the game on
  all three; its first run there will say. On the owner's Linux machine:
  the game started with no files (`./ps2/ps2 /path/to/image.bin`, or the
  window's drop and dialog), and `make ps2_extract`, `./ps2_extract
  assets IMAGE`, `python3 tools/make_assets_c_check.py IMAGE` in `ps2/`.
* The packages without Python: the Windows one tried here; the Linux and
  macOS ones never run.
* The JAPANESE language through the game's window or `./ps2_extract
  assets JP_IMAGE RETRANSLATION` (no Japanese image here; the expected
  hashes are the private tree's).
* The packaged game played by a person: only its start-up log and the
  first-start window's pictures were read. Dropping the image on the
  window and the file dialog need a person.
* The first-start window on Linux and macOS (compiled on Linux against
  SDL3's headers only; never linked or run there).

## Next

1. The macOS package (run #5's artifact `ps2-engine-macos`), when the
   owner tries it on a Mac: if it plays, attach it to release v0.1.0 and
   add macOS to the release job (see the 2026-11-24 decision).
2. (2026-11-24) Public, with release v0.1.0. The owner deleted run #1, the
   only public link to the replaced commit e4902a1; that commit (with the
   stripped `PSII_Disasm/`) still opens by its ID (checked: HTTP 200).
   Removing it for certain means GitHub Support, or deleting and
   recreating the repository (then push main and the tag again): the
   owner's call, offered and not taken up.
3. With the Japanese image here: JAPANESE through the game's window, then
   the 9 language routes and lang_check's Japanese crawl.
4. A release of the packages without Python (v0.1.0's still carry
   Python and Pillow): read the push's CI run, then tag (`git tag v0.2.0
   && git push origin v0.2.0`) when the owner says.
5. Later: other revisions of the cartridge (refused by their SHA-1;
   supporting one means a map of its own).

## Open

* The engine's text is the private tree's text: anyone with both could
  tell they are the same work. The private repos' links are secret, which is
  what keeps them apart.
