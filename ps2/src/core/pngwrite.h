#ifndef PS2_PNGWRITE_H
#define PS2_PNGWRITE_H

#include "ps2_types.h"

#include <stddef.h>

/* (build 105) PNG out: the route probe's indexed frames (moved here from
 * tools/route_probe.c unchanged, so its PNGs are byte for byte what they
 * were) and tools/crt_ref.c's RGB crops, 8-bit sRGB or 16-bit PQ. One
 * zlib stream of fixed-Huffman deflate with greedy matching, no
 * dependencies, like png.c's reader. */

/* A cICP chunk (PNG, third edition): H.273's colour primaries, transfer,
 * matrix and full-range flag. 9, 16, 0, 1 is Rec.2020 with PQ, full range
 * -- an HDR10 still a browser shows as HDR. */
typedef struct {
    u8 primaries, transfer, matrix, full_range;
} PngCicp;

/* `pixels` is h rows of `row_bytes` (16-bit samples big-endian, as PNG
 * stores them); colour type 3 needs `plte` (plte_count RGB triples).
 * `cicp` may be NULL. False when the file cannot be written. */
bool png_write(const char *path, int w, int h, int bit_depth,
               int colour_type, const u8 *pixels, size_t row_bytes,
               const u8 *plte, int plte_count, const PngCicp *cicp);

#endif /* PS2_PNGWRITE_H */
