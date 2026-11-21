# The art as the game draws it: the proposed format (build 139c, for the owner)

**Status: a proposal, partly approved (after build 139d: the folder layout
A and glow masks). Nothing under src/ or assets/ followed it until build
142: the portraits do now (`assets/art/portraits/`, order 2 below).** It is
written for the owner to look at and change before any code; the decisions
still open are his (the last section); his answers so far are the section
before it. Build 139b's handover (job B) and docs/palette_effects.md ("Full
colour", "Proposed order") are where it comes from.

The owner, after build 139a: every visual asset moves off "palette combined
with sprite chunks" to files that look like the thing in the game, which
the game reads -- "human hackable", no export step, no assembly-style
image parsing and palettizing. "World map stuff probably makes sense to
keep as one big sprite sheet, but not portraits or characters and
creatures." The same files serve **high-definition art for a parallel
game-play mode**: no palettes in HD, and no 8x8 pieces -- an HD sprite is a
whole painted frame.

## The principles

1. **A file is what you see.** A picture is a full-colour PNG (RGBA,
   transparent where nothing is drawn) of exactly what the screen shows
   for that thing -- a portrait as a portrait, a monster's frame as the
   monster in that pose. No palette, no tile sheet to assemble, no map of
   8x8 pieces. Open it in any editor, paint, save; the game shows it.
2. **The game reads the file a person edits.** There is no export: the
   extractor writes these files once from the ROM, the game loads them, a
   mod replaces them (mods/ takes the same paths, as for the music).
3. **Native pixels are the game's unit.** Every position, size and anchor
   is in Mega Drive pixels (320x224 a screen). Game logic never sees HD.
4. **Two art sets, one description.** (HD is a bonus: the game is
   complete with the native set alone.) The native set is 1x, exactly the
   ROM's pixels in their colours. The HD set has the same names in a
   parallel folder, each file drawn at a whole-number scale of its native
   size (4x by default) into the same native rectangle. Which set draws is
   a menu setting; a thing with no HD file falls back to its native picture
   scaled up, so HD can be painted a piece at a time.
5. **Timing stays where the game keeps it.** What makes a frame change --
   a monster's attack script with its hit and its overlays, a walk cycle,
   an event's steps -- is game logic and stays in the game's data
   (battle.json's scripts, the object code), which is JSON or C already.
   The art description says which picture is frame N and where it sits;
   it carries timing only for animations that are purely visual (the
   water's tiles, a glow cycle, the title's sparkle) and for HD
   in-betweens (below, "The owner's answers").
6. **Every kind is proved before its old path goes**: every route's
   frames drawn the new way equal the old way pixel for pixel; only then is
   that kind's tile + map + palette loader deleted (docs/palette_effects.md's
   proof, kind by kind).

## The files

Layout A, the owner's choice of the three put to him (B kept both sets in one
folder a thing, C a subfolder a set):

    assets/art/<kind>/<thing>/<thing>.json     the description (shared)
    assets/art/<kind>/<thing>/<frame>.png      native pictures, 1x
    assets/art_hd/<kind>/<thing>/<frame>.png   HD pictures, same names
    mods/art/... and mods/art_hd/...           a mod of either, by path

`<kind>` is `portraits`, `scenes`, `party`, `people`, `objects`,
`monsters`, `effects`, `battle_party`, `levels`, `windows`. Names are the
port's friendly names (asset_names.py), never ROM labels.

**The description**, one a thing:

```json
{
  "thing": "fire_ant",
  "kind": "monster",
  "size": [64, 56],
  "anchor": [32, 56],
  "frames": {
    "1": { "file": "idle_1.png", "anchor": [32, 56] },
    "2": { "file": "idle_2.png", "anchor": [31, 56] }
  },
  "overlays": { "attack": "fire_ant_attack", "technique": "fire_ant_spit" },
  "layer": "under_windows",
  "source": "Battle_AntArt, Battle_AntMap, the enemy's 15 colours (for checking)"
}
```

- `size`: the native rectangle every frame fits in; `anchor`: the point the
  game places (an enemy's feet, a sprite's origin as its mapping has it).
  A frame may give its own anchor where its picture is cut tighter.
- `frames`: keyed by the number the game's script uses, so the scripts in
  battle.json keep working unchanged.
- HD needs no entry: `art_hd/.../idle_1.png` at 256x224 is idle_1 at 4x.
  A file whose size is not a whole multiple of the native picture's is
  refused, naming it.
- `source`: written by the extractor, never read -- what the picture was
  made from, for checking against the ROM.
- (build 146) A frame's `slots`: its glow mask, `<frame>_slots.png` beside
  it, an indexed PNG of the same size whose index at a pixel is the
  palette entry (1-15, in the line the picture is drawn in) an effect
  writes there, 0 elsewhere; the extractor writes one only where an
  effect touches the picture (tools/extract_pictures.py, SLOTS). The
  grey ramp is only so it can be looked at; the index is what counts.
- (build 148) Transparent pixels are the ROM's colour 0, where a tile
  draws nothing and whatever is behind shows: most often the screen's
  black, but the battle's flashes (the backdrop's colour) show through the
  floor. (Build 148 said the stars show through the ending's pages: they
  are black there, their palette line cleared until the final days' page,
  build 149.) A viewer that paints transparency white
  (the Photos app, Paint) shows those pixels white where the game shows
  black; one that paints a checkerboard (GIMP, Krita, Paint.NET) shows
  them as what they are. They stay transparent: made opaque black they
  would hide what the game shows through them.

## Each kind

**Portraits** (the shopkeepers, the party's faces, the Data Memory's
screen): one picture each, the portrait without its window frame (the
window system draws the frame). Today: a tile sheet, a window map and a
palette line.

**Cut-scene pictures** (the dreams, Lutz, Tyler, the ending's pages, the
title, the Sega logo): one picture a plane where the screen has two (plane
A over plane B), each its full drawn area -- the title as the title. A
picture another picture scrolls over stays two pictures.

**The party, townspeople and scene objects on the map**: a frame a facing
and step, whole (the party's walk is 4 facings x its steps). Their
movement and timing stay in the object code.

**Monsters** (126 enemies, drawn from 42 shapes in 123 colour sets):
**one picture set per enemy**, as decided after build 138 -- a Fire Ant
and an Armorant are two folders, so recolouring one never touches the
other. A frame is whatever the enemy's map draws for it, whole: the ROM
mixes plane A cells (most idle frames) and sprite pieces (most attack
frames) in one map set (docs/battle.md), and each becomes one picture.

*The overlays.* An enemy's attack or technique adds one or two pieces
beside its body -- the Locusta's arm reaching up, a claw, a bite, a spit,
a beam -- drawn from the enemy's own art and map. They are not the
party's weapon and technique effects (below); they belong to the enemy.
In the ROM each is a separate object (the enemy's overlay objects, $10
and $11, loc_3166 and loc_31BC): the attack script launches it, it takes
the enemy's art, map and place, and runs its own frame script beside the
body's. 183 of the enemies' attacks and techniques launch one (nearly
every enemy, counted from battle.json's scripts); no idle loop does. The
proposal keeps each overlay as **its own set of whole frames**, placed by
the game as now, rather than baking body and overlay together: the game
moves and times them as two objects, so two sets of pictures is what the
game already draws, and a body frame and an overlay frame can be painted
apart. (Nothing of the enemy boxes is above either: build 100b found the
body's cells and the overlays' pieces both draw over the boxes.) That is
the whole of "parts": no creature combines frames any other way, so no
creature needs a parts system with anchors.

**Effects** (weapons' and techniques' effects, the party's battle poses):
whole frames per object, as monsters.

**World maps** (levels; the owner's "one big sprite sheet"):
- *Native*: a full-colour **block sheet** per tileset and colour set (the
  24 pairs; a block is 32x32, 4x4 of the ROM's tiles, every flip and
  palette line already applied), and the Tiled maps over it as today
  (gid = block + 1), so a level is edited in Tiled with its real colours.
  A block's **high-priority** pixels -- the ones that draw over the party,
  like the overhead pipes -- go in a second sheet beside it,
  `<sheet>_over.png`, so "what is in front" is painted, not flagged.
- *HD*: the ROM builds every level from its blocks, the same 32x32 blocks
  repeated over and over; an HD block sheet keeps that look (every tree the
  same tree). **A painted level** is the other way: the level's plane B and
  plane A each one big picture at the HD scale (Motavia's overworld at 4x
  is 12,288 x 8,192), drawn freely like an illustration of the map -- no
  repeats, no grid -- with its `_over` picture beside it. "Painted" means
  only that: drawn whole rather than assembled from blocks; nothing to do
  with saturation or glow. The build cuts it into GPU-sized chunks and the
  artist never sees a block; the native Tiled map still drives collision,
  doors and encounters, so the painting must keep walls where the map has
  them. Each map patch (a dam drained, a road opened, a door unlocked) is a
  painted overlay picture for its rectangle. A level with no painting
  draws from HD block sheets, and with none of those, the native sheet
  scaled.

**Windows and the font**: a full-colour sheet of the window parts
(corners, edges, fill, cursor, icons) and a font sheet with a JSON giving
each character's cell; native and HD alike. The window system keeps
drawing windows from the parts; text stays text.

## Colour effects in place of palette effects

From docs/palette_effects.md, kind by kind:
- **Fades and flashes** (the black and white fades in the ROM's channel
  order, the damage floor, a hit's red): colour maths on the finished
  frame, the same formulas, native and HD alike.
- **The backdrop**: the frame's own colour.
- **Colour cycling** (Mother Brain's lights, the dungeons' and towns'
  cycles, the water, the Sega logo's sweep, the credits' fade, Tyler's
  monitor, Gaira's panel). In the ROM the picture is drawn once and the
  effect rewrites a few palette entries every few frames; every pixel drawn
  with those entries changes colour. With no palette there are two ways:
  - **A glow mask**: a second picture beside the art, the same size, whose
    pixels say "this pixel takes cycle colour 5" (or nothing). The effect
    still runs every frame exactly as the ROM's does -- it steps its
    counter and sets, say, Mother Brain's 22 cycle colours -- and the
    drawing step paints each masked pixel in its colour's current value.
    On the GPU path that drawing step is a few lines in the shader (the
    mask a texture, the 22 colours a small table updated each frame); on
    the CPU path it is the same lookup in the compose. So it is both: a
    per-frame effect that drives the colours, and a shader (or compose)
    pass that applies them. The extractor makes the mask from the ROM's
    picture, so native is exact by construction; an HD painter paints an
    HD mask beside the HD picture. In HDR the masked pixels can be lifted
    above paper white, which is where Mother Brain's lights and the Nei
    Sword's flash would glow.
  - **An animation**: the picture repainted once per cycle step. Plain to
    edit (each step is just a picture), but heavy where the cycles
    combine: Mother Brain's chase, glow and rainbow run at different
    rates, so her picture would need a frame for every combination.
  The proposal is glow masks, native and HD, with animations allowed for a
  thing where an artist prefers frames.

## The order, once this is approved

1. The renderer's colour layer (docs/palette_effects.md's step 2): every
   frame composed as colour beside the index plane, every route drawn both
   ways and equal; the palette path still shown. (Done in build 140.)
2. Portraits and cut-scene pictures -- the first check **one portrait
   shown native and HD** -- then the rest of them; proved, their old
   loader deleted. (Build 142: the portraits extracted as pictures,
   `assets/art/portraits/`, and drawn from them -- a picture bound to the
   tiles it was made from, its colours shown, the tiles' indices kept as
   the reference -- proved equal to the tiles on every route. Build 145:
   a route draws every one of the 27 pictures, the twelve 142 left
   undrawn among them. The HD check waits for the owner's HD art
   (answer 5 below), so the old loader goes out first: build 146.
   Build 146: the library graph a picture too, and the first glow masks
   -- "slots" in a frame, `<thing>_slots.png`, an indexed PNG whose index
   is the palette entry an effect writes at that pixel: the radar's
   DANGER (Gaira's control panel) and the graph's hidden lines (its
   overlay). Build 146a: the loader out -- no portrait art to VRAM, no
   portrait palette loaded (Lutz's, not a picture yet, stays); the
   palette path takes a picture's pixels from the colour plane, and a
   route shot with a picture is RGB. Every route's frames the same
   colours as 146's but one made to open Rolf's window where the game
   never does. Build 147: the first cut-scene picture, Lutz
   (`assets/art/scenes/lutz/`: LutzPortraitTileInd drawn whole, "at" its
   cell in the table), bound over the table's cells, his art and palette
   no longer loaded. Build 148: the scenes nothing but fades touches --
   the roof's flight's planet (one picture by where the ship goes), Nei's
   face after Neifirst in each case's palette and her closed eyes, the
   battle floor, the ending's six pages and its last picture -- the same
   way. Build 149: the colour cycles, each with its glow mask -- the SEGA
   logo (its sweep), the ending's star field (the credits' fade) and
   Tyler's monitor, the first thing of several frames (`"frames"` keyed
   "0"-"4" as the game numbers them, `tyler_monitor_<n>.png` each with its
   `_slots.png`). The logo's art and palette are no longer loaded; the
   star field's art stays for the ending's UFO, a sprite from the same
   sheet, and the monitor's for the dissolve that brings it and Tyler in,
   which waits for the reveal mask. Build 150: the dream's waking, the
   last picture only fades touch -- its line's own fade in, line 3 alone
   -- its art and palette no longer loaded. The tables left all change
   their tiles pixel by pixel, for the reveal mask. Build 151: the reveal
   mask -- a dissolve as the set of a tile's 64 places it has put in or
   taken out, the same in every tile of its range, so a picture is drawn
   through it (the compositor's, with no file: the order is
   `assets/tables/dissolve_order.json`); the dream's fight and Tyler's
   "Help!!!" through it, their art no longer loaded. An HD picture would
   take the mask a native pixel to its block. Build 152: Tyler's portrait,
   and the monitor, through the dissolve that brings them in; their art
   stays for the monitor's explosion, a sprite from the same tiles. Build
   154: the title -- plane A one picture (the figures and the logo), bound
   once a dissolve's tile range, each with its own mask; plane B and game
   over's background pictures too; their art no longer loaded. Build 155:
   the stars behind Mother Brain, with the chase's slot map, through the
   enemy art's mask (the battle's dissolve in, game over's erase); her art
   stays for her figure. Every nametable picture is a picture now. Build
   156: the sheets of the art the pictures replaced leave the tree; the
   extractors read it from the disassembly (tools/disasm_art.py).)
3. The party, townspeople, objects; monsters and their overlays; effects.
4. World maps.
5. Windows and the font.

Each step a build of its own, with the proof above and the owner's look at
stills (or a short video) of what changed.

## The owner's answers so far (after build 139b)

1. **HD is a bonus feature.** The native set is the game; HD is an
   optional set laid over it, a file at a time. 4x stays the default
   scale, any whole scale allowed per file; nothing in the game depends on
   HD existing.
2. **Timing can be finer in HD.** The game's timing (a script's steps, the
   hit, the sounds) stays the native one. An HD frame may be split into
   **in-betweens**: the HD description lists, for a native frame, several
   HD pictures with their ticks, adding up to the native frame's time, so
   an HD attack can move more smoothly and still land its hit on the same
   tick:

   ```json
   "frames": {
     "3": { "file": "attack_3.png",
            "hd_inbetweens": [ { "file": "attack_3a.png", "ticks": 2 },
                               { "file": "attack_3b.png", "ticks": 2 } ] }
   }
   ```

   A native step lasts two to eight ticks (a sixtieth of a second each), so
   one to seven in-betweens fit at the tick. Finer than a tick (smoothing
   on a 120 Hz screen) would mean blending between pictures at display
   rate; possible later, not proposed now.
3. **The folder layout is A** (after build 139d: "the asset files paths
   sound good to me"): `assets/art/<kind>/<thing>/` holds `<thing>.json`
   and the native pictures, `assets/art_hd/<kind>/<thing>/` the HD
   pictures under the same names; one description a thing.
4. **Glow masks** for colour cycling (after build 139d: "we can try glow"),
   native and HD; animations still allowed for a thing an artist prefers
   to paint in frames.
5. **No HD stand-ins** (after build 144: "Not ready I don't have assets"):
   the HD check waits until there is painted HD art. An upscale made to
   stand in for it (NOTES' plan after 142) is not the check.
6. **HD is drawn without the CRT model, and in HDR** (after build 144:
   "HD really shouldn't need any CRT since it can have native pixel
   resolution, it would be nice to introduce HDR elements when the actual
   art is available"): an HD frame goes to the display at its own
   resolution, with no scanlines, mask or phosphor model, and in HDR where
   the display has it -- the glow masks' pixels above paper white, as
   "Colour effects" above has it. Today the HDR encode (HDR10 or scRGB,
   WHITE and PEAK) is the CRT chain's last stage (CrtSettings; crt_hdr_why
   allows no HDR without the chain), so HD will need that stage on its
   own.

## Open for the owner

(Answered after build 139d and moved above: the folder layout, A; glow
masks.)

1. **Painted HD levels** (above: a level drawn whole as one big HD
   picture, collision still from the native map) as the HD target, with
   HD block sheets as the fallback -- or HD block sheets only. Needed by
   step 4 (world maps).
2. **Overlays as their own frame sets** (proposed), not baked into the
   enemy's frames. Needed by step 3 (monsters).
3. **The extractor's `source` notes** in each description (what the
   picture was made from, never read) -- keep or leave out.
