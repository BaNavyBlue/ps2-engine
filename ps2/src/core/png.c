#include "png.h"

#include "assetfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- inflate (RFC 1951) ----
 *
 * Enough of it to read what image editors write: stored blocks, fixed
 * Huffman and dynamic Huffman. No compressor is needed anywhere, since
 * the extractors write PNGs from Python.
 */

typedef struct {
    const u8 *in;
    size_t    in_len, in_pos;
    u32       bitbuf;
    int       bitcnt;

    u8    *out;
    size_t out_len, out_cap;
    bool   error;
} Inflate;

/* Canonical Huffman: counts per length, then symbols in code order. */
typedef struct {
    u16 count[16];
    u16 symbol[288];
} Huff;

static int inf_bit(Inflate *z)
{
    if (z->bitcnt == 0) {
        if (z->in_pos >= z->in_len) {
            z->error = true;
            return 0;
        }
        z->bitbuf = z->in[z->in_pos++];
        z->bitcnt = 8;
    }
    int b = z->bitbuf & 1;
    z->bitbuf >>= 1;
    z->bitcnt--;
    return b;
}

static int inf_bits(Inflate *z, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        v |= inf_bit(z) << i;
    }
    return v;
}

static void huff_build(Huff *h, const u8 *lengths, int n)
{
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;

    u16 offs[16];
    offs[0] = 0;
    offs[1] = 0;
    for (int len = 1; len < 15; len++) {
        offs[len + 1] = (u16)(offs[len] + h->count[len]);
    }
    for (int i = 0; i < n; i++) {
        if (lengths[i] != 0) {
            h->symbol[offs[lengths[i]]++] = (u16)i;
        }
    }
}

static int huff_decode(Inflate *z, const Huff *h)
{
    int code = 0, first = 0, index = 0;

    for (int len = 1; len < 16; len++) {
        code |= inf_bit(z);
        int count = h->count[len];
        if (code - first < count) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
        if (z->error) {
            break;
        }
    }
    z->error = true;
    return -1;
}

static void out_byte(Inflate *z, u8 v)
{
    if (z->out_len >= z->out_cap) {
        size_t cap = z->out_cap ? z->out_cap * 2 : 65536;
        u8 *grown = realloc(z->out, cap);
        if (grown == NULL) {
            z->error = true;
            return;
        }
        z->out = grown;
        z->out_cap = cap;
    }
    z->out[z->out_len++] = v;
}

static const u16 LEN_BASE[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51,
    59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const u8 LEN_EXTRA[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
    4, 5, 5, 5, 5, 0
};
static const u16 DIST_BASE[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
    513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const u8 DIST_EXTRA[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10,
    10, 11, 11, 12, 12, 13, 13
};

static void inflate_block(Inflate *z, const Huff *lit, const Huff *dist)
{
    for (;;) {
        int sym = huff_decode(z, lit);
        if (z->error) {
            return;
        }
        if (sym < 256) {
            out_byte(z, (u8)sym);
            continue;
        }
        if (sym == 256) {
            return;
        }

        sym -= 257;
        if (sym >= 29) {
            z->error = true;
            return;
        }
        int len = LEN_BASE[sym] + inf_bits(z, LEN_EXTRA[sym]);

        int dsym = huff_decode(z, dist);
        if (z->error || dsym < 0 || dsym >= 30) {
            z->error = true;
            return;
        }
        size_t d = (size_t)DIST_BASE[dsym] + (size_t)inf_bits(z,
                                                    DIST_EXTRA[dsym]);
        if (d > z->out_len) {
            z->error = true;
            return;
        }
        size_t from = z->out_len - d;
        for (int i = 0; i < len; i++) {
            out_byte(z, z->out[from + (size_t)i]);
            if (z->error) {
                return;
            }
        }
    }
}

static void build_fixed(Huff *lit, Huff *dist)
{
    u8 lengths[288];
    int i = 0;
    for (; i < 144; i++) {
        lengths[i] = 8;
    }
    for (; i < 256; i++) {
        lengths[i] = 9;
    }
    for (; i < 280; i++) {
        lengths[i] = 7;
    }
    for (; i < 288; i++) {
        lengths[i] = 8;
    }
    huff_build(lit, lengths, 288);

    u8 dlen[30];
    for (i = 0; i < 30; i++) {
        dlen[i] = 5;
    }
    huff_build(dist, dlen, 30);
}

static void build_dynamic(Inflate *z, Huff *lit, Huff *dist)
{
    static const u8 ORDER[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    int hlit  = inf_bits(z, 5) + 257;
    int hdist = inf_bits(z, 5) + 1;
    int hclen = inf_bits(z, 4) + 4;
    if (hlit > 286 || hdist > 30) {
        z->error = true;
        return;
    }

    u8 clen[19];
    memset(clen, 0, sizeof clen);
    for (int i = 0; i < hclen; i++) {
        clen[ORDER[i]] = (u8)inf_bits(z, 3);
    }

    Huff code;
    huff_build(&code, clen, 19);

    u8 lengths[318];
    memset(lengths, 0, sizeof lengths);
    int n = 0;
    while (n < hlit + hdist) {
        int sym = huff_decode(z, &code);
        if (z->error || sym < 0) {
            z->error = true;
            return;
        }
        if (sym < 16) {
            lengths[n++] = (u8)sym;
        } else if (sym == 16) {
            if (n == 0) {
                z->error = true;
                return;
            }
            u8 prev = lengths[n - 1];
            int rep = 3 + inf_bits(z, 2);
            while (rep-- && n < hlit + hdist) {
                lengths[n++] = prev;
            }
        } else {
            int rep = (sym == 17) ? (3 + inf_bits(z, 3))
                                  : (11 + inf_bits(z, 7));
            while (rep-- && n < hlit + hdist) {
                lengths[n++] = 0;
            }
        }
    }

    huff_build(lit, lengths, hlit);
    huff_build(dist, lengths + hlit, hdist);
}

/* zlib stream (RFC 1950): two header bytes, deflate data, adler32. */
static u8 *zlib_inflate(const u8 *in, size_t len, size_t *out_len)
{
    if (len < 2) {
        return NULL;
    }

    Inflate z;
    memset(&z, 0, sizeof z);
    z.in = in;
    z.in_len = len;
    z.in_pos = 2;          /* CMF, FLG */

    for (;;) {
        int last = inf_bit(&z);
        int type = inf_bits(&z, 2);

        if (type == 0) {
            z.bitcnt = 0;
            if (z.in_pos + 4 > z.in_len) {
                z.error = true;
            } else {
                int n = z.in[z.in_pos] | (z.in[z.in_pos + 1] << 8);
                z.in_pos += 4;
                for (int i = 0; i < n && !z.error; i++) {
                    if (z.in_pos >= z.in_len) {
                        z.error = true;
                        break;
                    }
                    out_byte(&z, z.in[z.in_pos++]);
                }
            }
        } else if (type == 1 || type == 2) {
            Huff lit, dist;
            if (type == 1) {
                build_fixed(&lit, &dist);
            } else {
                build_dynamic(&z, &lit, &dist);
            }
            if (!z.error) {
                inflate_block(&z, &lit, &dist);
            }
        } else {
            z.error = true;
        }

        if (z.error) {
            free(z.out);
            return NULL;
        }
        if (last) {
            break;
        }
    }

    *out_len = z.out_len;
    return z.out;
}

/* ---- PNG ---- */

static u32 be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u8 paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) {
        return (u8)a;
    }
    return (pb <= pc) ? (u8)b : (u8)c;
}

static bool unfilter(u8 *raw, size_t raw_len, int width, int height,
                     int depth, u8 *out)
{
    /* Indexed PNG has one channel, so the "previous pixel" distance is
     * one byte at depth 8 and zero below it (filters operate on bytes). */
    int bpp = (depth == 8) ? 1 : 1;
    size_t stride = ((size_t)width * (size_t)depth + 7) / 8;

    if (raw_len < (stride + 1) * (size_t)height) {
        return false;
    }

    u8 *prev = calloc(stride, 1);
    u8 *line = malloc(stride);
    if (prev == NULL || line == NULL) {
        free(prev);
        free(line);
        return false;
    }

    for (int y = 0; y < height; y++) {
        const u8 *src = raw + (size_t)y * (stride + 1);
        int filter = *src++;
        memcpy(line, src, stride);

        for (size_t i = 0; i < stride; i++) {
            int a = (i >= (size_t)bpp) ? line[i - bpp] : 0;
            int b = prev[i];
            int c = (i >= (size_t)bpp) ? prev[i - bpp] : 0;

            switch (filter) {
                case 0:
                    break;
                case 1:
                    line[i] = (u8)(line[i] + a);
                    break;
                case 2:
                    line[i] = (u8)(line[i] + b);
                    break;
                case 3:
                    line[i] = (u8)(line[i] + ((a + b) >> 1));
                    break;
                case 4:
                    line[i] = (u8)(line[i] + paeth(a, b, c));
                    break;
                default:
                    free(prev);
                    free(line);
                    return false;
            }
        }

        /* Expand sub-byte depths to one index per byte. */
        u8 *dst = out + (size_t)y * (size_t)width;
        if (depth == 8) {
            memcpy(dst, line, (size_t)width);
        } else {
            int per_byte = 8 / depth;
            int mask = (1 << depth) - 1;
            for (int x = 0; x < width; x++) {
                int byte = line[x / per_byte];
                int shift = 8 - depth * (x % per_byte + 1);
                dst[x] = (u8)((byte >> shift) & mask);
            }
        }

        memcpy(prev, line, stride);
    }

    free(prev);
    free(line);
    return true;
}

bool png_decode_indexed(PngImage *img, const u8 *file, size_t size,
                        const char *path)
{
    memset(img, 0, sizeof *img);

    if (file == NULL || size < 8) {
        fprintf(stderr, "png: cannot read %s\n", path);
        return false;
    }

    static const u8 SIG[8] = { 137, 'P', 'N', 'G', '\r', '\n', 26, '\n' };
    if (memcmp(file, SIG, 8) != 0) {
        fprintf(stderr, "png: %s is not a PNG\n", path);
        return false;
    }

    int depth = 0;
    u8 *idat = NULL;
    size_t idat_len = 0;
    size_t pos = 8;
    bool ok = true;

    while (pos + 8 <= size) {
        u32 len = be32(file + pos);
        const char *type = (const char *)file + pos + 4;
        const u8 *data = file + pos + 8;
        if (pos + 12 + len > size) {
            ok = false;
            break;
        }

        if (memcmp(type, "IHDR", 4) == 0 && len >= 13) {
            img->width  = (int)be32(data);
            img->height = (int)be32(data + 4);
            depth = data[8];
            if (data[9] != 3) {
                fprintf(stderr, "png: %s is colour type %d, not indexed\n",
                        path, data[9]);
                ok = false;
                break;
            }
            if (data[12] != 0) {
                fprintf(stderr, "png: %s is interlaced\n", path);
                ok = false;
                break;
            }
        } else if (memcmp(type, "PLTE", 4) == 0) {
            img->pal_count = (int)(len / 3);
            if (img->pal_count > PNG_MAX_PALETTE) {
                img->pal_count = PNG_MAX_PALETTE;
            }
            for (int i = 0; i < img->pal_count; i++) {
                img->pal[i][0] = data[i * 3 + 0];
                img->pal[i][1] = data[i * 3 + 1];
                img->pal[i][2] = data[i * 3 + 2];
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            u8 *grown = realloc(idat, idat_len + len);
            if (grown == NULL) {
                ok = false;
                break;
            }
            idat = grown;
            memcpy(idat + idat_len, data, len);
            idat_len += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }

        pos += 12 + len;
    }

    if (ok && (img->width <= 0 || img->height <= 0 || idat == NULL
               || (depth != 1 && depth != 2 && depth != 4 && depth != 8))) {
        fprintf(stderr, "png: %s has no usable image data\n", path);
        ok = false;
    }

    size_t raw_len = 0;
    u8 *raw = ok ? zlib_inflate(idat, idat_len, &raw_len) : NULL;
    if (ok && raw == NULL) {
        fprintf(stderr, "png: %s failed to decompress\n", path);
        ok = false;
    }

    if (ok) {
        img->px = malloc((size_t)img->width * (size_t)img->height);
        ok = img->px != NULL
          && unfilter(raw, raw_len, img->width, img->height, depth, img->px);
        if (!ok) {
            fprintf(stderr, "png: %s failed to unfilter\n", path);
        }
    }

    free(raw);
    free(idat);

    if (!ok) {
        png_free(img);
        return false;
    }
    return true;
}

static bool png_read_indexed(PngImage *img, const char *path)
{
    /* The bytes come from the RAM file store, which owns them. */
    size_t size = 0;
    const u8 *file = assetfs_get(path, &size);
    return png_decode_indexed(img, file, size, path);
}

bool png_load_indexed(PngImage *img, const char *path)
{
    /* (build 157d) A mod it refuses is set aside and the game's own
     * read in its place (docs/mods.md, "When a mod is refused"). */
    return png_read_indexed(img, path)
        || (assetfs_mod_refused(path) && png_read_indexed(img, path));
}

void png_free(PngImage *img)
{
    free(img->px);
    memset(img, 0, sizeof *img);
}

/* (build 142) Undo the row filters with `bpp` bytes a pixel (at least one)
 * and `stride` bytes a row; `out` gets the rows, stride bytes each. */
static bool unfilter_rows(const u8 *raw, size_t raw_len, size_t stride,
                          int height, int bpp, u8 *out)
{
    if (raw_len < (stride + 1) * (size_t)height) {
        return false;
    }
    for (int y = 0; y < height; y++) {
        const u8 *src  = raw + (size_t)y * (stride + 1);
        int       filt = *src++;
        u8       *line = out + (size_t)y * stride;
        const u8 *prev = (y > 0) ? out + (size_t)(y - 1) * stride : NULL;
        for (size_t i = 0; i < stride; i++) {
            int a = (i >= (size_t)bpp) ? line[i - bpp] : 0;
            int b = (prev != NULL) ? prev[i] : 0;
            int c = (prev != NULL && i >= (size_t)bpp) ? prev[i - bpp] : 0;
            int v = src[i];
            switch (filt) {
                case 0: {
                    break;
                }
                case 1: {
                    v += a;
                    break;
                }
                case 2: {
                    v += b;
                    break;
                }
                case 3: {
                    v += (a + b) >> 1;
                    break;
                }
                case 4: {
                    v += paeth(a, b, c);
                    break;
                }
                default: {
                    return false;
                }
            }
            line[i] = (u8)v;
        }
    }
    return true;
}

static bool png_read_rgba(PngRgba *img, const char *path)
{
    memset(img, 0, sizeof *img);

    size_t size = 0;
    const u8 *file = assetfs_get(path, &size);
    if (file == NULL || size < 8) {
        fprintf(stderr, "png: cannot read %s\n", path);
        return false;
    }
    static const u8 SIG[8] = { 137, 'P', 'N', 'G', '\r', '\n', 26, '\n' };
    if (memcmp(file, SIG, 8) != 0) {
        fprintf(stderr, "png: %s is not a PNG\n", path);
        return false;
    }

    int    depth = 0, ctype = -1, interlace = 0;
    u8     pal[PNG_MAX_PALETTE][4];
    int    pal_count = 0;
    u8    *idat = NULL;
    size_t idat_len = 0;
    size_t pos = 8;
    bool   ok = true;

    for (int i = 0; i < PNG_MAX_PALETTE; i++) {
        pal[i][0] = pal[i][1] = pal[i][2] = 0;
        pal[i][3] = 255;
    }
    while (pos + 8 <= size) {
        u32 len = be32(file + pos);
        const char *type = (const char *)file + pos + 4;
        const u8 *data = file + pos + 8;
        if (pos + 12 + len > size) {
            ok = false;
            break;
        }
        if (memcmp(type, "IHDR", 4) == 0 && len >= 13) {
            img->width  = (int)be32(data);
            img->height = (int)be32(data + 4);
            depth       = data[8];
            ctype       = data[9];
            interlace   = data[12];
        } else if (memcmp(type, "PLTE", 4) == 0) {
            pal_count = (int)(len / 3);
            if (pal_count > PNG_MAX_PALETTE) {
                pal_count = PNG_MAX_PALETTE;
            }
            for (int i = 0; i < pal_count; i++) {
                pal[i][0] = data[i * 3 + 0];
                pal[i][1] = data[i * 3 + 1];
                pal[i][2] = data[i * 3 + 2];
            }
        } else if (memcmp(type, "tRNS", 4) == 0) {
            for (u32 i = 0; i < len && i < PNG_MAX_PALETTE; i++) {
                pal[i][3] = data[i];
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            u8 *grown = realloc(idat, idat_len + len);
            if (grown == NULL) {
                ok = false;
                break;
            }
            idat = grown;
            memcpy(idat + idat_len, data, len);
            idat_len += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;
    }

    bool known = (ctype == 6 && depth == 8) || (ctype == 2 && depth == 8)
              || (ctype == 3 && (depth == 1 || depth == 2 || depth == 4
                                 || depth == 8));
    if (ok && (!known || interlace != 0 || img->width <= 0
               || img->height <= 0 || idat == NULL)) {
        fprintf(stderr, "png: %s is not a picture this reads (colour type "
                        "%d, depth %d%s)\n", path, ctype, depth,
                interlace ? ", interlaced" : "");
        ok = false;
    }

    size_t raw_len = 0;
    u8 *raw = ok ? zlib_inflate(idat, idat_len, &raw_len) : NULL;
    if (ok && raw == NULL) {
        fprintf(stderr, "png: %s failed to decompress\n", path);
        ok = false;
    }

    int    channels = (ctype == 6) ? 4 : ((ctype == 2) ? 3 : 1);
    size_t stride   = ((size_t)img->width * (size_t)channels
                       * (size_t)depth + 7) / 8;
    u8    *rows     = ok ? malloc(stride * (size_t)img->height) : NULL;
    if (ok) {
        int bpp = (ctype == 3) ? 1 : channels;
        ok = rows != NULL
          && unfilter_rows(raw, raw_len, stride, img->height, bpp, rows);
        if (!ok) {
            fprintf(stderr, "png: %s failed to unfilter\n", path);
        }
    }
    if (ok) {
        img->rgba = malloc((size_t)img->width * (size_t)img->height * 4);
        ok = img->rgba != NULL;
    }
    for (int y = 0; ok && y < img->height; y++) {
        const u8 *line = rows + (size_t)y * stride;
        for (int x = 0; x < img->width; x++) {
            u8 *o = img->rgba + ((size_t)y * (size_t)img->width + x) * 4;
            if (ctype == 6) {
                memcpy(o, line + x * 4, 4);
            } else if (ctype == 2) {
                memcpy(o, line + x * 3, 3);
                o[3] = 255;
            } else {
                int per  = 8 / depth;
                int byte = line[x / per];
                int sh   = 8 - depth * (x % per + 1);
                int i    = (byte >> sh) & ((1 << depth) - 1);
                memcpy(o, pal[i], 4);
            }
        }
    }

    free(rows);
    free(raw);
    free(idat);
    if (!ok) {
        png_rgba_free(img);
        return false;
    }
    return true;
}

bool png_load_rgba(PngRgba *img, const char *path)
{
    /* (build 157d) A mod it refuses is set aside and the game's own
     * read in its place (docs/mods.md, "When a mod is refused"). */
    return png_read_rgba(img, path)
        || (assetfs_mod_refused(path) && png_read_rgba(img, path));
}

void png_rgba_free(PngRgba *img)
{
    free(img->rgba);
    memset(img, 0, sizeof *img);
}
