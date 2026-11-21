#include "level.h"

#include "assetfs.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_PX    32                 /* a block is 32 x 32 pixels */
#define BLOCK_TILES (BLOCK_PX / TILE_W)  /* 4 x 4 tiles */
#define BLOCK_CELLS (BLOCK_TILES * BLOCK_TILES)

/* A writable copy of a file out of the RAM store; the parsers below cut
 * their input up as they go. */
static char *slurp(const char *path)
{
    char *text = assetfs_copy_text(path);
    if (text == NULL) {
        fprintf(stderr, "level: cannot open %s\n", path);
    }
    return text;
}

bool level_blocks_load(BlockSet *bs, const char *path)
{
    bs->cell = NULL;
    bs->count = 0;

    char *text = slurp(path);
    if (text == NULL) {
        return false;
    }

    /* Two passes: count the data lines, then read them. */
    int count = 0;
    for (const char *p = text; *p != '\0'; ) {
        const char *eol = strchr(p, '\n');
        if (*p != '#' && *p != '\n' && *p != '\r') {
            count++;
        }
        if (eol == NULL) {
            break;
        }
        p = eol + 1;
    }

    u16 *cell = calloc((size_t)count * BLOCK_CELLS, sizeof *cell);
    if (cell == NULL) {
        free(text);
        return false;
    }

    int block = 0;
    for (char *p = text; *p != '\0' && block < count; ) {
        char *eol = strchr(p, '\n');
        if (eol != NULL) {
            *eol = '\0';
        }

        if (*p != '#' && *p != '\0' && *p != '\r') {
            char *cursor = p;
            for (int i = 0; i < BLOCK_CELLS; i++) {
                cell[block * BLOCK_CELLS + i] =
                    (u16)strtoul(cursor, &cursor, 16);
            }
            block++;
        }

        if (eol == NULL) {
            break;
        }
        p = eol + 1;
    }

    free(text);
    bs->cell = cell;
    bs->count = block;
    return true;
}

void level_blocks_free(BlockSet *bs)
{
    free(bs->cell);
    bs->cell = NULL;
    bs->count = 0;
}

/* A targeted scanner rather than an XML parser: Tiled writes the same
 * shape every time and only three things are wanted out of it. Anything
 * unexpected fails rather than guesses. */
static int tmx_attr_int(const char *at, const char *name, int fallback)
{
    char pattern[64];
    snprintf(pattern, sizeof pattern, "%s=\"", name);

    const char *hit = strstr(at, pattern);
    return (hit != NULL) ? atoi(hit + strlen(pattern)) : fallback;
}

static int tmx_property_int(const char *text, const char *name, int fallback)
{
    char pattern[96];
    snprintf(pattern, sizeof pattern, "<property name=\"%s\"", name);

    const char *hit = strstr(text, pattern);
    return (hit != NULL) ? tmx_attr_int(hit, "value", fallback) : fallback;
}

/* Reads one named layer's csv into a freshly allocated byte map. gids are
 * block id + 1; Tiled's flip flags live in the top three bits and are not
 * used by anything here, so they are masked off. */
static u8 *tmx_layer(const char *text, const char *name, int *w, int *h)
{
    char pattern[96];
    snprintf(pattern, sizeof pattern, "name=\"%s\"", name);

    const char *at = strstr(text, pattern);
    if (at == NULL) {
        return NULL;
    }

    *w = tmx_attr_int(at, "width", 0);
    *h = tmx_attr_int(at, "height", 0);
    if (*w <= 0 || *h <= 0) {
        return NULL;
    }

    const char *data = strstr(at, "<data");
    if (data == NULL) {
        return NULL;
    }
    data = strchr(data, '>');
    if (data == NULL) {
        return NULL;
    }
    data++;

    size_t cells = (size_t)*w * (size_t)*h;
    u8 *out = malloc(cells);
    if (out == NULL) {
        return NULL;
    }

    char *cursor = (char *)data;
    for (size_t i = 0; i < cells; i++) {
        while (*cursor == ',' || *cursor == '\n' || *cursor == '\r'
               || *cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor < '0' || *cursor > '9') {
            free(out);
            return NULL;
        }
        unsigned long gid = strtoul(cursor, &cursor, 10) & 0x1FFFFFFFul;
        out[i] = (u8)(gid ? gid - 1 : 0);
    }

    return out;
}

static bool level_read(Level *lv, const char *dir, const char *stem)
{
    memset(lv, 0, sizeof *lv);

    char path[512];
    snprintf(path, sizeof path, "%s/%s.tmx", dir, stem);
    char *text = slurp(path);
    if (text == NULL) {
        return false;
    }

    lv->index   = tmx_property_int(text, "index", 0);
    lv->tileset = tmx_property_int(text, "tileset", 0);
    lv->palette = tmx_property_int(text, "palette", 0);
    lv->music   = tmx_property_int(text, "music", 0);
    lv->flags   = tmx_property_int(text, "flags", 0);
    lv->enemy_group      = tmx_property_int(text, "enemy_group", 0);
    lv->enemy_group_late = tmx_property_int(text, "enemy_group_late", 0);
    lv->extra_art        = tmx_property_int(text, "extra_art", -1);
    if (lv->extra_art < 0) {
        /* A .tmx from before build 46 has no extra_art property. The
         * level's .json beside it has carried the byte all along, so
         * take it from there rather than lose the dungeons' parallax
         * until the levels are extracted again. */
        char jpath[512];
        snprintf(jpath, sizeof jpath, "%s/%s.json", dir, stem);
        JsonValue *meta = json_parse_file(jpath);
        lv->extra_art = 0;
        if (meta != NULL) {
            lv->extra_art = (int)json_int(meta, "extra_art", 0);
            json_free(meta);
        }
    }
    lv->cols    = tmx_property_int(text, "cols", 0);
    lv->rows    = tmx_property_int(text, "rows", 0);

    static const char *const LAYER_NAME[LAYER_COUNT] = { "planeB", "planeA" };
    static const int backdrop_bit[LAYER_COUNT] = {
        LEVEL_FLAG_B_BACKDROP, LEVEL_FLAG_A_BACKDROP
    };

    bool ok = true;
    for (int layer = 0; layer < LAYER_COUNT; layer++) {
        int w = 0, h = 0;
        u8 *map = NULL;

        if (lv->flags & backdrop_bit[layer]) {
            char bd[512];
            snprintf(bd, sizeof bd, "%s/%s_%c.tmx", dir, stem,
                     LAYER_NAME[layer][5]);
            char *btext = slurp(bd);
            if (btext != NULL) {
                map = tmx_layer(btext, LAYER_NAME[layer], &w, &h);
                free(btext);
            }
        } else {
            map = tmx_layer(text, LAYER_NAME[layer], &w, &h);
        }

        if (map == NULL) {
            fprintf(stderr, "level: %s has no %s layer\n", stem,
                    LAYER_NAME[layer]);
            ok = false;
            break;
        }

        lv->plane_cols[layer] = w;
        lv->plane_rows[layer] = h;
        lv->map[layer] = map;
    }

    free(text);
    if (!ok) {
        level_free(lv);
        return false;
    }
    return true;
}

bool level_load(Level *lv, const char *dir, const char *stem)
{
    if (level_read(lv, dir, stem)) {
        return true;
    }
    /* (build 157d) A mod it refuses -- the map, or a backdrop plane's
     * own -- is set aside and the game's own read in its place
     * (docs/mods.md, "When a mod is refused"). */
    static const char *const SUFFIX[3] = { "", "_B", "_A" };
    bool refused = false;
    for (int i = 0; i < 3; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s%s.tmx", dir, stem, SUFFIX[i]);
        refused = assetfs_mod_refused(path) || refused;
    }
    return refused && level_read(lv, dir, stem);
}

void level_free(Level *lv)
{
    for (int layer = 0; layer < LAYER_COUNT; layer++) {
        free(lv->map[layer]);
        lv->map[layer] = NULL;
    }
}

/* Positive modulus: camera positions go negative while a level wraps. */
static int wrap(int v, int n)
{
    int r = v % n;
    return (r < 0) ? r + n : r;
}

void level_draw_plane(Video *v, LayerId layer, const Level *lv,
                      const BlockSet *bs, int cam_x, int cam_y)
{
    if (lv->map[layer] == NULL || bs->cell == NULL) {
        return;
    }

    const int map_cols = lv->plane_cols[layer];
    const int map_rows = lv->plane_rows[layer];
    const int map_tw = map_cols * BLOCK_TILES;   /* map width in tiles */
    const int map_th = map_rows * BLOCK_TILES;

    /* The plane cell a map tile lands in is the tile index masked to the
     * plane's size, which is what loc_8FEA computes with
     * ((y & $E0) << 4) + ((x & $1E0) >> 2). Starting from the camera's own
     * tile and walking PLANE_COLS across covers each plane column exactly
     * once, so nothing needs clearing first. */
    const int base_tx = cam_x / TILE_W;
    const int base_ty = cam_y / TILE_H;

    for (int r = 0; r < PLANE_ROWS; r++) {
        int ty = wrap(base_ty + r, map_th);
        int block_row = ty / BLOCK_TILES;
        int sub_y = ty % BLOCK_TILES;
        int plane_row = (base_ty + r) & (PLANE_ROWS - 1);

        for (int c = 0; c < PLANE_COLS; c++) {
            int tx = wrap(base_tx + c, map_tw);
            int block_col = tx / BLOCK_TILES;
            int sub_x = tx % BLOCK_TILES;

            int id = lv->map[layer][block_row * map_cols + block_col];
            if (id >= bs->count) {
                continue;
            }

            u16 e = bs->cell[id * BLOCK_CELLS + sub_y * BLOCK_TILES + sub_x];

            Cell cell;
            memset(&cell, 0, sizeof cell);
            cell.tile   = (u16)(e & 0x07FF);
            cell.flip_h = (e >> 11) & 1;
            cell.flip_v = (e >> 12) & 1;
            cell.pal    = (e >> 13) & 3;
            cell.prio   = (e >> 15) & 1;

            v->plane[layer][plane_row][(base_tx + c) & (PLANE_COLS - 1)] =
                cell;
        }
    }
}
