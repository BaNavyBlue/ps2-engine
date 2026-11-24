/* (build 187) The cartridge image read by the disassembly's names, as
 * tools/rommap.py reads it: tools/rom_map.json's labels (ps2.asm's, in its
 * line order), the drum samples' labels, the binclude'd files and the
 * constants. A name is matched exactly, then without regard to case, as
 * AS matches symbols, and as rommap.py's dicts do: among labels the first
 * in line order, then the drums; a constant's last in the map's order
 * (rommap.py builds that one with a dict comprehension). */

#include "extract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- sorting and finding ---- */

static int cmp_key(const void *a, const void *b)
{
    const ExKey *p = a;
    const ExKey *q = b;
    int c = strcmp(p->key, q->key);
    if (c != 0) {
        return c;
    }
    return (p->rank > q->rank) - (p->rank < q->rank);
}

static int cmp_at(const void *a, const void *b)
{
    const ExAt *p = a;
    const ExAt *q = b;
    if (p->addr != q->addr) {
        return (p->addr > q->addr) ? 1 : -1;
    }
    return (p->rank > q->rank) - (p->rank < q->rank);
}

/* The first entry whose key is `key` (the lowest rank), or -1. */
static int find_first(const ExKey *index, int n, const char *key)
{
    int lo = 0;
    int hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (strcmp(index[mid].key, key) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return (lo < n && strcmp(index[lo].key, key) == 0) ? lo : -1;
}

/* The last entry whose key is `key` (the highest rank), or -1. */
static int find_last(const ExKey *index, int n, const char *key)
{
    int i = find_first(index, n, key);
    if (i < 0) {
        return -1;
    }
    while (i + 1 < n && strcmp(index[i + 1].key, key) == 0) {
        i++;
    }
    return i;
}

/* ---- loading ---- */

static const JsonValue *member(Extract *x, const JsonValue *obj,
                               const char *key, JsonType type,
                               const char *path)
{
    const JsonValue *v = json_get(obj, key);
    if (v == NULL || v->type != type) {
        ex_fail(x, "%s: no \"%s\" of the kind a ROM map has", path, key);
    }
    return v;
}

static long long whole(Extract *x, const JsonValue *v, const char *path)
{
    if (v == NULL || v->type != JSON_NUMBER || v->number < 0
        || v->number > 4294967295.0 || v->number != (double)(long long)v->number) {
        ex_fail(x, "%s: a number that is no address or constant", path);
    }
    return (long long)v->number;
}

static ExLabel *read_pairs(Extract *x, const JsonValue *arr, int *count,
                           const char *path)
{
    int n = json_length(arr);
    ExLabel *out = ex_alloc(x, sizeof *out * (size_t)(n > 0 ? n : 1));
    int i = 0;
    for (const JsonValue *e = arr->child; e != NULL; e = e->next, i++) {
        const JsonValue *name = json_at(e, 0);
        if (e->type != JSON_ARRAY || name == NULL || name->type != JSON_STRING) {
            ex_fail(x, "%s: a label that is not [name, address]", path);
        }
        out[i].name = ex_strdup(x, name->string);
        out[i].addr = (u32)whole(x, json_at(e, 1), path);
    }
    *count = n;
    return out;
}

static ExKey *index_names(Extract *x, const ExLabel *l, int n)
{
    ExKey *ix = ex_alloc(x, sizeof *ix * (size_t)(n > 0 ? n : 1));
    for (int i = 0; i < n; i++) {
        ix[i].key  = l[i].name;
        ix[i].rank = i;
    }
    qsort(ix, (size_t)n, sizeof *ix, cmp_key);
    return ix;
}

void ex_map_load(Extract *x, const char *map_path, const u8 *rom,
                 size_t size);

void ex_map_load(Extract *x, const char *map_path, const u8 *rom,
                 size_t size)
{
    ExMap *m = &x->map;
    u8 *text = ex_read_file(x, map_path, NULL);
    if (text == NULL) {
        ex_fail(x, "%s: there is no ROM map there", map_path);
    }
    size_t where = 0;
    JsonValue *doc = json_parse((const char *)text, &where);
    if (doc == NULL) {
        ex_fail(x, "%s: not JSON (near byte %zu)", map_path, where);
    }
    /* freed with the step's parsed files (ex_open lets them go) */
    x->parsed    = malloc(sizeof *x->parsed);
    if (x->parsed == NULL) {
        json_free(doc);
        ex_fail(x, "out of memory");
    }
    x->parsed[0] = doc;
    x->nparsed   = 1;
    x->capparsed = 1;

    const JsonValue *want = member(x, doc, "rom", JSON_OBJECT, map_path);
    long long want_size = whole(x, json_get(want, "size"), map_path);
    const char *want_sha1 = json_str(want, "sha1", "");
    u8 sha1[20];
    ex_sha1(rom, size, sha1);
    char hex[41];
    for (int i = 0; i < 20; i++) {
        snprintf(hex + 2 * i, 3, "%02x", sha1[i]);
    }
    if ((long long)size != want_size || strcmp(hex, want_sha1) != 0) {
        ex_fail(x, "the image is not the cartridge image %s is of (%lld "
                   "bytes, SHA-1 %s): Phantasy Star II (USA, Europe) Rev 02, "
                   "raw, with no copier header", map_path, want_size,
                want_sha1);
    }
    m->rom  = rom;
    m->size = size;

    m->labels = read_pairs(x, member(x, doc, "labels", JSON_ARRAY, map_path),
                           &m->nlabels, map_path);
    m->drums  = read_pairs(x, member(x, doc, "drums", JSON_ARRAY, map_path),
                           &m->ndrums, map_path);

    const JsonValue *files = member(x, doc, "files", JSON_OBJECT, map_path);
    m->nfiles = json_length(files);
    m->files  = ex_alloc(x, sizeof *m->files * (size_t)(m->nfiles + 1));
    int i = 0;
    for (const JsonValue *f = files->child; f != NULL; f = f->next, i++) {
        if (f->type != JSON_ARRAY) {
            ex_fail(x, "%s: a file that is not [address, size]", map_path);
        }
        m->files[i].name = ex_strdup(x, f->key);
        m->files[i].addr = (u32)whole(x, json_at(f, 0), map_path);
        m->files[i].size = (u32)whole(x, json_at(f, 1), map_path);
    }

    const JsonValue *consts = member(x, doc, "constants", JSON_OBJECT,
                                     map_path);
    m->nconsts = json_length(consts);
    m->consts  = ex_alloc(x, sizeof *m->consts * (size_t)(m->nconsts + 1));
    i = 0;
    for (const JsonValue *c = consts->child; c != NULL; c = c->next, i++) {
        m->consts[i].name  = ex_strdup(x, c->key);
        m->consts[i].value = whole(x, c, map_path);
    }

    /* the indexes */
    m->by_name      = index_names(x, m->labels, m->nlabels);
    m->drum_by_name = index_names(x, m->drums, m->ndrums);
    int all = m->nlabels + m->ndrums;
    m->by_lower = ex_alloc(x, sizeof *m->by_lower * (size_t)(all + 1));
    for (i = 0; i < m->nlabels; i++) {
        m->by_lower[i].key  = ex_lower(x, m->labels[i].name);
        m->by_lower[i].rank = i;
    }
    for (i = 0; i < m->ndrums; i++) {
        m->by_lower[m->nlabels + i].key  = ex_lower(x, m->drums[i].name);
        m->by_lower[m->nlabels + i].rank = m->nlabels + i;
    }
    qsort(m->by_lower, (size_t)all, sizeof *m->by_lower, cmp_key);
    m->by_addr = ex_alloc(x, sizeof *m->by_addr * (size_t)(m->nlabels + 1));
    for (i = 0; i < m->nlabels; i++) {
        m->by_addr[i].addr = m->labels[i].addr;
        m->by_addr[i].rank = i;
    }
    qsort(m->by_addr, (size_t)m->nlabels, sizeof *m->by_addr, cmp_at);
    m->const_by_name  = ex_alloc(x, sizeof *m->const_by_name
                                    * (size_t)(m->nconsts + 1));
    m->const_by_lower = ex_alloc(x, sizeof *m->const_by_lower
                                    * (size_t)(m->nconsts + 1));
    for (i = 0; i < m->nconsts; i++) {
        m->const_by_name[i].key   = m->consts[i].name;
        m->const_by_name[i].rank  = i;
        m->const_by_lower[i].key  = ex_lower(x, m->consts[i].name);
        m->const_by_lower[i].rank = i;
    }
    qsort(m->const_by_name, (size_t)m->nconsts, sizeof *m->const_by_name,
          cmp_key);
    qsort(m->const_by_lower, (size_t)m->nconsts, sizeof *m->const_by_lower,
          cmp_key);
}

/* ---- names ---- */

const char *ex_name(Extract *x, const char *name)
{
    ExMap *m = &x->map;
    int i = find_first(m->by_name, m->nlabels, name);
    if (i >= 0) {
        return m->labels[m->by_name[i].rank].name;
    }
    i = find_first(m->drum_by_name, m->ndrums, name);
    if (i >= 0) {
        return m->drums[m->drum_by_name[i].rank].name;
    }
    /* any case: the lowest rank, a label before a drum */
    char low[512];
    size_t n = strlen(name);
    if (n >= sizeof low) {
        return NULL;
    }
    for (size_t k = 0; k <= n; k++) {
        char c = name[k];
        low[k] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    i = find_first(m->by_lower, m->nlabels + m->ndrums, low);
    if (i < 0) {
        return NULL;
    }
    int rank = m->by_lower[i].rank;
    return (rank < m->nlabels) ? m->labels[rank].name
                               : m->drums[rank - m->nlabels].name;
}

bool ex_has(Extract *x, const char *name)
{
    return ex_name(x, name) != NULL;
}

/* A found name's first label number, or -1 for a drum's. */
static int label_number(Extract *x, const char *found)
{
    int i = find_first(x->map.by_name, x->map.nlabels, found);
    return (i >= 0) ? x->map.by_name[i].rank : -1;
}

u32 ex_addr(Extract *x, const char *name)
{
    const char *found = ex_name(x, name);
    if (found == NULL) {
        ex_fail(x, "no label %s in the ROM map", name);
    }
    int i = label_number(x, found);
    if (i >= 0) {
        return x->map.labels[i].addr;
    }
    int d = find_first(x->map.drum_by_name, x->map.ndrums, found);
    return x->map.drums[x->map.drum_by_name[d].rank].addr;
}

int ex_index(Extract *x, const char *name)
{
    const char *found = ex_name(x, name);
    int i = (found != NULL) ? label_number(x, found) : -1;
    if (i < 0) {
        ex_fail(x, "no label %s among ps2.asm's", name);
    }
    return i;
}

u32 ex_next_addr(Extract *x, const char *name)
{
    int i = ex_index(x, name);
    if (i + 1 < x->map.nlabels) {
        return x->map.labels[i + 1].addr;
    }
    return (u32)x->map.size;
}

const ExLabel **ex_names_at(Extract *x, u32 address, int *count)
{
    ExMap *m = &x->map;
    int lo = 0;
    int hi = m->nlabels;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (m->by_addr[mid].addr < address) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    int n = 0;
    while (lo + n < m->nlabels && m->by_addr[lo + n].addr == address) {
        n++;
    }
    const ExLabel **out = ex_alloc(x, sizeof *out * (size_t)(n + 1));
    for (int k = 0; k < n; k++) {
        out[k] = &m->labels[m->by_addr[lo + k].rank];
    }
    *count = n;
    return out;
}

/* ---- bytes ---- */

const u8 *ex_read(Extract *x, long long address, long long length)
{
    if (address < 0 || address + length > (long long)x->map.size) {
        ex_fail(x, "read of %lld at %llX is outside the ROM", length,
                (unsigned long long)address);
    }
    return x->map.rom + address;
}

const u8 *ex_blob(Extract *x, const char *name, size_t *length)
{
    long long start = ex_addr(x, name);
    long long end   = ex_next_addr(x, name);
    /* Python's rom[start:end]: nothing when the end is before the start */
    *length = (end > start) ? (size_t)(end - start) : 0;
    if (start > (long long)x->map.size) {
        *length = 0;
        return x->map.rom + x->map.size;
    }
    if (end > (long long)x->map.size) {
        *length = x->map.size - (size_t)start;
    }
    return x->map.rom + start;
}

const u8 *ex_run_of(Extract *x, const char *name, size_t length)
{
    return ex_read(x, ex_addr(x, name), (long long)length);
}

u8 ex_u8(Extract *x, long long address)
{
    if (address < 0 || address >= (long long)x->map.size) {
        ex_fail(x, "index out of range: a byte at %llX",
                (unsigned long long)address);
    }
    return x->map.rom[address];
}

u16 ex_u16(Extract *x, long long address)
{
    const u8 *p = ex_read(x, address, 2);
    return (u16)(p[0] << 8 | p[1]);
}

u32 ex_u32(Extract *x, long long address)
{
    const u8 *p = ex_read(x, address, 4);
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

const u8 *ex_file(Extract *x, const char *name, size_t *size)
{
    char *key = ex_strdup(x, name);
    for (char *c = key; *c != '\0'; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }
    for (int i = 0; i < x->map.nfiles; i++) {
        if (strcmp(x->map.files[i].name, key) == 0) {
            *size = x->map.files[i].size;
            return ex_read(x, x->map.files[i].addr, x->map.files[i].size);
        }
    }
    ex_fail(x, "no file %s in the ROM map", name);
}

/* ---- constants ---- */

static int const_rank(Extract *x, const char *name)
{
    ExMap *m = &x->map;
    int i = find_first(m->const_by_name, m->nconsts, name);
    if (i >= 0) {
        return m->const_by_name[i].rank;
    }
    char low[512];
    size_t n = strlen(name);
    if (n >= sizeof low) {
        return -1;
    }
    for (size_t k = 0; k <= n; k++) {
        char c = name[k];
        low[k] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    i = find_last(m->const_by_lower, m->nconsts, low);
    return (i >= 0) ? m->const_by_lower[i].rank : -1;
}

bool ex_has_const(Extract *x, const char *name)
{
    return const_rank(x, name) >= 0;
}

long long ex_const(Extract *x, const char *name)
{
    int r = const_rank(x, name);
    if (r < 0) {
        ex_fail(x, "no constant %s in the ROM map", name);
    }
    return x->map.consts[r].value;
}
