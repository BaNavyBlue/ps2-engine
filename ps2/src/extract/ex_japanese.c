/* (build 191) tools/import_japanese.py in C: the JAPANESE language
 * (assets/lang/ja/) from the player's Japanese cartridge -- Phantasy Star
 * II: Kaerazaru Toki no Owari ni -- and Excalibur_Z's retranslation, whose
 * work/dialogue.json and work/script.json carry the cartridge's text
 * matched to the US script ids (docs/languages.md, Japanese). The same
 * files as the Python's, byte for byte (the two PNGs the same picture);
 * tools/japanese_c_check.py holds the two together.
 *
 * Written from the cartridge: the face (font.png, font_upper.png: each
 * code's lower and upper tile from FontsIconsArt) and the character map
 * (VDPCharacterMaps: each code's two tiles, whose letters give its
 * character); the name board's rows, from its window's art. From the
 * retranslation's `jp`: every US script id's Japanese (script/), the
 * names and labels drawn (names.json). Where each comes from, and where
 * the port changes it, is the Python's docstring and docs/languages.md.
 *
 * The Python's text is Unicode; here it is UTF-8, and what the Python
 * does to a string is done to its code points: str.isspace's whitespace,
 * re's \d and int()'s digits (Unicode 16's decimal digits, Python
 * 3.14's), str.splitlines's line ends -- the tables below. The kana are
 * written here as UTF-8, as this file is. */

#include "extract.h"

#include "exart.h"
#include "exzip.h"

#include <stdlib.h>
#include <string.h>

#define JP_SIZE        EX_JP_SIZE
#define JP_CRC         EX_JP_CRC
#define JP_FONT        0x26800u   /* FontsIconsArt */
#define JP_CHARMAP     0x129C8u   /* VDPCharacterMaps: (upper, lower) */
/* WindowArtLayoutPtrs ($13768 in the US), a window id's 8 bytes at id - 1:
 * position, art pointer, width + 1, lines - 1 (his tools/jptables.py); the
 * name board is window $24, its art in the image itself. */
#define JP_WINDOWS     0x13568u
#define WIN_NAME_INPUT 0x24
#define CODES          0xD0       /* the map's entries */
#define DAKUTEN        0x5B
#define HANDAKUTEN     0x5C
#define BLANK          0x26
#define FONT_TILES     192
#define SHEET_TILES    16         /* ps2png.SHEET_TILES */

typedef unsigned long Cp;         /* a code point */

/* The JP font sheet's letters by tile (Excalibur_Z's tools/ps2text.py,
 * _JP_TILES, "read off the JP font sheet"); '_' is no letter. */
static const struct {
    int         start;
    const char *chars;
} TILE_ROWS[] = {
    { 0x10, "BC" },
    { 0x26, " あいうえおかきくけ" },
    { 0x30, "こさしすせそたちつてとなにぬねの" },
    { 0x40, "はひふへほまみむめもやゆよらりる" },
    { 0x50, "れろ!?わをんっゃゅょ__、。ア" },
    { 0x60, "イウエオカキクケコサシスセソタチ" },
    { 0x70, "ツテトナニヌネノハヒフ・ホマミム" },
    { 0x80, "メモヤユヨラリルレロワヲンァィ_" },
    { 0x90, "ェォッャュョー012345678" },
    { 0xA0, "9ADEHLMNOPSTVWXY" },
    { 0xB0, "「」/▼" },
};

/* The voiced and half-voiced forms its dakuten and handakuten make: the
 * n-th letter of each FROM to the n-th of its TO. */
static const char VOICED_FROM[] =
    "かきくけこさしすせそたちつてとはひふへほ"
    "カキクケコサシスセソタチツテトハヒフヘホウ";
static const char VOICED_TO[] =
    "がぎぐげござじずぜぞだぢづでどばびぶべぼ"
    "ガギグゲゴザジズゼゾダヂヅデドバビブベボヴ";
static const char HALF_FROM[] = "はひふへほハヒフヘホ";
static const char HALF_TO[]   = "ぱぴぷぺぽパピプペポ";

/* His notation to the port's, as import_retranslation.py's. */
static const struct {
    const char *token;
    const char *port;
} TOKENS[] = {
    { "BR",     "\n" },
    { "CLR",    "{cont}" },
    { "PAGE",   "{pause}" },
    { "END",    "{end:C4}" },
    { "C5",     "{end:C5}" },
    { "C6",     "{end:C6}" },
    { "C7",     "{end:C7}" },
    { "NAME",   "{char}" },
    { "NAME2",  "{char2}" },
    { "ENEMY",  "{enemy}" },
    { "TECH",   "{tech}" },
    { "ITEM",   "{item}" },
    { "MESETA", "{meseta}" },
};

/* 0D05 (the Central Tower's report on Neifirst): both cartridges have no
 * end code there, and run on into the Room bank's table bytes and its
 * greeting; his `jp` keeps them. The port's ends it, as the bank's last
 * entry, and as his English does: the Japanese up to the garbage page,
 * ended. */
static const char GARBAGE_0D05[] = "えはけうねまみせぬふねとのあやあ";

/* tools/script_ends.py's RUN_ON: $010B's run-on is the language's own. */
#define RUN_ON 0x010B

static const char NOTICE[] =
    "The Japanese here is SEGA's, from Phantasy Star II: Kaerazaru Toki\n"
    "no Owari ni (1989; the cartridge CRC32 BEC8EB5A): the font and the\n"
    "character map read from the cartridge by the port's\n"
    "tools/import_japanese.py, and the text as Excalibur_Z's Phantasy\n"
    "Star II: English Retranslation (github.com/ExcalZ/ps2-retranslation)\n"
    "carries it -- decoded from that cartridge and matched to the US\n"
    "script ids by his tools (work/dialogue.json, work/script.json, the\n"
    "`jp` fields; his tools and his work are MIT, see\n"
    "../retranslation/LICENSE.txt). Where the port changes it: the\n"
    "Central Tower's 0D05 ended before the bytes both cartridges run on\n"
    "into, and the second halves of the messages the US split empty, the\n"
    "first half's text running on through them (docs/languages.md).\n";

/* ---- Python's text, in UTF-8 ---- */

/* The first code point of each run of ten decimal digits (Unicode 16.0's
 * category Nd, which re's \d and int() take: Python 3.14's). */
static const Cp DIGIT_RUNS[] = {
    0x30, 0x660, 0x6F0, 0x7C0, 0x966, 0x9E6, 0xA66, 0xAE6, 0xB66, 0xBE6,
    0xC66, 0xCE6, 0xD66, 0xDE6, 0xE50, 0xED0, 0xF20, 0x1040, 0x1090,
    0x17E0, 0x1810, 0x1946, 0x19D0, 0x1A80, 0x1A90, 0x1B50, 0x1BB0,
    0x1C40, 0x1C50, 0xA620, 0xA8D0, 0xA900, 0xA9D0, 0xA9F0, 0xAA50,
    0xABF0, 0xFF10, 0x104A0, 0x10D30, 0x10D40, 0x11066, 0x110F0, 0x11136,
    0x111D0, 0x112F0, 0x11450, 0x114D0, 0x11650, 0x116C0, 0x116D0,
    0x116DA, 0x11730, 0x118E0, 0x11950, 0x11BF0, 0x11C50, 0x11D50,
    0x11DA0, 0x11F50, 0x16130, 0x16A60, 0x16AC0, 0x16B50, 0x16D70,
    0x1CCF0, 0x1D7CE, 0x1D7D8, 0x1D7E2, 0x1D7EC, 0x1D7F6, 0x1E140,
    0x1E2F0, 0x1E4F0, 0x1E5F1, 0x1E950, 0x1FBF0,
};

/* A decimal digit's value, or -1. */
static int py_digit(Cp c)
{
    for (size_t i = 0; i < sizeof DIGIT_RUNS / sizeof DIGIT_RUNS[0]; i++) {
        if (c >= DIGIT_RUNS[i] && c < DIGIT_RUNS[i] + 10) {
            return (int)(c - DIGIT_RUNS[i]);
        }
    }
    return -1;
}

/* str.isspace (and re's \s). */
static bool py_space(Cp c)
{
    return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20)
        || c == 0x85 || c == 0xA0 || c == 0x1680
        || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029
        || c == 0x202F || c == 0x205F || c == 0x3000;
}

/* Whether n bytes are UTF-8 as Python's strict decoder takes it: no
 * overlong forms, no surrogates, nothing past U+10FFFF. */
static bool utf8_valid(const u8 *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        u8 b = s[i];
        if (b < 0x80) {
            i++;
            continue;
        }
        int more;
        Cp c;
        Cp least;
        if (b >= 0xC2 && b <= 0xDF) {
            more = 1;
            c = b & 0x1F;
            least = 0x80;
        } else if (b >= 0xE0 && b <= 0xEF) {
            more = 2;
            c = b & 0x0F;
            least = 0x800;
        } else if (b >= 0xF0 && b <= 0xF4) {
            more = 3;
            c = b & 0x07;
            least = 0x10000;
        } else {
            return false;
        }
        if (n - i <= (size_t)more) {
            return false;
        }
        for (int k = 1; k <= more; k++) {
            if ((s[i + (size_t)k] & 0xC0) != 0x80) {
                return false;
            }
            c = (c << 6) | (s[i + (size_t)k] & 0x3F);
        }
        if (c < least || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
            return false;
        }
        i += (size_t)more + 1;
    }
    return true;
}

/* The code point at *p (the text is valid UTF-8), *p past it. */
static Cp utf8_next(const char **p)
{
    const u8 *s = (const u8 *)*p;
    Cp c = s[0];
    int more = 0;
    if (c >= 0xF0) {
        c &= 0x07;
        more = 3;
    } else if (c >= 0xE0) {
        c &= 0x0F;
        more = 2;
    } else if (c >= 0xC0) {
        c &= 0x1F;
        more = 1;
    }
    for (int k = 1; k <= more && s[k] != 0; k++) {
        c = (c << 6) | (s[k] & 0x3F);
    }
    *p += 1 + more;
    return c;
}

static void utf8_put(ExStr *s, Cp c)
{
    char b[4];
    size_t n;
    if (c < 0x80) {
        b[0] = (char)c;
        n = 1;
    } else if (c < 0x800) {
        b[0] = (char)(0xC0 | (c >> 6));
        b[1] = (char)(0x80 | (c & 0x3F));
        n = 2;
    } else if (c < 0x10000) {
        b[0] = (char)(0xE0 | (c >> 12));
        b[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        b[2] = (char)(0x80 | (c & 0x3F));
        n = 3;
    } else {
        b[0] = (char)(0xF0 | (c >> 18));
        b[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        b[3] = (char)(0x80 | (c & 0x3F));
        n = 4;
    }
    ex_str_addn(s, b, n);
}

/* A new empty text (its buffer there even when nothing is added). */
static void str_new(Extract *x, ExStr *s)
{
    ex_str_init(x, s);
    ex_str_addn(s, "", 0);
}

/* The length of s[0..n) with str.rstrip()'s whitespace off its end. */
static size_t rstrip_len(const char *s, size_t n)
{
    size_t keep = 0;
    const char *p = s;
    while (p < s + n) {
        Cp c = utf8_next(&p);
        if (!py_space(c)) {
            keep = (size_t)(p - s);
        }
    }
    return keep;
}

/* str.strip(). */
static char *py_strip(Extract *x, const char *s)
{
    const char *p = s;
    while (*p != '\0') {
        const char *at = p;
        if (!py_space(utf8_next(&p))) {
            p = at;
            break;
        }
    }
    return ex_strndup(x, p, rstrip_len(p, strlen(p)));
}

/* repr() of one character, for the messages. */
static const char *char_repr(Extract *x, Cp c)
{
    if (c < 0x20 || c == 0x7F) {
        return ex_fmt(x, "'\\x%02lx'", c);
    }
    ExStr s;
    str_new(x, &s);
    ex_str_addc(&s, (c == '\'') ? '"' : '\'');
    utf8_put(&s, c);
    ex_str_addc(&s, (c == '\'') ? '"' : '\'');
    return s.buf;
}

/* str.splitlines(): the next line of [*p, end), its ending left out;
 * false when there are none. */
static bool next_line(const char **p, const char *end, const char **line,
                      size_t *len)
{
    if (*p >= end) {
        return false;
    }
    const char *s = *p;
    const char *q = s;
    while (q < end) {
        u8 b = (u8)*q;
        size_t eol = 0;
        if (b == '\r') {
            eol = (q + 1 < end && q[1] == '\n') ? 2 : 1;
        } else if (b == '\n' || b == 0x0B || b == 0x0C || b == 0x1C
                   || b == 0x1D || b == 0x1E) {
            eol = 1;
        } else if (b == 0xC2 && q + 1 < end && (u8)q[1] == 0x85) {
            eol = 2;
        } else if (b == 0xE2 && q + 2 < end && (u8)q[1] == 0x80
                   && ((u8)q[2] == 0xA8 || (u8)q[2] == 0xA9)) {
            eol = 3;
        }
        if (eol > 0) {
            *line = s;
            *len = (size_t)(q - s);
            *p = q + eol;
            return true;
        }
        q++;
    }
    *line = s;
    *len = (size_t)(end - s);
    *p = end;
    return true;
}

/* int() of s[0..n): whitespace around it, a sign, digits with single
 * '_' between them. False when it is not one. */
static bool py_int(const char *s, size_t n, long *out)
{
    const char *p = s;
    const char *end = s + n;
    while (p < end) {
        const char *at = p;
        if (!py_space(utf8_next(&p))) {
            p = at;
            break;
        }
    }
    size_t trimmed = rstrip_len(p, (size_t)(end - p));
    end = p + trimmed;
    bool negative = false;
    if (p < end && (*p == '+' || *p == '-')) {
        negative = *p == '-';
        p++;
    }
    long v = 0;
    int digits = 0;
    bool underscore = false;
    while (p < end) {
        if (*p == '_') {
            if (digits == 0 || underscore) {
                return false;
            }
            underscore = true;
            p++;
            continue;
        }
        int d = py_digit(utf8_next(&p));
        if (d < 0) {
            return false;
        }
        v = v * 10 + d;
        digits++;
        underscore = false;
    }
    if (digits == 0 || underscore) {
        return false;
    }
    *out = negative ? -v : v;
    return true;
}

/* ---- the run ---- */

typedef struct {
    Extract  *x;
    const u8 *rom;              /* the image, its first 768 KB checked */
    u8        top[CODES];       /* each code's upper tile */
    u8        bot[CODES];       /* and lower */
    Cp        decode[CODES];    /* its character; 0: none */
    int       characters;
} Jp;

/* A JSON value's truth, as Python's: null, false, 0, "", [] and {} are
 * false. */
static bool truthy(const JsonValue *v)
{
    if (v == NULL) {
        return false;
    }
    switch (v->type) {
    case JSON_NULL:
        return false;
    case JSON_BOOL:
        return v->boolean;
    case JSON_NUMBER:
        return v->number != 0;
    case JSON_STRING:
        return v->string[0] != '\0';
    case JSON_ARRAY:
        return v->child != NULL;
    case JSON_OBJECT:
        return v->child != NULL;
    }
    return false;
}

/* obj[key], which must be there and of the type (as the Python's
 * obj[key] must, or it stops). */
static const JsonValue *need(Extract *x, const JsonValue *obj,
                             const char *key, JsonType type,
                             const char *where)
{
    const JsonValue *v = json_get(obj, key);
    if (v == NULL) {
        ex_fail(x, "%s: no \"%s\"", where, key);
    }
    if (v->type != type) {
        ex_fail(x, "%s: \"%s\" is not %s", where, key,
                (type == JSON_STRING) ? "text"
                : (type == JSON_NUMBER) ? "a number"
                : (type == JSON_ARRAY) ? "a list" : "an object");
    }
    return v;
}

/* A text file read as Python's read_text: it must be UTF-8. */
static const char *read_text(Extract *x, const char *path)
{
    size_t size = 0;
    u8 *raw = ex_read_file(x, path, &size);
    if (raw == NULL) {
        ex_fail(x, "%s: there is no such file", path);
    }
    if (!utf8_valid(raw, size) || strlen((const char *)raw) != size) {
        ex_fail(x, "%s: not UTF-8 text", path);
    }
    return (const char *)raw;
}

static const JsonValue *read_json(Extract *x, const char *path)
{
    return ex_parse_json(x, read_text(x, path), path);
}

/* (build 192) One of the retranslation's two files: from its folder, or
 * the bytes its zip held (ExJapanese's dialogue and script, which
 * make_assets.c unpacks as tools/make_assets.py does into a folder of
 * its own). `*path` names it in messages. */
static const JsonValue *read_work(Extract *x, const char *repo,
                                  const char *file, const u8 *bytes,
                                  size_t size, const char **path)
{
    if (bytes == NULL) {
        *path = ex_path(x, repo, file);
        return read_json(x, *path);
    }
    *path = file;
    if (!utf8_valid(bytes, size) || memchr(bytes, 0, size) != NULL) {
        ex_fail(x, "%s: not UTF-8 text", file);
    }
    return ex_parse_json(x, ex_strndup(x, (const char *)bytes, size), file);
}

/* The Japanese cartridge's image, or the one image in a zip: its first
 * 768 KB must be CRC32 BEC8EB5A (a 1 MB overdump is fine). */
static const u8 *read_rom(Extract *x, const char *path)
{
    size_t size = 0;
    const u8 *data = ex_read_file(x, path, &size);
    if (data == NULL) {
        ex_fail(x, "%s: there is no such file", path);
    }
    ExZip z;
    if (ex_zip_open(x, data, size, &z)) {
        static const char *const SUFFIXES[] = { ".bin", ".md", ".gen", ".smd" };
        int found = -1;
        int count = 0;
        ExStr names;
        str_new(x, &names);
        for (int i = 0; i < z.count; i++) {
            const char *lower = ex_lower(x, z.entries[i].name);
            for (size_t k = 0; k < 4; k++) {
                if (ex_ends(lower, SUFFIXES[k])) {
                    ex_str_addf(&names, "%s'%s'", (count > 0) ? ", " : "",
                                z.entries[i].name);
                    found = i;
                    count++;
                    break;
                }
            }
        }
        if (count != 1) {
            ex_fail(x, "%s: not one ROM image in the zip ([%s])", path,
                    names.buf);
        }
        data = ex_zip_read(x, &z, found, &size);
    }
    size_t n = (size < JP_SIZE) ? size : JP_SIZE;
    u32 crc = ex_crc32(data, n);
    if (n < JP_SIZE || crc != JP_CRC) {
        ex_fail(x, "%s: its first 768 KB are CRC32 %08lX, not the Japanese "
                   "cartridge's %08lX", path, (unsigned long)crc,
                (unsigned long)JP_CRC);
    }
    return data;
}

/* The n-th letter of FROM's in TO, or 0. */
static Cp formed(const char *from, const char *to, Cp c)
{
    const char *f = from;
    const char *t = to;
    while (*f != '\0' && *t != '\0') {
        Cp a = utf8_next(&f);
        Cp b = utf8_next(&t);
        if (a == c) {
            return b;
        }
    }
    return 0;
}

/* The code the cartridge has for a character, or -1. */
static int code_of(const Jp *j, Cp c)
{
    for (int code = 0; code < CODES; code++) {
        if (j->decode[code] == c) {
            return code;
        }
    }
    return -1;
}

/* Each code's (upper tile, lower tile), and the characters: a code whose
 * tiles are a letter (the first code for each). */
static void charmap(Jp *j)
{
    Cp tiles[256];
    memset(tiles, 0, sizeof tiles);
    for (size_t r = 0; r < sizeof TILE_ROWS / sizeof TILE_ROWS[0]; r++) {
        const char *p = TILE_ROWS[r].chars;
        int t = TILE_ROWS[r].start;
        while (*p != '\0') {
            Cp c = utf8_next(&p);
            if (c != '_') {
                tiles[t] = c;
            }
            t++;
        }
    }
    for (int code = 0; code < CODES; code++) {
        u8 top = j->rom[JP_CHARMAP + 2 * (u32)code];
        u8 bot = j->rom[JP_CHARMAP + 2 * (u32)code + 1];
        j->top[code] = top;
        j->bot[code] = bot;
        Cp ch = tiles[bot];
        if (ch == 0) {
            continue;
        }
        if (top == DAKUTEN) {
            ch = formed(VOICED_FROM, VOICED_TO, ch);
        } else if (top == HANDAKUTEN) {
            ch = formed(HALF_FROM, HALF_TO, ch);
        } else if (top != BLANK) {
            ch = 0;
        }
        if (ch != 0 && code_of(j, ch) < 0) {
            j->decode[code] = ch;
            j->characters++;
        }
    }
}

/* A tile's ink (its colour 1) as 1, everything else 0; a blank or a tile
 * past the sheet none. */
static void glyph(const ExTiles *sheet, int t, u8 *px)
{
    if (t == BLANK || t >= sheet->count) {
        return;
    }
    const u8 *tile = sheet->data + (size_t)t * EX_TILE_BYTES;
    for (int i = 0; i < EX_TILE_BYTES; i++) {
        px[2 * i]     = ((tile[i] >> 4) == 1) ? 1 : 0;
        px[2 * i + 1] = ((tile[i] & 0x0F) == 1) ? 1 : 0;
    }
}

/* font.png and font_upper.png: a tile a code, the cartridge's lower and
 * upper tiles. */
static void write_face(Jp *j, const char *out)
{
    Extract *x = j->x;
    ExTiles sheet = ex_art(x, j->rom + JP_FONT, JP_SIZE - JP_FONT);
    if (sheet.count != FONT_TILES) {
        ex_fail(x, "FontsIconsArt at %#x is %d tiles, not %d",
                (unsigned)JP_FONT, sheet.count, FONT_TILES);
    }
    u8 *lower = ex_alloc(x, 256 * 64);
    u8 *upper = ex_alloc(x, 256 * 64);
    for (int code = 0; code < CODES; code++) {
        glyph(&sheet, j->bot[code], lower + (size_t)code * 64);
        glyph(&sheet, j->top[code], upper + (size_t)code * 64);
    }
    ExRgb rgb;
    memset(&rgb, 0, sizeof rgb);
    ex_rgb_add(&rgb, 0, 0, 0);
    ex_rgb_add(&rgb, 238, 238, 238);
    ex_png_tiles(x, ex_path(x, out, "font.png"), lower, 256, &rgb,
                 SHEET_TILES, false, NULL, NULL);
    ex_png_tiles(x, ex_path(x, out, "font_upper.png"), upper, 256, &rgb,
                 SHEET_TILES, false, NULL, NULL);
}

/* A byte of the name board's art: row k, column c (the Python's
 * art[k][c], which stops past its rows or the image). */
static u8 board_art(Jp *j, u32 ptr, int width, int lines, int k, int c)
{
    unsigned long long at = (unsigned long long)ptr
                          + (unsigned long long)k * (unsigned)width
                          + (unsigned)c;
    if (k >= lines || at >= JP_SIZE) {
        ex_fail(j->x, "the name board's art (window $24) runs past its "
                      "rows or the image");
    }
    return j->rom[at];
}

/* The name board's six rows of kana as its art has them (rows 5-15, two
 * apart, the marks above each), a cell a character, a space where a cell
 * holds no letter. */
static JOut *name_board(Jp *j)
{
    Extract *x = j->x;
    const u8 *e = j->rom + JP_WINDOWS + 8 * (WIN_NAME_INPUT - 1);
    u32 ptr = ((u32)e[2] << 24) | ((u32)e[3] << 16) | ((u32)e[4] << 8)
            | e[5];
    int width = e[6] - 1;
    int lines = e[7] + 1;
    JOut *rows = jo_arr(x);
    for (int k = 5; k < 16; k += 2) {
        ExStr row;
        str_new(x, &row);
        for (int c = 0; c < width; c++) {
            u8 top = board_art(j, ptr, width, lines, k - 1, c);
            u8 bot = board_art(j, ptr, width, lines, k, c);
            /* the last code with these tiles, as the Python's dict keeps */
            int code = -1;
            for (int d = CODES - 1; d >= 0; d--) {
                if (j->decode[d] != 0 && j->top[d] == top
                    && j->bot[d] == bot) {
                    code = d;
                    break;
                }
            }
            if (code >= 0) {
                utf8_put(&row, j->decode[code]);
            } else {
                ex_str_addc(&row, ' ');
            }
        }
        jo_add(rows, jo_str(x, row.buf));
    }
    return rows;
}

/* His notation to the port's, the characters kept: each must be one the
 * cartridge has a code for. */
static char *to_port(Jp *j, const char *text, const char *where)
{
    Extract *x = j->x;
    ExStr out;
    str_new(x, &out);
    const char *p = text;
    while (*p != '\0') {
        if (*p == '{') {
            const char *close = strchr(p, '}');
            if (close == NULL) {
                ex_fail(x, "%s: a '{' with no '}' after it", where);
            }
            const char *tok = p + 1;
            size_t n = (size_t)(close - tok);
            const char *port = NULL;
            for (size_t i = 0; i < sizeof TOKENS / sizeof TOKENS[0]; i++) {
                if (strlen(TOKENS[i].token) == n
                    && memcmp(TOKENS[i].token, tok, n) == 0) {
                    port = TOKENS[i].port;
                    break;
                }
            }
            bool hex = n == 2;
            for (size_t i = 0; i < n && hex; i++) {
                hex = (tok[i] >= '0' && tok[i] <= '9')
                   || (tok[i] >= 'A' && tok[i] <= 'F');
            }
            if (port != NULL) {
                ex_str_add(&out, port);
            } else if (hex) {
                ex_str_addf(&out, "{%.2s}", tok);
            } else {
                ex_fail(x, "%s: no token {%.*s}", where, (int)n, tok);
            }
            p = close + 1;
            continue;
        }
        const char *at = p;
        Cp c = utf8_next(&p);
        if (code_of(j, c) < 0) {
            ex_fail(x, "%s: %s has no code on the cartridge", where,
                    char_repr(x, c));
        }
        ex_str_addn(&out, at, (size_t)(p - at));
    }
    return out.buf;
}

/* The Japanese the port's id shows: its own `jp`; "" for the second half
 * of a pair the US split (`jp_cont`: the head's runs on into it, and on
 * into the next, as his notes say the chain still works); 0D05 ended
 * where the garbage starts. */
static const char *entry_jp(Extract *x, const JsonValue *e, const char *sid)
{
    if (truthy(json_get(e, "jp_cont"))) {
        return "";
    }
    const JsonValue *v = json_get(e, "jp");
    const char *jp = "";
    if (v != NULL) {
        if (v->type != JSON_STRING) {
            ex_fail(x, "%s: its \"jp\" is not text", sid);
        }
        jp = v->string;
    }
    const JsonValue *id = json_get(e, "id");
    if (id == NULL) {
        ex_fail(x, "%s: its entry has no \"id\"", sid);
    }
    if (id->type == JSON_STRING && strcmp(id->string, "loc_1B6EC") == 0) {
        const char *garbage = strstr(jp, GARBAGE_0D05);
        if (garbage != NULL) {
            size_t n = rstrip_len(jp, (size_t)(garbage - jp));
            if (n >= 6 && memcmp(jp + n - 6, "{PAGE}", 6) == 0) {
                n -= 6;
            }
            return ex_fmt(x, "%.*s{END}", (int)n, jp);
        }
    }
    return jp;
}

/* ---- tools/script_ends.py ---- */

typedef struct {
    long  n;
    ExStr text;              /* its lines joined with "\n" */
    int   lines;
} UsEntry;

typedef struct {
    UsEntry *e;
    int      count;
} UsEntries;

/* "[n]": n, or -1 when the line is not one. */
static long entry_number(const char *line, size_t len)
{
    if (len < 3 || line[0] != '[' || line[len - 1] != ']') {
        return -1;
    }
    const char *p = line + 1;
    const char *end = line + len - 1;
    long v = 0;
    while (p < end) {
        int d = py_digit(utf8_next(&p));
        if (d < 0) {
            return -1;
        }
        v = v * 10 + d;
    }
    return (p == end) ? v : -1;
}

/* read_entries: a script file in the port's notation, {n: text} -- each
 * entry's lines between its `[n]` line and the next, joined, the line
 * ends off both of its ends; sorted by n. */
static UsEntries read_entries(Extract *x, const char *path)
{
    const char *text = read_text(x, path);
    const char *p = text;
    const char *end = text + strlen(text);
    int cap = 64;
    UsEntries us = { ex_alloc(x, sizeof(UsEntry) * (size_t)cap), 0 };
    int cur = -1;
    const char *line;
    size_t len;
    while (next_line(&p, end, &line, &len)) {
        if (len > 0 && line[0] == '#') {
            continue;
        }
        long n = entry_number(line, len);
        if (n >= 0) {
            cur = -1;
            for (int i = 0; i < us.count; i++) {
                if (us.e[i].n == n) {
                    cur = i;
                }
            }
            if (cur < 0) {
                if (us.count == cap) {
                    UsEntry *grown = ex_alloc(x, sizeof(UsEntry)
                                                 * (size_t)cap * 2);
                    memcpy(grown, us.e, sizeof(UsEntry) * (size_t)cap);
                    us.e = grown;
                    cap *= 2;
                }
                cur = us.count++;
                us.e[cur].n = n;
            }
            /* a number seen again starts its lines again, as the
             * Python's out[cur] = [] */
            str_new(x, &us.e[cur].text);
            us.e[cur].lines = 0;
            continue;
        }
        if (cur >= 0) {
            if (us.e[cur].lines > 0) {
                ex_str_addc(&us.e[cur].text, '\n');
            }
            ex_str_addn(&us.e[cur].text, line, len);
            us.e[cur].lines++;
        }
    }
    for (int i = 0; i < us.count; i++) {
        /* .strip("\n") */
        char *s = us.e[i].text.buf;
        size_t n = strlen(s);
        size_t a = 0;
        while (a < n && s[a] == '\n') {
            a++;
        }
        while (n > a && s[n - 1] == '\n') {
            n--;
        }
        memmove(s, s + a, n - a);
        s[n - a] = '\0';
        us.e[i].text.len = n - a;
    }
    /* sorted(us): by the number */
    for (int i = 1; i < us.count; i++) {
        UsEntry e = us.e[i];
        int k = i;
        while (k > 0 && us.e[k - 1].n > e.n) {
            us.e[k] = us.e[k - 1];
            k--;
        }
        us.e[k] = e;
    }
    return us;
}

/* The first "{end:XX}" (XX upper-case hex) in s, or NULL. */
static const char *find_end(const char *s)
{
    for (const char *p = strstr(s, "{end:"); p != NULL;
         p = strstr(p + 1, "{end:")) {
        bool hex = true;
        for (int i = 5; i < 7 && hex; i++) {
            hex = (p[i] >= '0' && p[i] <= '9') || (p[i] >= 'A' && p[i] <= 'F');
        }
        if (hex && p[7] == '}') {
            return p;
        }
    }
    return NULL;
}

/* end_where_the_us_ends: texts[1..count] of bank `bank` changed in
 * place -- for each US entry that holds an end code, the language's text
 * that covers it (its own entry, or the last one before it with text,
 * inside the same US message) ends there too, with the US entry's end
 * code in place of a page break it ends with (RUN_ON left as it is). Each
 * entry changed is said. */
static void end_where_the_us_ends(Extract *x, char **texts, long count,
                                  const UsEntries *us, long bank)
{
    long start = 1;
    for (int i = 0; i < us->count; i++) {
        const char *m = find_end(us->e[i].text.buf);
        if (m == NULL) {
            continue;
        }
        long n = us->e[i].n;
        long k = n;
        while (k > start && (k < 1 || k > count || texts[k][0] == '\0')) {
            k--;
        }
        const char *text = (k >= 1 && k <= count) ? texts[k] : "";
        if (text[0] != '\0' && find_end(text) == NULL
            && ((bank << 8) | k) != RUN_ON) {
            size_t len = strlen(text);
            while (len > 0 && text[len - 1] == '\n') {
                len--;
            }
            if (len >= 7 && memcmp(text + len - 7, "{pause}", 7) == 0) {
                len -= 7;
            }
            texts[k] = ex_fmt(x, "%.*s%.8s", (int)len, text, m);
            ex_say(x, "%02lX%02lX: ended where the US message ends",
                   (unsigned long)bank, (unsigned long)k);
        }
        start = n + 1;
    }
}

/* ---- the files ---- */

typedef struct {
    const char      *sid;
    const JsonValue *entry;
} ById;

typedef struct {
    const char      *name;
    const JsonValue *bank;
    long             number;
} Bank;

static int write_scripts(Jp *j, const ExJapanese *a, const char *out,
                         const char *assets)
{
    Extract *x = j->x;
    const char *dpath = NULL;
    const JsonValue *d = read_work(x, a->retranslation, "work/dialogue.json",
                                   a->dialogue, a->dialogue_size, &dpath);
    const JsonValue *entries = need(x, d, "entries", JSON_ARRAY, dpath);
    int nby = 0;
    for (const JsonValue *e = entries->child; e != NULL; e = e->next) {
        nby += json_length(need(x, e, "ids", JSON_ARRAY, dpath));
    }
    ById *by_id = ex_alloc(x, sizeof *by_id * (size_t)(nby + 1));
    nby = 0;
    for (const JsonValue *e = entries->child; e != NULL; e = e->next) {
        const JsonValue *ids = json_get(e, "ids");
        for (const JsonValue *s = ids->child; s != NULL; s = s->next) {
            if (s->type == JSON_STRING) {
                by_id[nby].sid = s->string;
                by_id[nby].entry = e;
                nby++;
            }
        }
    }

    const char *script_dir = ex_path(x, assets, "script");
    const char *mpath = ex_path(x, script_dir, "script.json");
    const JsonValue *manifest = read_json(x, mpath);
    if (manifest->type != JSON_OBJECT) {
        ex_fail(x, "%s: not an object", mpath);
    }
    const char *out_dir = ex_path(x, out, "script");
    ex_mkdirs(x, out_dir);

    /* sorted by the bank, the manifest's order kept among equals */
    int nbanks = json_length(manifest);
    Bank *banks = ex_alloc(x, sizeof *banks * (size_t)(nbanks + 1));
    int nb = 0;
    for (const JsonValue *b = manifest->child; b != NULL; b = b->next) {
        banks[nb].name = b->key;
        banks[nb].bank = b;
        banks[nb].number = (long)need(x, b, "bank", JSON_NUMBER,
                                      mpath)->number;
        nb++;
    }
    for (int i = 1; i < nb; i++) {
        Bank b = banks[i];
        int k = i;
        while (k > 0 && banks[k - 1].number > b.number) {
            banks[k] = banks[k - 1];
            k--;
        }
        banks[k] = b;
    }

    int total = 0;
    for (int i = 0; i < nb; i++) {
        long bank = banks[i].number;
        const JsonValue *b = banks[i].bank;
        long count = (long)need(x, b, "count", JSON_NUMBER, mpath)->number;
        const char *file = need(x, b, "file", JSON_STRING, mpath)->string;
        ExStr text;
        str_new(x, &text);
        ex_str_addf(&text, "# %s, bank 0x%02lx; script_id = 0x%02lxNN, NN "
                           "1-based\n# The Japanese cartridge's "
                           "(tools/import_japanese.py; docs/languages.md)\n",
                    banks[i].name, (unsigned long)bank, (unsigned long)bank);
        char **texts = ex_alloc(x, sizeof *texts * (size_t)(count + 1));
        for (long n = 1; n <= count; n++) {
            const char *sid = ex_fmt(x, "%02lX%02lX", (unsigned long)bank,
                                     (unsigned long)n);
            const JsonValue *e = NULL;
            for (int k = nby - 1; k >= 0 && e == NULL; k--) {
                if (strcmp(by_id[k].sid, sid) == 0) {
                    e = by_id[k].entry;
                }
            }
            if (e == NULL || truthy(json_get(e, "added"))) {
                ex_fail(x, "%s: not in dialogue.json", sid);
            }
            texts[n] = to_port(j, entry_jp(x, e, sid), sid);
        }
        /* (build 168f) Each message ends where the US one does: 1101 and
         * 190B ran on into the next message, which the event code then
         * shows again. */
        UsEntries us = read_entries(x, ex_path(x, script_dir, file));
        end_where_the_us_ends(x, texts, count, &us, bank);
        for (long n = 1; n <= count; n++) {
            ex_str_addf(&text, "\n[%ld]\n%s\n", n, texts[n]);
            total++;
        }
        ex_write_text(x, ex_path(x, out_dir, file), text.buf);
    }
    ex_write_text(x, ex_path(x, out_dir, "script.json"),
                  ex_fmt(x, "%s\n", jo_dump(x, jo_from(x, manifest), 2,
                                            false)));
    return total;
}

/* A label of a window's row: its number placeholder ("たいりょく     0")
 * gone -- the game writes the number there itself: re.sub(r"\s+\d+$",
 * "", text).rstrip(). */
static char *clean_label(Extract *x, const char *s, size_t n)
{
    /* the code points and where each starts */
    size_t cap = n + 1;
    Cp *cp = ex_alloc(x, sizeof *cp * cap);
    size_t *at = ex_alloc(x, sizeof *at * (cap + 1));
    size_t m = 0;
    const char *p = s;
    while (p < s + n) {
        at[m] = (size_t)(p - s);
        cp[m++] = utf8_next(&p);
    }
    at[m] = n;
    /* '$' is the end, or before a line end that ends the text */
    size_t e = (m > 0 && cp[m - 1] == '\n') ? m - 1 : m;
    size_t d = e;
    while (d > 0 && py_digit(cp[d - 1]) >= 0) {
        d--;
    }
    size_t w = d;
    while (w > 0 && py_space(cp[w - 1])) {
        w--;
    }
    ExStr out;
    str_new(x, &out);
    if (d < e && w < d) {
        ex_str_addn(&out, s, at[w]);
        ex_str_addn(&out, s + at[e], n - at[e]);
    } else {
        ex_str_addn(&out, s, n);
    }
    out.buf[rstrip_len(out.buf, out.len)] = '\0';
    return out.buf;
}

static const char *name_text(Jp *j, const char *text, const char *where)
{
    const char *p = text;
    while (*p != '\0') {
        Cp c = utf8_next(&p);
        if (code_of(j, c) < 0) {
            ex_fail(j->x, "%s: %s has no code on the cartridge", where,
                    char_repr(j->x, c));
        }
    }
    return text;
}

/* A run's `jp` ("" for none), {C2} as the ヴ it stands for. */
static const char *run_jp(Extract *x, const JsonValue *r)
{
    const JsonValue *v = json_get(r, "jp");
    if (!truthy(v)) {
        return "";
    }
    if (v->type != JSON_STRING) {
        ex_fail(x, "a run's \"jp\" is not text");
    }
    ExStr out;
    str_new(x, &out);
    const char *p = v->string;
    const char *hit;
    while ((hit = strstr(p, "{C2}")) != NULL) {
        ex_str_addn(&out, p, (size_t)(hit - p));
        ex_str_add(&out, "ヴ");
        p = hit + 4;
    }
    ex_str_add(&out, p);
    return out.buf;
}

static const char *run_id(Extract *x, const JsonValue *r)
{
    return need(x, r, "id", JSON_STRING, "a run")->string;
}

/* int(r["id"].split("#")[1]) */
static long run_number(Extract *x, const JsonValue *r)
{
    const char *id = run_id(x, r);
    const char *hash = strchr(id, '#');
    if (hash == NULL) {
        ex_fail(x, "%s: no '#' in the run's id", id);
    }
    const char *from = hash + 1;
    const char *next = strchr(from, '#');
    size_t n = (next != NULL) ? (size_t)(next - from) : strlen(from);
    long v;
    if (!py_int(from, n, &v)) {
        ex_fail(x, "%s: not a number after its '#'", id);
    }
    return v;
}

static const JsonValue *runs(Extract *x, const JsonValue *script,
                             const char *seg, const char *path)
{
    const JsonValue *segments = need(x, script, "segments", JSON_OBJECT,
                                     path);
    const JsonValue *s = need(x, segments, seg, JSON_OBJECT, path);
    return need(x, s, "runs", JSON_ARRAY, path);
}

/* {id: field} of a list of objects, the last of an id winning: the field
 * for `id`, or NULL. */
static const char *keyed(Extract *x, const JsonValue *list, long id,
                         const char *field, const char *path)
{
    const char *found = NULL;
    for (const JsonValue *it = list->child; it != NULL; it = it->next) {
        const JsonValue *v = need(x, it, "id", JSON_NUMBER, path);
        if (v->number == (double)id) {
            found = need(x, it, field, JSON_STRING, path)->string;
        }
    }
    return found;
}

typedef struct {
    int items;
    int techniques;
    int enemies;
    int windows;
} NameCounts;

static JOut *write_names(Jp *j, const ExJapanese *a, const char *assets,
                         NameCounts *counts)
{
    Extract *x = j->x;
    const char *spath = NULL;
    const JsonValue *script = read_work(x, a->retranslation,
                                        "work/script.json", a->script,
                                        a->script_size, &spath);
    const char *ipath = ex_path(x, assets, "data/items.json");
    const char *tpath = ex_path(x, assets, "data/techniques.json");
    const char *bpath = ex_path(x, assets, "data/battle.json");
    const JsonValue *items = read_json(x, ipath);
    if (items->type != JSON_ARRAY) {
        ex_fail(x, "%s: not a list", ipath);
    }
    const JsonValue *techs = need(x, read_json(x, tpath), "techniques",
                                  JSON_ARRAY, tpath);
    const JsonValue *enemies = need(x, read_json(x, bpath), "enemies",
                                    JSON_ARRAY, bpath);

    JOut *names = jo_obj(x);
    jo_sets(x, names, "source", "work/script.json (jp)");

    JOut *chars = jo_arr(x);
    for (const JsonValue *r = runs(x, script, "charnames", spath)->child;
         r != NULL; r = r->next) {
        jo_add(chars, jo_str(x, name_text(j, run_jp(x, r), run_id(x, r))));
    }
    jo_set(x, names, "characters", chars);

    static const struct {
        const char *key;
        const char *seg;
        int         field;       /* 0 items, 1 techniques, 2 enemies */
    } KEYED[] = {
        { "items",      "items",   0 },
        { "techniques", "techs",   1 },
        { "enemies",    "enemies", 2 },
    };
    for (size_t k = 0; k < 3; k++) {
        JOut *obj = jo_obj(x);
        for (const JsonValue *r = runs(x, script, KEYED[k].seg, spath)->child;
             r != NULL; r = r->next) {
            long n = run_number(x, r);
            const char *key;
            if (KEYED[k].field == 0) {
                key = keyed(x, items, n, "key", ipath);
            } else if (KEYED[k].field == 1) {
                key = keyed(x, techs, n, "key", tpath);
            } else {
                key = keyed(x, enemies, n, "label", bpath);
            }
            /* the items and techniques leave out their number 0 */
            if (key == NULL || (KEYED[k].field != 2 && n == 0)
                || run_jp(x, r)[0] == '\0') {
                continue;
            }
            jo_set(x, obj, key,
                   jo_str(x, name_text(j, run_jp(x, r), run_id(x, r))));
        }
        jo_set(x, names, KEYED[k].key, obj);
    }
    counts->items      = jo_len(jo_get(names, "items"));
    counts->techniques = jo_len(jo_get(names, "techniques"));
    counts->enemies    = jo_len(jo_get(names, "enemies"));

    static const char *const LISTS[][2] = {
        { "songs",   "soundtracks" },
        { "towns",   "teleport" },
        { "jobs",    "jobs" },
        { "prompts", "prompts" },
        { "labels",  "labels" },
    };
    for (size_t k = 0; k < sizeof LISTS / sizeof LISTS[0]; k++) {
        JOut *arr = jo_arr(x);
        for (const JsonValue *r = runs(x, script, LISTS[k][1], spath)->child;
             r != NULL; r = r->next) {
            jo_add(arr, jo_str(x, name_text(j, run_jp(x, r), run_id(x, r))));
        }
        jo_set(x, names, LISTS[k][0], arr);
    }

    JOut *windows = jo_obj(x);
    for (const JsonValue *r = runs(x, script, "windows", spath)->child;
         r != NULL; r = r->next) {
        const char *text = run_jp(x, r);
        const char *label = need(x, r, "label", JSON_STRING, spath)->string;
        JOut *rows = jo_arr(x);
        bool first = true;
        const char *p = text;
        for (;;) {
            const char *br = strstr(p, "{BR}");
            size_t n = (br != NULL) ? (size_t)(br - p) : strlen(p);
            char *row = clean_label(x, p, n);
            /* The library's list has a title row the US window has not
             * got. */
            bool title = first && strcmp(label, "WinArt_LibraryOptions") == 0
                      && strcmp(row, "リスト") == 0;
            if (!title) {
                jo_add(rows, jo_str(x, name_text(j, row, run_id(x, r))));
            }
            first = false;
            if (br == NULL) {
                break;
            }
            p = br + 4;
        }
        jo_set(x, windows, label, rows);
    }
    jo_set(x, names, "windows", windows);
    counts->windows = jo_len(windows);
    return names;
}

/* The checkout's commit, as git would name it from its .git folder: ""
 * for a copy that is not a checkout. */
static char *read_stripped(Extract *x, const char *path)
{
    size_t size = 0;
    u8 *raw = ex_read_file(x, path, &size);
    if (raw == NULL) {
        return NULL;
    }
    if (!utf8_valid(raw, size)) {
        ex_fail(x, "%s: not UTF-8 text", path);
    }
    return py_strip(x, (const char *)raw);
}

static const char *commit_of(Extract *x, const char *repo)
{
    const char *git = ex_path(x, repo, ".git");
    char *head = read_stripped(x, ex_path(x, git, "HEAD"));
    if (head == NULL) {
        return "";
    }
    if (!ex_starts(head, "ref: ")) {
        return head;
    }
    const char *ref = head + 5;
    char *commit = read_stripped(x, ex_path(x, git, ref));
    if (commit != NULL) {
        return commit;
    }
    size_t size = 0;
    u8 *packed = ex_read_file(x, ex_path(x, git, "packed-refs"), &size);
    if (packed == NULL) {
        return "";
    }
    if (!utf8_valid(packed, size)) {
        ex_fail(x, "%s: not UTF-8 text", ex_path(x, git, "packed-refs"));
    }
    const char *wanted = ex_fmt(x, " %s", ref);
    size_t wn = strlen(wanted);
    const char *p = (const char *)packed;
    const char *end = p + size;
    const char *line;
    size_t len;
    while (next_line(&p, end, &line, &len)) {
        if (len >= wn && memcmp(line + len - wn, wanted, wn) == 0) {
            /* line.split()[0] */
            const char *s = line;
            const char *e = line + len;
            while (s < e) {
                const char *at = s;
                if (!py_space(utf8_next(&s))) {
                    s = at;
                    break;
                }
            }
            const char *t = s;
            while (t < e) {
                const char *at = t;
                if (py_space(utf8_next(&t))) {
                    t = at;
                    break;
                }
            }
            return ex_strndup(x, s, (size_t)(t - s));
        }
    }
    return "";
}

static void run_japanese(Extract *x, const void *arg)
{
    const ExJapanese *a = arg;
    const char *assets = (a->assets != NULL && a->assets[0] != '\0')
                       ? a->assets : "assets";
    const char *out = (a->out != NULL && a->out[0] != '\0')
                    ? a->out : ex_path(x, assets, "lang/ja");

    Jp j;
    memset(&j, 0, sizeof j);
    j.x = x;
    j.rom = read_rom(x, a->image);
    charmap(&j);
    ex_mkdirs(x, out);
    write_face(&j, out);
    int total = write_scripts(&j, a, out, assets);
    NameCounts counts;
    JOut *names = write_names(&j, a, assets, &counts);
    jo_set(x, names, "board", name_board(&j));
    ex_write_text(x, ex_path(x, out, "names.json"),
                  ex_fmt(x, "%s\n", jo_dump_unicode(x, names, 2, false)));

    const char *commit = (a->retranslation != NULL)
                       ? commit_of(x, a->retranslation) : "";
    if (commit[0] == '\0' && a->commit != NULL) {
        commit = a->commit;
    }
    JOut *lang = jo_obj(x);
    jo_sets(x, lang, "name", "JAPANESE");
    jo_sets(x, lang, "about",
            "the Japanese cartridge's text in its own font: the script and "
            "tables from Excalibur_Z's retranslation files' `jp`, the font "
            "and character map from the cartridge (docs/languages.md)");
    jo_sets(x, lang, "source",
            ex_fmt(x, "ps2-retranslation %s; the Japanese cartridge, CRC32 "
                      "BEC8EB5A", commit));
    jo_setb(x, lang, "proportional", true);
    jo_setb(x, lang, "tall", true);
    jo_sets(x, lang, "font", "font.png");
    jo_sets(x, lang, "font_upper", "font_upper.png");
    JOut *advance = jo_arr(x);
    for (int i = 0; i < 256; i++) {
        jo_add(advance, jo_int(x, 8));
    }
    jo_set(x, lang, "advance", advance);
    /* Every character, ASCII too, is the cartridge's code. */
    jo_setb(x, lang, "charset_full", true);
    JOut *charset = jo_obj(x);
    for (int code = 0; code < CODES; code++) {
        if (j.decode[code] != 0) {
            ExStr ch;
            str_new(x, &ch);
            utf8_put(&ch, j.decode[code]);
            jo_seti(x, charset, ch.buf, code);
        }
    }
    jo_set(x, lang, "charset", charset);
    ex_write_text(x, ex_path(x, out, "lang.json"),
                  ex_fmt(x, "%s\n", jo_dump_unicode(x, lang, 1, false)));
    ex_write_text(x, ex_path(x, out, "NOTICE.txt"), NOTICE);

    ex_say(x, "%d script ids; %d characters with codes; names: %d items, "
              "%d techniques, %d enemies, %d windows", total, j.characters,
           counts.items, counts.techniques, counts.enemies, counts.windows);
}

bool ex_japanese(Extract *x, const ExJapanese *a)
{
    return ex_call(x, run_japanese, a);
}
