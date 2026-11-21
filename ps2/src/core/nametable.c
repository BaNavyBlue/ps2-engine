#include "nametable.h"

#include "assetfs.h"
#include "tmx.h"

#include <stdio.h>
#include <string.h>

/* The entry one cell stands for, or -1 with the reason in *why. */
static long cell_entry(const Tmx *t, u32 gid, int empty_line,
                       const char **why)
{
    if ((gid & ~TMX_FLAGS) == 0) {
        if ((gid & TMX_FLAGS) != 0) {
            *why = "empty but flipped";
            return -1;
        }
        return (long)empty_line << 13;
    }
    if ((gid & (TMX_FLIP_D | TMX_FLIP_HEX)) != 0) {
        *why = "flipped diagonally, which the VDP cannot";
        return -1;
    }
    int local = 0;
    const TmxTileset *ts = tmx_tileset_of(t, gid, &local);
    if (ts == NULL) {
        *why = "in no tileset";
        return -1;
    }
    int tile = tmx_tileset_int(ts, "vram_base", -1);
    int line = tmx_tileset_int(ts, "palette_line", -1);
    int prio = tmx_tileset_int(ts, "priority", 0);
    if (tile < 0) {
        *why = "in a tileset without vram_base";
        return -1;
    }
    tile += local;
    if (tile > 0x7FF) {
        *why = "a tile past $7FF";
        return -1;
    }
    if (line < 0 || line > 3) {
        *why = "in a tileset whose palette_line is not 0-3";
        return -1;
    }
    if (prio < 0 || prio > 1) {
        *why = "in a tileset whose priority is not 0 or 1";
        return -1;
    }
    long e = (long)prio << 15 | (long)line << 13 | (long)tile;
    if ((gid & TMX_FLIP_V) != 0) {
        e |= 1L << 12;
    }
    if ((gid & TMX_FLIP_H) != 0) {
        e |= 1L << 11;
    }
    return e;
}

static bool nametable_read(Nametable *nt, const char *path)
{
    memset(nt, 0, sizeof *nt);

    Tmx t;
    if (!tmx_load(&t, path, "nametable")) {
        return false;
    }

    size_t cells = (size_t)t.width * (size_t)t.height;
    if (cells > NAMETABLE_MAX_CELLS) {
        fprintf(stderr, "nametable: %s has %zu cells\n", path, cells);
        tmx_free(&t);
        return false;
    }

    int  empty_line = tmx_map_int(&t, "empty_palette_line", 0);
    bool ok         = true;
    if (empty_line < 0 || empty_line > 3) {
        fprintf(stderr, "nametable: %s: empty_palette_line %d is not 0-3\n",
                path, empty_line);
        ok = false;
    }
    for (size_t i = 0; ok && i < cells; i++) {
        const char *why = "";
        long e = cell_entry(&t, t.gid[i], empty_line, &why);
        if (e < 0) {
            fprintf(stderr, "nametable: %s: cell %d,%d is %s\n", path,
                    (int)(i % (size_t)t.width), (int)(i / (size_t)t.width),
                    why);
            ok = false;
            continue;
        }
        nt->entry[i] = (u16)e;
    }

    if (ok) {
        nt->width  = t.width;
        nt->height = t.height;
    } else {
        memset(nt, 0, sizeof *nt);
    }
    tmx_free(&t);
    return ok;
}

bool nametable_load(Nametable *nt, const char *path)
{
    /* (build 157d) A mod it refuses is set aside and the game's own
     * read in its place (docs/mods.md, "When a mod is refused"). */
    return nametable_read(nt, path)
        || (assetfs_mod_refused(path) && nametable_read(nt, path));
}

Cell nametable_cell(u16 e)
{
    Cell cell;
    memset(&cell, 0, sizeof cell);
    cell.tile   = (u16)(e & 0x07FF);
    cell.flip_h = (e >> 11) & 1;
    cell.flip_v = (e >> 12) & 1;
    cell.pal    = (e >> 13) & 3;
    cell.prio   = (e >> 15) & 1;
    return cell;
}

void nametable_draw(Video *v, LayerId layer, const Nametable *nt,
                    int col, int row)
{
    nametable_draw_rows(v, layer, nt, col, row, 0, nt->height);
}

void nametable_draw_rows(Video *v, LayerId layer, const Nametable *nt,
                         int col, int row, int first, int rows)
{
    for (int y = first; y < first + rows && y < nt->height; y++) {
        if (y < 0) {
            continue;
        }
        for (int x = 0; x < nt->width; x++) {
            Cell cell = nametable_cell(nt->entry[y * nt->width + x]);
            video_set_tile(v, layer, col + x, row + y - first, cell);
        }
    }
}

void nametable_tiles(const Nametable *nt, u16 *first, u16 *count)
{
    int lo = 0x800;
    int hi = -1;
    for (int i = 0; i < nt->width * nt->height; i++) {
        int t = nametable_cell(nt->entry[i]).tile;
        if (t == 0) {
            continue;
        }
        lo = (t < lo) ? t : lo;
        hi = (t > hi) ? t : hi;
    }
    *first = (hi < 0) ? 0 : (u16)lo;
    *count = (hi < 0) ? 0 : (u16)(hi - lo + 1);
}

static bool bind(Video *v, LayerId layer, const Nametable *nt,
                 const Picture *p, int col, int row, int rows,
                 u16 first_tile, u16 count, u64 hidden)
{
    if (p == NULL) {
        return false;
    }
    VideoPicture b;
    memset(&b, 0, sizeof b);
    b.first_tile = first_tile;
    b.count      = count;
    b.picture = p;
    b.layer   = (u8)layer;
    b.col     = (s16)(col + p->at_col);
    b.row     = (s16)(row + p->at_row);
    b.col_lo  = 0;
    b.row_lo  = 0;
    b.col_hi  = (s16)(nt->width - 1 - p->at_col);   /* the bind clips */
    b.row_hi  = (s16)(rows - 1 - p->at_row);        /* to the picture */
    b.hidden  = hidden;
    return video_picture_bind(v, &b);
}

bool nametable_picture_rows(Video *v, LayerId layer, const Nametable *nt,
                            const Picture *p, int col, int row, int rows,
                            u64 hidden)
{
    u16 first = 0;
    u16 count = 0;
    nametable_tiles(nt, &first, &count);
    return bind(v, layer, nt, p, col, row, rows, first, count, hidden);
}

bool nametable_picture_tiles(Video *v, LayerId layer, const Nametable *nt,
                             const Picture *p, int col, int row,
                             u16 first_tile, u16 count, u64 hidden)
{
    return bind(v, layer, nt, p, col, row, nt->height, first_tile, count,
                hidden);
}

bool nametable_picture(Video *v, LayerId layer, const Nametable *nt,
                       const Picture *p, int col, int row)
{
    return nametable_picture_rows(v, layer, nt, p, col, row, nt->height, 0);
}

bool nametable_picture_hidden(Video *v, LayerId layer, const Nametable *nt,
                              const Picture *p, int col, int row,
                              u64 hidden)
{
    return nametable_picture_rows(v, layer, nt, p, col, row, nt->height,
                                  hidden);
}
