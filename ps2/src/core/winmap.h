#ifndef PS2_WINMAP_H
#define PS2_WINMAP_H

#include "video.h"

/* A window map is a rectangle of tile indices: every menu, message box and
 * portrait frame in the game is one of these. Extracted from the WinArt_*
 * blocks in ps2.asm rather than hand-transcribed.
 *
 * Portraits are 10 wide by 12 tall: a border row of $B9, ten rows of art,
 * and a border row of $BE. The art indices are into the portrait's own
 * tileset and may start at a nonzero base (Rolf's run from $A0), so the
 * base is supplied at draw time. Tiles repeat -- the compressed art stores
 * only unique tiles, which is why tile counts are not neat rectangles. */

#define WINMAP_MAX_CELLS 1024

typedef struct {
    int width, height;
    u8  cell[WINMAP_MAX_CELLS];
} WinMap;

/* (build 134) A Tiled map, assets/winart/<name>.tmx (before that a "PS2M"
 * container, .map): the bytes back from each cell's tileset and its
 * byte_base property. */
bool winmap_load(WinMap *map, const char *path);

/* Draws rows [row_first, row_last) of the map at (col, row) on the given
 * layer. tile_base is subtracted from each index before it is offset by
 * first_tile, which is where the tileset was appended. */
void winmap_draw(Video *v, LayerId layer, const WinMap *map,
                 int col, int row, int row_first, int row_last,
                 int tile_base, int first_tile, u8 pal);

#endif /* PS2_WINMAP_H */
