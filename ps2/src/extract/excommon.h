#ifndef PS2_EXTRACT_EXCOMMON_H
#define PS2_EXTRACT_EXCOMMON_H

/* (build 187) The helpers tools/extract_gamedata.py shares with the other
 * game-data extractors (and tools/ps2png.py's palette strip), for the C
 * steps: the game's name codes, a family of constants, a table's run of
 * labels, the label or file at an address. */

#include "extract.h"
#include "asset_names.h"

/* The codes to $C4 or the field's end ("?" for a code with no letter). A
 * trailing space is kept, as the ROM prints it. */
char *ex_decode_name(Extract *x, const u8 *codes, size_t n);

/* constant_names: {value: name less the prefix} of a family of constants,
 * in the order of their sorted full names; two names for one value stop
 * the run. `skip` is a NULL-ended list of endings left out (or NULL). */
typedef struct {
    long long   value;
    const char *name;
} ExValueName;

typedef struct {
    ExValueName *at;
    int          count;
} ExValueNames;

ExValueNames ex_constant_names(Extract *x, const char *prefix,
                               const char *const *skip);
/* The name for a value, or NULL. */
const char *ex_value_name(const ExValueNames *v, long long value);

/* id_keys: a family numbered 0, 1, 2 ... as a list in value order. */
const char **ex_id_keys(Extract *x, const char *prefix, int *count);

/* run_of: the labels after `first` while part_of(name) holds, and where
 * the first that is not one starts (the ROM's end when none is). */
typedef bool (*ExPartOf)(const char *name, const void *ctx);

const ExLabel **ex_run_of_labels(Extract *x, const char *first,
                                 ExPartOf part_of, const void *ctx,
                                 int *count, u32 *end);
bool ex_part_prefix(const char *name, const void *prefix);
bool ex_part_contains(const char *name, const void *needle);

/* block_at: from an address labels start at to the next label. */
const u8 *ex_block_at(Extract *x, u32 address, size_t *length);

/* label_at: the one label at an address starting with `prefix` (""), or
 * NULL; several stop the run. */
const char *ex_label_at(Extract *x, u32 address, const char *prefix);

/* file_at: the binclude'd file starting at an address, or NULL. */
const char *ex_file_at(Extract *x, u32 address);

/* "art/foo.bin" -> "foo" */
char *ex_file_stem(Extract *x, const char *name);

/* extract_paletteptrs.label_at: the label at an address, where several
 * share it the one that does not close the block before (an `...End`);
 * NULL for none, a stop for more than one. */
const char *ex_label_closing_at(Extract *x, u32 address);

/* extract_paletteptrs.parse_table: a PalettePtrs-style table's entries,
 * to the first whose pointer is no label's address. */
typedef struct {
    int         index;
    const char *symbol;
    long long   dest;
    int         words;
} ExPalEntry;

ExPalEntry *ex_palette_table(Extract *x, const char *label, int *count);

/* ps2png.cram_to_rgb: a CRAM word through the measured DAC ladder. */
void ex_cram_rgb(u16 word, u8 rgb[3]);

/* ps2png.write_palette: 16 wide, one row per line, pixel i index i, the
 * palette the words' colours then black to 256. */
void ex_png_palette(Extract *x, const char *path, const u16 *words, int n);

#endif /* PS2_EXTRACT_EXCOMMON_H */
