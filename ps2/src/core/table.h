#ifndef PS2_TABLE_H
#define PS2_TABLE_H

#include "ps2_types.h"

/* Small inline data tables lifted from ps2.asm that drive screen effects.
 * (build 136) A colour ramp is a picture: table_load_ramp. (build 136a)
 * The dissolve order is JSON: table_load_dissolve. (build 136e) The last
 * table container, the credits, is text (core/credits.h), and the
 * "PS2B"/"PS2W" container readers, table_load_bytes and table_load_words,
 * went with it. */

#define TABLE_MAX 512

typedef struct {
    int count;
    u8  value[TABLE_MAX];
} ByteTable;

typedef struct {
    int count;
    u16 value[TABLE_MAX];
} WordTable;

/* (build 136) A colour ramp (assets/palettes/, ps2png.write_ramp): an
 * indexed PNG whose pixels, in reading order, are the table's CRAM words --
 * each pixel's colour to the nearest DAC level, as palette_load does a
 * PLTE. The picture is the ramp. Refuses, naming it, a pixel whose index is
 * past the PLTE, and more than TABLE_MAX pixels. */
bool table_load_ramp(WordTable *t, const char *path);

/* (build 136a) The dissolve order (FadeEffectArray,
 * assets/tables/dissolve_order.json): "order", DISSOLVE_STEPS pixel
 * positions [x, y], each to its byte y * 8 + x -- the pixel's place in the
 * tile, which tileset_reveal_pixel takes. Refuses, naming it, a count
 * other than DISSOLVE_STEPS (the dissolves step that many) and a position
 * that is not two whole numbers 0-7. */
#define DISSOLVE_STEPS 64
bool table_load_dissolve(ByteTable *t, const char *path);

#endif /* PS2_TABLE_H */
