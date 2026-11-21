# ps2-engine: working notes

The public version of the phantasy-star-2 port: the same engine, with no
assets and no ROM data in the tree. A player supplies a cartridge image they
own. `tools/setup.py` runs the port's extractors on it into `ps2/assets/`
and checks every file. The extractors read the image itself, finding what
they need through `ps2/tools/rom_map.json`; no disassembly is needed or
shipped. README.md is the front page; this file holds the decisions, what is
proved and what is next.

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

## How the tree is made

```sh
python3 tools/sync_private.py ~/Desktop/GIT/Claude/phantasy-star-2   # ps2/, scrubbed
python3 tools/setup.py ROM                                           # ps2/assets/, checked
(cd ps2 && make -j all headless route_probe && make -j routes)       # the proofs
python3 tools/make_expected.py ~/Desktop/GIT/Claude/phantasy-star-2  # when the assets changed
python3 tools/rom_leak_check.py --rom ROM --assets PRIVATE/ps2/assets \
    --known-text PRIVATE/ps2/assets/script \
    --known-text PRIVATE/ps2/assets/lang/ja/script FILES_TO_COMMIT
```

`ps2/` is emptied and rewritten by each sync (`ps2/.synced-from-private`
marks it), so the engine's own files live outside it, or in `overlay/ps2/`
(laid over each sync: the icon). Left behind: assets/ (but for the
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

* The GitHub Actions runs: not read here (no `gh` on this machine). The Windows job repeats what was run
  here. The Linux job (SDL3 built static on Ubuntu 24.04, libstdc++ linked
  in) and the macOS job (a universal binary, SDL3 static, clang) are
  untested: macOS has never compiled the port.
* The extractors on Linux or macOS: they have run only on Windows.
* The JAPANESE language through `setup.py --ja` (no Japanese image here;
  the expected hashes are the private tree's).
* The packaged game played by a person: only its start-up log was read.

## Next

1. Read the Actions runs; fix what the Linux and macOS jobs say.
2. Before the repository goes public: delete the first Actions run (it ran
   on the replaced commit, which GitHub keeps reachable by its ID for a
   while), and look over the repository's pages once more. Public when the
   owner says.
3. With the Japanese image here: `setup.py --ja`, then the 10 language
   routes.
4. Later: a first-start "choose your ROM" in the game (SDL3's file dialog)
   with the extraction in C; other revisions of the cartridge (the setup
   refuses them by their SHA-1; supporting one means a map of its own).

## Open

* The engine's text is the private tree's text: anyone with both could
  tell they are the same work. The private repos' links are secret, which is
  what keeps them apart.
