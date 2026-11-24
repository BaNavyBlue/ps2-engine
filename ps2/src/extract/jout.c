/* (build 187) JSON out, written as Python's json.dumps writes it, so a
 * file made here is the Python extractor's byte for byte: with an indent,
 * "," and ": " between, each member on a line of its own at the indent of
 * its depth, "[]" and "{}" for empty ones; with none, ", " and ": ".
 * Strings as ensure_ascii writes them: \" \\ \b \f \n \r \t, any other
 * byte below a space and anything past '~' as \u and four small hex
 * digits (a pair of surrogates past U+FFFF). A dict keeps the order its
 * keys were first set in, and sort_keys sorts by the key's code points,
 * which for UTF-8 is strcmp's order. Integers only: no extractor writes a
 * float. */

#include "extract.h"

#include <stdlib.h>
#include <string.h>

typedef enum {
    JO_NULL, JO_BOOL, JO_INT, JO_STR, JO_ARR, JO_OBJ
} JoType;

struct JOut {
    JoType      type;
    long long   num;
    bool        boolean;
    const char *str;
    const char *key;         /* a member's */
    JOut       *first;
    JOut       *last;
    JOut       *next;
    int         count;
};

static JOut *make(Extract *x, JoType t)
{
    JOut *v = ex_alloc(x, sizeof *v);
    v->type = t;
    return v;
}

JOut *jo_obj(Extract *x)
{
    return make(x, JO_OBJ);
}

JOut *jo_arr(Extract *x)
{
    return make(x, JO_ARR);
}

JOut *jo_int(Extract *x, long long v)
{
    JOut *j = make(x, JO_INT);
    j->num = v;
    return j;
}

JOut *jo_str(Extract *x, const char *s)
{
    JOut *j = make(x, JO_STR);
    j->str = ex_strdup(x, s);
    return j;
}

JOut *jo_bool(Extract *x, bool b)
{
    JOut *j = make(x, JO_BOOL);
    j->boolean = b;
    return j;
}

JOut *jo_null(Extract *x)
{
    return make(x, JO_NULL);
}

JOut *jo_from(Extract *x, const JsonValue *v)
{
    if (v == NULL) {
        return jo_null(x);
    }
    switch (v->type) {
    case JSON_NULL:
        return jo_null(x);
    case JSON_BOOL:
        return jo_bool(x, v->boolean);
    case JSON_NUMBER: {
        long long n = (long long)v->number;
        if ((double)n != v->number) {
            ex_fail(x, "a number %g that is not an integer", v->number);
        }
        return jo_int(x, n);
    }
    case JSON_STRING:
        return jo_str(x, v->string);
    case JSON_ARRAY: {
        JOut *arr = jo_arr(x);
        for (const JsonValue *c = v->child; c != NULL; c = c->next) {
            jo_add(arr, jo_from(x, c));
        }
        return arr;
    }
    case JSON_OBJECT: {
        JOut *obj = jo_obj(x);
        for (const JsonValue *c = v->child; c != NULL; c = c->next) {
            jo_set(x, obj, c->key, jo_from(x, c));
        }
        return obj;
    }
    default:
        return jo_null(x);
    }
}

static void append(JOut *parent, JOut *v)
{
    v->next = NULL;
    if (parent->last == NULL) {
        parent->first = v;
    } else {
        parent->last->next = v;
    }
    parent->last = v;
    parent->count++;
}

void jo_add(JOut *arr, JOut *v)
{
    append(arr, v);
}

JOut *jo_get(const JOut *obj, const char *key)
{
    for (JOut *m = obj->first; m != NULL; m = m->next) {
        if (strcmp(m->key, key) == 0) {
            return m;
        }
    }
    return NULL;
}

void jo_set(Extract *x, JOut *obj, const char *key, JOut *v)
{
    for (JOut **at = &obj->first; *at != NULL; at = &(*at)->next) {
        if (strcmp((*at)->key, key) == 0) {
            /* the key keeps its place; the value is the new one */
            v->key  = (*at)->key;
            v->next = (*at)->next;
            if (obj->last == *at) {
                obj->last = v;
            }
            *at = v;
            return;
        }
    }
    v->key = ex_strdup(x, key);
    append(obj, v);
}

int jo_len(const JOut *v)
{
    return v->count;
}

void jo_seti(Extract *x, JOut *obj, const char *key, long long v)
{
    jo_set(x, obj, key, jo_int(x, v));
}

void jo_sets(Extract *x, JOut *obj, const char *key, const char *s)
{
    jo_set(x, obj, key, jo_str(x, s));
}

void jo_setb(Extract *x, JOut *obj, const char *key, bool b)
{
    jo_set(x, obj, key, jo_bool(x, b));
}

/* ---- writing ---- */

/* The next code point of UTF-8 text, or U+FFFD for a byte that starts
 * none. */
static unsigned long next_code(const unsigned char **p)
{
    const unsigned char *s = *p;
    unsigned long c = s[0];
    int more = 0;
    if (c < 0x80) {
        *p = s + 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        c &= 0x1F;
        more = 1;
    } else if ((c & 0xF0) == 0xE0) {
        c &= 0x0F;
        more = 2;
    } else if ((c & 0xF8) == 0xF0) {
        c &= 0x07;
        more = 3;
    } else {
        *p = s + 1;
        return 0xFFFD;
    }
    for (int i = 1; i <= more; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *p = s + i;
            return 0xFFFD;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    *p = s + 1 + more;
    return c;
}

static void quote(ExStr *o, const char *s)
{
    ex_str_addc(o, '"');
    const unsigned char *p = (const unsigned char *)s;
    while (*p != '\0') {
        unsigned long c = next_code(&p);
        if (c == '"') {
            ex_str_add(o, "\\\"");
        } else if (c == '\\') {
            ex_str_add(o, "\\\\");
        } else if (c == '\n') {
            ex_str_add(o, "\\n");
        } else if (c == '\r') {
            ex_str_add(o, "\\r");
        } else if (c == '\t') {
            ex_str_add(o, "\\t");
        } else if (c == '\b') {
            ex_str_add(o, "\\b");
        } else if (c == '\f') {
            ex_str_add(o, "\\f");
        } else if (c >= 0x20 && c <= 0x7E) {
            ex_str_addc(o, (char)c);
        } else if (c < 0x10000) {
            ex_str_addf(o, "\\u%04lx", c);
        } else {
            unsigned long n = c - 0x10000;
            ex_str_addf(o, "\\u%04lx\\u%04lx", 0xD800 | ((n >> 10) & 0x3FF),
                        0xDC00 | (n & 0x3FF));
        }
    }
    ex_str_addc(o, '"');
}

static int cmp_member(const void *a, const void *b)
{
    const JOut *p = *(const JOut *const *)a;
    const JOut *q = *(const JOut *const *)b;
    return strcmp(p->key, q->key);
}

static void newline(ExStr *o, int indent, int depth)
{
    ex_str_addc(o, '\n');
    for (int i = 0; i < indent * depth; i++) {
        ex_str_addc(o, ' ');
    }
}

static void dump(Extract *x, ExStr *o, const JOut *v, int indent,
                 bool sort_keys, int depth)
{
    switch (v->type) {
    case JO_NULL:
        ex_str_add(o, "null");
        return;
    case JO_BOOL:
        ex_str_add(o, v->boolean ? "true" : "false");
        return;
    case JO_INT:
        ex_str_addf(o, "%lld", v->num);
        return;
    case JO_STR:
        quote(o, v->str);
        return;
    case JO_ARR:
        break;
    case JO_OBJ:
        break;
    }
    bool obj = v->type == JO_OBJ;
    if (v->count == 0) {
        ex_str_add(o, obj ? "{}" : "[]");
        return;
    }
    const JOut **items = ex_alloc(x, sizeof *items * (size_t)v->count);
    int n = 0;
    for (const JOut *m = v->first; m != NULL; m = m->next) {
        items[n++] = m;
    }
    if (obj && sort_keys) {
        qsort(items, (size_t)n, sizeof *items, cmp_member);
    }
    ex_str_addc(o, obj ? '{' : '[');
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            ex_str_add(o, (indent >= 0) ? "," : ", ");
        }
        if (indent >= 0) {
            newline(o, indent, depth + 1);
        }
        if (obj) {
            quote(o, items[i]->key);
            ex_str_add(o, ": ");
        }
        dump(x, o, items[i], indent, sort_keys, depth + 1);
    }
    if (indent >= 0) {
        newline(o, indent, depth);
    }
    ex_str_addc(o, obj ? '}' : ']');
}

char *jo_dump(Extract *x, const JOut *v, int indent, bool sort_keys)
{
    ExStr o;
    ex_str_init(x, &o);
    dump(x, &o, v, indent, sort_keys, 0);
    return o.buf;
}

char *jo_quote(Extract *x, const char *s)
{
    ExStr o;
    ex_str_init(x, &o);
    quote(&o, s);
    return o.buf;
}
