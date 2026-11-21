#include "art.h"

#include "assetfs.h"
#include "json.h"
#include "palette.h"
#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A pair of whole numbers, "size": [w, h] or "at": [col, row]. */
static bool json_pair(const JsonValue *obj, const char *key, int *a, int *b)
{
    const JsonValue *arr = json_get(obj, key);
    const JsonValue *x   = json_at(arr, 0);
    const JsonValue *y   = json_at(arr, 1);
    if (arr == NULL || json_length(arr) != 2 || x == NULL || y == NULL
        || x->type != JSON_NUMBER || y->type != JSON_NUMBER
        || x->number != (double)(int)x->number
        || y->number != (double)(int)y->number) {
        return false;
    }
    *a = (int)x->number;
    *b = (int)y->number;
    return true;
}

/* A file named in a description sits beside it: a name, no folder. */
static bool beside(const char *file)
{
    return file != NULL && file[0] != '\0' && strchr(file, '/') == NULL
        && strchr(file, '\\') == NULL;
}

bool picture_load(Picture *p, const char *json)
{
    return picture_load_frame(p, json, 1);
}

static bool picture_read_frame(Picture *p, const char *json, int number)
{
    memset(p, 0, sizeof *p);
    char key[12];
    snprintf(key, sizeof key, "%d", number);

    JsonValue *desc = json_parse_file(json);
    if (desc == NULL) {
        fprintf(stderr, "art: cannot read %s\n", json);
        return false;
    }

    bool ok = true;
    int  w = 0, h = 0;
    if (!json_pair(desc, "size", &w, &h) || w <= 0 || h <= 0) {
        fprintf(stderr, "art: %s has no \"size\": [w, h]\n", json);
        ok = false;
    }
    if (ok && (!json_pair(desc, "at", &p->at_col, &p->at_row)
               || p->at_col < 0 || p->at_row < 0)) {
        fprintf(stderr, "art: %s has no \"at\": [col, row]\n", json);
        ok = false;
    }
    const JsonValue *frame = json_get(json_get(desc, "frames"), key);
    const char *file = json_str(frame, "file", NULL);
    const char *slots = json_str(frame, "slots", NULL);
    if (ok && !beside(file)) {
        fprintf(stderr, "art: %s has no frame \"%s\" with a \"file\" beside "
                        "it\n", json, key);
        ok = false;
    }
    if (ok && slots != NULL && !beside(slots)) {
        fprintf(stderr, "art: %s: frame \"%s\"'s \"slots\" is not a file "
                        "beside it\n", json, key);
        ok = false;
    }
    snprintf(p->thing, sizeof p->thing, "%s", json_str(desc, "thing", ""));
    p->frame = number;

    char png[256];
    char slots_png[256] = "";
    if (ok) {
        const char *slash = strrchr(json, '/');
        int dir = (slash != NULL) ? (int)(slash - json) + 1 : 0;
        if (snprintf(png, sizeof png, "%.*s%s", dir, json, file)
            >= (int)sizeof png
            || (slots != NULL
                && snprintf(slots_png, sizeof slots_png, "%.*s%s", dir, json,
                            slots) >= (int)sizeof slots_png)) {
            fprintf(stderr, "art: %s: the frame's path is too long\n", json);
            ok = false;
        }
    }
    json_free(desc);

    PngRgba img;
    if (ok && !png_load_rgba(&img, png)) {
        fprintf(stderr, "art: %s: its frame %s does not load\n", json, png);
        ok = false;
    } else if (ok && (img.width != w || img.height != h)) {
        fprintf(stderr, "art: %s is %dx%d, its description says %dx%d\n",
                png, img.width, img.height, w, h);
        png_rgba_free(&img);
        ok = false;
    }
    if (!ok) {
        memset(p, 0, sizeof *p);
        return false;
    }
    p->w    = w;
    p->h    = h;
    p->rgba = img.rgba;       /* taken over: png_rgba_free not called */

    if (slots_png[0] != '\0') {
        PngImage map;
        bool good = png_load_indexed(&map, slots_png);
        if (!good) {
            fprintf(stderr, "art: %s: its slot map %s does not load\n", json,
                    slots_png);
        } else if (map.width != w || map.height != h) {
            fprintf(stderr, "art: %s is %dx%d, its picture %dx%d\n",
                    slots_png, map.width, map.height, w, h);
            good = false;
        }
        for (int i = 0; good && i < w * h; i++) {
            if (map.px[i] >= PAL_ENTRIES) {
                fprintf(stderr, "art: %s: index %u at (%d,%d) is past a "
                                "palette line's 15\n", slots_png,
                        (unsigned)map.px[i], i % w, i / w);
                good = false;
            }
        }
        if (!good) {
            png_free(&map);
            picture_free(p);
            return false;
        }
        p->slots = map.px;    /* taken over: png_free not called */
    }
    return true;
}

bool picture_load_frame(Picture *p, const char *json, int number)
{
    if (picture_read_frame(p, json, number)) {
        return true;
    }
    /* (build 157d) A mod it refuses is set aside and the game's own
     * read in its place (docs/mods.md, "When a mod is refused"): the
     * thing's whole folder, its description, picture and slot map
     * being one. */
    char folder[256];
    const char *slash = strrchr(json, '/');
    int dir = (slash != NULL) ? (int)(slash - json) + 1 : 0;
    snprintf(folder, sizeof folder, "%.*s", dir, json);
    return dir > 0 && assetfs_mods_refused_under(folder) > 0
        && picture_read_frame(p, json, number);
}

void picture_free(Picture *p)
{
    free(p->rgba);
    free(p->slots);
    memset(p, 0, sizeof *p);
}

/* ------------------------------------------------------------------ */
/* The portraits                                                      */
/* ------------------------------------------------------------------ */

/* The windows' pictures and (build 146) the library graph's. */
#define ART_PORTRAIT_PICTURES (ART_PORTRAIT_WINDOWS + 1)

static Picture  g_portrait_pictures[ART_PORTRAIT_PICTURES];
static int      g_portrait_picture_count;
static const Picture *g_portrait_of[ART_PORTRAIT_WINDOWS];
static const Picture *g_library_graph;

void art_portraits_free(void)
{
    for (int i = 0; i < g_portrait_picture_count; i++) {
        picture_free(&g_portrait_pictures[i]);
    }
    g_portrait_picture_count = 0;
    memset(g_portrait_of, 0, sizeof g_portrait_of);
    g_library_graph = NULL;
}

/* A thing name is a folder: letters, digits and underscores. */
static bool thing_name(const char *s)
{
    if (s == NULL || s[0] == '\0' || strlen(s) >= sizeof(((Picture *)0)->thing)) {
        return false;
    }
    for (const char *c = s; *c != '\0'; c++) {
        bool word = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9')
                 || *c == '_';
        if (!word) {
            return false;
        }
    }
    return true;
}

/* assets/art/portraits/<thing>/<thing>.json, loaded once however many
 * windows show it; NULL, and `*ok` false, when it does not load. */
static const Picture *portrait_thing(const char *thing, bool *ok)
{
    for (int i = 0; i < g_portrait_picture_count; i++) {
        if (strcmp(g_portrait_pictures[i].thing, thing) == 0) {
            return &g_portrait_pictures[i];
        }
    }
    if (g_portrait_picture_count >= ART_PORTRAIT_PICTURES) {
        return NULL;
    }
    char json[160];
    snprintf(json, sizeof json, "assets/art/portraits/%s/%s.json", thing,
             thing);
    Picture *p = &g_portrait_pictures[g_portrait_picture_count];
    if (!picture_load(p, json)) {
        *ok = false;
        return NULL;
    }
    if (strcmp(p->thing, thing) != 0) {
        fprintf(stderr, "art: %s says it is \"%s\"; read as \"%s\"\n", json,
                p->thing, thing);
        snprintf(p->thing, sizeof p->thing, "%s", thing);
    }
    g_portrait_picture_count++;
    return p;
}

bool art_portraits_load(void)
{
    static const char INDEX[] = "assets/art/portraits/portraits.json";

    art_portraits_free();
    JsonValue *root = json_parse_file(INDEX);
    const JsonValue *windows = json_get(root, "windows");
    if (windows == NULL || windows->type != JSON_OBJECT) {
        fprintf(stderr, "art: cannot read %s's \"windows\": the portraits "
                        "draw from their tiles\n", INDEX);
        json_free(root);
        return false;
    }

    bool ok = true;
    for (const JsonValue *e = windows->child; e != NULL; e = e->next) {
        char *end = NULL;
        long  n   = strtol(e->key, &end, 10);
        if (end == e->key || *end != '\0' || n < 0
            || n >= ART_PORTRAIT_WINDOWS) {
            fprintf(stderr, "art: %s: window \"%s\" is not 0-%d\n", INDEX,
                    e->key, ART_PORTRAIT_WINDOWS - 1);
            ok = false;
            continue;
        }
        const char *thing = (e->type == JSON_STRING) ? e->string : NULL;
        if (!thing_name(thing)) {
            fprintf(stderr, "art: %s: window %ld's thing is not a name of "
                            "lower-case letters, digits and _\n", INDEX, n);
            ok = false;
            continue;
        }
        g_portrait_of[n] = portrait_thing(thing, &ok);
    }

    /* (build 146) The library graph. */
    const JsonValue *graph = json_get(root, "library_graph");
    const char *thing = (graph != NULL && graph->type == JSON_STRING)
                      ? graph->string : NULL;
    if (!thing_name(thing)) {
        fprintf(stderr, "art: %s has no \"library_graph\" naming a thing: "
                        "the graph draws from its tiles\n", INDEX);
        ok = false;
    } else {
        g_library_graph = portrait_thing(thing, &ok);
    }
    json_free(root);
    return ok;
}

const Picture *art_portrait(int n)
{
    return (n >= 0 && n < ART_PORTRAIT_WINDOWS) ? g_portrait_of[n] : NULL;
}

const Picture *art_library_graph(void)
{
    return g_library_graph;
}

/* ------------------------------------------------------------------ */
/* (build 147) The cut-scene pictures                                 */
/* ------------------------------------------------------------------ */

#define ART_SCENES 32      /* (build 149) a frame each */

static Picture g_scene_pictures[ART_SCENES];
static int     g_scene_count;

const Picture *art_scene(const char *thing)
{
    return art_scene_frame(thing, 1);
}

const Picture *art_scene_frame(const char *thing, int frame)
{
    for (int i = 0; i < g_scene_count; i++) {
        if (strcmp(g_scene_pictures[i].thing, thing) == 0
            && g_scene_pictures[i].frame == frame) {
            return &g_scene_pictures[i];
        }
    }
    if (!thing_name(thing)) {
        fprintf(stderr, "art: scene \"%s\" is not a name of lower-case "
                        "letters, digits and _\n", thing);
        return NULL;
    }
    if (g_scene_count >= ART_SCENES) {
        fprintf(stderr, "art: more than %d scene pictures; \"%s\" frame %d "
                        "not loaded\n", ART_SCENES, thing, frame);
        return NULL;
    }
    char json[160];
    snprintf(json, sizeof json, "assets/art/scenes/%s/%s.json", thing,
             thing);
    Picture *p = &g_scene_pictures[g_scene_count];
    if (!picture_load_frame(p, json, frame)) {
        return NULL;
    }
    snprintf(p->thing, sizeof p->thing, "%s", thing);
    g_scene_count++;
    return p;
}

void art_scenes_free(void)
{
    for (int i = 0; i < g_scene_count; i++) {
        picture_free(&g_scene_pictures[i]);
    }
    g_scene_count = 0;
}
