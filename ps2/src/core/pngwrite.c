#include "pngwrite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (build 105) Moved from tools/route_probe.c: the deflate, the CRC and the
 * chunks are the code that wrote every route PNG up to build 104, unchanged,
 * so a route's PNG is the same file it was. */


typedef struct {
    u8    *buf;
    size_t len, cap;
    u32    bits;
    int    nbits;
} ByteOut;

static void out_byte(ByteOut *o, u8 b)
{
    if (o->len == o->cap) {
        o->cap = (o->cap == 0) ? 65536 : o->cap * 2;
        o->buf = realloc(o->buf, o->cap);
    }
    o->buf[o->len++] = b;
}

/* Deflate packs bits from the least significant end. */
static void out_bits(ByteOut *o, u32 value, int n)
{
    o->bits |= value << o->nbits;
    o->nbits += n;
    while (o->nbits >= 8) {
        out_byte(o, (u8)o->bits);
        o->bits >>= 8;
        o->nbits -= 8;
    }
}

/* ...but Huffman codes are defined most significant bit first. */
static void out_code(ByteOut *o, u32 code, int n)
{
    u32 rev = 0;
    for (int i = 0; i < n; i++) {
        rev = (rev << 1) | ((code >> i) & 1);
    }
    out_bits(o, rev, n);
}

static void out_literal(ByteOut *o, int sym)
{
    if (sym < 144) {
        out_code(o, (u32)(0x30 + sym), 8);
    } else if (sym < 256) {
        out_code(o, (u32)(0x190 + sym - 144), 9);
    } else if (sym < 280) {
        out_code(o, (u32)(sym - 256), 7);
    } else {
        out_code(o, (u32)(0xC0 + sym - 280), 8);
    }
}

static const u16 LEN_BASE[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const u8 LEN_EXTRA[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const u16 DIST_BASE[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289,
    16385, 24577
};
static const u8 DIST_EXTRA[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static void out_match(ByteOut *o, int len, int dist)
{
    int l = 28;
    while (LEN_BASE[l] > len) {
        l--;
    }
    out_literal(o, 257 + l);
    out_bits(o, (u32)(len - LEN_BASE[l]), LEN_EXTRA[l]);

    int d = 29;
    while (DIST_BASE[d] > dist) {
        d--;
    }
    out_code(o, (u32)d, 5);
    out_bits(o, (u32)(dist - DIST_BASE[d]), DIST_EXTRA[d]);
}

#define HASH_BITS 15
#define WINDOW    32768
#define MATCH_MAX 258

/* A zlib stream: one fixed-Huffman block, greedy matching against the
 * last position each three-byte prefix was seen at. A 320x224 frame is
 * mostly runs, so this gets most of what a real deflate would. */
static void zlib_compress(ByteOut *o, const u8 *data, size_t n)
{
    static s32 head[1 << HASH_BITS];
    for (int i = 0; i < (1 << HASH_BITS); i++) {
        head[i] = -1;
    }

    out_byte(o, 0x78);
    out_byte(o, 0x01);
    out_bits(o, 1, 1);          /* BFINAL */
    out_bits(o, 1, 2);          /* BTYPE 01, fixed codes */

    size_t i = 0;
    while (i < n) {
        int best_len = 0, best_dist = 0;

        if (i + 3 <= n) {
            u32 h = ((u32)data[i] << 10 ^ (u32)data[i + 1] << 5
                     ^ data[i + 2]) & ((1 << HASH_BITS) - 1);
            s32 cand = head[h];
            head[h] = (s32)i;
            if (cand >= 0 && i - (size_t)cand <= WINDOW) {
                size_t max = n - i;
                if (max > MATCH_MAX) {
                    max = MATCH_MAX;
                }
                size_t len = 0;
                while (len < max && data[(size_t)cand + len] == data[i + len]) {
                    len++;
                }
                if (len >= 3) {
                    best_len  = (int)len;
                    best_dist = (int)(i - (size_t)cand);
                }
            }
        }

        if (best_len >= 3) {
            out_match(o, best_len, best_dist);
            for (size_t k = 1; k < (size_t)best_len && i + k + 3 <= n; k++) {
                u32 h = ((u32)data[i + k] << 10 ^ (u32)data[i + k + 1] << 5
                         ^ data[i + k + 2]) & ((1 << HASH_BITS) - 1);
                head[h] = (s32)(i + k);
            }
            i += (size_t)best_len;
        } else {
            out_literal(o, data[i]);
            i++;
        }
    }
    out_literal(o, 256);
    if (o->nbits > 0) {
        out_bits(o, 0, 8 - o->nbits);
    }

    u32 a = 1, b = 0;
    for (size_t k = 0; k < n; k++) {
        a = (a + data[k]) % 65521;
        b = (b + a) % 65521;
    }
    u32 adler = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) {
        out_byte(o, (u8)(adler >> s));
    }
}

static u32 crc32_of(const u8 *p, size_t n, u32 crc)
{
    static u32 table[256];
    static bool ready;
    if (!ready) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

static void put_be32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static void write_chunk(FILE *fh, const char *type, const u8 *data, size_t n)
{
    u8 head[8];
    put_be32(head, (u32)n);
    memcpy(head + 4, type, 4);
    fwrite(head, 1, 8, fh);
    if (n > 0) {
        fwrite(data, 1, n, fh);
    }
    u32 crc = crc32_of((const u8 *)type, 4, 0);
    crc = crc32_of(data, n, crc);
    u8 tail[4];
    put_be32(tail, crc);
    fwrite(tail, 1, 4, fh);
}

bool png_write(const char *path, int w, int h, int bit_depth,
               int colour_type, const u8 *pixels, size_t row_bytes,
               const u8 *plte, int plte_count, const PngCicp *cicp)
{
    FILE *fh = fopen(path, "wb");
    if (fh == NULL) {
        return false;
    }

    static const u8 SIG[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    fwrite(SIG, 1, 8, fh);

    u8 ihdr[13];
    put_be32(ihdr, (u32)w);
    put_be32(ihdr + 4, (u32)h);
    ihdr[8]  = (u8)bit_depth;
    ihdr[9]  = (u8)colour_type;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    write_chunk(fh, "IHDR", ihdr, sizeof ihdr);

    if (cicp != NULL) {
        u8 c[4] = { cicp->primaries, cicp->transfer, cicp->matrix,
                    cicp->full_range };
        write_chunk(fh, "cICP", c, sizeof c);
    }
    if (plte != NULL && plte_count > 0) {
        write_chunk(fh, "PLTE", plte, (size_t)plte_count * 3);
    }

    size_t stride = row_bytes + 1;
    u8    *raw    = malloc(stride * (size_t)h);
    if (raw == NULL) {
        fclose(fh);
        return false;
    }
    for (int y = 0; y < h; y++) {
        raw[(size_t)y * stride] = 0;        /* filter: none */
        memcpy(&raw[(size_t)y * stride + 1], &pixels[(size_t)y * row_bytes],
               row_bytes);
    }
    ByteOut z;
    memset(&z, 0, sizeof z);
    zlib_compress(&z, raw, stride * (size_t)h);
    free(raw);
    write_chunk(fh, "IDAT", z.buf, z.len);
    free(z.buf);

    write_chunk(fh, "IEND", NULL, 0);
    return fclose(fh) == 0;
}
