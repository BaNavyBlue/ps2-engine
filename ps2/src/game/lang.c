#include "lang.h"

#include "../core/assetfs.h"
#include "../core/json.h"
#include "../core/tileset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LANG_LIST  "assets/lang/languages.json"
#define ASSETS     "assets/"
#define ASSETS_LEN 7

/* Letters past ASCII a language's names write (lang.json "charset"):
 * a code point and its script byte. */
#define LANG_EXTRA_MAX 256       /* (build 165) the Japanese cartridge's 187 */

#define LANG_CHARS     8          /* the party (GD_CHARS) */
#define LANG_CHAR_LEN  16

typedef struct {
    char key[LANG_KEY_LEN];
    char name[LANG_NAME_LEN + 1];
    char dir[64];               /* "assets/lang/<key>/", "" for the stock */
    /* Its names.json's default party names, read at lang_init: a save's
     * name that is one of them is a default (lang_is_default_name). */
    char chars[LANG_CHARS][LANG_CHAR_LEN];
} LangEntry;

static LangEntry g_lang[LANG_MAX];
static int       g_lang_count;
static int       g_current;

/* The chosen language's: its names, its face and its letters. */
static JsonValue *g_names;
static bool       g_proportional;
static bool       g_tall;               /* (build 165) */
static bool       g_charset_full;       /* (build 165) */
static TileSet    g_face;
static TileSet    g_face_upper;         /* (build 165) */
static char       g_layout[8][16];      /* (build 165) lang.json "layout" */
static int        g_layout_count;
static u8         g_advance[256];
static u32        g_extra_cp[LANG_EXTRA_MAX];
static u8         g_extra_code[LANG_EXTRA_MAX];
static int        g_extra_count;

static void set_stock(LangEntry *e)
{
    memset(e, 0, sizeof *e);
    snprintf(e->key, sizeof e->key, "us");
    snprintf(e->name, sizeof e->name, "ENGLISH 1989");
}

static void read_default_names(LangEntry *l)
{
    char path[128];
    snprintf(path, sizeof path, "%snames.json", l->dir);
    if (!assetfs_exists(path)) {
        return;
    }
    JsonValue *names = json_parse_file(path);
    const JsonValue *chars = json_get(names, "characters");
    for (int c = 0; c < LANG_CHARS; c++) {
        const JsonValue *v = json_at(chars, c);
        if (v != NULL && v->type == JSON_STRING) {
            snprintf(l->chars[c], sizeof l->chars[c], "%s", v->string);
        }
    }
    json_free(names);
}

void lang_init(void)
{
    memset(g_lang, 0, sizeof g_lang);
    g_lang_count = 0;

    JsonValue *root = json_parse_file(LANG_LIST);
    const JsonValue *list = json_get(root, "languages");
    for (const JsonValue *e = (list != NULL) ? list->child : NULL;
         e != NULL && g_lang_count < LANG_MAX; e = e->next) {
        LangEntry *l = &g_lang[g_lang_count];
        const char *key = json_str(e, "key", "");
        if (key[0] == '\0') {
            continue;
        }
        snprintf(l->key, sizeof l->key, "%s", key);
        snprintf(l->name, sizeof l->name, "%s", json_str(e, "name", key));
        /* The first is the cartridge's: the game's own files. */
        if (g_lang_count > 0) {
            snprintf(l->dir, sizeof l->dir, "%slang/%s/", ASSETS, key);
            read_default_names(l);
        }
        g_lang_count++;
    }
    json_free(root);

    /* No list (a tree from before build 163, or a broken mod of it): the
     * stock language alone. */
    if (g_lang_count == 0) {
        set_stock(&g_lang[0]);
        g_lang_count = 1;
    }
    g_current = -1;
    lang_select(0);
}

int lang_count(void)
{
    return g_lang_count;
}

const char *lang_key(int i)
{
    return (i >= 0 && i < g_lang_count) ? g_lang[i].key : NULL;
}

const char *lang_name(int i)
{
    return (i >= 0 && i < g_lang_count) ? g_lang[i].name : NULL;
}

int lang_find(const char *key)
{
    for (int i = 0; key != NULL && i < g_lang_count; i++) {
        if (strcmp(g_lang[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

int lang_current(void)
{
    return g_current;
}

static void unload(void)
{
    json_free(g_names);
    g_names = NULL;
    tileset_free(&g_face);
    tileset_free(&g_face_upper);
    g_proportional = false;
    g_tall         = false;
    g_charset_full = false;
    g_layout_count = 0;
    memset(g_advance, 0, sizeof g_advance);
    g_extra_count = 0;
}

/* One UTF-8 code point from *pp, advancing past it; a byte that starts
 * none is taken as itself. */
u32 lang_next_code_point(const char **pp)
{
    const u8 *p = (const u8 *)*pp;
    u32 cp = p[0];
    int extra = 0;
    if (cp >= 0xF0 && cp < 0xF8) {
        cp &= 0x07;
        extra = 3;
    } else if (cp >= 0xE0) {
        cp &= 0x0F;
        extra = 2;
    } else if (cp >= 0xC0) {
        cp &= 0x1F;
        extra = 1;
    }
    int k = 1;
    for (; k <= extra && (p[k] & 0xC0) == 0x80; k++) {
        cp = (cp << 6) | (p[k] & 0x3F);
    }
    if (k <= extra) {
        cp = p[0];
        k  = 1;
    }
    *pp += k;
    return cp;
}

/* lang.json: whether the face is proportional, the face, its advances
 * and the letters past ASCII. False (said) when it is unusable. */
static bool load_lang_json(const LangEntry *l)
{
    char path[128];
    snprintf(path, sizeof path, "%slang.json", l->dir);
    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        fprintf(stderr, "lang: %s does not parse\n", path);
        return false;
    }

    bool ok = true;
    g_proportional = json_bool(root, "proportional", false);
    g_tall         = json_bool(root, "tall", false);
    g_charset_full = json_bool(root, "charset_full", false);
    const JsonValue *layout = json_get(root, "layout");
    for (const JsonValue *f = (layout != NULL) ? layout->child : NULL;
         f != NULL && g_layout_count < 8; f = f->next) {
        if (f->type == JSON_STRING) {
            snprintf(g_layout[g_layout_count++], sizeof g_layout[0], "%s",
                     f->string);
        }
    }
    if (g_tall) {
        char upper[128];
        snprintf(upper, sizeof upper, "%s%s", l->dir,
                 json_str(root, "font_upper", "font_upper.png"));
        if (!tileset_load(&g_face_upper, upper) || g_face_upper.count < 256) {
            fprintf(stderr, "lang: %s is not a face of 256 glyphs\n", upper);
            ok = false;
        }
    }
    if (g_proportional) {
        char font[128];
        snprintf(font, sizeof font, "%s%s", l->dir,
                 json_str(root, "font", "font.png"));
        const JsonValue *adv = json_get(root, "advance");
        if (!tileset_load(&g_face, font) || g_face.count < 256) {
            fprintf(stderr, "lang: %s is not a face of 256 glyphs\n", font);
            ok = false;
        } else if (json_length(adv) != 256) {
            fprintf(stderr, "lang: %s has no advance for each of 256 "
                            "bytes\n", path);
            ok = false;
        } else {
            for (int i = 0; i < 256; i++) {
                const JsonValue *a = json_at(adv, i);
                int w = (a != NULL && a->type == JSON_NUMBER)
                      ? (int)a->number : 8;
                g_advance[i] = (u8)((w < 0) ? 0 : (w > 16) ? 16 : w);
            }
        }
    }

    const JsonValue *cs = json_get(root, "charset");
    for (const JsonValue *c = (cs != NULL) ? cs->child : NULL;
         c != NULL && g_extra_count < LANG_EXTRA_MAX; c = c->next) {
        if (c->key == NULL || c->type != JSON_NUMBER) {
            continue;
        }
        const char *k = c->key;
        g_extra_cp[g_extra_count]   = lang_next_code_point(&k);
        g_extra_code[g_extra_count] = (u8)c->number;
        g_extra_count++;
    }
    json_free(root);
    return ok;
}

void lang_select(int i)
{
    if (i < 0 || i >= g_lang_count) {
        i = 0;
    }
    if (i == g_current) {
        return;
    }
    unload();
    g_current = i;
    const LangEntry *l = &g_lang[i];
    if (l->dir[0] == '\0') {
        return;
    }

    if (!load_lang_json(l)) {
        fprintf(stderr, "lang: %s is set aside; %s is read\n", l->key,
                g_lang[0].key);
        unload();
        g_current = 0;
        return;
    }
    char path[128];
    snprintf(path, sizeof path, "%snames.json", l->dir);
    if (assetfs_exists(path)) {
        g_names = json_parse_file(path);
        if (g_names == NULL) {
            fprintf(stderr, "lang: %s does not parse; the game's own names "
                            "stand\n", path);
        }
    }
}

const char *lang_path(const char *path, char *buf, size_t n)
{
    const LangEntry *l = (g_current >= 0) ? &g_lang[g_current] : NULL;
    if (l == NULL || l->dir[0] == '\0'
        || strncmp(path, ASSETS, ASSETS_LEN) != 0) {
        return path;
    }
    snprintf(buf, n, "%s%s", l->dir, path + ASSETS_LEN);
    return assetfs_exists(buf) ? buf : path;
}

/* ---- the face ---- */

bool lang_proportional(void)
{
    return g_proportional;
}

const Tile *lang_glyph(u8 code)
{
    return (g_proportional && code < g_face.count) ? &g_face.tiles[code]
                                                   : NULL;
}

bool lang_tall(void)
{
    return g_tall;
}

const Tile *lang_glyph_upper(u8 code)
{
    return (g_tall && code < g_face_upper.count) ? &g_face_upper.tiles[code]
                                                 : NULL;
}

bool lang_charset_full(void)
{
    return g_charset_full;
}

bool lang_layout(const char *flag)
{
    for (int i = 0; i < g_layout_count; i++) {
        if (strcmp(g_layout[i], flag) == 0) {
            return true;
        }
    }
    return false;
}

int lang_advance(u8 code)
{
    return g_proportional ? g_advance[code] : 8;
}

int lang_measure(const u8 *codes, int n)
{
    int w = 0;
    for (int i = 0; i < n; i++) {
        w += lang_advance(codes[i]);
    }
    return w;
}

static void draw_glyphs(Tile *strip, int cells, int x, const u8 *codes,
                        int n, u8 ink, bool upper)
{
    const int width = cells * TILE_W;
    for (int i = 0; i < n; i++) {
        const Tile *g   = upper ? lang_glyph_upper(codes[i])
                                : lang_glyph(codes[i]);
        int         adv = lang_advance(codes[i]);
        /* His engine's rule (ext/vwf.asm): a glyph whose ink would cross
         * the line's end is dropped; the advance's last pixel is the gap. */
        if (x + adv - 1 > width) {
            return;
        }
        for (int gy = 0; g != NULL && gy < TILE_H; gy++) {
            for (int gx = 0; gx < TILE_W; gx++) {
                int px = x + gx;
                if (g->px[gy * TILE_W + gx] == 0 || px >= width) {
                    continue;
                }
                strip[px / TILE_W].px[gy * TILE_W + px % TILE_W] = ink;
            }
        }
        x += adv;
    }
}

void lang_draw(Tile *strip, int cells, int x, const u8 *codes, int n, u8 ink)
{
    draw_glyphs(strip, cells, x, codes, n, ink, false);
}

void lang_draw_upper(Tile *strip, int cells, int x, const u8 *codes, int n,
                     u8 ink)
{
    draw_glyphs(strip, cells, x, codes, n, ink, true);
}

/* ---- the names ---- */

/* The script charset (ps2.asm:25907) for a code point, then the
 * language's own letters; 0 (a space) for anything else. */
static u8 code_for(u32 cp)
{
    if (g_charset_full) {
        for (int i = 0; i < g_extra_count; i++) {
            if (g_extra_cp[i] == cp) {
                return g_extra_code[i];
            }
        }
        return 0;
    }
    if (cp >= '0' && cp <= '9') {
        return (u8)(1 + (cp - '0'));
    }
    if (cp >= 'A' && cp <= 'Z') {
        return (u8)(11 + (cp - 'A'));
    }
    if (cp >= 'a' && cp <= 'z') {
        return (u8)(37 + (cp - 'a'));
    }
    static const char PUNCT[] = ",.;\"?!'-";
    for (int i = 0; PUNCT[i] != '\0'; i++) {
        if (cp == (u32)(u8)PUNCT[i]) {
            return (u8)(0x3F + i);
        }
    }
    if (cp == ':') {
        return 0x77;
    }
    for (int i = 0; i < g_extra_count; i++) {
        if (g_extra_cp[i] == cp) {
            return g_extra_code[i];
        }
    }
    return 0;
}

u8 lang_code_of(u32 cp)
{
    return code_for(cp);
}

/* One code point as UTF-8 into out (room for four and the NUL). */
static void put_utf8(u32 cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        out[1] = '\0';
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        out[2] = '\0';
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = '\0';
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        out[4] = '\0';
    }
}

/* A code's character: the language's own (charset), or the script
 * charset's ASCII; '?' for one with none. */
static u32 char_of(u8 code)
{
    for (int i = 0; i < g_extra_count; i++) {
        if (g_extra_code[i] == code) {
            return g_extra_cp[i];
        }
    }
    if (g_charset_full) {
        return '?';
    }
    if (code == 0) {
        return ' ';
    }
    if (code >= 1 && code <= 10) {
        return (u32)('0' + code - 1);
    }
    if (code >= 11 && code <= 36) {
        return (u32)('A' + code - 11);
    }
    if (code >= 37 && code <= 62) {
        return (u32)('a' + code - 37);
    }
    static const char PUNCT[] = ",.;\"?!'-";
    if (code >= 0x3F && code < 0x3F + 8) {
        return (u32)(u8)PUNCT[code - 0x3F];
    }
    if (code == 0x77) {
        return ':';
    }
    return '?';
}

void lang_text_of(const u8 *codes, int n, char *out, size_t size)
{
    size_t k = 0;
    out[0] = '\0';
    for (int i = 0; i < n && codes[i] != 0xC4; i++) {
        char one[5];
        put_utf8(char_of(codes[i]), one);
        size_t len = strlen(one);
        if (k + len + 1 > size) {
            break;
        }
        memcpy(out + k, one, len + 1);
        k += len;
    }
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

int lang_codes(const char *text, u8 *out, int max)
{
    int n = 0;
    const char *p = text;
    while (p != NULL && *p != '\0' && n < max) {
        if (p[0] == '{' && hex_value(p[1]) >= 0 && hex_value(p[2]) >= 0
            && p[3] == '}') {
            out[n++] = (u8)(hex_value(p[1]) * 16 + hex_value(p[2]));
            p += 4;
            continue;
        }
        out[n++] = code_for(lang_next_code_point(&p));
    }
    return n;
}

static const char *names_str(const char *table, const char *key)
{
    const JsonValue *t = json_get(g_names, table);
    const JsonValue *v = json_get(t, key);
    return (v != NULL && v->type == JSON_STRING) ? v->string : NULL;
}

static const char *names_at(const char *table, int index)
{
    const JsonValue *v = json_at(json_get(g_names, table), index);
    return (v != NULL && v->type == JSON_STRING) ? v->string : NULL;
}

const char *lang_item_name(const char *key)
{
    return (key != NULL) ? names_str("items", key) : NULL;
}

const char *lang_tech_name(const char *key)
{
    return (key != NULL) ? names_str("techniques", key) : NULL;
}

const char *lang_enemy_name(const char *label)
{
    return (label != NULL) ? names_str("enemies", label) : NULL;
}

const char *lang_char_name(int index)
{
    return names_at("characters", index);
}

bool lang_is_default_name(int index, const char *name)
{
    if (index < 0 || index >= LANG_CHARS || name == NULL || name[0] == '\0') {
        return false;
    }
    for (int i = 0; i < g_lang_count; i++) {
        if (strcmp(g_lang[i].chars[index], name) == 0) {
            return true;
        }
    }
    return false;
}

const char *lang_list_name(const char *list, int index)
{
    return names_at(list, index);
}

const char *lang_window_label(const char *art, int row)
{
    const JsonValue *v = json_at(json_get(json_get(g_names, "windows"), art),
                                 row);
    return (v != NULL && v->type == JSON_STRING) ? v->string : NULL;
}
