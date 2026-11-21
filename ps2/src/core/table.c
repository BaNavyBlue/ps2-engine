#include "table.h"

#include "assetfs.h"
#include "json.h"
#include "palette.h"
#include "png.h"

#include <stdio.h>
#include <string.h>

static bool ramp_read(WordTable *t, const char *path)
{
    memset(t, 0, sizeof *t);

    PngImage img;
    if (!png_load_indexed(&img, path)) {
        return false;
    }

    int  count = img.width * img.height;
    bool ok    = true;
    if (count > TABLE_MAX) {
        fprintf(stderr, "table: %s has %d colours, more than %d\n",
                path, count, TABLE_MAX);
        ok = false;
    }
    for (int i = 0; ok && i < count; i++) {
        int ix = img.px[i];
        if (ix >= img.pal_count) {
            fprintf(stderr, "table: %s: pixel (%d, %d) is colour %d, past "
                            "the palette's %d\n", path, i % img.width,
                    i / img.width, ix, img.pal_count);
            ok = false;
        } else {
            t->value[i] = cram_from_rgb8(img.pal[ix][0], img.pal[ix][1],
                                         img.pal[ix][2]);
        }
    }

    t->count = ok ? count : 0;
    png_free(&img);
    return ok;
}

bool table_load_ramp(WordTable *t, const char *path)
{
    /* (build 157d) A mod it refuses is set aside and the game's own
     * read in its place (docs/mods.md, "When a mod is refused"). */
    return ramp_read(t, path)
        || (assetfs_mod_refused(path) && ramp_read(t, path));
}

/* One coordinate of a dissolve position: a whole number 0-7, or -1. */
static int dissolve_coord(const JsonValue *v)
{
    if (v == NULL || v->type != JSON_NUMBER || v->number < 0.0
        || v->number > 7.0 || v->number != (double)(int)v->number) {
        return -1;
    }
    return (int)v->number;
}

static bool dissolve_read(ByteTable *t, const char *path)
{
    memset(t, 0, sizeof *t);

    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        fprintf(stderr, "table: cannot read %s\n", path);
        return false;
    }

    const JsonValue *order = json_get(root, "order");
    int  n  = json_length(order);
    bool ok = order != NULL && order->type == JSON_ARRAY
           && n == DISSOLVE_STEPS;
    if (!ok) {
        fprintf(stderr, "table: %s: \"order\" holds %d positions, not %d\n",
                path, n, DISSOLVE_STEPS);
    }
    for (int i = 0; ok && i < n; i++) {
        const JsonValue *pos = json_at(order, i);
        int x = (pos != NULL && pos->type == JSON_ARRAY
                 && json_length(pos) == 2) ? dissolve_coord(json_at(pos, 0)) : -1;
        int y = (x >= 0) ? dissolve_coord(json_at(pos, 1)) : -1;
        if (y < 0) {
            fprintf(stderr, "table: %s: \"order\" position %d is not "
                            "[x, y], each 0-7\n", path, i);
            ok = false;
        } else {
            t->value[i] = (u8)(y * 8 + x);
        }
    }

    t->count = ok ? n : 0;
    json_free(root);
    return ok;
}

bool table_load_dissolve(ByteTable *t, const char *path)
{
    /* (build 157d) A mod it refuses is set aside and the game's own
     * read in its place (docs/mods.md, "When a mod is refused"). */
    return dissolve_read(t, path)
        || (assetfs_mod_refused(path) && dissolve_read(t, path));
}
