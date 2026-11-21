#ifndef PS2_TYPES_H
#define PS2_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Ported game logic uses these rather than raw stdint types. The original
 * is 16-bit throughout and some formulas depend on wrapping at word
 * boundaries, so widening later is a deliberate decision made in one place
 * rather than an accident spread across the codebase. */
typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef uint64_t u64;

/* H40 mode, no interlace. The original sets VDP reg $8C81 once at boot and
 * never changes it, so these are constants rather than runtime state. */
#define SCREEN_W 320
#define SCREEN_H 224

/* NTSC Mega Drive field rate. Game logic runs at exactly one tick per
 * frame because every animation counter in the original assumes it. */
#define TICK_HZ 59.92

/* (build 125) A file's bytes from somewhere else than the disk -- the
 * game's pack (assetfs_pack_find) -- or NULL: the sound loaders, which
 * tools without assetfs link too, take one through a setter
 * (smps_set_reader, fm_set_reader, q_synth_set_reader). */
typedef const u8 *(*Ps2Reader)(const char *path, size_t *size);

#endif /* PS2_TYPES_H */
