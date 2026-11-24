#ifndef PS2_EXTRACT_EXSHEETS_H
#define PS2_EXTRACT_EXSHEETS_H

/* (build 189) The art's tile sheets by name, for the C steps:
 * tools/disasm_art.py's DisasmArt, and what it reads of extract_art.py
 * (the art/<stem>.bin files, guess_width) and extract_inline_art.py (its
 * labels, output_name). A sheet is the tiles its PNG would hold, padded
 * to the grid, read from the cartridge image. */

#include "extract.h"

/* extract_inline_art.py's DEFAULT_LABELS, NULL-ended. */
extern const char *const EX_INLINE_LABELS[];

/* output_name: a label's sheet name. */
const char *ex_inline_name(Extract *x, const char *label);

/* art_files: the binclude'd art/<stem>.bin files, in (stem, file) order. */
typedef struct {
    const char *stem;
    const char *file;
} ExArtBin;

ExArtBin *ex_art_bins(Extract *x, int *count);

/* guess_width: a sheet's columns. */
int ex_guess_width(const char *stem, int tiles);

typedef struct {
    u8  *px;        /* count tiles of 64 indices, row by row */
    int  count;     /* the grid's tiles, padding included */
    int  across;
    int  rows;
} ExSheet;

typedef struct ExSheets ExSheets;

/* DisasmArt.index: every sheet name, a stop on two sources for one. */
ExSheets      *ex_sheets(Extract *x);
/* DisasmArt.sheet, a stop for a name it has not. */
const ExSheet *ex_sheet(ExSheets *s, const char *name);

#endif /* PS2_EXTRACT_EXSHEETS_H */
