/* (build 187) What every extractor step uses: the arena, the stop, the
 * messages, text and files (extract.h). Standard C; making a folder is
 * the one call no standard has, _mkdir on Windows and POSIX's mkdir
 * elsewhere. */

#define _POSIX_C_SOURCE 200809L

#include "extract.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* dirent.h is POSIX's; MinGW has it too, as autoplay.c uses it */
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define make_one_dir(path) _mkdir(path)
#else
#define make_one_dir(path) mkdir(path, 0777)
#endif

/* ---- the arena ---- */

#define BLOCK_BYTES 65536

struct ExBlock {
    ExBlock *next;
    size_t   used;
    size_t   cap;
    /* the bytes follow, aligned for anything */
    max_align_t data[];
};

void *ex_alloc(Extract *x, size_t n)
{
    size_t align = sizeof(max_align_t);
    n = (n + align - 1) / align * align;
    if (n == 0) {
        n = align;
    }
    ExBlock *b = x->arena;
    if (b == NULL || b->cap - b->used < n) {
        size_t cap = (n > BLOCK_BYTES) ? n : BLOCK_BYTES;
        ExBlock *nb = malloc(sizeof *nb + cap);
        if (nb == NULL) {
            ex_fail(x, "out of memory (%zu bytes)", n);
        }
        nb->used = 0;
        nb->cap  = cap;
        if (b != NULL && n > BLOCK_BYTES) {
            /* a big one behind the current block, which keeps its room */
            nb->next = b->next;
            b->next  = nb;
        } else {
            nb->next = b;
            x->arena = nb;
        }
        b = nb;
    }
    void *p = (u8 *)b->data + b->used;
    b->used += n;
    memset(p, 0, n);
    return p;
}

static void free_blocks(ExBlock **head)
{
    ExBlock *b = *head;
    while (b != NULL) {
        ExBlock *next = b->next;
        free(b);
        b = next;
    }
    *head = NULL;
}

static void free_parsed(Extract *x)
{
    for (int i = 0; i < x->nparsed; i++) {
        json_free(x->parsed[i]);
    }
    free(x->parsed);
    x->parsed    = NULL;
    x->nparsed   = 0;
    x->capparsed = 0;
}

static void free_arena(Extract *x)
{
    free_blocks(&x->arena);
    free_parsed(x);
}

char *ex_strndup(Extract *x, const char *s, size_t n)
{
    char *p = ex_alloc(x, n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *ex_strdup(Extract *x, const char *s)
{
    return ex_strndup(x, s, strlen(s));
}

static char *vfmt(Extract *x, const char *fmt, va_list ap)
{
    va_list again;
    va_copy(again, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    if (n < 0) {
        va_end(again);
        ex_fail(x, "a message that cannot be written (%s)", fmt);
    }
    char *p = ex_alloc(x, (size_t)n + 1);
    vsnprintf(p, (size_t)n + 1, fmt, again);
    va_end(again);
    return p;
}

char *ex_fmt(Extract *x, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *p = vfmt(x, fmt, ap);
    va_end(ap);
    return p;
}

char *ex_lower(Extract *x, const char *s)
{
    char *p = ex_strdup(x, s);
    for (char *c = p; *c != '\0'; c++) {
        if (*c >= 'A' && *c <= 'Z') {
            *c = (char)(*c - 'A' + 'a');
        }
    }
    return p;
}

bool ex_starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool ex_ends(const char *s, const char *suffix)
{
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

long long ex_floordiv(long long a, long long b)
{
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        q--;
    }
    return q;
}

long long ex_mod(long long a, long long b)
{
    return a - ex_floordiv(a, b) * b;
}

/* ---- the stop and the messages ---- */

void ex_fail(Extract *x, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(x->error, sizeof x->error, fmt, ap);
    va_end(ap);
    if (x->jump != NULL) {
        longjmp(*x->jump, 1);
    }
    /* outside a step: there is nowhere to go back to */
    fprintf(stderr, "extract: %s\n", x->error);
    abort();
}

static void say_line(Extract *x, const char *line)
{
    if (x->say != NULL) {
        x->say(x->user, line);
    } else {
        printf("%s\n", line);
        fflush(stdout);
    }
}

void ex_say(Extract *x, const char *fmt, ...)
{
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    say_line(x, line);
}

void ex_warn(Extract *x, const char *fmt, ...)
{
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (x->say != NULL) {
        say_line(x, line);
    } else {
        fprintf(stderr, "%s\n", line);
    }
}

/* ---- text ---- */

void ex_str_init(Extract *x, ExStr *s)
{
    s->x   = x;
    s->buf = NULL;
    s->len = 0;
    s->cap = 0;
}

static void str_room(ExStr *s, size_t more)
{
    if (s->len + more + 1 <= s->cap) {
        return;
    }
    size_t cap = (s->cap == 0) ? 256 : s->cap;
    while (cap < s->len + more + 1) {
        cap *= 2;
    }
    char *nb = ex_alloc(s->x, cap);
    if (s->len > 0) {
        memcpy(nb, s->buf, s->len);
    }
    s->buf = nb;
    s->cap = cap;
}

void ex_str_addn(ExStr *s, const char *text, size_t n)
{
    str_room(s, n);
    memcpy(s->buf + s->len, text, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

void ex_str_add(ExStr *s, const char *text)
{
    ex_str_addn(s, text, strlen(text));
}

void ex_str_addc(ExStr *s, char c)
{
    ex_str_addn(s, &c, 1);
}

void ex_str_addf(ExStr *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *p = vfmt(s->x, fmt, ap);
    va_end(ap);
    ex_str_add(s, p);
}

/* ---- files ---- */

char *ex_path(Extract *x, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    if (n == 0) {
        return ex_strdup(x, name);
    }
    bool slash = dir[n - 1] == '/' || dir[n - 1] == '\\';
    return ex_fmt(x, "%s%s%s", dir, slash ? "" : "/", name);
}

void ex_mkdirs(Extract *x, const char *dir)
{
    char *p = ex_strdup(x, dir);
    size_t n = strlen(p);
    for (size_t i = 1; i <= n; i++) {
        if (i < n && p[i] != '/' && p[i] != '\\') {
            continue;
        }
        char keep = p[i];
        p[i] = '\0';
        /* a drive's "C:" is no folder to make */
        bool drive = (i == 2 && p[1] == ':');
        if (!drive && make_one_dir(p) != 0 && errno != EEXIST) {
            ex_fail(x, "%s: cannot make the folder (%s)", p, strerror(errno));
        }
        p[i] = keep;
    }
}

void ex_write(Extract *x, const char *path, const void *data, size_t n)
{
    FILE *fh = fopen(path, "wb");
    if (fh == NULL) {
        ex_fail(x, "%s: cannot write it (%s)", path, strerror(errno));
    }
    size_t put = (n > 0) ? fwrite(data, 1, n, fh) : 0;
    if (fclose(fh) != 0 || put != n) {
        ex_fail(x, "%s: writing it failed", path);
    }
}

void ex_write_text(Extract *x, const char *path, const char *text)
{
    ex_write(x, path, text, strlen(text));
}

void ex_remove(Extract *x, const char *path)
{
    (void)x;
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        remove(path);
    }
}

void ex_remove_ending(Extract *x, const char *dir, const char *suffix)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (ex_ends(e->d_name, suffix)) {
            ex_remove(x, ex_path(x, dir, e->d_name));
        }
    }
    closedir(d);
}

u8 *ex_read_file(Extract *x, const char *path, size_t *size)
{
    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return NULL;
    }
    ExStr s;
    ex_str_init(x, &s);
    char buf[65536];
    size_t got;
    while ((got = fread(buf, 1, sizeof buf, fh)) > 0) {
        ex_str_addn(&s, buf, got);
    }
    bool bad = ferror(fh) != 0;
    fclose(fh);
    if (bad) {
        ex_fail(x, "%s: reading it failed", path);
    }
    if (s.buf == NULL) {
        s.buf = ex_alloc(x, 1);
    }
    if (size != NULL) {
        *size = s.len;
    }
    return (u8 *)s.buf;
}

const JsonValue *ex_read_json(Extract *x, const char *path)
{
    u8 *text = ex_read_file(x, path, NULL);
    if (text == NULL) {
        ex_fail(x, "%s: there is no such file (an earlier step writes it)",
                path);
    }
    size_t where = 0;
    JsonValue *v = json_parse((const char *)text, &where);
    if (v == NULL) {
        ex_fail(x, "%s: not JSON (near byte %zu)", path, where);
    }
    if (x->nparsed == x->capparsed) {
        int cap = (x->capparsed == 0) ? 8 : x->capparsed * 2;
        JsonValue **np = realloc(x->parsed, sizeof *np * (size_t)cap);
        if (np == NULL) {
            json_free(v);
            ex_fail(x, "out of memory");
        }
        x->parsed    = np;
        x->capparsed = cap;
    }
    x->parsed[x->nparsed++] = v;
    return v;
}

/* ---- a run ---- */

const ExStep *ex_step(const char *name)
{
    for (int i = 0; i < EX_STEP_COUNT; i++) {
        if (strcmp(EX_STEPS[i].name, name) == 0) {
            return &EX_STEPS[i];
        }
    }
    return NULL;
}

bool ex_run(Extract *x, const ExStep *step, const ExArgs *a)
{
    jmp_buf here;
    x->error[0] = '\0';
    x->jump = &here;
    if (setjmp(here) != 0) {
        x->jump = NULL;
        free_arena(x);
        return false;
    }
    ExArgs args = *a;
    if (args.out == NULL) {
        args.out = step->default_out;
    }
    step->run(x, &args);
    x->jump = NULL;
    free_arena(x);
    return true;
}

/* exmap.c: the map read into x->arena, the image checked against it. */
void ex_map_load(Extract *x, const char *map_path, const u8 *rom,
                 size_t size);

bool ex_open(Extract *x, const char *map_path, const u8 *rom, size_t size)
{
    memset(&x->map, 0, sizeof x->map);
    x->arena = NULL;
    jmp_buf here;
    x->error[0] = '\0';
    x->jump = &here;
    if (setjmp(here) != 0) {
        x->jump = NULL;
        free_arena(x);
        memset(&x->map, 0, sizeof x->map);
        return false;
    }
    ex_map_load(x, map_path, rom, size);
    x->jump = NULL;
    /* the map's memory is its own from here; the parsed file goes */
    x->map.arena = x->arena;
    x->arena = NULL;
    free_parsed(x);
    return true;
}

void ex_close(Extract *x)
{
    free_arena(x);
    free_blocks(&x->map.arena);
    memset(&x->map, 0, sizeof x->map);
}
