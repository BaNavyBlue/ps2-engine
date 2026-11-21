# Palette effects: the survey (build 138)

The owner's direction after build 136d: the art goes full colour all the way
through, every palette effect rebuilt as a colour effect, the old palette
path kept callable as a reference (a frame drawn both ways and compared is
the proof) and unused once replaced. This page is the first step, a
**read-only survey**: every routine in the ROM that writes the palette,
what it writes and when, what kind of operation it is, what the port does
today, and how a full-colour renderer would do the same. **Build 138
changes nothing under src/ or assets/.**

How the list was made, from the code rather than from memory:

* every use of the live palette's label (`palette_table`, $FFFFFB00: 15
  lines) and of the raw addresses beside it, the live palette and the fade
  target at $FFFFFB80 (35 lines; the target has no label in
  ps2_constants.asm);
* the destinations in PalettePtrs (`dc.w $FBxx` / `dc.b $FB, $xx`), and
  every call of the two loaders that use them (26);
* every write to colour RAM itself (the VDP's CRAM write command, `$C0000000`
  and the DMA's `$C000`): three;
* every caller of each palette routine, followed to what calls it, and the
  state variables those routines use ($F626/$F628, the fade range;
  $F632/$F634, the cycles' step and wait; $F650-$F65A and $CC98, the
  battle's lights).

Names first, ROM labels in parentheses, as everywhere in the docs. A
palette position is written "line L, entry E" (entries 0-15, line 0 entry
0 the backdrop); the port's `Palette.entry[]` index is L x 16 + E.

## How the palette reaches the screen

* **Two buffers.** The live palette (`palette_table`, $FFFFFB00, 64 words)
  is what is shown; the fade target ($FFFFFB80, 64 words) is what the fades
  walk toward and what a few routines restore from. A palette word is
  `0000 BBB0 GGG0 RRR0`, three bits a gun.
* **One copy a frame.** The vertical blank (VBlankSub_1C_24, run by every
  vertical-blank routine but one) DMAs the live palette's 64 words to colour
  RAM, then the sprite table. The one that skips it is the pause loop's
  (CheckGamePause, routine $20), which only reads the pads: the shown
  palette holds while paused, and nothing writes it then anyway.
* **Colour RAM is written nowhere else** but at power-on (VDPSetupGame
  zeroes it) and on a failed checksum (ChecksumError fills it with $00E,
  red, and stops).
* **No mid-frame changes.** The horizontal interrupt is off (register 0
  $8006) and its handler is a bare `rte`. Every frame is drawn with one
  palette, top to bottom.
* **The backdrop is line 0, entry 0** (register 7 $8700, never changed).
  Shadow/highlight is off (register 12 $8C81) and never switched on.
* **The fades block.** Each fade is a loop of `WaitForVBlank` with nothing
  else in it: no object, cycle or window runs while one does. So a fade's
  frame depends only on each colour and the step -- which is what lets a
  fade become colour maths on the final pixel.

For full colour this is good news: one palette a frame, no raster tricks,
no shadow/highlight. A frame is its pictures' colours plus one backdrop
colour, and every effect below is a function of a pixel's colour, of the
palette entry it was drawn with, or of the frame's step.

## The four kinds

1. **Loads.** A picture's colours arriving with it. In full colour the
   picture simply carries its colours; there is nothing to do at run time.
   The work is in the assets (below, "Shapes the ROM recolours").
2. **Whole-palette operations.** Every entry treated alike (the fades, the
   damage floor's white field). In full colour, colour maths on the final
   pixel: a function of the pixel's own colour and the step.
3. **Index-dependent operations.** Certain entries rewritten while
   pictures drawn with them are on the screen (the cycles, the sweeps,
   Mother Brain's lights, the library's overlay). In full colour the
   picture ships a map of which pixels listen to which entry -- a slot
   map -- and the effect writes the slot's colour.
4. **The backdrop.** Line 0 entry 0 shows wherever nothing is drawn. Three
   effects change only it. In full colour it is one colour of the frame,
   not of any picture.

## 1. Loads

PalettePtrs (ps2.asm:26819) holds, per entry, the source, the destination
($FBxx) and a count. **PaletteLoad1** copies an entry into the fade target
(it adds $80 to the destination); **PaletteLoad2** copies it straight into
the live palette, shown at the next vertical blank. File names are
`assets/palettes/<name>.png`, from tools/asset_names.py.

| When | What | Buffer | ROM |
|---|---|---|---|
| The Sega screen | `sega_logo` | target | SegaScreen |
| The title, the intro, game over | `title_screen` | target | TitleScreen, IntroScreen (loc_8950), GameOverScreen |
| A level | the level record's palette (lines 2-3), then `field_common` (lines 0-1) | target | LoadLevelData |
| Nei's death and her body (the level one-shots 1 and 2) | `nei_death_picture`, `nei_body_picture`, line 1 | target | loc_7E02, loc_7E6A |
| A building | `field_common`; then Rolf's portrait, the library's graph, the title's (Tyler's spaceship) or Lutz's | target | BuildingScreen |
| A building's visitor, and the party picks in Rolf's house | the portrait's palette, line 2 | both | LoadPortraits |
| Tyler's spaceship, its last picture | `portrait_central_tower_outside_2`, lines 2-3 | both | loc_CD50 |
| The library's "overlap the graphs" | `library_graph_overlay`, line 1 | live | loc_C4B2 |
| A battle | `battle` (all four lines), then each enemy group's fifteen colours into line 2 (first group) or line 3 (second), entries 1-15 | target | BattleScreen, loc_796 / loc_8D4 |
| The flight | `flight_from_dezolis` or `flight_from_motavia` (line 3), `flight_spaceship` (line 2) | target | loc_C5EA |
| The ending | `title_screen`; each page's (`ending_page_1`-`6`) as its slide starts; `ending_wipe`; `ending_last_picture` | target; pages and wipe live | EndingScreen, loc_7932 |
| The dream's second picture | `dream`, line 3 | target | loc_D59C |

Single entries written over a load (patches):

* **A level on the Skure, cave or Dezolis dungeon tilesets** (1, 3, 6):
  target line 1, entry 7 = $ECA, with a piece of extra art beside it
  (LoadLevelData, loc_9222).
* **Mother Brain's battle**: the target's backdrop = black, over the battle
  palette's dark blue $200 (BattleScreen).
* **The ending's title**: target line 3 cleared (EndingScreen); **its last
  picture**: target line 1, entry 1 = $A44, the credits' lower case colour
  (EndingScreen, after loc_77B2).
* **The dream**: live line 3 cleared once the first picture has dissolved
  out (loc_D572).

None of these is an effect. In full colour each is the picture's own
colours, except that the patches show the ROM sometimes changes one colour
of a shared palette for one place: those places get their own picture.

Two loads go straight to the live palette while a picture may be on the
screen: the ending pages and the wipe (the plane is cleared at each page's
end, loc_799A, and the next picture is drawn scrolled off-screen before its
palette loads, so by this reading no frame shows a picture in another's
colours -- the proof will say for certain) and the portraits (art and
palette change together). The library's overlay is the exception and is an
effect: section 3.

## 2. Whole-palette operations

All four fades take 21 frames ($14 + 1) and move **one channel of each
entry a frame**, one level, in a fixed order. They work on the range
$F626-$F628, which every fade sets to all 64 entries -- except the dream's,
which sets line 3.

| Fade | Rule, per entry, per frame | ROM |
|---|---|---|
| To black | red down one level if not 0, else green, else blue | PaletteFadeFrom, PaletteFadeOut, PaletteDecreaseColor |
| In from black | zero the range first; then blue up while the word stays at or below the target's, else green, else red: blue reaches the target, then green, then red | PaletteFadeTo, PaletteFadeIn, PaletteAddColor |
| To white | red up to 7, then green, then blue | loc_634E, loc_636C, loc_6382 |
| From white | fill the range with $EEE; then blue down while the word stays at or above the target's, else green, else red | loc_62CC, loc_6302, loc_631E |

Who calls them:

* **To black**: the Sega screen, the title, a level (unless arriving at
  Mother Brain), a building, game over (after its dissolve out), the
  intro, the roof before the flight to Skure (loc_C572), Ustvestia's lesson
  (loc_C7AC: black for 120 frames under Musik, then in again), Gaira's
  control panel after the explosion (loc_CC10).
* **In from black**: the Sega screen, the title, a level (unless arriving
  at Mother Brain), a building, game over, the intro, the flight
  (loc_C5EA), Ustvestia's lesson.
* **To white**: entering a battle, the ending's start, before the ending's
  last picture, and a level when the arrival at Mother Brain is next
  (LevelScreen, event_flags $E00, set by the approach, loc_70D6).
* **From white**: a battle, the ending's title and last picture, the
  arrival at Mother Brain.
* **The dream's fade** (loc_D59C into PaletteFadeTo's body, loc_61F4, with
  the range set to line 3): in from black on line 3 alone -- the second
  dream picture, which is the only thing drawn in line 3 then.

**The damage floor** (loc_7108): a step onto a damage block (Motavia's
dungeons $1A, $46, $77, $78; the caves' $63) sets all 64 live entries to
white ($EEE) for one frame with its sound, then copies the **fade target**
back into the live palette -- the level as loaded -- and waits seven more
frames.

**In full colour.** Because the fades block, each frame of a fade is a
function of one colour and the step s (1-21), the same for every pixel.
In levels (0-7 a gun, the DAC ladder's index):

* to black: `r' = max(0, r - s)`; `s2 = max(0, s - r)`;
  `g' = max(0, g - s2)`; `s3 = max(0, s2 - g)`; `b' = max(0, b - s3)`;
* in from black toward the pixel's colour t: `b' = min(t.b, s)`;
  `g' = min(t.g, max(0, s - t.b))`; `r' = min(t.r, max(0, s - t.b - t.g))`;
* to white and from white: the same, mirrored toward 7, in their own
  orders (red first going up; blue first coming down).

A pixel's colour is the shown colour (to black, to white) or the
picture's own (in from black, from white), so a fade needs nothing but the
frame it is fading. For art whose colours sit on the ladder -- all of it
today -- this is exact, and the proof compares pixels. For colours off the
ladder (hi-res art later), the level is a real number read through the
ladder between its measured points; the question for the owner below is
whether the stepped look stays. The dream's fade applies to one picture
(its layer), not the frame. The damage floor's white field is the whole
frame white for one frame; its restore is "draw the level as loaded".

## 3. Index-dependent operations

Each of these rewrites certain entries while pictures drawn with them are
on the screen. They run inside their screen's loop, every frame, gated by
a wait counter ($F634) and stepped by a counter ($F632) that **nothing
resets on entering a screen** (their phase carries over).

**The Sega logo's sweep** (PaletteCycle; the ramp is
`assets/palettes/sega_logo_sweep.png`, 31 colours). After the fade in, an
11-colour window slides down the ramp into line 0, entries 2-12, a word
every four frames, 21 steps, then stops. *Port*: game.c
`palette_cycle_sega`.

**The level cycles** (loc_6430, every frame of the level loop), by the
level's tileset ($F710):

* *Motavia's dungeons* (tileset 5: the tubes, Shure, Nido, Roron, the
  dams, the lab, Climatrol, the tower, Gaira), every eighth frame:
  line 2, entries 14-15, a two-colour blue pulse (loc_65D8's first eight
  pairs); in the Biosystems Lab B1 also entries 12-13 (its other eight);
  line 3, entry 12, a sixteen-step warm ramp, brown to near-white and back
  (loc_6618); line 3, entries 13-14, two oranges swapping each step.
* *Gaira* (also tileset 5), on top: while the off-orbit flag is set
  (FLAG_GAIRA_OFF_ORBIT, $C73E, set by the explosion), line 2 entries
  2-10 through four nine-colour sets, grey to blue and back over a
  sixteen-step cycle, with the alarm sound at its fifth step (loc_6668,
  loc_6678); and always, line 3 entries 3, 8 and 10 through four sets
  (loc_64E8, loc_6650).
* *Motavia's towns* (tileset 2, levels before Aukba), every eighth frame:
  line 3 entries 12-13 alternate between two pairs of oranges.
* *Uzo* (tileset 3, Uzo alone), every sixteenth frame: line 2 entries
  11-13 rotate three warm colours (loc_6638).
* *Noah* (tileset 6, Noah's levels), every eighth frame: Gaira's line 3
  set (loc_64E8), and line 2 entry 13 at $442 -- or, during the
  confrontation with Mother Brain (from her first message, loc_E128, to
  the event's end, loc_E1D2), a slow red pulse through the first sixteen
  of loc_66C0's colours (its last ten are never read).

*Port*: game.c `level_palette_cycle`, entry for entry the same, with
`cycle_glow` for loc_64E8.

Which pixels show these depends on each level's blocks: the cycle writes
its entries on every level of the tileset. A slot map made from the index
planes finds them exactly (below).

**The building cycles** (loc_66F6, every frame of the building loop):

* *Gaira's control panel*, until its event reaches step 4, every eighth
  frame: the backdrop red ($00E) for eight steps of sixteen and black for
  the other eight, the alarm at the start of each sixteen; line 2 entry 5
  through four blues (loc_6790).
* *Tyler's spaceship* (building $11), from its event's step $A, every
  fourth frame: line 3 entries 12-15 through four sets (loc_6798).

*Port*: game.c `building_palette_cycle`.

**The battle's lights** (loc_67B8, every frame of the battle loop, by the
mode $CC98, cleared on entering a battle):

* *The Nei Sword's light* (mode 1): when an attack with the Nei Sword makes
  its one-in-sixteen roll and cures the party's status against Dark Force
  (Obj_CharactersBattle, loc_1C1C on), every second frame the backdrop goes
  through $620, $A62, $EA6, $EEA, $EEE -- a burst of warm light -- and back
  to the battle's dark blue $200 (loc_694C), then the mode clears.
* *Mother Brain's lights* (mode 2, set when her battle's dissolve in ends),
  every fourth frame, three parts:
  - the chase: one lit entry walks along line 3, entries 8-15, the one
    before it set to black; its colour rises through four blues over the
    first lap (loc_6958), then alternates between two (loc_6978). It
    skips a step while the backdrop is red (a hit, below);
  - after the first lap, the glow: line 2 entries 8-15 build up over eight
    steps (loc_6998), then an eight-colour window slides along a
    22-colour white-cyan-blue ramp (loc_6A18);
  - with the glow's slide, the rainbow: line 2 entries 1-6 build up over
    six steps through blues (loc_6A44), then a six-colour window slides
    back along a 42-colour rainbow (loc_6A8C).

  This is the "palette swap" Mother Brain is remembered for: her picture
  is drawn once and these three cycles light it. Nothing else in her
  battle writes the palette but the hit flash.
* *The red damage flash* (Obj_CharactersBattle's hurt blink, loc_1BA8 and
  loc_1BE4): every sixteen counts of a hit member's blink, at count 8 the
  backdrop goes red ($00E), at count 0 back to $200 with the red-screen
  sound. In Mother Brain's battle line 3 entries 8-15 go red with it and
  black after it (the chase rebuilds them), and the backdrop goes back to
  black.

*Port*: battle.c `palette_effects` and `mother_brain_lights` (the tables
`CHASE_*`, `GLOW_*`, `RAINBOW*`, `NEI_SWORD_LIGHT`); battle_obj.c the red
flash.

**The credits' fade** (loc_6AE0; the ramp is
`assets/palettes/ending_credits_fade.png`, nine rows of four): one row into
line 3, entries 9-12, every eighth credits step, the last (black) row
first, so the four colours brighten to the top row; reaching it starts the
UFO. *Port*: ending.c `credits_palette_fade`.

**The library's graph overlay** (loc_C4B2, after "overlap the graphs",
script $1012): `library_graph_overlay` straight into the live line 1,
under the graph on the screen. The graph's own palette paints entries 5, 8
and 10-14 the same dark blue as the background (`portrait_library_graph`);
the overlay gives them reds, cyans, white and violet, so lines hidden in
the picture appear. *Port*: not ported (see "What the port does today").

**In full colour.** A slot map with the picture: one byte a pixel, 0 for
"no effect", else the palette entry the pixel was drawn with (line x 16 +
entry + 1), written only where some effect writes that entry. The
extractor can make it from the index plane it already has, so it is exact
by construction, not drawn by hand. At run time each effect keeps writing
its entries as today -- into a small table of slot colours -- and a pixel
with a slot takes the slot's colour instead of its own. Counting what the
ROM writes, the slots in use at once are few: the sweep 11, a level's
cycles at most 16 (Gaira), Gaira's panel 1 and Tyler's monitor 4, Mother
Brain 22, the credits 4, the library 7. The library's overlay could
instead be a second picture (the graph with its lines showing).

## 4. The backdrop

Line 0 entry 0. Three effects change it alone: the battle's red flash
(and its return to $200 or black), the Nei Sword's light, Gaira's panel
alarm. In full colour the frame has a backdrop colour of its own, drawn
where no picture covers, and those effects set it. Everything else that
loads line 0 sets it to its entry 0 (black on most screens; the battle's
dark blue).

## Not palette effects

Read for their palette side and found to have none: the dissolves --
the title's figures and logo (FadeArtTiles), the dreams and Tyler's
monitor (loc_6BBE, loc_6C54), a battle's enemies in (loc_86DA) and game
over's dissolve out -- which write tile pixels only; every technique's
effect and every hit on an enemy (sprites); the pause (the copy stops, the
palette is untouched). No technique flashes the screen.

## Shapes the ROM recolours

The ROM stores a shape once and colours it by the palette line it is
drawn with. Full colour turns each (shape, colours) pair into a picture.
Counted from the port's own data:

* **Enemies**: 126 enemies (battle.json) are drawn from 42 shapes with 123
  distinct fifteen-colour sets; a group draws in line 2 or line 3 as it
  is first or second.
* **Level tiles**: Motavia's dungeons' tileset is shown under 11 palettes
  (the four dams are one set of tiles in four colours), Dezolis's
  dungeons under 6, the caves under 3, the towns under 2 (levels.json):
  24 tileset-and-palette pairs for 6 tilesets.
* **Scene objects in lines 2-3** take whatever screen's colours are
  loaded: the two dungeon doors, the pump, the monitor's explosion, the
  spaceship, the Esper mansion's pieces and door, the ending's UFO
  (scene_objects.json). The townspeople draw in lines 0-1, the same on
  every level.
* **The font** draws in line 0 (white) and line 1 (the credits' blue
  lower case).

## What the port does today

src/core/palette.[ch] keeps CRAM words (`Cram`) and the operations on
them: `palette_load` / `palette_load_entry` (PalettePtrs by name),
`palette_fade_step`, `palette_fade_from_white_step`,
`palette_fade_to_white_step`, `palette_add`, `palette_cycle`,
`cram_to_rgb8` / `cram_from_rgb8` (the measured ladder). The renderer is
indexed to the end: video.c composes a 320x224 plane of indices 0-63 (and
16 of the Escape menu's overlay, above the game's), and the platforms
turn them into light -- platform_sdl3.c and platform_headless.c through
`cram_to_rgb8`, the CRT chain (crt.c, crt_gpu.c and the shaders) from the
index plane and an 80-entry palette texture. The routes' PNGs are written
the same way. (build 146a) Not under a picture: a portrait's pixels have
no palette entry, so the palette path takes their colour from the colour
plane, and a route's shot with a picture in it is an RGB PNG.

Every effect in sections 2-4 has its C function (named above), with four
differences from the ROM found by this reading. **All four are fixed in
build 138d** (full colour's step 1), each with a route that fails on 138c:
`fade_channel_order`, `mother_brain_white`, `damage_floor_restore`,
`library_graph_overlay` (docs/history.md, Build 138d). As found:

1. **The fades to and from black move all three channels at once.**
   game.c's `fade_out_step` is `palette_add(-1)` and `fade_in_step` is
   `palette_fade_step`, both a level on every channel a frame. The ROM
   moves one channel a frame per entry (red first going down, blue first
   coming up), so the port's fade is done in 7 frames and holds for 14,
   with different colours on the way. palette.h's comment says it matches
   PaletteFadeTo/In/Out; it does not. Every black screen change, the
   lesson, Gaira's panel, game over (`GO_FADE_IN`) and the dream's line-3
   fade (`DREAM_FX_PALETTE_IN`) use them. The fades through white are the
   ROM's (`palette_fade_*_white_step`, written from loc_631E and loc_6382).
2. **The arrival at Mother Brain fades through black.** walk.c's
   `approach_mother_brain` changes level with `goto_screen` (black, both
   ways); LevelScreen goes to white and comes back from white when
   event_flags is $E00.
3. **The damage floor restores the palette as it was shown.**
   `level_dangerous_floor` saves the live palette and puts it back; the ROM
   copies the fade target back, the level as loaded, so the cycling
   entries show their loaded colours until the next cycle step.
4. **The library's overlay is not loaded.** building.c's `LB_GRAPH_PALETTE`
   step says "PaletteLoad2 $32: into the fade target only ... Not ported";
   PaletteLoad2 writes the live palette (it is PaletteLoad1 that adds $80
   for the target), so in the ROM the graph's hidden lines appear and in
   the port they do not.

## The colour path (build 141)

Step 3 as built (src/core/video.[ch], `VideoColours`; docs/history.md,
Build 141). The colour plane is made from the colour path's own state and
never from the live palette:

* **The pictures' colours** -- while the art is indexed, a colour per
  palette entry, written by every load, whether the ROM loads into the live
  palette or a fade target: the colour path has one set of picture colours
  and the fade in brings a target's up, since every load into a target is
  made while the screen is covered (black or white). The comparison holds
  that true on every route.
* **The glow** -- the colours the index-dependent effects write, each entry
  lit from the effect's write until its picture next loads. The level and
  building cycles, the Sega logo's sweep, Mother Brain's lights, the
  credits' fade and the library's overlay are glow; so are the backdrop's
  flashes (entry 0: the backdrop is the frame's own colour, its picture
  colour the screen's as loaded). A pixel drawn with a lit entry shows the
  glow colour -- the slot plane, while the art is indexed, is the index
  plane where lit; with glow masks it is the composed mask. (build 146) A
  picture's pixel takes its entry from the picture's slot map (art.h,
  tools/extract_pictures.py's SLOTS), not from the tile under it: the
  radar's DANGER (line 2 entry 5, Gaira's control panel) and every line of
  the library graph (line 1, the overlay). A picture with no slot map, or
  a pixel its map leaves 0, no effect recolours; over every route only
  the radar's entry 5 was ever lit under a picture before 146.
* **The whole-frame effect** -- one of the four fades or the floor's white
  field, and its step, worked out pixel by pixel after compose (section 2's
  formulas, each channel's level on the DAC ladder, stepped one level a
  frame in the ROM's channel order; a colour off the ladder is read between
  its points, so full-colour art fades the same stepped way). A fade out
  starts from what is shown -- the frame, or a screen already all black or
  white (a second fade to black over a black screen keeps it black) -- and
  a fade in comes up to the pictures' colours. The dream's fade applies to
  its own picture (its palette line, while the art is indexed).

Every palette operation in the game is one `video_*` call (`video_colours_
load`, `_clear`, `_copy`, `_set`, `video_glow`, `video_glow_load`,
`video_backdrop`, `video_target_from_shown`, `video_shown_from_target`,
`video_fade_begin`, `video_fade_step`, `video_flash_white`), each making the
palette path's write as before -- the reference -- and the colour path's.
The game shows the colour plane; `PS2_DRAW=palette` draws the reference.
The GPU chain still takes the index plane and the palette until 141a.

Found while converting, fixed in build 141b: LoadLevelData's patch for the
Skure, cave and Dezolis dungeon tilesets (line 1 entry 7 of the target set
to $ECA, beside 23 tiles of loc_412F8 at VRAM $3020, tiles $181-$197,
ps2.asm:13381) was not in the port. It is the mist: those tilesets'
backdrop blocks draw the 23 tiles in line 1, white and $ECA (docs/
history.md, Build 141b).

## Full colour: how the renderer would do each

* **A pixel carries its colour beside its index**: video_compose writes a
  colour plane (and a slot plane) next to the index plane, from the same
  tiles and sprites, so both paths exist side by side. While the art is
  still indexed, a pixel's colour is its entry's colour in the palette as
  loaded -- the picture's own colour -- and its slot is its entry where an
  effect writes it.
* **Loads** set the pictures' colours; nothing happens per frame.
* **Whole-palette operations** run on the colour plane after compose (the
  formulas in section 2), with the step their screen already counts.
* **Index-dependent operations** keep their code; their writes land in
  the slot table, and the colour plane takes a slot's colour where a pixel
  has one.
* **The backdrop** is the frame's own colour.
* **The CRT chain's first step** ("index to light", docs/crt.md) becomes
  "colour to light": a gun's level read from the colour through the ladder
  (or, off the ladder, between its points). The GPU path uploads a colour
  frame instead of the index plane and the palette texture, which changes
  the shaders -- the owner's `make shaders` on his machines, as for any shader
  change.
* **The proof**: every frame of every route drawn both ways -- the palette
  path's index plane through the palette, the colour path's colour plane --
  and compared pixel for pixel; then, as each kind of asset becomes a
  full-colour file, the same comparison with the new file in place of the
  indexed art and its palette.

## Proposed order (the next full-colour build on)

1. **(Step 1, done in build 138d) The reference first**: the four
   differences above fixed, each with a route that fails on the old code
   and passes on the new, in a build of their own since they change
   routes' frames by intent. The owner to check them in the emulator.
2. **(Step 2, done in build 140) The colour plane**: video_compose writes
   a colour plane beside the index plane, the platforms and the PNG writer
   can draw from either, and a check draws every route's frames both ways
   (every route file the same, the comparison clean). The palette path
   stays the one shown. The slot plane is left for step 3: while the art
   is indexed it would be the index plane again, and with glow masks
   (The owner's choice, docs/asset_format.md) it is the composed mask.
3. **(Step 3, done in build 141) The effects on the colour plane**: the
   fades and the floor's flash as colour maths, the backdrop, the slots for
   the cycles, sweeps, lights and the overlay. Proved by the same comparison
   along every route, then the colour path shown. The CRT chain's shaders
   move to the colour frame in build 141a (point 5).
4. **The assets a kind at a time**, each kind's grey sheets and palettes
   going once its full-colour form proves equal: the baked pictures first
   (portraits, the battle floor, the title, the nametable pictures, the
   ending's pages -- README "Baking the rectangles"), as RGB PNGs with a
   slot map where an effect touches them; then the sprites by the atlas
   (README "Sprite mappings to an atlas"), the enemies one picture each;
   then the level tiles, one sheet per tileset and palette, with slot maps
   for the cycles. Coloured view sheets for Tiled are moot.
5. **The GPU path** moves to the colour frame with the effects (step 3) or
   last, as the owner prefers, since it needs his `make shaders`.

## Questions for the owner

(Answered after build 138c, "go with your picks": the reference fixed
first (done, 138d); the stepped, channel-by-channel fades kept; one
full-colour picture per use; the GPU path's shaders with the effects,
step 3.)

1. The four differences (fades to and from black, the arrival at Mother
   Brain, the damage floor's restore, the library's overlay): check them in
   the emulator and fix them first, as step 1?
2. The fades step the hardware's eight levels a channel at a time (the
   look: colours lose red first going dark). On full-colour art -- and on
   hi-res art later, whose colours need not sit on the eight levels --
   keep that stepped look, or a smooth fade?
3. The recoloured shapes: one picture per use (126 enemy pictures, 24 level
   sheets, the doors per level) -- simplest and what "full colour all the
   way" suggests -- or one shape plus colour sets kept as data?
4. The GPU path's shaders change for the colour frame: with the effects
   (step 3) or after the assets?
