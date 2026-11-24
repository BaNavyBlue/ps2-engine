/* (build 187) SHA-1 (FIPS 180-4), for knowing the cartridge image by the
 * SHA-1 tools/rom_map.json gives: plain C, no library. */

#include "extract.h"

#include <string.h>

static u32 rol(u32 v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static void block(u32 h[5], const u8 *p)
{
    u32 w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16
             | (u32)p[4 * i + 2] << 8 | (u32)p[4 * i + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    u32 a = h[0];
    u32 b = h[1];
    u32 c = h[2];
    u32 d = h[3];
    u32 e = h[4];
    for (int i = 0; i < 80; i++) {
        u32 f;
        u32 k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        u32 t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void ex_sha1(const u8 *data, size_t n, u8 out[20])
{
    u32 h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u,
                 0xC3D2E1F0u };
    size_t full = n / 64 * 64;
    for (size_t i = 0; i < full; i += 64) {
        block(h, data + i);
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
    block(h, tail);
    if (len == 128) {
        block(h, tail + 64);
    }
    for (int i = 0; i < 5; i++) {
        out[4 * i]     = (u8)(h[i] >> 24);
        out[4 * i + 1] = (u8)(h[i] >> 16);
        out[4 * i + 2] = (u8)(h[i] >> 8);
        out[4 * i + 3] = (u8)h[i];
    }
}
