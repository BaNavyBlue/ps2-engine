#ifndef PS2_LEVEL_H
#define PS2_LEVEL_H

#include "video.h"

/* A level is three nested things, which is why 101 of them fit in so
 * little ROM (LoadLevelData, ps2.asm:13229):
 *
 *     block map   one byte per 32x32 block, row-major
 *     block       4x4 nametable entries, 32 bytes, at id * 32
 *     entry       priority(1) palette(2) vflip(1) hflip(1) tile(11)
 *
 * Plane A is the one the party walks on; plane B is behind it. Both share
 * the tileset, the block definitions and, usually, the map dimensions.
 *
 * A plane flagged as a backdrop is the exception: loc_8F70 substitutes a
 * fixed $200 x $100 extent for it, so it draws from a 16 x 8 block map
 * that wraps, and loc_8E00 never refreshes it. Every dungeon does this
 * with plane A. */

#define LEVEL_BACKDROP_COLS 16
#define LEVEL_BACKDROP_ROWS 8

#define LEVEL_FLAG_A_BACKDROP 0x08
#define LEVEL_FLAG_B_BACKDROP 0x04

typedef struct {
    int  count;
    u16 *cell;        /* count * 16 entries, host byte order */
} BlockSet;

typedef struct {
    int index;
    int cols, rows;        /* the level's own extent, in 32px blocks */
    int tileset;
    int palette;           /* PalettePtrs index for lines 2 and 3 */
    int music;
    int flags;

    /* The flag bytes on the two map pointers (LevelData +2 and +6): the
     * EnemyFormationTable group ProcessRandomBattle rolls from, and the
     * one it uses instead once $C737 is set (after Climatrol). Zero is a
     * level with no encounters -- every town. */
    int enemy_group;
    int enemy_group_late;

    /* LevelData +18. Bits 0-6 pick the art list for the people and
     * chests (npc.c reads that from level_sprites.json); bit 7 is
     * $FFFFF73C: plane A's camera moves two pixels for the main camera's
     * one, which is how every dungeon's overhead layer gets its depth. */
    int extra_art;

    /* Per-plane map extent: the level's own, or the backdrop's 16 x 8. */
    int plane_cols[LAYER_COUNT];
    int plane_rows[LAYER_COUNT];
    u8 *map[LAYER_COUNT];
} Level;

/* blocks<N>.txt: one block per line, 16 hex nametable entries,
 * row-major. Lines beginning with # are comments.
 *
 * These are deliberately not an image. A block is 4x4 nametable entries
 * and the fields that matter -- priority, palette line, the two flips --
 * are not things a picture can carry. blocks<N>.png is the same data
 * rendered for Tiled to lay out with, and is not read here. */
bool level_blocks_load(BlockSet *bs, const char *path);
void level_blocks_free(BlockSet *bs);

/* Loads <dir>/<stem>.tmx, a Tiled map whose layers are named planeB and
 * planeA and whose gids are block id + 1. The tileset index, palette,
 * music and flag byte are map properties.
 *
 * A backdrop plane lives in <dir>/<stem>_A.tmx or _B.tmx instead,
 * because a Tiled map has one size for all its layers and a backdrop is
 * 16x8 whatever the level is. */
bool level_load(Level *lv, const char *dir, const char *stem);
void level_free(Level *lv);

/* Fills one plane from the map, placing map tiles at the plane cells the
 * camera implies.
 *
 * The original refreshes a single column or row as the camera crosses a
 * 32px boundary (loc_8E00), because the VDP had a bandwidth budget for
 * exactly that much per field. There is no such budget here, so the whole
 * 64 x 32 window is rebuilt each frame. The result is identical -- the
 * plane is a sliding window either way -- and it removes the four
 * direction cases, their dirty-bit flags and the wrap bookkeeping that
 * goes with them. */
void level_draw_plane(Video *v, LayerId layer, const Level *lv,
                      const BlockSet *bs, int cam_x, int cam_y);

#endif /* PS2_LEVEL_H */
