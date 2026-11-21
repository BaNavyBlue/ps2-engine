#include "tileset.h"

#include "assetfs.h"
#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TILE_BYTES (TILE_W * TILE_H)

static bool tileset_read(TileSet *ts, const char *path)
{
    ts->tiles = NULL;
    ts->count = 0;

    /* A tile sheet is an indexed PNG: a grid of 8x8 tiles, row-major, in
     * the tileset's own order, so a nametable entry or a block
     * definition indexes it directly. The index plane is already one
     * byte per pixel, which is what Tile wants, so loading is a copy per
     * tile and no unpacking at all. */
    PngImage img;
    if (!png_load_indexed(&img, path)) {
        return false;
    }

    if (img.width % TILE_W != 0 || img.height % TILE_H != 0) {
        fprintf(stderr, "tileset: %s is %dx%d, not a whole number of"
                " tiles\n", path, img.width, img.height);
        png_free(&img);
        return false;
    }

    int across = img.width / TILE_W;
    int down   = img.height / TILE_H;
    int count  = across * down;

    Tile *tiles = calloc((size_t)count, sizeof *tiles);
    if (tiles == NULL) {
        png_free(&img);
        return false;
    }

    for (int t = 0; t < count; t++) {
        int ox = (t % across) * TILE_W;
        int oy = (t / across) * TILE_H;
        for (int y = 0; y < TILE_H; y++) {
            memcpy(tiles[t].px + y * TILE_W,
                   img.px + (size_t)(oy + y) * img.width + ox, TILE_W);
        }
    }

    png_free(&img);
    ts->tiles = tiles;
    ts->count = count;
    return true;
}

bool tileset_load(TileSet *ts, const char *path)
{
    /* (build 157d) A mod it refuses is set aside and the game's own
     * read in its place (docs/mods.md, "When a mod is refused"). */
    return tileset_read(ts, path)
        || (assetfs_mod_refused(path) && tileset_read(ts, path));
}

void tileset_free(TileSet *ts)
{
    free(ts->tiles);
    ts->tiles = NULL;
    ts->count = 0;
}

bool tileset_append(TileSet *dst, const TileSet *src, int *first_index)
{
    int total = dst->count + src->count;
    Tile *grown = realloc(dst->tiles, (size_t)total * sizeof *grown);
    if (grown == NULL) {
        return false;
    }

    memcpy(grown + dst->count, src->tiles,
           (size_t)src->count * sizeof *grown);
    if (first_index != NULL) {
        *first_index = dst->count;
    }
    dst->tiles = grown;
    dst->count = total;
    return true;
}

bool tileset_alloc(TileSet *ts, int count)
{
    Tile *tiles = calloc((size_t)count, sizeof *tiles);
    if (tiles == NULL) {
        return false;
    }
    ts->tiles = tiles;
    ts->count = count;
    return true;
}

void tileset_place(TileSet *dst, const TileSet *src, int first_index)
{
    for (int i = 0; i < src->count; i++) {
        int at = first_index + i;
        if (at < 0 || at >= dst->count) {
            continue;
        }
        dst->tiles[at] = src->tiles[i];
    }
}

void tileset_place_clearing(TileSet *dst, const TileSet *src, int first_index,
                            u8 clear)
{
    tileset_place(dst, src, first_index);
    for (int i = 0; i < src->count; i++) {
        int at = first_index + i;
        if (at < 0 || at >= dst->count) {
            continue;
        }
        for (int p = 0; p < TILE_W * TILE_H; p++) {
            if (dst->tiles[at].px[p] == clear) {
                dst->tiles[at].px[p] = 0;
            }
        }
    }
}

void tileset_clear(TileSet *ts)
{
    if (ts->tiles != NULL) {
        memset(ts->tiles, 0, (size_t)ts->count * sizeof *ts->tiles);
    }
}

void tileset_reveal_pixel(TileSet *dst, const TileSet *src, int first_index,
                          int count, int pixel)
{
    if (pixel < 0 || pixel >= TILE_W * TILE_H) {
        return;
    }

    for (int i = 0; i < count && i < src->count; i++) {
        int at = first_index + i;
        if (at < 0 || at >= dst->count) {
            continue;
        }
        dst->tiles[at].px[pixel] = src->tiles[i].px[pixel];
    }
}
