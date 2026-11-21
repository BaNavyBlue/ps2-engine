#include "winmap.h"

#include "assetfs.h"
#include "tmx.h"

#include <stdio.h>
#include <string.h>

/* (build 134) A Tiled map (extract_winart.py, write_winart_tmx): every
 * cell names a tile of one of its tilesets, and that tileset's byte_base
 * turns the tile back into the map byte. A cell left empty, flipped, or
 * outside every tileset is an error -- a window map has a byte in every
 * cell and no flips. */
static bool winmap_read(WinMap *map, const char *path)
{
    memset(map, 0, sizeof *map);

    Tmx t;
    if (!tmx_load(&t, path, NULL)) {
        return false;
    }

    size_t cells = (size_t)t.width * (size_t)t.height;
    if (cells > WINMAP_MAX_CELLS) {
        fprintf(stderr, "winmap: %s has %zu cells\n", path, cells);
        tmx_free(&t);
        return false;
    }

    bool ok = true;
    for (size_t i = 0; ok && i < cells; i++) {
        int x = (int)(i % (size_t)t.width);
        int y = (int)(i / (size_t)t.width);
        if ((t.gid[i] & TMX_FLAGS) != 0) {
            fprintf(stderr, "winmap: %s: cell %d,%d is flipped\n", path, x, y);
            ok = false;
            continue;
        }
        int local = 0;
        const TmxTileset *ts = tmx_tileset_of(&t, t.gid[i], &local);
        if (ts == NULL) {
            fprintf(stderr, "winmap: %s: cell %d,%d is %s\n", path, x, y,
                    (t.gid[i] == 0) ? "empty" : "in no tileset");
            ok = false;
            continue;
        }
        int byte = local + tmx_tileset_int(ts, "byte_base", 0);
        if (byte < 0 || byte > 0xFF) {
            fprintf(stderr, "winmap: %s: cell %d,%d is byte %d\n", path, x, y,
                    byte);
            ok = false;
            continue;
        }
        map->cell[i] = (u8)byte;
    }

    if (ok) {
        map->width  = t.width;
        map->height = t.height;
    } else {
        memset(map, 0, sizeof *map);
    }
    tmx_free(&t);
    return ok;
}

bool winmap_load(WinMap *map, const char *path)
{
    /* (build 157d) A mod it refuses is set aside and the game's own
     * read in its place (docs/mods.md, "When a mod is refused"). */
    return winmap_read(map, path)
        || (assetfs_mod_refused(path) && winmap_read(map, path));
}

void winmap_draw(Video *v, LayerId layer, const WinMap *map,
                 int col, int row, int row_first, int row_last,
                 int tile_base, int first_tile, u8 pal)
{
    if (row_last > map->height) {
        row_last = map->height;
    }

    for (int y = row_first; y < row_last; y++) {
        for (int x = 0; x < map->width; x++) {
            Cell cell;
            memset(&cell, 0, sizeof cell);

            int index = map->cell[y * map->width + x] - tile_base;
            cell.tile = (u16)(first_tile + index);
            cell.pal  = pal & 3;
            cell.prio = 0;

            video_set_tile(v, layer, col + x, row + (y - row_first), cell);
        }
    }
}
