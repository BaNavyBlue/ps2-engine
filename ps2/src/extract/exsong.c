/* (build 190) tools/song_text.py's vocabulary in C: exsong.h. */

#include "exsong.h"

#include <string.h>

const ExDrumName EX_DRUM_NAMES[] = {
    { 0x80, "high_tom" },     /* the disassembly: "Left Speaker (High Tom)" */
    { 0x81, "mid_tom" },
    { 0x82, "low_tom" },      /* the disassembly: "Right Speaker (Low Tom)" */
    { 0x84, "snare" },
    { 0x88, "kick" },
    { 0x90, "unused" },
    { 0xA0, "open_hat" },
    { 0xC0, "closed_hat" },
};

const int EX_DRUM_NAME_COUNT =
    (int)(sizeof EX_DRUM_NAMES / sizeof EX_DRUM_NAMES[0]);

/* $E0-$FF: cfHandler and each flag's reads. */
const u8 EXS_OPERANDS[32] = {
    1, 0, 1, 2, 1, 1, 0, 0,     /* E0-E7 */
    1, 1, 1, 1, 1, 0, 1, 1,     /* E8-EF */
    1, 0, 0, 1, 1, 1, 2, 4,     /* F0-F7 */
    2, 0, 1, 1, 1, 1, 4, 1,     /* F8-FF */
};

const ExsFlagBit EXS_HEADER_FLAG_BITS[] = {
    { 0x20, "pitch_slide" },
    { 0x08, "raw_freq" },
    { 0x40, "flag_6" },
    { 0x10, "flag_4" },
    { 0x04, "lent" },
    { 0x02, "held" },
    { 0x01, "fm3_special" },
};

const int EXS_HEADER_FLAG_BIT_COUNT =
    (int)(sizeof EXS_HEADER_FLAG_BITS / sizeof EXS_HEADER_FLAG_BITS[0]);

static const char *const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

bool exs_is_end(u8 b)
{
    return b == 0xF2 || b == 0xE1;
}

const char *exs_channel(int channel)
{
    switch (channel) {
    case 0x80:
        return "drums";
    case 0:
        return "fm1";
    case 1:
        return "fm2";
    case 2:
        return "fm3";
    case 4:
        return "fm4";
    case 5:
        return "fm5";
    case 6:
        return "fm6";
    default:
        return NULL;
    }
}

/* ---- names ---- */

/* semitone: NOTE_RE, ^([A-G]#?)(-?\d+)$; false for anything else. */
static bool semitone(const char *name, long long *out)
{
    if (name == NULL || name[0] < 'A' || name[0] > 'G') {
        return false;
    }
    char letter[3] = { name[0], '\0', '\0' };
    const char *p = name + 1;
    if (*p == '#') {
        letter[1] = '#';
        p++;
    }
    bool neg = false;
    if (*p == '-') {
        neg = true;
        p++;
    }
    if (*p < '0' || *p > '9') {
        return false;
    }
    long long v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    if (*p != '\0') {
        return false;
    }
    int idx = -1;
    for (int i = 0; i < 12; i++) {
        if (strcmp(NOTE_NAMES[i], letter) == 0) {
            idx = i;
        }
    }
    if (idx < 0) {
        return false;
    }
    *out = idx + 12 * (neg ? -v : v);
    return true;
}

static char *semitone_name(Extract *x, long long semi)
{
    return ex_fmt(x, "%s%lld", NOTE_NAMES[ex_mod(semi, 12)],
                  ex_floordiv(semi, 12));
}

void exs_names_init(Extract *x, ExsNames *n, const char *const *entry_notes,
                    int nentries, const char *const *instruments,
                    int ninstruments, const char *const *envelopes,
                    int nenvelopes)
{
    memset(n, 0, sizeof *n);
    long long first = 0;
    if (nentries < 2 || !semitone(entry_notes[1], &first)) {
        ex_fail(x, "note_frequencies.json: entry 1 has no note name");
    }
    n->note_of_entry[0] = "silence";
    for (int e = 1; e < EXS_NOTES; e++) {
        if (e < 64) {
            long long got = 0;
            bool have = e < nentries && semitone(entry_notes[e], &got);
            if (!have || got != first + e - 1) {
                ex_fail(x, "note_frequencies.json: entry %d is %s, not a "
                           "semitone above entry %d", e,
                        e < nentries && entry_notes[e] != NULL
                            ? jo_quote(x, entry_notes[e]) : "None", e - 1);
            }
        }
        n->note_of_entry[e] = semitone_name(x, first + e - 1);
    }
    for (int i = 0; i < EX_DRUM_NAME_COUNT; i++) {
        n->drum_of_byte[EX_DRUM_NAMES[i].byte] = EX_DRUM_NAMES[i].name;
    }
    n->instruments = instruments;
    n->ninstruments = ninstruments;
    n->envelopes = envelopes;
    n->nenvelopes = nenvelopes;
}

/* A list's [i] as Python indexes it: a negative i from the end. */
static const char *note_at(const ExsNames *n, int i)
{
    if (i < 0) {
        i += EXS_NOTES;
    }
    if (i < 0 || i >= EXS_NOTES) {
        return NULL;
    }
    return n->note_of_entry[i];
}

const char *exs_note(const ExsNames *n, u8 b)
{
    if (b == 0x80) {
        return "rest";
    }
    const char *s = note_at(n, (int)b - 0x80);
    return s != NULL ? s : "?";
}

const char *exs_heard(const ExsNames *n, u8 b, int transpose)
{
    return n->note_of_entry[(b + transpose) & 0x3F];
}

const char *exs_pitch(const ExsNames *n, u8 b, bool drums)
{
    if (drums && n->drum_of_byte[b] != NULL) {
        return n->drum_of_byte[b];
    }
    return exs_note(n, b);
}

const char *exs_instrument(Extract *x, const ExsNames *n, int i)
{
    if (i >= 0 && i < n->ninstruments) {
        return n->instruments[i];
    }
    return ex_fmt(x, "$%02X", i);
}

const char *exs_envelope(Extract *x, const ExsNames *n, int i)
{
    if (i == 0) {
        return "off";
    }
    if (i - 1 < n->nenvelopes) {
        return n->envelopes[i - 1];
    }
    return ex_fmt(x, "$%02X", i);
}

const char *exs_sound(Extract *x, const ExsNames *n, int id)
{
    if (id >= 0 && id < 256 && n->sounds[id] != NULL) {
        return n->sounds[id];
    }
    return ex_fmt(x, "$%02X", id);
}

/* ---- numbers ---- */

int exs_signed(u8 b)
{
    return b > 127 ? (int)b - 256 : (int)b;
}

char *exs_signed_text(Extract *x, u8 b)
{
    int v = exs_signed(b);
    return v >= 0 ? ex_fmt(x, "+%d", v) : ex_fmt(x, "%d", v);
}

u32 exs_disp_target(const u8 *image, size_t size, u32 second)
{
    if (second == 0 || second >= size) {
        return 0;
    }
    u32 d = (u32)image[second - 1] | (u32)image[second] << 8;
    return (second + d) & 0xFFFF;
}

u32 exs_disp_target_args(u32 at, const u8 *args, int second_index)
{
    u32 second = at + (u32)second_index;
    u32 d = (u32)args[second_index - 2] | (u32)args[second_index - 1] << 8;
    return (second + d) & 0xFFFF;
}

/* ---- flag_text ---- */

static const char *label_or_fail(Extract *x, ExsLabelOf label_of, void *ctx,
                                 u32 addr)
{
    if (label_of == NULL) {
        ex_fail(x, "a jump's words with no labels to name it by");
    }
    return label_of(x, ctx, addr);
}

static const char *pan_anim_name(Extract *x, u8 v)
{
    static const char *const kinds[7] = {
        "off", "sweep", "cycle", "left_first", "right_first", "sweep_5",
        "sweep_6"
    };
    return v < 7 ? kinds[v] : ex_fmt(x, "%d", v);
}

static const char *pan_name(Extract *x, u8 v)
{
    switch (v) {
    case 0xC0:
        return "center";
    case 0x80:
        return "left";
    case 0x40:
        return "right";
    case 0x00:
        return "none";
    default:
        return ex_fmt(x, "$%02X", v);
    }
}

/* "W off" for 0, "W on" (or `one`) for 1, else "W N". */
static char *switch_text(Extract *x, const char *word, u8 v, const char *one)
{
    if (v == 0) {
        return ex_fmt(x, "%s off", word);
    }
    if (v == 1) {
        return ex_fmt(x, "%s %s", word, one);
    }
    return ex_fmt(x, "%s %d", word, v);
}

char *exs_flag_text(Extract *x, u8 b, const u8 *args, const ExsNames *n,
                    ExsLabelOf label_of, void *ctx, u32 at)
{
    switch (b) {
    case 0xE0:
        if (args[0] == 0) {
            return ex_strdup(x, "music stop");
        }
        if (args[0] == 1) {
            return ex_strdup(x, "music restart");
        }
        return ex_fmt(x, "music restart %d", args[0]);
    case 0xE1:
        return ex_strdup(x, "end2");
    case 0xE2:
        return ex_fmt(x, "sound %s", exs_sound(x, n, args[0]));
    case 0xE3:
        return ex_fmt(x, "pan_anim %s %d", pan_anim_name(x, args[0]), args[1]);
    case 0xE4:
        return ex_fmt(x, "tempo %d", args[0]);
    case 0xE5:
        if (args[0] == 0) {
            return ex_strdup(x, "chord off");
        }
        return ex_fmt(x, "chord %d", args[0]);
    case 0xE6:
        return ex_strdup(x, "hold");
    case 0xE7:
        return ex_strdup(x, "release");
    case 0xE8:
        return switch_text(x, "pause", args[0], "on");
    case 0xE9:
        return ex_fmt(x, "comm %d", args[0]);
    case 0xEA:
        return ex_fmt(x, "fms %d", args[0]);
    case 0xEB:
        return ex_fmt(x, "ams %d", args[0]);
    case 0xEC:
        return ex_fmt(x, "volume %s", exs_signed_text(x, args[0]));
    case 0xED:
        return ex_strdup(x, "unused_ed");
    case 0xEE:
        return switch_text(x, "legato", args[0], "on");
    case 0xEF:
        return ex_fmt(x, "instrument %s", exs_instrument(x, n, args[0]));
    case 0xF0:
        return ex_fmt(x, "volume %d", args[0]);
    case 0xF1:
        return ex_strdup(x, "nop");
    case 0xF2:
        return ex_strdup(x, "end");
    case 0xF3:
        return ex_fmt(x, "noise %d", args[0]);
    case 0xF4:
        return ex_fmt(x, "mod %s", exs_envelope(x, n, args[0]));
    case 0xF5:
        return ex_fmt(x, "psg_instrument %d", args[0]);
    case 0xF6:
        return ex_fmt(x, "goto %s", label_or_fail(
            x, label_of, ctx, exs_disp_target_args(at, args, 2)));
    case 0xF7: {
        const char *to = label_or_fail(x, label_of, ctx,
                                       exs_disp_target_args(at, args, 4));
        if (args[0] != 0) {
            return ex_fmt(x, "loop %s %d times counter %d", to, args[1],
                          args[0]);
        }
        return ex_fmt(x, "loop %s %d times", to, args[1]);
    }
    case 0xF8:
        return ex_fmt(x, "call %s", label_or_fail(
            x, label_of, ctx, exs_disp_target_args(at, args, 2)));
    case 0xF9:
        return ex_strdup(x, "return");
    case 0xFA:
        return ex_fmt(x, "tick %d", args[0]);
    case 0xFB:
        return ex_fmt(x, "transpose %s", exs_signed_text(x, args[0]));
    case 0xFC:
        return switch_text(x, "pitch_slide", args[0], "on");
    case 0xFD:
        return switch_text(x, "raw_freq", args[0], "on");
    case 0xFE:
        return ex_fmt(x, "fm3_special %d %d %d %d", args[0], args[1],
                      args[2], args[3]);
    case 0xFF:
        return ex_fmt(x, "pan %s", pan_name(x, args[0]));
    default:
        ex_fail(x, "flag $%02X", b);
    }
}

/* ---- tracks ---- */

void exs_track(ExsTrack *t, const char *sound, u32 header_at, const u8 *raw,
               bool counted)
{
    memset(t, 0, sizeof *t);
    t->sound = sound;
    t->header_at = header_at;
    memcpy(t->raw, raw, 9);
    t->counted = counted;
    t->flags = raw[0];
    t->channel = raw[1];
    t->tick = raw[2];
    t->start = (u32)raw[3] | (u32)raw[4] << 8;
    t->transpose = exs_signed(raw[5]);
    t->mod = raw[6];
    t->instrument = raw[7];
    t->volume = raw[8];
}

bool exs_track_drums(const ExsTrack *t)
{
    return (t->channel & 0x80) != 0;
}

char *exs_track_name(Extract *x, const ExsTrack *t)
{
    const char *ch = exs_channel(t->channel);
    if (ch == NULL) {
        return ex_fmt(x, "%s_ch%d", t->sound, t->channel);
    }
    return ex_fmt(x, "%s_%s", t->sound, ch);
}

/* ---- the walk ---- */

bool exs_event_same(const ExsEvent *a, const ExsEvent *b)
{
    if (a->size != b->size || a->kind != b->kind) {
        return false;
    }
    switch (a->kind) {
    case EXS_EV_FLAG:
        return a->b == b->b && a->nargs == b->nargs
            && memcmp(a->args, b->args, (size_t)a->nargs) == 0;
    case EXS_EV_AGAIN:
        return a->b == b->b;
    case EXS_EV_DRUM:
        return a->b == b->b && a->len == b->len;
    case EXS_EV_RAW:
        return a->word == b->word && a->speed == b->speed
            && a->len == b->len;
    case EXS_EV_SLIDE:
        return a->b == b->b && a->nparts == b->nparts
            && a->unused == b->unused && a->speed == b->speed
            && a->len == b->len;
    case EXS_EV_NOTE:
        return a->b == b->b && a->nparts == b->nparts && a->len == b->len;
    default:
        return false;
    }
}

/* The walks' seen states: byte strings in an open-addressed table. */
static u32 fnv(const u8 *p, size_t n)
{
    u32 h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

void exs_seen_init(Extract *x, ExsSeen *s)
{
    s->x = x;
    s->cap = 1024;
    s->count = 0;
    s->keys = ex_alloc(x, s->cap * sizeof *s->keys);
    s->hashes = ex_alloc(x, s->cap * sizeof *s->hashes);
    s->values = ex_alloc(x, s->cap * sizeof *s->values);
}

static bool set_find(const ExsSeen *s, const u8 *key, size_t n, u32 h,
                     size_t *slot)
{
    size_t i = h & (s->cap - 1);
    while (s->keys[i] != NULL) {
        const u8 *k = s->keys[i];
        size_t kn = (size_t)k[0] | (size_t)k[1] << 8;
        if (s->hashes[i] == h && kn == n && memcmp(k + 2, key, n) == 0) {
            *slot = i;
            return true;
        }
        i = (i + 1) & (s->cap - 1);
    }
    *slot = i;
    return false;
}

bool exs_seen_add(ExsSeen *s, const u8 *key, size_t n, int value, int *old)
{
    if (n > 0xFFFF) {
        ex_fail(s->x, "a walk's state of %zu bytes", n);
    }
    u32 h = fnv(key, n);
    size_t slot = 0;
    if (set_find(s, key, n, h, &slot)) {
        if (old != NULL) {
            *old = s->values[slot];
        }
        return false;
    }
    if (2 * (s->count + 1) > s->cap) {
        ExsSeen bigger = *s;
        bigger.cap = s->cap * 2;
        bigger.keys = ex_alloc(s->x, bigger.cap * sizeof *bigger.keys);
        bigger.hashes = ex_alloc(s->x, bigger.cap * sizeof *bigger.hashes);
        bigger.values = ex_alloc(s->x, bigger.cap * sizeof *bigger.values);
        for (size_t i = 0; i < s->cap; i++) {
            if (s->keys[i] != NULL) {
                size_t j = s->hashes[i] & (bigger.cap - 1);
                while (bigger.keys[j] != NULL) {
                    j = (j + 1) & (bigger.cap - 1);
                }
                bigger.keys[j] = s->keys[i];
                bigger.hashes[j] = s->hashes[i];
                bigger.values[j] = s->values[i];
            }
        }
        *s = bigger;
        set_find(s, key, n, h, &slot);
    }
    u8 *k = ex_alloc(s->x, n + 2);
    k[0] = (u8)(n & 0xFF);
    k[1] = (u8)(n >> 8);
    memcpy(k + 2, key, n);
    s->keys[slot] = k;
    s->hashes[slot] = h;
    s->values[slot] = value;
    s->count++;
    return true;
}

typedef struct {
    Extract       *x;
    const u8      *image;
    size_t         size;
    const ExsTrack *t;
} WalkImage;

static u8 wbyte(const WalkImage *w, long long i)
{
    if (i < 0 || (size_t)i >= w->size) {
        ex_fail(w->x, "%s: the stream runs past the image at $%04llX",
                exs_track_name(w->x, w->t), i);
    }
    return w->image[i];
}

/* A growing byte buffer for a state's key. */
typedef struct {
    u8    *buf;
    size_t len;
    size_t cap;
} KeyBuf;

static void key_put(Extract *x, KeyBuf *k, u32 v, int bytes)
{
    if (k->len + (size_t)bytes > k->cap) {
        size_t cap = k->cap * 2 + 64;
        u8 *nb = ex_alloc(x, cap);
        if (k->len > 0) {
            memcpy(nb, k->buf, k->len);
        }
        k->buf = nb;
        k->cap = cap;
    }
    for (int i = 0; i < bytes; i++) {
        k->buf[k->len++] = (u8)(v >> (8 * i));
    }
}

void exs_walk(Extract *x, const u8 *image, size_t size, const ExsTrack *t,
              ExsOnEvent on, void *ctx)
{
    const long limit = 400000;
    WalkImage w = { x, image, size, t };
    ExsSeen seen;
    exs_seen_init(x, &seen);
    KeyBuf key = { NULL, 0, 0 };

    u32  a = t->start;
    bool raw = (t->flags & 0x08) != 0;
    bool slide = (t->flags & 0x20) != 0;
    int  chord = 0;
    int  tr = t->transpose;
    ExsState st = { tr, 0, 0 };
    bool has_counter[256];
    u8   counter[256];
    memset(has_counter, 0, sizeof has_counter);
    memset(counter, 0, sizeof counter);
    u32 *stack = NULL;
    int  depth = 0;
    int  stack_cap = 0;
    long steps = 0;

    while (steps < limit) {
        /* state = (a, raw, slide, chord, tr, part_tr, counters, stack) */
        key.len = 0;
        key_put(x, &key, a, 4);
        key_put(x, &key, raw, 1);
        key_put(x, &key, slide, 1);
        key_put(x, &key, (u32)chord, 1);
        key_put(x, &key, (u32)(tr & 0xFF), 1);
        key_put(x, &key, (u32)st.part_len, 1);
        key_put(x, &key, (u32)(st.part_tr & 0xFF), 1);
        for (int s = 0; s < 256; s++) {
            if (has_counter[s]) {
                key_put(x, &key, (u32)s, 1);
                key_put(x, &key, counter[s], 1);
            }
        }
        key_put(x, &key, 0x100, 2);       /* the counters' end */
        for (int i = 0; i < depth; i++) {
            key_put(x, &key, stack[i], 4);
        }
        if (!exs_seen_add(&seen, key.buf, key.len, 0, NULL)) {
            return;
        }
        bool entry_raw = raw;
        st.tr = tr;
        /* The flags before the note. */
        for (;;) {
            steps++;
            u8 b = wbyte(&w, a);
            if (b < 0xE0) {
                break;
            }
            ExsEvent ev;
            memset(&ev, 0, sizeof ev);
            ev.at = a;
            ev.kind = EXS_EV_FLAG;
            ev.b = b;
            ev.nargs = EXS_OPERANDS[b - 0xE0];
            ev.size = 1 + ev.nargs;
            ev.len = -1;
            ev.speed = -1;
            for (int k = 0; k < ev.nargs; k++) {
                ev.args[k] = wbyte(&w, (long long)a + 1 + k);
            }
            st.tr = tr;
            if (b == 0xF6) {
                on(x, ctx, &ev, &st);
                a = exs_disp_target(image, size, a + 2);
                continue;
            }
            if (b == 0xF7) {
                on(x, ctx, &ev, &st);
                u8 slot = ev.args[0];
                u8 count = ev.args[1];
                u8 c = has_counter[slot] ? counter[slot] : 0;
                if (c == 0) {
                    c = count;
                }
                c = (u8)(c - 1);
                has_counter[slot] = true;
                counter[slot] = c;
                a = c != 0 ? exs_disp_target(image, size, a + 4) : a + 5;
                continue;
            }
            if (b == 0xF8) {
                on(x, ctx, &ev, &st);
                if (depth == stack_cap) {
                    int cap = stack_cap * 2 + 8;
                    u32 *ns = ex_alloc(x, (size_t)cap * sizeof *ns);
                    if (depth > 0) {
                        memcpy(ns, stack, (size_t)depth * sizeof *ns);
                    }
                    stack = ns;
                    stack_cap = cap;
                }
                stack[depth++] = a + 1;
                a = exs_disp_target(image, size, a + 2);
                continue;
            }
            if (b == 0xF9) {
                on(x, ctx, &ev, &st);
                if (depth == 0) {
                    return;
                }
                a = stack[--depth] + 2;
                continue;
            }
            on(x, ctx, &ev, &st);
            if (exs_is_end(b)) {
                return;
            }
            if (b == 0xFC) {
                slide = ev.args[0] != 0;
            } else if (b == 0xFD) {
                raw = ev.args[0] != 0;
            } else if (b == 0xFB) {
                tr = exs_signed((u8)((tr + ev.args[0]) & 0xFF));
            } else if (b == 0xE5) {
                chord = ev.args[0];
                st.part_len = chord - 1 > 0 ? chord - 1 : 0;
                st.part_tr = st.part_len > 0 ? tr : 0;  /* () has no value */
            }
            a += 1 + (u32)ev.nargs;
        }
        /* The note, by the mode: raw-ness as it was when the read began
         * (ProcFMTrack_Raw never looks again), slide as it is now. */
        u8 b = wbyte(&w, a);
        ExsEvent ev;
        memset(&ev, 0, sizeof ev);
        ev.at = a;
        ev.b = b;
        ev.len = -1;
        ev.speed = -1;
        if (exs_track_drums(t)) {
            if (b >= 0x80) {
                u8 next = wbyte(&w, (long long)a + 1);
                ev.kind = EXS_EV_DRUM;
                ev.len = next < 0x80 ? next : -1;
                ev.size = ev.len < 0 ? 1 : 2;
            } else {
                ev.kind = EXS_EV_AGAIN;
                ev.size = 1;
            }
        } else if (entry_raw) {
            ev.kind = EXS_EV_RAW;
            ev.word = (u16)(b << 8 | wbyte(&w, (long long)a + 1));
            ev.speed = slide ? wbyte(&w, (long long)a + 2) : -1;
            u32 k = a + (slide ? 3 : 2);
            ev.len = wbyte(&w, k);
            ev.size = (int)(k + 1 - a);
        } else if (slide) {
            ev.kind = EXS_EV_SLIDE;
            u32 k = a + 1;
            if (b != 0x80 && chord != 0) {
                for (int i = 0; i < chord - 1; i++) {
                    (void)wbyte(&w, (long long)k + i);
                }
                ev.nparts = chord - 1 > 0 ? chord - 1 : 0;
                k += (u32)(chord - 1);
            }
            ev.unused = wbyte(&w, k);
            ev.speed = wbyte(&w, (long long)k + 1);
            ev.len = wbyte(&w, (long long)k + 2);
            ev.size = (int)(k + 3 - a);
        } else if (b < 0x80) {
            ev.kind = EXS_EV_AGAIN;
            ev.size = 1;
        } else {
            ev.kind = EXS_EV_NOTE;
            u32 k = a + 1;
            if (b != 0x80 && chord != 0) {
                for (int i = 0; i < chord - 1; i++) {
                    (void)wbyte(&w, (long long)k + i);
                }
                ev.nparts = chord - 1 > 0 ? chord - 1 : 0;
                k += (u32)(chord - 1);
            }
            u8 ln = wbyte(&w, k);
            ev.len = ln < 0x80 ? ln : -1;
            ev.size = (int)(k - a) + (ev.len < 0 ? 0 : 1);
        }
        st.tr = tr;
        on(x, ctx, &ev, &st);
        a += (u32)ev.size;
        steps++;
    }
}
