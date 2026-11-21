#ifndef PS2_CREDITS_H
#define PS2_CREDITS_H

#include "ps2_types.h"

/* (build 136e) The ending's credits (EndingCredits_Script) as text,
 * assets/script/ending_credits.txt (tools/extract_tables.py writes it).
 * credits_load turns it back into the byte stream the credits writer
 * (loc_787E; ending.c's credits_write_row) reads a row at a time:
 *
 *   a line's leading spaces   blank runs: a byte n for n + 1 cells (n
 *                             under CREDITS_BLANK_RUN_BELOW), 20 cells a
 *                             byte at most
 *   any other space           CREDITS_SPACE, one blank cell
 *   a capital A-Z             its ASCII byte: drawn in palette line 0
 *   a lower case a-z          its ASCII byte, bit 5 set: the same capital
 *                             in palette line 1 (the alphabet has
 *                             capitals only)
 *   the line's end            CREDITS_NEXT_LINE: two blank rows follow
 *   {close} at its end        CREDITS_CLOSE_LINE: one blank row
 *   {section} at its end      CREDITS_END_SECTION: 28 blank rows
 *   {empty}, alone            CREDITS_NEXT_LINE with nothing before it
 *   {end}, alone              CREDITS_END: the credits stop on it
 *
 * Lines starting # and empty lines are skipped. Refused, naming the line:
 * a character that is not a letter or a space; a tag it does not know or
 * one out of its place; a line of more than CREDITS_LINE_CELLS cells (the
 * writer counts 40 a row, and a 40th runs its count out, past the row);
 * a line after {end}, which the writer never reaches; no {end}; and more
 * than CREDITS_MAX bytes. */

#define CREDITS_SPACE           0x20
#define CREDITS_NEXT_LINE       0xC1
#define CREDITS_CLOSE_LINE      0xC3    /* one blank row after, not two */
#define CREDITS_END_SECTION     0xC4    /* 28 blank rows after */
#define CREDITS_END             0xC5    /* likewise; the credits stop on it */
#define CREDITS_BLANK_RUN_BELOW 0x14    /* under it: that many cells + 1 */
#define CREDITS_LOWERCASE       0x20    /* bit 5: palette line 1 */
#define CREDITS_LINE_CELLS      39
#define CREDITS_MAX             4096    /* the ROM's are 441 bytes */

typedef struct {
    int count;
    u8  byte[CREDITS_MAX];
} CreditsScript;

bool credits_load(CreditsScript *c, const char *path);

#endif /* PS2_CREDITS_H */
