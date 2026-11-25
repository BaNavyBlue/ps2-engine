#include "json.h"

#include "assetfs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text;
    size_t      pos;
    bool        failed;
} Parser;

static JsonValue *parse_value(Parser *p);

static void skip_space(Parser *p)
{
    while (isspace((unsigned char)p->text[p->pos])) {
        p->pos++;
    }
}

static JsonValue *new_value(JsonType type)
{
    JsonValue *v = calloc(1, sizeof *v);
    if (v != NULL) {
        v->type = type;
    }
    return v;
}

/* Four hex digits at s, or -1 (a NUL among them stops the reading). */
static long hex4(const char *s)
{
    long v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            d = c - 'A' + 10;
        } else {
            return -1;
        }
        v = v * 16 + d;
    }
    return v;
}

/* One byte more of the string being built; false when out of memory (the
 * string is then freed). */
static bool put_byte(char **out, size_t *len, size_t *cap, char ch)
{
    if (*len + 2 > *cap) {
        size_t grown_cap = *cap * 2;
        char *grown = realloc(*out, grown_cap);
        if (grown == NULL) {
            free(*out);
            *out = NULL;
            return false;
        }
        *out = grown;
        *cap = grown_cap;
    }
    (*out)[(*len)++] = ch;
    return true;
}

/* (build 191) A \u escape at p->pos (on the 'u'), and a second one after
 * it when the two are a surrogate pair, as UTF-8 -- as Python's json
 * reads them (the Japanese importer, src/extract/ex_japanese.c, reads
 * another project's files). Until 191 each was a '?': save files are
 * ASCII, and nothing in the tree is escaped so. A lone surrogate is
 * U+FFFD; U+0000 ends the string where it stands. */
static bool put_escape_u(Parser *p, char **out, size_t *len, size_t *cap)
{
    long c = hex4(&p->text[p->pos + 1]);
    if (c < 0) {
        p->failed = true;
        return false;
    }
    p->pos += 4;
    if (c >= 0xD800 && c <= 0xDBFF && p->text[p->pos + 1] == '\\'
        && p->text[p->pos + 2] == 'u') {
        long low = hex4(&p->text[p->pos + 3]);
        if (low >= 0xDC00 && low <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
            p->pos += 6;
        }
    }
    if (c >= 0xD800 && c <= 0xDFFF) {
        c = 0xFFFD;
    }
    char bytes[4];
    int n;
    if (c < 0x80) {
        bytes[0] = (char)c;
        n = 1;
    } else if (c < 0x800) {
        bytes[0] = (char)(0xC0 | (c >> 6));
        bytes[1] = (char)(0x80 | (c & 0x3F));
        n = 2;
    } else if (c < 0x10000) {
        bytes[0] = (char)(0xE0 | (c >> 12));
        bytes[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (c & 0x3F));
        n = 3;
    } else {
        bytes[0] = (char)(0xF0 | (c >> 18));
        bytes[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        bytes[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        bytes[3] = (char)(0x80 | (c & 0x3F));
        n = 4;
    }
    for (int i = 0; i < n; i++) {
        if (!put_byte(out, len, cap, bytes[i])) {
            return false;
        }
    }
    return true;
}

static char *parse_string_raw(Parser *p)
{
    if (p->text[p->pos] != '"') {
        p->failed = true;
        return NULL;
    }
    p->pos++;

    size_t cap = 16, len = 0;
    char  *out = malloc(cap);

    while (out != NULL && p->text[p->pos] != '"') {
        char ch = p->text[p->pos];
        if (ch == '\0') {
            p->failed = true;
            break;
        }
        if (ch == '\\') {
            p->pos++;
            ch = p->text[p->pos];
            switch (ch) {
                case 'n': {
                    ch = '\n';
                    break;
                }
                case 't': {
                    ch = '\t';
                    break;
                }
                case 'r': {
                    ch = '\r';
                    break;
                }
                case 'b': {
                    ch = '\b';
                    break;
                }
                case 'f': {
                    ch = '\f';
                    break;
                }
                case 'u': {
                    if (put_escape_u(p, &out, &len, &cap)) {
                        p->pos++;
                        continue;
                    }
                    break;
                }
                case '\0': {
                    p->failed = true;
                    break;
                }
                default: {
                    break;      /* \" \\ \/ stand for themselves */
                }
            }
            if (p->failed || out == NULL) {
                break;
            }
        }
        if (!put_byte(&out, &len, &cap, ch)) {
            break;
        }
        p->pos++;
    }

    if (out == NULL || p->failed) {
        free(out);
        p->failed = true;
        return NULL;
    }
    out[len] = '\0';
    p->pos++;
    return out;
}

static JsonValue *parse_container(Parser *p, bool is_object)
{
    JsonValue *v    = new_value(is_object ? JSON_OBJECT : JSON_ARRAY);
    JsonValue *tail = NULL;
    char       close = is_object ? '}' : ']';

    p->pos++;
    skip_space(p);
    if (p->text[p->pos] == close) {
        p->pos++;
        return v;
    }

    while (!p->failed) {
        char *key = NULL;

        skip_space(p);
        if (is_object) {
            key = parse_string_raw(p);
            skip_space(p);
            if (p->failed || p->text[p->pos] != ':') {
                free(key);
                p->failed = true;
                break;
            }
            p->pos++;
        }

        JsonValue *item = parse_value(p);
        if (item == NULL) {
            free(key);
            p->failed = true;
            break;
        }
        item->key = key;

        if (tail == NULL) {
            v->child = item;
        } else {
            tail->next = item;
        }
        tail = item;

        skip_space(p);
        if (p->text[p->pos] == ',') {
            p->pos++;
        } else if (p->text[p->pos] == close) {
            p->pos++;
            return v;
        } else {
            p->failed = true;
        }
    }

    json_free(v);
    return NULL;
}

static JsonValue *parse_value(Parser *p)
{
    skip_space(p);
    char ch = p->text[p->pos];

    if (ch == '{') {
        return parse_container(p, true);
    }
    if (ch == '[') {
        return parse_container(p, false);
    }
    if (ch == '"') {
        char *s = parse_string_raw(p);
        if (s == NULL) {
            return NULL;
        }
        JsonValue *v = new_value(JSON_STRING);
        v->string = s;
        return v;
    }
    if (strncmp(&p->text[p->pos], "true", 4) == 0) {
        p->pos += 4;
        JsonValue *v = new_value(JSON_BOOL);
        v->boolean = true;
        return v;
    }
    if (strncmp(&p->text[p->pos], "false", 5) == 0) {
        p->pos += 5;
        return new_value(JSON_BOOL);
    }
    if (strncmp(&p->text[p->pos], "null", 4) == 0) {
        p->pos += 4;
        return new_value(JSON_NULL);
    }
    if (ch == '-' || isdigit((unsigned char)ch)) {
        char  *end = NULL;
        double n   = strtod(&p->text[p->pos], &end);
        p->pos = (size_t)(end - p->text);
        JsonValue *v = new_value(JSON_NUMBER);
        v->number = n;
        return v;
    }

    p->failed = true;
    return NULL;
}

JsonValue *json_parse(const char *text, size_t *error_offset)
{
    Parser p = { text, 0, false };
    JsonValue *v = parse_value(&p);

    skip_space(&p);
    if (v != NULL && (p.failed || text[p.pos] != '\0')) {
        json_free(v);
        v = NULL;
    }
    if (v == NULL && error_offset != NULL) {
        *error_offset = p.pos;
    }
    return v;
}

static JsonValue *parse_reporting(const char *text, const char *path)
{
    size_t where = 0;
    JsonValue *v = json_parse(text, &where);
    if (v == NULL) {
        fprintf(stderr, "json: %s: syntax error near byte %zu\n", path, where);
    }
    return v;
}

/* Assets come out of the RAM file store. Anything else -- which means
 * save files -- is read from disk every time, because it changes. */
JsonValue *json_parse_file(const char *path)
{
    if (strncmp(path, "assets/", 7) == 0) {
        const u8 *text = assetfs_get(path, NULL);
        JsonValue *v = (text != NULL)
                     ? parse_reporting((const char *)text, path) : NULL;
        /* (build 157d) A mod it refuses is set aside and the game's own
         * read in its place (docs/mods.md, "When a mod is refused"). */
        if (v == NULL && assetfs_mod_refused(path)) {
            text = assetfs_get(path, NULL);
            v = (text != NULL) ? parse_reporting((const char *)text, path)
                               : NULL;
        }
        return v;
    }

    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return NULL;
    }

    fseek(fh, 0, SEEK_END);
    long size = ftell(fh);
    fseek(fh, 0, SEEK_SET);

    JsonValue *v = NULL;
    char *text = (size >= 0) ? malloc((size_t)size + 1) : NULL;
    if (text != NULL && fread(text, 1, (size_t)size, fh) == (size_t)size) {
        text[size] = '\0';
        v = parse_reporting(text, path);
    }
    free(text);
    fclose(fh);
    return v;
}

void json_free(JsonValue *v)
{
    while (v != NULL) {
        JsonValue *next = v->next;
        json_free(v->child);
        free(v->string);
        free(v->key);
        free(v);
        v = next;
    }
}

const JsonValue *json_get(const JsonValue *obj, const char *key)
{
    if (obj == NULL || obj->type != JSON_OBJECT) {
        return NULL;
    }
    for (const JsonValue *m = obj->child; m != NULL; m = m->next) {
        if (m->key != NULL && strcmp(m->key, key) == 0) {
            return m;
        }
    }
    return NULL;
}

const JsonValue *json_at(const JsonValue *arr, int index)
{
    if (arr == NULL || arr->type != JSON_ARRAY) {
        return NULL;
    }
    const JsonValue *m = arr->child;
    while (m != NULL && index > 0) {
        m = m->next;
        index--;
    }
    return m;
}

int json_length(const JsonValue *arr)
{
    int n = 0;
    if (arr != NULL && (arr->type == JSON_ARRAY || arr->type == JSON_OBJECT)) {
        for (const JsonValue *m = arr->child; m != NULL; m = m->next) {
            n++;
        }
    }
    return n;
}

long json_int(const JsonValue *obj, const char *key, long fallback)
{
    const JsonValue *v = json_get(obj, key);
    return (v != NULL && v->type == JSON_NUMBER) ? (long)v->number : fallback;
}

bool json_bool(const JsonValue *obj, const char *key, bool fallback)
{
    const JsonValue *v = json_get(obj, key);
    return (v != NULL && v->type == JSON_BOOL) ? v->boolean : fallback;
}

const char *json_str(const JsonValue *obj, const char *key,
                     const char *fallback)
{
    const JsonValue *v = json_get(obj, key);
    return (v != NULL && v->type == JSON_STRING) ? v->string : fallback;
}
