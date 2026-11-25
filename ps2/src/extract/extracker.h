#ifndef PS2_EXTRACT_EXTRACKER_H
#define PS2_EXTRACT_EXTRACKER_H

/* (build 190) tools/tracker.py's export half for the C steps: a song laid
 * out as a tracker, the text of songs/<song>.trk (docs/audio.md, "The
 * tracker format"), as tracker.write_song(b, song) writes it, byte for
 * byte. The music step writes one for each song, as extract_music.py's
 * write_trackers does. */

#include "exsong.h"

/* tracker.write_song(b, song): the .trk text of one song, whose header
 * (a count byte, then 9 bytes a track) is at `header_at` in `image`;
 * tempo, pal_tempo and pal_tick are b.songs[song]'s. */
char *ex_tracker_song(Extract *x, const u8 *image, size_t size, u32 header_at,
                      const char *song, int tempo, int pal_tempo, int pal_tick,
                      const ExsNames *names);

#endif /* PS2_EXTRACT_EXTRACKER_H */
