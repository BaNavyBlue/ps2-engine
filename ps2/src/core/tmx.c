#include "tmx.h"

#include "assetfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The value of attribute `name` in the tag starting at `tag`, or NULL. The
 * search stops at the tag's '>'. */
static const char *attr(const char *tag, const char *name)
{
    const char *end = strchr(tag, '>');
    size_t      n   = strlen(name);
    for (const char *p = tag; p != NULL && (end == NULL || p < end);
         p = strchr(p + 1, ' ')) {
        if (strncmp(p + 1, name, n) == 0 && p[1 + n] == '=' && p[2 + n] == '"') {
            return p + 3 + n;
        }
    }
    return NULL;
}

static long attr_long(const char *tag, const char *name, long fallback)
{
    const char *v = attr(tag, name);
    return (v != NULL) ? strtol(v, NULL, 10) : fallback;
}

/* The <property> elements from `from` up to `until` (the end of the text
 * when NULL), into `props`; past TMX_PROPS_MAX they are dropped. */
static void read_props(TmxProps *props, const char *from, const char *until)
{
    for (const char *p = strstr(from, "<property ");
         p != NULL && (until == NULL || p < until);
         p = strstr(p + 1, "<property ")) {
        const char *name = attr(p, "name");
        if (name == NULL || props->count >= TMX_PROPS_MAX) {
            continue;
        }
        int k = props->count++;
        size_t len = strcspn(name, "\"");
        if (len >= TMX_NAME_MAX) {
            len = TMX_NAME_MAX - 1;
        }
        memcpy(props->name[k], name, len);
        props->name[k][len] = '\0';
        props->value[k] = (int)attr_long(p, "value", 0);
    }
}

static int props_int(const TmxProps *props, const char *name, int fallback)
{
    for (int i = 0; i < props->count; i++) {
        if (strcmp(props->name[i], name) == 0) {
            return props->value[i];
        }
    }
    return fallback;
}

/* One <tileset> element, from its opening tag to its </tileset>. */
static bool read_tileset(TmxTileset *ts, const char *tag, const char *path)
{
    memset(ts, 0, sizeof *ts);
    if (attr(tag, "source") != NULL) {
        fprintf(stderr, "tmx: %s: an external tileset; the port reads only "
                        "embedded ones\n", path);
        return false;
    }
    ts->firstgid  = (u32)attr_long(tag, "firstgid", 0);
    ts->tilecount = (u32)attr_long(tag, "tilecount", 0);
    if (ts->firstgid == 0 || ts->tilecount == 0) {
        fprintf(stderr, "tmx: %s: a tileset without firstgid or tilecount\n",
                path);
        return false;
    }

    read_props(&ts->props, tag, strstr(tag, "</tileset>"));
    return true;
}

/* (build 135) The map's own properties: those after its <map> tag and
 * before its first tileset or layer, whichever comes first. */
static void read_map_props(TmxProps *props, const char *text)
{
    const char *map = strstr(text, "<map ");
    if (map == NULL) {
        return;
    }
    const char *ts    = strstr(map, "<tileset ");
    const char *layer = strstr(map, "<layer ");
    const char *until = ts;
    if (until == NULL || (layer != NULL && layer < until)) {
        until = layer;
    }
    read_props(props, map, until);
}

static const char *find_layer(const char *text, const char *layer)
{
    for (const char *p = strstr(text, "<layer "); p != NULL;
         p = strstr(p + 1, "<layer ")) {
        if (layer == NULL) {
            return p;
        }
        const char *name = attr(p, "name");
        size_t      n    = strlen(layer);
        if (name != NULL && strncmp(name, layer, n) == 0 && name[n] == '"') {
            return p;
        }
    }
    return NULL;
}

static bool tmx_read(Tmx *t, const char *path, const char *layer)
{
    memset(t, 0, sizeof *t);
    char *text = assetfs_copy_text(path);
    if (text == NULL) {
        fprintf(stderr, "tmx: cannot open %s\n", path);
        return false;
    }

    read_map_props(&t->props, text);

    bool ok = true;
    for (const char *p = strstr(text, "<tileset "); ok && p != NULL;
         p = strstr(p + 1, "<tileset ")) {
        if (t->tileset_count >= TMX_TILESETS_MAX) {
            fprintf(stderr, "tmx: %s: more than %d tilesets\n", path,
                    TMX_TILESETS_MAX);
            ok = false;
        } else {
            ok = read_tileset(&t->tileset[t->tileset_count++], p, path);
        }
    }

    const char *lay = ok ? find_layer(text, layer) : NULL;
    const char *data = NULL;
    if (ok && lay == NULL) {
        fprintf(stderr, "tmx: %s: no layer %s\n", path,
                (layer != NULL) ? layer : "at all");
        ok = false;
    }
    if (ok) {
        t->width  = (int)attr_long(lay, "width", 0);
        t->height = (int)attr_long(lay, "height", 0);
        data = strstr(lay, "<data ");
        const char *enc = (data != NULL) ? attr(data, "encoding") : NULL;
        if (enc == NULL || strncmp(enc, "csv\"", 4) != 0) {
            fprintf(stderr, "tmx: %s: the layer is not CSV\n", path);
            ok = false;
        }
    }
    size_t cells = (size_t)t->width * (size_t)t->height;
    if (ok && cells == 0) {
        fprintf(stderr, "tmx: %s: a layer of no cells\n", path);
        ok = false;
    }
    if (ok) {
        t->gid = calloc(cells, sizeof *t->gid);
        ok = (t->gid != NULL);
    }
    if (ok) {
        const char *p   = strchr(data, '>') + 1;
        const char *end = strstr(p, "</data>");
        size_t      n   = 0;
        while (end != NULL && p < end) {
            char *next;
            unsigned long v = strtoul(p, &next, 10);
            if (next == p) {
                p++;
                continue;
            }
            if (n < cells) {
                t->gid[n] = (u32)v;
            }
            n++;
            p = next;
        }
        if (end == NULL || n != cells) {
            fprintf(stderr, "tmx: %s: %zu cells for a %dx%d layer\n", path,
                    n, t->width, t->height);
            ok = false;
        }
    }

    free(text);
    if (!ok) {
        tmx_free(t);
    }
    return ok;
}

bool tmx_load(Tmx *t, const char *path, const char *layer)
{
    /* (build 157d) A mod it refuses is set aside and the game's own
     * read in its place (docs/mods.md, "When a mod is refused"). */
    return tmx_read(t, path, layer)
        || (assetfs_mod_refused(path) && tmx_read(t, path, layer));
}

void tmx_free(Tmx *t)
{
    free(t->gid);
    t->gid = NULL;
}

const TmxTileset *tmx_tileset_of(const Tmx *t, u32 gid, int *local)
{
    gid &= ~TMX_FLAGS;
    for (int i = 0; i < t->tileset_count; i++) {
        const TmxTileset *ts = &t->tileset[i];
        if (gid >= ts->firstgid && gid < ts->firstgid + ts->tilecount) {
            *local = (int)(gid - ts->firstgid);
            return ts;
        }
    }
    return NULL;
}

int tmx_tileset_int(const TmxTileset *ts, const char *name, int fallback)
{
    return props_int(&ts->props, name, fallback);
}

int tmx_map_int(const Tmx *t, const char *name, int fallback)
{
    return props_int(&t->props, name, fallback);
}
