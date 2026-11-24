#ifndef PS2_EXTRACT_EXART_H
#define PS2_EXTRACT_EXART_H

/* (build 188) The cartridge's two compressions and the indexed PNGs the
 * art is kept as, for the C steps: tools/ps2art.py (DecompressArt's
 * format), tools/nemesis.py (NemDec's) and tools/ps2png.py's tile sheet,
 * flat image and grey ramp. */

#include "extract.h"

#define EX_TILE_BYTES 32

/* Decoded art: `count` tiles of 32 packed bytes, and how many bytes of
 * the stream they took. */
typedef struct {
    u8    *data;
    int    count;
    size_t used;
} ExTiles;

/* ps2art.decompress: false with *why set (ArtError's text) when the
 * stream runs out, for callers that go on; ex_art fails the step. */
bool    ex_art_try(Extract *x, const u8 *data, size_t len, ExTiles *out,
                   const char **why);
ExTiles ex_art(Extract *x, const u8 *data, size_t len);
/* nemesis.decompress, the same way. */
bool    ex_nemesis_try(Extract *x, const u8 *data, size_t len, ExTiles *out,
                       const char **why);
ExTiles ex_nemesis(Extract *x, const u8 *data, size_t len);

/* A colour list for a PNG's palette: up to 256 RGB triples, the rest of
 * the 256 black, as ps2png._put_palette pads it. */
typedef struct {
    u8  rgb[256 * 3];
    int count;
} ExRgb;

void ex_rgb_add(ExRgb *p, u8 r, u8 g, u8 b);
void ex_rgb_cram(ExRgb *p, u16 word);       /* through the DAC ladder */
void ex_rgb_grey_ramp(ExRgb *p);            /* ps2png.grey_ramp(16) */

/* ps2png.write_tile_sheet: tiles of 32 packed bytes (or, `packed` false,
 * 64 indices each) in a grid `across` wide (clamped to 1..count), the
 * last row padded with index 0. *across_out and *rows_out may be NULL. */
void ex_png_tiles(Extract *x, const char *path, const u8 *tiles, int count,
                  const ExRgb *rgb, int across, bool packed, int *across_out,
                  int *rows_out);
/* ps2png.write_flat: a w x h picture of indices. */
void ex_png_flat(Extract *x, const char *path, int w, int h, const u8 *px,
                 const ExRgb *rgb);
/* ps2png.write_ramp: `across` colours a row, pixel i index i. */
void ex_png_ramp(Extract *x, const char *path, const u16 *words, int n,
                 int across);

#endif /* PS2_EXTRACT_EXART_H */
