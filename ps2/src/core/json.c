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
                case 'u': {
                    /* Save files are ASCII; anything else becomes '?'. */
                    for (int i = 0; i < 4 && p->text[p->pos + 1] != '\0'; i++) {
                        p->pos++;
                    }
                    ch = '?';
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
            if (p->failed) {
                break;
            }
        }
        if (len + 2 > cap) {
            cap *= 2;
            char *grown = realloc(out, cap);
            if (grown == NULL) {
                free(out);
                out = NULL;
                break;
            }
            out = grown;
        }
        out[len++] = ch;
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
