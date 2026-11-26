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
#include <stdio.h>

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

/* (build 193) The system's file calls, by UTF-8 names (exfs.c). Every
 * path the extraction takes is UTF-8 -- SDL gives a dropped or chosen
 * file's so, and ex_utf8_args reads the command line's so -- which
 * Windows' narrow calls do not take (they read the ANSI code page): there
 * each is opened by its wide name. A path that is not UTF-8 is taken as
 * it is. Elsewhere the bytes are the name. Everything under src/extract/
 * reaches the disk through these and ex_read_file/ex_write. */
typedef enum {
    EX_PATH_NONE,               /* nothing there (or it cannot be seen) */
    EX_PATH_FILE,
    EX_PATH_DIR,
    EX_PATH_OTHER
} ExPathKind;

FILE      *ex_fopen(const char *path, const char *mode);
/* What is at `path` (no separator at its end), and its size (may be
 * NULL). */
ExPathKind ex_path_kind(const char *path, long long *size);
int        ex_mkdir_one(const char *path);     /* 0, or -1 with errno */
int        ex_remove_file(const char *path);   /* 0, or -1 */

/* A folder's names, "." and ".." left out; NULL when it cannot be
 * opened. A name stays until the next ex_dir_next. */
typedef struct ExDir ExDir;
ExDir      *ex_dir_open(const char *path);     /* "" is the current one */
const char *ex_dir_next(ExDir *d);             /* NULL at the end */
void        ex_dir_close(ExDir *d);

/* The last `n` of the program's arguments (`args`, main()'s argv from
 * some place on), as UTF-8: on Windows read again from the wide command
 * line, as main() is handed them in the ANSI code page; elsewhere the
 * same pointers. One block, NULL after the n, for free(); NULL when there
 * is no memory. */
char      **ex_utf8_args(int n, char **args);
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
/* (build 191) The same with ensure_ascii=False: only '"', '\\' and the
 * bytes below a space escaped, the rest as its UTF-8. */
char *jo_dump_unicode(Extract *x, const JOut *v, int indent,
                      bool sort_keys);
/* json.dumps of one string. */
char *jo_quote(Extract *x, const char *s);

/* ---- (build 191) work that is not one of the steps ---- */

/* fn(x, arg) run as a step is: ex_fail ends it, its memory goes after;
 * false with x->error when it failed. No map is needed (ex_open). */
bool ex_call(Extract *x, void (*fn)(Extract *x, const void *arg),
             const void *arg);

/* JSON text parsed, the tree freed with the step's memory; a stop, with
 * `path` named, when it is not JSON. */
const JsonValue *ex_parse_json(Extract *x, const char *text,
                               const char *path);

/* The Japanese cartridge, as tools/import_japanese.py knows it: its
 * first 768 KB (a 1 MB overdump is fine) and their CRC32. */
#define EX_JP_SIZE 0xC0000u
#define EX_JP_CRC  0xBEC8EB5Au

/* tools/import_japanese.py: the JAPANESE language (assets/lang/ja/) from
 * the Japanese cartridge and Excalibur_Z's retranslation
 * (docs/languages.md; ex_japanese.c). */
typedef struct {
    const char *image;          /* the cartridge's image, or a zip of it */
    const char *retranslation;  /* the folder holding work/dialogue.json;
                                   NULL: the two files below */
    const char *out;            /* -o; NULL: ASSETS/lang/ja */
    const char *assets;         /* the game's own files; NULL: "assets" */
    const char *commit;         /* --commit; NULL or "": none */
    /* (build 192) work/dialogue.json's and work/script.json's bytes, from
     * the retranslation's zip, when `retranslation` is NULL */
    const u8   *dialogue;
    size_t      dialogue_size;
    const u8   *script;
    size_t      script_size;
} ExJapanese;

bool ex_japanese(Extract *x, const ExJapanese *a);

/* ---- hashes ---- */

void ex_sha1(const u8 *data, size_t n, u8 out[20]);

/* (build 192) SHA-256 (exdigest.c), and a file as tools/make_assets.py's
 * file_digest gives it: the SHA-256 of its bytes in hex, or for a PNG
 * "png:" and the SHA-256 of its picture as Pillow opens it -- mode, size,
 * palette, transparency, pixels -- since two zlibs pack the same picture
 * in other bytes. NULL when the file cannot be read; a PNG this cannot
 * open as Pillow would gives a digest no list holds (EX_DIGEST_UNREAD). */
void  ex_sha256(const u8 *data, size_t n, u8 out[32]);
char *ex_file_digest(Extract *x, const char *path);
#define EX_DIGEST_UNREAD "png:unreadable"

/* ---- (build 192) tools/make_assets.py in C (exmake.c) ---- */

/* The game's files from what the player gives -- the US/European
 * cartridge, revision 02, in any of the ways an image is kept; or the
 * Japanese cartridge and Excalibur_Z's retranslation -- each told apart
 * by what it is, as make_assets.py tells them: the steps (EX_STEPS, the
 * JAPANESE language, the preload list) into GAME/assets, every step's
 * lines into GAME/make_assets.log, then the files checked against
 * GAME/tools/assets.sha256 where there is one. It says what the Python
 * says, line for line, and ends with its exit status (EX_MAKE_*). A
 * window runs it a step at a time: ex_make_begin, ex_make_step for each
 * step (stopping at a failure, unless keep_going), ex_make_finish,
 * ex_make_end; ex_make_assets runs it all. */
enum {
    EX_MAKE_DONE    = 0,    /* made (and checked, where there is a list) */
    EX_MAKE_FAILED  = 1,    /* a step failed (the log says how) */
    EX_MAKE_NOTHING = 2,    /* nothing the files can be made from */
    EX_MAKE_DIFFERS = 3     /* made, but some differ from the list's */
};

typedef struct {
    const char *game;            /* the game's folder: assets/, tools/,
                                    the log; NULL or "": the current one */
    const char *const *files;    /* the images and the retranslation, in
                                    any order, as make_assets.py's FILE...
                                    (then its --ja and --retranslation) */
    int         nfiles;
    bool        keep_going;
    /* Each line said; NULL prints them to stdout. */
    void      (*say)(void *user, const char *line);
    void       *user;
} ExMakeArgs;

typedef struct ExMake ExMake;

/* The files told apart and the steps planned. NULL with *status when
 * there is nothing to make (EX_MAKE_NOTHING, the reason said) or it
 * cannot start (EX_MAKE_FAILED: no map, no memory). */
ExMake     *ex_make_begin(const ExMakeArgs *a, int *status);
int         ex_make_steps(const ExMake *m);
const char *ex_make_step_name(const ExMake *m, int k);
/* Step k, 0 first: "[k+1/N] NAME" said, then the step run. False when it
 * failed (the end of what it said is said). */
bool        ex_make_step(ExMake *m, int k);
bool        ex_make_keep_going(const ExMake *m);
/* After the steps: the failures named, or the files checked; the exit
 * status. */
int         ex_make_finish(ExMake *m);
void        ex_make_end(ExMake *m);

/* All of it, as `python3 tools/make_assets.py` runs: the exit status. */
int ex_make_assets(const ExMakeArgs *a);

#endif /* PS2_EXTRACT_H */
