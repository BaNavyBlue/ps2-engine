# Engine: levels, the opening crawl, frame pacing, text

Moved verbatim from the README in build 81c.

## Level format

`LoadLevelData` (ps2.asm:13229) indexes `LevelData` (46217) by
`level_index`, 20 bytes per level:

```
+0   u8   planet in bits 7+, tileset in bits 0-3
+1   u8   high nibble * $100 = height px, low nibble * $100 = width px
+2   u32  flags<<24 | plane B block map   -> $FF9000
+6   u32  flags<<24 | plane A block map   -> $FFA800
+10  u32  flags<<24 | per-block collision table
+14  u32  palette<<24 | interaction list
+18  u8   index into the extra-art table at loc_283EE
+19  u8   music id
```

Three nested things, which is why 101 maps fit in so little ROM: a block
map of one byte per 32x32 block, row-major, width/32 by height/32; a
block of 32 bytes, being 4x4 nametable entries; and an entry indexing the
level's own tileset from tile 0.

**Two codecs, not one.** The block maps and block definitions are
`DecompressArt2`, the format above. The tile graphics are **Nemesis**
(`NemDec`, ps2.asm:8830), an unrelated Huffman-coded format decoded in
`tools/nemesis.py`. Anything that only knew the art format would decode a
level's maps and then fail on its tileset. The `*_tile_ind.bin` note that
used to sit under "Not done yet" was a red herring: those five files are
title-screen and portrait nametables and have nothing to do with levels.

The tileset index picks a pair of pointers out of `loc_9342`. Six
tilesets serve all 101 levels:

```
tileset 0: 484 tiles, 170 blocks, highest tile referenced 483
tileset 1: 317 tiles, 105 blocks, highest tile referenced 407
tileset 2: 442 tiles, 134 blocks, highest tile referenced 441
tileset 3: 270 tiles, 105 blocks, highest tile referenced 407
tileset 5: 570 tiles, 129 blocks, highest tile referenced 569
tileset 6: 241 tiles, 71 blocks, highest tile referenced 407
```

Slot 4 is not a tileset. No level uses it, and its graphics pointer lands
on `loc_412F8`, the 23-tile mist `loc_9222` loads at VRAM `$3020` (tiles
$181-$197; the VDP command `$70200000`, which earlier builds read as VRAM
$7020) for tilesets 1, 3 and 6, in the art format rather than Nemesis.
Decoding it as Nemesis fails, correctly. (build 141b) Those tilesets'
backdrop blocks are drawn with it -- every tileset 1, 3 and 6 level's plane
A backdrop but Noah's and the underwater passage's is mist -- and game.c's
`level_mist` places it, extracted as `assets/tiles/level_mist.png`.

**A plane can be a backdrop rather than a map.** `btst #6, ($FFFFF712).w`
is a byte operation on the high byte, so it tests bit 14, not bit 6; read
as bit 6 it looks dead, since nothing ever sets that bit. What it means:
bit 3 of the +10 flag byte makes plane A a fixed 16x8 block map that
wraps, because `loc_8F70` substitutes a `$200` x `$100` extent for it,
and `loc_8E00` never refreshes it. Bit 2 does the same for plane B. Every
dungeon does this with plane A, which is why those maps are 128 bytes
rather than thousands. All 101 levels agree with the bit.

A backdrop plane is always its own file, `<stem>_A.tmx` (or `_B`), and
`level_load` looks for it there whenever the bit is set. Up to build 61
`extract_levels.py` split a plane out by *size* instead -- a plane whose
dimensions differed from the level's -- so the two levels that are
themselves 16x8 blocks, `$10` EsperMansionB1 and `$52` IkutoGroundF,
had their backdrop written into the level's own `.tmx` as a second
layer, and loading them failed with "cannot open ..._A.tmx" and "no
planeA layer": a black screen and no level. The extractor now goes by
the bit (build 62; `make extract-levels`, then `make manifest`). Rerun,
it changes those two levels and `levels.json` and nothing else. The
routes `esper_b1` and `ikuto_ground` walk into each by its stairs.

Two things bite when scraping this out of `ps2.asm`. A compressed stream
is one blob, but the disassembler puts a label wherever something points
into the middle of it -- several dungeon maps are pointed at from two
levels at different offsets -- so a reader that stops at the next label
truncates them; `Asm.run` reads straight through and lets the
decompressor decide where the stream ends. And a data line may carry its
own label (`PtrPal_Sega:  dc.l Pal_Sega`), which a pattern anchored on
leading whitespace misses entirely.

**Verified.** Frames drawn through the whole asset pipeline -- PNG tile
sheets, PNG palettes round-tripped through the DAC ladder, text block
definitions, Tiled maps -- are identical at every crawl waypoint to the
reference below, so the format change is provably lossless.

All 101 levels decode to their full size with every block
id in range, and the highest tile a block set references is one below the
tile count its Nemesis stream produces -- two independent sources
agreeing on the tileset's extent. Frames drawn by `src/core/level.c` at
each of the six opening-crawl camera positions are identical, all 71680
palette indices, to a reference computed straight from the disassembly
with no shared code.

The port rebuilds the whole 64x32 plane every frame rather than
refreshing one column or row as the camera crosses a 32px boundary
(`loc_8E00`). The original did that because the VDP had a bandwidth
budget for exactly that much per field. There is no such budget here, the
result is the same sliding window either way, and it removes four
direction cases, their dirty-bit flags and the wrap bookkeeping.

## The Motavia opening

`MoveToOpeningScreen` (ps2.asm:10783) is where the title screen times out
to: level 0, camera at `$5B0`/`$520`, `controls_locked` = 1 and
`event_flags` = `$1007`.

The two bytes of `event_flags` drive different things. The high byte
indexes the one-shot cases in `loc_7E02`, and `$10` is the last of them;
it starts script `$3B`. The low byte indexes the per-frame camera cases
in `loc_809C`, and `$07` is `loc_8192`, which steps the camera one pixel
per field toward (`$FFFFC648`, `$FFFFC64A`) and, on arrival, runs the
next script from `$FFFFF780` -- which `MoveToOpeningScreen` seeded with
`$3C`.

Scripts `$3B`..`$40` (`EventType_DialogueIndex`, ps2.asm:19934) are the
crawl. Each sets one axis of the target, opens a message window and loads
`demo_timer` with `$10E`; the camera cannot move while a script is
active, so the order on screen is message, hold, move. Script `$40` has
no target and hands back to `ScreenID_Sega`, which is how the attract
loop closes.

`level_x_pos` and `level_y_pos` are the **centre** of the view, not its
corner: `LoadLevelData` derives the camera as `pos - $A0` horizontally
and `pos - $80` vertically (ps2.asm:13325). So the opening camera is
`$510`/`$4A0`, centred on (1456, 1312) -- and the Motavia interaction
list has an entry at (1456, 1328) leading to level 9, which is **Piata**.
The opening looks down on Piata, not Paseo, whose entrance is at
(1168, 912).

That offset also explains the shape of the first move. Script `$3B` sets
a Y target of `$4A0`, which the camera is already at, so the first leg is
due west and nothing pans diagonally. Taking the position as the corner
put the camera `$A0` right and `$80` down of where it belongs and
invented a diagonal that the game never makes.

```
        camera        script   message
start   $510, $4A0
        $1E0, $4A0    $3B      $1401   (sets both axes)
        $1E0, $120    $3C      $1402
        $760, $120    $3D      $1403
        $760, $340    $3E      $1404
        $3F0, $340    $3F      $1405
                      $40      $1406   then ScreenID_Sega
```

4544 fields of movement plus six holds of 270, so 6164 fields, a little
under two minutes.

`demo_timer` is decremented by the VBlank handler (`VBlankSub_04_08`),
not by the screen routine, so the title screen's `$6CC` keeps counting
through both dissolves -- it is loaded before them and `TitleScreenLoop`
only runs out the remaining ~1228 fields. Counting it down only in the
wait loop adds the dissolves' 512 fields on top and puts the opening
eight seconds late.

**No objects are involved.** Because `controls_locked` is 1, `LevelScreen`
skips `move.w #ObjID_CharactersLevel, ($FFFFE400).w` entirely, and the
following-party loop is gated on that same slot. Nobody walks during the
opening. The earlier note that this needed the object system first was
wrong.

**No music is queued either.** `MoveToOpeningScreen` writes `$82` straight
into the last-sound latch at `$FFFFF640` without going through the sound
queue, so when `LoadLevelData` asks for Restoration it matches the latch
and is dropped. The title theme plays on across the transition.

Still missing from the opening: the six message windows, which want
`ProcessWindows` and the text routine's glyph base.

## Frame pacing

The game is 60 progressive frames per second, not 30 interlaced. NTSC
broadcast is 60 interlaced fields making 30 frames, but consoles of this
era skipped the half-line offset between fields so every field redrew
the same 224 lines -- "240p", and the reason CRTs show visible scanlines
on them. The Mega Drive's own VBlank fires at 59.92 Hz and the game's
timers are counted in it: the SEGA screen holds for `$12C` = 300 counts
and the title for `$6CC` = 1740, which are 5 and 29 seconds at 60 Hz and
would be 10 and 58 at 30. Every pan and every blink is one step per
VBlank. A 60 Hz display can therefore show the game exactly, and any
jitter left on one is the loop's, not the game's -- `PS2_TRACE=1` prints
per-frame deltas and tick counts so it can be seen rather than guessed.

The loop is single-threaded: pump input, tick at a fixed 60 Hz through
an accumulator, render, present. The one other thread is SDL's audio
thread, which calls `audio_render` from its callback; it touches the
same `Audio` state that `audio_play_sound` writes from the main thread
with no lock between them, which has not bitten yet but is a real race.

The accumulator alone stutters under vsync. Every frame is one field,
but the clock reads 16.5 ms one frame and 16.9 the next -- and now and
then 21 followed by 12, when the timestamp is taken late -- and an
accumulator turns that into a frame with two ticks and one with none: a
2px jump and a held frame in a 1px-per-field walk. Snapping deltas near
one tick to exactly one tick was not enough, because a late/early pair
falls outside any sensible window and leaves a residue that comes back
later as another bad pair.

So `main.c` does not ask the clock how many ticks a frame is worth when
it does not need to. While the display is judged to be running at the
game's rate it is *locked*: exactly one tick per presented frame,
whatever that frame's own delta says. The Mega Drive ran one game loop per
VBlank and slowed down when it overran; locked, a dropped frame likewise
costs one hold and no catch-up jump. A 59.94 Hz display gets one tick per
field, as the hardware did at 59.92.

The judgement has to survive the things it exists to absorb. The first
version used a running average with a 5% band, and a single missed
refresh (33 ms) moves such an average by 0.83 ms -- exactly the width of
the band -- so every dropped frame and every level load knocked it out of
lock for a few frames and produced the catch-up jump it was meant to
prevent. The estimate is now the mean of the last 64 frame times with the
8 longest and 8 shortest discarded, which no isolated frame can move, and
the lock has hysteresis: taken within 4% of a tick, given up beyond 8%.

Anything else -- 120 or 144 Hz, vsync off, a machine that cannot keep up
-- is *free* and uses the accumulator, which keeps game time right at the
cost of uneven steps. 120 Hz gets a tick every other frame; 144 Hz and
similar judder inherently, since 60 does not divide them, and only frame
interpolation would fix that.

At startup the SDL build prints what is driving the screen:

    display: video wayland, renderer opengl, 60.00 Hz, vsync on

`PS2_TRACE=1` prints every frame. `PS2_PACE=1` prints one line every five
seconds, which is the one to read first:

    pace: 300/300 locked  est 16.68 ms  dt 15.9..17.6  off-beat 0  long 0  work max 0.94 ms

`off-beat` counts frames that ran 0 or 2+ ticks (always 0 while locked),
`long` counts frames that took more than a tick and a half -- missed
refreshes -- and `work max` is the most the loop spent ticking, drawing
and composing, as opposed to waiting on the display. Jerkiness with
`long` at 0 and everything locked is not coming from this loop.

When `long` is not 0 but `work max` is a millisecond or two, the loop is
ready in time and the frame is still arriving late, which puts the cause
in the wait for the display: the video backend, the renderer, or another
thread holding the CPU. `PS2_NO_AUDIO=1` runs without opening the sound
device, so no audio thread exists; SDL's own `SDL_VIDEO_DRIVER=x11` (or
`wayland`) and `SDL_RENDER_DRIVER=opengl` (or `vulkan`, `gpu`, `software`)
switch the other two. `PS2_SNDLOG=1` prints every chip write the
sequencer emits, with its tick, on stderr.

## Fullscreen (build 100)

FULLSCREEN in the Escape menu is SDL3's borderless fullscreen
(`SDL_SetWindowFullscreenMode` with no mode, then `SDL_SetWindowFullscreen`):
the window takes the desktop's own size on the display it is on, and no
video mode is switched. `saves/settings.json` keeps it as `"fullscreen":
true|false`; a file without the key is windowed. main.c applies it at start
and when the menu changes it, as it does the controls and the aspect.

Nothing new places the frame. `platform_present` asks the renderer's output
size every frame and lays the frame out from it (`frame_layout`: the largest
whole multiple of 224 lines that fits, that many lines' width at the pixel
aspect, bars round it), and the whole-multiple render target is remade
whenever that size changes -- so the first frame after the window changes
size, fullscreen or back, is placed for the new size. A window system that
applies the change late costs a frame or two at the old placement, not a
wrong one. On a 1920x1080 display that is 4x up and down (896 lines, 92
lines of bar top and bottom); on 2560x1440, 6x (1344, bars of 48); on
3840x2160, 9x (2016, bars of 72). The toggle prints one `display:` line
with the output size in pixels, the desktop's size and pixel density, and
where the frame landed. If the window system takes the window out of
fullscreen by itself, the platform follows it (the window events), but the
menu's ON is not updated until the next press.

What it gives the subpixel simulation, and nothing more is done for it now:
the display's native size. A CRT's shadow mask or aperture grille has to be
drawn onto the display's own subpixels -- a phosphor triad mapped to whole
RGB stripes -- which needs the output to be the panel's pixels one for one,
unscaled by the window system and at a known size. Borderless fullscreen is
that: the whole panel, at its native resolution, with no mode change, so the
mask pattern can be laid out once for the panel's size and the frame's
placement in it. (On a display the desktop scales, the window also needs
`SDL_WINDOW_HIGH_PIXEL_DENSITY` for the renderer's output to be the panel's
pixels rather than scaled points; the window is created without it now.)

## Text

`script_id` is a bank in the high byte and a 1-based entry in the low
byte. `LoadScript` (ps2.asm:25476) turns the bank into an index into
`GameScriptPtrs` with `lsr.w #6` and `andi.w #$3FC`, so `$1401` is entry
`$14`, `Script_Opening`, script 1. The entry then walks a table of deltas
at the head of the block: the first is the size of the table itself and
each one after is the length of the script before it, so entry 1 lands on
the first script rather than on the table.

`tools/extract_script.py` pulls all 26 blocks, 611 scripts.

**Number by the delta table, not by label.** A delta can be zero --
`dc.b loc_1A43E-loc_1A43E` -- which makes that entry the same text as
the one before it. Four blocks do this: RolfHouse (`$E`, `$15`, `$16`),
LevelEvents (five), Battle (two) and ItemAction (one). Numbering
scripts by their labels gives a shared label one number, and every
entry after it comes out one short -- three short in RolfHouse by
`$17`, which put "Hey, Shir is coming back!" at `$924` instead of
`$927`. The extractor now reads the delta table's minuends in order and
emits every entry, duplicates included. Nothing ported before build 33
read past a zero-length entry in its bank, so nothing on screen was
wrong yet. Control codes
survive as readable tokens: `{char}`, `{enemy}`, `{tech}`, `{item}` and
`{meseta}` for the `$BB`..`$C0` substitutions `LoadScript` resolves from
party state, `{pause}` for `$C3`, a newline for `$C1`, and `{end:XX}` for
the `>= $C4` terminator that also raises `window_active_flag`.

**Two glyph bases, and they are not the two `charset` blocks.** Those
blocks (ps2.asm:12534 and 25327) map letters identically; the first
simply omits digits. What actually splits is where the glyphs sit in
FontsIconsArt. A CHARSET code plus 28 reaches the letters -- `'A'` is
code 11 and tile 39 -- and the punctuation at `$3F`..`$46` follows
straight after `'z'`. But tiles 0..38 are item icons, so the digits
cannot sit below `'A'`: `'0'`..`'9'` are tiles 118..127, and a digit
code (1..10) adds 117 instead. Code 0 is not a blank either -- it means
leave the cell alone, since 28 + 0 is an icon.

`loc_978C` redraws the whole box each frame at a width that grows by
two interior columns per frame -- and shifts its start address right by
half the remaining width (`andi.w #$FFFE, d5` then `add.b d5, d0`), so
the narrow box is centred and both sides move outward one column per
frame. Thirteen frames for the big box. Closing (`loc_9872`) is the
mirror: the right border written one column further left each frame
with the map restored beyond it, then `a1` reset and the same from the
left, so the box shrinks to its centre. The open is part of `demo_timer`'s 270 and the camera cannot
move until the close finishes and `$FFFFDE70` drops to zero, so each
message costs 283 fields, not 270.

`WinID_ScriptMessageBig` is `$488E`: a nametable byte offset of `$880`
past the plane base, which is row 17 at `$80` bytes per row, and `$0E`
bytes, which is column 7. Window positions are **relative to the
camera** -- ProcessWindows builds the address from the camera's own tile
position, `(F718 & $F8) << 4` plus `(F71A & $1F8) >> 2`, and adds the
window offset to that. (build 133) Before it sets a new window up it
tests the main camera's x and y against the 16px grid and returns while
either is off it (`loc_9478`): the word waits, so a window asked for while
the screen scrolls under a step opens as the step ends. A close, a window
already opening and the script's typing are not tested. The port has it
in `win_process` (`WinSys.camera_off_grid`), set by the level screen.
(build 133a) BuildingScreen (`loc_839A`) and BattleScreen write 0 to both
cameras on the way in, so the test holds a window only on the level screen.

