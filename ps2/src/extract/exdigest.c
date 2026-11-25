/* (build 192) tools/make_assets.py's file_digest in C (extract.h): how the
 * game's files are held to tools/assets.sha256, the list of the files a
 * proved extraction made.
 *
 * A file's digest is the SHA-256 of its bytes, in hex. A PNG's is "png:"
 * and the SHA-256 of its picture as Pillow opens it -- the Python hashes
 * what Pillow gives:
 *
 *     "%s %dx%d;" % (im.mode, im.width, im.height)
 *     bytes(im.getpalette()), or b"-" when there is none
 *     repr(im.info.get("transparency"))
 *     im.tobytes()
 *
 * since two zlibs pack the same picture in other bytes (the game decodes
 * them, so only the picture matters). What Pillow makes of a PNG is done
 * here for the kinds it opens without converting them: indexed ("P") at
 * 1, 2, 4 or 8 bits, a pixel to a byte as Pillow unpacks them, the
 * palette PLTE's own entries; grey ("L"), grey and alpha ("LA"), RGB and
 * RGBA at 8 bits; a tRNS as Pillow's PNG reader reads it (an index, a
 * byte string, a grey level, an RGB triple). Every file the game's files
 * are made of is one of those (indexed at 4 and 8 bits, RGBA; the icon's
 * portrait, RGB). Anything else -- 16 bits, grey below 8, interlaced, a
 * broken file -- gets EX_DIGEST_UNREAD, which no list holds: it is
 * reported as differing, where the Python would hash what Pillow made of
 * it or stop. tools/make_assets_c_check.py holds the two together, on
 * every file in assets/ and on PNGs of every kind Pillow writes. */

#include "extract.h"

#include "exzip.h"
#include "../core/png.h"

#include <stdlib.h>
#include <string.h>

/* ---- SHA-256 (FIPS 180-4) ---- */

static const u32 K256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static u32 ror(u32 v, int n)
{
    return (v >> n) | (v << (32 - n));
}

static void sha256_block(u32 h[8], const u8 *p)
{
    u32 w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16
             | (u32)p[4 * i + 2] << 8 | (u32)p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        u32 s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = h[0];
    u32 b = h[1];
    u32 c = h[2];
    u32 d = h[3];
    u32 e = h[4];
    u32 f = h[5];
    u32 g = h[6];
    u32 k = h[7];
    for (int i = 0; i < 64; i++) {
        u32 s1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        u32 ch = (e & f) ^ (~e & g);
        u32 t1 = k + s1 + ch + K256[i] + w[i];
        u32 s0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = s0 + maj;
        k = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += k;
}

void ex_sha256(const u8 *data, size_t n, u8 out[32])
{
    u32 h[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    size_t full = n / 64 * 64;
    for (size_t i = 0; i < full; i += 64) {
        sha256_block(h, data + i);
    }
    u8 tail[128];
    size_t rest = n - full;
    memset(tail, 0, sizeof tail);
    if (rest > 0) {
        memcpy(tail, data + full, rest);
    }
    tail[rest] = 0x80;
    size_t len = (rest + 1 + 8 <= 64) ? 64 : 128;
    unsigned long long bits = (unsigned long long)n * 8;
    for (int i = 0; i < 8; i++) {
        tail[len - 1 - i] = (u8)(bits >> (8 * i));
    }
    sha256_block(h, tail);
    if (len == 128) {
        sha256_block(h, tail + 64);
    }
    for (int i = 0; i < 8; i++) {
        out[4 * i]     = (u8)(h[i] >> 24);
        out[4 * i + 1] = (u8)(h[i] >> 16);
        out[4 * i + 2] = (u8)(h[i] >> 8);
        out[4 * i + 3] = (u8)h[i];
    }
}

static char *hex_of(Extract *x, const u8 *data, size_t n)
{
    u8 sum[32];
    ex_sha256(data, n, sum);
    char *out = ex_alloc(x, 65);
    for (int i = 0; i < 32; i++) {
        static const char HEX[] = "0123456789abcdef";
        out[2 * i]     = HEX[sum[i] >> 4];
        out[2 * i + 1] = HEX[sum[i] & 15];
    }
    out[64] = '\0';
    return out;
}

/* ---- a PNG's picture, as Pillow opens it ---- */

static u32 be32(const u8 *p)
{
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | (u32)p[3];
}

static u32 be16(const u8 *p)
{
    return (u32)p[0] << 8 | (u32)p[1];
}

/* repr(bytes): b'...', Python's quote and escapes. */
static void repr_bytes(ExStr *s, const u8 *p, size_t n)
{
    bool single = memchr(p, '\'', n) != NULL;
    bool dbl = memchr(p, '"', n) != NULL;
    char quote = (single && !dbl) ? '"' : '\'';
    ex_str_addc(s, 'b');
    ex_str_addc(s, quote);
    for (size_t i = 0; i < n; i++) {
        u8 c = p[i];
        if (c == (u8)quote || c == '\\') {
            ex_str_addc(s, '\\');
            ex_str_addc(s, (char)c);
        } else if (c == '\t') {
            ex_str_add(s, "\\t");
        } else if (c == '\n') {
            ex_str_add(s, "\\n");
        } else if (c == '\r') {
            ex_str_add(s, "\\r");
        } else if (c < 0x20 || c >= 0x7F) {
            ex_str_addf(s, "\\x%02x", (unsigned)c);
        } else {
            ex_str_addc(s, (char)c);
        }
    }
    ex_str_addc(s, quote);
}

/* Pillow's _simple_palette, re.match(b"^\xff*\x00\xff*$"): one 0 among
 * 255s ("$" takes a newline at the very end too). The 0's index, or -1. */
static long simple_palette(const u8 *p, size_t n)
{
    if (n > 0 && p[n - 1] == '\n') {
        n--;
    }
    long zero = -1;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == 0 && zero < 0) {
            zero = (long)i;
        } else if (p[i] != 0xFF) {
            return -1;
        }
    }
    return zero;
}

typedef struct {
    int         ctype;
    int         depth;
    const char *mode;
    int         channels;     /* in the PNG's rows */
} PngKind;

static const PngKind KINDS[] = {
    { 3, 1, "P",    1 },
    { 3, 2, "P",    1 },
    { 3, 4, "P",    1 },
    { 3, 8, "P",    1 },
    { 0, 8, "L",    1 },
    { 4, 8, "LA",   2 },
    { 2, 8, "RGB",  3 },
    { 6, 8, "RGBA", 4 },
};

static u8 paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = abs(p - a);
    int pb = abs(p - b);
    int pc = abs(p - c);
    if (pa <= pb && pa <= pc) {
        return (u8)a;
    }
    return (u8)((pb <= pc) ? b : c);
}

/* The rows unfiltered in place (each row's filter byte left where it
 * is); false on a filter that is none of PNG's five. */
static bool unfilter(u8 *raw, size_t stride, u32 height, size_t bpp)
{
    for (u32 y = 0; y < height; y++) {
        u8 *row = raw + (size_t)y * (stride + 1);
        u8 *cur = row + 1;
        const u8 *up = (y > 0) ? row - stride : NULL;
        int filter = row[0];
        for (size_t i = 0; i < stride; i++) {
            int a = (i >= bpp) ? cur[i - bpp] : 0;
            int b = (up != NULL) ? up[i] : 0;
            int c = (up != NULL && i >= bpp) ? up[i - bpp] : 0;
            switch (filter) {
            case 0:
                break;
            case 1:
                cur[i] = (u8)(cur[i] + a);
                break;
            case 2:
                cur[i] = (u8)(cur[i] + b);
                break;
            case 3:
                cur[i] = (u8)(cur[i] + ((a + b) >> 1));
                break;
            case 4:
                cur[i] = (u8)(cur[i] + paeth(a, b, c));
                break;
            default:
                return false;
            }
        }
    }
    return true;
}

/* What Pillow hashes for the PNG `file`, or NULL when it is not a PNG
 * this reads as Pillow does. */
static char *png_picture(Extract *x, const u8 *file, size_t size)
{
    static const u8 SIGNATURE[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A,
                                     0x1A, 0x0A };
    if (size < 8 || memcmp(file, SIGNATURE, 8) != 0) {
        return NULL;
    }
    u32 width = 0;
    u32 height = 0;
    const PngKind *kind = NULL;
    const u8 *plte = NULL;
    size_t nplte = 0;
    const u8 *trns = NULL;
    size_t ntrns = 0;
    ExStr idat;
    ex_str_init(x, &idat);
    bool ended = false;
    size_t at = 8;
    while (!ended) {
        if (size - at < 12) {
            return NULL;
        }
        u32 len = be32(file + at);
        if (len > size - at - 12) {
            return NULL;
        }
        const u8 *type = file + at + 4;
        const u8 *data = file + at + 8;
        if (ex_crc32(type, (size_t)len + 4) != be32(data + len)) {
            return NULL;
        }
        bool first = (at == 8);
        if (first != (memcmp(type, "IHDR", 4) == 0)) {
            return NULL;
        }
        if (memcmp(type, "IHDR", 4) == 0) {
            if (len != 13) {
                return NULL;
            }
            width = be32(data);
            height = be32(data + 4);
            int depth = data[8];
            int ctype = data[9];
            /* compression and filtering method 0, no interlacing */
            if (data[10] != 0 || data[11] != 0 || data[12] != 0
                || width == 0 || height == 0
                || width > 0x7FFFFFFFu || height > 0x7FFFFFFFu) {
                return NULL;
            }
            for (size_t i = 0; i < sizeof KINDS / sizeof KINDS[0]; i++) {
                if (KINDS[i].ctype == ctype && KINDS[i].depth == depth) {
                    kind = &KINDS[i];
                }
            }
            if (kind == NULL) {
                return NULL;
            }
        } else if (memcmp(type, "PLTE", 4) == 0) {
            plte = data;
            nplte = len;
        } else if (memcmp(type, "tRNS", 4) == 0) {
            trns = data;
            ntrns = len;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            ex_str_addn(&idat, (const char *)data, len);
        } else if (memcmp(type, "IEND", 4) == 0) {
            ended = true;
        }
        at += (size_t)len + 12;
    }
    if (kind->ctype == 3 && (plte == NULL || nplte == 0 || nplte % 3 != 0
                             || nplte > 768)) {
        return NULL;
    }

    /* zlib: CMF and FLG (deflate, no preset dictionary), then deflate */
    if (idat.len < 2 || (idat.buf[0] & 0x0F) != 8
        || ((u32)(u8)idat.buf[0] * 256 + (u8)idat.buf[1]) % 31 != 0
        || (idat.buf[1] & 0x20) != 0) {
        return NULL;
    }
    size_t bits = (size_t)kind->channels * (size_t)kind->depth;
    unsigned long long row_bytes = ((unsigned long long)width * bits + 7) / 8;
    unsigned long long all = (unsigned long long)height * (row_bytes + 1);
    if (all > (unsigned long long)(((size_t)-1) / 4)) {
        return NULL;
    }
    size_t stride = (size_t)row_bytes;
    size_t need = (size_t)all;
    size_t got = 0;
    u8 *raw = png_inflate_raw((const u8 *)idat.buf + 2, idat.len - 2, &got);
    if (raw == NULL) {
        return NULL;
    }
    if (got < need || !unfilter(raw, stride, height,
                                (bits >= 8) ? bits / 8 : 1)) {
        free(raw);
        return NULL;
    }

    ExStr s;
    ex_str_init(x, &s);
    ex_str_addf(&s, "%s %lux%lu;", kind->mode, (unsigned long)width,
                (unsigned long)height);
    if (kind->ctype == 3) {
        ex_str_addn(&s, (const char *)plte, nplte);
    } else {
        ex_str_add(&s, "-");
    }
    if (trns == NULL) {
        ex_str_add(&s, "None");
    } else if (kind->ctype == 3) {
        long zero = simple_palette(trns, ntrns);
        if (zero >= 0) {
            ex_str_addf(&s, "%ld", zero);
        } else {
            repr_bytes(&s, trns, ntrns);
        }
    } else if (kind->ctype == 0 && ntrns >= 2) {
        ex_str_addf(&s, "%lu", (unsigned long)be16(trns));
    } else if (kind->ctype == 2 && ntrns >= 6) {
        ex_str_addf(&s, "(%lu, %lu, %lu)", (unsigned long)be16(trns),
                    (unsigned long)be16(trns + 2),
                    (unsigned long)be16(trns + 4));
    } else {
        /* a tRNS Pillow's reader would not take for this kind */
        free(raw);
        return NULL;
    }

    /* im.tobytes(): the rows, a pixel's channels to a byte each; an
     * indexed pixel below 8 bits unpacked to its own byte */
    for (u32 y = 0; y < height; y++) {
        const u8 *row = raw + (size_t)y * (stride + 1) + 1;
        if (kind->depth == 8) {
            ex_str_addn(&s, (const char *)row, stride);
            continue;
        }
        int per = 8 / kind->depth;
        int mask = (1 << kind->depth) - 1;
        for (u32 px = 0; px < width; px++) {
            int shift = 8 - kind->depth * (int)(px % (u32)per + 1);
            ex_str_addc(&s, (char)((row[px / (u32)per] >> shift) & mask));
        }
    }
    free(raw);
    return ex_fmt(x, "png:%s", hex_of(x, (const u8 *)s.buf, s.len));
}

/* Path(path).suffix.lower() == ".png": the name's last dot, not its
 * first character. */
static bool png_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    const char *dot = strrchr(name, '.');
    if (dot == NULL || dot == name || dot[1] == '\0') {
        return false;
    }
    static const char PNG[] = ".png";
    for (int i = 0; i < 5; i++) {
        char c = dot[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != PNG[i]) {
            return false;
        }
    }
    return true;
}

char *ex_file_digest(Extract *x, const char *path)
{
    size_t size = 0;
    const u8 *data = ex_read_file(x, path, &size);
    if (data == NULL) {
        return NULL;
    }
    if (!png_name(path)) {
        return hex_of(x, data, size);
    }
    char *picture = png_picture(x, data, size);
    return (picture != NULL) ? picture : ex_strdup(x, EX_DIGEST_UNREAD);
}
