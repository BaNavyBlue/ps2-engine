/* Diagnostic: draw one level frame at a given camera position.
 *
 * The crawl is 2000-odd frames into the attract loop, which is a poor
 * place to check pixels from. This puts the level renderer on its own so
 * a frame can be produced at any camera position and diffed against an
 * independently computed reference.
 *
 *     make level_probe
 *     ./level_probe 00_MotaviaOutside 0x5B0 0x520 out
 *
 * Writes out.ppm and out.idx, the latter being one byte of palette index
 * per pixel, which is what the comparison actually uses -- the RGB in
 * the PPM has been through the DAC ladder and is a preview.
 */

#include "../src/core/json.h"
#include "../src/core/level.h"
#include "../src/core/palette.h"
#include "../src/core/tileset.h"
#include "../src/core/video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s <level-stem> <cam_x> <cam_y> <outstem>\n",
                argv[0]);
        return 2;
    }

    int cam_x = (int)strtol(argv[2], NULL, 0);
    int cam_y = (int)strtol(argv[3], NULL, 0);

    Level level;
    if (!level_load(&level, "assets/levels", argv[1])) {
        return 1;
    }

    /* (build 97b) The tileset's files by the names the level's own record
     * gives them, and its palettes through the PalettePtrs table. */
    char path[256];
    snprintf(path, sizeof path, "assets/levels/%s.json", argv[1]);
    JsonValue *rec = json_parse_file(path);
    const JsonValue *files = json_get(rec, "files");
    char blocks_file[64], tiles_file[64];
    snprintf(blocks_file, sizeof blocks_file, "%s",
             json_str(files, "blocks", ""));
    snprintf(tiles_file, sizeof tiles_file, "%s",
             json_str(files, "tiles", ""));
    json_free(rec);
    BlockSet blocks;
    snprintf(path, sizeof path, "assets/levels/%s", blocks_file);
    if (!level_blocks_load(&blocks, path)) {
        return 1;
    }

    TileSet art, vram;
    snprintf(path, sizeof path, "assets/tiles/%s", tiles_file);
    if (!tileset_load(&art, path) || !tileset_alloc(&vram, 2048)) {
        return 1;
    }
    tileset_place(&vram, &art, 0);

    Video video;
    video_init(&video, &vram);
    palette_load_entry(&video.palette, 0, 0x02);   /* PALETTE_FIELD_COMMON */
    palette_load_entry(&video.palette, 2, level.palette);

    video_sprites_begin(&video);
    level_draw_plane(&video, LAYER_B, &level, &blocks, cam_x, cam_y);
    level_draw_plane(&video, LAYER_A, &level, &blocks, cam_x, cam_y);
    video_scroll(&video, LAYER_B, cam_x, cam_y);
    video_scroll(&video, LAYER_A, cam_x, cam_y);

    static Framebuffer fb;
    video_compose(&video, &fb);

    snprintf(path, sizeof path, "%s.idx", argv[4]);
    FILE *fh = fopen(path, "wb");
    if (fh != NULL) {
        fwrite(fb.px, 1, sizeof fb.px, fh);
        fclose(fh);
    }

    snprintf(path, sizeof path, "%s.ppm", argv[4]);
    fh = fopen(path, "wb");
    if (fh != NULL) {
        fprintf(fh, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
            Rgb8 c = cram_to_rgb8(video.palette.entry[fb.px[i]]);
            fputc(c.r, fh);
            fputc(c.g, fh);
            fputc(c.b, fh);
        }
        fclose(fh);
    }

    fprintf(stderr, "level %d: %dx%d blocks, tileset %d (%d tiles),"
            " %d blocks, palette %d, camera %d,%d\n",
            level.index, level.cols, level.rows, level.tileset, art.count,
            blocks.count, level.palette, cam_x, cam_y);
    return 0;
}
