#include "songtext.h"
#include "instruments.h"
#include "../core/json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (build 137a) The song text to the driver's image: songtext.h. The image's
 * order, as tools/song_text.py's assemble lays it (docs/audio.md, "The song
 * text", Placing): the seven root words, the priorities ($81-$D8), the NTSC
 * tempos (32), the envelope pointers and envelopes padded to $100, the PAL
 * tempos and tick multipliers (32 each), the song, effect and special
 * pointer tables, the song headers in id order (an empty one after them if
 * an id plays `none`), FMInsPtrs and the instruments, the effects in
 * effects.txt's order (an empty one after them), the driver's code (zeros),
 * the note frequencies, then sounds.txt's `streams` in order. A label is
 * found wherever it lands; the order keeps the image the ROM's. */

#define ST_NAME       64       /* a name or label, NUL included */
#define ST_LINE       1024     /* a line, NUL included */
#define ST_TOKENS     128
#define ST_FILES      64
#define ST_SOUNDS     128      /* ids $81 on */
#define ST_SONGS      64
#define ST_EFFECTS    128
#define ST_BLOCKS     256
#define ST_LAYOUT     256
#define ST_TRACKS     16
#define ST_ENVELOPES  16
#define ST_DRUMS      64
#define ST_NOTES      0x60     /* note bytes $80-$DF */

#define FIRST_ID          0x81
#define FIRST_EFFECT_ID   0xA0
#define FIRST_SPECIAL_ID  0xD8
#define PRIORITIES        (FIRST_SPECIAL_ID - FIRST_ID + 1)
#define TEMPO_ENTRIES     32
#define PAL_TEMPO_AT      0x100
#define ROOTS             7
#define IMAGE_MAX         0x10000

typedef struct {
    char name[ST_NAME];
    u32  at;
} StLabel;

typedef struct {
    u32  at;            /* where the word goes */
    u32  second;        /* a jump: the displacement's second byte */
    bool absolute;      /* a header's pointer: the label's offset */
    int  file, line;
    char label[ST_NAME];
} StFix;

typedef struct {
    u8      *data;
    u32      len, cap;
    StLabel *lab;
    int      nlab, labcap;
    StFix   *fix;
    int      nfix, fixcap;
} StChunk;

typedef struct {
    char label[ST_NAME];
    u8   bytes[9];
    int  file, line;
} StTrack;

typedef struct {
    char    name[ST_NAME];
    int     tempo, pal_tempo, pal_tick;     /* songs */
    StTrack tracks[ST_TRACKS];
    int     counted, total;                 /* tracks, and with unused_track */
    StChunk stream;                         /* effects */
    int     file, line;
} StSound;

typedef struct {
    char    name[ST_NAME];
    StChunk chunk;
    int     file, line;
    bool    placed;
    bool    tracker;            /* (139b) a tracker song's: after the streams */
} StBlock;

/* (build 139b) A song as scan_songs finds it in its song file, and where it
 * is read from. */
typedef struct {
    char  name[ST_NAME];
    int   file;                 /* its song file, st->song_file's index */
    char *trk;                  /* songs/<name>.trk's text, read in the song
                                   file's place; NULL: the stream text */
} StSrc;

typedef struct {
    bool    is_block;
    char    name[ST_NAME];
    StChunk bytes;
    int     file, line;
} StPlace;

typedef struct {
    Ps2Reader reader;
    char      path[ST_FILES][512];
    int       nfiles;

    char note[ST_NOTES][8];                 /* byte $80 + n */
    struct { u8 byte; char name[ST_NAME]; } drum[ST_DRUMS];
    int  ndrums;
    char (*ins_name)[INSTRUMENT_NAME_MAX];
    u8  *bank;
    int  nins;
    char env_name[ST_ENVELOPES][ST_NAME];
    StChunk env[ST_ENVELOPES];
    int  nenv;

    struct { char name[ST_NAME]; u8 priority; int file, line; } sound[ST_SOUNDS];
    int  nsounds;
    char song_file[ST_SONGS][ST_NAME];
    int  song_file_line[ST_SONGS];
    int  nsong_files;
    int  same[256];                         /* -1, or the one it is stored as */
    int  share[256];                        /* 0, or bytes shared with i - 1 */
    long gap;
    StPlace place[ST_LAYOUT];
    int  nplace;

    StSound songs[ST_SONGS];
    int     nsongs;
    StSound effects[ST_EFFECTS];
    int     neffects;
    StBlock blocks[ST_BLOCKS];
    int     nblocks;

    /* (139b) scan_songs: every song file's songs and blocks, which files are
     * still read, and whether any is. */
    StSrc src[ST_SONGS];
    int   nsrc;
    struct { char name[ST_NAME]; int file; } scanned[ST_BLOCKS];
    int   nscanned;
    bool  file_read[ST_SONGS];
    bool  any_file_read;

    StChunk image;                          /* the image, its labels, fixes */
} St;

/* (build 139b) songtext_set_mod_test's: whether a path is the mods
 * folder's. */
static bool (*g_is_mod)(const char *path);

void songtext_set_mod_test(bool (*is_mod)(const char *path))
{
    g_is_mod = is_mod;
}

/* ------------------------------------------------------------------ */
/* Small things                                                        */

static bool fail(St *st, int file, int line, const char *fmt, ...)
{
    va_list ap;
    if (file >= 0) {
        fprintf(stderr, "songtext: %s:%d: ", st->path[file], line);
    } else {
        fprintf(stderr, "songtext: ");
    }
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return false;
}

static bool grow(void **p, int *cap, int need, size_t size)
{
    if (need <= *cap) {
        return true;
    }
    int n = (*cap > 0) ? *cap * 2 : 64;
    while (n < need) {
        n *= 2;
    }
    void *q = realloc(*p, (size_t)n * size);
    if (q == NULL) {
        return false;
    }
    *p = q;
    *cap = n;
    return true;
}

static bool put(StChunk *c, const u8 *bytes, u32 n)
{
    if (c->len + n > c->cap) {
        u32 cap = c->cap ? c->cap : 256;
        while (cap < c->len + n) {
            cap *= 2;
        }
        u8 *q = realloc(c->data, cap);
        if (q == NULL) {
            return false;
        }
        c->data = q;
        c->cap = cap;
    }
    if (bytes != NULL) {
        memcpy(c->data + c->len, bytes, n);
    } else {
        memset(c->data + c->len, 0, n);
    }
    c->len += n;
    return true;
}

static bool put_byte(StChunk *c, u8 b)
{
    return put(c, &b, 1);
}

static bool add_label(StChunk *c, const char *name, u32 at)
{
    if (!grow((void **)&c->lab, &c->labcap, c->nlab + 1, sizeof *c->lab)) {
        return false;
    }
    snprintf(c->lab[c->nlab].name, ST_NAME, "%s", name);
    c->lab[c->nlab].at = at;
    c->nlab++;
    return true;
}

static bool add_fix(StChunk *c, u32 at, u32 second, bool absolute,
                    const char *label, int file, int line)
{
    if (!grow((void **)&c->fix, &c->fixcap, c->nfix + 1, sizeof *c->fix)) {
        return false;
    }
    StFix *f = &c->fix[c->nfix++];
    f->at = at;
    f->second = second;
    f->absolute = absolute;
    f->file = file;
    f->line = line;
    snprintf(f->label, ST_NAME, "%s", label);
    return true;
}

static void free_chunk(StChunk *c)
{
    free(c->data);
    free(c->lab);
    free(c->fix);
    memset(c, 0, sizeof *c);
}

/* A chunk copied onto the end of another, its labels and fixes moved. */
static bool append_chunk(StChunk *to, const StChunk *from)
{
    u32 base = to->len;
    if (!put(to, from->data, from->len)) {
        return false;
    }
    for (int i = 0; i < from->nlab; i++) {
        if (!add_label(to, from->lab[i].name, base + from->lab[i].at)) {
            return false;
        }
    }
    for (int i = 0; i < from->nfix; i++) {
        const StFix *f = &from->fix[i];
        if (!add_fix(to, base + f->at, base + f->second, f->absolute,
                     f->label, f->file, f->line)) {
            return false;
        }
    }
    return true;
}

static char *read_text(const char *path, Ps2Reader reader)
{
    size_t size = 0;
    const u8 *packed = (reader != NULL) ? reader(path, &size) : NULL;
    if (packed != NULL) {
        char *copy = malloc(size + 1);
        if (copy != NULL) {
            memcpy(copy, packed, size);
            copy[size] = 0;
        }
        return copy;
    }
    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return NULL;
    }
    char *buf = NULL;
    long len = (fseek(fh, 0, SEEK_END) == 0) ? ftell(fh) : -1;
    if (len >= 0 && fseek(fh, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)len + 1);
    }
    if (buf != NULL && fread(buf, 1, (size_t)len, fh) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    if (buf != NULL) {
        buf[len] = 0;
    }
    fclose(fh);
    return buf;
}

static bool is_name(const char *w)
{
    if (!((w[0] >= 'A' && w[0] <= 'Z') || (w[0] >= 'a' && w[0] <= 'z')
          || w[0] == '_')) {
        return false;
    }
    for (const char *p = w + 1; *p; p++) {
        bool ok = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')
               || (*p >= '0' && *p <= '9') || *p == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
        || (c >= 'A' && c <= 'F');
}

/* $hex or decimal digits; -1 when it is not one, a big number when past
 * any byte or word (the caller's range check names it). */
static long number(const char *w)
{
    const char *p = (w[0] == '$') ? w + 1 : w;
    if (*p == 0) {
        return -1;
    }
    for (const char *q = p; *q; q++) {
        if (w[0] == '$' ? !is_hex(*q) : !(*q >= '0' && *q <= '9')) {
            return -1;
        }
    }
    if (strlen(p) > 7) {
        return 0x7FFFFFFFL;
    }
    return strtol(p, NULL, (w[0] == '$') ? 16 : 10);
}

/* +N or -N: -129 when it is not one. */
static long signed_number(const char *w)
{
    if (w[0] != '+' && w[0] != '-') {
        return -129;
    }
    if (w[1] == 0) {
        return -129;
    }
    for (const char *q = w + 1; *q; q++) {
        if (!(*q >= '0' && *q <= '9')) {
            return -129;
        }
    }
    if (strlen(w) > 8) {
        return 0x7FFFFFFFL;
    }
    return strtol(w, NULL, 10);
}

static bool ends_colon_name(const char *w, char out[ST_NAME])
{
    size_t n = strlen(w);
    if (n < 2 || n >= ST_NAME || w[n - 1] != ':') {
        return false;
    }
    memcpy(out, w, n - 1);
    out[n - 1] = 0;
    return is_name(out);
}

/* ------------------------------------------------------------------ */
/* Lines and words                                                     */

typedef struct {
    St   *st;
    int   file, line;
    char  buf[ST_LINE];
    char *tok[ST_TOKENS];
    int   n, i;
} Cur;

/* (build 139b) The words of `s` (inside c->buf), cut up in place, into
 * c->tok: next_line's, and each cell of a tracker file's grid line. A word
 * that starts with # ends them (a note's # is inside its word). False,
 * said, past the limits. */
static bool split_words(Cur *c, char *s)
{
    c->n = 0;
    c->i = 0;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '\r') {
            *s++ = 0;
        }
        if (*s == 0) {
            break;
        }
        if (*s == '#') {
            break;
        }
        if (c->n == ST_TOKENS) {
            return fail(c->st, c->file, c->line, "more than %d words",
                        ST_TOKENS);
        }
        c->tok[c->n++] = s;
        while (*s && *s != ' ' && *s != '\t' && *s != '\r') {
            s++;
        }
        if (s - c->tok[c->n - 1] >= ST_NAME) {
            return fail(c->st, c->file, c->line, "a word past %d characters",
                        ST_NAME - 1);
        }
    }
    return true;
}

/* The next line of `*text` into c, its words split; false at the end. */
static bool next_line(const char **text, Cur *c, bool *ok)
{
    const char *p = *text;
    if (*p == 0) {
        return false;
    }
    const char *e = strchr(p, '\n');
    size_t n = (e != NULL) ? (size_t)(e - p) : strlen(p);
    *text = (e != NULL) ? e + 1 : p + n;
    c->line++;
    c->n = 0;
    c->i = 0;
    if (n >= ST_LINE) {
        *ok = fail(c->st, c->file, c->line, "a line of %zu characters, past "
                   "%d", n, ST_LINE - 1);
        return true;
    }
    memcpy(c->buf, p, n);
    c->buf[n] = 0;
    if (!split_words(c, c->buf)) {
        *ok = false;
    }
    return true;
}

static bool more(const Cur *c)
{
    return c->i < c->n;
}

static const char *peek(const Cur *c)
{
    return more(c) ? c->tok[c->i] : NULL;
}

static bool take(Cur *c, const char *what, const char **out)
{
    *out = "";
    if (!more(c)) {
        return fail(c->st, c->file, c->line, "%s missing at the end of the "
                    "line", what);
    }
    *out = c->tok[c->i++];
    return true;
}

static bool take_byte(Cur *c, const char *what, long top, int *out)
{
    const char *w;
    if (!take(c, what, &w)) {
        return false;
    }
    long v = number(w);
    if (v < 0) {
        return fail(c->st, c->file, c->line, "%s: '%s' is not a number",
                    what, w);
    }
    if (v > top) {
        return fail(c->st, c->file, c->line, "%s: %s is past %ld", what, w,
                    top);
    }
    *out = (int)v;
    return true;
}

static bool take_signed(Cur *c, const char *what, int *out)
{
    const char *w;
    if (!take(c, what, &w)) {
        return false;
    }
    long v = signed_number(w);
    if (v == -129) {
        return fail(c->st, c->file, c->line, "%s: '%s' wants a sign, + or -",
                    what, w);
    }
    if (v < -128 || v > 127) {
        return fail(c->st, c->file, c->line, "%s: %s is outside -128 to +127",
                    what, w);
    }
    *out = (int)(v & 0xFF);
    return true;
}

static bool take_name(Cur *c, const char *what, const char **out)
{
    if (!take(c, what, out)) {
        return false;
    }
    if (!is_name(*out)) {
        return fail(c->st, c->file, c->line, "%s: '%s' is not a name", what,
                    *out);
    }
    return true;
}

static bool take_on_off(Cur *c, const char *what, int *out)
{
    const char *w = peek(c);
    if (w != NULL && strcmp(w, "on") == 0) {
        c->i++;
        *out = 1;
        return true;
    }
    if (w != NULL && strcmp(w, "off") == 0) {
        c->i++;
        *out = 0;
        return true;
    }
    if (w != NULL && number(w) >= 0) {
        return take_byte(c, what, 255, out);
    }
    return fail(c->st, c->file, c->line, "%s: '%s' is not on or off", what,
                w ? w : "");
}

/* ------------------------------------------------------------------ */
/* Names                                                               */

static int note_byte(const St *st, const char *w)
{
    if (strcmp(w, "rest") == 0) {
        return 0x80;
    }
    for (int n = 1; n < ST_NOTES; n++) {
        if (strcmp(st->note[n], w) == 0) {
            return 0x80 + n;
        }
    }
    return -1;
}

static int drum_byte(const St *st, const char *w)
{
    for (int i = 0; i < st->ndrums; i++) {
        if (strcmp(st->drum[i].name, w) == 0) {
            return st->drum[i].byte;
        }
    }
    return -1;
}

static int instrument_number(const St *st, const char *w)
{
    for (int i = 0; i < st->nins; i++) {
        if (strcmp(st->ins_name[i], w) == 0) {
            return i;
        }
    }
    return -1;
}

static int sound_id(const St *st, const char *w)
{
    for (int i = 0; i < st->nsounds; i++) {
        if (strcmp(st->sound[i].name, w) == 0) {
            return FIRST_ID + i;
        }
    }
    return -1;
}

static bool take_instrument(Cur *c, int *out)
{
    const char *w;
    if (!take(c, "instrument: a name", &w)) {
        return false;
    }
    *out = instrument_number(c->st, w);
    if (*out < 0) {
        return fail(c->st, c->file, c->line, "instrument: no instrument is "
                    "named '%s' (fm_instruments.json)", w);
    }
    return true;
}

static bool take_envelope(Cur *c, int *out)
{
    const char *w;
    if (!take(c, "mod: an envelope's name or off", &w)) {
        return false;
    }
    if (strcmp(w, "off") == 0) {
        *out = 0;
        return true;
    }
    for (int i = 0; i < c->st->nenv; i++) {
        if (strcmp(c->st->env_name[i], w) == 0) {
            *out = i + 1;
            return true;
        }
    }
    return fail(c->st, c->file, c->line, "mod: no envelope is named '%s' "
                "(envelopes.txt)", w);
}

/* note_frequencies.json's names: entry n is byte $80 + n; entries 1-63
 * each a semitone above the last, then on a semitone a step to $DF. */
static int semitone(const char *s)
{
    static const char *names[12] = { "C", "C#", "D", "D#", "E", "F", "F#",
                                     "G", "G#", "A", "A#", "B" };
    for (int k = 11; k >= 0; k--) {
        size_t n = strlen(names[k]);
        if (strncmp(s, names[k], n) == 0) {
            const char *o = s + n;
            if (*o == 0) {
                return -10000;
            }
            char *end = NULL;
            long oct = strtol(o, &end, 10);
            if (end == NULL || *end != 0 || oct < -1 || oct > 20) {
                return -10000;
            }
            return k + 12 * (int)oct;
        }
    }
    return -10000;
}

static bool load_note_names(St *st, const char *path)
{
    static const char *names[12] = { "C", "C#", "D", "D#", "E", "F", "F#",
                                     "G", "G#", "A", "A#", "B" };
    char *text = read_text(path, st->reader);
    if (text == NULL) {
        return fail(st, -1, 0, "cannot read %s", path);
    }
    size_t where = 0;
    JsonValue *root = json_parse(text, &where);
    free(text);
    if (root == NULL) {
        return fail(st, -1, 0, "%s is not JSON (at byte %zu)", path, where);
    }
    const JsonValue *e = json_get(root, "entries");
    bool ok = json_length(e) >= 64;
    int first = ok ? semitone(json_str(json_at(e, 1), "note", "")) : 0;
    strcpy(st->note[0], "silence");
    for (int n = 1; ok && n < ST_NOTES; n++) {
        int semi = first + n - 1;
        if (n < 64 && semitone(json_str(json_at(e, n), "note", "")) != semi) {
            fail(st, -1, 0, "%s: entry %d's note is not a semitone above "
                 "entry %d's", path, n, n - 1);
            ok = false;
            break;
        }
        int octave = (semi >= 0) ? semi / 12 : -((11 - semi) / 12);
        if (octave < -9 || octave > 99) {
            fail(st, -1, 0, "%s: entry 1's note is out of range", path);
            ok = false;
            break;
        }
        snprintf(st->note[n], sizeof st->note[n], "%s%d",
                 names[((semi % 12) + 12) % 12], octave % 100);
    }
    json_free(root);
    return ok;
}

static bool load_drum_names(St *st, const char *path)
{
    char *text = read_text(path, st->reader);
    if (text == NULL) {
        return fail(st, -1, 0, "cannot read %s", path);
    }
    size_t where = 0;
    JsonValue *root = json_parse(text, &where);
    free(text);
    if (root == NULL) {
        return fail(st, -1, 0, "%s is not JSON (at byte %zu)", path, where);
    }
    const JsonValue *list = json_get(root, "drums");
    int n = json_length(list);
    bool ok = n > 0 && n <= ST_DRUMS;
    for (int i = 0; ok && i < n; i++) {
        const JsonValue *d = json_at(list, i);
        long b = json_int(d, "byte", -1);
        const char *nm = json_str(d, "name", "");
        if (b < 0x80 || b > 0xFF || !is_name(nm) || strlen(nm) >= ST_NAME) {
            fail(st, -1, 0, "%s: drum %d needs a byte $80-$FF and a name",
                 path, i);
            ok = false;
            break;
        }
        st->drum[i].byte = (u8)b;
        snprintf(st->drum[i].name, ST_NAME, "%s", nm);
    }
    st->ndrums = ok ? n : 0;
    json_free(root);
    return ok;
}

static int add_file(St *st, const char *path)
{
    if (st->nfiles == ST_FILES) {
        fail(st, -1, 0, "more than %d files", ST_FILES);
        return -1;
    }
    snprintf(st->path[st->nfiles], sizeof st->path[0], "%s", path);
    return st->nfiles++;
}

/* ------------------------------------------------------------------ */
/* sounds.txt                                                          */

static bool read_stream_line(Cur *c, StChunk *to);

static bool read_sounds(St *st, const char *dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/sounds.txt", dir);
    int file = add_file(st, path);
    if (file < 0) {
        return false;
    }
    char *text = read_text(path, st->reader);
    if (text == NULL) {
        return fail(st, -1, 0, "cannot read %s", path);
    }
    Cur c = { .st = st, .file = file };
    const char *p = text;
    bool ok = true;
    int section = 0;            /* 0 ids, 1 instruments, 2 streams */
    st->gap = -1;
    for (int i = 0; i < 256; i++) {
        st->same[i] = -1;
        st->share[i] = 0;
    }
    while (ok && next_line(&p, &c, &ok)) {
        if (!ok || c.n == 0) {
            continue;
        }
        const char *w = c.tok[0];
        if (w[0] == '$' && section == 0) {
            int id, pr;
            const char *nm;
            ok = take_byte(&c, "the sound id", 255, &id)
              && take_name(&c, "the sound's name", &nm)
              && take_byte(&c, "its priority", 255, &pr);
            if (ok && more(&c)) {
                ok = fail(st, file, c.line, "a sound line is: id, name, "
                          "priority");
            }
            if (ok && id != FIRST_ID + st->nsounds) {
                ok = fail(st, file, c.line, "$%02X: the ids run from $81 in "
                          "order; $%02X next", id, FIRST_ID + st->nsounds);
            }
            if (ok && st->nsounds == ST_SOUNDS) {
                ok = fail(st, file, c.line, "past $FF");
            }
            if (ok) {
                snprintf(st->sound[st->nsounds].name, ST_NAME, "%s", nm);
                st->sound[st->nsounds].priority = (u8)pr;
                st->sound[st->nsounds].file = file;
                st->sound[st->nsounds].line = c.line;
                st->nsounds++;
            }
        } else if (strcmp(w, "song_file") == 0 && section == 0) {
            const char *nm;
            c.i = 1;
            ok = take_name(&c, "song_file: a name", &nm);
            if (ok && more(&c)) {
                ok = fail(st, file, c.line, "song_file NAME: one name a line");
            }
            if (ok && st->nsong_files == ST_SONGS) {
                ok = fail(st, file, c.line, "more than %d song files",
                          ST_SONGS);
            }
            if (ok) {
                snprintf(st->song_file[st->nsong_files], ST_NAME, "%s", nm);
                st->song_file_line[st->nsong_files++] = c.line;
            }
        } else if (strcmp(w, "instruments") == 0 && c.n == 1) {
            section = 1;
        } else if (strcmp(w, "same") == 0 && section == 1) {
            int group[256];
            int n = 0;
            c.i = 1;
            while (ok && more(&c)) {
                const char *x;
                ok = take(&c, "same", &x);
                int k = ok ? instrument_number(st, x) : -1;
                if (ok && k < 0) {
                    ok = fail(st, file, c.line, "same: no instrument is named "
                              "'%s'", x);
                }
                if (ok && n < 256) {
                    group[n++] = k;
                }
            }
            for (int k = 1; ok && k < n; k++) {
                if (group[k] != group[0] + k) {
                    ok = false;
                }
            }
            if (ok && n < 2) {
                ok = false;
            }
            if (!ok) {
                fail(st, file, c.line, "same: two or more instruments, one "
                     "after another");
                break;
            }
            for (int k = 1; k < n; k++) {
                st->same[group[k]] = group[0];
            }
        } else if (strcmp(w, "share") == 0 && section == 1) {
            const char *a, *b;
            int n;
            c.i = 1;
            ok = take(&c, "share A", &a) && take(&c, "share B", &b)
              && take_byte(&c, "share: how many bytes",
                           INSTRUMENT_REGISTERS - 1, &n);
            int ia = ok ? instrument_number(st, a) : -1;
            int ib = ok ? instrument_number(st, b) : -1;
            if (ok && (ia < 0 || ib < 0)) {
                ok = fail(st, file, c.line, "share: no instrument is named "
                          "'%s'", (ia < 0) ? a : b);
            }
            if (ok && ib != ia + 1) {
                ok = fail(st, file, c.line, "share: %s is not the instrument "
                          "after %s", b, a);
            }
            if (ok) {
                st->share[ib] = n;
            }
        } else if (strcmp(w, "driver_code") == 0) {
            int n;
            c.i = 1;
            ok = take_byte(&c, "driver_code: its bytes", 0xFFFF, &n);
            st->gap = n;
        } else if (strcmp(w, "streams") == 0 && c.n == 1) {
            section = 2;
        } else if (section == 2 && (strcmp(w, "block") == 0
                                    || strcmp(w, "bytes") == 0)) {
            if (st->nplace == ST_LAYOUT) {
                ok = fail(st, file, c.line, "more than %d streams", ST_LAYOUT);
                break;
            }
            StPlace *pl = &st->place[st->nplace++];
            pl->file = file;
            pl->line = c.line;
            if (w[1] == 'l') {
                const char *nm;
                c.i = 1;
                ok = take_name(&c, "block: a name", &nm);
                pl->is_block = true;
                snprintf(pl->name, ST_NAME, "%s", nm);
            } else {
                ok = read_stream_line(&c, &pl->bytes);
            }
        } else {
            ok = fail(st, file, c.line, "'%s': not a line sounds.txt has", w);
        }
    }
    free(text);
    if (ok && st->gap < 0) {
        ok = fail(st, file, c.line, "no driver_code line");
    }
    if (ok && st->nsounds < PRIORITIES) {
        ok = fail(st, file, c.line, "the sound ids stop before $D8");
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* envelopes.txt                                                       */

static bool read_envelopes(St *st, const char *dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/envelopes.txt", dir);
    int file = add_file(st, path);
    if (file < 0) {
        return false;
    }
    char *text = read_text(path, st->reader);
    if (text == NULL) {
        return fail(st, -1, 0, "cannot read %s", path);
    }
    Cur c = { .st = st, .file = file };
    const char *p = text;
    bool ok = true;
    StChunk *e = NULL;
    while (ok && next_line(&p, &c, &ok)) {
        if (!ok || c.n == 0) {
            continue;
        }
        if (strcmp(c.tok[0], "envelope") == 0) {
            const char *nm;
            c.i = 1;
            ok = take_name(&c, "envelope: a name", &nm);
            if (ok && st->nenv == ST_ENVELOPES) {
                ok = fail(st, file, c.line, "more than %d envelopes",
                          ST_ENVELOPES);
            }
            if (ok) {
                snprintf(st->env_name[st->nenv], ST_NAME, "%s", nm);
                e = &st->env[st->nenv++];
            }
            continue;
        }
        if (e == NULL) {
            ok = fail(st, file, c.line, "a line before the first `envelope`");
            break;
        }
        size_t tl = strlen(c.tok[0]);
        if (c.n == 1 && tl > 1 && c.tok[0][tl - 1] == ':') {
            c.tok[0][tl - 1] = 0;
            ok = add_label(e, c.tok[0], e->len);
            continue;
        }
        while (ok && more(&c)) {
            const char *w = c.tok[c.i++];
            long v;
            if (strcmp(w, "depth") == 0) {
                int d;
                ok = take_signed(&c, "depth", &d) && put_byte(e, 0x84)
                  && put_byte(e, (u8)d);
            } else if (strcmp(w, "jump") == 0) {
                const char *l;
                ok = take_name(&c, "jump", &l) && put_byte(e, 0x85)
                  && add_fix(e, e->len, 0, false, l, file, c.line)
                  && put_byte(e, 0);
            } else if (strcmp(w, "restart") == 0) {
                ok = put_byte(e, 0x80);
            } else if (strcmp(w, "hold") == 0) {
                ok = put_byte(e, 0x81);
            } else if (strcmp(w, "stop") == 0) {
                ok = put_byte(e, 0x83);
            } else if ((v = signed_number(w)) != -129
                       || (v = number(w), w[0] >= '0' && w[0] <= '9'
                           && v >= 0)) {
                u8 b = (u8)(v & 0xFF);
                if (v < -128 || v > 127 || b == 0x80 || b == 0x81
                    || b == 0x83 || b == 0x84 || b == 0x85) {
                    ok = fail(st, file, c.line, "%s: a step is -128 to 127, "
                              "but not -128, -127, -125, -124 or -123 (the "
                              "codes' bytes)", w);
                } else {
                    ok = put_byte(e, b);
                }
            } else {
                ok = fail(st, file, c.line, "'%s' is not a step or an "
                          "envelope word", w);
            }
        }
    }
    free(text);
    /* Each envelope's jumps to its own labels: the byte is the index. */
    for (int i = 0; ok && i < st->nenv; i++) {
        StChunk *x = &st->env[i];
        for (int f = 0; ok && f < x->nfix; f++) {
            int k = -1;
            for (int l = 0; l < x->nlab; l++) {
                if (strcmp(x->lab[l].name, x->fix[f].label) == 0) {
                    k = (int)x->lab[l].at;
                }
            }
            if (k < 0 || k > 255) {
                ok = fail(st, x->fix[f].file, x->fix[f].line, "jump: no "
                          "label %s in envelope %s", x->fix[f].label,
                          st->env_name[i]);
            } else {
                x->data[x->fix[f].at] = (u8)k;
            }
        }
        x->nfix = 0;
        x->nlab = 0;
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* Streams                                                             */

static const char *kPanAnim[] = { "off", "sweep", "cycle", "left_first",
                                  "right_first", "sweep_5", "sweep_6" };

static bool emit(Cur *c, StChunk *to, const u8 *b, u32 n)
{
    if (!put(to, b, n)) {
        return fail(c->st, c->file, c->line, "out of memory");
    }
    return true;
}

/* A flag word's bytes: 1 done, 0 not a flag word, -1 refused. */
static int read_flag(Cur *c, StChunk *to, const char *w)
{
    u8 b[6];
    int v, x;
    const char *s;
    bool ok = true;
    int n = 0;

    if (strcmp(w, "music") == 0) {
        ok = take(c, "music: stop or restart", &s);
        if (ok && strcmp(s, "stop") == 0) {
            b[0] = 0xE0; b[1] = 0; n = 2;
        } else if (ok && strcmp(s, "restart") == 0) {
            v = 1;
            if (more(c) && number(peek(c)) >= 0) {
                ok = take_byte(c, "music restart", 255, &v);
            }
            if (ok && v == 0) {
                ok = fail(c->st, c->file, c->line, "music restart: 0 is "
                          "`music stop`");
            }
            b[0] = 0xE0; b[1] = (u8)v; n = 2;
        } else if (ok) {
            ok = fail(c->st, c->file, c->line, "music: '%s' is not stop or "
                      "restart", s);
        }
    } else if (strcmp(w, "end2") == 0) {
        b[0] = 0xE1; n = 1;
    } else if (strcmp(w, "sound") == 0) {
        s = peek(c);
        if (s != NULL && number(s) >= 0) {
            ok = take_byte(c, "sound", 255, &v);
        } else {
            ok = take(c, "sound: a sound's name", &s);
            v = ok ? sound_id(c->st, s) : 0;
            if (ok && v < 0) {
                ok = fail(c->st, c->file, c->line, "sound: no sound is named "
                          "'%s' (sounds.txt)", s);
            }
        }
        b[0] = 0xE2; b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "pan_anim") == 0) {
        s = peek(c);
        v = -1;
        for (int k = 0; s != NULL && k < 7; k++) {
            if (strcmp(s, kPanAnim[k]) == 0) {
                v = k;
            }
        }
        if (v >= 0) {
            c->i++;
        } else {
            ok = take_byte(c, "pan_anim: a type", 255, &v);
        }
        ok = ok && take_byte(c, "pan_anim: its timeout", 255, &x);
        b[0] = 0xE3; b[1] = (u8)v; b[2] = (u8)x; n = 3;
    } else if (strcmp(w, "tempo") == 0) {
        ok = take_byte(c, "tempo", 255, &v);
        b[0] = 0xE4; b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "chord") == 0) {
        if (peek(c) != NULL && strcmp(peek(c), "off") == 0) {
            c->i++;
            v = 0;
        } else {
            /* (build 184) at most SMPS_CHORD_MAX notes: the chord voices
             * past the chip stop there (smps.h); until 184 any byte, and a
             * chord past the driver's tracks wrote past its RAM */
            ok = take_byte(c, "chord: the notes", SMPS_CHORD_MAX, &v);
        }
        b[0] = 0xE5; b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "hold") == 0) {
        b[0] = 0xE6; n = 1;
    } else if (strcmp(w, "tie") == 0) {
        ok = take_byte(c, "tie: a length", 127, &v);
        b[0] = 0xE6; b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "release") == 0) {
        b[0] = 0xE7; n = 1;
        if (more(c) && number(peek(c)) >= 0) {
            ok = take_byte(c, "release: a length", 127, &v);
            b[1] = (u8)v; n = 2;
        }
    } else if (strcmp(w, "pause") == 0) {
        ok = take_on_off(c, "pause", &v);
        b[0] = 0xE8; b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "comm") == 0 || strcmp(w, "fms") == 0
               || strcmp(w, "ams") == 0 || strcmp(w, "noise") == 0 || strcmp(w, "psg_instrument") == 0
               || strcmp(w, "tick") == 0) {
        u8 code = (w[0] == 'c') ? 0xE9 : (w[0] == 'f') ? 0xEA
                : (w[0] == 'a') ? 0xEB : (w[0] == 'n') ? 0xF3
                : (w[0] == 'p') ? 0xF5 : 0xFA;
        ok = take_byte(c, w, 255, &v);
        b[0] = code; b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "volume") == 0) {
        s = peek(c);
        if (s != NULL && (s[0] == '+' || s[0] == '-')) {
            ok = take_signed(c, "volume", &v);
            b[0] = 0xEC;
        } else {
            ok = take_byte(c, "volume", 255, &v);
            b[0] = 0xF0;
        }
        b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "unused_ed") == 0) {
        b[0] = 0xED; n = 1;
    } else if (strcmp(w, "legato") == 0) {
        ok = take_on_off(c, "legato", &v);
        b[0] = 0xEE; b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "instrument") == 0) {
        ok = take_instrument(c, &v);
        b[0] = 0xEF; b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "nop") == 0) {
        b[0] = 0xF1; n = 1;
    } else if (strcmp(w, "end") == 0) {
        b[0] = 0xF2; n = 1;
    } else if (strcmp(w, "mod") == 0) {
        ok = take_envelope(c, &v);
        b[0] = 0xF4; b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "goto") == 0 || strcmp(w, "call") == 0) {
        ok = take_name(c, w[0] == 'g' ? "goto: a label" : "call: a label", &s)
          && add_fix(to, to->len + 1, to->len + 2, false, s, c->file, c->line);
        b[0] = (w[0] == 'g') ? 0xF6 : 0xF8; b[1] = 0; b[2] = 0; n = 3;
    } else if (strcmp(w, "loop") == 0) {
        const char *t = NULL;
        int slot = 0;
        ok = take_name(c, "loop: a label", &s)
          && take_byte(c, "loop: how many times", 255, &v)
          && take(c, "loop: `times`", &t);
        if (ok && strcmp(t, "times") != 0) {
            ok = fail(c->st, c->file, c->line, "loop LABEL N times: `times` "
                      "missing");
        }
        if (ok && peek(c) != NULL && strcmp(peek(c), "counter") == 0) {
            c->i++;
            ok = take_byte(c, "loop counter", 255, &slot);
        }
        ok = ok && add_fix(to, to->len + 3, to->len + 4, false, s, c->file,
                           c->line);
        b[0] = 0xF7; b[1] = (u8)slot; b[2] = (u8)v; b[3] = 0; b[4] = 0;
        n = 5;
    } else if (strcmp(w, "return") == 0) {
        b[0] = 0xF9; n = 1;
    } else if (strcmp(w, "transpose") == 0) {
        ok = take_signed(c, "transpose", &v);
        b[0] = 0xFB; b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "pitch_slide") == 0 || strcmp(w, "raw_freq") == 0) {
        ok = take_on_off(c, w, &v);
        b[0] = (w[0] == 'p') ? 0xFC : 0xFD; b[1] = (u8)v; n = 2;
    } else if (strcmp(w, "fm3_special") == 0) {
        b[0] = 0xFE; n = 5;
        for (int k = 1; ok && k < 5; k++) {
            ok = take_byte(c, "fm3_special", 255, &v);
            b[k] = (u8)v;
        }
    } else if (strcmp(w, "pan") == 0) {
        static const struct { const char *w; u8 b; } pans[] = {
            { "center", 0xC0 }, { "left", 0x80 }, { "right", 0x40 },
            { "none", 0x00 } };
        s = peek(c);
        v = -1;
        for (int k = 0; s != NULL && k < 4; k++) {
            if (strcmp(s, pans[k].w) == 0) {
                v = pans[k].b;
            }
        }
        if (v >= 0) {
            c->i++;
        } else if (s != NULL && number(s) >= 0) {
            ok = take_byte(c, "pan", 255, &v);
        } else {
            ok = fail(c->st, c->file, c->line, "pan: '%s' is not left, right, "
                      "center or none", s ? s : "");
        }
        b[0] = 0xFF; b[1] = (u8)v; n = 2;
    } else {
        return 0;
    }
    if (!ok) {
        return -1;
    }
    return emit(c, to, b, (u32)n) ? 1 : -1;
}

/* One line of a stream onto `to`: labels, flags, notes, bytes. */
static bool read_stream_line(Cur *c, StChunk *to)
{
    bool ok = true;
    while (ok && more(c)) {
        const char *w = c->tok[c->i++];
        char lbl[ST_NAME];
        int v;

        if (ends_colon_name(w, lbl)) {
            ok = add_label(to, lbl, to->len);
            continue;
        }
        if (strcmp(w, "bytes") == 0) {
            if (!more(c)) {
                return fail(c->st, c->file, c->line, "bytes: none given");
            }
            while (ok && more(c)) {
                ok = take_byte(c, "bytes", 255, &v) && put_byte(to, (u8)v);
            }
            continue;
        }
        int f = read_flag(c, to, w);
        if (f < 0) {
            return false;
        }
        if (f > 0) {
            continue;
        }
        if (strcmp(w, "again") == 0) {
            ok = take_byte(c, "again: a length", 127, &v)
              && put_byte(to, (u8)v);
            continue;
        }
        if (strcmp(w, "freq") == 0) {
            const char *s;
            int ln, sp;
            u8 b[4];
            u32 n = 0;
            if (!take(c, "freq: the chip's frequency word", &s)) {
                return false;
            }
            size_t sl = strlen(s);
            long word = (s[0] == '$' && sl >= 2 && sl <= 5) ? number(s) : -1;
            if (word < 0) {
                return fail(c->st, c->file, c->line, "freq: '%s' is not a "
                            "word like $2060", s);
            }
            if ((word >> 8) >= 0xE0) {
                return fail(c->st, c->file, c->line, "freq: %s starts with "
                            "$%02lX, which the driver reads as a flag", s,
                            word >> 8);
            }
            if (!take_byte(c, "freq: a length", 255, &ln)) {
                return false;
            }
            b[n++] = (u8)(word >> 8);
            b[n++] = (u8)word;
            if (peek(c) != NULL && strcmp(peek(c), "slide") == 0) {
                c->i++;
                if (!take_signed(c, "slide: a speed", &sp)) {
                    return false;
                }
                b[n++] = (u8)sp;
            }
            b[n++] = (u8)ln;
            ok = emit(c, to, b, n);
            continue;
        }

        /* A note (or chord, rest, drum), its length, a slide. */
        u8 data[ST_TOKENS + 4];
        u32 n = 0;
        char part[ST_NAME];
        const char *p = w;
        int k = 0;
        while (ok) {
            const char *plus = strchr(p, '+');
            size_t len = (plus != NULL) ? (size_t)(plus - p) : strlen(p);
            memcpy(part, p, len);
            part[len] = 0;
            int byte = note_byte(c->st, part);
            if (k == 0 && byte < 0) {
                byte = drum_byte(c->st, part);
            }
            if (byte < 0) {
                return fail(c->st, c->file, c->line, (k == 0)
                            ? "'%s' is not a note, a drum or a word this file "
                              "knows" : "'%s': '%s' is not a note", w, part);
            }
            if (n < sizeof data - 4) {
                data[n++] = (u8)byte;
            }
            k++;
            if (plus == NULL) {
                break;
            }
            p = plus + 1;
        }
        if (k > 1 && data[0] == 0x80) {
            return fail(c->st, c->file, c->line, "'%s': a rest takes no chord "
                        "parts", w);
        }
        char inner[ST_NAME] = "";
        int ln = -1;
        if (more(c) && c->i + 1 < c->n && ends_colon_name(peek(c), inner)
            && number(c->tok[c->i + 1]) >= 0) {
            c->i++;             /* a label on the length byte */
        } else {
            inner[0] = 0;
        }
        if (more(c) && number(peek(c)) >= 0) {
            if (!take_byte(c, "a length", 255, &ln)) {
                return false;
            }
        }
        if (peek(c) != NULL && strcmp(peek(c), "slide") == 0) {
            int sp, unused = 0;
            c->i++;
            if (ln < 0 || inner[0] != 0) {
                return fail(c->st, c->file, c->line, "%s: a sliding note "
                            "needs its length, and no label inside it", w);
            }
            if (!take_signed(c, "slide: a speed", &sp)) {
                return false;
            }
            if (peek(c) != NULL && strcmp(peek(c), "unused") == 0) {
                c->i++;
                if (!take_byte(c, "unused", 255, &unused)) {
                    return false;
                }
            }
            data[n++] = (u8)unused;
            data[n++] = (u8)sp;
            data[n++] = (u8)ln;
            ok = emit(c, to, data, n);
            continue;
        }
        if (ln > 127) {
            return fail(c->st, c->file, c->line, "%s %d: a length is 0-127", w,
                        ln);
        }
        if (inner[0] != 0) {
            u8 l8 = (u8)ln;
            ok = emit(c, to, data, n) && add_label(to, inner, to->len)
              && emit(c, to, &l8, 1);
            continue;
        }
        if (ln >= 0) {
            data[n++] = (u8)ln;
        }
        ok = emit(c, to, data, n);
    }
    return ok;
}

/* A `track` or `unused_track` line to its nine bytes and start label. */
static bool read_header(Cur *c, StTrack *t)
{
    static const struct { const char *w; u8 bit; } bits[] = {
        { "pitch_slide", 0x20 }, { "raw_freq", 0x08 }, { "flag_6", 0x40 },
        { "flag_4", 0x10 }, { "lent", 0x04 }, { "held", 0x02 },
        { "fm3_special", 0x01 } };
    static const struct { const char *w; u8 b; } chans[] = {
        { "drums", 0x80 }, { "fm1", 0 }, { "fm2", 1 }, { "fm3", 2 },
        { "fm4", 4 }, { "fm5", 5 }, { "fm6", 6 } };
    const char *ch, *label;
    int got[5] = { -1, -1, -1, -1, -1 };    /* tick transpose mod ins vol */
    static const char *keys[5] = { "tick", "transpose", "mod", "instrument",
                                   "volume" };
    u8 flags = 0x80;
    int chan = -1;

    c->i = 1;
    if (!take(c, "the channel", &ch)) {
        return false;
    }
    for (int k = 0; k < 7; k++) {
        if (strcmp(ch, chans[k].w) == 0) {
            chan = chans[k].b;
        }
    }
    if (chan < 0) {
        return fail(c->st, c->file, c->line, "'%s' is not a channel (drums, "
                    "fm1-fm6)", ch);
    }
    if (!take_name(c, "where the stream starts", &label)) {
        return false;
    }
    while (more(c)) {
        const char *k = c->tok[c->i++];
        int which = -1;
        bool ok = true;
        for (int j = 0; j < 5; j++) {
            if (strcmp(k, keys[j]) == 0) {
                which = j;
            }
        }
        if (which >= 0 && got[which] >= 0) {
            return fail(c->st, c->file, c->line, "%s given twice", k);
        }
        if (which == 0) {
            ok = take_byte(c, "tick", 255, &got[0]);
        } else if (which == 1) {
            ok = take_signed(c, "transpose", &got[1]);
        } else if (which == 2) {
            ok = take_envelope(c, &got[2]);
        } else if (which == 3) {
            ok = take_instrument(c, &got[3]);
        } else if (which == 4) {
            ok = take_byte(c, "volume", 255, &got[4]);
        } else if (strcmp(k, "not_in_use") == 0) {
            flags &= 0x7F;
        } else {
            int bit = -1;
            for (int j = 0; j < 7; j++) {
                if (strcmp(k, bits[j].w) == 0) {
                    bit = bits[j].bit;
                }
            }
            if (bit < 0) {
                return fail(c->st, c->file, c->line, "'%s' is not a header "
                            "field", k);
            }
            flags |= (u8)bit;
        }
        if (!ok) {
            return false;
        }
    }
    for (int j = 0; j < 5; j++) {
        if (got[j] < 0) {
            return fail(c->st, c->file, c->line, "the header has no %s",
                        keys[j]);
        }
    }
    snprintf(t->label, ST_NAME, "%s", label);
    u8 h[9] = { flags, (u8)chan, (u8)got[0], 0, 0, (u8)got[1], (u8)got[2],
                (u8)got[3], (u8)got[4] };
    memcpy(t->bytes, h, 9);
    t->file = c->file;
    t->line = c->line;
    return true;
}

static StSound *find_sound(StSound *list, int n, const char *name)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].name, name) == 0) {
            return &list[i];
        }
    }
    return NULL;
}

/* (build 139b) Whether a song is read from its tracker file. */
static bool replaced(const St *st, const char *name)
{
    for (int k = 0; k < st->nsrc; k++) {
        if (st->src[k].trk != NULL && strcmp(st->src[k].name, name) == 0) {
            return true;
        }
    }
    return false;
}

/* effects.txt (song = false) or a song file. (build 139b) A song file's
 * song read from its tracker file is passed over: its `song` line and the
 * `track` lines after it. */
static bool read_streams(St *st, const char *path, bool song)
{
    int file = add_file(st, path);
    if (file < 0) {
        return false;
    }
    char *text = read_text(path, st->reader);
    if (text == NULL) {
        return fail(st, -1, 0, "cannot read %s", path);
    }
    Cur c = { .st = st, .file = file };
    const char *p = text;
    bool ok = true;
    StSound *cur = NULL;
    StBlock *block = NULL;
    bool passing = false;       /* (139b) a replaced song's header */
    while (ok && next_line(&p, &c, &ok)) {
        if (!ok || c.n == 0) {
            continue;
        }
        const char *w = c.tok[0];
        if (song && strcmp(w, "song") == 0 && c.n > 1
            && replaced(st, c.tok[1])) {
            passing = true;
            continue;
        }
        if (passing && strcmp(w, "track") == 0) {
            continue;
        }
        passing = false;
        if (song && strcmp(w, "song") == 0) {
            const char *nm;
            if (block != NULL) {
                ok = fail(st, file, c.line, "a `song` line after the blocks");
                break;
            }
            c.i = 1;
            if (!take_name(&c, "song: a name", &nm)) {
                ok = false;
                break;
            }
            if (find_sound(st->songs, st->nsongs, nm) != NULL) {
                ok = fail(st, file, c.line, "song %s given twice", nm);
                break;
            }
            if (st->nsongs == ST_SONGS) {
                ok = fail(st, file, c.line, "more than %d songs", ST_SONGS);
                break;
            }
            cur = &st->songs[st->nsongs++];
            memset(cur, 0, sizeof *cur);
            snprintf(cur->name, ST_NAME, "%s", nm);
            cur->file = file;
            cur->line = c.line;
            int *f[3] = { &cur->tempo, &cur->pal_tempo, &cur->pal_tick };
            static const char *keys[3] = { "tempo", "pal_tempo", "pal_tick" };
            for (int k = 0; k < 3; k++) {
                *f[k] = -1;
            }
            while (ok && more(&c)) {
                const char *k = c.tok[c.i++];
                int which = -1;
                for (int j = 0; j < 3; j++) {
                    if (strcmp(k, keys[j]) == 0) {
                        which = j;
                    }
                }
                if (which < 0 || *f[which] >= 0) {
                    ok = fail(st, file, c.line, "'%s': a song line is `song "
                              "NAME tempo N pal_tempo N pal_tick N`", k);
                } else {
                    ok = take_byte(&c, k, 255, f[which]);
                }
            }
            for (int k = 0; ok && k < 3; k++) {
                if (*f[k] < 0) {
                    ok = fail(st, file, c.line, "song %s: no %s", nm, keys[k]);
                }
            }
            continue;
        }
        if (!song && strcmp(w, "effect") == 0) {
            const char *nm;
            c.i = 1;
            if (!take_name(&c, "effect: a name", &nm)) {
                ok = false;
                break;
            }
            if (find_sound(st->effects, st->neffects, nm) != NULL) {
                ok = fail(st, file, c.line, "effect %s given twice", nm);
                break;
            }
            if (st->neffects == ST_EFFECTS) {
                ok = fail(st, file, c.line, "more than %d effects", ST_EFFECTS);
                break;
            }
            cur = &st->effects[st->neffects++];
            memset(cur, 0, sizeof *cur);
            snprintf(cur->name, ST_NAME, "%s", nm);
            cur->file = file;
            cur->line = c.line;
            continue;
        }
        bool unused = strcmp(w, "unused_track") == 0;
        if (unused || strcmp(w, "track") == 0) {
            if (cur == NULL || block != NULL) {
                ok = fail(st, file, c.line, "a `%s` line outside a song's or "
                          "effect's header", w);
                break;
            }
            if (song && unused) {
                ok = fail(st, file, c.line, "unused_track: effects only");
                break;
            }
            if (cur->stream.len > 0 || cur->stream.nlab > 0) {
                ok = fail(st, file, c.line, "a `%s` line after the effect's "
                          "stream began", w);
                break;
            }
            if (!unused && cur->total > cur->counted) {
                ok = fail(st, file, c.line, "a `track` line after an "
                          "`unused_track`");
                break;
            }
            if (cur->total == ST_TRACKS) {
                ok = fail(st, file, c.line, "more than %d tracks", ST_TRACKS);
                break;
            }
            ok = read_header(&c, &cur->tracks[cur->total]);
            cur->total++;
            if (!unused) {
                cur->counted++;
            }
            continue;
        }
        if (song && strcmp(w, "block") == 0) {
            const char *nm;
            c.i = 1;
            if (!take_name(&c, "block: a name", &nm)) {
                ok = false;
                break;
            }
            for (int i = 0; i < st->nblocks; i++) {
                if (strcmp(st->blocks[i].name, nm) == 0) {
                    ok = fail(st, file, c.line, "block %s given twice", nm);
                }
            }
            if (ok && st->nblocks == ST_BLOCKS) {
                ok = fail(st, file, c.line, "more than %d blocks", ST_BLOCKS);
            }
            if (!ok) {
                break;
            }
            block = &st->blocks[st->nblocks++];
            memset(block, 0, sizeof *block);
            snprintf(block->name, ST_NAME, "%s", nm);
            block->file = file;
            block->line = c.line;
            ok = add_label(&block->chunk, nm, 0);
            continue;
        }
        StChunk *to = song ? (block ? &block->chunk : NULL)
                           : (cur ? &cur->stream : NULL);
        if (to == NULL) {
            ok = fail(st, file, c.line, song ? "a stream line outside a block"
                      : "a stream line before the first `effect`");
            break;
        }
        c.i = 0;
        ok = read_stream_line(&c, to);
    }
    free(text);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Tracker files                                                       */

/* (build 139b) A song's tracker file, songs/<song>.trk (tools/tracker.py;
 * docs/audio.md, "The tracker format" and "Build 139b"), read in place of
 * its song file's stream text: tracker.py's read_tracker, channel_events
 * and channel_stream, each channel's block built straight into the bytes
 * song_text.assemble makes of the stream text tracker.py writes for it --
 * every repeat written out, `again` where the note is the one struck last,
 * a length left out where the driver keeps it (but before an `again` and
 * at the loop's cell), a bare hold after a note marked `hold`, a `nop`
 * between a note marked `cut` and its plain `tie`. The refusals are
 * tracker.py's, by file and line, in its words. A channel's block and its
 * track's start are `trk_<song>_<channel>` (tracker.py's PREFIX), so the
 * other song of a pair file, still read, keeps its own labels. */

#define TK_CHANNELS 7
#define TK_NOTES    SMPS_CHORD_MAX  /* a cell's notes: the lead, a chord's
                                       parts (16; build 184 named it) */
#define TK_PREFIX   "trk_"

enum { TK_NONE, TK_NOTE, TK_DRUM, TK_OFF, TK_TIE };
enum { TK_GO_ON, TK_END, TK_GOTO };

typedef struct {
    int  line;
    int  sec, ch;
    long row, time, delay;
    u8   kind, nnotes, drum, art;   /* art: 0, 'h' hold or 'c' cut */
    u8   notes[TK_NOTES];           /* entries 1-63, the lead's first */
    u8   end;                       /* TK_GO_ON, TK_END or TK_GOTO */
    long go_sec, go_row, go_delay;
    u32  items, nitems;             /* its changes in Tk.pool: each a length
                                       byte, then the flag's bytes */
} TkCell;

typedef struct {
    int  line, index;
    long rows, row, start;
    int  copy[TK_CHANNELS];         /* -1, or the section it plays again */
    bool col[TK_CHANNELS];
    int  cols[TK_CHANNELS];         /* the grid's channels, in its order */
    int  ncols;
    int  cell_from;                 /* its first cell as read */
    int  first[TK_CHANNELS];        /* its cells by channel, once sorted */
    int  count[TK_CHANNELS];
} TkSection;

typedef struct {
    St        *st;
    int        file;
    char       name[ST_NAME];
    int        line;                /* the `song` line */
    int        tempo[3];
    int        nch;
    char       ch[TK_CHANNELS][8];
    u8         hb[TK_CHANNELS][9];  /* each channel's track header */
    int        ch_line[TK_CHANNELS];
    TkSection *sec;
    int        nsec, seccap;
    TkCell    *cell;
    int        ncell, cellcap;
    StChunk    pool;
} Tk;

static const char *kTkChannels[TK_CHANNELS] = { "drums", "fm1", "fm2", "fm3",
                                                "fm4", "fm5", "fm6" };
static const char *kTkTempo[3] = { "tempo", "pal_tempo", "pal_tick" };

/* Digits alone (tracker.py's \d+): their value, a big one past nine of
 * them; -1 when it is not that. */
static long digits(const char *w)
{
    if (*w == 0) {
        return -1;
    }
    for (const char *p = w; *p; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
    }
    if (strlen(w) > 9) {
        return 999999999L;
    }
    return strtol(w, NULL, 10);
}

static int tk_channel_of(const Tk *tk, const char *w)
{
    for (int k = 0; k < tk->nch; k++) {
        if (strcmp(tk->ch[k], w) == 0) {
            return k;
        }
    }
    return -1;
}

/* `C-5`, `C#5` or `C5` to the note's entry (1-95); 0 when it is none. */
static int tk_note(const St *st, const char *w)
{
    char name[8];
    if (strlen(w) == 3 && w[0] >= 'A' && w[0] <= 'G'
        && (w[1] == '-' || w[1] == '#') && w[2] >= '0' && w[2] <= '9') {
        int n = 0;
        name[n++] = w[0];
        if (w[1] == '#') {
            name[n++] = '#';
        }
        name[n++] = w[2];
        name[n] = 0;
        w = name;
    }
    int b = note_byte(st, w);
    return (b > 0x80) ? b - 0x80 : 0;
}

/* `SS.RR` or `SS.RR@D`. */
static bool tk_goto_place(const char *w, TkCell *cell)
{
    char buf[ST_NAME];
    snprintf(buf, sizeof buf, "%s", w);
    char *dot = strchr(buf, '.');
    if (dot == NULL) {
        return false;
    }
    *dot = 0;
    char *at = strchr(dot + 1, '@');
    if (at != NULL) {
        *at = 0;
    }
    cell->go_sec = digits(buf);
    cell->go_row = digits(dot + 1);
    cell->go_delay = (at != NULL) ? digits(at + 1) : 0;
    return cell->go_sec >= 0 && cell->go_row >= 0 && cell->go_delay >= 0;
}

/* One cell's words (tracker.py's parse_cell): 1 a cell, 0 nothing in it,
 * -1 refused. */
static int tk_cell(Tk *tk, Cur *c, bool drums, TkCell *cell)
{
    St *st = tk->st;
    memset(cell, 0, sizeof *cell);
    if (c->n == 0 || (c->n == 1 && strcmp(c->tok[0], "...") == 0)) {
        return 0;
    }
    int kind = -1;              /* nothing struck read yet */
    while (more(c)) {
        const char *w = peek(c);
        if (strcmp(w, "...") == 0) {
            c->i++;
            if (kind < 0) {
                kind = TK_NONE;
            }
            continue;
        }
        if ((kind < 0 || kind == TK_NOTE) && !drums) {
            int n = tk_note(st, w);
            if (n > 0) {
                c->i++;
                if (n > 63) {
                    fail(st, c->file, c->line, "%s: the driver plays B0 to "
                         "C#6 (six bits of a note and its transpose); %s "
                         "would sound as %s", w, w, st->note[n & 0x3F]);
                    return -1;
                }
                if (cell->nnotes == TK_NOTES) {
                    fail(st, c->file, c->line, "more than %d notes in a "
                         "cell", TK_NOTES);
                    return -1;
                }
                cell->notes[cell->nnotes++] = (u8)n;
                kind = TK_NOTE;
                continue;
            }
        }
        if (kind < 0 && drums && drum_byte(st, w) >= 0) {
            c->i++;
            kind = TK_DRUM;
            cell->drum = (u8)drum_byte(st, w);
            continue;
        }
        if (kind < 0 && strcmp(w, "===") == 0) {
            c->i++;
            kind = TK_OFF;
            continue;
        }
        if (kind < 0 && strcmp(w, "tie") == 0) {
            c->i++;
            kind = TK_TIE;
            continue;
        }
        break;
    }
    cell->kind = (kind < 0) ? TK_NONE : (u8)kind;
    cell->items = tk->pool.len;
    while (more(c)) {
        const char *w = c->tok[c->i++];
        if (w[0] == '@' && digits(w + 1) >= 0) {
            cell->delay = digits(w + 1);
            continue;
        }
        if (strcmp(w, "hold") == 0 || strcmp(w, "cut") == 0) {
            if (drums) {
                fail(st, c->file, c->line, "`%s`: a drum is not let go "
                     "early, nor held", w);
                return -1;
            }
            cell->art = (u8)w[0];
            continue;
        }
        if (strcmp(w, "end") == 0) {
            cell->end = TK_END;
            if (more(c)) {
                fail(st, c->file, c->line, "nothing comes after `end`");
                return -1;
            }
            break;
        }
        if (strcmp(w, "goto") == 0) {
            const char *g;
            if (!take(c, "goto: a section and row, SS.RR", &g)) {
                return -1;
            }
            if (!tk_goto_place(g, cell)) {
                fail(st, c->file, c->line, "goto: a section and row, like "
                     "05.03 (or 05.03@2)");
                return -1;
            }
            cell->end = TK_GOTO;
            if (more(c)) {
                fail(st, c->file, c->line, "nothing comes after `goto "
                     "SS.RR`");
                return -1;
            }
            break;
        }
        if (strcmp(w, "transpose") == 0) {
            fail(st, c->file, c->line, "transpose: notes are written as "
                 "they are heard");
            return -1;
        }
        if (strcmp(w, "loop") == 0 || strcmp(w, "call") == 0
            || strcmp(w, "return") == 0 || strcmp(w, "end2") == 0) {
            fail(st, c->file, c->line, "%s: a channel's flow is its "
                 "sections, `goto` and `end`", w);
            return -1;
        }
        u32 at = tk->pool.len;
        if (!put_byte(&tk->pool, 0)) {
            fail(st, c->file, c->line, "out of memory");
            return -1;
        }
        int f = read_flag(c, &tk->pool, w);
        if (f < 0) {
            return -1;
        }
        if (f == 0) {
            fail(st, c->file, c->line, "'%s' is not a note, a drum or a word "
                 "this file knows", w);
            return -1;
        }
        u8 *it = tk->pool.data + at + 1;
        u32 len = tk->pool.len - at - 1;
        if (it[0] == 0xFC || it[0] == 0xFD) {
            fail(st, c->file, c->line, "%s: the tracker has no pitch slides "
                 "or raw frequencies (no song uses them); write this song as "
                 "stream text", w);
            return -1;
        }
        /* As tracker.py's words write them: `hold` and `release` with no
         * length, a sound by its name (the first id of that name). */
        if ((it[0] == 0xE6 || it[0] == 0xE7) && len == 2) {
            tk->pool.len--;
            len = 1;
        }
        if (it[0] == 0xE2 && it[1] >= FIRST_ID
            && it[1] - FIRST_ID < st->nsounds) {
            it[1] = (u8)sound_id(st, st->sound[it[1] - FIRST_ID].name);
        }
        tk->pool.data[at] = (u8)len;
    }
    cell->nitems = tk->pool.len - cell->items;
    if (cell->kind == TK_NONE && (cell->nitems > 0 || cell->art != 0
                                  || cell->delay != 0)
        && cell->end == TK_GO_ON) {
        fail(st, c->file, c->line, "changes need a note, a drum, `===` or "
             "`tie` in their cell (the driver makes them as it reads the next "
             "one)");
        return -1;
    }
    if (cell->kind != TK_NONE && cell->end != TK_GO_ON) {
        fail(st, c->file, c->line, "a channel's `goto` or `end` stands in a "
             "cell of its own");
        return -1;
    }
    return 1;
}

static char *strip(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\v' || *s == '\f') {
        s++;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\v'
                     || s[n - 1] == '\f')) {
        s[--n] = 0;
    }
    return s;
}

/* A grid line: `row | drums | fm1 ...` or a row's cells. */
static bool tk_grid_line(Tk *tk, Cur *c, int cur)
{
    St *st = tk->st;
    char *field[ST_TOKENS + 1];
    int nf = 0;
    field[nf++] = c->buf;
    for (char *q = c->buf; *q; q++) {
        if (*q == '|') {
            if (nf == ST_TOKENS + 1) {
                return fail(st, c->file, c->line, "more than %d cells",
                            ST_TOKENS);
            }
            *q = 0;
            field[nf++] = q + 1;
        }
    }
    if (cur < 0) {
        return fail(st, c->file, c->line, "a grid line before the first "
                    "`section`");
    }
    TkSection *s = &tk->sec[cur];
    char *first = strip(field[0]);
    if (strcmp(first, "row") == 0) {
        if (s->ncols > 0) {
            return fail(st, c->file, c->line, "a second `row |` line in the "
                        "section");
        }
        for (int k = 1; k < nf; k++) {
            if (!split_words(c, field[k])) {
                return false;
            }
            if (c->n == 0) {
                continue;
            }
            int ch = tk_channel_of(tk, c->tok[0]);
            if (ch < 0) {
                return fail(st, c->file, c->line, "'%s' is not a channel the "
                            "song's `channel` lines name", c->tok[0]);
            }
            if (s->col[ch]) {
                return fail(st, c->file, c->line, "%s given twice",
                            c->tok[0]);
            }
            if (c->n == 3 && strcmp(c->tok[1], "=") == 0
                && digits(c->tok[2]) >= 0) {
                s->copy[ch] = (int)digits(c->tok[2]);
            } else if (c->n != 1) {
                return fail(st, c->file, c->line, "a column heading is a "
                            "channel, or `fm1 = 03` (section 03's again)");
            }
            s->col[ch] = true;
            s->cols[s->ncols++] = ch;
        }
        return true;
    }
    long row = digits(first);
    if (row < 0) {
        return fail(st, c->file, c->line, "a grid line starts with its row "
                    "number");
    }
    if (s->ncols == 0) {
        return fail(st, c->file, c->line, "a row before the section's `row "
                    "|` line");
    }
    if (row >= s->rows) {
        return fail(st, c->file, c->line, "row %ld: section %02d has %ld "
                    "rows", row, s->index, s->rows);
    }
    for (int k = s->ncols; k < nf - 1; k++) {
        if (!split_words(c, field[1 + k])) {
            return false;
        }
        if (c->n > 0) {
            return fail(st, c->file, c->line, "more cells than the section's "
                        "columns");
        }
    }
    for (int k = 0; k < s->ncols && k < nf - 1; k++) {
        int ch = s->cols[k];
        TkCell cell;
        if (!split_words(c, field[1 + k])) {
            return false;
        }
        int got = tk_cell(tk, c, tk->hb[ch][1] == 0x80, &cell);
        if (got < 0) {
            return false;
        }
        if (got == 0 || (cell.kind == TK_NONE && cell.end == TK_GO_ON)) {
            continue;
        }
        for (int i = s->cell_from; i < tk->ncell; i++) {
            if (tk->cell[i].ch == ch && tk->cell[i].row == row) {
                return fail(st, c->file, c->line, "%s row %ld given twice",
                            tk->ch[ch], row);
            }
        }
        if (!grow((void **)&tk->cell, &tk->cellcap, tk->ncell + 1,
                  sizeof *tk->cell)) {
            return fail(st, c->file, c->line, "out of memory");
        }
        cell.line = c->line;
        cell.sec = cur;
        cell.ch = ch;
        cell.row = row;
        tk->cell[tk->ncell++] = cell;
    }
    return true;
}

/* Any other line: `song`, `channel` or `section`. */
static bool tk_line(Tk *tk, Cur *c, int *cur)
{
    St *st = tk->st;
    const char *w = c->tok[c->i++];
    if (strcmp(w, "song") == 0) {
        const char *nm;
        if (tk->name[0] != 0) {
            return fail(st, c->file, c->line, "a second `song` line");
        }
        if (!take_name(c, "song: a name", &nm)) {
            return false;
        }
        snprintf(tk->name, ST_NAME, "%s", nm);
        tk->line = c->line;
        while (more(c)) {
            const char *k = c->tok[c->i++];
            int which = -1;
            for (int j = 0; j < 3; j++) {
                if (strcmp(k, kTkTempo[j]) == 0) {
                    which = j;
                }
            }
            if (which < 0 || tk->tempo[which] >= 0) {
                return fail(st, c->file, c->line, "'%s': a song line is `song "
                            "NAME tempo N pal_tempo N pal_tick N`", k);
            }
            if (!take_byte(c, k, 255, &tk->tempo[which])) {
                return false;
            }
        }
        for (int j = 0; j < 3; j++) {
            if (tk->tempo[j] < 0) {
                return fail(st, c->file, c->line, "song %s: no %s", tk->name,
                            kTkTempo[j]);
            }
        }
        return true;
    }
    if (strcmp(w, "channel") == 0) {
        if (tk->name[0] == 0 || tk->nsec > 0) {
            return fail(st, c->file, c->line, "`channel` lines come after the "
                        "`song` line, before the sections");
        }
        char *ch = (c->n > 1) ? c->tok[1] : "";
        bool known = false;
        for (int k = 0; k < TK_CHANNELS; k++) {
            known = known || strcmp(ch, kTkChannels[k]) == 0;
        }
        if (!known) {
            return fail(st, c->file, c->line, "'%s' is not a channel (drums, "
                        "fm1-fm6)", ch);
        }
        if (tk_channel_of(tk, ch) >= 0) {
            return fail(st, c->file, c->line, "channel %s given twice", ch);
        }
        for (int k = 0; k < c->n; k++) {
            if (strcmp(c->tok[k], "transpose") == 0) {
                return fail(st, c->file, c->line, "transpose: notes are "
                            "written as they are heard");
            }
        }
        /* A channel line is a `track` line without its label and transpose:
         * read_header's, with them put in. */
        if (c->n + 3 > ST_TOKENS) {
            return fail(st, c->file, c->line, "more than %d words",
                        ST_TOKENS - 3);
        }
        Cur h = { .st = st, .file = c->file, .line = c->line };
        h.tok[h.n++] = "track";
        h.tok[h.n++] = ch;
        h.tok[h.n++] = "x";
        for (int k = 2; k < c->n; k++) {
            h.tok[h.n++] = c->tok[k];
        }
        h.tok[h.n++] = "transpose";
        h.tok[h.n++] = "+0";
        StTrack t;
        if (!read_header(&h, &t)) {
            return false;
        }
        snprintf(tk->ch[tk->nch], sizeof tk->ch[0], "%s", ch);
        memcpy(tk->hb[tk->nch], t.bytes, 9);
        tk->ch_line[tk->nch] = c->line;
        tk->nch++;
        return true;
    }
    if (strcmp(w, "section") == 0) {
        static const char *form = "a section line is `section NN  ROWS rows "
                                  "x T ticks`";
        const char *x;
        int idx, rows, row;
        if (tk->nch == 0) {
            return fail(st, c->file, c->line, "a section before the `channel` "
                        "lines");
        }
        if (!take_byte(c, "section: its number", 999, &idx)) {
            return false;
        }
        if (idx != tk->nsec) {
            return fail(st, c->file, c->line, "section %02d: the sections are "
                        "numbered from 00 in order; %02d next", idx,
                        tk->nsec);
        }
        if (!take_byte(c, "section: how many rows", 999, &rows)
            || !take(c, "section N ROWS rows x T ticks: `rows`", &x)) {
            return false;
        }
        if (strcmp(x, "rows") != 0) {
            return fail(st, c->file, c->line, "%s", form);
        }
        if (!take(c, "`x`", &x)) {
            return false;
        }
        if (strcmp(x, "x") != 0) {
            return fail(st, c->file, c->line, "%s", form);
        }
        if (!take_byte(c, "section: ticks a row", 255, &row)
            || !take(c, "`ticks`", &x)) {
            return false;
        }
        if (strcmp(x, "ticks") != 0 || more(c)) {
            return fail(st, c->file, c->line, "%s", form);
        }
        if (rows == 0 || row == 0) {
            return fail(st, c->file, c->line, "a section has at least one row "
                        "of at least one tick");
        }
        if (!grow((void **)&tk->sec, &tk->seccap, tk->nsec + 1,
                  sizeof *tk->sec)) {
            return fail(st, c->file, c->line, "out of memory");
        }
        TkSection *s = &tk->sec[tk->nsec];
        memset(s, 0, sizeof *s);
        s->line = c->line;
        s->index = idx;
        s->rows = rows;
        s->row = row;
        if (tk->nsec > 0) {
            const TkSection *p = &tk->sec[tk->nsec - 1];
            s->start = p->start + p->rows * p->row;
        }
        for (int k = 0; k < TK_CHANNELS; k++) {
            s->copy[k] = -1;
        }
        s->cell_from = tk->ncell;
        *cur = tk->nsec++;
        return true;
    }
    return fail(st, c->file, c->line, "'%s': not a line a tracker file has",
                w);
}

static int tk_cell_order(const void *a, const void *b)
{
    const TkCell *x = a, *y = b;
    if (x->sec != y->sec) {
        return (x->sec < y->sec) ? -1 : 1;
    }
    if (x->ch != y->ch) {
        return (x->ch < y->ch) ? -1 : 1;
    }
    return (x->row < y->row) ? -1 : (x->row > y->row);
}

/* The whole file (tracker.py's read_tracker), its cells then sorted by
 * section, channel and row. */
static bool tk_read(Tk *tk, const char *text)
{
    St *st = tk->st;
    Cur c = { .st = st, .file = tk->file };
    const char *p = text;
    int cur = -1;
    bool ok = true;
    for (int j = 0; j < 3; j++) {
        tk->tempo[j] = -1;
    }
    while (ok && *p) {
        const char *e = strchr(p, '\n');
        size_t n = (e != NULL) ? (size_t)(e - p) : strlen(p);
        size_t k = 0;
        c.line++;
        /* tracker.py reads the text with its carriage returns taken out */
        for (size_t j = 0; j < n; j++) {
            if (p[j] == '\r') {
                continue;
            }
            if (k + 1 >= ST_LINE) {
                return fail(st, c.file, c.line, "a line of %zu characters, "
                            "past %d", n, ST_LINE - 1);
            }
            c.buf[k++] = p[j];
        }
        c.buf[k] = 0;
        p = (e != NULL) ? e + 1 : p + n;
        const char *lead = c.buf;
        while (*lead == ' ' || *lead == '\t' || *lead == '\v'
               || *lead == '\f') {
            lead++;
        }
        if (strchr(c.buf, '|') != NULL && *lead != '#') {
            ok = tk_grid_line(tk, &c, cur);
            continue;
        }
        if (!split_words(&c, c.buf)) {
            return false;
        }
        if (c.n > 0) {
            ok = tk_line(tk, &c, &cur);
        }
    }
    if (ok && tk->name[0] == 0) {
        ok = fail(st, tk->file, 1, "no `song` line");
    }
    if (ok && tk->nsec == 0) {
        ok = fail(st, tk->file, 1, "no sections");
    }
    if (!ok) {
        return false;
    }
    if (tk->ncell > 1) {
        qsort(tk->cell, (size_t)tk->ncell, sizeof *tk->cell, tk_cell_order);
    }
    for (int i = 0; i < tk->ncell; i++) {
        TkSection *s = &tk->sec[tk->cell[i].sec];
        if (s->count[tk->cell[i].ch]++ == 0) {
            s->first[tk->cell[i].ch] = i;
        }
    }
    return true;
}

/* tracker.py's RCell.key, in an order of its own. */
static int tk_key_order(const Tk *tk, const TkCell *a, const TkCell *b)
{
#define TK_CMP(x, y) if ((x) != (y)) { return ((x) < (y)) ? -1 : 1; }
    TK_CMP(a->kind, b->kind)
    TK_CMP(a->nnotes, b->nnotes)
    int m = memcmp(a->notes, b->notes, a->nnotes);
    if (m != 0) {
        return m;
    }
    TK_CMP(a->drum, b->drum)
    TK_CMP(a->delay, b->delay)
    TK_CMP(a->art, b->art)
    TK_CMP(a->nitems, b->nitems)
    if (a->nitems > 0) {
        m = memcmp(tk->pool.data + a->items, tk->pool.data + b->items,
                   a->nitems);
        if (m != 0) {
            return m;
        }
    }
    TK_CMP(a->end, b->end)
    TK_CMP(a->go_sec, b->go_sec)
    TK_CMP(a->go_row, b->go_row)
    TK_CMP(a->go_delay, b->go_delay)
#undef TK_CMP
    return 0;
}

typedef struct {
    long          tick;
    const TkCell *cell;
} TkAt;

/* A column's cells by tick (and key), for a `= NN` column written out. */
static void tk_sort_at(const Tk *tk, TkAt *a, int n)
{
    for (int i = 1; i < n; i++) {
        TkAt x = a[i];
        int j = i - 1;
        while (j >= 0 && (a[j].tick > x.tick
                          || (a[j].tick == x.tick
                              && tk_key_order(tk, a[j].cell, x.cell) > 0))) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = x;
    }
}

/* Whether section s's own cells in channel ch are src's (tracker.py's have
 * and want). */
static bool tk_same_column(const Tk *tk, const TkSection *s,
                           const TkSection *src, int ch)
{
    int n = s->count[ch];
    if (n != src->count[ch]) {
        return false;
    }
    TkAt *have = malloc(sizeof *have * (size_t)n);
    TkAt *want = malloc(sizeof *want * (size_t)n);
    bool same = have != NULL && want != NULL;
    for (int i = 0; same && i < n; i++) {
        const TkCell *a = &tk->cell[s->first[ch] + i];
        const TkCell *b = &tk->cell[src->first[ch] + i];
        have[i].tick = a->row * s->row + a->delay;
        have[i].cell = a;
        want[i].tick = b->row * src->row + b->delay;
        want[i].cell = b;
    }
    if (same) {
        tk_sort_at(tk, have, n);
        tk_sort_at(tk, want, n);
    }
    for (int i = 0; same && i < n; i++) {
        same = have[i].tick == want[i].tick
            && tk_key_order(tk, have[i].cell, want[i].cell) == 0;
    }
    free(have);
    free(want);
    return same;
}

/* One channel's cells in time order, `= NN` columns resolved, each with its
 * tick, and its end (tracker.py's channel_events). */
static bool tk_events(Tk *tk, int ch, TkCell **out, int *n, int *cap,
                      TkCell *end)
{
    St *st = tk->st;
    const char *name = tk->ch[ch];
    bool ended = false;
    for (int si = 0; si < tk->nsec; si++) {
        const TkSection *s = &tk->sec[si];
        const TkSection *src = s;
        if (s->copy[ch] >= 0) {
            int k = s->copy[ch];
            if (k >= s->index) {
                return fail(st, tk->file, s->line, "%s = %02d: a column plays "
                            "an earlier section's again", name, k);
            }
            src = &tk->sec[k];
            while (src->copy[ch] >= 0) {
                src = &tk->sec[src->copy[ch]];
            }
            if (src->rows * src->row != s->rows * s->row) {
                return fail(st, tk->file, s->line, "%s = %02d: section %02d is "
                            "%ld ticks, this one %ld", name, k, k,
                            src->rows * src->row, s->rows * s->row);
            }
            if (s->count[ch] > 0 && !tk_same_column(tk, s, src, ch)) {
                return fail(st, tk->file, tk->cell[s->first[ch]].line, "%s in "
                            "section %02d plays section %02d's again (`= "
                            "%02d`) but its cells differ: edit section %02d's, "
                            "or take away `= %02d` to make these its own",
                            name, s->index, k, k, k, k);
            }
        }
        for (int i = 0; i < src->count[ch]; i++) {
            const TkCell *c = &tk->cell[src->first[ch] + i];
            if (c->delay >= src->row) {
                return fail(st, tk->file, c->line, "@%ld: past the row's %ld "
                            "ticks", c->delay, src->row);
            }
            if (ended) {
                return fail(st, tk->file, c->line, "%s: a cell after the "
                            "channel's `goto` or `end` (%s:%d)", name,
                            st->path[tk->file], end->line);
            }
            TkCell x = *c;
            x.time = s->start + c->row * src->row + c->delay;
            if (x.end != TK_GO_ON) {
                *end = x;
                ended = true;
                continue;
            }
            if (!grow((void **)out, cap, *n + 1, sizeof **out)) {
                return fail(st, -1, 0, "out of memory");
            }
            (*out)[(*n)++] = x;
        }
    }
    if (!ended) {
        return fail(st, tk->file, tk->sec[tk->nsec - 1].line, "%s has no "
                    "`goto` or `end`", name);
    }
    return true;
}

/* Where a `goto SS.RR` lands, in ticks. */
static bool tk_goto_tick(const Tk *tk, const TkCell *end, long *t)
{
    St *st = tk->st;
    if (end->go_sec >= tk->nsec) {
        return fail(st, tk->file, end->line, "goto %02ld.%02ld: no section "
                    "%02ld", end->go_sec, end->go_row, end->go_sec);
    }
    const TkSection *s = &tk->sec[end->go_sec];
    if (end->go_row >= s->rows) {
        return fail(st, tk->file, end->line, "goto %02ld.%02ld: section %02ld "
                    "has %ld rows", end->go_sec, end->go_row, end->go_sec,
                    s->rows);
    }
    *t = s->start + end->go_row * s->row + end->go_delay;
    return true;
}

static bool tk_put_items(StChunk *to, const Tk *tk, u32 at, u32 len)
{
    for (u32 k = 0; k < len; k += 1u + tk->pool.data[at + k]) {
        if (!put(to, tk->pool.data + at + k + 1, tk->pool.data[at + k])) {
            return false;
        }
    }
    return true;
}

typedef struct {
    long ln, mult;
    bool label, again;
    u8   after;                 /* 0, $E6 a bare hold, $F1 a nop */
} TkEvent;

/* One channel's block (tracker.py's channel_stream, and assemble's bytes
 * of the stream text it writes). */
static bool tk_block(Tk *tk, int ch, const char *label, StChunk *to)
{
    St *st = tk->st;
    const char *name = tk->ch[ch];
    bool drums = tk->hb[ch][1] == 0x80;
    TkCell *cells = NULL;
    TkCell end = { 0 };
    TkEvent *ev = NULL;
    int n = 0, cap = 0, target = -1;
    char loop[ST_NAME + 8];     /* label_loop: the name check keeps it short */
    bool ok = tk_events(tk, ch, &cells, &n, &cap, &end);

    for (int i = 1; ok && i < n; i++) {
        if (cells[i].time <= cells[i - 1].time) {
            int k = 0;
            while (cells[k].time != cells[i].time) {
                k++;
            }
            ok = fail(st, tk->file, cells[k].line, "two cells at one tick");
        }
    }
    if (ok && end.end == TK_GOTO) {
        long t = 0;
        ok = tk_goto_tick(tk, &end, &t);
        for (int i = 0; ok && i < n && target < 0; i++) {
            if (cells[i].time == t) {
                target = i;
            }
        }
        if (ok && target < 0) {
            ok = fail(st, tk->file, end.line, "goto: %s has no cell at that "
                      "place (tick %ld)", name, t);
        }
    }
    if (ok && n > 0 && end.time <= cells[n - 1].time) {
        ok = fail(st, tk->file, end.line, "the end comes before the last cell "
                  "ends");
    }
    ev = ok ? calloc((size_t)(n > 0 ? n : 1), sizeof *ev) : NULL;
    ok = ok && (ev != NULL || fail(st, -1, 0, "out of memory"));

    long mult = tk->hb[ch][2];
    int chord = 0;
    const TkCell *last = NULL;  /* the notes or drum struck last */
    bool hold_put = false;      /* a bare hold just put after an event */
    for (int i = 0; ok && i < n; i++) {
        const TkCell *c = &cells[i];
        const TkCell *nxt = (i + 1 < n) ? &cells[i + 1]
                          : (target >= 0) ? &cells[target] : NULL;
        long dur = ((i + 1 < n) ? cells[i + 1].time : end.time) - c->time;
        for (u32 k = 0; ok && k < c->nitems;
             k += 1u + tk->pool.data[c->items + k]) {
            const u8 *it = tk->pool.data + c->items + k + 1;
            if (it[0] == 0xE5) {
                chord = it[1];
                if (chord == 1) {
                    ok = fail(st, tk->file, c->line, "chord 1 is not a chord: "
                              "chord 2 or more, or off");
                }
            } else if (it[0] == 0xFA) {
                mult = it[1];
            }
        }
        if (ok && (mult == 0 || dur % mult != 0 || dur / mult <= 0
                   || dur / mult > 127)) {
            ok = fail(st, tk->file, c->line, "%s lasts %ld ticks here; with "
                      "tick %ld a length is a multiple of %ld up to %ld%s",
                      name, dur, mult, mult, 127 * mult,
                      (dur > 127 * mult) ? " (split it with a `tie`)" : "");
        }
        if (!ok) {
            break;
        }
        TkEvent *e = &ev[i];
        e->label = i == target;
        e->ln = dur / mult;
        e->mult = mult;
        if (c->kind == TK_TIE) {
            if (c->nitems > 0 && i > 0 && cells[i - 1].art != 'c' && !drums) {
                ok = fail(st, tk->file, c->line, "a `tie` with changes in it "
                          "cannot hold the note before (the driver reads the "
                          "changes between them): mark the cell before it "
                          "`cut` (%s:%d), or move the changes",
                          st->path[tk->file], cells[i - 1].line);
            }
        } else if (c->kind == TK_DRUM) {
            e->again = last != NULL && last->drum == c->drum && !hold_put
                    && i != target;
            last = c;
        } else if (c->kind == TK_NOTE) {
            int want = (chord >= 2) ? chord : 1;
            if (c->nnotes != want) {
                char in[32];
                if (chord >= 2) {
                    snprintf(in, sizeof in, "`chord %d`", chord);
                } else {
                    snprintf(in, sizeof in, "single notes (no chord)");
                }
                ok = fail(st, tk->file, c->line, "%d notes here, and the "
                          "channel is in %s", c->nnotes, in);
                break;
            }
            e->again = last != NULL && last->nnotes == c->nnotes
                    && memcmp(last->notes, c->notes, c->nnotes) == 0
                    && !hold_put && i != target;
            last = c;
        }
        hold_put = false;
        if (ok && !drums) {
            bool held = c->art == 'h' || (nxt != NULL && nxt->kind == TK_TIE
                                          && c->art != 'c');
            if (c->art == 'h' && nxt != NULL && nxt->kind == TK_TIE) {
                ok = fail(st, tk->file, c->line, "`hold` before a `tie`: a "
                          "note before a tie is held anyway");
            }
            bool plain_tie_next = i + 1 < n && nxt->kind == TK_TIE
                               && nxt->nitems == 0;
            if (held && !plain_tie_next) {
                e->after = 0xE6;
                hold_put = true;
            } else if (!held && plain_tie_next) {
                e->after = 0xF1;
            }
        }
    }

    /* The bytes: a length left out where it is the last one (the driver
     * keeps it) -- but not before an `again` (its length byte would be read
     * as this one's), nor at the loop's first cell, which two places
     * reach. */
    snprintf(loop, sizeof loop, "%s_loop", label);
    long last_len = -1;
    for (int i = 0; ok && i < n; i++) {
        const TkCell *c = &cells[i];
        const TkEvent *e = &ev[i];
        bool nxt_again = i + 1 < n && ev[i + 1].again && e->after == 0;
        long ticks = e->ln * e->mult;
        bool keep = ticks == last_len && !e->label && !nxt_again;
        u8 ln = (u8)e->ln;
        last_len = ticks;
        if (e->label) {
            ok = add_label(to, loop, to->len);
        }
        ok = ok && tk_put_items(to, tk, c->items, c->nitems);
        if (e->again) {
            ok = ok && put_byte(to, ln);
        } else if (c->kind == TK_TIE) {
            ok = ok && put_byte(to, 0xE6) && put_byte(to, ln);
        } else {
            if (c->kind == TK_OFF) {
                ok = ok && put_byte(to, 0x80);
            } else if (c->kind == TK_DRUM) {
                ok = ok && put_byte(to, c->drum);
            } else {
                for (int k = 0; ok && k < c->nnotes; k++) {
                    ok = put_byte(to, (u8)(0x80 + c->notes[k]));
                }
            }
            if (!keep) {
                ok = ok && put_byte(to, ln);
            }
        }
        if (e->after != 0) {
            ok = ok && put_byte(to, e->after);
        }
    }
    ok = ok && tk_put_items(to, tk, end.items, end.nitems);
    if (ok && end.end == TK_GOTO) {
        u8 b[3] = { 0xF6, 0, 0 };
        ok = add_fix(to, to->len + 1, to->len + 2, false, loop, tk->file,
                     end.line) && put(to, b, 3);
    } else if (ok) {
        ok = put_byte(to, 0xF2);
    }
    free(cells);
    free(ev);
    return ok;
}

/* songs/<name>.trk (its text read by scan_songs) to the song, its tracks
 * starting at a block a channel. */
static bool read_tracker_song(St *st, const char *dir, StSrc *src)
{
    char path[512];
    snprintf(path, sizeof path, "%s/songs/%s.trk", dir, src->name);
    int file = add_file(st, path);
    if (file < 0) {
        return false;
    }
    Tk *tk = calloc(1, sizeof *tk);
    if (tk == NULL) {
        return fail(st, -1, 0, "out of memory");
    }
    tk->st = st;
    tk->file = file;
    bool ok = tk_read(tk, src->trk);
    if (ok && strcmp(tk->name, src->name) != 0) {
        ok = fail(st, file, tk->line, "the song line names %s, not %s",
                  tk->name, src->name);
    }
    if (ok && find_sound(st->songs, st->nsongs, tk->name) != NULL) {
        ok = fail(st, file, tk->line, "song %s given twice", tk->name);
    }
    if (ok && st->nsongs == ST_SONGS) {
        ok = fail(st, file, tk->line, "more than %d songs", ST_SONGS);
    }
    if (ok && strlen(TK_PREFIX) + strlen(tk->name) + 12 >= ST_NAME) {
        ok = fail(st, file, tk->line, "song %s: a name of %d letters at most "
                  "for a tracker song's labels", tk->name,
                  (int)(ST_NAME - 13 - strlen(TK_PREFIX)));
    }
    StSound *s = NULL;
    if (ok) {
        s = &st->songs[st->nsongs++];
        memset(s, 0, sizeof *s);
        snprintf(s->name, ST_NAME, "%s", tk->name);
        s->tempo = tk->tempo[0];
        s->pal_tempo = tk->tempo[1];
        s->pal_tick = tk->tempo[2];
        s->file = file;
        s->line = tk->line;
    }
    for (int ch = 0; ok && ch < tk->nch; ch++) {
        char label[ST_NAME];
        snprintf(label, sizeof label, "%s%s_%s", TK_PREFIX, tk->name,
                 tk->ch[ch]);
        for (int i = 0; ok && i < st->nblocks; i++) {
            if (strcmp(st->blocks[i].name, label) == 0) {
                ok = fail(st, file, tk->ch_line[ch], "block %s given twice",
                          label);
            }
        }
        if (ok && st->nblocks == ST_BLOCKS) {
            ok = fail(st, file, tk->ch_line[ch], "more than %d blocks",
                      ST_BLOCKS);
        }
        if (!ok) {
            break;
        }
        StBlock *b = &st->blocks[st->nblocks++];
        memset(b, 0, sizeof *b);
        snprintf(b->name, ST_NAME, "%s", label);
        b->file = file;
        b->line = tk->ch_line[ch];
        b->tracker = true;
        ok = add_label(&b->chunk, label, 0)
          && tk_block(tk, ch, label, &b->chunk);
        StTrack *t = &s->tracks[ch];
        snprintf(t->label, ST_NAME, "%s", label);
        memcpy(t->bytes, tk->hb[ch], 9);
        t->file = file;
        t->line = tk->ch_line[ch];
        s->counted = s->total = ch + 1;
    }
    free(tk->sec);
    free(tk->cell);
    free_chunk(&tk->pool);
    free(tk);
    return ok;
}

/* The first two words of a line (none past a word that starts with #). */
static void two_words(const char *p, size_t n, char a[ST_NAME],
                      char b[ST_NAME])
{
    char *out[2] = { a, b };
    size_t i = 0;
    a[0] = b[0] = 0;
    for (int w = 0; w < 2; w++) {
        while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r')) {
            i++;
        }
        if (i == n || p[i] == '#') {
            return;
        }
        size_t k = 0;
        while (i < n && p[i] != ' ' && p[i] != '\t' && p[i] != '\r') {
            if (k + 1 < ST_NAME) {
                out[w][k] = p[i];
            }
            k++;
            i++;
        }
        out[w][(k + 1 < ST_NAME) ? k : 0] = 0;
    }
}

/* (build 139b) Every song file's songs and blocks, from its `song` and
 * `block` lines (tracker.py's song_files), and where each song is read
 * from, first found: a .trk in the mods folder, its song file there, the
 * game's .trk, the game's song file (a stream text mod is not hidden
 * behind the game's .trk). A song file none of whose songs is left to it
 * is not read. */
static bool scan_songs(St *st, const char *dir)
{
    char path[512], txt[512];
    for (int f = 0; f < st->nsong_files; f++) {
        snprintf(path, sizeof path, "%s/songs/%s.txt", dir, st->song_file[f]);
        char *text = read_text(path, st->reader);
        if (text == NULL) {
            return fail(st, -1, 0, "cannot read %s", path);
        }
        for (const char *p = text; *p;) {
            const char *e = strchr(p, '\n');
            size_t n = (e != NULL) ? (size_t)(e - p) : strlen(p);
            char a[ST_NAME], b[ST_NAME];
            two_words(p, n, a, b);
            p = (e != NULL) ? e + 1 : p + n;
            if (b[0] == 0) {
                continue;
            }
            if (strcmp(a, "song") == 0 && st->nsrc < ST_SONGS) {
                snprintf(st->src[st->nsrc].name, ST_NAME, "%s", b);
                st->src[st->nsrc++].file = f;
            } else if (strcmp(a, "block") == 0 && st->nscanned < ST_BLOCKS) {
                snprintf(st->scanned[st->nscanned].name, ST_NAME, "%s", b);
                st->scanned[st->nscanned++].file = f;
            }
        }
        free(text);
    }
    for (int k = 0; k < st->nsrc; k++) {
        StSrc *s = &st->src[k];
        snprintf(path, sizeof path, "%s/songs/%s.trk", dir, s->name);
        snprintf(txt, sizeof txt, "%s/songs/%s.txt", dir,
                 st->song_file[s->file]);
        bool trk_mod = g_is_mod != NULL && g_is_mod(path);
        bool txt_mod = g_is_mod != NULL && g_is_mod(txt);
        if (trk_mod || !txt_mod) {
            s->trk = read_text(path, st->reader);
        }
    }
    for (int k = 0; k < st->nsrc; k++) {
        if (st->src[k].trk == NULL) {
            st->file_read[st->src[k].file] = true;
            st->any_file_read = true;
        }
    }
    return true;
}

/* A sounds.txt `block` in a song file not read, and in none that is. */
static bool dropped_block(const St *st, const char *name)
{
    bool dropped = false;
    for (int k = 0; k < st->nscanned; k++) {
        if (strcmp(st->scanned[k].name, name) != 0) {
            continue;
        }
        if (st->file_read[st->scanned[k].file]) {
            return false;
        }
        dropped = true;
    }
    return dropped;
}

/* ------------------------------------------------------------------ */
/* The image                                                           */

static void put_word_le(u8 *at, u32 v)
{
    at[0] = (u8)v;
    at[1] = (u8)(v >> 8);
}

/* A sound's header: its count, then each track's nine bytes, the start
 * label fixed up later. */
static bool put_header(St *st, StChunk *img, const StSound *s, u32 *at)
{
    *at = img->len;
    bool ok = put_byte(img, (u8)s->counted);
    for (int i = 0; ok && i < s->total; i++) {
        const StTrack *t = &s->tracks[i];
        ok = add_fix(img, img->len + 3, 0, true, t->label, t->file, t->line)
          && put(img, t->bytes, 9);
    }
    return ok || fail(st, -1, 0, "out of memory");
}

/* MusicTempo (which 0), loc_B8100 (1, PAL) or loc_B8120 (2, PAL tick
 * multipliers): 32 entries, a song id's from its `song` line, 0 for an id
 * with no song and past the songs. */
static bool put_tempos(St *st, StChunk *img, int which)
{
    int nmusic = FIRST_EFFECT_ID - FIRST_ID;
    bool ok = true;
    for (int k = 0; ok && k < TEMPO_ENTRIES; k++) {
        const StSound *s = (k < nmusic)
            ? find_sound(st->songs, st->nsongs, st->sound[k].name) : NULL;
        int v = (s == NULL) ? 0 : (which == 0) ? s->tempo
              : (which == 1) ? s->pal_tempo : s->pal_tick;
        ok = put_byte(img, (u8)v);
    }
    return ok;
}

static bool lay_out(St *st, SmpsData *d)
{
    StChunk *img = &st->image;
    int nmusic = FIRST_EFFECT_ID - FIRST_ID;
    int neffect = FIRST_SPECIAL_ID - FIRST_EFFECT_ID;
    int nspecial = st->nsounds - PRIORITIES + 1;
    u32 header[ST_SOUNDS];
    bool ok = true;

    /* Every id's sound exists. */
    for (int i = 0; i < st->nsounds; i++) {
        const char *nm = st->sound[i].name;
        bool music = i < nmusic;
        if (strcmp(nm, "none") == 0) {
            if (i >= nmusic + neffect) {
                return fail(st, st->sound[i].file, st->sound[i].line,
                            "a special id needs an effect");
            }
            continue;
        }
        if (music ? !find_sound(st->songs, st->nsongs, nm)
                  : !find_sound(st->effects, st->neffects, nm)) {
            return fail(st, st->sound[i].file, st->sound[i].line,
                        music ? "no song is named '%s' (songs/)"
                              : "no effect is named '%s' (effects.txt)", nm);
        }
    }

    u32 roots_at = img->len;
    ok = put(img, NULL, 2 * ROOTS);
    u32 prio_at = img->len;
    for (int i = 0; ok && i < PRIORITIES; i++) {
        ok = put_byte(img, st->sound[i].priority);
    }
    u32 tempo_at = img->len;
    ok = ok && put_tempos(st, img, 0);

    /* The envelopes between the NTSC tempos and the PAL tables, which the
     * driver reads at $100 by address. */
    u32 env_ptrs_at = img->len;
    ok = ok && put(img, NULL, 2 * (u32)st->nenv);
    for (int i = 0; ok && i < st->nenv; i++) {
        put_word_le(img->data + env_ptrs_at + 2 * i, img->len);
        ok = put(img, st->env[i].data, st->env[i].len);
    }
    if (ok && img->len > PAL_TEMPO_AT) {
        return fail(st, -1, 0, "envelopes.txt: the envelopes end at $%X, "
                    "past $100 where the PAL tables are read", img->len);
    }
    ok = ok && put(img, NULL, PAL_TEMPO_AT - img->len)
            && put_tempos(st, img, 1) && put_tempos(st, img, 2);
    u32 music_ptrs = img->len;
    u32 sfx_ptrs = music_ptrs + 2 * (u32)nmusic;
    u32 spc_ptrs = sfx_ptrs + 2 * (u32)neffect;
    ok = ok && put(img, NULL, 2 * (u32)(nmusic + neffect + nspecial));

    /* The song headers in id order, then the empty one. */
    bool none_song = false;
    for (int i = 0; ok && i < nmusic; i++) {
        const char *nm = st->sound[i].name;
        bool seen = false;
        for (int k = 0; k < i; k++) {
            seen = seen || strcmp(st->sound[k].name, nm) == 0;
        }
        if (strcmp(nm, "none") == 0) {
            none_song = true;
            continue;
        }
        if (!seen) {
            ok = put_header(st, img,
                            find_sound(st->songs, st->nsongs, nm), &header[i]);
        }
    }
    u32 none_song_at = img->len;
    if (none_song) {
        ok = ok && put_byte(img, 0);
    }

    /* FMInsPtrs and the instruments. */
    u32 ins_ptrs = img->len;
    u32 stored[256];
    ok = ok && put(img, NULL, 2 * (u32)st->nins);
    for (int i = 0; ok && i < st->nins; i++) {
        const u8 *ins = st->bank + (size_t)i * INSTRUMENT_REGISTERS;
        int sh = st->share[i];
        u32 at;
        if (st->same[i] >= 0 && memcmp(st->bank + (size_t)st->same[i]
                                       * INSTRUMENT_REGISTERS, ins,
                                       INSTRUMENT_REGISTERS) == 0) {
            at = stored[st->same[i]];
        } else if (sh > 0 && st->same[i - 1] < 0
                   && memcmp(ins - sh, ins, (size_t)sh) == 0) {
            at = img->len - (u32)sh;
            ok = put(img, ins + sh, (u32)(INSTRUMENT_REGISTERS - sh));
        } else {
            at = img->len;
            ok = put(img, ins, INSTRUMENT_REGISTERS);
        }
        stored[i] = at;
        put_word_le(img->data + ins_ptrs + 2 * i, at);
    }

    /* The effects in effects.txt's order, then the empty one. */
    u32 effect_at[ST_EFFECTS];
    for (int i = 0; ok && i < st->neffects; i++) {
        ok = put_header(st, img, &st->effects[i], &effect_at[i])
          && append_chunk(img, &st->effects[i].stream);
    }
    u32 none_effect_at = img->len;
    bool none_effect = false;
    for (int i = nmusic; i < nmusic + neffect; i++) {
        none_effect = none_effect || strcmp(st->sound[i].name, "none") == 0;
    }
    if (none_effect) {
        ok = ok && put_byte(img, 0);
    }

    /* The driver's code, the note frequencies, the streams. */
    ok = ok && put(img, NULL, (u32)st->gap);
    for (int i = 0; ok && i < d->freq_count; i++) {
        u8 w[2] = { (u8)(d->freqs[i] >> 8), (u8)d->freqs[i] };
        ok = put(img, w, 2);
    }
    /* (build 139b) Without the song files' blocks, the bytes between them
     * go too; a song file's blocks go with it; the tracker songs' come
     * after the streams, in song and channel order. */
    for (int i = 0; ok && i < st->nplace; i++) {
        StPlace *pl = &st->place[i];
        if (!pl->is_block) {
            ok = !st->any_file_read || append_chunk(img, &pl->bytes);
            continue;
        }
        if (dropped_block(st, pl->name)) {
            continue;
        }
        StBlock *b = NULL;
        for (int k = 0; k < st->nblocks; k++) {
            if (strcmp(st->blocks[k].name, pl->name) == 0) {
                b = &st->blocks[k];
            }
        }
        if (b == NULL) {
            return fail(st, pl->file, pl->line, "no block is named '%s' "
                        "(songs/)", pl->name);
        }
        if (b->placed) {
            return fail(st, pl->file, pl->line, "block %s placed twice",
                        pl->name);
        }
        b->placed = true;
        ok = append_chunk(img, &b->chunk);
    }
    for (int k = 0; ok && k < st->nblocks; k++) {
        StBlock *b = &st->blocks[k];
        if (!b->tracker) {
            continue;
        }
        if (b->placed) {
            return fail(st, b->file, b->line, "block %s placed twice",
                        b->name);
        }
        b->placed = true;
        ok = append_chunk(img, &b->chunk);
    }
    if (!ok) {
        return fail(st, -1, 0, "out of memory");
    }
    for (int k = 0; k < st->nblocks; k++) {
        if (!st->blocks[k].placed) {
            return fail(st, st->blocks[k].file, st->blocks[k].line, "block %s "
                        "is not placed in sounds.txt's streams",
                        st->blocks[k].name);
        }
    }
    if (img->len > IMAGE_MAX) {
        return fail(st, -1, 0, "the image is %u bytes, past the driver's "
                    "64 KB", img->len);
    }

    /* Labels: once each. Then every jump and pointer. */
    for (int i = 0; i < img->nlab; i++) {
        for (int k = 0; k < i; k++) {
            if (strcmp(img->lab[i].name, img->lab[k].name) == 0) {
                return fail(st, -1, 0, "label %s defined twice",
                            img->lab[i].name);
            }
        }
    }
    for (int i = 0; i < img->nfix; i++) {
        const StFix *f = &img->fix[i];
        int found = -1;
        for (int k = 0; k < img->nlab; k++) {
            if (strcmp(img->lab[k].name, f->label) == 0) {
                found = k;
                break;
            }
        }
        if (found < 0) {
            return fail(st, f->file, f->line, "no label %s", f->label);
        }
        u32 t = img->lab[found].at;
        put_word_le(img->data + f->at,
                    f->absolute ? t : ((t - f->second) & 0xFFFF));
    }
    u32 roots[ROOTS] = { prio_at, tempo_at, music_ptrs, ins_ptrs,
                         env_ptrs_at, music_ptrs, sfx_ptrs };
    for (int i = 0; i < ROOTS; i++) {
        put_word_le(img->data + roots_at + 2 * i, roots[i]);
    }
    for (int i = 0; i < st->nsounds; i++) {
        const char *nm = st->sound[i].name;
        u32 slot, at = 0;
        if (i < nmusic) {
            slot = music_ptrs + 2 * (u32)i;
            if (strcmp(nm, "none") == 0) {
                at = none_song_at;
            } else {
                for (int k = 0; k <= i; k++) {
                    if (strcmp(st->sound[k].name, nm) == 0) {
                        at = header[k];
                        break;
                    }
                }
            }
        } else {
            slot = (i < nmusic + neffect) ? sfx_ptrs + 2 * (u32)(i - nmusic)
                                          : spc_ptrs + 2 * (u32)(i - nmusic
                                                                 - neffect);
            if (strcmp(nm, "none") == 0) {
                at = none_effect_at;
            } else {
                for (int k = 0; k < st->neffects; k++) {
                    if (strcmp(st->effects[k].name, nm) == 0) {
                        at = effect_at[k];
                    }
                }
            }
        }
        put_word_le(img->data + slot, at);
    }

    d->image = img->data;
    d->size = img->len;
    d->spc_sfx_ptrs = spc_ptrs;
    d->music_count = nmusic;
    d->sfx_count = neffect;
    d->spc_count = nspecial;    /* (184) for smps_load's chord walk */
    img->data = NULL;           /* the image is d's now */
    return true;
}

bool songtext_build(const char *dir, Ps2Reader reader, SmpsData *d)
{
    St *st = calloc(1, sizeof *st);
    char path[512];
    u8 order[INSTRUMENT_REGISTERS];
    bool ok = st != NULL;

    if (!ok) {
        return false;
    }
    st->reader = reader;
    snprintf(path, sizeof path, "%s/note_frequencies.json", dir);
    ok = load_note_names(st, path);
    snprintf(path, sizeof path, "%s/drums.json", dir);
    ok = ok && load_drum_names(st, path);
    snprintf(path, sizeof path, "%s/fm_instruments.json", dir);
    ok = ok && instruments_load_named(path, reader, order, &st->bank,
                                      &st->nins, &st->ins_name);
    if (ok && st->ins_name[0][0] == 0) {
        ok = fail(st, -1, 0, "%s: the instruments have no names", path);
    }
    ok = ok && read_sounds(st, dir) && read_envelopes(st, dir);
    /* (build 139b) Which songs come from their tracker files, and those
     * read first (tracker.py's game_music reads them before the rest). */
    ok = ok && scan_songs(st, dir);
    for (int k = 0; ok && k < st->nsrc; k++) {
        if (st->src[k].trk != NULL) {
            ok = read_tracker_song(st, dir, &st->src[k]);
        }
    }
    snprintf(path, sizeof path, "%s/effects.txt", dir);
    ok = ok && read_streams(st, path, false);
    for (int i = 0; ok && i < st->nsong_files; i++) {
        if (!st->file_read[i]) {
            continue;
        }
        snprintf(path, sizeof path, "%s/songs/%s.txt", dir,
                 st->song_file[i]);
        ok = read_streams(st, path, true);
    }
    ok = ok && lay_out(st, d);

    for (int k = 0; k < st->nsrc; k++) {
        free(st->src[k].trk);
    }
    free_chunk(&st->image);
    for (int i = 0; i < st->nenv; i++) {
        free_chunk(&st->env[i]);
    }
    for (int i = 0; i < st->nplace; i++) {
        free_chunk(&st->place[i].bytes);
    }
    for (int i = 0; i < st->neffects; i++) {
        free_chunk(&st->effects[i].stream);
    }
    for (int i = 0; i < st->nblocks; i++) {
        free_chunk(&st->blocks[i].chunk);
    }
    free(st->bank);
    free(st->ins_name);
    free(st);
    return ok;
}
