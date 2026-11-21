#ifndef PS2_NAMETABLE_H
#define PS2_NAMETABLE_H

#include "video.h"

/* A VDP nametable as DrawArtTiles feeds it to the data port. Each entry:
 *
 *     P CC V H TTTTTTTTTTT
 *     priority(1) palette(2) vflip(1) hflip(1) tile(11)
 *
 * The tile field is an absolute VRAM tile index, so the tileset mirrors
 * VRAM tile space and each art blob is placed at the index its load
 * address implies. */

#define NAMETABLE_MAX_CELLS 1200

typedef struct {
    int width, height;
    u16 entry[NAMETABLE_MAX_CELLS];   /* host byte order */
} Nametable;

/* (build 135) A Tiled map (extract_nametables.py, write_nametable_tmx):
 * one CSV layer named "nametable" over tilesets that are the art sheets
 * themselves, each with the integer properties vram_base, palette_line
 * and priority. A cell's entry is
 *
 *     priority << 15 | palette_line << 13 | V << 12 | H << 11
 *                    | (vram_base + the tile's index in its sheet)
 *
 * with H and V from Tiled's flip flags. An empty cell is tile 0 in the
 * map's own property empty_palette_line (0, the entry $0000, unless the
 * map says otherwise: Mother Brain's star field has 3). A cell this
 * cannot turn into an entry -- a diagonal or hexagonal flip, a tile in
 * no tileset, a tile past $7FF, a line or priority out of range -- is an
 * error, named. Before build 135: "PS2N" containers (.nam). */
bool nametable_load(Nametable *nt, const char *path);

/* One entry as a plane cell. */
Cell nametable_cell(u16 entry);

void nametable_draw(Video *v, LayerId layer, const Nametable *nt,
                    int col, int row);

/* Rows first .. first + rows - 1 of the table, the first of them at
 * (col, row): one frame of a table that stacks several, as
 * TylerSpcshp_MonitorTileInd's five do ($168 bytes apart). */
void nametable_draw_rows(Video *v, LayerId layer, const Nametable *nt,
                         int col, int row, int first, int rows);

/* (build 147) The tiles a table's cells name: the lowest and how many up
 * to the highest (count 0 for a table of nothing but tile 0). */
void nametable_tiles(const Nametable *nt, u16 *first, u16 *count);

/* (build 147) A cut-scene picture (assets/art/scenes, art.h) over a table
 * drawn at (col, row) of `layer`: the cells of that layer whose tiles are
 * the table's (nametable_tiles) take their colours from the picture, its
 * top left at the table's `at` cell (VideoPicture). Bound each frame the
 * table is drawn, after the planes are cleared. False when it is not
 * bound (no picture, the bindings full). */
bool nametable_picture(Video *v, LayerId layer, const Nametable *nt,
                       const Picture *p, int col, int row);

/* (build 149) The same over one frame of a stacked table, `rows` of its
 * rows drawn at (col, row) by nametable_draw_rows: `p` is that frame's
 * picture (art_scene_frame), "at" counted from the frame's first row.
 * (build 152) Through a reveal mask, `hidden` (0 for none). */
bool nametable_picture_rows(Video *v, LayerId layer, const Nametable *nt,
                            const Picture *p, int col, int row, int rows,
                            u64 hidden);

/* (build 151) The whole table's picture with a reveal mask (VideoPicture's
 * `hidden`): the pixels of every cell a dissolve has not put in, or has
 * taken out. */
bool nametable_picture_hidden(Video *v, LayerId layer, const Nametable *nt,
                              const Picture *p, int col, int row,
                              u64 hidden);

/* (build 154) The whole table's picture over the cells that name tiles
 * first_tile .. first_tile + count - 1 alone, through `hidden`: one of the
 * tile ranges a table's cells name, where each is dissolved apart (the
 * title's figures and its logo, one picture bound once a range). */
bool nametable_picture_tiles(Video *v, LayerId layer, const Nametable *nt,
                             const Picture *p, int col, int row,
                             u16 first_tile, u16 count, u64 hidden);

#endif /* PS2_NAMETABLE_H */
