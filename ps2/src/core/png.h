#ifndef PS2_PNG_H
#define PS2_PNG_H

#include "ps2_types.h"

/* Indexed PNG is the asset format for everything made of pixels.
 *
 * PNG colour type 3 stores exactly what the hardware stored: a plane of
 * palette indices plus a palette. One file therefore carries both, it
 * opens in any image editor, the palette is editable in the same window
 * as the art, and the index plane loads with no unpacking and no colour
 * conversion -- `px` below is one byte per pixel, which is already what
 * `Tile` wants.
 *
 * The reader is here rather than behind libpng because the headless
 * target is supposed to build anywhere. It handles colour type 3 at bit
 * depths 1, 2, 4 and 8, non-interlaced, which is what every asset here
 * is and what editors write for indexed images.
 */

#define PNG_MAX_PALETTE 256

typedef struct {
    int width, height;
    u8 *px;                        /* width * height palette indices */
    u8  pal[PNG_MAX_PALETTE][3];   /* RGB, 8 bits per channel */
    int pal_count;
} PngImage;

/* Returns false and leaves `img` zeroed on any failure, reporting why on
 * stderr. Free with png_free. */
bool png_load_indexed(PngImage *img, const char *path);
void png_free(PngImage *img);

/* (build 189) The same from a file's bytes the caller holds, past the
 * asset store's cache and the mods (the extractors, src/extract/, which
 * read what an earlier step wrote); `path` names it in messages. */
bool png_decode_indexed(PngImage *img, const u8 *file, size_t size,
                        const char *path);

/* (build 142) A full-colour picture (docs/asset_format.md): RGBA, 8 bits a
 * channel, row major. png_load_rgba reads colour types 6 (RGBA) and 2
 * (RGB, opaque) at depth 8, and 3 (indexed, any depth, alpha from tRNS),
 * non-interlaced -- what an editor writes for a picture. Returns false and
 * leaves `img` zeroed on any failure, reporting why on stderr. */
typedef struct {
    int width, height;
    u8 *rgba;                      /* width * height * 4 */
} PngRgba;

bool png_load_rgba(PngRgba *img, const char *path);
void png_rgba_free(PngRgba *img);

/* (build 191) The reader's inflate on raw deflate (RFC 1951, no zlib
 * header): a zip's entries (src/extract/exzip.c). The bytes are malloc'd,
 * the caller frees them; NULL on a stream that is not deflate's. */
u8 *png_inflate_raw(const u8 *in, size_t len, size_t *out_len);

#endif /* PS2_PNG_H */
