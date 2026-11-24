#ifndef PS2_EXTRACT_H
#define PS2_EXTRACT_H

/* (build 187) The game's files made from the cartridge image in C: the
 * extractors of tools/extract_*.py, one for one, writing the same files
 * byte for byte (a PNG the same picture), so that making them needs no
 * Python (docs/first_start.md, "In C"). C11 and the tree's own code only
 * (src/core/json.c reads, jout.c writes, src/core/pngwrite.c draws): no
 * library, nothing of one system's.
 *
 * Each extractor is a step (EX_STEPS, ex_steps.c) run on an Extract: the
 * image, the map of it (tools/rom_map.json, as tools/rommap.py reads it),
 * and an arena everything a step allocates comes from. A step that cannot
 * go on calls ex_fail, as the Python ones call sys.exit: the run ends
 * there, its memory is let go, and ex_run says why. The Python extractors
 * stay as the reference; tools/extract_c_check.py holds the two to the
 * same files. */

#include "../core/ps2_types.h"
#include "../core/json.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>

/* printf's checks on the formats, where the compiler has them. MinGW's
 * "printf" checks Microsoft's old msvcrt formats; the C runtime the tree
 * builds against there (UCRT) is C99's, %zu and %lld, as glibc's. */
#if defined(__MINGW32__)
#define EX_PRINTF(f, a) __attribute__((format(gnu_printf, f, a)))
#elif defined(__GNUC__) || defined(__clang__)
#define EX_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define EX_PRINTF(f, a)
#endif

/* ---- the map ---- */

typedef struct {
    char *name;
    u32   addr;
} ExLabel;

typedef struct {
    char *name;              /* "art/foo.bin" */
    u32   addr;
    u32   size;
} ExFile;

typedef struct {
    char     *name;
    long long value;
} ExConst;

/* An index's entry: a name (or an address) and the entry's rank, its
 * place in the map's own order. Sorted by the key, then the rank. */
typedef struct {
    const char *key;
    int         rank;
} ExKey;

typedef struct {
    u32 addr;
    int rank;
} ExAt;

typedef struct ExBlock ExBlock;

typedef struct {
    const u8 *rom;
    size_t    size;
    ExLabel  *labels;        /* ps2.asm's, in its line order */
    int       nlabels;
    ExLabel  *drums;         /* the drum samples' (sound/ps2.pcm_drums.asm) */
    int       ndrums;
    ExFile   *files;         /* binclude'd files, in the map's order */
    int       nfiles;
    ExConst  *consts;        /* in the map's order */
    int       nconsts;
    /* indexes */
    ExKey    *by_name;       /* the labels' names */
    ExKey    *drum_by_name;
    ExKey    *by_lower;      /* labels (rank: number) then drums (rank:
                                nlabels + number), names in lowercase */
    ExAt     *by_addr;       /* the labels' addresses */
    ExKey    *const_by_name; /* also Python's sorted(rom.consts) */
    ExKey    *const_by_lower;
    ExBlock  *arena;         /* all of the above */
} ExMap;

/* ---- a run ---- */

typedef struct Extract {
    ExMap      map;
    jmp_buf   *jump;         /* where ex_fail goes, while a step runs */
    char       error[1024];
    ExBlock   *arena;
    JsonValue **parsed;      /* files a step read, freed after it */
    int        nparsed;
    int        capparsed;
    /* The steps' messages (what the Python ones print), one line at a
     * time without its newline; NULL prints them to stdout. */
    void     (*say)(void *user, const char *line);
    void      *user;
} Extract;

typedef struct {
    const char *out;         /* the Python's -o */
    const char *assets;      /* extract_pictures' --assets */
} ExArgs;

typedef struct {
    const char *name;        /* the Makefile's EXTRACTORS name */
    void      (*run)(Extract *x, const ExArgs *a);
    const char *default_out; /* the Python's -o default */
} ExStep;

extern const ExStep EX_STEPS[];
extern const int    EX_STEP_COUNT;

const ExStep *ex_step(const char *name);

/* The image (raw, in order) and the map; false with x->error when the
 * image is not the one the map is of. ex_close lets both go (the image
 * stays the caller's). */
bool ex_open(Extract *x, const char *map_path, const u8 *rom, size_t size);
void ex_close(Extract *x);

/* One step; false with x->error when it failed. */
bool ex_run(Extract *x, const ExStep *step, const ExArgs *a);

/* ---- what steps use ---- */

/* Ends the step: the message is x->error. */
_Noreturn void ex_fail(Extract *x, const char *fmt, ...) EX_PRINTF(2, 3);

void ex_say(Extract *x, const char *fmt, ...) EX_PRINTF(2, 3);

void ex_warn(Extract *x, const char *fmt, ...) EX_PRINTF(2, 3);   /* stderr's lines */

void *ex_alloc(Extract *x, size_t n);              /* zeroed; the step's */
char *ex_strdup(Extract *x, const char *s);
char *ex_strndup(Extract *x, const char *s, size_t n);
char *ex_fmt(Extract *x, const char *fmt, ...) EX_PRINTF(2, 3);
char *ex_lower(Extract *x, const char *s);
bool  ex_starts(const char *s, const char *prefix);
bool  ex_ends(const char *s, const char *suffix);

/* Text built a piece at a time, in the step's arena. */
typedef struct {
    Extract *x;
    char    *buf;
    size_t   len;
    size_t   cap;
} ExStr;

void ex_str_init(Extract *x, ExStr *s);
void ex_str_add(ExStr *s, const char *text);
void ex_str_addn(ExStr *s, const char *text, size_t n);
void ex_str_addc(ExStr *s, char c);
void ex_str_addf(ExStr *s, const char *fmt, ...) EX_PRINTF(2, 3);

/* Files. A path is joined with '/', which every system takes. */
char *ex_path(Extract *x, const char *dir, const char *name);
void  ex_mkdirs(Extract *x, const char *dir);      /* parents, may exist */
void  ex_write(Extract *x, const char *path, const void *data, size_t n);
void  ex_write_text(Extract *x, const char *path, const char *text);
/* (build 189) Path.unlink(missing_ok=True): the file gone, if it was
 * there. */
void  ex_remove(Extract *x, const char *path);
/* (build 189) Each file in `dir` whose name ends in `suffix` gone, as
 * the Python's dir.glob("*" + suffix) unlinks them; nothing when there
 * is no `dir`. */
void  ex_remove_ending(Extract *x, const char *dir, const char *suffix);
/* A file's bytes (NUL after them), or NULL when there is none. */
u8   *ex_read_file(Extract *x, const char *path, size_t *size);
/* A JSON file the earlier steps wrote; a stop when it is missing. */
const JsonValue *ex_read_json(Extract *x, const char *path);

/* The map, as tools/rommap.py: a name exactly, then in any case. */
const char *ex_name(Extract *x, const char *name);  /* NULL: none */
bool        ex_has(Extract *x, const char *name);
u32         ex_addr(Extract *x, const char *name);  /* a drum's too */
int         ex_index(Extract *x, const char *name); /* in line order */
u32         ex_next_addr(Extract *x, const char *name);
/* Every label at an address, in line order: *count of them. */
const ExLabel **ex_names_at(Extract *x, u32 address, int *count);
const u8   *ex_read(Extract *x, long long address, long long length);
const u8   *ex_blob(Extract *x, const char *name, size_t *length);
const u8   *ex_run_of(Extract *x, const char *name, size_t length);
u8          ex_u8(Extract *x, long long address);
u16         ex_u16(Extract *x, long long address);
u32         ex_u32(Extract *x, long long address);
const u8   *ex_file(Extract *x, const char *name, size_t *size);
bool        ex_has_const(Extract *x, const char *name);
long long   ex_const(Extract *x, const char *name);

/* Python's // and % (floor division, a modulo of the divisor's sign). */
long long ex_floordiv(long long a, long long b);
long long ex_mod(long long a, long long b);

/* ---- JSON out, as Python's json.dumps ---- */

typedef struct JOut JOut;

JOut *jo_obj(Extract *x);
JOut *jo_arr(Extract *x);
JOut *jo_int(Extract *x, long long v);
JOut *jo_str(Extract *x, const char *s);  /* UTF-8; copied */
JOut *jo_bool(Extract *x, bool b);
JOut *jo_null(Extract *x);
/* A dict's d[key] = v: a new key goes last, a key there keeps its place. */
void  jo_set(Extract *x, JOut *obj, const char *key, JOut *v);
JOut *jo_get(const JOut *obj, const char *key);
void  jo_add(JOut *arr, JOut *v);
int   jo_len(const JOut *v);
void  jo_seti(Extract *x, JOut *obj, const char *key, long long v);
void  jo_sets(Extract *x, JOut *obj, const char *key, const char *s);
void  jo_setb(Extract *x, JOut *obj, const char *key, bool b);
/* (build 189) A JSON file's value read back to be written out again (a
 * manifest merged into); a number must be an integer. */
JOut *jo_from(Extract *x, const JsonValue *v);
/* json.dumps(v, indent=indent, sort_keys=sort_keys); indent < 0 is no
 * indent (", " and ": "). ensure_ascii, as the default: anything past
 * '~' as \uXXXX. */
char *jo_dump(Extract *x, const JOut *v, int indent, bool sort_keys);
/* json.dumps of one string. */
char *jo_quote(Extract *x, const char *s);

/* ---- hashes ---- */

void ex_sha1(const u8 *data, size_t n, u8 out[20]);

#endif /* PS2_EXTRACT_H */
