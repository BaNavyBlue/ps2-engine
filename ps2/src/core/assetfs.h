#ifndef PS2_ASSETFS_H
#define PS2_ASSETFS_H

#include "ps2_types.h"

#include <stddef.h>
#include <stdio.h>

/* Every asset file, held in RAM.
 *
 * assetfs_preload reads each file named in a manifest into memory once, at
 * startup. After that every loader -- PNG, TMX, JSON, scripts, collision
 * tables -- gets its bytes from here, so entering a screen, changing level
 * or opening a door decodes from memory and never waits on the disk.
 *
 * The manifest is a list of paths, one a line, written by
 * tools/make_manifest.py (`make manifest`, and the last step of
 * `make extract`). A file that is asked for but was not in it is read from
 * disk on the spot, kept, and reported once -- so a hand-added asset still
 * works and the message says the manifest wants regenerating.
 *
 * Save files are not assets and do not come through here. */

bool assetfs_preload(const char *manifest_path);

/* The file's bytes, with a NUL after the last one so text can be parsed
 * in place. Owned by the cache: do not free, do not write. NULL if the
 * file cannot be read at all. */
const u8 *assetfs_get(const char *path, size_t *size);

/* (build 163) Whether a file is there to be read -- in the cache, the
 * mods folder, the pack or on disk -- without reading or keeping it, and
 * saying nothing when it is not: for the languages' folders
 * (src/game/lang.c), whose files stand in for the game's own where they
 * are. */
bool assetfs_exists(const char *path);

/* A malloc'd, writable, NUL-terminated copy, for parsers that cut their
 * input up as they go. The caller frees it. */
char *assetfs_copy_text(const char *path);

/* Walks text a line at a time without touching it. Copies the next line,
 * less its line ending, into `out`; false when there are no more. */
bool assetfs_next_line(const char **cursor, char *out, size_t cap);

void assetfs_stats(int *files, size_t *bytes, int *late);

/* (build 125) The pack: every file the game reads, and what COMPUTER PLAYS
 * runs, linked into the program itself (tools/make_pack.py, `make dist`;
 * PS2_PACKED). A build without it has none, and each call below says so --
 * the tree's builds read their files as they always have.
 *
 * With it, assetfs_preload takes the pack's files in place of the
 * manifest's (their bytes are the pack's, not copied), and a loader that
 * reads a file itself (the tables, nametables, window maps, the sound
 * data, the shaders) asks assetfs_pack_find first. */

/* The file's bytes in the pack, with a NUL after the last, and its size;
 * NULL when there is no pack or the file is not in it. */
const u8 *assetfs_pack_find(const char *path, size_t *size);

/* (build 138b) The mods folder (docs/mods.md): a file under assets/ is read
 * from `dir`/<the rest of its path> where a file is there, in place of the
 * game's own -- by assetfs_get, the preload, assetfs_pack_find and
 * everything that reads through it. (build 138c) Every build's: the game's
 * own is the pack's in a packed game, the file under assets/ in a tree
 * build. Set before the first read; NULL or "" for none (the probe and the
 * tools never set one). */
void assetfs_set_mods(const char *dir);

/* How many of the pack's files a mod replaces now, and the i'th's path in
 * the pack ("assets/..."), for the startup report. */
int         assetfs_mods_count(void);
const char *assetfs_mods_path(int i);

/* (build 138b) Every mod whose path starts with `prefix` set aside, for a
 * loader that refused one: from here on the game's own files are read
 * there (the preload's copies too). Returns how many. */
int assetfs_mods_drop(const char *prefix);

/* (build 157d) A loader refused `path`: when its bytes are a mod's, the mod
 * is set aside as assetfs_mods_drop does, the console told, and true -- the
 * loader reads the game's own then. False when they were the game's own
 * (the refusal stands). Every loader of a picture, a map, a JSON file, a
 * script or the credits asks it (docs/mods.md, "When a mod is refused"). */
bool assetfs_mod_refused(const char *path);

/* (build 139b) Whether `path` is the mods folder's now: a mod is there and
 * not set aside. The song text asks it which of a song's two files is read
 * (songtext_set_mod_test: a .trk or a song file, the mod's first). */
bool assetfs_is_mod(const char *path);

/* (build 157d) The same for every mod under `folder` (a path ending in
 * '/'): a picture's description, picture and slot map are one thing.
 * Returns how many were set aside. */
int assetfs_mods_refused_under(const char *folder);

/* The pack's files one by one, for COMPUTER PLAYS to write out what its
 * driver reads (autoplay.c): false past the last (or with no pack). */
int  assetfs_pack_count(void);
bool assetfs_pack_entry(int i, const char **path, const u8 **data,
                        size_t *size);

/* A hash of the whole pack, 0 with none: whether files written out from
 * it are this program's. */
u32  assetfs_pack_id(void);

/* A file's bytes whole, for a loader that decodes them itself: the pack's
 * when it has the file (*owned false), else read from disk now, not kept
 * and not reported (*owned true: the caller frees it). NULL if neither. */
u8  *assetfs_read_whole(const char *path, size_t *size, bool *owned);

/* (build 125) A file read a piece at a time, as fopen and fread do, from
 * the pack when it has the file and from disk when not: for the loaders
 * that decode as they read (tables, nametables, window maps). */
typedef struct {
    FILE     *fh;
    const u8 *p;
    size_t    left;
} AssetReader;

bool   asset_open(AssetReader *r, const char *path);
size_t asset_read(AssetReader *r, void *dst, size_t n);
void   asset_close(AssetReader *r);

#endif /* PS2_ASSETFS_H */
