#ifndef PS2_TILESET_H
#define PS2_TILESET_H

#include "video.h"

/* A tileset is an indexed PNG: a grid of 8x8 tiles, row-major, in the
 * tileset's own order. The grid is padded to a full last row and the
 * padding tiles are blank, so the count comes out of the image
 * dimensions and nothing needs to record it.
 *
 * Every tileset is loaded once at startup and stays resident. The original
 * spends a lot of effort juggling 64 KB of VRAM; none of that is needed
 * when the whole game's art fits comfortably in RAM. */

bool tileset_load(TileSet *ts, const char *path);
void tileset_free(TileSet *ts);

/* Appends src to dst and reports where it landed, so several sources can
 * share one index space. */
bool tileset_append(TileSet *dst, const TileSet *src, int *first_index);

/* Allocates a blank tileset of `count` tiles. Used to mirror VRAM tile
 * space so nametable entries index it directly. */
bool tileset_alloc(TileSet *ts, int count);

/* Copies src into dst starting at an absolute tile index, matching the
 * VRAM address a load used. Tiles past the end of dst are dropped. */
void tileset_place(TileSet *dst, const TileSet *src, int first_index);

/* Blanks every tile. The original screens open with a DMA VRAM fill to
 * zero; without it, art revealed on a previous visit is still resident
 * and shows through until something overwrites it. */
void tileset_clear(TileSet *ts);

/* DrawArtTiles2 (ps2.asm:12320): src placed as tileset_place places it,
 * with every pixel of colour `clear` made transparent. The game over and
 * ending screens put the font in this way with colour 11, which is the
 * whole of the window fill tile -- so their text sits on the picture
 * with nothing behind it. */
void tileset_place_clearing(TileSet *dst, const TileSet *src, int first_index,
                            u8 clear);

/* Copies one pixel position of every tile in a range from src into dst.
 * Stepping through FadeEffectArray's 64 positions reveals the art the way
 * FadeArtTiles does, a fraction at a time. */
void tileset_reveal_pixel(TileSet *dst, const TileSet *src, int first_index,
                          int count, int pixel);

#endif /* PS2_TILESET_H */
