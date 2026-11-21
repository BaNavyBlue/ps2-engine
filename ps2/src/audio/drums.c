/* (build 136b) The DAC drums as files: assets/music/drums.json and the WAVs
 * it names, read into the drum table and Z80 RAM image the backends play
 * from (drums.h, drums_load). tools/extract_drums.py writes them. */
#include "drums.h"
#include "../core/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRUM_SAMPLES_MAX  16
#define DRUM_PROGRAM_END  0x118   /* the Z80 program's own bytes end here */
#define DRUM_PATH_MAX     512

/* A whole file, through the pack reader first, else the disk: a malloc'd
 * copy with a NUL after it (the JSON reader wants a string), or NULL. */
static u8 *read_whole(const char *path, Ps2Reader reader, size_t *size)
{
    const u8 *packed = (reader != NULL) ? reader(path, size) : NULL;
    if (packed != NULL) {
        u8 *copy = malloc(*size + 1);
        if (copy != NULL) {
            memcpy(copy, packed, *size);
            copy[*size] = 0;
        }
        return copy;
    }

    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return NULL;
    }
    u8 *buf = NULL;
    long len = (fseek(fh, 0, SEEK_END) == 0) ? ftell(fh) : -1;
    if (len >= 0 && fseek(fh, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)len + 1);
    }
    if (buf != NULL && fread(buf, 1, (size_t)len, fh) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    fclose(fh);
    if (buf != NULL) {
        buf[len] = 0;
        *size = (size_t)len;
    }
    return buf;
}

static u32 le16(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8);
}

static u32 le32(const u8 *p)
{
    return le16(p) | (le16(p + 2) << 16);
}

/* A WAV's sample bytes: RIFF/WAVE, a "fmt " chunk saying PCM, one channel,
 * eight bits, then a "data" chunk; other chunks are skipped. */
static bool wav_pcm8(const u8 *b, size_t n, const char *path,
                     const u8 **data, u32 *len)
{
    if (n < 12 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "drums: %s is not a WAV\n", path);
        return false;
    }
    bool   fmt_ok = false;
    size_t pos    = 12;
    while (pos + 8 <= n) {
        u32 size = le32(b + pos + 4);
        const u8 *body = b + pos + 8;
        if (size > n - pos - 8) {
            fprintf(stderr, "drums: %s: a chunk runs past the file's end\n",
                    path);
            return false;
        }
        if (memcmp(b + pos, "fmt ", 4) == 0) {
            fmt_ok = size >= 16 && le16(body) == 1 && le16(body + 2) == 1
                  && le16(body + 14) == 8;
            if (!fmt_ok) {
                fprintf(stderr, "drums: %s is not 8-bit mono PCM\n", path);
                return false;
            }
        } else if (memcmp(b + pos, "data", 4) == 0) {
            if (!fmt_ok) {
                fprintf(stderr, "drums: %s: its data comes before its "
                                "format\n", path);
                return false;
            }
            *data = body;
            *len  = size;
            return true;
        }
        pos += 8 + (size_t)size + (size & 1);
    }
    fprintf(stderr, "drums: %s has no data\n", path);
    return false;
}

typedef struct {
    const char *name;
    u32         addr;
    u32         len;
} DrumSample;

/* One sample: its WAV into `ram` at its address, refused where drums.h
 * says. */
static bool load_sample(const JsonValue *s, int i, const char *dir,
                        Ps2Reader reader, const char *path, DrumSample *out,
                        const DrumSample *before, u8 *ram)
{
    const char *name = json_str(s, "name", NULL);
    const char *file = json_str(s, "file", NULL);
    long        addr = json_int(s, "z80_address", -1);
    char        wav_path[DRUM_PATH_MAX];

    if (name == NULL || file == NULL) {
        fprintf(stderr, "drums: %s: sample %d has no name or no file\n",
                path, i);
        return false;
    }
    snprintf(wav_path, sizeof wav_path, "%s%s", dir, file);

    size_t size = 0;
    u8 *bytes = read_whole(wav_path, reader, &size);
    if (bytes == NULL) {
        fprintf(stderr, "drums: cannot open %s\n", wav_path);
        return false;
    }
    const u8 *data = NULL;
    u32 len = 0;
    bool ok = wav_pcm8(bytes, size, wav_path, &data, &len);
    if (ok && (addr < DRUM_PROGRAM_END || addr + (long)len > DRUM_Z80_RAM)) {
        fprintf(stderr, "drums: %s: sample %s at $%lX, %u bytes, is not "
                        "inside $%X-$%X (the Z80 program, then its RAM)\n",
                path, name, addr, len, DRUM_PROGRAM_END, DRUM_Z80_RAM);
        ok = false;
    }
    for (int j = 0; ok && j < i; j++) {
        if ((u32)addr < before[j].addr + before[j].len
            && before[j].addr < (u32)addr + len) {
            fprintf(stderr, "drums: %s: sample %s overlaps sample %s\n",
                    path, name, before[j].name);
            ok = false;
        }
    }
    if (ok) {
        memcpy(ram + addr, data, len);
        out->name = name;
        out->addr = (u32)addr;
        out->len  = len;
    }
    free(bytes);
    return ok;
}

/* One drum into table[i], refused where drums.h says. */
static bool load_drum(const JsonValue *d, int i, const char *path,
                      const DrumSample *samples, int nsamples, DrumEntry *table)
{
    long        byte   = json_int(d, "byte", -1);
    const char *sample = json_str(d, "sample", NULL);
    long        length = json_int(d, "length", -1);
    long        delay  = json_int(d, "delay", -1);

    if (byte < 0x80 || byte > 0xFF) {
        fprintf(stderr, "drums: %s: drum %d's byte %ld is not $80-$FF\n",
                path, i, byte);
        return false;
    }
    for (int j = 0; j < i; j++) {
        if (table[j].byte == (u8)byte) {
            fprintf(stderr, "drums: %s: drum $%02lX is given twice\n", path,
                    byte);
            return false;
        }
    }
    const DrumSample *s = NULL;
    for (int j = 0; sample != NULL && j < nsamples; j++) {
        if (strcmp(samples[j].name, sample) == 0) {
            s = &samples[j];
        }
    }
    if (s == NULL) {
        fprintf(stderr, "drums: %s: drum $%02lX names no sample in "
                        "\"samples\"\n", path, byte);
        return false;
    }
    if (delay < 1 || delay > 255) {
        fprintf(stderr, "drums: %s: drum $%02lX's delay %ld is not 1-255\n",
                path, byte, delay);
        return false;
    }
    if (length < 1 || s->addr + (u32)length > DRUM_Z80_RAM) {
        fprintf(stderr, "drums: %s: drum $%02lX plays %ld bytes from $%X, "
                        "not inside Z80 RAM\n", path, byte, length, s->addr);
        return false;
    }
    table[i].byte   = (u8)byte;
    table[i].delay  = (u16)delay;
    table[i].addr   = s->addr;
    table[i].length = (u32)length;
    return true;
}

bool drums_load(const char *path, Ps2Reader reader, DrumEntry *table,
                int *count, u8 *ram)
{
    DrumSample samples[DRUM_SAMPLES_MAX];
    char       dir[DRUM_PATH_MAX];
    size_t     size = 0;
    size_t     where = 0;

    *count = 0;
    memset(ram, 0, DRUM_Z80_RAM);

    u8 *text = read_whole(path, reader, &size);
    if (text == NULL) {
        fprintf(stderr, "drums: cannot open %s\n", path);
        return false;
    }
    JsonValue *root = json_parse((const char *)text, &where);
    free(text);
    if (root == NULL) {
        fprintf(stderr, "drums: %s is not JSON (at byte %zu)\n", path, where);
        return false;
    }

    const char *slash = strrchr(path, '/');
    int dir_len = (slash != NULL) ? (int)(slash - path + 1) : 0;
    snprintf(dir, sizeof dir, "%.*s", dir_len, path);

    const JsonValue *js = json_get(root, "samples");
    const JsonValue *jd = json_get(root, "drums");
    int  ns = json_length(js);
    int  nd = json_length(jd);
    bool ok = true;
    if (js == NULL || js->type != JSON_ARRAY || ns < 1
        || ns > DRUM_SAMPLES_MAX) {
        fprintf(stderr, "drums: %s: \"samples\" holds %d samples, not 1-%d\n",
                path, ns, DRUM_SAMPLES_MAX);
        ok = false;
    }
    if (ok && (jd == NULL || jd->type != JSON_ARRAY || nd < 1
               || nd > DRUM_MAX)) {
        fprintf(stderr, "drums: %s: \"drums\" holds %d drums, not 1-%d\n",
                path, nd, DRUM_MAX);
        ok = false;
    }
    for (int i = 0; ok && i < ns; i++) {
        ok = load_sample(json_at(js, i), i, dir, reader, path, &samples[i],
                         samples, ram);
    }
    for (int i = 0; ok && i < nd; i++) {
        ok = load_drum(json_at(jd, i), i, path, samples, ns, table);
    }

    *count = ok ? nd : 0;
    json_free(root);
    return ok;
}
