/* (build 171) The player's little things: lists, sets of cells, files, the
 * harness error. See player.h. */

#include "pl.h"

#include "../core/assetfs.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

jmp_buf *pl_trap;
char     pl_error_text[512];

_Noreturn void pl_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(pl_error_text, sizeof pl_error_text, fmt, ap);
    va_end(ap);
    if (pl_trap == NULL) {
        fprintf(stderr, "player: %s\n", pl_error_text);
        exit(3);
    }
    longjmp(*pl_trap, 1);
}

void *pl_alloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (p == NULL) {
        fprintf(stderr, "player: out of memory\n");
        exit(3);
    }
    return p;
}

void *pl_grow(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (q == NULL) {
        fprintf(stderr, "player: out of memory\n");
        exit(3);
    }
    return q;
}

char *pl_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = pl_alloc(n);
    memcpy(d, s, n);
    return d;
}

/* ---- Lines ---- */

void lines_add(Lines *l, const char *text)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = pl_grow(l->v, (size_t)l->cap * sizeof *l->v);
    }
    l->v[l->n++] = pl_strdup(text);
}

void lines_addf(Lines *l, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    lines_add(l, buf);
}

void lines_extend(Lines *l, const Lines *more)
{
    for (int i = 0; i < more->n; i++) {
        lines_add(l, more->v[i]);
    }
}

void lines_clear(Lines *l)
{
    for (int i = 0; i < l->n; i++) {
        free(l->v[i]);
    }
    l->n = 0;
}

void lines_free(Lines *l)
{
    lines_clear(l);
    free(l->v);
    l->v = NULL;
    l->cap = 0;
}

bool lines_any(const Lines *l, const char *needle)
{
    for (int i = 0; i < l->n; i++) {
        if (strstr(l->v[i], needle) != NULL) {
            return true;
        }
    }
    return false;
}

bool lines_any2(const Lines *l, const char *a, const char *b)
{
    for (int i = 0; i < l->n; i++) {
        if (strstr(l->v[i], a) != NULL && strstr(l->v[i], b) != NULL) {
            return true;
        }
    }
    return false;
}

/* ---- Ints ---- */

void ints_add(Ints *l, int v)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = pl_grow(l->v, (size_t)l->cap * sizeof *l->v);
    }
    l->v[l->n++] = v;
}

void ints_free(Ints *l)
{
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

bool ints_has(const Ints *l, int v)
{
    for (int i = 0; i < l->n; i++) {
        if (l->v[i] == v) {
            return true;
        }
    }
    return false;
}

/* ---- cells ---- */

int cell_cmp(const void *a, const void *b)
{
    const Cell *p = a, *q = b;
    if (p->x != q->x) {
        return (p->x < q->x) ? -1 : 1;
    }
    if (p->y != q->y) {
        return (p->y < q->y) ? -1 : 1;
    }
    return 0;
}

static u32 cset_hash(u64 k)
{
    k ^= k >> 33;
    k *= 0xFF51AFD7ED558CCDull;
    k ^= k >> 33;
    k *= 0xC4CEB9FE1A85EC53ull;
    k ^= k >> 33;
    return (u32)k;
}

void cset_clear(CSet *s)
{
    s->n = 0;
    if (s->slot != NULL) {
        memset(s->slot, 0, (size_t)s->slots * sizeof *s->slot);
    }
}

void cset_free(CSet *s)
{
    free(s->key);
    free(s->val);
    free(s->slot);
    memset(s, 0, sizeof *s);
}

static void cset_rehash(CSet *s, int slots)
{
    free(s->slot);
    s->slots = slots;
    s->slot = pl_alloc((size_t)slots * sizeof *s->slot);
    for (int i = 0; i < s->n; i++) {
        u32 h = cset_hash(s->key[i]) & (u32)(slots - 1);
        while (s->slot[h] != 0) {
            h = (h + 1) & (u32)(slots - 1);
        }
        s->slot[h] = i + 1;
    }
}

int cset_find(const CSet *s, int x, int y)
{
    return kset_find(s, cell_key(x, y));
}

int kset_find(const CSet *s, u64 k)
{
    if (s->n == 0 || s->slots == 0) {
        return -1;
    }
    u32 h = cset_hash(k) & (u32)(s->slots - 1);
    while (s->slot[h] != 0) {
        int i = s->slot[h] - 1;
        if (s->key[i] == k) {
            return i;
        }
        h = (h + 1) & (u32)(s->slots - 1);
    }
    return -1;
}

bool cset_has(const CSet *s, int x, int y)
{
    return cset_find(s, x, y) >= 0;
}

int cset_get(const CSet *s, int x, int y, int fallback)
{
    int i = cset_find(s, x, y);
    return (i >= 0) ? s->val[i] : fallback;
}

bool cset_add(CSet *s, int x, int y, int val)
{
    return kset_add(s, cell_key(x, y), val);
}

bool kset_add(CSet *s, u64 k, int val)
{
    if (kset_find(s, k) >= 0) {
        return false;
    }
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->key = pl_grow(s->key, (size_t)s->cap * sizeof *s->key);
        s->val = pl_grow(s->val, (size_t)s->cap * sizeof *s->val);
    }
    s->key[s->n] = k;
    s->val[s->n] = val;
    s->n++;
    if (s->n * 2 > s->slots) {
        cset_rehash(s, s->slots ? s->slots * 2 : 64);
    } else {
        u32 h = cset_hash(s->key[s->n - 1]) & (u32)(s->slots - 1);
        while (s->slot[h] != 0) {
            h = (h + 1) & (u32)(s->slots - 1);
        }
        s->slot[h] = s->n;
    }
    return true;
}

void cset_put(CSet *s, int x, int y, int val)
{
    int i = cset_find(s, x, y);
    if (i >= 0) {
        s->val[i] = val;
    } else {
        cset_add(s, x, y, val);
    }
}

Cell cset_cell(const CSet *s, int i)
{
    return key_cell((u32)s->key[i]);
}

void cset_copy(CSet *dst, const CSet *src)
{
    cset_clear(dst);
    for (int i = 0; i < src->n; i++) {
        Cell c = cset_cell(src, i);
        cset_add(dst, c.x, c.y, src->val[i]);
    }
}

void cset_union(CSet *dst, const CSet *add)
{
    for (int i = 0; i < add->n; i++) {
        Cell c = cset_cell(add, i);
        cset_add(dst, c.x, c.y, add->val[i]);
    }
}

void cset_sorted(const CSet *s, Cell *out)
{
    for (int i = 0; i < s->n; i++) {
        out[i] = cset_cell(s, i);
    }
    qsort(out, (size_t)s->n, sizeof *out, cell_cmp);
}

void cells_repr(const Cell *c, int n, char *out, size_t cap)
{
    size_t used = (size_t)snprintf(out, cap, "[");
    for (int i = 0; i < n && used < cap; i++) {
        used += (size_t)snprintf(out + used, cap - used, "%s(%d, %d)",
                                 i ? ", " : "", c[i].x, c[i].y);
    }
    if (used < cap) {
        snprintf(out + used, cap - used, "]");
    }
}

void cset_repr(const CSet *s, int k, char *out, size_t cap)
{
    Cell *c = pl_alloc((size_t)(s->n + 1) * sizeof *c);
    cset_sorted(s, c);
    cells_repr(c, (s->n < k) ? s->n : k, out, cap);
    free(c);
}

const char *xy_repr(int x, int y, char *out, size_t cap)
{
    char a[16], b[16];
    snprintf(a, sizeof a, "%d", x);
    snprintf(b, sizeof b, "%d", y);
    snprintf(out, cap, "(%s, %s)", (x == PL_NONE) ? "None" : a,
             (y == PL_NONE) ? "None" : b);
    return out;
}

void cells_add(Cells *l, int x, int y)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = pl_grow(l->v, (size_t)l->cap * sizeof *l->v);
    }
    l->v[l->n].x = x;
    l->v[l->n].y = y;
    l->n++;
}

void cells_free(Cells *l)
{
    free(l->v);
    memset(l, 0, sizeof *l);
}

void intslist_add(IntsList *l, const Ints *v)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = pl_grow(l->v, (size_t)l->cap * sizeof *l->v);
    }
    Ints c = { 0 };
    for (int i = 0; i < v->n; i++) {
        ints_add(&c, v->v[i]);
    }
    l->v[l->n++] = c;
}

void intslist_free(IntsList *l)
{
    for (int i = 0; i < l->n; i++) {
        ints_free(&l->v[i]);
    }
    free(l->v);
    memset(l, 0, sizeof *l);
}

/* ---- files ---- */

char *pl_read_file(const char *path, size_t *size)
{
    size_t n = 0;
    const u8 *packed = assetfs_pack_find(path, &n);
    if (packed != NULL) {
        char *t = pl_alloc(n + 1);
        memcpy(t, packed, n);
        t[n] = '\0';
        if (size != NULL) {
            *size = n;
        }
        return t;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    size_t cap = 1 << 16, len = 0;
    char *t = pl_alloc(cap + 1);
    for (;;) {
        size_t got = fread(t + len, 1, cap - len, f);
        len += got;
        if (len < cap) {
            break;
        }
        cap *= 2;
        t = pl_grow(t, cap + 1);
    }
    fclose(f);
    t[len] = '\0';
    if (size != NULL) {
        *size = len;
    }
    return t;
}

bool pl_file_exists(const char *path)
{
    struct stat sb;
    return stat(path, &sb) == 0;
}

bool pl_copy_file(const char *from, const char *to)
{
    size_t n = 0;
    char *t = pl_read_file(from, &n);
    if (t == NULL) {
        return false;
    }
    FILE *f = fopen(to, "wb");
    bool ok = f != NULL && fwrite(t, 1, n, f) == n;
    if (f != NULL && fclose(f) != 0) {
        ok = false;
    }
    free(t);
    return ok;
}

bool pl_write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    size_t n = strlen(text);
    bool ok = fwrite(text, 1, n, f) == n;
    return (fclose(f) == 0) && ok;
}

bool pl_make_dirs(const char *path)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; ; p++) {
        if (*p == '/' || *p == '\0') {
            char was = *p;
            *p = '\0';
#ifdef _WIN32
            int r = _mkdir(buf);
#else
            int r = mkdir(buf, 0777);
#endif
            if (r != 0 && errno != EEXIST) {
                return false;
            }
            *p = was;
            if (was == '\0') {
                break;
            }
        }
    }
    return true;
}
