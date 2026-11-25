#ifndef PS2_EXTRACT_EXZIP_H
#define PS2_EXTRACT_EXZIP_H

/* (build 191) Zip files, as Python's zipfile reads them, for what the
 * player hands the game: a cartridge image in a zip, the retranslation's
 * "Download ZIP" (tools/import_japanese.py, tools/make_assets.py). Stored
 * and deflated entries (src/core/png.c's inflate), each checked by its
 * CRC; no encryption, no zip64 (a cartridge's zip is far too small to
 * need it). Everything in the step's arena. */

#include "extract.h"

typedef struct {
    char  *name;             /* the bytes the zip holds */
    u32    crc;
    size_t csize;
    size_t size;
    u16    method;
    u16    flags;
    size_t local;            /* its local header, from the data's start */
} ExZipEntry;

typedef struct {
    const u8   *data;
    size_t      size;
    ExZipEntry *entries;     /* the central directory's order */
    int         count;
    const char *comment;     /* the archive's comment (NUL after it) */
    size_t      comment_len;
} ExZip;

/* zipfile.is_zipfile, and the directory read: false when `data` is not a
 * zip (or one whose directory cannot be read). */
bool ex_zip_open(Extract *x, const u8 *data, size_t size, ExZip *z);

/* The entry's bytes (NUL after them); a stop when it cannot be read --
 * as zipfile raises -- or its CRC is not the one written. */
u8 *ex_zip_read(Extract *x, const ExZip *z, int index, size_t *size);

/* zlib.crc32. */
u32 ex_crc32(const u8 *data, size_t n);
u32 ex_crc32_more(u32 crc, const u8 *data, size_t n);

#endif /* PS2_EXTRACT_EXZIP_H */
