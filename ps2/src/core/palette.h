#ifndef PS2_PALETTE_H
#define PS2_PALETTE_H

#include "ps2_types.h"

#define PAL_LINES   4   /* palette lines the hardware exposes */
#define PAL_ENTRIES 16  /* colours per line; index 0 is transparent */
#define PAL_TOTAL   (PAL_LINES * PAL_ENTRIES)

/* A CRAM word: 0000 BBB0 GGG0 RRR0. Kept in native format because every
 * fade, flash and colour-cycle routine in the original does arithmetic on
 * the 3-bit channels directly. */
typedef u16 Cram;

typedef struct {
    u8 r, g, b;
} Rgb8;

typedef struct {
    float r, g, b;
} RgbLinear;

typedef struct {
    Cram entry[PAL_TOTAL];
} Palette;

u8   cram_r(Cram c);
u8   cram_g(Cram c);
u8   cram_b(Cram c);
Cram cram_make(u8 r, u8 g, u8 b);

/* Inverse of cram_to_rgb8: the nearest level on the DAC ladder for each
 * channel. Used when a palette comes back from an indexed PNG. */
Cram cram_from_rgb8(u8 r, u8 g, u8 b);

/* Measured VA4 DAC output, not a linear ramp. cram_to_linear is what the
 * CRT chain consumes; cram_to_rgb8 is for tools and previews. */
Rgb8      cram_to_rgb8(Cram c);
RgbLinear cram_to_linear(Cram c);

/* (build 141) The DAC ladder as the scale the colour path's fades step on
 * (video.c, docs/palette_effects.md section 2): a channel's 8-bit value as
 * a level 0-7 -- exact on the ladder's eight points, and read linearly
 * between its measured points for a colour off it (full-colour art
 * later) -- and a level back to its 8-bit value, exact for the eight
 * whole levels. */
float colour_level(u8 v);
u8    level_colour(float level);

void palette_clear(Palette *p);
void palette_copy(Palette *dst, const Palette *src);

/* (build 138d) One frame of the fade out to black (PaletteFadeOut's
 * per-entry step, PaletteDecreaseColor): an entry that is 0 stays; else
 * red down one level while it has any, then green, then blue -- one
 * channel a frame, so white to black is 21. (Until 138d every channel
 * stepped at once, done in 7 frames: palette_add(-1).) */
void palette_fade_out_step(Palette *p);

/* (build 138d) One frame of the fade in toward `target` (PaletteFadeIn's
 * per-entry step, PaletteAddColor): an entry at its target stays; else
 * blue up one level while the word stays at or under the target's, else
 * green the same, else red up one level -- from black, blue reaches the
 * target's, then green, then red, one channel a frame. (Until 138d every
 * channel stepped at once: palette_fade_step.) */
void palette_fade_in_step(Palette *p, const Palette *target);

/* One frame of the fade down from white (loc_62CC's per-entry step,
 * loc_631E): each entry not yet at its target loses one step of blue
 * while that stays at or above the target as a word, else one of green,
 * else one of red -- one channel a frame, so white to black is 21. */
void palette_fade_from_white_step(Palette *live, const Palette *target);

/* One frame of the fade into white (loc_634E's per-entry step, loc_6382):
 * each entry not yet $EEE gains one step of red until red reads $E, then
 * of green, then of blue -- 21 frames from black. */
void palette_fade_to_white_step(Palette *live);


/* Loads a palette from an indexed PNG whose PLTE is the palette -- 16
 * wide, one row per line. Entries land at [line * PAL_ENTRIES, ...); an
 * image holding more than one line's worth fills onward from there. */
bool palette_load(Palette *p, int line, const char *path);

/* (build 97b) PalettePtrs entry `index` (PALETTE_* in rom_ids.h) to
 * `line`, as palette_load does: the entry's file is the one
 * assets/palettes/paletteptrs.json names for it (extract_paletteptrs.py,
 * named in tools/asset_names.py), the table read once. False for an index
 * the table does not have. */
bool palette_load_entry(Palette *p, int line, int index);

#endif /* PS2_PALETTE_H */
