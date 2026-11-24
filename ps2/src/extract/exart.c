/* (build 188) tools/ps2art.py and tools/nemesis.py in C -- the two
 * compressions every level, sprite and picture comes in -- and the PNG
 * shapes of tools/ps2png.py, written with src/core/pngwrite.c (exart.h). */

#include "exart.h"
#include "excommon.h"
#include "../core/pngwrite.h"

#include <string.h>

/* ---- DecompressArt's format (ps2art.py) ---- */

/* A tile at a time: a count byte ($80 and up ends the stream; 0 is a tile
 * of literals), that many groups of a value and a 32-bit mask of where it
 * goes, then a literal byte for every place no mask covered. */
bool ex_art_try(Extract *x, const u8 *data, size_t len, ExTiles *out,
                const char **why)
{
    ExStr tiles;
    ex_str_init(x, &tiles);
    size_t pos = 0;
    int count = 0;
    for (;;) {
        if (pos >= len) {
            *why = ex_fmt(x, "ran off the end at %#zx", pos);
            return false;
        }
        u8 groups = data[pos++];
        if (groups >= 0x80) {
            break;
        }
        u8  tile[EX_TILE_BYTES];
        u32 covered = 0;
        memset(tile, 0, sizeof tile);
        for (int g = 0; g < groups; g++) {
            if (pos + 5 > len) {
                *why = ex_fmt(x, "truncated run group at %#zx", pos);
                return false;
            }
            u8  value = data[pos];
            u32 mask  = (u32)data[pos + 1] << 24 | (u32)data[pos + 2] << 16
                      | (u32)data[pos + 3] << 8 | data[pos + 4];
            pos += 5;
            covered |= mask;
            for (int i = 0; i < EX_TILE_BYTES; i++) {
                if (mask & (1u << (31 - i))) {
                    tile[i] = value;
                }
            }
        }
        for (int i = 0; i < EX_TILE_BYTES; i++) {
            if (!(covered & (1u << (31 - i)))) {
                if (pos >= len) {
                    *why = ex_fmt(x, "truncated literals at %#zx", pos);
                    return false;
                }
                tile[i] = data[pos++];
            }
        }
        ex_str_addn(&tiles, (const char *)tile, EX_TILE_BYTES);
        count++;
    }
    out->data  = (tiles.buf != NULL) ? (u8 *)tiles.buf : ex_alloc(x, 1);
    out->count = count;
    out->used  = pos;
    return true;
}

ExTiles ex_art(Extract *x, const u8 *data, size_t len)
{
    ExTiles t;
    const char *why = NULL;
    if (!ex_art_try(x, data, len, &t, &why)) {
        ex_fail(x, "art: %s", why);
    }
    return t;
}

/* ---- Nemesis (nemesis.py) ---- */

typedef struct {
    Extract    *x;
    const u8   *data;
    size_t      len;
    size_t      pos;
    u32         window;
    int         bits;
    const char *why;
} Bits;

static bool refill(Bits *b)
{
    if (b->bits < 9) {
        if (b->pos >= b->len) {
            b->why = "bitstream truncated";
            return false;
        }
        b->bits += 8;
        b->window = ((b->window << 8) | b->data[b->pos]) & 0xFFFF;
        b->pos++;
    }
    return true;
}

bool ex_nemesis_try(Extract *x, const u8 *data, size_t len, ExTiles *out,
                    const char **why)
{
    if (len < 2) {
        *why = "no header";
        return false;
    }
    size_t pos = 0;
    u32 header = (u32)data[0] << 8 | data[1];
    pos += 2;
    bool xor_mode = (header & 0x8000) != 0;
    long longs_left = (long)(header & 0x7FFF) * 8;

    /* the code table: a flat 256 entries, high byte the code's length,
     * low byte (repeat << 4) | index */
    u16 table[256];
    memset(table, 0, sizeof table);
    if (pos >= len) {
        *why = "code table runs off the end";
        return false;
    }
    u8 d0 = data[pos++];
    while (d0 != 0xFF) {
        u8 group = d0;
        for (;;) {
            if (pos >= len) {
                *why = "unterminated code table";
                return false;
            }
            d0 = data[pos++];
            if (d0 >= 0x80) {
                break;
            }
            u16 entry  = (u16)((group & 0x0F) | (d0 & 0x70) | ((d0 & 0x0F) << 8));
            int length = d0 & 0x0F;
            if (pos >= len) {
                *why = "code table runs off the end";
                return false;
            }
            u8 code = data[pos++];
            int shift = 8 - length;
            if (shift < 0) {
                *why = "a code longer than 8 bits";
                return false;
            }
            if (shift == 0) {
                table[code] = entry;
            } else {
                long base = (long)code << shift;
                if (base + (1L << shift) > 256) {
                    *why = "a code past the table";
                    return false;
                }
                for (long k = 0; k < (1L << shift); k++) {
                    table[base + k] = entry;
                }
            }
        }
    }

    if (pos + 2 > len) {
        *why = "bitstream truncated";
        return false;
    }
    Bits b = { x, data, len, pos + 2, (u32)data[pos] << 8 | data[pos + 1], 16,
               NULL };

    ExStr outb;
    ex_str_init(x, &outb);
    u32 acc     = 0;
    int nibbles = 8;
    u32 running = 0;
    while (longs_left > 0) {
        u32 prefix = (b.window >> (b.bits - 8)) & 0xFF;
        u32 index;
        u32 repeat;
        if (prefix >= 0xFC) {
            b.bits -= 6;
            if (!refill(&b)) {
                *why = b.why;
                return false;
            }
            b.bits -= 7;
            u32 symbol = b.window >> b.bits;
            index  = symbol & 0x0F;
            repeat = (symbol & 0x70) >> 4;
            if (!refill(&b)) {
                *why = b.why;
                return false;
            }
        } else {
            u16 entry = table[prefix];
            int length = (entry >> 8) & 0xFF;
            if (length == 0) {
                *why = ex_fmt(x, "no code for prefix %#04x", (unsigned)prefix);
                return false;
            }
            b.bits -= length;
            if (!refill(&b)) {
                *why = b.why;
                return false;
            }
            index  = entry & 0x0F;
            repeat = (entry & 0xF0) >> 4;
        }
        for (u32 r = 0; r < repeat + 1; r++) {
            acc = (acc << 4) | index;
            nibbles--;
            if (nibbles != 0) {
                continue;
            }
            u32 v = acc;
            if (xor_mode) {
                running ^= acc;
                v = running;
            }
            char be[4] = { (char)(v >> 24), (char)(v >> 16), (char)(v >> 8),
                           (char)v };
            ex_str_addn(&outb, be, 4);
            acc     = 0;
            nibbles = 8;
            longs_left--;
            if (longs_left == 0) {
                break;
            }
        }
    }
    out->data  = (outb.buf != NULL) ? (u8 *)outb.buf : ex_alloc(x, 1);
    out->count = (int)((outb.len + EX_TILE_BYTES - 1) / EX_TILE_BYTES);
    out->used  = b.pos;
    return true;
}

ExTiles ex_nemesis(Extract *x, const u8 *data, size_t len)
{
    ExTiles t;
    const char *why = NULL;
    if (!ex_nemesis_try(x, data, len, &t, &why)) {
        ex_fail(x, "nemesis: %s", why);
    }
    return t;
}

/* ---- the PNG shapes (ps2png.py) ---- */

void ex_rgb_add(ExRgb *p, u8 r, u8 g, u8 b)
{
    if (p->count >= 256) {
        return;
    }
    p->rgb[3 * p->count]     = r;
    p->rgb[3 * p->count + 1] = g;
    p->rgb[3 * p->count + 2] = b;
    p->count++;
}

void ex_rgb_cram(ExRgb *p, u16 word)
{
    u8 c[3];
    ex_cram_rgb(word, c);
    ex_rgb_add(p, c[0], c[1], c[2]);
}

void ex_rgb_grey_ramp(ExRgb *p)
{
    for (int i = 0; i < 16; i++) {
        u8 v = (u8)(i * 255 / 15);
        ex_rgb_add(p, v, v, v);
    }
}

static void write_indexed(Extract *x, const char *path, int w, int h,
                          const u8 *px, const ExRgb *rgb)
{
    u8 plte[256 * 3];
    memset(plte, 0, sizeof plte);
    memcpy(plte, rgb->rgb, (size_t)rgb->count * 3);
    if (!png_write(path, w, h, 8, 3, px, (size_t)w, plte, 256, NULL)) {
        ex_fail(x, "%s: cannot write it", path);
    }
}

void ex_png_tiles(Extract *x, const char *path, const u8 *tiles, int count,
                  const ExRgb *rgb, int across, bool packed, int *across_out,
                  int *rows_out)
{
    int cap = (count > 0) ? count : 1;
    if (across > cap) {
        across = cap;
    }
    if (across < 1) {
        across = 1;
    }
    int rows = (count + across - 1) / across;
    int w = across * 8;
    int h = rows * 8;
    u8 *px = ex_alloc(x, (size_t)w * (size_t)(h > 0 ? h : 1));
    for (int t = 0; t < count; t++) {
        int ox = (t % across) * 8;
        int oy = (t / across) * 8;
        for (int i = 0; i < 64; i++) {
            u8 v;
            if (packed) {
                u8 b = tiles[(size_t)t * 32 + (size_t)(i / 2)];
                v = (i % 2 == 0) ? (u8)(b >> 4) : (u8)(b & 0x0F);
            } else {
                v = tiles[(size_t)t * 64 + (size_t)i];
            }
            px[(size_t)(oy + i / 8) * (size_t)w + (size_t)(ox + i % 8)] = v;
        }
    }
    write_indexed(x, path, w, h, px, rgb);
    if (across_out != NULL) {
        *across_out = across;
    }
    if (rows_out != NULL) {
        *rows_out = rows;
    }
}

void ex_png_flat(Extract *x, const char *path, int w, int h, const u8 *px,
                 const ExRgb *rgb)
{
    write_indexed(x, path, w, h, px, rgb);
}

void ex_png_ramp(Extract *x, const char *path, const u16 *words, int n,
                 int across)
{
    if (n == 0 || n % across != 0) {
        ex_fail(x, "%s: %d words do not fill rows of %d", path, n, across);
    }
    ExRgb rgb;
    memset(&rgb, 0, sizeof rgb);
    for (int i = 0; i < n; i++) {
        ex_rgb_cram(&rgb, words[i]);
    }
    u8 *px = ex_alloc(x, (size_t)n);
    for (int i = 0; i < n; i++) {
        px[i] = (u8)i;
    }
    write_indexed(x, path, across, n / across, px, &rgb);
}
