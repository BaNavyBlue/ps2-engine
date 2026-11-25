#ifndef PS2_EXTRACT_EXMUSIC_H
#define PS2_EXTRACT_EXMUSIC_H

/* (build 190) The music step's parts (tools/extract_music.py and the
 * song_text.py it runs): the driver's image to the song text
 * (ex_songtext.c, song_text.disassemble and annotate), and the step
 * itself (ex_music.c). */

#include "exsong.h"

/* One file of the song text: its path under the music folder and its
 * text, as song_text.disassemble's {relative path: text} holds it. */
typedef struct {
    const char *path;          /* "sounds.txt", "songs/<file>.txt" */
    char       *text;
} ExsFile;

/* A song as the built text gives it (song_text.Built's headers and songs):
 * the tracker's input. */
typedef struct {
    const char *name;
    u32         header_at;     /* in the image */
    int         tempo;
    int         pal_tempo;
    int         pal_tick;
} ExsSong;

typedef struct {
    ExsFile *files;            /* sounds.txt, envelopes.txt, effects.txt,
                                  then each song file */
    int      nfiles;
    ExsSong *songs;            /* every song, in the order of its id */
    int      nsongs;
} ExsText;

/* song_text.disassemble(image, labels, music, sfx, special, base, names,
 * driver_gap_end), then song_text.annotate's `# heard:` comments on the
 * lines whose notes are heard as other than written. `image` is the
 * driver's image (offset 0 is SoundDriverBase, at `base` in the ROM); the
 * pointer tables' labels in order; `names` gets the sounds' names. */
void exs_disassemble(Extract *x, const u8 *image, size_t size, u32 base,
                     const char *const *music, int nmusic,
                     const char *const *sfx, int nsfx,
                     const char *const *special, int nspecial,
                     ExsNames *names, u32 driver_gap_end, ExsText *out);

/* song_text.sound_name: Music_ExcitingTown -> exciting_town, SFX_B8 ->
 * effect_b8, *_Null -> none. */
char *exs_sound_name(Extract *x, const char *label);

/* Python's str.title(): a letter after a non-letter in capitals, every
 * other letter in lower case. */
char *exs_title(Extract *x, const char *s);

#endif /* PS2_EXTRACT_EXMUSIC_H */
