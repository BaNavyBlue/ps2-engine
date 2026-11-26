/* (build 192) tools/make_assets.py in C (extract.h; docs/first_start.md,
 * "In C"): the game's files from what the player gives, told apart by
 * what each file is, never by its name or its place on the line.
 *
 *   - the US/European cartridge, revision 02 (tools/rom_map.json's size
 *     and SHA-1): a raw image (.bin, .md, .gen), a .smd (interleaved: put
 *     in order), one with a copier's 512-byte header (dropped), or a zip
 *     holding one of those. Every step (EX_STEPS, the Makefile's
 *     EXTRACTORS in its order) runs on it into GAME/assets, then the
 *     preload list;
 *   - the Japanese cartridge (EX_JP_CRC) and Excalibur_Z's retranslation
 *     (a checkout, its GitHub "Download ZIP", or that zip unpacked):
 *     together the JAPANESE language (ex_japanese.c), the zip's two files
 *     read where they lie in it.
 *
 * Then, where GAME/tools/assets.sha256 is, every file is held to it
 * (ex_file_digest). The Python's lines, said the same: a line per file
 * told apart, "[K/N] STEP" as each step starts, the end of a failed
 * step's output, the check's lines, "Done."; its exit statuses
 * (EX_MAKE_*; the Python's 4, no Pillow, has no twin here). Each step's
 * lines go to GAME/make_assets.log, as the Python's tools' output does.
 *
 * Where it is not the Python's, on purpose: the US image is read where it
 * lies, not copied to a temporary folder first; the log names each step
 * (the Python's, each tool's command line); a failed step's output is
 * the step's lines and its reason, where the Python's is the tool's
 * output and a traceback. tools/make_assets_c_check.py holds the two
 * together.
 *
 * Standard C; a folder's names and what a path is come from exfs.c,
 * which opens Windows' files by their wide names (build 193: the game's
 * window hands this the UTF-8 paths SDL gives). */

#include "extract.h"

#include "exzip.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The retranslation's commit assets/lang/ja was made from (its lang.json's
 * "source"): another may carry other text, and the player is told so. */
#define RETRANSLATION_PROVED "061e1f5799d72ee88ac1b1a872215b3a96cc850e"

static const char *const RETRANSLATION_FILES[2] = {
    "work/dialogue.json", "work/script.json"
};

static const char *const IMAGE_SUFFIXES[4] = { ".bin", ".md", ".gen", ".smd" };

/* The Python's tail of a failed step's output, in characters. */
#define TAIL_CHARS 2000

typedef enum {
    KIND_US,
    KIND_JA,
    KIND_RT,
    KINDS
} Kind;

static const char *const KIND_TWICE[KINDS] = {
    "the US cartridge", "the Japanese cartridge", "the retranslation"
};

static const char *const KIND_WHAT[KINDS] = {
    "the US/European cartridge, revision 02", "the Japanese cartridge",
    "Excalibur_Z's retranslation"
};

typedef enum {
    STEP_EXTRACT,
    STEP_JAPANESE,
    STEP_MANIFEST
} StepKind;

typedef struct {
    StepKind      kind;
    const ExStep *step;          /* STEP_EXTRACT's */
    const char   *name;
    const char   *out;           /* GAME/ its -o */
} Step;

struct ExMake {
    Extract     plan;            /* the plan's memory: the files' bytes */
    Extract     run;             /* the steps' (the map, once opened) */
    bool        run_open;
    const char *game;            /* "" for the current folder */
    const char *assets;          /* GAME/assets */
    const char *map;             /* GAME/tools/rom_map.json */
    const char *log_path;        /* GAME/make_assets.log */
    bool        keep_going;
    void      (*say)(void *user, const char *line);
    void       *user;

    long long   rom_size;
    const char *rom_sha1;

    bool        have[KINDS];
    const u8   *image;           /* the US cartridge, in order */
    size_t      image_size;
    const char *ja;              /* the Japanese cartridge, as given */
    const char *rt_dir;          /* the retranslation's folder; or */
    ExZip       rt_zip;          /* its zip (rt_dir NULL) */
    int         rt_entry[2];     /* RETRANSLATION_FILES' entries in it */
    const u8   *dialogue;
    size_t      dialogue_size;
    const u8   *script;
    size_t      script_size;
    const char *commit;          /* the zip's, or NULL */

    Step       *steps;
    int         nsteps;
    FILE       *log;
    ExStr       failed;          /* the failed steps' names, ", " between */
    int         nfailed;

    char       *out;             /* the running step's lines */
    size_t      out_len;
    size_t      out_cap;
};

/* ---- saying ---- */

static void say_line(ExMake *m, const char *line)
{
    if (m->say != NULL) {
        m->say(m->user, line);
    } else {
        fputs(line, stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
}

/* print(text): a line at each newline in it. */
static void say_text(ExMake *m, const char *text)
{
    const char *p = text;
    for (;;) {
        const char *nl = strchr(p, '\n');
        if (nl == NULL) {
            say_line(m, p);
            return;
        }
        size_t n = (size_t)(nl - p);
        char *line = malloc(n + 1);
        if (line == NULL) {
            return;
        }
        memcpy(line, p, n);
        line[n] = '\0';
        say_line(m, line);
        free(line);
        p = nl + 1;
    }
}

static void sayf(ExMake *m, const char *fmt, ...) EX_PRINTF(2, 3);

static void sayf(ExMake *m, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list again;
    va_copy(again, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n >= 0) {
        char *text = malloc((size_t)n + 1);
        if (text != NULL) {
            vsnprintf(text, (size_t)n + 1, fmt, again);
            say_text(m, text);
            free(text);
        }
    }
    va_end(again);
}

/* A step's line: kept for the log, and for its tail when it fails. */
static void keep_line(ExMake *m, const char *line)
{
    size_t n = strlen(line);
    if (m->out_len + n + 2 > m->out_cap) {
        size_t cap = (m->out_cap == 0) ? 4096 : m->out_cap;
        while (cap < m->out_len + n + 2) {
            cap *= 2;
        }
        char *nb = realloc(m->out, cap);
        if (nb == NULL) {
            return;
        }
        m->out = nb;
        m->out_cap = cap;
    }
    memcpy(m->out + m->out_len, line, n);
    m->out_len += n;
    m->out[m->out_len++] = '\n';
    m->out[m->out_len] = '\0';
}

static void step_said(void *user, const char *line)
{
    keep_line(user, line);
}

/* ---- paths ---- */

static bool is_sep(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

/* The path without the separators at its end (a folder named "dir/"),
 * as Windows' stat wants it. */
static const char *trimmed(Extract *x, const char *path)
{
    size_t n = strlen(path);
    while (n > 1 && is_sep(path[n - 1]) && path[n - 2] != ':') {
        n--;
    }
    return ex_strndup(x, path, n);
}

static bool is_file(Extract *x, const char *path)
{
    return ex_path_kind(trimmed(x, path), NULL) == EX_PATH_FILE;
}

static bool is_dir(Extract *x, const char *path)
{
    return ex_path_kind(trimmed(x, path), NULL) == EX_PATH_DIR;
}

/* Path(path).name: the last part. */
static const char *path_name(Extract *x, const char *path)
{
    const char *p = trimmed(x, path);
    const char *name = p;
    for (const char *c = p; *c != '\0'; c++) {
        if (is_sep(*c)) {
            name = c + 1;
        }
#ifdef _WIN32
        if (*c == ':' && c == p + 1) {
            name = c + 1;
        }
#endif
    }
    if (strcmp(name, ".") == 0) {
        return "";
    }
    return name;
}

/* str(Path(path)): the parts the system's separator apart, "." parts and
 * doubled separators gone. */
static const char *path_str(Extract *x, const char *path)
{
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    ExStr s;
    ex_str_init(x, &s);
    const char *p = path;
#ifdef _WIN32
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'))
        && p[1] == ':') {
        ex_str_addn(&s, p, 2);
        p += 2;
    }
#endif
    if (is_sep(*p)) {
        ex_str_addc(&s, sep);
        while (is_sep(*p)) {
            p++;
        }
    }
    size_t root = s.len;
    while (*p != '\0') {
        const char *e = p;
        while (*e != '\0' && !is_sep(*e)) {
            e++;
        }
        size_t n = (size_t)(e - p);
        if (!(n == 1 && p[0] == '.')) {
            if (s.len > root) {
                ex_str_addc(&s, sep);
            }
            ex_str_addn(&s, p, n);
        }
        p = e;
        while (is_sep(*p)) {
            p++;
        }
    }
    if (s.len == 0) {
        return ".";
    }
    return s.buf;
}

static char *game_path(ExMake *m, const char *rel)
{
    return ex_path(&m->plan, m->game, rel);
}

/* ---- Python's text ---- */

/* str.isspace for an ASCII character. */
static bool py_space(int c)
{
    return c == ' ' || (c >= '\t' && c <= '\r') || (c >= 0x1C && c <= 0x1F);
}

/* bytes.decode("ascii", "replace") of p[0..n) and its strip(): the
 * bytes past ASCII each U+FFFD (none is a space). [*from, *to). */
static void stripped(const u8 *p, size_t n, size_t *from, size_t *to)
{
    size_t a = 0;
    size_t b = n;
    while (a < b && p[a] < 0x80 && py_space(p[a])) {
        a++;
    }
    while (b > a && p[b - 1] < 0x80 && py_space(p[b - 1])) {
        b--;
    }
    *from = a;
    *to = b;
}

/* repr() of the str bytes.decode("ascii", "replace") gives. */
static void repr_ascii(ExStr *s, const u8 *p, size_t n)
{
    bool single = memchr(p, '\'', n) != NULL;
    bool dbl = memchr(p, '"', n) != NULL;
    char quote = (single && !dbl) ? '"' : '\'';
    ex_str_addc(s, quote);
    for (size_t i = 0; i < n; i++) {
        u8 c = p[i];
        if (c >= 0x80) {
            ex_str_add(s, "\xEF\xBF\xBD");          /* U+FFFD, printable */
        } else if (c == (u8)quote || c == '\\') {
            ex_str_addc(s, '\\');
            ex_str_addc(s, (char)c);
        } else if (c == '\t') {
            ex_str_add(s, "\\t");
        } else if (c == '\n') {
            ex_str_add(s, "\\n");
        } else if (c == '\r') {
            ex_str_add(s, "\\r");
        } else if (c < 0x20 || c == 0x7F) {
            ex_str_addf(s, "\\x%02x", (unsigned)c);
        } else {
            ex_str_addc(s, (char)c);
        }
    }
    ex_str_addc(s, quote);
}

/* ---- the files told apart ---- */

static bool has_image_suffix(Extract *x, const char *name)
{
    const char *lower = ex_lower(x, name);
    for (size_t i = 0; i < 4; i++) {
        if (ex_ends(lower, IMAGE_SUFFIXES[i])) {
            return true;
        }
    }
    return false;
}

/* A .smd: a 512-byte header, then 16 KB blocks, each the odd bytes' half
 * then the even bytes' (a short last block too, as the Python slices). */
static const u8 *deinterleave(Extract *x, const u8 *data, size_t size,
                              size_t *out_size)
{
    size_t body = (size > 512) ? size - 512 : 0;
    const u8 *in = data + ((size > 512) ? 512 : size);
    u8 *out = ex_alloc(x, body + 1);
    for (size_t b = 0; b < body; b += 16384) {
        size_t len = (body - b < 16384) ? body - b : 16384;
        size_t half = len / 2;
        for (size_t i = 0; i < half; i++) {
            out[b + 1 + 2 * i] = in[b + i];
        }
        for (size_t i = 0; i < len - half; i++) {
            out[b + 2 * i] = in[b + half + i];
        }
    }
    *out_size = body;
    return out;
}

/* The cartridge's bytes, in order, from a file's (or the zip it is):
 * NULL with *refusal when the zip holds other than one image. */
static const u8 *read_image(Extract *x, const char *name, const u8 *data,
                            size_t size, const ExZip *z, size_t *out_size,
                            const char **refusal)
{
    const char *lower = ex_lower(x, name);
    if (z != NULL) {
        int count = 0;
        int first = -1;
        for (int i = 0; i < z->count; i++) {
            if (has_image_suffix(x, z->entries[i].name)) {
                if (first < 0) {
                    first = i;
                }
                count++;
            }
        }
        if (count != 1) {
            *refusal = ex_fmt(x, "%s: a zip with %d cartridge images in it, "
                                 "not one", name, count);
            return NULL;
        }
        lower = ex_lower(x, z->entries[first].name);
        data = ex_zip_read(x, z, first, &size);
    }
    if (ex_ends(lower, ".smd")
        || (size % 16384 == 512 && data[8] == 0xAA && data[9] == 0xBB)) {
        return deinterleave(x, data, size, out_size);
    }
    if (size % 16384 == 512) {
        *out_size = size - 512;                /* a copier's header */
        return data + 512;
    }
    *out_size = size;
    return data;
}

static bool both_files(Extract *x, const char *dir)
{
    return is_file(x, ex_path(x, dir, RETRANSLATION_FILES[0]))
        && is_file(x, ex_path(x, dir, RETRANSLATION_FILES[1]));
}

/* The folder holding work/ in a retranslation checkout or an unpacked
 * zip (GitHub's has one folder at its top), or NULL. */
static const char *retranslation_root(Extract *x, const char *dir)
{
    if (both_files(x, dir)) {
        return dir;
    }
    ExDir *d = ex_dir_open(trimmed(x, dir));
    if (d == NULL) {
        return NULL;
    }
    int subs = 0;
    const char *sub = NULL;
    const char *entry;
    while ((entry = ex_dir_next(d)) != NULL) {
        const char *p = ex_path(x, dir, entry);
        if (is_dir(x, p)) {
            subs++;
            sub = p;
        }
    }
    ex_dir_close(d);
    return (subs == 1 && both_files(x, sub)) ? sub : NULL;
}

/* Whether the zip is the retranslation's: each of its two files at its
 * top or under one folder there (the last such entry, as a dict keeps). */
static bool retranslation_zip(const ExZip *z, int entry[2])
{
    entry[0] = -1;
    entry[1] = -1;
    for (int i = 0; i < z->count; i++) {
        const char *n = z->entries[i].name;
        int slashes = 0;
        for (const char *c = n; *c != '\0'; c++) {
            slashes += (*c == '/');
        }
        for (int k = 0; k < 2; k++) {
            const char *f = RETRANSLATION_FILES[k];
            size_t nn = strlen(n);
            size_t nf = strlen(f);
            bool under = nn > nf && n[nn - nf - 1] == '/'
                      && strcmp(n + nn - nf, f) == 0 && slashes == 2;
            if (strcmp(n, f) == 0 || under) {
                entry[k] = i;
            }
        }
    }
    return entry[0] >= 0 && entry[1] >= 0;
}

static const char *not_the_cartridge(ExMake *m, Extract *x,
                                     const char *name, const u8 *data,
                                     size_t size)
{
    u8 sum[20];
    ex_sha1(data, size, sum);
    ExStr s;
    ex_str_init(x, &s);
    ex_str_addf(&s, "%s is not a cartridge the game knows.\n", name);
    ex_str_add(&s, "  its SHA-1: ");
    for (int i = 0; i < 20; i++) {
        ex_str_addf(&s, "%02x", (unsigned)sum[i]);
    }
    ex_str_addf(&s, " (%zu bytes)\n", size);
    size_t lo = (size < 0x150) ? size : 0x150;
    size_t hi = (size < 0x180) ? size : 0x180;
    size_t from;
    size_t to;
    stripped(data + lo, hi - lo, &from, &to);
    if (to > from && size > 0x18E && memcmp(data + 0x100, "SEGA", 4) == 0) {
        ex_str_add(&s, "  its header names: ");
        repr_ascii(&s, data + lo + from, to - from);
        ex_str_add(&s, ", version ");
        repr_ascii(&s, data + 0x18C, 2);
        ex_str_addc(&s, '\n');
    }
    ex_str_addf(&s, "The game's own files are made from Phantasy Star II for "
                    "the USA/Europe, revision 02, the image commonly named "
                    "'Phantasy Star II (UE) (REV02)' (SHA-1 %s, %lld bytes); "
                    "earlier revisions differ in code and cannot be used.\n",
                m->rom_sha1, m->rom_size);
    ex_str_addf(&s, "The JAPANESE language is made from the Japanese "
                    "cartridge, Kaerazaru Toki no Owari ni (its first 768 KB "
                    "CRC32 %08lX).", (unsigned long)EX_JP_CRC);
    return s.buf;
}

/* What `given` is: a kind, its bytes kept in m; KINDS with *refusal
 * saying what it is instead. */
static Kind identify(ExMake *m, const char *given, const char **refusal)
{
    Extract *x = &m->plan;
    const char *name = path_name(x, given);
    ExPathKind what = ex_path_kind(trimmed(x, given), NULL);
    if (what == EX_PATH_NONE) {
        *refusal = ex_fmt(x, "%s: there is no such file", path_str(x, given));
        return KINDS;
    }
    if (what == EX_PATH_DIR) {
        const char *root = retranslation_root(x, given);
        if (root == NULL) {
            *refusal = ex_fmt(x, "%s: a folder, but not the retranslation's "
                                 "(no work/dialogue.json in it)", name);
            return KINDS;
        }
        if (!m->have[KIND_RT]) {
            m->rt_dir = root;
        }
        return KIND_RT;
    }
    size_t size = 0;
    const u8 *data = ex_read_file(x, given, &size);
    if (data == NULL) {
        ex_fail(x, "%s: it cannot be read", given);
    }
    ExZip z;
    bool zip = ex_zip_open(x, data, size, &z);
    int entry[2];
    if (zip && retranslation_zip(&z, entry)) {
        if (!m->have[KIND_RT]) {
            m->rt_dir = NULL;
            m->rt_zip = z;
            m->rt_entry[0] = entry[0];
            m->rt_entry[1] = entry[1];
        }
        return KIND_RT;
    }
    size_t isize = 0;
    const u8 *image = read_image(x, name, data, size, zip ? &z : NULL,
                                 &isize, refusal);
    if (image == NULL) {
        return KINDS;
    }
    if ((long long)isize == m->rom_size) {
        u8 sum[20];
        ex_sha1(image, isize, sum);
        char hex[41];
        for (int i = 0; i < 20; i++) {
            snprintf(hex + 2 * i, 3, "%02x", (unsigned)sum[i]);
        }
        if (strcmp(hex, m->rom_sha1) == 0) {
            if (!m->have[KIND_US]) {
                m->image = image;
                m->image_size = isize;
            }
            return KIND_US;
        }
    }
    if (isize >= EX_JP_SIZE && ex_crc32(image, EX_JP_SIZE) == EX_JP_CRC) {
        if (!m->have[KIND_JA]) {
            m->ja = given;
        }
        return KIND_JA;
    }
    *refusal = not_the_cartridge(m, x, name, image, isize);
    return KINDS;
}

/* The commit a GitHub "Download ZIP" was made from: its comment, when
 * that is one; NULL otherwise. */
static const char *zip_commit(Extract *x, const ExZip *z)
{
    size_t from;
    size_t to;
    stripped((const u8 *)z->comment, z->comment_len, &from, &to);
    if (to - from != 40) {
        return NULL;
    }
    for (size_t i = from; i < to; i++) {
        char c = z->comment[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return NULL;
        }
    }
    return ex_strndup(x, z->comment + from, 40);
}

typedef struct {
    ExMake            *m;
    const ExMakeArgs  *a;
    int                status;
} Plan;

static void plan(Extract *x, void *arg)
{
    Plan *p = arg;
    ExMake *m = p->m;
    const ExMakeArgs *a = p->a;
    const char *game = (a->game != NULL && strcmp(a->game, ".") != 0)
                     ? a->game : "";
    m->game = ex_strdup(x, game);
    m->assets = game_path(m, "assets");
    m->map = game_path(m, "tools/rom_map.json");
    m->log_path = game_path(m, "make_assets.log");

    const char *text = (const char *)ex_read_file(x, m->map, NULL);
    if (text == NULL) {
        ex_fail(x, "%s: there is no such file", m->map);
    }
    const JsonValue *rom = json_get(ex_parse_json(x, text, m->map), "rom");
    const JsonValue *rsize = json_get(rom, "size");
    const JsonValue *rsha1 = json_get(rom, "sha1");
    if (rsize == NULL || rsize->type != JSON_NUMBER || rsha1 == NULL
        || rsha1->type != JSON_STRING) {
        ex_fail(x, "%s: no \"rom\" with its size and SHA-1", m->map);
    }
    m->rom_size = (long long)rsize->number;
    m->rom_sha1 = ex_strdup(x, rsha1->string);

    for (int i = 0; i < a->nfiles; i++) {
        const char *refusal = NULL;
        Kind kind = identify(m, a->files[i], &refusal);
        if (kind == KINDS) {
            say_text(m, refusal);
            p->status = EX_MAKE_NOTHING;
            return;
        }
        if (m->have[kind]) {
            sayf(m, "Two of the same given: %s twice (%s).", KIND_TWICE[kind],
                 a->files[i]);
            p->status = EX_MAKE_NOTHING;
            return;
        }
        m->have[kind] = true;
        sayf(m, "%s: %s", path_name(x, a->files[i]), KIND_WHAT[kind]);
    }
    bool any = m->have[KIND_US] || m->have[KIND_JA] || m->have[KIND_RT];
    if (any && !m->have[KIND_US]
        && !is_file(x, game_path(m, "assets/manifest.txt"))) {
        say_line(m, "The game's own files come first, made from the "
                    "US/European cartridge (revision 02); the JAPANESE "
                    "language is added to them afterwards.");
        p->status = EX_MAKE_NOTHING;
        return;
    }
    if (m->have[KIND_JA] != m->have[KIND_RT]) {
        sayf(m, "The JAPANESE language is made from two things: the "
                "Japanese cartridge's image and Excalibur_Z's retranslation "
                "(github.com/ExcalZ/ps2-retranslation, its \"Download "
                "ZIP\"). Only %s was given.",
             m->have[KIND_JA] ? "the cartridge" : "the retranslation");
        p->status = EX_MAKE_NOTHING;
        return;
    }
    if (!any) {
        say_line(m, "Nothing given: the US/European cartridge's image "
                    "(revision 02), or the Japanese cartridge and the "
                    "retranslation.");
        p->status = EX_MAKE_NOTHING;
        return;
    }

    /* the steps */
    m->steps = ex_alloc(x, sizeof *m->steps * (size_t)(EX_STEP_COUNT + 2));
    if (m->have[KIND_US]) {
        for (int i = 0; i < EX_STEP_COUNT; i++) {
            Step *s = &m->steps[m->nsteps++];
            s->kind = STEP_EXTRACT;
            s->step = &EX_STEPS[i];
            s->name = EX_STEPS[i].name;
            s->out = game_path(m, EX_STEPS[i].default_out);
        }
    }
    if (m->have[KIND_JA]) {
        if (m->rt_dir == NULL) {
            m->dialogue = ex_zip_read(x, &m->rt_zip, m->rt_entry[0],
                                      &m->dialogue_size);
            m->script = ex_zip_read(x, &m->rt_zip, m->rt_entry[1],
                                    &m->script_size);
            m->commit = zip_commit(x, &m->rt_zip);
        }
        Step *s = &m->steps[m->nsteps++];
        s->kind = STEP_JAPANESE;
        s->name = "the Japanese language";
        s->out = game_path(m, "assets/lang/ja");
        if (m->commit != NULL
            && strcmp(m->commit, RETRANSLATION_PROVED) != 0) {
            sayf(m, "(the retranslation's commit %.12s; the game was proved "
                    "with %.12s: its text may differ)", m->commit,
                 RETRANSLATION_PROVED);
        }
    }
    Step *s = &m->steps[m->nsteps++];
    s->kind = STEP_MANIFEST;
    s->name = "the preload list";
    s->out = m->assets;

    m->log = ex_fopen(m->log_path, "w");
    if (m->log == NULL) {
        ex_fail(x, "%s: cannot write it", m->log_path);
    }
    ex_str_init(x, &m->failed);
}

/* fn with ex_fail's stop, its memory kept (the plan's, until the end). */
static bool kept(Extract *x, void (*fn)(Extract *x, void *arg), void *arg)
{
    jmp_buf here;
    x->error[0] = '\0';
    x->jump = &here;
    if (setjmp(here) != 0) {
        x->jump = NULL;
        return false;
    }
    fn(x, arg);
    x->jump = NULL;
    return true;
}

ExMake *ex_make_begin(const ExMakeArgs *a, int *status)
{
    ExMake *m = calloc(1, sizeof *m);
    if (m == NULL) {
        *status = EX_MAKE_FAILED;
        return NULL;
    }
    m->keep_going = a->keep_going;
    m->say = a->say;
    m->user = a->user;
    Plan p = { m, a, EX_MAKE_DONE };
    if (!kept(&m->plan, plan, &p)) {
        say_text(m, m->plan.error);
        p.status = EX_MAKE_FAILED;
    }
    if (p.status != EX_MAKE_DONE) {
        ex_make_end(m);
        *status = p.status;
        return NULL;
    }
    *status = EX_MAKE_DONE;
    return m;
}

int ex_make_steps(const ExMake *m)
{
    return m->nsteps;
}

const char *ex_make_step_name(const ExMake *m, int k)
{
    return (k >= 0 && k < m->nsteps) ? m->steps[k].name : "";
}

bool ex_make_keep_going(const ExMake *m)
{
    return m->keep_going;
}

/* ---- the preload list: tools/make_manifest.py ---- */

typedef struct {
    char    **paths;
    int       count;
    int       cap;
    long long total;
} Listing;

/* make_manifest.py's NOT_READ_FILES: listings no loader opens. */
static const char *const NOT_READ_FILES[] = {
    "music/songs.json", "nametables/nametables.json",
    "palettes/palettes.json", "winart.json"
};

static void listed(Extract *x, Listing *l, const char *rel, const char *name,
                   long long size)
{
    if (strcmp(rel, "manifest.txt") == 0 || name[0] == '.'
        || ex_starts(rel, "icon/")) {
        return;
    }
    for (size_t i = 0; i < sizeof NOT_READ_FILES / sizeof NOT_READ_FILES[0];
         i++) {
        if (strcmp(rel, NOT_READ_FILES[i]) == 0) {
            return;
        }
    }
    const char *dot = strrchr(name, '.');
    if (dot != NULL && dot != name && dot[1] != '\0'
        && (strcmp(dot, ".tsx") == 0 || strcmp(dot, ".bak") == 0)) {
        return;
    }
    if (l->count == l->cap) {
        int cap = (l->cap == 0) ? 1024 : l->cap * 2;
        char **np = ex_alloc(x, sizeof *np * (size_t)cap);
        if (l->count > 0) {
            memcpy(np, l->paths, sizeof *np * (size_t)l->count);
        }
        l->paths = np;
        l->cap = cap;
    }
    l->paths[l->count++] = ex_fmt(x, "assets/%s", rel);
    l->total += size;
}

/* Path.rglob("*")'s files under dir (rel its place under assets/). */
static void walk(Extract *x, Listing *l, const char *dir, const char *rel)
{
    ExDir *d = ex_dir_open(dir);
    if (d == NULL) {
        return;
    }
    const char *entry;
    while ((entry = ex_dir_next(d)) != NULL) {
        const char *name = ex_strdup(x, entry);
        const char *full = ex_path(x, dir, name);
        const char *r = (rel[0] != '\0') ? ex_fmt(x, "%s/%s", rel, name)
                                         : name;
        long long size = 0;
        ExPathKind what = ex_path_kind(full, &size);
        if (what == EX_PATH_DIR) {
            walk(x, l, full, r);
        } else if (what == EX_PATH_FILE) {
            listed(x, l, r, name, size);
        }
    }
    ex_dir_close(d);
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void manifest(Extract *x, const void *arg)
{
    const ExMake *m = arg;
    Listing l;
    memset(&l, 0, sizeof l);
    walk(x, &l, m->assets, "");
    if (l.count > 1) {
        qsort(l.paths, (size_t)l.count, sizeof *l.paths, by_name);
    }
    ExStr s;
    ex_str_init(x, &s);
    ex_str_add(&s, "# Files preloaded into RAM at startup. Generated by "
                   "tools/make_manifest.py.\n");
    for (int i = 0; i < l.count; i++) {
        ex_str_add(&s, l.paths[i]);
        ex_str_addc(&s, '\n');
    }
    if (l.count == 0) {
        ex_str_addc(&s, '\n');
    }
    ex_write(x, ex_path(x, m->assets, "manifest.txt"), s.buf, s.len);
    ex_say(x, "%d files, %.1f MB -> assets/manifest.txt", l.count,
           (double)l.total / 1e6);
}

/* ---- a step ---- */

/* The end of the step's output, as the Python says
 * proc.stdout.rstrip()[-2000:]. */
static void say_tail(ExMake *m)
{
    size_t n = m->out_len;
    while (n > 0 && py_space((u8)m->out[n - 1])) {
        n--;
    }
    size_t from = n;
    int chars = 0;
    while (from > 0 && chars < TAIL_CHARS) {
        from--;
        if (((u8)m->out[from] & 0xC0) != 0x80) {
            chars++;
        }
    }
    char *tail = malloc(n - from + 1);
    if (tail == NULL) {
        return;
    }
    memcpy(tail, m->out + from, n - from);
    tail[n - from] = '\0';
    say_text(m, tail);
    free(tail);
}

bool ex_make_step(ExMake *m, int k)
{
    if (k < 0 || k >= m->nsteps) {
        return false;
    }
    const Step *s = &m->steps[k];
    sayf(m, "[%d/%d] %s", k + 1, m->nsteps, s->name);
    m->out_len = 0;
    if (m->out != NULL) {
        m->out[0] = '\0';
    }
    m->run.say = step_said;
    m->run.user = m;
    bool ok = true;
    switch (s->kind) {
    case STEP_EXTRACT:
        if (!m->run_open) {
            ok = ex_open(&m->run, m->map, m->image, m->image_size);
            m->run_open = ok;
        }
        if (ok) {
            ExArgs args = { s->out, m->assets };
            ok = ex_run(&m->run, s->step, &args);
        }
        break;
    case STEP_JAPANESE: {
        ExJapanese j;
        memset(&j, 0, sizeof j);
        j.image = m->ja;
        j.retranslation = m->rt_dir;
        j.out = s->out;
        j.assets = m->assets;
        j.commit = m->commit;
        j.dialogue = m->dialogue;
        j.dialogue_size = m->dialogue_size;
        j.script = m->script;
        j.script_size = m->script_size;
        ok = ex_japanese(&m->run, &j);
        break;
    }
    case STEP_MANIFEST:
        ok = ex_call(&m->run, manifest, m);
        break;
    }
    if (!ok) {
        keep_line(m, m->run.error);
    }
    if (m->log != NULL) {
        fprintf(m->log, "$ %s\n%s\n", s->name, (m->out != NULL) ? m->out : "");
        fflush(m->log);
    }
    if (!ok) {
        if (m->nfailed > 0) {
            ex_str_add(&m->failed, ", ");
        }
        ex_str_add(&m->failed, s->name);
        m->nfailed++;
        say_tail(m);
    }
    return ok;
}

/* ---- the check against tools/assets.sha256 ---- */

typedef struct {
    const char *rel;
    const char *digest;
    int         order;
} Want;

static int by_rel(const void *a, const void *b)
{
    const Want *p = a;
    const Want *q = b;
    int c = strcmp(p->rel, q->rel);
    return (c != 0) ? c : (p->order > q->order) - (p->order < q->order);
}

typedef struct {
    ExMake     *m;
    const char *list;
    bool        ja;
    int         differ;
} Verify;

/* A line's words, as str.split(): the first, and the rest past the
 * spaces after it (str.split(None, 1)). */
static const char *first_word(Extract *x, const char *line, const char **rest)
{
    const char *a = line;
    while (*a != '\0' && py_space((u8)*a)) {
        a++;
    }
    const char *b = a;
    while (*b != '\0' && !py_space((u8)*b)) {
        b++;
    }
    const char *r = b;
    while (*r != '\0' && py_space((u8)*r)) {
        r++;
    }
    *rest = r;
    return ex_strndup(x, a, (size_t)(b - a));
}

static void verify(Extract *x, const void *arg)
{
    Verify *v = (Verify *)arg;
    ExMake *m = v->m;
    size_t size = 0;
    char *text = (char *)ex_read_file(x, v->list, &size);
    if (text == NULL) {
        ex_fail(x, "%s: it cannot be read", v->list);
    }
    /* the lines (splitlines: \n, \r\n, \r) */
    int nlines = 1;
    for (size_t i = 0; i < size; i++) {
        nlines += (text[i] == '\n' || text[i] == '\r');
    }
    char **lines = ex_alloc(x, sizeof *lines * (size_t)(nlines + 1));
    int n = 0;
    char *p = text;
    char *end = text + size;
    while (p < end) {
        char *e = p;
        while (e < end && *e != '\n' && *e != '\r') {
            e++;
        }
        char *next = e;
        if (next < end) {
            next += (next[0] == '\r' && next + 1 < end && next[1] == '\n')
                  ? 2 : 1;
        }
        *e = '\0';
        lines[n++] = p;
        p = next;
    }

    static const char *const SECTIONS[2] = { "us", "ja" };
    Want *want = ex_alloc(x, sizeof *want * (size_t)(n + 1));
    int nwant = 0;
    for (int s = 0; s < (v->ja ? 2 : 1); s++) {
        const char *current = "";
        for (int i = 0; i < n; i++) {
            const char *line = lines[i];
            if (ex_starts(line, "# section ")) {
                const char *rest;
                first_word(x, line, &rest);             /* # */
                const char *word = first_word(x, rest, &rest);  /* section */
                (void)word;
                current = first_word(x, rest, &rest);
            } else if (line[0] != '\0' && line[0] != '#'
                       && strcmp(current, SECTIONS[s]) == 0) {
                const char *rest;
                const char *digest = first_word(x, line, &rest);
                if (rest[0] == '\0') {
                    ex_fail(x, "%s: a line with no file: %s", v->list, line);
                }
                want[nwant].rel = rest;
                want[nwant].digest = digest;
                want[nwant].order = nwant;
                nwant++;
            }
        }
    }
    if (nwant > 1) {
        qsort(want, (size_t)nwant, sizeof *want, by_rel);
    }
    /* a later line for the same file is the one kept */
    int kept_n = 0;
    for (int i = 0; i < nwant; i++) {
        if (i + 1 < nwant && strcmp(want[i].rel, want[i + 1].rel) == 0) {
            continue;
        }
        want[kept_n++] = want[i];
    }
    nwant = kept_n;

    const char **missing = ex_alloc(x, sizeof *missing * (size_t)(nwant + 1));
    const char **bad = ex_alloc(x, sizeof *bad * (size_t)(nwant + 1));
    int nmissing = 0;
    int nbad = 0;
    for (int i = 0; i < nwant; i++) {
        const char *path = ex_path(x, m->game, want[i].rel);
        if (!is_file(x, path)) {
            missing[nmissing++] = want[i].rel;
            continue;
        }
        const char *digest = ex_file_digest(x, path);
        if (digest == NULL || strcmp(digest, want[i].digest) != 0) {
            bad[nbad++] = want[i].rel;
        }
    }
    sayf(m, "checked: %d of %d files as the game was proved with",
         nwant - nbad - nmissing, nwant);
    for (int i = 0; i < nmissing && i < 20; i++) {
        sayf(m, "  MISSING  %s", missing[i]);
    }
    for (int i = 0; i < nbad && i < 20; i++) {
        sayf(m, "  DIFFERS  %s", bad[i]);
    }
    v->differ = nbad + nmissing;
}

int ex_make_finish(ExMake *m)
{
    if (m->log != NULL) {
        fclose(m->log);
        m->log = NULL;
    }
    if (m->nfailed > 0) {
        sayf(m, "FAILED: %s (the whole output is in %s)", m->failed.buf,
             m->log_path);
        return EX_MAKE_FAILED;
    }
    Verify v;
    memset(&v, 0, sizeof v);
    v.m = m;
    v.list = game_path(m, "tools/assets.sha256");
    if (is_file(&m->plan, v.list)) {
        v.ja = is_file(&m->plan, game_path(m, "assets/lang/ja/lang.json"));
        m->run.say = NULL;
        if (!ex_call(&m->run, verify, &v)) {
            say_text(m, m->run.error);
            return EX_MAKE_FAILED;
        }
        if (v.differ > 0) {
            sayf(m, "%d files differ from the ones the game was proved with. "
                    "It may still play; %s has the whole output.", v.differ,
                 m->log_path);
            return EX_MAKE_DIFFERS;
        }
    } else {
        say_line(m, "(no tools/assets.sha256 here: the files are not "
                    "checked)");
    }
    say_line(m, "Done.");
    return EX_MAKE_DONE;
}

void ex_make_end(ExMake *m)
{
    if (m == NULL) {
        return;
    }
    if (m->log != NULL) {
        fclose(m->log);
    }
    ex_close(&m->run);
    ex_close(&m->plan);
    free(m->out);
    free(m);
}

int ex_make_assets(const ExMakeArgs *a)
{
    int status = EX_MAKE_DONE;
    ExMake *m = ex_make_begin(a, &status);
    if (m == NULL) {
        return status;
    }
    for (int k = 0; k < ex_make_steps(m); k++) {
        if (!ex_make_step(m, k) && !m->keep_going) {
            break;
        }
    }
    status = ex_make_finish(m);
    ex_make_end(m);
    return status;
}
