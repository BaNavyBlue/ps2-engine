#ifndef PS2_EXTRACT_EXSONG_H
#define PS2_EXTRACT_EXSONG_H

/* (build 190) tools/song_text.py's vocabulary for the C steps: the sound
 * driver's channels, flags and header bits, the names a song's text uses
 * (song_text.Names), a flag's words (flag_text), a track's header (Track)
 * and the walk of a track's stream as the sequencer reads it (walk). The
 * music step (ex_music.c, ex_songtext.c) and the tracker (ex_tracker.c)
 * share them, as extract_music.py and tracker.py share song_text.py; the
 * drums step takes the drums' names from here, as extract_music.py takes
 * them from extract_drums.py. */

#include "extract.h"

#define EXS_FIRST_ID          0x81
#define EXS_FIRST_EFFECT_ID   0xA0
#define EXS_FIRST_SPECIAL_ID  0xD8
#define EXS_PAL_TEMPO_AT      0x100
#define EXS_PAL_TICK_AT       0x120
#define EXS_TEMPO_ENTRIES     32
#define EXS_ROOTS             7
#define EXS_INSTRUMENT_BYTES  26
#define EXS_NOTES             0x60     /* note bytes $80-$DF */

/* extract_drums.DRUMS: the drums by the byte a song sends, the port's
 * names, in that order. */
typedef struct {
    u8          byte;
    const char *name;
} ExDrumName;

extern const ExDrumName EX_DRUM_NAMES[];
extern const int        EX_DRUM_NAME_COUNT;

/* song_text.OPERANDS: a flag's operand count, EXS_OPERANDS[b - 0xE0]. */
extern const u8 EXS_OPERANDS[32];

/* song_text.ENDS: $F2 and $E1, after which nothing is read. */
bool exs_is_end(u8 b);

/* song_text.CHANNELS: "drums", "fm1"-"fm6", or NULL for another byte. */
const char *exs_channel(int channel);

/* song_text.HEADER_FLAG_BITS, in its order. */
typedef struct {
    u8          bit;
    const char *name;
} ExsFlagBit;

extern const ExsFlagBit EXS_HEADER_FLAG_BITS[];
extern const int        EXS_HEADER_FLAG_BIT_COUNT;

/* ---- song_text.Names ---- */

typedef struct {
    const char        *note_of_entry[EXS_NOTES];  /* "silence", "B0", ... */
    const char        *drum_of_byte[256];         /* NULL: no drum */
    const char *const *instruments;
    int                ninstruments;
    const char *const *envelopes;
    int                nenvelopes;
    const char        *sounds[256];               /* NULL: none named */
} ExsNames;

/* Names(freqs_json, drum_names(), instruments, envelopes): `entry_notes`
 * is note_frequencies.json's "note" of each entry, in order (entry 1 a
 * note name, each next a semitone up, to entry 63); the drums are
 * EX_DRUM_NAMES. A stop when the entries are not a semitone apart. The
 * sounds start empty (the disassembly names them). */
void exs_names_init(Extract *x, ExsNames *n, const char *const *entry_notes,
                    int nentries, const char *const *instruments,
                    int ninstruments, const char *const *envelopes,
                    int nenvelopes);

const char *exs_note(const ExsNames *n, u8 b);                 /* note() */
const char *exs_heard(const ExsNames *n, u8 b, int transpose); /* heard() */
const char *exs_pitch(const ExsNames *n, u8 b, bool drums);    /* pitch() */
const char *exs_instrument(Extract *x, const ExsNames *n, int i);
const char *exs_envelope(Extract *x, const ExsNames *n, int i);
const char *exs_sound(Extract *x, const ExsNames *n, int id);

/* ---- numbers ---- */

int   exs_signed(u8 b);                       /* signed() */
char *exs_signed_text(Extract *x, u8 b);      /* "+3", "-12", "+0" */

/* disp_target: the displacement's second byte plus the 16-bit word. */
u32 exs_disp_target(const u8 *image, size_t size, u32 second);
/* disp_target_args: a jump at `at` whose displacement is the operand
 * bytes ending at operand `second_index` (1-based: the flag is 0). */
u32 exs_disp_target_args(u32 at, const u8 *args, int second_index);

/* ---- flag_text ---- */

/* Names a jump target, or stops (song_text's label_of). */
typedef const char *(*ExsLabelOf)(Extract *x, void *ctx, u32 addr);

/* A flag's words: `b` $E0-$FF and its operands. `label_of` (and `ctx`)
 * may be NULL where no jump is ever given (the tracker's cells). */
char *exs_flag_text(Extract *x, u8 b, const u8 *args, const ExsNames *n,
                    ExsLabelOf label_of, void *ctx, u32 at);

/* ---- song_text.Track ---- */

typedef struct {
    const char *sound;
    u32         header_at;
    u8          raw[9];
    bool        counted;
    u8          flags;
    u8          channel;
    u8          tick;
    u32         start;
    int         transpose;
    u8          mod;
    u8          instrument;
    u8          volume;
} ExsTrack;

/* The track whose 9 header bytes are `raw` (copied). */
void  exs_track(ExsTrack *t, const char *sound, u32 header_at, const u8 *raw,
                bool counted);
bool  exs_track_drums(const ExsTrack *t);
char *exs_track_name(Extract *x, const ExsTrack *t);   /* "sound_fm1" */

/* ---- the walks' seen states ---- */

/* A dict from byte strings (a walk's state, packed) to an int, in the
 * step's arena. */
typedef struct {
    Extract *x;
    u8     **keys;          /* [len lo, len hi, bytes...] */
    u32     *hashes;
    int     *values;
    size_t   cap;
    size_t   count;
} ExsSeen;

void exs_seen_init(Extract *x, ExsSeen *s);
/* seen.setdefault: adds key -> value and gives true, or gives false with
 * *old the value already there (`old` may be NULL). */
bool exs_seen_add(ExsSeen *s, const u8 *key, size_t n, int value, int *old);

/* ---- song_text.walk ---- */

typedef enum {
    EXS_EV_FLAG,
    EXS_EV_AGAIN,
    EXS_EV_DRUM,
    EXS_EV_RAW,
    EXS_EV_SLIDE,
    EXS_EV_NOTE
} ExsKind;

/* One event of a stream, song_text.Event: `info` by kind --
 *   flag:  b, args[nargs]
 *   again: b (the length)
 *   drum:  b, len (-1: none)
 *   note:  b, parts (nparts bytes at at + 1), len (-1: none)
 *   slide: b, parts, unused, speed, len
 *   raw:   word, speed (-1: none), len */
typedef struct {
    u32     at;
    int     size;
    ExsKind kind;
    u8      b;
    u8      args[4];
    int     nargs;
    int     nparts;
    int     len;
    int     speed;
    int     unused;
    u16     word;
} ExsEvent;

/* song_text's Event equality: the same size, kind and info. */
bool exs_event_same(const ExsEvent *a, const ExsEvent *b);

/* The state an event is read in: the transpose and the chord parts'
 * transposes (part_len copies of part_tr). */
typedef struct {
    int tr;
    int part_tr;
    int part_len;
} ExsState;

typedef void (*ExsOnEvent)(Extract *x, void *ctx, const ExsEvent *ev,
                           const ExsState *st);

/* walk(image, track, on_event): follows the track as the sequencer does,
 * an event at a time; stops at a track end, an empty return, or a state
 * already seen (the song's repeat). A stream past the image stops the
 * step. */
void exs_walk(Extract *x, const u8 *image, size_t size, const ExsTrack *t,
              ExsOnEvent on, void *ctx);

#endif /* PS2_EXTRACT_EXSONG_H */
