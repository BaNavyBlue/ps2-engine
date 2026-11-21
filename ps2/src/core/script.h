#ifndef PS2_SCRIPT_H
#define PS2_SCRIPT_H

#include "ps2_types.h"

/* A block of window text, as `tools/extract_script.py` writes it.
 *
 * `script_id` is a bank in the high byte and a 1-based entry in the low
 * byte: `LoadScript` (ps2.asm:25476) turns the bank into an index into
 * `GameScriptPtrs` and the entry into a walk down the block's delta
 * table. Here the bank is the file and the entry is the index.
 *
 * Substitutions the original resolves while copying into the text buffer
 * -- character, enemy, technique and item names, and the meseta value --
 * survive extraction as `{char}` and the like, since resolving them
 * needs party state; window.c resolves them as it types the text. */

#define SCRIPT_MAX_ENTRIES 256  /* a script id's low byte */

typedef struct {
    int    count;
    char **entry;     /* entry[0] is script 1 */
} ScriptBlock;

bool script_load(ScriptBlock *sb, const char *path);
void script_free(ScriptBlock *sb);

/* 1-based, matching the low byte of script_id. NULL if out of range. */
const char *script_entry(const ScriptBlock *sb, int n);

#endif /* PS2_SCRIPT_H */
