# Phantasy Star II: a ROM-free engine

A port of *Phantasy Star II* (SEGA, Mega Drive, 1989) to modern computers, in
portable C with SDL3. It plays the original game's logic, maps, battles,
music (on an emulated YM2612, or re-voiced with Cycfi's Q synth) and script.
It also adds a CRT look, rebindable controls and gamepads. A "COMPUTER PLAYS"
mode can play the whole game.

**This repository contains none of the game.** There is no ROM and no art,
music, maps or text taken from one. You make the game's files once, on your
own machine, from your own cartridge image. The setup rebuilds the
disassembly from your image, extracts the game's files from it and checks
each one. Nothing is downloaded or sent anywhere.

## Playing

1. Download the package for your system from
   [Releases](../../releases) and unpack it.
2. You need your own image of *Phantasy Star II* for the USA/Europe,
   **revision 02**: 786,432 bytes, SHA-1
   `0711080e968490a6b8c5fafbb9db3e62ba597231` (commonly named
   "Phantasy Star II (UE) (REV02)"). A `.bin`, `.md`, `.gen` or `.smd` file
   works, and so does a `.zip` holding one.
3. Make the game's files:
   * **Windows:** drag your image onto `setup.bat`.
   * **Linux, macOS:** `./setup.sh /path/to/image.bin`. This needs Python 3
     with Pillow (`apt install python3-pil`, `brew install python pillow`, ...).
4. Run `ps2\ps2.exe` (Windows) or `./ps2/ps2`.

The packages are not signed. On Windows choose "More info", then "Run
anyway". On macOS, run `xattr -dr com.apple.quarantine .` once in the
unpacked folder.

### Languages

* **English**: the US script.
* **RETRANSLATED**: Excalibur_Z's
  [English retranslation](https://github.com/ExcalZ/ps2-retranslation),
  under its MIT licence. It is included.
* **JAPANESE**: needs the Japanese cartridge (*Kaerazaru Toki no Owari ni*)
  and a copy of Excalibur_Z's repository, which carries the Japanese
  script: `python3 tools/setup.py US_IMAGE --ja JP_IMAGE --retranslation
  ps2-retranslation/`.

## Building from source

You need:

* a C11 compiler and make;
* SDL 3.4 or later, with pkg-config;
* Python 3 with Pillow (for the setup);
* optionally Cycfi's [Q](https://github.com/cycfi/Q) v1.5 and a C++20
  compiler, for the re-voiced synth.

```sh
python3 tools/setup.py /path/to/image.bin     # the game's files, from your image
cd ps2
make                                          # the game: ./ps2
make headless route_probe && make routes      # the checks (162 of 172 pass
                                              # without the Japanese files)
```

On Windows, use MSYS2's UCRT64 shell. `ps2/docs/windows_build.md` is the
recipe and `ps2/tools/setup_msys2.sh` lists the packages. Build with
`Q_DIR=/path/to/Q` when Q is not in `~/GIT/Q`. `ps2/README.md` and
`ps2/docs/` describe the port itself.

## How it works

* `ps2/tools/rom_map.json` says where everything is in the cartridge, by
  the names the game's disassembly gives it: every label with its address,
  the art files' places and lengths, and the constants. It holds names and
  numbers only, no data and no code. The private tree makes it from the
  disassembly (`ps2/tools/make_rom_map.sh` assembles the disassembly, checks
  the result is the ROM byte for byte, and reads the listing).
* `ps2/Makefile` names the port's extractors (`EXTRACTORS`). `tools/setup.py`
  checks your image's SHA-1 and runs them on it in that order, as `make
  extract ROM=image.bin` does. They read the image through
  `ps2/tools/rommap.py` and the map, and make `ps2/assets/`.
* `tools/assets.sha256` lists the files the engine was proved with. The
  setup checks every file it makes against it. For a PNG it compares the
  picture, not the bytes, since compression varies between Pillow builds.

Every push builds the game for Windows, Linux and macOS in GitHub Actions
(`.github/workflows/build.yml`). The packages are kept as each run's
artifacts. Pushing a tag `v*` publishes them as a release.

## Layout

| Path | What |
|---|---|
| `ps2/` | the port: source (`src/`), tools and extractors (`tools/`), test routes (`routes/`), compiled shaders, docs |
| `tools/` | the setup, the release packager, the leak check, the sync from the private tree |
| `overlay/ps2/` | the engine's own versions of files the game would otherwise take from the cartridge (the icon) |

## Licence

The engine's own work is MIT-licensed (`LICENSE`). The disassembly's names
in the ROM map, Nuked-OPN2, Q, SDL3 and the retranslation keep their own
terms; `NOTICE.md` lists them. *Phantasy Star II* is SEGA's. This project is not affiliated
with SEGA.

## Credits

* The disassembly, whose names the ROM map and the port's code use: lory90,
  *Phantasy Star II Disassembly for Mega Drive/Genesis* (Phantasy Star
  Cave).
* The English retranslation and the Japanese script data: Excalibur_Z.
* The YM2612: Nuked-OPN2, by Alexey Khokholov (Nuke.YKT).
* The re-voiced synth: Cycfi's Q, by Joel de Guzman.
* SDL3: Sam Lantinga and contributors.
