/* (build 189) tools/extract_inline_art.py and extract_art.py in C: the
 * compressed art as indexed tile sheets under assets/tiles -- the inline
 * blocks (each label's bytes, its stream ending itself) in grey, and the
 * binclude'd art/<stem>.bin files in the palette their name matches, grey when
 * none does. The art the pictures replaced keeps no sheet: an old one
 * goes (asset_names.PICTURE_ART). */

#include "exart.h"
#include "excommon.h"
#include "exsheets.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void ex_step_inline_art(Extract *x, const ExArgs *a);
void ex_step_art(Extract *x, const ExArgs *a);

void ex_step_inline_art(Extract *x, const ExArgs *a)
{
    ex_mkdirs(x, a->out);
    ExRgb grey;
    memset(&grey, 0, sizeof grey);
    ex_rgb_grey_ramp(&grey);
    for (int i = 0; EX_INLINE_LABELS[i] != NULL; i++) {
        const char *label = EX_INLINE_LABELS[i];
        const char *name = ex_inline_name(x, label);
        char *out = ex_path(x, a->out, ex_fmt(x, "%s.png", name));
        if (ex_listed(EX_PICTURE_ART, name)) {
            ex_remove(x, out);
            ex_say(x, "%s: picture art, no sheet (%s.png)", label, name);
            continue;
        }
        size_t len = 0;
        const u8 *raw = ex_blob(x, label, &len);
        ExTiles tiles = ex_art(x, raw, len);
        ex_png_tiles(x, out, tiles.data, tiles.count, &grey, 16, true, NULL,
                     NULL);
        ex_say(x, "%s: %d tiles, %zu/%zu bytes (%zu trailing) -> %s", label,
               tiles.count, tiles.used, len, len - tiles.used, out);
    }
}

/* re.sub(r"[^a-z0-9]", "", s.lower()) */
static char *alnum_lower(Extract *x, const char *s)
{
    char *out = ex_alloc(x, strlen(s) + 1);
    size_t n = 0;
    for (; *s != '\0'; s++) {
        char c = (char)tolower((unsigned char)*s);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[n++] = c;
        }
    }
    out[n] = '\0';
    return out;
}

static int hex2(const char *s)
{
    int v = 0;
    for (int i = 0; i < 2; i++) {
        char c = (char)tolower((unsigned char)s[i]);
        int d = (c >= '0' && c <= '9') ? c - '0'
              : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
        if (d < 0) {
            return -1;
        }
        v = v * 16 + d;
    }
    return v;
}

/* palette_for: the first palette whose name, less "pal", is in the
 * stem or holds it (letters and digits only, any case). */
static const JsonValue *palette_for(Extract *x, const char *stem,
                                    const JsonValue *palettes,
                                    const char **name)
{
    if (palettes == NULL || palettes->type != JSON_OBJECT) {
        return NULL;
    }
    const char *key = alnum_lower(x, stem);
    for (const JsonValue *p = palettes->child; p != NULL; p = p->next) {
        const char *cand = alnum_lower(x, p->key);
        if (ex_starts(cand, "pal")) {
            cand += 3;
        }
        if (cand[0] != '\0'
            && (strstr(key, cand) != NULL || strstr(cand, key) != NULL)) {
            *name = p->key;
            return json_get(p, "rgb");
        }
    }
    return NULL;
}

void ex_step_art(Extract *x, const ExArgs *a)
{
    /* The Python's -p default is assets/palettes/palettes.json from where
     * it runs, which is under -o as the Makefile runs it ("-o assets"). */
    char *pal_path = ex_path(x, a->out, "palettes/palettes.json");
    const JsonValue *palettes = NULL;
    if (ex_read_file(x, pal_path, NULL) != NULL) {
        palettes = ex_read_json(x, pal_path);
    }
    char *tile_dir = ex_path(x, a->out, "tiles");
    ex_mkdirs(x, tile_dir);

    ExRgb grey;
    memset(&grey, 0, sizeof grey);
    ex_rgb_grey_ramp(&grey);
    int nbins = 0;
    ExArtBin *bins = ex_art_bins(x, &nbins);
    int decoded = 0;
    long long total = 0;
    int unmatched = 0;
    ExStr unmatched_names;
    ex_str_init(x, &unmatched_names);
    ExStr failures;
    ex_str_init(x, &failures);
    for (int i = 0; i < nbins; i++) {
        const char *stem = bins[i].stem;
        if (ex_listed(EX_ART_NOT_ART, stem)) {
            continue;
        }
        size_t size = 0;
        const u8 *raw = ex_file(x, bins[i].file, &size);
        ExTiles tiles;
        const char *why = NULL;
        if (!ex_art_try(x, raw, size, &tiles, &why)) {
            ex_str_addf(&failures, "  FAILED %s: %s\n", bins[i].file, why);
            continue;
        }
        const char *name = ex_art_name(stem);
        char *out = ex_path(x, tile_dir, ex_fmt(x, "%s.png", name));
        if (ex_listed(EX_PICTURE_ART, name)) {
            ex_remove(x, out);
            continue;
        }
        int width = ex_guess_width(stem, tiles.count);
        const char *pal_name = NULL;
        const JsonValue *rgb_list = palette_for(x, stem, palettes, &pal_name);
        ExRgb rgb;
        memset(&rgb, 0, sizeof rgb);
        if (pal_name == NULL) {
            rgb = grey;
            if (unmatched < 6) {
                ex_str_addf(&unmatched_names, "%s%s",
                            (unmatched > 0) ? ", " : "", name);
            }
            unmatched++;
        } else {
            int k = 0;
            for (const JsonValue *c = (rgb_list != NULL) ? rgb_list->child
                                                         : NULL;
                 c != NULL && k < 16; c = c->next, k++) {
                const char *s = (c->type == JSON_STRING) ? c->string : "";
                int r = (strlen(s) >= 7) ? hex2(s + 1) : -1;
                int g = (strlen(s) >= 7) ? hex2(s + 3) : -1;
                int b = (strlen(s) >= 7) ? hex2(s + 5) : -1;
                if (r < 0 || g < 0 || b < 0) {
                    ex_fail(x, "%s: a colour %s is not #RRGGBB", pal_name, s);
                }
                ex_rgb_add(&rgb, (u8)r, (u8)g, (u8)b);
            }
        }
        ex_png_tiles(x, out, tiles.data, tiles.count, &rgb, width, true,
                     NULL, NULL);
        decoded++;
        total += tiles.count;
    }
    ex_say(x, "decoded %d files, %lld tiles", decoded, total);
    if (unmatched > 0) {
        ex_say(x, "  no palette matched for %d: %s...", unmatched,
               unmatched_names.buf);
    }
    if (failures.len > 0) {
        failures.buf[failures.len - 1] = '\0';
        ex_warn(x, "%s", failures.buf);
    }
}
