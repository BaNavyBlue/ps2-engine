/* (build 191) Zip files, as Python's zipfile (3.14) reads them: the end
 * record looked for where _EndRecData looks, is_zipfile's test of the
 * first directory entry, the directory in its order, an entry read from
 * its local header with the directory's sizes, inflated (png.c's
 * inflate) and checked by its CRC. A name is kept as its bytes (Python
 * reads a name without the UTF-8 flag as cp437; only ASCII endings are
 * looked at here). */

#include "exzip.h"

#include "../core/png.h"

#include <stdlib.h>
#include <string.h>

#define END_SIZE      22u        /* sizeEndCentDir */
#define DIR_SIZE      46u        /* sizeCentralDir */
#define LOCAL_SIZE    30u        /* sizeFileHeader */
#define FLAG_ENCRYPTED 0x0001u
#define FLAG_PATCHED   0x0020u
#define FLAG_STRONG    0x0040u
#define MAX_VERSION   63         /* MAX_EXTRACT_VERSION */

static const u8 END_MAGIC[4]   = { 'P', 'K', 5, 6 };
static const u8 DIR_MAGIC[4]   = { 'P', 'K', 1, 2 };
static const u8 LOCAL_MAGIC[4] = { 'P', 'K', 3, 4 };
static const u8 Z64_MAGIC[4]   = { 'P', 'K', 6, 7 };   /* zip64's locator */

static u32 crc_table[256];
static bool crc_ready;

u32 ex_crc32_more(u32 crc, const u8 *data, size_t n)
{
    if (!crc_ready) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            crc_table[i] = c;
        }
        crc_ready = true;
    }
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

u32 ex_crc32(const u8 *data, size_t n)
{
    return ex_crc32_more(0, data, n);
}

static u16 le16(const u8 *p)
{
    return (u16)(p[0] | (p[1] << 8));
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16)
         | ((u32)p[3] << 24);
}

/* The end record: its place and its fields; false when there is none
 * (_EndRecData's None). *zip64 when zip64's locator is before it. */
typedef struct {
    size_t at;
    u16    disk;
    u16    disk_start;
    u16    total;
    u32    dir_size;
    u32    dir_offset;
    size_t comment_at;
    size_t comment_len;
    bool   zip64;
} EndRecord;

static bool find_end(const u8 *data, size_t size, EndRecord *e)
{
    if (size < END_SIZE) {
        return false;
    }
    size_t at = size - END_SIZE;
    bool found = memcmp(data + at, END_MAGIC, 4) == 0
              && data[size - 2] == 0 && data[size - 1] == 0;
    if (!found) {
        /* the last 64 KB and the record's own: the last magic there */
        size_t from = (size > 65536u + END_SIZE)
                    ? size - 65536u - END_SIZE : 0;
        found = false;
        for (size_t i = size - 4 + 1; i-- > from;) {
            if (memcmp(data + i, END_MAGIC, 4) == 0) {
                at = i;
                found = true;
                break;
            }
        }
        if (!found || size - at < END_SIZE) {
            return false;
        }
    }
    const u8 *r = data + at;
    e->at          = at;
    e->disk        = le16(r + 4);
    e->disk_start  = le16(r + 6);
    e->total       = le16(r + 10);
    e->dir_size    = le32(r + 12);
    e->dir_offset  = le32(r + 16);
    e->comment_at  = at + END_SIZE;
    e->comment_len = le16(r + 20);
    if (e->comment_len > size - e->comment_at) {
        e->comment_len = size - e->comment_at;   /* the slice Python takes */
    }
    e->zip64 = at >= 20 && memcmp(data + at - 20, Z64_MAGIC, 4) == 0;
    return true;
}

bool ex_zip_open(Extract *x, const u8 *data, size_t size, ExZip *z)
{
    memset(z, 0, sizeof *z);
    EndRecord e;
    if (!find_end(data, size, &e)) {
        return false;
    }
    /* is_zipfile: an empty zip, or the first directory entry's magic
     * where the record says the directory is (anything before the zip,
     * "concat", counted) */
    long long start = (long long)e.at - (long long)e.dir_size;
    bool empty = e.total == 0 && e.dir_size == 0 && e.dir_offset == 0;
    if (!empty) {
        if (e.disk != e.disk_start || e.dir_size < DIR_SIZE || start < 0
            || (size_t)start + DIR_SIZE > size
            || memcmp(data + start, DIR_MAGIC, 4) != 0) {
            return false;
        }
    }
    if (e.zip64) {
        ex_fail(x, "a zip64 archive, which is not read here");
    }
    long long concat = start - (long long)e.dir_offset;

    z->data        = data;
    z->size        = size;
    z->comment     = ex_strndup(x, (const char *)data + e.comment_at,
                                e.comment_len);
    z->comment_len = e.comment_len;
    if (empty) {
        return true;
    }
    if ((size_t)start + e.dir_size > size) {
        ex_fail(x, "a zip whose directory runs past its end");
    }

    /* the directory, twice: counted, then read */
    for (int pass = 0; pass < 2; pass++) {
        size_t pos = (size_t)start;
        size_t end = pos + e.dir_size;
        int n = 0;
        while (pos < end) {
            if (end - pos < DIR_SIZE) {
                ex_fail(x, "a zip's directory cut short");
            }
            const u8 *d = data + pos;
            if (memcmp(d, DIR_MAGIC, 4) != 0) {
                ex_fail(x, "a zip's directory with a bad entry");
            }
            size_t name_len    = le16(d + 28);
            size_t extra_len   = le16(d + 30);
            size_t comment_len = le16(d + 32);
            if (end - pos - DIR_SIZE < name_len) {
                ex_fail(x, "a zip's directory cut short");
            }
            if (pass == 1) {
                ExZipEntry *en = &z->entries[n];
                if (d[6] > MAX_VERSION) {
                    ex_fail(x, "a zip file of version %d.%d", d[6] / 10,
                            d[6] % 10);
                }
                en->name   = ex_strndup(x, (const char *)d + DIR_SIZE,
                                        name_len);
                en->flags  = le16(d + 8);
                en->method = le16(d + 10);
                en->crc    = le32(d + 16);
                u32 csize  = le32(d + 20);
                u32 fsize  = le32(d + 24);
                u32 local  = le32(d + 42);
                if (csize == 0xFFFFFFFFu || fsize == 0xFFFFFFFFu
                    || local == 0xFFFFFFFFu) {
                    ex_fail(x, "%s: a zip64 entry, which is not read here",
                            en->name);
                }
                en->csize = csize;
                en->size  = fsize;
                long long at = (long long)local + concat;
                en->local = (at < 0) ? (size_t)-1 : (size_t)at;
            }
            n++;
            pos += DIR_SIZE + name_len + extra_len + comment_len;
        }
        if (pass == 0) {
            z->entries = ex_alloc(x, sizeof *z->entries
                                     * (size_t)(n > 0 ? n : 1));
        }
        z->count = n;
    }
    return true;
}

u8 *ex_zip_read(Extract *x, const ExZip *z, int index, size_t *size)
{
    const ExZipEntry *en = &z->entries[index];
    if (en->local == (size_t)-1 || en->local > z->size
        || z->size - en->local < LOCAL_SIZE) {
        ex_fail(x, "%s: the zip's file header is cut short", en->name);
    }
    const u8 *h = z->data + en->local;
    if (memcmp(h, LOCAL_MAGIC, 4) != 0) {
        ex_fail(x, "%s: a bad file header in the zip", en->name);
    }
    size_t name_len  = le16(h + 26);
    size_t extra_len = le16(h + 28);
    size_t at = en->local + LOCAL_SIZE;
    if (z->size - at < name_len
        || memcmp(h + LOCAL_SIZE, en->name, name_len) != 0
        || strlen(en->name) != name_len) {
        ex_fail(x, "%s: its name in the zip's directory and its header "
                   "differ", en->name);
    }
    at += name_len + extra_len;
    if (en->flags & (FLAG_PATCHED | FLAG_STRONG)) {
        ex_fail(x, "%s: a zip entry of a kind not read (patched or "
                   "strongly encrypted)", en->name);
    }
    if (en->flags & FLAG_ENCRYPTED) {
        ex_fail(x, "%s: encrypted in the zip: a password is needed",
                en->name);
    }
    if (at > z->size || z->size - at < en->csize) {
        ex_fail(x, "%s: cut short in the zip", en->name);
    }
    const u8 *packed = z->data + at;

    u8 *out;
    size_t len;
    if (en->method == 0) {
        len = (en->csize < en->size) ? en->csize : en->size;
        out = ex_alloc(x, len + 1);
        memcpy(out, packed, len);
    } else if (en->method == 8) {
        size_t got = 0;
        u8 *raw = png_inflate_raw(packed, en->csize, &got);
        if (raw == NULL) {
            ex_fail(x, "%s: its deflate stream in the zip is bad", en->name);
        }
        len = (got < en->size) ? got : en->size;
        out = ex_alloc(x, len + 1);
        memcpy(out, raw, len);
        free(raw);
    } else {
        ex_fail(x, "%s: compressed in the zip by method %u, which is not "
                   "read here (only stored and deflated)", en->name,
                (unsigned)en->method);
    }
    out[len] = 0;
    if (ex_crc32(out, len) != en->crc) {
        ex_fail(x, "%s: bad CRC-32 in the zip", en->name);
    }
    if (size != NULL) {
        *size = len;
    }
    return out;
}
