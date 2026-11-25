/* (build 190) tools/song_text.py's disassemble and annotate in C: the
 * driver's image to the song text (sounds.txt, envelopes.txt, effects.txt,
 * songs/<file>.txt), every song and effect walked as the sequencer walks
 * it, its notes by name, then the `# heard:` comments on the lines whose
 * notes sound other than written. exmusic.h; the music step (ex_music.c)
 * builds the text back with the game's own songtext.c before writing it.
 *
 * Python's annotate assembles the text and finds each note's line through
 * the assembler's record of where each byte came from; here each line
 * keeps the notes it was written with, which is the same line. */

#include "exmusic.h"
#include "asset_names.h"

#include <stdlib.h>
#include <string.h>

/* The text's headers: song_text.py's, written in by a script from it
 * (tools/song_text.py's SOUNDS_HEADER ... SONG_HEADER), not retyped. */
static const char SOUNDS_HEADER[] =
    "# The sounds (build 137): every sound id the game can ask the driver for,\n"
    "# what it plays, and the driver's own tables. Built back into the driver's\n"
    "# image (SoundDriverBase) with envelopes.txt, effects.txt, songs/*.txt,\n"
    "# fm_instruments.json and note_frequencies.json -- docs/audio.md, \"The song\n"
    "# text\". Lines from # are comments.\n"
    "#\n"
    "# One line a sound id, $81 to $D8 in order: the id, the song or effect it\n"
    "# plays (the name on a `song` line in songs/ or an `effect` line in\n"
    "# effects.txt; `none` is an empty sound) and its priority (SoundPriorities):\n"
    "# a sound with bit 7 set always plays, and with bit 0 too becomes the song\n"
    "# an effect's `music restart` brings back; any other plays if its priority\n"
    "# is at least the sounding effect's. $81-$9F are songs (MusicPtrs), $A0-$D7\n"
    "# effects (SFXPtrs), $D8 on the special effects (SpcSFXPtrs), which take\n"
    "# their effect's first track only. `song_file` lines name the files under\n"
    "# songs/ to read; a song file not named is not read.\n";

static const char LAYOUT_HEADER[] =
    "# Where things sit in the image, after the effects. `driver_code` is the\n"
    "# driver's own code, zeros here; then the note frequencies; then `streams`:\n"
    "# the songs' blocks in this order, and `bytes` no stream reaches. The order\n"
    "# only places things -- a jump finds its label wherever it sits -- and keeps\n"
    "# the image the ROM's.\n";

static const char INSTRUMENTS_HEADER[] =
    "# The instruments (FMInsPtrs, then the bank from fm_instruments.json, in\n"
    "# number order), each stored whole but for these lines: `same A B ...`\n"
    "# stores B (and the rest) at A's place when they are the same bytes, and\n"
    "# `share A B N` stores B starting in A's last N bytes when those are B's\n"
    "# first N, as the ROM stores them; when the bytes differ (an instrument\n"
    "# edited), each is stored whole.\n";

static const char ENVELOPES_HEADER[] =
    "# The vibrato envelopes (build 137; ModEnvelopePtrs, DoModEnv). A track's\n"
    "# header or a `mod` line names one. Each tick a note sounds, the driver reads\n"
    "# the envelope's next byte and sends the note's frequency plus that byte\n"
    "# times the track's multiplier -- an offset from the note, not a sum. A new\n"
    "# note sets the multiplier back to 0 but not the place in the envelope.\n"
    "#\n"
    "#   a number       a step, -128 to 127 (but not the codes' bytes below).\n"
    "#                  The ROM extends a negative step wrongly: every one of\n"
    "#                  them plays as -1 times the multiplier.\n"
    "#   depth +N       ($84 N) add N to the multiplier\n"
    "#   jump LABEL     ($85) go on from LABEL, a `name:` line in the envelope\n"
    "#   restart        ($80) go on from the envelope's first byte\n"
    "#   hold, stop     ($81, $83) stop moving: the offset stays where it is\n"
    "#\n"
    "# The envelopes sit together in the image, ending at $100 at most (the PAL\n"
    "# tables are read there by address); they are padded with zeros to it.\n";

static const char EFFECTS_HEADER[] =
    "# The sound effects (build 137; SFXPtrs). docs/audio.md, \"The song text\",\n"
    "# has the whole format; in short:\n"
    "#\n"
    "#   effect NAME            an effect (sounds.txt gives its id), then its\n"
    "#   track CH LABEL ...     tracks' headers: the channel (fm1-fm6, drums),\n"
    "#                          where its stream starts and its starting state\n"
    "#   unused_track ...       header bytes after the counted ones; never read\n"
    "#   NAME:                  a label: where a goto, loop or call lands\n"
    "#   C5 12                  a note and its length in ticks (times the tick\n"
    "#                          multiplier); a note with no length keeps the last\n"
    "#   C5+E5+G5 12            chord mode: one note a part\n"
    "#   rest 12                silence\n"
    "#   again 12               the last note struck again, for 12\n"
    "#   tie 12                 the note held on 12 more, not struck again\n"
    "#   C5 12 slide +3         pitch-slide mode: the note, then a slide of +3\n"
    "#                          F-number units a tick (`unused N`: a byte the\n"
    "#                          driver skips)\n"
    "#   freq $2060 12          raw-frequency mode: the chip's frequency word\n"
    "#   instrument NAME, volume +4 (or volume 20), transpose -12, mod NAME,\n"
    "#   pan left, chord 3, tick 2, tempo 6, ...  state, until changed\n"
    "#   goto L, loop L 4 times, call L, return, end\n"
    "#   bytes $80 $00          bytes as they are: nothing reads them\n"
    "#\n"
    "# Notes are concert names (note_frequencies.json): byte $81 is B0. A track's\n"
    "# transpose moves what is heard; `# heard:` comments say what (they are\n"
    "# written by `python3 tools/song_text.py annotate`, never read).\n";

static const char SONG_HEADER_BEFORE[] =
    "# ";

static const char SONG_HEADER_AFTER[] =
    " (build 137). docs/audio.md, \"The song text\", has the format;\n"
    "# effects.txt's header has it in short. `song` lines give the tempo (a tick\n"
    "# is skipped every N frames; 0 never), the PAL tempo and the PAL tick\n"
    "# multiplier, then a `track` line a part. The streams follow in blocks,\n"
    "# which sounds.txt places in the image. Notes are concert names; `# heard:`\n"
    "# comments are written by `python3 tools/song_text.py annotate`.\n";

/* ---- names ---- */

static bool is_upper(char c)
{
    return c >= 'A' && c <= 'Z';
}

static bool is_lower(char c)
{
    return c >= 'a' && c <= 'z';
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* snake: ExcitingTown -> exciting_town, MovementVer2 -> movement_ver2 --
 * an underscore between a lower case letter or digit and a capital, then
 * between a capital and a capital followed by a lower case letter. */
static char *snake(Extract *x, const char *name)
{
    ExStr a;
    ex_str_init(x, &a);
    for (size_t i = 0; name[i] != '\0'; i++) {
        if (i > 0 && (is_lower(name[i - 1]) || is_digit(name[i - 1]))
            && is_upper(name[i])) {
            ex_str_addc(&a, '_');
        }
        ex_str_addc(&a, name[i]);
    }
    const char *s = a.len > 0 ? a.buf : "";
    ExStr b;
    ex_str_init(x, &b);
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (i > 0 && is_upper(s[i - 1]) && is_upper(s[i])
            && is_lower(s[i + 1])) {
            ex_str_addc(&b, '_');
        }
        char c = s[i];
        ex_str_addc(&b, is_upper(c) ? (char)(c - 'A' + 'a') : c);
    }
    return b.len > 0 ? b.buf : ex_strdup(x, "");
}

char *exs_sound_name(Extract *x, const char *label)
{
    const char *u = strchr(label, '_');
    if (u == NULL) {
        ex_fail(x, "%s: a sound's label with no stem", label);
    }
    const char *stem = u + 1;
    if (strcmp(stem, "Null") == 0) {
        return ex_strdup(x, "none");
    }
    bool hex2 = strlen(stem) == 2;
    for (int i = 0; hex2 && i < 2; i++) {
        hex2 = is_digit(stem[i]) || (stem[i] >= 'A' && stem[i] <= 'F');
    }
    if (hex2) {
        return ex_fmt(x, "effect_%c%c",
                      is_upper(stem[0]) ? stem[0] - 'A' + 'a' : stem[0],
                      is_upper(stem[1]) ? stem[1] - 'A' + 'a' : stem[1]);
    }
    return snake(x, stem);
}

char *exs_title(Extract *x, const char *s)
{
    char *out = ex_strdup(x, s);
    bool after_letter = false;
    for (char *p = out; *p != '\0'; p++) {
        if (is_upper(*p) || is_lower(*p)) {
            if (after_letter && is_upper(*p)) {
                *p = (char)(*p - 'A' + 'a');
            } else if (!after_letter && is_lower(*p)) {
                *p = (char)(*p - 'a' + 'A');
            }
            after_letter = true;
        } else {
            after_letter = false;
        }
    }
    return out;
}

/* ---- lines ---- */

/* A line of a file, and the events written on it (their image offsets,
 * in order), which annotate's heard notes are looked up by. */
typedef struct {
    char *text;
    u32  *ats;
    int   nats;
} Line;

typedef struct {
    Line **at;
    int    n;
    int    cap;
} Lines;

static void lines_put(Extract *x, Lines *l, Line *line)
{
    if (l->n == l->cap) {
        int cap = l->cap * 2 + 32;
        Line **nl = ex_alloc(x, (size_t)cap * sizeof *nl);
        if (l->n > 0) {
            memcpy(nl, l->at, (size_t)l->n * sizeof *nl);
        }
        l->at = nl;
        l->cap = cap;
    }
    l->at[l->n++] = line;
}

static Line *lines_add(Extract *x, Lines *l, const char *text)
{
    Line *line = ex_alloc(x, sizeof *line);
    line->text = ex_strdup(x, text);
    lines_put(x, l, line);
    return line;
}

static void lines_extend(Extract *x, Lines *l, const Lines *more)
{
    for (int i = 0; i < more->n; i++) {
        lines_put(x, l, more->at[i]);
    }
}

/* "\n".join(lines) + "\n" (or no newline after, `newline` false). */
static char *lines_join(Extract *x, const Lines *l, bool newline)
{
    ExStr s;
    ex_str_init(x, &s);
    for (int i = 0; i < l->n; i++) {
        if (i > 0) {
            ex_str_addc(&s, '\n');
        }
        ex_str_add(&s, l->at[i]->text);
    }
    if (newline) {
        ex_str_addc(&s, '\n');
    }
    return s.len > 0 ? s.buf : ex_strdup(x, "");
}

/* A row of words being gathered onto one line, with their events. */
typedef struct {
    char **w;
    u32   *at;
    int    n;
    int    cap;
} Row;

static void row_add(Extract *x, Row *r, char *word, u32 at)
{
    if (r->n == r->cap) {
        int cap = r->cap * 2 + 16;
        char **nw = ex_alloc(x, (size_t)cap * sizeof *nw);
        u32 *na = ex_alloc(x, (size_t)cap * sizeof *na);
        if (r->n > 0) {
            memcpy(nw, r->w, (size_t)r->n * sizeof *nw);
            memcpy(na, r->at, (size_t)r->n * sizeof *na);
        }
        r->w = nw;
        r->at = na;
        r->cap = cap;
    }
    r->w[r->n] = word;
    r->at[r->n] = at;
    r->n++;
}

/* flush: the row as one line, four spaces in, its words two apart. */
static void row_flush(Extract *x, Row *r, Lines *out)
{
    if (r->n == 0) {
        return;
    }
    ExStr s;
    ex_str_init(x, &s);
    ex_str_add(&s, "    ");
    for (int i = 0; i < r->n; i++) {
        if (i > 0) {
            ex_str_add(&s, "  ");
        }
        ex_str_add(&s, r->w[i]);
    }
    Line *line = lines_add(x, out, s.buf);
    line->ats = ex_alloc(x, (size_t)r->n * sizeof *line->ats);
    memcpy(line->ats, r->at, (size_t)r->n * sizeof *line->ats);
    line->nats = r->n;
    r->n = 0;
}

/* row[-1].split()[0] */
static char *first_word(Extract *x, const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n') {
        s++;
    }
    size_t n = 0;
    while (s[n] != '\0' && s[n] != ' ' && s[n] != '\t' && s[n] != '\n') {
        n++;
    }
    return ex_strndup(x, s, n);
}

static const char *const FLAG_WORDS[] = {
    "music", "end2", "sound", "pan_anim", "tempo", "chord", "hold", "tie",
    "release", "pause", "comm", "fms", "ams", "volume", "unused_ed",
    "legato", "instrument", "nop", "end", "noise", "mod", "psg_instrument",
    "goto", "loop", "call", "return", "tick", "transpose", "pitch_slide",
    "raw_freq", "fm3_special", "pan", NULL
};

/* The words a row of state changes may go on after. */
static const char *const STATE_WORDS[] = {
    "instrument", "volume", "transpose", "mod", "pan", "chord", "tick",
    "tempo", "pitch_slide", "raw_freq", "legato", "fm3_special", "music",
    "sound", "pause", "pan_anim", NULL
};

static bool is_flag_word(const char *w)
{
    return ex_listed(FLAG_WORDS, w);
}

static bool starts_with_any(const char *s, const char *const *prefixes)
{
    for (int i = 0; prefixes[i] != NULL; i++) {
        if (ex_starts(s, prefixes[i])) {
            return true;
        }
    }
    return false;
}

/* ---- the disassembly ---- */

#define OWN_BYTES 32            /* a bit a sound name: 256 at most */

typedef struct {
    const char *label;
    u32         off;
    ExsTrack   *tracks;
    int         ntracks;
} Header;

typedef struct {
    const char *name;
    int         count;
} Count;

typedef struct {
    Extract       *x;
    const u8      *image;
    size_t         size;
    size_t         span;        /* the tables' length: every 16-bit offset */
    ExsNames      *names;
    ExsEvent     **ev;          /* events by offset */
    u32           *order;       /* offsets in the order first met */
    int            norder;
    u8            *owner;       /* span * OWN_BYTES: the sounds reading it */
    const char   **snames;      /* the sound names, sorted */
    int            nsnames;
    const ExsTrack **targets;   /* the first track to jump there */
    const ExsTrack **starts;    /* the first track to start there */
    const char   **label_at;
    bool          *drums_owner;
    const ExsEvent **ends;      /* by the offset after an event */
    const ExsTrack *cur;
    int            cur_sound;
} Dis;

static const char *kind_name(ExsKind k)
{
    switch (k) {
    case EXS_EV_FLAG:
        return "flag";
    case EXS_EV_AGAIN:
        return "again";
    case EXS_EV_DRUM:
        return "drum";
    case EXS_EV_RAW:
        return "raw";
    case EXS_EV_SLIDE:
        return "slide";
    case EXS_EV_NOTE:
        return "note";
    default:
        return "?";
    }
}

static int sound_index(const Dis *d, const char *name)
{
    int lo = 0;
    int hi = d->nsnames - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(d->snames[mid], name);
        if (c == 0) {
            return mid;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

static void check_at(Dis *d, u32 at)
{
    if (at >= d->span) {
        ex_fail(d->x, "an event at $%X, past every 16-bit offset", at);
    }
}

static void own(Dis *d, u32 at, int sound)
{
    check_at(d, at);
    d->owner[(size_t)at * OWN_BYTES + (size_t)(sound >> 3)] |=
        (u8)(1u << (sound & 7));
}

static bool owned(const Dis *d, u32 at)
{
    for (int k = 0; k < OWN_BYTES; k++) {
        if (d->owner[(size_t)at * OWN_BYTES + (size_t)k] != 0) {
            return true;
        }
    }
    return false;
}

/* min(owner[at]): the first name in sorted order; -1 for none. */
static int first_owner(const Dis *d, u32 at)
{
    for (int s = 0; s < d->nsnames; s++) {
        if (d->owner[(size_t)at * OWN_BYTES + (size_t)(s >> 3)]
            & (1u << (s & 7))) {
            return s;
        }
    }
    return -1;
}

static void on_event(Extract *x, void *ctx, const ExsEvent *ev,
                     const ExsState *st)
{
    Dis *d = ctx;
    (void)st;
    check_at(d, ev->at);
    ExsEvent *old = d->ev[ev->at];
    if (old != NULL && !exs_event_same(old, ev)) {
        ex_fail(x, "$%04X read two ways: %s and %s (%s)", ev->at,
                kind_name(old->kind), kind_name(ev->kind),
                exs_track_name(x, d->cur));
    }
    if (old == NULL) {
        ExsEvent *copy = ex_alloc(x, sizeof *copy);
        *copy = *ev;
        d->ev[ev->at] = copy;
        d->order[d->norder++] = ev->at;
    } else {
        *old = *ev;
    }
    for (u32 k = ev->at; k < ev->at + (u32)ev->size; k++) {
        own(d, k, d->cur_sound);
    }
    if (ev->kind == EXS_EV_FLAG
        && (ev->b == 0xF6 || ev->b == 0xF7 || ev->b == 0xF8)) {
        int second = ev->b == 0xF7 ? 4 : 2;
        u32 to = exs_disp_target_args(ev->at, ev->args, second);
        if (d->targets[to] == NULL) {
            d->targets[to] = d->cur;
        }
    }
}

static void on_drum_event(Extract *x, void *ctx, const ExsEvent *ev,
                          const ExsState *st)
{
    Dis *d = ctx;
    (void)x;
    (void)st;
    check_at(d, ev->at);
    d->drums_owner[ev->at] = true;
}

static const char *label_of(Extract *x, void *ctx, u32 a)
{
    Dis *d = ctx;
    if (a >= d->span || d->label_at[a] == NULL) {
        ex_fail(x, "no label at $%04X", a);
    }
    return d->label_at[a];
}

static char *bytes_text(Extract *x, const u8 *data, size_t n)
{
    ExStr s;
    ex_str_init(x, &s);
    ex_str_add(&s, "bytes");
    for (size_t i = 0; i < n; i++) {
        ex_str_addf(&s, " $%02X", data[i]);
    }
    return s.buf;
}

/* event_text: an event's words. */
static char *event_text(Dis *d, const ExsEvent *ev, bool drums)
{
    Extract *x = d->x;
    ExStr s;
    ex_str_init(x, &s);
    switch (ev->kind) {
    case EXS_EV_FLAG:
        return exs_flag_text(x, ev->b, ev->args, d->names, label_of, d,
                             ev->at);
    case EXS_EV_AGAIN:
        return ex_fmt(x, "again %d", ev->b);
    case EXS_EV_DRUM: {
        const char *name = exs_pitch(d->names, ev->b, true);
        return ev->len < 0 ? ex_strdup(x, name)
                           : ex_fmt(x, "%s %d", name, ev->len);
    }
    case EXS_EV_NOTE:
        ex_str_add(&s, exs_pitch(d->names, ev->b, drums));
        for (int k = 0; k < ev->nparts; k++) {
            ex_str_addc(&s, '+');
            ex_str_add(&s, exs_note(d->names, d->image[ev->at + 1 + (u32)k]));
        }
        if (ev->len >= 0) {
            ex_str_addf(&s, " %d", ev->len);
        }
        return s.buf;
    case EXS_EV_SLIDE:
        ex_str_add(&s, exs_note(d->names, ev->b));
        for (int k = 0; k < ev->nparts; k++) {
            ex_str_addc(&s, '+');
            ex_str_add(&s, exs_note(d->names, d->image[ev->at + 1 + (u32)k]));
        }
        ex_str_addf(&s, " %d slide %s", ev->len,
                    exs_signed_text(x, (u8)ev->speed));
        if (ev->unused != 0) {
            ex_str_addf(&s, " unused %d", ev->unused);
        }
        return s.buf;
    case EXS_EV_RAW:
        ex_str_addf(&s, "freq $%04X %d", ev->word, ev->len);
        if (ev->speed >= 0) {
            ex_str_addf(&s, " slide %s", exs_signed_text(x, (u8)ev->speed));
        }
        return s.buf;
    default:
        ex_fail(x, "an event of no kind");
    }
}

/* header_text: a track's header line. */
static char *header_text(Dis *d, const ExsTrack *t, const char *word)
{
    Extract *x = d->x;
    const char *ch = exs_channel(t->channel);
    char *chan = ch != NULL ? ex_strdup(x, ch) : ex_fmt(x, "$%02X", t->channel);
    ExStr s;
    ex_str_init(x, &s);
    ex_str_addf(&s, "%s %-5s", word, chan);
    ex_str_addf(&s, "  %s", label_of(x, d, t->start));
    ex_str_addf(&s, "  tick %d", t->tick);
    ex_str_addf(&s, "  transpose %s", exs_signed_text(x, t->raw[5]));
    ex_str_addf(&s, "  mod %s", exs_envelope(x, d->names, t->mod));
    ex_str_addf(&s, "  instrument %s",
                exs_instrument(x, d->names, t->instrument));
    ex_str_addf(&s, "  volume %d", t->volume);
    if ((t->flags & 0x80) == 0) {
        ex_str_add(&s, "  not_in_use");
    }
    for (int i = 0; i < EXS_HEADER_FLAG_BIT_COUNT; i++) {
        if (t->flags & EXS_HEADER_FLAG_BITS[i].bit) {
            ex_str_addf(&s, "  %s", EXS_HEADER_FLAG_BITS[i].name);
        }
    }
    return s.buf;
}

/* looks_like_header: header bytes past the count that read as a track
 * whose stream starts at a label. */
static bool looks_like_header(const Dis *d, u32 a)
{
    if ((size_t)a + 9 > d->size) {
        return false;
    }
    const u8 *h = d->image + a;
    u32 start = (u32)h[3] | (u32)h[4] << 8;
    return (h[0] & 0x80) != 0 && exs_channel(h[1]) != NULL
        && start < d->span && d->label_at[start] != NULL;
}

/* lines_for: text lines for image bytes a0..a1 -- labels, events, bytes
 * no stream reaches (the label at a0 too when `first_label`; a block's
 * line names it). */
static Lines lines_for(Dis *d, u32 a0, u32 a1, bool first_label)
{
    Extract *x = d->x;
    Lines out = { NULL, 0, 0 };
    Row row = { NULL, NULL, 0, 0 };
    u32 a = a0;
    while (a < a1) {
        if (d->label_at[a] != NULL && (a != a0 || first_label)) {
            row_flush(x, &row, &out);
            lines_add(x, &out, ex_fmt(x, "%s:", d->label_at[a]));
        }
        const ExsEvent *ev = d->ev[a];
        if (ev == NULL) {
            u32 b = a;
            while (b < a1 && d->ev[b] == NULL
                   && (b == a || d->label_at[b] == NULL)) {
                b++;
            }
            row_flush(x, &row, &out);
            lines_add(x, &out, ex_fmt(x, "    %s    # no stream reaches these",
                                      bytes_text(x, d->image + a, b - a)));
            a = b;
            continue;
        }
        bool drums = d->drums_owner[a];
        if (ev->kind == EXS_EV_FLAG) {
            u8 b = ev->b;
            u32 after = a + (u32)ev->size;
            const ExsEvent *nxt = after < d->span ? d->ev[after] : NULL;
            if ((b == 0xE6 || b == 0xE7) && nxt != NULL
                && nxt->kind == EXS_EV_AGAIN && d->label_at[after] == NULL) {
                const char *word = b == 0xE6 ? "tie" : "release";
                if (row.n > 0) {
                    char *w = first_word(x, row.w[row.n - 1]);
                    if (is_flag_word(w) && strcmp(w, "tie") != 0
                        && strcmp(w, "release") != 0) {
                        row_flush(x, &row, &out);
                    }
                }
                row_add(x, &row, ex_fmt(x, "%s %d", word, nxt->b), a);
                if (row.n >= 8) {
                    row_flush(x, &row, &out);
                }
                a += (u32)ev->size + (u32)nxt->size;
                continue;
            }
            char *text = event_text(d, ev, drums);
            const ExsEvent *before = d->ends[a];
            bool after_note = before != NULL
                && (before->kind == EXS_EV_NOTE || before->kind == EXS_EV_AGAIN
                    || before->kind == EXS_EV_SLIDE
                    || before->kind == EXS_EV_RAW);
            if (b == 0xE6 && before != NULL && !drums && after_note) {
                /* (build 139b) Where the track's pointer rests after a
                 * note, a rest or a length: DoNoteStop finds the hold
                 * there and leaves the note on (cfE6 itself, a note next,
                 * does nothing). */
                row_flush(x, &row, &out);
                lines_add(x, &out, ex_fmt(x, "    %s    # holds the note before "
                                             "up to the next", text));
            } else if (b == 0xE6 || b == 0xE7) {
                row_flush(x, &row, &out);
                lines_add(x, &out, ex_fmt(x, "    %s    # does nothing: the "
                                             "next byte is not a length", text));
            } else if (b == 0xF6 || b == 0xF7 || b == 0xF8 || b == 0xF9
                       || b == 0xF2 || b == 0xE1) {
                row_flush(x, &row, &out);
                lines_add(x, &out, ex_fmt(x, "    %s", text));
            } else {
                if (row.n > 0
                    && !starts_with_any(row.w[row.n - 1], STATE_WORDS)) {
                    row_flush(x, &row, &out);
                }
                row_add(x, &row, text, a);
            }
        } else {
            if (row.n > 0 && is_flag_word(first_word(x, row.w[row.n - 1]))) {
                row_flush(x, &row, &out);
            }
            char *text = event_text(d, ev, drums);
            u32 inner = 0;
            int ninner = 0;
            for (u32 k = a + 1; k < a + (u32)ev->size; k++) {
                if (d->label_at[k] != NULL) {
                    if (ninner == 0) {
                        inner = k;
                    }
                    ninner++;
                }
            }
            if (ninner > 0) {
                /* A loop that lands on a note's length byte: it reads the
                 * length alone, `again`, each time round. */
                int ln = ev->len;
                if ((ev->kind != EXS_EV_NOTE && ev->kind != EXS_EV_DRUM)
                    || ln < 0 || ninner != 1
                    || inner != a + (u32)ev->size - 1) {
                    ex_fail(x, "$%04X: a label inside %s at $%04X", inner,
                            kind_name(ev->kind), a);
                }
                char *space = strrchr(text, ' ');
                char *head = ex_strndup(x, text, (size_t)(space - text));
                row_add(x, &row, ex_fmt(x, "%s %s: %d", head,
                                        d->label_at[inner], ln), a);
                row_flush(x, &row, &out);
                Line *last = out.at[out.n - 1];
                last->text = ex_fmt(x, "%s    # the loop comes back to the %d: "
                                       "`again %d` each time round",
                                    last->text, ln, ln);
            } else {
                row_add(x, &row, text, a);
            }
            if (row.n >= 8) {
                row_flush(x, &row, &out);
            }
        }
        a += (u32)ev->size;
    }
    row_flush(x, &row, &out);
    return out;
}

/* envelope_lines' flush: the steps one space apart. */
static void steps_flush(Extract *x, ExStr *row, int *count, Lines *out)
{
    if (*count == 0) {
        return;
    }
    lines_add(x, out, ex_fmt(x, "    %s", row->buf));
    ex_str_init(x, row);
    *count = 0;
}

/* envelope_lines: one envelope's bytes to text lines -- steps sixteen a
 * line, the codes on lines of their own, a label where a jump lands. */
static void envelope_lines(Extract *x, Lines *out, const u8 *data, size_t n,
                           const char *name)
{
    bool jump[256];
    memset(jump, 0, sizeof jump);
    int njumps = 0;
    size_t i = 0;
    while (i < n) {
        u8 b = data[i];
        if (b == 0x84 || b == 0x85) {
            if (i + 1 >= n) {
                ex_fail(x, "envelope %s: a code with no operand", name);
            }
            if (b == 0x85 && !jump[data[i + 1]]) {
                jump[data[i + 1]] = true;
                njumps++;
            }
            i += 2;
        } else {
            i += 1;
        }
    }
    ExStr row;
    ex_str_init(x, &row);
    int count = 0;
    i = 0;
    while (i < n) {
        if (i < 256 && jump[i]) {
            steps_flush(x, &row, &count, out);
            lines_add(x, out, njumps == 1
                ? ex_fmt(x, "  %s_cycle:", name)
                : ex_fmt(x, "  %s_%zu:", name, i));
        }
        u8 b = data[i];
        if (b == 0x84) {
            steps_flush(x, &row, &count, out);
            lines_add(x, out, ex_fmt(x, "    depth %s",
                                     exs_signed_text(x, data[i + 1])));
            i += 2;
        } else if (b == 0x85) {
            steps_flush(x, &row, &count, out);
            lines_add(x, out, njumps == 1
                ? ex_fmt(x, "    jump %s_cycle", name)
                : ex_fmt(x, "    jump %s_%d", name, data[i + 1]));
            i += 2;
        } else if (b == 0x80 || b == 0x81 || b == 0x83) {
            steps_flush(x, &row, &count, out);
            lines_add(x, out, b == 0x80 ? "    restart"
                              : (b == 0x81 ? "    hold" : "    stop"));
            i += 1;
        } else {
            ex_str_addf(&row, count > 0 ? " %d" : "%d", exs_signed(b));
            count++;
            if (count >= 16) {
                steps_flush(x, &row, &count, out);
            }
            i += 1;
        }
    }
    steps_flush(x, &row, &count, out);
}

/* ---- union-find over the songs ---- */

static int uf_find(int *group, int i)
{
    while (group[i] != i) {
        i = group[i];
    }
    return i;
}

/* ---- annotate ---- */

typedef struct {
    Dis   *d;
    char ***heard;              /* by offset: the texts, first met first */
    int   *nheard;
    int   *heard_parts;
} Heard;

static void on_heard(Extract *x, void *ctx, const ExsEvent *ev,
                     const ExsState *st)
{
    Heard *h = ctx;
    if ((ev->kind != EXS_EV_NOTE && ev->kind != EXS_EV_SLIDE)
        || ev->b == 0x80) {
        return;
    }
    const u8 *img = h->d->image;
    ExStr s;
    ex_str_init(x, &s);
    ex_str_add(&s, exs_heard(h->d->names, ev->b, st->tr));
    for (int k = 0; k < ev->nparts; k++) {
        int tr = k < st->part_len ? st->part_tr : st->tr;
        ex_str_addc(&s, '+');
        ex_str_add(&s, exs_heard(h->d->names, img[ev->at + 1 + (u32)k], tr));
    }
    u32 at = ev->at;
    h->heard_parts[at] = ev->nparts;
    for (int i = 0; i < h->nheard[at]; i++) {
        if (strcmp(h->heard[at][i], s.buf) == 0) {
            return;
        }
    }
    char **more = ex_alloc(x, (size_t)(h->nheard[at] + 1) * sizeof *more);
    if (h->nheard[at] > 0) {
        memcpy(more, h->heard[at], (size_t)h->nheard[at] * sizeof *more);
    }
    more[h->nheard[at]] = s.buf;
    h->heard[at] = more;
    h->nheard[at]++;
}

/* written_note: the note event at `at` as written. */
static char *written_note(Heard *h, u32 at)
{
    Extract *x = h->d->x;
    ExStr s;
    ex_str_init(x, &s);
    for (int k = 0; k <= h->heard_parts[at]; k++) {
        if (k > 0) {
            ex_str_addc(&s, '+');
        }
        ex_str_add(&s, exs_note(h->d->names, h->d->image[at + (u32)k]));
    }
    return s.buf;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

/* HEARD_RE.sub("", line) on each line of a text: from the whitespace
 * before the first "# heard:" to the line's end gone -- the old comments.
 * As in the Python, this also cuts a header's line that names them (the
 * song files' and effects.txt's: "`# heard:` comments ..."). */
static char *strip_heard(Extract *x, const char *text)
{
    ExStr s;
    ex_str_init(x, &s);
    const char *p = text;
    for (;;) {
        const char *nl = strchr(p, '\n');
        size_t n = nl != NULL ? (size_t)(nl - p) : strlen(p);
        size_t keep = n;
        for (size_t h = 0; h + 8 <= n; h++) {
            if (memcmp(p + h, "# heard:", 8) == 0) {
                keep = h;
                while (keep > 0 && is_space(p[keep - 1])) {
                    keep--;
                }
                break;
            }
        }
        ex_str_addn(&s, p, keep);
        if (nl == NULL) {
            break;
        }
        ex_str_addc(&s, '\n');
        p = nl + 1;
    }
    return s.len > 0 ? s.buf : ex_strdup(x, "");
}

/* annotate: walks every counted track of the built headers -- the songs
 * in id order, then the effects in effects.txt's order -- and writes on
 * each line with a note heard as other than written that line's notes as
 * heard. */
static void annotate(Dis *d, const Header *const *built, int nbuilt,
                     Lines *const *files, int nfiles)
{
    Extract *x = d->x;
    Heard h;
    h.d = d;
    h.heard = ex_alloc(x, d->span * sizeof *h.heard);
    h.nheard = ex_alloc(x, d->span * sizeof *h.nheard);
    h.heard_parts = ex_alloc(x, d->span * sizeof *h.heard_parts);
    for (int i = 0; i < nbuilt; i++) {
        const Header *hd = built[i];
        for (int k = 0; k < hd->ntracks; k++) {
            const ExsTrack *t = &hd->tracks[k];
            if (!t->counted || exs_track_drums(t)) {
                continue;
            }
            exs_walk(x, d->image, d->size, t, on_heard, &h);
        }
    }
    for (int f = 0; f < nfiles; f++) {
        const Lines *l = files[f];
        for (int n = 0; n < l->n; n++) {
            Line *line = l->at[n];
            line->text = strip_heard(x, line->text);
            ExStr texts;
            ex_str_init(x, &texts);
            bool differs = false;
            int found = 0;
            for (int i = 0; i < line->nats; i++) {
                u32 at = line->ats[i];
                if (h.nheard[at] == 0) {
                    continue;
                }
                char *written = written_note(&h, at);
                if (h.nheard[at] != 1 || strcmp(h.heard[at][0], written) != 0) {
                    differs = true;
                }
                if (found > 0) {
                    ex_str_add(&texts, "  ");
                }
                for (int j = 0; j < h.nheard[at]; j++) {
                    if (j > 0) {
                        ex_str_addc(&texts, '|');
                    }
                    ex_str_add(&texts, h.heard[at][j]);
                }
                found++;
            }
            if (differs) {
                line->text = ex_fmt(x, "%s    # heard: %s", line->text,
                                    texts.buf);
            }
        }
    }
}

/* ---- disassemble ---- */

typedef struct {
    int         sid;
    const char *label;
} Id;

static const Header *find_header(const Header *h, int n, const char *label)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(h[i].label, label) == 0) {
            return &h[i];
        }
    }
    return NULL;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static u16 ptr(const u8 *image, u32 off)
{
    return (u16)(image[off] | image[off + 1] << 8);
}

void exs_disassemble(Extract *x, const u8 *image, size_t size, u32 base,
                     const char *const *music, int nmusic,
                     const char *const *sfx, int nsfx,
                     const char *const *special, int nspecial,
                     ExsNames *names, u32 driver_gap_end, ExsText *out)
{
    Dis d;
    memset(&d, 0, sizeof d);
    d.x = x;
    d.image = image;
    d.size = size;
    d.span = (size > 0x10000 ? size : 0x10000) + 16;
    d.names = names;

    /* The sounds, by id. */
    int nids = nmusic + nsfx + nspecial;
    Id *ids = ex_alloc(x, (size_t)nids * sizeof *ids);
    for (int i = 0; i < nmusic; i++) {
        ids[i].sid = EXS_FIRST_ID + i;
        ids[i].label = music[i];
    }
    for (int i = 0; i < nsfx; i++) {
        ids[nmusic + i].sid = EXS_FIRST_EFFECT_ID + i;
        ids[nmusic + i].label = sfx[i];
    }
    for (int i = 0; i < nspecial; i++) {
        ids[nmusic + nsfx + i].sid = EXS_FIRST_SPECIAL_ID + i;
        ids[nmusic + nsfx + i].label = special[i];
    }
    for (int i = 0; i < nids; i++) {
        if (ids[i].sid < 256 && names->sounds[ids[i].sid] == NULL) {
            names->sounds[ids[i].sid] = exs_sound_name(x, ids[i].label);
        }
    }

    /* Headers and their tracks. */
    Header *header_of = ex_alloc(x, (size_t)nids * sizeof *header_of);
    int nheaders = 0;
    int ntracks = 0;
    for (int i = 0; i < nids; i++) {
        const char *lbl = ids[i].label;
        if (find_header(header_of, nheaders, lbl) != NULL
            || ex_ends(lbl, "_Null")) {
            continue;
        }
        u32 off = ex_addr(x, lbl) - base;
        if (off >= size) {
            ex_fail(x, "%s: past the driver's image", lbl);
        }
        Header *h = &header_of[nheaders++];
        h->label = lbl;
        h->off = off;
        h->ntracks = image[off];
        h->tracks = ex_alloc(x, (size_t)(h->ntracks + 1) * sizeof *h->tracks);
        const char *sound = exs_sound_name(x, lbl);
        for (int k = 0; k < h->ntracks; k++) {
            u32 at = off + 1 + 9 * (u32)k;
            if ((size_t)at + 9 > size) {
                ex_fail(x, "%s: its header runs past the image", lbl);
            }
            exs_track(&h->tracks[k], sound, at, image + at, true);
        }
        ntracks += h->ntracks;
    }

    /* The sound names, sorted, each a bit of the owner sets. */
    const char **all = ex_alloc(x, (size_t)(ntracks + 1) * sizeof *all);
    int nall = 0;
    for (int i = 0; i < nheaders; i++) {
        for (int k = 0; k < header_of[i].ntracks; k++) {
            all[nall++] = header_of[i].tracks[k].sound;
        }
    }
    qsort(all, (size_t)nall, sizeof *all, cmp_str);
    d.snames = ex_alloc(x, (size_t)(nall + 1) * sizeof *d.snames);
    for (int i = 0; i < nall; i++) {
        if (d.nsnames == 0 || strcmp(d.snames[d.nsnames - 1], all[i]) != 0) {
            d.snames[d.nsnames++] = all[i];
        }
    }
    if (d.nsnames > OWN_BYTES * 8) {
        ex_fail(x, "%d sounds, more than %d", d.nsnames, OWN_BYTES * 8);
    }

    d.ev = ex_alloc(x, d.span * sizeof *d.ev);
    d.order = ex_alloc(x, d.span * sizeof *d.order);
    d.owner = ex_alloc(x, d.span * OWN_BYTES);
    d.targets = ex_alloc(x, d.span * sizeof *d.targets);
    d.starts = ex_alloc(x, d.span * sizeof *d.starts);
    d.label_at = ex_alloc(x, d.span * sizeof *d.label_at);
    d.drums_owner = ex_alloc(x, d.span * sizeof *d.drums_owner);
    d.ends = ex_alloc(x, (d.span + 0x10000) * sizeof *d.ends);

    /* Walk every track. */
    for (int i = 0; i < nheaders; i++) {
        for (int k = 0; k < header_of[i].ntracks; k++) {
            const ExsTrack *t = &header_of[i].tracks[k];
            if (t->start < d.span && d.starts[t->start] == NULL) {
                d.starts[t->start] = t;
            }
            d.cur = t;
            d.cur_sound = sound_index(&d, t->sound);
            exs_walk(x, image, size, t, on_event, &d);
        }
    }

    /* Labels: a track's start by its track, others by the first track to
     * land there, numbered in address order. */
    for (u32 a = 0; a < d.span; a++) {
        if (d.starts[a] != NULL) {
            d.label_at[a] = exs_track_name(x, d.starts[a]);
        }
    }
    Count *counts = ex_alloc(x, (size_t)(ntracks + 1) * sizeof *counts);
    int ncounts = 0;
    for (u32 a = 0; a < d.span; a++) {
        if (d.targets[a] == NULL || d.label_at[a] != NULL) {
            continue;
        }
        char *tn = exs_track_name(x, d.targets[a]);
        int c = -1;
        for (int i = 0; i < ncounts; i++) {
            if (strcmp(counts[i].name, tn) == 0) {
                c = i;
            }
        }
        if (c < 0) {
            c = ncounts++;
            counts[c].name = tn;
            counts[c].count = 0;
        }
        counts[c].count++;
        d.label_at[a] = ex_fmt(x, "%s_%d", tn, counts[c].count);
    }

    /* The songs' files: songs sharing a stream byte go in one file. */
    Id *song_ids = ex_alloc(x, (size_t)(nids + 1) * sizeof *song_ids);
    int nsong_ids = 0;
    for (int i = 0; i < nids; i++) {
        if (ids[i].sid < EXS_FIRST_EFFECT_ID
            && !ex_ends(ids[i].label, "_Null")) {
            song_ids[nsong_ids++] = ids[i];
        }
    }
    bool *is_song = ex_alloc(x, (size_t)(d.nsnames + 1) * sizeof *is_song);
    int *group = ex_alloc(x, (size_t)(d.nsnames + 1) * sizeof *group);
    for (int s = 0; s < d.nsnames; s++) {
        group[s] = s;
    }
    for (int i = 0; i < nsong_ids; i++) {
        int s = sound_index(&d, exs_sound_name(x, song_ids[i].label));
        if (s < 0) {
            ex_fail(x, "%s: a song with no track", song_ids[i].label);
        }
        is_song[s] = true;
    }
    for (u32 a = 0; a < d.span; a++) {
        int first = -1;
        for (int s = 0; s < d.nsnames; s++) {
            if (!is_song[s] || !(d.owner[(size_t)a * OWN_BYTES
                                         + (size_t)(s >> 3)]
                                 & (1u << (s & 7)))) {
                continue;
            }
            if (first < 0) {
                first = s;
            } else {
                group[uf_find(group, s)] = uf_find(group, first);
            }
        }
    }
    /* files: root -> members in song id order */
    int *file_root = ex_alloc(x, (size_t)(nsong_ids + 1) * sizeof *file_root);
    ExStr *file_name = ex_alloc(x, (size_t)(nsong_ids + 1) * sizeof *file_name);
    int nfiles = 0;
    const char **file_of = ex_alloc(x, (size_t)(d.nsnames + 1)
                                       * sizeof *file_of);
    int *file_index_of = ex_alloc(x, (size_t)(d.nsnames + 1)
                                     * sizeof *file_index_of);
    const char ***members = ex_alloc(x, (size_t)(nsong_ids + 1)
                                        * sizeof *members);
    int *nmembers = ex_alloc(x, (size_t)(nsong_ids + 1) * sizeof *nmembers);
    for (int i = 0; i < nsong_ids; i++) {
        char *nm = exs_sound_name(x, song_ids[i].label);
        int root = uf_find(group, sound_index(&d, nm));
        int f = -1;
        for (int k = 0; k < nfiles; k++) {
            if (file_root[k] == root) {
                f = k;
            }
        }
        if (f < 0) {
            f = nfiles++;
            file_root[f] = root;
            ex_str_init(x, &file_name[f]);
            members[f] = ex_alloc(x, (size_t)(nsong_ids + 1)
                                     * sizeof *members[f]);
        } else {
            ex_str_add(&file_name[f], "_and_");
        }
        ex_str_add(&file_name[f], nm);
        members[f][nmembers[f]++] = nm;
    }
    for (int f = 0; f < nfiles; f++) {
        for (int m = 0; m < nmembers[f]; m++) {
            int s = sound_index(&d, members[f][m]);
            file_of[s] = file_name[f].buf;
            file_index_of[s] = f;
        }
    }

    for (int i = 0; i < nheaders; i++) {
        for (int k = 0; k < header_of[i].ntracks; k++) {
            const ExsTrack *t = &header_of[i].tracks[k];
            if (exs_track_drums(t)) {
                exs_walk(x, image, size, t, on_drum_event, &d);
            }
        }
    }
    for (int i = 0; i < d.norder; i++) {
        const ExsEvent *e = d.ev[d.order[i]];
        d.ends[e->at + (u32)e->size] = e;
    }

    /* sounds.txt */
    Lines sl = { NULL, 0, 0 };
    lines_add(x, &sl, SOUNDS_HEADER);
    u32 prio = ex_addr(x, "SoundPriorities") - base;
    for (int k = 0; k < nids; k++) {
        lines_add(x, &sl, ex_fmt(x, "$%02X  %-22s $%02X", ids[k].sid,
                                 exs_sound_name(x, ids[k].label),
                                 image[prio + (u32)k]));
    }
    lines_add(x, &sl, "");
    lines_add(x, &sl, "# The song files, songs/<name>.txt, read in this order.");
    const char **sorted_files = ex_alloc(x, (size_t)(nfiles + 1)
                                            * sizeof *sorted_files);
    for (int f = 0; f < nfiles; f++) {
        sorted_files[f] = file_name[f].buf;
    }
    qsort(sorted_files, (size_t)nfiles, sizeof *sorted_files, cmp_str);
    for (int f = 0; f < nfiles; f++) {
        lines_add(x, &sl, ex_fmt(x, "song_file %s", sorted_files[f]));
    }
    lines_add(x, &sl, "");
    lines_add(x, &sl, INSTRUMENTS_HEADER);
    lines_add(x, &sl, "instruments");
    u32 table = ex_addr(x, "FMInsPtrs") - base;
    int count_ins = names->ninstruments;
    u16 *ptrs = ex_alloc(x, (size_t)(count_ins + 1) * sizeof *ptrs);
    for (int i = 0; i < count_ins; i++) {
        ptrs[i] = ptr(image, table + 2 * (u32)i);
    }
    int i = 1;
    while (i < count_ins) {
        int j = i;
        while (j < count_ins && ptrs[j] == ptrs[i - 1]) {
            j++;
        }
        if (j > i) {
            ExStr s;
            ex_str_init(x, &s);
            ex_str_add(&s, "same");
            for (int k = i - 1; k < j; k++) {
                ex_str_addf(&s, " %s", exs_instrument(x, names, k));
            }
            lines_add(x, &sl, s.buf);
            i = j;
            continue;
        }
        int gap = (int)ptrs[i] - (int)ptrs[i - 1];
        i++;
        if (gap > 0 && gap < EXS_INSTRUMENT_BYTES) {
            lines_add(x, &sl, ex_fmt(x, "share %s %s %d",
                                     exs_instrument(x, names, i - 2),
                                     exs_instrument(x, names, i - 1),
                                     EXS_INSTRUMENT_BYTES - gap));
        }
    }
    lines_add(x, &sl, "");
    lines_add(x, &sl, LAYOUT_HEADER);
    u32 sfx_null = ex_addr(x, "SFX_Null") - base;
    u32 effects_end = sfx_null + 1;
    lines_add(x, &sl, ex_fmt(x, "driver_code %lld",
                             (long long)driver_gap_end
                             - (long long)effects_end));
    lines_add(x, &sl, "streams");
    u32 freqs_end = ex_addr(x, "FMFreqs") - base + 2 * 85;
    u32 a = freqs_end;
    u32 end = (u32)size;
    Lines *blocks = ex_alloc(x, (size_t)(nfiles + 1) * sizeof *blocks);
    while (a < end) {
        if (d.ev[a] == NULL) {
            u32 b = a;
            while (b < end && d.ev[b] == NULL) {
                b++;
            }
            bool padding = b - a == 1 && image[a] == 0;
            lines_add(x, &sl, ex_fmt(x, "    %s    # %s",
                                     bytes_text(x, image + a, b - a),
                                     padding ? "padding"
                                             : "no stream reaches these"));
            a = b;
            continue;
        }
        int s = first_owner(&d, a);
        if (s < 0 || file_of[s] == NULL) {
            ex_fail(x, "$%04X: a stream byte no song reads", a);
        }
        int f = file_index_of[s];
        u32 b = a;
        while (b < end && owned(&d, b)) {
            int sb = first_owner(&d, b);
            if (file_of[sb] == NULL || file_index_of[sb] != f) {
                break;
            }
            b++;
        }
        const char *name = label_of(x, &d, a);
        lines_add(x, &sl, ex_fmt(x, "    block %s", name));
        lines_add(x, &blocks[f], "");
        lines_add(x, &blocks[f], ex_fmt(x, "block %s", name));
        Lines body = lines_for(&d, a, b, false);
        lines_extend(x, &blocks[f], &body);
        a = b;
    }

    /* envelopes.txt */
    Lines el = { NULL, 0, 0 };
    lines_add(x, &el, ENVELOPES_HEADER);
    u32 env_table = ex_addr(x, "ModEnvelopePtrs") - base;
    int nenv = names->nenvelopes;
    u16 *env_ptrs = ex_alloc(x, (size_t)(nenv + 1) * sizeof *env_ptrs);
    for (int k = 0; k < nenv; k++) {
        env_ptrs[k] = ptr(image, env_table + 2 * (u32)k);
    }
    u32 env_end = EXS_PAL_TEMPO_AT;
    for (int k = 0; k < nenv; k++) {
        u32 p = env_ptrs[k];
        u32 q = k + 1 < nenv ? env_ptrs[k + 1] : env_end;
        u32 hi = q < size ? q : (u32)size;
        lines_add(x, &el, ex_fmt(x, "envelope %s", names->envelopes[k]));
        envelope_lines(x, &el, image + p, p < hi ? hi - p : 0,
                       names->envelopes[k]);
        lines_add(x, &el, "");
    }
    long long last = nenv > 0 ? env_ptrs[nenv - 1] : 0;
    long long have = (long long)(env_end < size ? env_end : size) - last;
    long long pad = (long long)env_end - (last + (have > 0 ? have : 0));

    /* effects.txt: from the first effect header to the empty one. */
    Lines xl = { NULL, 0, 0 };
    lines_add(x, &xl, EFFECTS_HEADER);
    bool any = false;
    u32 first = 0;
    for (int k = 0; k < nids; k++) {
        if (ids[k].sid < EXS_FIRST_EFFECT_ID
            || ids[k].sid >= EXS_FIRST_SPECIAL_ID) {
            continue;
        }
        const Header *h = find_header(header_of, nheaders, ids[k].label);
        if (h != NULL && (!any || h->off < first)) {
            first = h->off;
            any = true;
        }
    }
    if (!any) {
        ex_fail(x, "no effect has a header");
    }
    const Header **hdr_at = ex_alloc(x, d.span * sizeof *hdr_at);
    for (int k = 0; k < nheaders; k++) {
        hdr_at[header_of[k].off] = &header_of[k];
    }
    const Header **built = ex_alloc(x, (size_t)(2 * nheaders + 1)
                                       * sizeof *built);
    int nbuilt = 0;
    /* The songs first, in id order, as the assembler places them. */
    for (int k = 0; k < nmusic; k++) {
        const Header *h = find_header(header_of, nheaders, ids[k].label);
        bool dup = false;
        for (int j = 0; h != NULL && j < nbuilt; j++) {
            dup = dup || built[j] == h;
        }
        if (h != NULL && !dup) {
            built[nbuilt++] = h;
        }
    }
    a = first;
    while (a < sfx_null) {
        if (hdr_at[a] != NULL) {
            const Header *h = hdr_at[a];
            lines_add(x, &xl, "");
            lines_add(x, &xl, ex_fmt(x, "effect %s",
                                     exs_sound_name(x, h->label)));
            built[nbuilt++] = h;
            for (int k = 0; k < h->ntracks; k++) {
                lines_add(x, &xl, header_text(&d, &h->tracks[k], "track"));
            }
            a = h->off + 1 + 9 * (u32)h->ntracks;
            /* Header bytes past the count that read as a header whose
             * stream is a label: the ROM's, never read (SFX_Slasher's
             * second). */
            while (a + 9 <= sfx_null && d.ev[a] == NULL && hdr_at[a] == NULL
                   && looks_like_header(&d, a)) {
                ExsTrack t;
                exs_track(&t, exs_sound_name(x, h->label), a, image + a, false);
                lines_add(x, &xl, ex_fmt(x, "%s    # past the header's count: "
                                            "never read",
                                         header_text(&d, &t, "unused_track")));
                a += 9;
            }
            continue;
        }
        u32 b = a + 1;
        while (b < sfx_null && hdr_at[b] == NULL) {
            b++;
        }
        Lines body = lines_for(&d, a, b, true);
        lines_extend(x, &xl, &body);
        a = b;
    }

    /* songs/<file>.txt */
    Lines *fl = ex_alloc(x, (size_t)(nfiles + 1) * sizeof *fl);
    out->songs = ex_alloc(x, (size_t)(nsong_ids + 1) * sizeof *out->songs);
    out->nsongs = 0;
    u32 tempo_at = ex_addr(x, "MusicTempo") - base;
    for (int f = 0; f < nfiles; f++) {
        ExStr title;
        ex_str_init(x, &title);
        ex_str_add(&title, SONG_HEADER_BEFORE);
        for (int m = 0; m < nmembers[f]; m++) {
            if (m > 0) {
                ex_str_add(&title, " and ");
            }
            char *spaced = ex_strdup(x, members[f][m]);
            for (char *p = spaced; *p != '\0'; p++) {
                if (*p == '_') {
                    *p = ' ';
                }
            }
            ex_str_add(&title, exs_title(x, spaced));
        }
        ex_str_add(&title, SONG_HEADER_AFTER);
        lines_add(x, &fl[f], title.buf);
        for (int m = 0; m < nmembers[f]; m++) {
            const Id *id = NULL;
            for (int k = 0; k < nsong_ids && id == NULL; k++) {
                if (strcmp(exs_sound_name(x, song_ids[k].label),
                           members[f][m]) == 0) {
                    id = &song_ids[k];
                }
            }
            u32 idx = (u32)(id->sid - EXS_FIRST_ID);
            int tempo = image[tempo_at + idx];
            int pal_tempo = image[EXS_PAL_TEMPO_AT + idx];
            int pal_tick = image[EXS_PAL_TICK_AT + idx];
            lines_add(x, &fl[f], ex_fmt(x, "song %s  tempo %d  pal_tempo %d  "
                                           "pal_tick %d", members[f][m], tempo,
                                        pal_tempo, pal_tick));
            const Header *h = find_header(header_of, nheaders, id->label);
            for (int k = 0; h != NULL && k < h->ntracks; k++) {
                lines_add(x, &fl[f], header_text(&d, &h->tracks[k], "track"));
            }
            lines_add(x, &fl[f], "");
            ExsSong *song = &out->songs[out->nsongs++];
            song->name = members[f][m];
            song->header_at = h != NULL ? h->off : 0;
            song->tempo = tempo;
            song->pal_tempo = pal_tempo;
            song->pal_tick = pal_tick;
        }
        lines_extend(x, &fl[f], &blocks[f]);
    }

    /* The heard notes, on the effects' and the songs' lines; annotate
     * reads every file the text's bytes come from. */
    Lines **annotated = ex_alloc(x, (size_t)(nfiles + 4) * sizeof *annotated);
    int nannotated = 0;
    annotated[nannotated++] = &sl;
    annotated[nannotated++] = &el;
    annotated[nannotated++] = &xl;
    for (int f = 0; f < nfiles; f++) {
        annotated[nannotated++] = &fl[f];
    }
    annotate(&d, built, nbuilt, annotated, nannotated);

    out->nfiles = 3 + nfiles;
    out->files = ex_alloc(x, (size_t)out->nfiles * sizeof *out->files);
    out->files[0].path = "sounds.txt";
    out->files[0].text = lines_join(x, &sl, true);
    out->files[1].path = "envelopes.txt";
    out->files[1].text = lines_join(x, &el, pad == 0);
    out->files[2].path = "effects.txt";
    out->files[2].text = lines_join(x, &xl, true);
    for (int f = 0; f < nfiles; f++) {
        out->files[3 + f].path = ex_fmt(x, "songs/%s.txt", file_name[f].buf);
        out->files[3 + f].text = lines_join(x, &fl[f], true);
    }
}
