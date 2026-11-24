#ifndef PS2_EXTRACT_ASSET_NAMES_H
#define PS2_EXTRACT_ASSET_NAMES_H

/* (build 187) tools/asset_names.py's tables for the C extractors,
 * written into asset_names.c by tools/gen_asset_names.py. Lists end with
 * a NULL (an index of -1). */

#include <stdbool.h>

typedef struct {
    const char *key;
    const char *value;
} ExNamePair;

typedef struct {
    int         index;
    const char *name;
} ExIndexName;

typedef struct {
    const char *label;
    const char *name;
    int         across;
} ExRamp;

extern const char *const EX_PALETTE_NAMES[];
extern const int         EX_PALETTE_NAME_COUNT;
extern const ExIndexName EX_TILESET_NAMES[];
extern const ExNamePair  EX_SPRITE_ART_NAMES[];
extern const ExNamePair  EX_LEVEL_ANIM_NAMES[];
extern const ExNamePair  EX_ART_RENAMES[];
extern const char *const EX_ART_NOT_ART[];
extern const char *const EX_PICTURE_ART[];
extern const ExRamp      EX_RAMPS[];
extern const ExNamePair  EX_DRUM_SAMPLE_FILES[];
extern const ExNamePair  EX_MUSIC_FILES[];
extern const ExNamePair  EX_TABLE_FILES[];
extern const ExNamePair  EX_SCRIPT_FILES[];
extern const char *const EX_INSTRUMENT_NAMES[];
extern const char *const EX_ENVELOPE_NAMES[];

/* A pair list's value for a key, or NULL. */
const char *ex_pair(const ExNamePair *list, const char *key);
/* Whether a NULL-ended list holds a string. */
bool ex_listed(const char *const *list, const char *s);
/* asset_names.art_name: the file's new name, or the stem itself. */
const char *ex_art_name(const char *stem);
/* PALETTE_NAMES[index], or NULL (the Python's KeyError). */
const char *ex_palette_name(int index);

#endif /* PS2_EXTRACT_ASSET_NAMES_H */
