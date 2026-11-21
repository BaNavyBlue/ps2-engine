#ifndef PS2_SONGTEXT_H
#define PS2_SONGTEXT_H

/* (build 137a) The song text: every song and effect, and the sound
 * driver's tables, as text under assets/music (sounds.txt, envelopes.txt,
 * effects.txt, songs/<name>.txt; docs/audio.md, "The song text"), built
 * into the driver's image -- the bytes smps.c plays, which until 137a came
 * from SoundData.bin. The instruments come from fm_instruments.json, by
 * name; the notes are named from note_frequencies.json's "note", the drums
 * from drums.json. tools/song_text.py's assemble reads the same files the
 * same way (make roundtrip packs them with it): the unedited text builds
 * the ROM's image byte for byte.
 *
 * songtext_build refuses, naming the file and line, anything it cannot
 * read: a word it does not know, a name no file defines (note, drum,
 * instrument, envelope, sound, label), a number out of range, a label or
 * block given twice, a block not placed, ids not $81 on in order, the
 * envelopes past $100, the image past 64 KB. */

#include "smps.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `music_dir` is assets/music (or the asset tree's music directory);
 * `reader` the pack reader or NULL for the disk. Fills d->image, size,
 * spc_sfx_ptrs, music_count and sfx_count; d->freqs must already hold the
 * note frequencies (they go into the image after the driver's code). */
bool songtext_build(const char *music_dir, Ps2Reader reader, SmpsData *d);

/* (build 139b) A song with a tracker file, songs/<song>.trk (tools/
 * tracker.py; docs/audio.md, "Build 139b"), is read from it in place of its
 * song file's stream text, and refused, by file and line, in tracker.py's
 * words. Which of the two is read, first found: a .trk in the mods folder,
 * the song file there, the game's .trk, the game's song file. `is_mod`
 * says whether a path is the mods folder's (assetfs_is_mod); without one
 * (the tools) a song's .trk is read wherever there is one. */
void songtext_set_mod_test(bool (*is_mod)(const char *path));

#ifdef __cplusplus
}
#endif

#endif
