/* (build 190) tools/tracker.py's export half in C (extracker.h): a song
 * laid out as a tracker, as tracker.write_song(b, song) writes it, byte
 * for byte. The pieces keep the Python's names and order:
 *
 *   the timed walk   timed_walk (Ev, TrackTime)
 *   the channels     channel_cells, song_channels, set_marks (Cell, Channel)
 *   the sections     column_key, cut_sections, find_copies, row_fits,
 *                    choose_rows, layout (Section)
 *   the text         ft2_note, ticks_a_second, number_text, cell_text,
 *                    render_cell, section_label, goto_text, write_song
 *
 * Only write_song's full=False: a column that repeats an earlier section's
 * is left blank. The Python's dicts by channel name (a section's cells,
 * copies and marks) are arrays here by a channel's slot, the first channel
 * of its name, so two tracks of one channel would share a column as they
 * share a key there. Ticks are long long: a Python int does not wrap. */

#include "extracker.h"

#include <string.h>

typedef long long Tick;

/* ------------------------------------------------------------------------
 * The timed walk. */

#define TK_HOLD        0xE6     /* cfE6_HoldOn: what DoNoteStop looks for */
#define TK_WALK_LIMIT  200000L

/* Ev.kind */
typedef enum {
    TK_EV_DRUM,
    TK_EV_AGAIN,
    TK_EV_REST,
    TK_EV_NOTE,
    TK_EV_TIE,
    TK_EV_RELEASE
} TkEvKind;

/* A flag read before an event, (b, args): its first
 * EXS_OPERANDS[b - 0xE0] operands are the flag's. */
typedef struct {
    u8 b;
    u8 args[4];
} TkFlag;

typedef struct {
    TkFlag *at;
    int     count;
    int     cap;
} TkFlags;

static int flag_operands(u8 b)
{
    return EXS_OPERANDS[b - 0xE0];
}

static void flags_add(Extract *x, TkFlags *f, u8 b, const u8 *args)
{
    if (f->count == f->cap) {
        int cap = f->cap * 2 + 8;
        TkFlag *grown = ex_alloc(x, (size_t)cap * sizeof *grown);
        if (f->count > 0) {
            memcpy(grown, f->at, (size_t)f->count * sizeof *grown);
        }
        f->at = grown;
        f->cap = cap;
    }
    TkFlag *g = &f->at[f->count++];
    g->b = b;
    memcpy(g->args, args, sizeof g->args);
}

/* The list as it is now, in a copy of its own. */
static TkFlag *flags_copy(Extract *x, const TkFlags *f)
{
    TkFlag *c = ex_alloc(x, (size_t)f->count * sizeof *c);
    if (f->count > 0) {
        memcpy(c, f->at, (size_t)f->count * sizeof *c);
    }
    return c;
}

/* Ev: one timed event of a track, what starts at `time` and lasts `dur`
 * counted ticks, with the flags read just before it (in order). Of its
 * state (tr, part_tr, chord, volume, instrument) the tracker reads the
 * transposes; part_tr is part_len copies of one value. */
typedef struct {
    Tick          time;
    Tick          dur;
    TkEvKind      kind;
    const TkFlag *flags;
    int           nflags;
    int           note;         /* the note, rest or drum byte; -1: None */
    const u8     *parts;        /* the chord parts' bytes, in the image */
    int           nparts;
    bool          held;
    int           tr;
    int           part_tr;
    int           part_len;
} TkEv;

/* TrackTime: a track's events from its start, and where it repeats. */
typedef struct {
    TkEv         *events;
    int           nevents;
    int           loop;         /* the event it comes back to; -1: it ends */
    const TkFlag *end_flags;    /* the flags read before the track's end */
    int           nend_flags;
} TkTime;

static Tick loop_time(const TkTime *tt)
{
    return tt->events[tt->loop].time;
}

static Tick time_length(const TkTime *tt)
{
    if (tt->nevents == 0) {
        return 0;
    }
    const TkEv *e = &tt->events[tt->nevents - 1];
    return e->time + e->dur;
}

typedef struct {
    Extract        *x;
    const u8       *image;
    size_t          size;
    const ExsTrack *track;
} TkImage;

/* timed_walk's byte(i). */
static u8 image_byte(const TkImage *w, Tick i)
{
    if (i < 0 || (unsigned long long)i >= w->size) {
        ex_fail(w->x, "%s: the stream runs past the image at $%04llX",
                exs_track_name(w->x, w->track), (unsigned long long)i);
    }
    return w->image[i];
}

/* A walk's state packed into bytes: every part a fixed size or counted,
 * so two states are equal exactly when their bytes are. */
typedef struct {
    u8    *buf;
    size_t len;
    size_t cap;
} TkKey;

static void key_put(Extract *x, TkKey *k, unsigned long long v, int bytes)
{
    if (k->len + (size_t)bytes > k->cap) {
        size_t cap = k->cap * 2 + 64;
        u8 *grown = ex_alloc(x, cap);
        if (k->len > 0) {
            memcpy(grown, k->buf, k->len);
        }
        k->buf = grown;
        k->cap = cap;
    }
    for (int i = 0; i < bytes; i++) {
        k->buf[k->len++] = (u8)(v >> (8 * i));
    }
}

static void walk_done(Extract *x, TkTime *out, TkEv *events, int nevents,
                      int loop, const TkFlags *end_flags)
{
    out->events = events;
    out->nevents = nevents;
    out->loop = loop;
    out->end_flags = end_flags != NULL ? flags_copy(x, end_flags) : NULL;
    out->nend_flags = end_flags != NULL ? end_flags->count : 0;
}

/* timed_walk(image, track): follows the track as the sequencer does
 * (song_text.walk's rules) and gives its TrackTime. The repeat is found
 * when the whole state comes round again -- the place in the stream, the
 * modes, chord parts, transposes, loop counters, the call stack, the tick
 * multiplier, the last length, the volume and the instrument. */
static void timed_walk(Extract *x, const u8 *image, size_t size,
                       const ExsTrack *t, TkTime *out)
{
    TkImage w = { x, image, size, t };
    ExsSeen seen;
    exs_seen_init(x, &seen);
    TkKey key = { NULL, 0, 0 };
    TkEv *events = NULL;
    int nevents = 0;
    int capevents = 0;

    u32  a = t->start;
    bool raw = (t->flags & 0x08) != 0;
    bool slide = (t->flags & 0x20) != 0;
    int  chord = 0;
    int  tr = t->transpose;
    int  part_tr = 0;
    int  part_len = 0;
    bool has_counter[256];
    u8   counter[256];
    memset(has_counter, 0, sizeof has_counter);
    memset(counter, 0, sizeof counter);
    u32 *stack = NULL;
    int  depth = 0;
    int  stack_cap = 0;
    int  mult = t->tick;
    int  final = 0;
    int  volume = t->volume;
    int  instrument = t->instrument;
    Tick time = 0;
    TkFlags flags = { NULL, 0, 0 };
    long steps = 0;

    memset(out, 0, sizeof *out);
    while (steps < TK_WALK_LIMIT) {
        /* state = (a, raw, slide, chord, tr, part_tr, counters, stack,
         *          mult, final, volume, instrument, flags) */
        key.len = 0;
        key_put(x, &key, a, 4);
        key_put(x, &key, raw, 1);
        key_put(x, &key, slide, 1);
        key_put(x, &key, (unsigned)chord, 1);
        key_put(x, &key, (unsigned)tr & 0xFF, 1);
        key_put(x, &key, (unsigned)part_len, 1);
        key_put(x, &key, part_len > 0 ? (unsigned)part_tr & 0xFF : 0, 1);
        int ncounters = 0;
        for (int s = 0; s < 256; s++) {
            ncounters += has_counter[s] ? 1 : 0;
        }
        key_put(x, &key, (unsigned)ncounters, 2);
        for (int s = 0; s < 256; s++) {
            if (has_counter[s]) {
                key_put(x, &key, (unsigned)s, 1);
                key_put(x, &key, counter[s], 1);
            }
        }
        key_put(x, &key, (unsigned)depth, 4);
        for (int i = 0; i < depth; i++) {
            key_put(x, &key, stack[i], 4);
        }
        key_put(x, &key, (unsigned)mult, 1);
        key_put(x, &key, (unsigned)final, 2);
        key_put(x, &key, (unsigned)volume, 1);
        key_put(x, &key, (unsigned)instrument, 1);
        key_put(x, &key, (unsigned)flags.count, 4);
        for (int i = 0; i < flags.count; i++) {
            key_put(x, &key, flags.at[i].b, 1);
            for (int k = 0; k < flag_operands(flags.at[i].b); k++) {
                key_put(x, &key, flags.at[i].args[k], 1);
            }
        }
        int old = 0;
        if (!exs_seen_add(&seen, key.buf, key.len, nevents, &old)) {
            walk_done(x, out, events, nevents, old, NULL);
            return;
        }
        bool entry_raw = raw;
        for (;;) {
            steps++;
            u8 b = image_byte(&w, a);
            if (b < 0xE0) {
                break;
            }
            int n = flag_operands(b);
            u8 args[4] = { 0, 0, 0, 0 };
            for (int k = 0; k < n; k++) {
                args[k] = image_byte(&w, (Tick)a + 1 + k);
            }
            if (b == 0xF6) {
                a = exs_disp_target(image, size, a + 2);
                continue;
            }
            if (b == 0xF7) {
                u8 slot = args[0];
                u8 c = has_counter[slot] ? counter[slot] : 0;
                if (c == 0) {
                    c = args[1];
                }
                c = (u8)(c - 1);
                has_counter[slot] = true;
                counter[slot] = c;
                a = c != 0 ? exs_disp_target(image, size, a + 4) : a + 5;
                continue;
            }
            if (b == 0xF8) {
                if (depth == stack_cap) {
                    int cap = stack_cap * 2 + 8;
                    u32 *grown = ex_alloc(x, (size_t)cap * sizeof *grown);
                    if (depth > 0) {
                        memcpy(grown, stack, (size_t)depth * sizeof *grown);
                    }
                    stack = grown;
                    stack_cap = cap;
                }
                stack[depth++] = a + 1;
                a = exs_disp_target(image, size, a + 2);
                continue;
            }
            if (b == 0xF9) {
                if (depth == 0) {
                    walk_done(x, out, events, nevents, -1, &flags);
                    return;
                }
                a = stack[--depth] + 2;
                continue;
            }
            flags_add(x, &flags, b, args);
            if (exs_is_end(b)) {
                walk_done(x, out, events, nevents, -1, &flags);
                return;
            }
            if (b == 0xFC) {
                slide = args[0] != 0;
            } else if (b == 0xFD) {
                raw = args[0] != 0;
            } else if (b == 0xFB) {
                tr = exs_signed((u8)(((unsigned)tr + args[0]) & 0xFF));
            } else if (b == 0xE5) {
                chord = args[0];
                part_len = chord - 1 > 0 ? chord - 1 : 0;
                part_tr = tr;
            } else if (b == 0xFA) {
                mult = args[0];
            } else if (b == 0xEC) {
                volume = (volume + args[0]) & 0xFF;
            } else if (b == 0xF0) {
                volume = args[0];
            } else if (b == 0xEF) {
                instrument = args[0];
            }
            a += 1 + (u32)n;
        }
        u8 b = image_byte(&w, a);
        TkEvKind kind = TK_EV_AGAIN;
        int note = -1;
        int ln = -1;
        int size_ev = 1;
        const u8 *parts = NULL;
        int nparts = 0;
        if (exs_track_drums(t)) {
            if (b >= 0x80) {
                kind = TK_EV_DRUM;
                note = b;
                u8 next = image_byte(&w, (Tick)a + 1);
                ln = next < 0x80 ? next : -1;
                size_ev = ln < 0 ? 1 : 2;
            } else {
                kind = TK_EV_AGAIN;
                ln = b;
                size_ev = 1;
            }
        } else if (entry_raw || slide) {
            ex_fail(x, "%s: raw-frequency or pitch-slide mode at $%04X: the "
                       "tracker takes songs, which use neither",
                    exs_track_name(x, t), (unsigned)a);
        } else if (b < 0x80) {
            kind = TK_EV_AGAIN;
            ln = b;
            size_ev = 1;
        } else {
            kind = b == 0x80 ? TK_EV_REST : TK_EV_NOTE;
            note = b;
            Tick k = (Tick)a + 1;
            if (b != 0x80 && chord != 0) {
                for (int i = 0; i < chord - 1; i++) {
                    (void)image_byte(&w, k + i);
                }
                parts = image + k;
                nparts = chord - 1;
                k += chord - 1;
            }
            u8 lb = image_byte(&w, k);
            ln = lb < 0x80 ? lb : -1;
            size_ev = (int)(k - (Tick)a) + (ln < 0 ? 0 : 1);
        }
        /* cfE6 / cfE7 right before a length make it a tie (the hold bit
         * set: not struck) or a release (keyed off, then the hold bit). */
        if (kind == TK_EV_AGAIN && flags.count > 0
            && (flags.at[flags.count - 1].b == 0xE6
                || flags.at[flags.count - 1].b == 0xE7)) {
            kind = flags.at[flags.count - 1].b == 0xE6 ? TK_EV_TIE
                                                       : TK_EV_RELEASE;
            flags.count--;
        }
        if (ln >= 0) {
            final = (ln * mult) & 0xFFFF;
        }
        bool held = image_byte(&w, (Tick)a + size_ev) == TK_HOLD;
        if (nevents == capevents) {
            int cap = capevents * 2 + 64;
            TkEv *grown = ex_alloc(x, (size_t)cap * sizeof *grown);
            if (nevents > 0) {
                memcpy(grown, events, (size_t)nevents * sizeof *grown);
            }
            events = grown;
            capevents = cap;
        }
        TkEv *e = &events[nevents++];
        e->time = time;
        e->dur = final;
        e->kind = kind;
        e->flags = flags_copy(x, &flags);
        e->nflags = flags.count;
        e->note = note;
        e->parts = parts;
        e->nparts = nparts;
        e->held = held;
        e->tr = tr;
        e->part_tr = part_tr;
        e->part_len = part_len;
        flags.count = 0;
        time += final;
        a += (u32)size_ev;
        steps++;
    }
    ex_fail(x, "%s: no repeat after %ld steps", exs_track_name(x, t),
            TK_WALK_LIMIT);
}

/* ------------------------------------------------------------------------
 * A song's channels as timelines of cells. */

/* Cell.kind */
typedef enum {
    TK_NOTE,
    TK_DRUM,
    TK_OFF,
    TK_TIE
} TkCellKind;

/* Cell.art: see set_marks. */
typedef enum {
    TK_ART_NONE,
    TK_ART_HOLD,
    TK_ART_CUT
} TkArt;

/* Cell: what a channel does at one tick -- strike notes (entry numbers,
 * the lead's first, then each chord part's), a drum, `===` (silence),
 * `tie` -- with the changes read before it (`words`, in the driver's
 * order) and whether the note before it is held to it. key() is (kind,
 * notes, drum, words, art, dur). */
typedef struct {
    Tick         time;
    TkCellKind   kind;
    const u8    *notes;
    int          nnotes;
    int          drum;          /* -1: None */
    const char **words;
    int          nwords;
    bool         held;
    Tick         dur;
    TkArt        art;
} TkCell;

typedef struct {
    TkCell *at;
    int     count;
    int     cap;
} TkCells;

static TkCell *cells_add(Extract *x, TkCells *c)
{
    if (c->count == c->cap) {
        int cap = c->cap * 2 + 64;
        TkCell *grown = ex_alloc(x, (size_t)cap * sizeof *grown);
        if (c->count > 0) {
            memcpy(grown, c->at, (size_t)c->count * sizeof *grown);
        }
        c->at = grown;
        c->cap = cap;
    }
    TkCell *n = &c->at[c->count++];
    memset(n, 0, sizeof *n);
    return n;
}

typedef struct {
    const char  *name;
    int          slot;          /* the first channel of this name */
    ExsTrack     track;
    TkCell      *cells;         /* time order, from 0 to `end` */
    int          ncells;
    Tick         end;           /* the tick the last cell ends */
    bool         loops;
    Tick         loop_to;       /* the tick it goes back to, if it loops */
    const char **end_words;     /* changes read before it stops */
    int          nend_words;
} TkChannel;

/* (unsigned)v & 0x3F: Python's v & 0x3F of a negative v too. */
static u8 entry_of(int v)
{
    return (u8)((unsigned)v & 0x3F);
}

/* channel_cells(tt, names): the events as cells, the transposes folded
 * into the notes and the volume written as the driver's value. */
static void channel_cells(Extract *x, const TkTime *tt, const ExsTrack *t,
                          const ExsNames *names, TkCells *out)
{
    bool drums = exs_track_drums(t);
    int volume = t->volume;
    const u8 *last_notes = NULL;
    int nlast = 0;
    int last_drum = -1;
    memset(out, 0, sizeof *out);
    for (int i = 0; i < tt->nevents; i++) {
        const TkEv *e = &tt->events[i];
        const char **words = ex_alloc(x, (size_t)e->nflags * sizeof *words);
        int nwords = 0;
        for (int f = 0; f < e->nflags; f++) {
            const TkFlag *fl = &e->flags[f];
            if (fl->b == 0xFB || fl->b == 0xE6) {
                continue;   /* transposes folded in; a bare hold is `held` */
            }
            if (fl->b == 0xEC) {
                volume = (volume + fl->args[0]) & 0xFF;
                words[nwords++] = ex_fmt(x, "volume %d", volume);
            } else if (fl->b == 0xF0) {
                volume = fl->args[0];
                words[nwords++] = ex_fmt(x, "volume %d", volume);
            } else {
                words[nwords++] = exs_flag_text(x, fl->b, fl->args, names,
                                                NULL, NULL, 0);
            }
        }
        TkCell c;
        memset(&c, 0, sizeof c);
        c.time = e->time;
        c.drum = -1;
        if (e->kind == TK_EV_NOTE) {
            u8 *notes = ex_alloc(x, (size_t)(1 + e->nparts));
            notes[0] = entry_of(e->note + e->tr);
            for (int k = 0; k < e->nparts; k++) {
                int ptr = k < e->part_len ? e->part_tr : e->tr;
                notes[1 + k] = entry_of(e->parts[k] + ptr);
            }
            last_notes = notes;
            nlast = 1 + e->nparts;
            c.kind = TK_NOTE;
            c.notes = last_notes;
            c.nnotes = nlast;
        } else if (e->kind == TK_EV_AGAIN && !drums) {
            if (last_notes == NULL) {
                ex_fail(x, "%s: `again` before any note",
                        exs_track_name(x, t));
            }
            c.kind = TK_NOTE;
            c.notes = last_notes;
            c.nnotes = nlast;
        } else if (e->kind == TK_EV_DRUM) {
            last_drum = e->note;
            c.kind = TK_DRUM;
            c.drum = e->note;
        } else if (e->kind == TK_EV_AGAIN) {
            if (last_drum < 0) {
                ex_fail(x, "%s: `again` before any drum",
                        exs_track_name(x, t));
            }
            c.kind = TK_DRUM;
            c.drum = last_drum;
        } else if (e->kind == TK_EV_REST || e->kind == TK_EV_RELEASE) {
            c.kind = TK_OFF;
        } else if (e->kind == TK_EV_TIE) {
            c.kind = TK_TIE;
        } else {
            ex_fail(x, "%s: an event of kind %d", exs_track_name(x, t),
                    (int)e->kind);
        }
        c.words = words;
        c.nwords = nwords;
        c.held = e->held && !drums;
        c.dur = e->dur;
        *cells_add(x, out) = c;
    }
}

/* set_marks: a note before a `tie` is held to it, any other let go 3
 * ticks early (DoNoteStop); `hold` and `cut` mark the ones that go the
 * other way -- the next cell after the last is the one the channel goes
 * back to. */
static void set_marks(Extract *x, TkChannel *ch)
{
    if (exs_track_drums(&ch->track)) {
        return;
    }
    for (int i = 0; i < ch->ncells; i++) {
        TkCell *c = &ch->cells[i];
        const TkCell *nxt = NULL;
        if (i + 1 < ch->ncells) {
            nxt = &ch->cells[i + 1];
        } else if (ch->loops) {
            for (int j = 0; j < ch->ncells; j++) {
                if (ch->cells[j].time == ch->loop_to) {
                    nxt = &ch->cells[j];
                    break;
                }
            }
            if (nxt == NULL) {
                ex_fail(x, "%s: no cell at tick %lld to go back to",
                        ch->name, ch->loop_to);
            }
        }
        bool tie_next = nxt != NULL && nxt->kind == TK_TIE;
        if (c->held && !tie_next) {
            c->art = TK_ART_HOLD;
        } else if (tie_next && !c->held) {
            c->art = TK_ART_CUT;
        }
    }
}

/* song_channels(b, song): the song's channels, each unrolled to end
 * within one event of the others: a channel whose part repeats sooner is
 * written out until the longest repeat, and every channel goes back to the
 * latest place it can so the ends line up. */
static TkChannel *song_channels(Extract *x, const u8 *image, size_t size,
                                u32 at, const char *song,
                                const ExsNames *names, int *count)
{
    if ((size_t)at >= size) {
        ex_fail(x, "%s: the song's header at $%04X is past the image", song,
                (unsigned)at);
    }
    int ntracks = image[at];
    ExsTrack *tracks = ex_alloc(x, (size_t)ntracks * sizeof *tracks);
    TkTime *times = ex_alloc(x, (size_t)ntracks * sizeof *times);
    for (int i = 0; i < ntracks; i++) {
        u32 h = at + 1 + 9 * (u32)i;
        if ((size_t)h + 9 > size) {
            ex_fail(x, "%s: track %d's header runs past the image", song, i);
        }
        exs_track(&tracks[i], song, h, image + h, true);
    }
    for (int i = 0; i < ntracks; i++) {
        timed_walk(x, image, size, &tracks[i], &times[i]);
    }
    bool have_period = false;
    Tick period = 0;
    for (int i = 0; i < ntracks; i++) {
        if (times[i].loop >= 0) {
            Tick p = time_length(&times[i]) - loop_time(&times[i]);
            if (!have_period || p > period) {
                period = p;
            }
            have_period = true;
        }
    }
    for (int i = 0; i < ntracks; i++) {
        if (times[i].loop < 0) {
            continue;
        }
        Tick p = time_length(&times[i]) - loop_time(&times[i]);
        if (p == 0) {
            /* the Python's period % p divides by zero */
            ex_fail(x, "%s: a part repeats every 0 ticks", song);
        }
        if (ex_mod(period, p) != 0) {
            ex_fail(x, "%s: parts repeat every %lld and %lld ticks; the "
                       "tracker lays them out together only when one "
                       "divides the other", song, p, period);
        }
    }
    bool have_target = false;
    Tick target = 0;
    for (int i = 0; i < ntracks; i++) {
        if (times[i].loop >= 0) {
            Tick v = loop_time(&times[i]) + period;
            if (!have_target || v > target) {
                target = v;
            }
            have_target = true;
        }
    }

    TkChannel *chans = ex_alloc(x, (size_t)ntracks * sizeof *chans);
    for (int i = 0; i < ntracks; i++) {
        const ExsTrack *t = &tracks[i];
        const TkTime *tt = &times[i];
        TkChannel *ch = &chans[i];
        ch->name = exs_channel(t->channel);
        if (ch->name == NULL) {
            ex_fail(x, "%s: no channel %d", exs_track_name(x, t),
                    t->channel);
        }
        ch->slot = i;
        for (int j = 0; j < i; j++) {
            if (strcmp(chans[j].name, ch->name) == 0) {
                ch->slot = chans[j].slot;
                break;
            }
        }
        ch->track = *t;
        TkCells cells;
        channel_cells(x, tt, t, names, &cells);
        if (tt->loop < 0) {
            ch->cells = cells.at;
            ch->ncells = cells.count;
            ch->end = time_length(tt);
            ch->loops = false;
            ch->end_words = ex_alloc(x, (size_t)tt->nend_flags
                                        * sizeof *ch->end_words);
            for (int f = 0; f < tt->nend_flags; f++) {
                const TkFlag *fl = &tt->end_flags[f];
                if (!exs_is_end(fl->b)) {
                    ch->end_words[ch->nend_words++] = exs_flag_text(
                        x, fl->b, fl->args, names, NULL, NULL, 0);
                }
            }
            continue;
        }
        /* Unroll the loop until a pass ends at or past the target, then go
         * back to the latest cell that lies a whole period before the end.
         * The loop's cells are copied first (cells[tt.loop:]): cell i is
         * event i's. */
        int nloop = cells.count - tt->loop;
        TkCell *loop_cells = ex_alloc(x, (size_t)nloop * sizeof *loop_cells);
        memcpy(loop_cells, cells.at + tt->loop,
               (size_t)nloop * sizeof *loop_cells);
        Tick lt = loop_time(tt);
        Tick p = time_length(tt) - lt;
        for (Tick k = 1;; k++) {
            const TkCell *last = &cells.at[cells.count - 1];
            if (last->time + last->dur >= target) {
                break;
            }
            for (int j = 0; j < nloop; j++) {
                TkCell *n = cells_add(x, &cells);
                *n = loop_cells[j];
                n->time = loop_cells[j].time + k * p;
                n->art = TK_ART_NONE;
            }
        }
        /* The latest cell start t with t >= the loop and t + period <=
         * target. */
        bool found = false;
        Tick back = 0;
        for (int j = 0; j < cells.count; j++) {
            Tick ct = cells.at[j].time;
            if (ct >= lt && ct + period <= target) {
                if (!found || ct > back) {
                    back = ct;
                }
                found = true;
            }
        }
        if (!found) {
            ex_fail(x, "%s: no cell a whole period before the end",
                    exs_track_name(x, t));
        }
        Tick end = back + period;
        int kept = 0;
        for (int j = 0; j < cells.count; j++) {
            if (cells.at[j].time < end) {
                cells.at[kept++] = cells.at[j];
            }
        }
        ch->cells = cells.at;
        ch->ncells = kept;
        ch->end = end;
        ch->loops = true;
        ch->loop_to = back;
    }
    for (int i = 0; i < ntracks; i++) {
        set_marks(x, &chans[i]);
    }
    *count = ntracks;
    return chans;
}

/* ------------------------------------------------------------------------
 * Sections: the song cut into spans of the same length, every channel side
 * by side; a channel whose span repeats an earlier one is written once. */

static const Tick SECTION_TICKS[] = { 96, 144, 192, 288, 384 };
static const Tick ROW_TICKS[] = { 48, 24, 12, 6, 4, 3, 2, 1 };
#define TK_SECTION_LENGTHS  5
#define TK_ROW_LENGTHS      8
#define TK_MAX_DELAYED      2   /* cells a section may start between rows */

/* A section's column: Section.cells, .marks and .copy of one name. */
typedef struct {
    TkCell **cells;             /* the cells starting here */
    int      ncells;
    bool     marked;
    Tick     mark;              /* the tick the channel ends, here */
    int      mark_chan;         /* and the channel (the Python's Channel) */
    int      copy;              /* the section it repeats; -1: none */
} TkColumn;

typedef struct {
    int       index;
    Tick      start;
    Tick      length;
    Tick      row;
    TkColumn *cols;             /* by channel slot */
} TkSection;

typedef struct {
    Extract        *x;
    TkChannel      *chans;
    int             nchans;
    TkSection      *secs;
    int             nsecs;
    int            *stamp;      /* row_fits' set of rows: stamp[r] == gen */
    Tick            nstamp;
    int             gen;
} TkSong;

/* Section.rows: -(-length // row). */
static Tick sec_rows(const TkSection *s)
{
    return -ex_floordiv(-s->length, s->row);
}

/* column_key(a, slot) == column_key(b, slot): the section's length, each
 * cell's (time from the start,) + key(), and the mark's (tick from the
 * start, channel). */
static bool cell_same(const TkCell *a, Tick a_start, const TkCell *b,
                      Tick b_start)
{
    if (a->time - a_start != b->time - b_start || a->kind != b->kind
        || a->nnotes != b->nnotes || a->drum != b->drum
        || a->nwords != b->nwords || a->art != b->art || a->dur != b->dur) {
        return false;
    }
    if (a->nnotes > 0 && memcmp(a->notes, b->notes, (size_t)a->nnotes) != 0) {
        return false;
    }
    for (int i = 0; i < a->nwords; i++) {
        if (strcmp(a->words[i], b->words[i]) != 0) {
            return false;
        }
    }
    return true;
}

static bool column_same(const TkSection *a, const TkSection *b, int slot)
{
    const TkColumn *ca = &a->cols[slot];
    const TkColumn *cb = &b->cols[slot];
    if (a->length != b->length || ca->ncells != cb->ncells
        || ca->marked != cb->marked) {
        return false;
    }
    for (int i = 0; i < ca->ncells; i++) {
        if (!cell_same(ca->cells[i], a->start, cb->cells[i], b->start)) {
            return false;
        }
    }
    if (ca->marked && (ca->mark - a->start != cb->mark - b->start
                       || ca->mark_chan != cb->mark_chan)) {
        return false;
    }
    return true;
}

static void cut_sections(TkSong *sg, Tick length)
{
    Extract *x = sg->x;
    Tick end = 1;
    for (int i = 0; i < sg->nchans; i++) {
        if (sg->chans[i].end > end) {
            end = sg->chans[i].end;
        }
    }
    int n = 0;
    for (Tick start = 0; start < end || n == 0; start += length) {
        n++;
    }
    TkSection *secs = ex_alloc(x, (size_t)n * sizeof *secs);
    for (int i = 0; i < n; i++) {
        TkSection *s = &secs[i];
        s->index = i;
        s->start = (Tick)i * length;
        s->length = length < end - s->start ? length : end - s->start;
        s->row = 1;
        s->cols = ex_alloc(x, (size_t)sg->nchans * sizeof *s->cols);
        for (int c = 0; c < sg->nchans; c++) {
            s->cols[c].copy = -1;
        }
    }
    /* The last section holds the ends of the channels that end on it. */
    TkSection *last = &secs[n - 1];
    for (int i = 0; i < sg->nchans; i++) {
        if (sg->chans[i].end == last->start + last->length) {
            last->length += 1;
            break;
        }
    }
    /* Each cell to its section's column, in the channels' order: counted,
     * then placed. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < sg->nchans; i++) {
            const TkChannel *ch = &sg->chans[i];
            for (int j = 0; j < ch->ncells; j++) {
                Tick k = ex_floordiv(ch->cells[j].time, length);
                TkColumn *col = &secs[k < n - 1 ? k : n - 1].cols[ch->slot];
                if (pass == 1) {
                    col->cells[col->ncells] = &ch->cells[j];
                }
                col->ncells++;
            }
        }
        if (pass == 0) {
            for (int i = 0; i < n; i++) {
                for (int c = 0; c < sg->nchans; c++) {
                    TkColumn *col = &secs[i].cols[c];
                    col->cells = ex_alloc(x, (size_t)col->ncells
                                             * sizeof *col->cells);
                    col->ncells = 0;
                }
            }
        }
    }
    for (int i = 0; i < sg->nchans; i++) {
        const TkChannel *ch = &sg->chans[i];
        Tick k = ex_floordiv(ch->end, length);
        TkSection *s = &secs[k < n - 1 ? k : n - 1];
        if (ch->end >= s->start + s->length) {
            s->length = ch->end - s->start + 1;
        }
        TkColumn *col = &s->cols[ch->slot];
        col->marked = true;
        col->mark = ch->end;
        col->mark_chan = i;
    }
    sg->secs = secs;
    sg->nsecs = n;
}

/* find_copies: a column with cells and no mark repeats the first section
 * whose column_key is its (the Python's `first`). */
static void find_copies(TkSong *sg)
{
    for (int i = 0; i < sg->nchans; i++) {
        int slot = sg->chans[i].slot;
        for (int s = 0; s < sg->nsecs; s++) {
            TkColumn *col = &sg->secs[s].cols[slot];
            if (col->marked || col->ncells == 0) {
                continue;
            }
            for (int j = 0; j < s; j++) {
                if (column_same(&sg->secs[j], &sg->secs[s], slot)) {
                    col->copy = j;
                    break;
                }
            }
        }
    }
}

/* row_fits(s, row, chans): the cells between rows in the section's
 * written columns, or -1 (None) when two cells of a column fall in one
 * row. */
static int row_fits(TkSong *sg, const TkSection *s, Tick row)
{
    int delayed = 0;
    for (int i = 0; i < sg->nchans; i++) {
        const TkColumn *col = &s->cols[sg->chans[i].slot];
        if (col->copy >= 0) {
            continue;
        }
        sg->gen++;
        for (int j = 0; j <= col->ncells; j++) {
            Tick t = 0;
            if (j < col->ncells) {
                t = col->cells[j]->time;
            } else if (col->marked) {
                t = col->mark;
            } else {
                break;
            }
            Tick r = ex_floordiv(t - s->start, row);
            Tick d = ex_mod(t - s->start, row);
            if (r < 0) {
                ex_fail(sg->x, "a cell before its section");
            }
            if (r >= sg->nstamp) {
                Tick cap = r * 2 + 64;
                int *grown = ex_alloc(sg->x, (size_t)cap * sizeof *grown);
                if (sg->nstamp > 0) {
                    memcpy(grown, sg->stamp,
                           (size_t)sg->nstamp * sizeof *grown);
                }
                sg->stamp = grown;
                sg->nstamp = cap;
            }
            if (sg->stamp[r] == sg->gen) {
                return -1;
            }
            sg->stamp[r] = sg->gen;
            delayed += d != 0 ? 1 : 0;
        }
    }
    return delayed;
}

static bool fits(TkSong *sg, const TkSection *s, Tick r)
{
    if (ex_mod(s->length, r) != 0 && s != &sg->secs[sg->nsecs - 1]) {
        return false;
    }
    int d = row_fits(sg, s, r);
    return d >= 0 && d <= TK_MAX_DELAYED;
}

/* choose_rows: each section's row, the song's usual row where it fits,
 * else the largest that does (a few cells may start between rows). */
static void choose_rows(TkSong *sg)
{
    Tick usual = 1;
    for (int i = 0; i < TK_ROW_LENGTHS; i++) {
        int ok = 0;
        for (int s = 0; s < sg->nsecs; s++) {
            ok += fits(sg, &sg->secs[s], ROW_TICKS[i]) ? 1 : 0;
        }
        if ((double)ok >= 0.8 * (double)sg->nsecs) {
            usual = ROW_TICKS[i];
            break;
        }
    }
    for (int s = 0; s < sg->nsecs; s++) {
        TkSection *sec = &sg->secs[s];
        if (fits(sg, sec, usual)) {
            sec->row = usual;
            continue;
        }
        sec->row = 1;
        for (int i = 0; i < TK_ROW_LENGTHS; i++) {
            if (ROW_TICKS[i] < usual && fits(sg, sec, ROW_TICKS[i])) {
                sec->row = ROW_TICKS[i];
                break;
            }
        }
    }
}

/* layout: the section length that writes the least (the first of the
 * least), and its sections. */
static void layout(TkSong *sg)
{
    bool have = false;
    Tick best = 0;
    TkSection *best_secs = NULL;
    int best_n = 0;
    for (int i = 0; i < TK_SECTION_LENGTHS; i++) {
        cut_sections(sg, SECTION_TICKS[i]);
        find_copies(sg);
        choose_rows(sg);
        Tick written = 0;
        for (int s = 0; s < sg->nsecs; s++) {
            const TkSection *sec = &sg->secs[s];
            Tick cols = 1;
            for (int c = 0; c < sg->nchans; c++) {
                cols += sec->cols[sg->chans[c].slot].copy < 0 ? 1 : 0;
            }
            written += sec_rows(sec) * cols;
        }
        written += 4 * (Tick)sg->nsecs;
        if (!have || written < best) {
            have = true;
            best = written;
            best_secs = sg->secs;
            best_n = sg->nsecs;
        }
    }
    sg->secs = best_secs;
    sg->nsecs = best_n;
}

/* ------------------------------------------------------------------------
 * Writing a song. */

/* FILE_HEADER: its conversions are the title, the ticks a second and the
 * beats a minute. */
#define FILE_HEADER \
    "# %s (build 139): the song laid out as a tracker. docs/audio.md, \"The\n" \
    "# tracker format\", has the whole format; the game plays this file in place\n" \
    "# of the song's stream text (build 139b). In short:\n" \
    "#\n" \
    "#   song        the tempo: a tick is lost every `tempo`-th 60 Hz frame (0:\n" \
    "#               none), so %s ticks a second here; a beat of 24 ticks is\n" \
    "#               %s BPM. The PAL tempo and tick multiplier follow.\n" \
    "#   channel     a part and how it starts: its tick multiplier (a length is\n" \
    "#               written in ticks, so this only says what lengths it can\n" \
    "#               take: a multiple of it, up to 127 times it), vibrato,\n" \
    "#               instrument and volume (an attenuation: higher is quieter)\n" \
    "#   section     a span of the song, every channel side by side: N rows of T\n" \
    "#               ticks. A column headed `fm1 = 03` plays section 03's fm1\n" \
    "#               again; its cells are left blank (or, if written, must match)\n" \
    "#   a cell      the notes struck (as heard: B0 to C#6; a chord one note a\n" \
    "#               part), a drum, `===` silence, `tie` the note held on, `...`\n" \
    "#               nothing new; then `@N` (struck N ticks after the row),\n" \
    "#               `hold` / `cut` (below) and the changes, in the order the\n" \
    "#               driver makes them: the song text's words (instrument NAME,\n" \
    "#               volume N, mod, pan_anim, chord, tempo, ...)\n" \
    "#   goto SS.RR  where a channel ends and goes back to (section SS, row RR);\n" \
    "#               `end` where it stops\n" \
    "#\n" \
    "# A note lasts until the next cell in its column, and the driver lets it go\n" \
    "# 3 ticks before then -- unless the next cell is a `tie`. `hold` on a note\n" \
    "# keeps it sounding up to the next note anyway; `cut` lets it go before a\n" \
    "# `tie` (which then holds silence), as the ROM's songs do in places.\n"

/* ft2_note: B0 -> B-0, C#5 -> C#5. */
static const char *ft2_note(Extract *x, const char *name)
{
    if (strchr(name, '#') != NULL) {
        return name;
    }
    return ex_fmt(x, "%c-%s", name[0], name + 1);
}

static double ticks_a_second(int tempo)
{
    if (tempo == 0) {
        return 60.0;
    }
    return 60.0 * (double)(tempo - 1) / (double)tempo;
}

/* number_text: f"{v:g}" when v is whole, else f"{v:.1f}". */
static char *number_text(Extract *x, double v)
{
    if (v == (double)(long long)v) {
        return ex_fmt(x, "%g", v);
    }
    return ex_fmt(x, "%.1f", v);
}

/* song.replace("_", " ").title(): a letter after a letter in lower case,
 * any other in upper case (a digit is no letter: "town2b" -> "Town2B"). */
static char *title_of(Extract *x, const char *song)
{
    char *t = ex_strdup(x, song);
    bool after_letter = false;
    for (char *p = t; *p != '\0'; p++) {
        if (*p == '_') {
            *p = ' ';
        }
        if (*p >= 'a' && *p <= 'z') {
            *p = after_letter ? *p : (char)(*p - 'a' + 'A');
            after_letter = true;
        } else if (*p >= 'A' && *p <= 'Z') {
            *p = after_letter ? (char)(*p - 'A' + 'a') : *p;
            after_letter = true;
        } else {
            after_letter = false;
        }
    }
    return t;
}

static char *section_label(Extract *x, int k)
{
    return ex_fmt(x, "%02d", k);
}

/* " ".join's next token. */
static void join_add(ExStr *s, bool *first, const char *tok)
{
    if (!*first) {
        ex_str_addc(s, ' ');
    }
    *first = false;
    ex_str_add(s, tok);
}

/* render_cell (with cell_text): the cell's text in its row. */
static char *render_cell(Extract *x, const TkCell *c, const TkSection *s,
                         const ExsNames *names)
{
    ExStr o;
    ex_str_init(x, &o);
    bool first = true;
    if (c->kind == TK_DRUM) {
        const char *drum = names->drum_of_byte[c->drum];
        join_add(&o, &first,
                 drum != NULL ? drum : ex_fmt(x, "$%02X", (unsigned)c->drum));
    } else if (c->kind == TK_NOTE) {
        for (int i = 0; i < c->nnotes; i++) {
            join_add(&o, &first,
                     ft2_note(x, names->note_of_entry[c->notes[i]]));
        }
    } else if (c->kind == TK_OFF) {
        join_add(&o, &first, "===");
    } else {
        join_add(&o, &first, "tie");
    }
    Tick d = ex_mod(c->time - s->start, s->row);
    if (d != 0) {
        join_add(&o, &first, ex_fmt(x, "@%lld", d));
    }
    if (c->art == TK_ART_HOLD) {
        join_add(&o, &first, "hold");
    } else if (c->art == TK_ART_CUT) {
        join_add(&o, &first, "cut");
    }
    for (int i = 0; i < c->nwords; i++) {
        join_add(&o, &first, c->words[i]);
    }
    return o.buf != NULL ? o.buf : ex_strdup(x, "");
}

static char *goto_text(Extract *x, const TkSection *secs, int nsecs, Tick t)
{
    for (int i = 0; i < nsecs; i++) {
        const TkSection *s = &secs[i];
        if (s->start <= t && t < s->start + s->length) {
            Tick r = ex_floordiv(t - s->start, s->row);
            Tick d = ex_mod(t - s->start, s->row);
            if (d != 0) {
                return ex_fmt(x, "goto %s.%02lld@%lld",
                              section_label(x, s->index), r, d);
            }
            return ex_fmt(x, "goto %s.%02lld", section_label(x, s->index), r);
        }
    }
    ex_fail(x, "no section holds tick %lld", t);
}

/* str.rstrip()'s whitespace, in ASCII. */
static bool is_space(char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r') || (c >= 0x1C && c <= 0x1F);
}

/* `line` to `out` as one line, rstripped or not; `line` empty after. */
static void put_line(ExStr *out, ExStr *line, bool rstrip)
{
    size_t n = line->len;
    while (rstrip && n > 0 && is_space(line->buf[n - 1])) {
        n--;
    }
    if (n > 0) {
        ex_str_addn(out, line->buf, n);
    }
    ex_str_addc(out, '\n');
    line->len = 0;
}

char *ex_tracker_song(Extract *x, const u8 *image, size_t size, u32 header_at,
                      const char *song, int tempo, int pal_tempo, int pal_tick,
                      const ExsNames *names)
{
    TkSong sg;
    memset(&sg, 0, sizeof sg);
    sg.x = x;
    sg.chans = song_channels(x, image, size, header_at, song, names,
                             &sg.nchans);
    if (sg.nchans == 0) {
        ex_fail(x, "%s: a song with no tracks", song);
    }
    layout(&sg);
    const TkChannel *chans = sg.chans;
    const TkSection *secs = sg.secs;

    ExStr out;
    ex_str_init(x, &out);
    ExStr line;
    ex_str_init(x, &line);
    double tps = ticks_a_second(tempo);
    ex_str_addf(&line, FILE_HEADER, title_of(x, song), number_text(x, tps),
                number_text(x, tps * 60.0 / 24.0));
    put_line(&out, &line, false);
    ex_str_addf(&line, "song %s  tempo %d  pal_tempo %d  pal_tick %d", song,
                tempo, pal_tempo, pal_tick);
    put_line(&out, &line, false);
    int width = 0;
    for (int i = 0; i < sg.nchans; i++) {
        int n = (int)strlen(chans[i].name);
        width = n > width ? n : width;
    }
    for (int i = 0; i < sg.nchans; i++) {
        const ExsTrack *t = &chans[i].track;
        ex_str_addf(&line, "channel %-*s  tick %d  mod %s  instrument %s  "
                           "volume %d", width, chans[i].name, t->tick,
                    exs_envelope(x, names, t->mod),
                    exs_instrument(x, names, t->instrument), t->volume);
        if ((t->flags & 0x80) == 0) {
            ex_str_add(&line, "  not_in_use");
        }
        for (int b = 0; b < EXS_HEADER_FLAG_BIT_COUNT; b++) {
            if ((t->flags & EXS_HEADER_FLAG_BITS[b].bit) != 0) {
                ex_str_addf(&line, "  %s", EXS_HEADER_FLAG_BITS[b].name);
            }
        }
        put_line(&out, &line, false);
    }
    put_line(&out, &line, false);

    /* The order, as a comment: which sections each channel writes anew. */
    ex_str_add(&line, "# The order (not read: the sections below say it). `.` "
                      "a column written in its section,");
    put_line(&out, &line, false);
    ex_str_add(&line, "# NN one that plays section NN's again.");
    put_line(&out, &line, false);
    ex_str_add(&line, "#  section  ticks        rows    ");
    for (int i = 0; i < sg.nchans; i++) {
        ex_str_addf(&line, "%s%-5s", i > 0 ? "  " : "", chans[i].name);
    }
    put_line(&out, &line, true);
    for (int s = 0; s < sg.nsecs; s++) {
        const TkSection *sec = &secs[s];
        ex_str_addf(&line, "#  %-7s  %5lld-%-5lld  %2lld x %-2lld  ",
                    section_label(x, sec->index), sec->start,
                    sec->start + sec->length - 1, sec_rows(sec), sec->row);
        for (int i = 0; i < sg.nchans; i++) {
            int copy = sec->cols[chans[i].slot].copy;
            ex_str_addf(&line, "%s%-5s", i > 0 ? "  " : "",
                        copy >= 0 ? section_label(x, copy) : ".");
        }
        put_line(&out, &line, true);
    }
    put_line(&out, &line, false);

    for (int s = 0; s < sg.nsecs; s++) {
        const TkSection *sec = &secs[s];
        Tick nrows = sec_rows(sec);
        put_line(&out, &line, false);
        ex_str_addf(&line, "section %s  %lld rows x %lld ticks    # ticks "
                           "%lld-%lld", section_label(x, sec->index), nrows,
                    sec->row, sec->start, sec->start + sec->length - 1);
        put_line(&out, &line, false);
        const char ***grid = ex_alloc(x, (size_t)sg.nchans * sizeof *grid);
        const char **heads = ex_alloc(x, (size_t)sg.nchans * sizeof *heads);
        int *widths = ex_alloc(x, (size_t)sg.nchans * sizeof *widths);
        for (int i = 0; i < sg.nchans; i++) {
            const TkChannel *ch = &chans[i];
            const TkColumn *col = &sec->cols[ch->slot];
            const char **rows = ex_alloc(x, (size_t)nrows * sizeof *rows);
            for (Tick r = 0; r < nrows; r++) {
                rows[r] = "";
            }
            if (col->copy < 0) {
                for (int j = 0; j < col->ncells; j++) {
                    const TkCell *c = col->cells[j];
                    Tick r = ex_floordiv(c->time - sec->start, sec->row);
                    if (r < 0 || r >= nrows) {
                        ex_fail(x, "%s: a cell past its section's rows",
                                ch->name);
                    }
                    rows[r] = render_cell(x, c, sec, names);
                }
                if (col->marked) {
                    Tick r = ex_floordiv(col->mark - sec->start, sec->row);
                    Tick d = ex_mod(col->mark - sec->start, sec->row);
                    if (r < 0 || r >= nrows) {
                        ex_fail(x, "%s: an end past its section's rows",
                                ch->name);
                    }
                    ExStr m;
                    ex_str_init(x, &m);
                    bool first = true;
                    if (d != 0) {
                        join_add(&m, &first, ex_fmt(x, "@%lld", d));
                    }
                    if (!ch->loops) {
                        for (int k = 0; k < ch->nend_words; k++) {
                            join_add(&m, &first, ch->end_words[k]);
                        }
                        join_add(&m, &first, "end");
                    } else {
                        join_add(&m, &first,
                                 goto_text(x, secs, sg.nsecs, ch->loop_to));
                    }
                    rows[r] = m.buf;
                }
                for (Tick r = 0; r < nrows; r++) {
                    if (rows[r][0] == '\0') {
                        rows[r] = "...";
                    }
                }
            }
            heads[i] = col->copy >= 0
                ? ex_fmt(x, "%s = %s", ch->name, section_label(x, col->copy))
                : ch->name;
            int w = (int)strlen(heads[i]);
            for (Tick r = 0; r < nrows; r++) {
                int n = (int)strlen(rows[r]);
                w = n > w ? n : w;
            }
            grid[i] = rows;
            widths[i] = w;
        }
        ex_str_add(&line, "row | ");
        for (int i = 0; i < sg.nchans; i++) {
            ex_str_addf(&line, "%s%-*s", i > 0 ? " | " : "", widths[i],
                        heads[i]);
        }
        put_line(&out, &line, true);
        for (Tick r = 0; r < nrows; r++) {
            ex_str_addf(&line, " %02lld | ", r);
            for (int i = 0; i < sg.nchans; i++) {
                ex_str_addf(&line, "%s%-*s", i > 0 ? " | " : "", widths[i],
                            grid[i][r]);
            }
            put_line(&out, &line, true);
        }
    }
    return out.buf;
}
