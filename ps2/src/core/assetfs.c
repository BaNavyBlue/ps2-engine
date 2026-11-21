#include "assetfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Open addressing, linear probing. The asset tree is a little over 800
 * files; the table is sized so it never gets crowded. */
#define SLOTS 8192

typedef struct {
    char  *path;
    u8    *data;
    size_t size;
} Entry;

/* (build 125) The pack (assetfs.h), linked in by `make dist`: "PS2P", a
 * count, then each file -- its path's length with the NUL, the path and
 * its NUL, its size, its bytes and a NUL -- sizes little-endian. */
#ifdef PS2_PACKED
extern const u8 ps2_pack[];
extern const u8 ps2_pack_end[];
#endif

#define PACK_MAX 4096

typedef struct {
    const char *path;
    const u8   *data;
    size_t      size;
} PackEntry;

static PackEntry g_pack[PACK_MAX];
static int       g_pack_count = -1;     /* -1: not read yet */
static u32       g_pack_id;

/* (build 138b) The mods folder (assetfs_set_mods). (build 138c) Every
 * build's, not only a packed one's: each asset path asked for is looked
 * for there once, and the answer kept -- the mod's bytes, or none (data
 * NULL, also once set aside). */
#define MODS_DIR_MAX 1024
#define MODS_MAX     4096

typedef struct {
    char  *path;        /* "assets/..." */
    u8    *data;        /* the mod's bytes; NULL: no mod, or set aside */
    size_t size;
} Mod;

static char g_mods_dir[MODS_DIR_MAX];
static Mod  g_mods[MODS_MAX];
static int  g_mod_count;

static Entry  g_slot[SLOTS];
static int    g_files;
static size_t g_bytes;
static int    g_late;
static bool   g_preloaded;

static unsigned hash_path(const char *path)
{
    unsigned h = 2166136261u;               /* FNV-1a */
    for (const char *p = path; *p != '\0'; p++) {
        h = (h ^ (unsigned char)*p) * 16777619u;
    }
    return h;
}

static Entry *find_slot(const char *path)
{
    unsigned at = hash_path(path) & (SLOTS - 1);

    for (int probe = 0; probe < SLOTS; probe++) {
        Entry *e = &g_slot[at];
        if (e->path == NULL || strcmp(e->path, path) == 0) {
            return e;
        }
        at = (at + 1) & (SLOTS - 1);
    }
    return NULL;
}

#ifdef PS2_PACKED
static u32 pack_u32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16)
         | ((u32)p[3] << 24);
}
#endif

/* The pack's index, read once. */
static void pack_open(void)
{
    if (g_pack_count >= 0) {
        return;
    }
    g_pack_count = 0;
#ifdef PS2_PACKED
    const u8 *p   = ps2_pack;
    const u8 *end = ps2_pack_end;
    if (end - p < 8 || memcmp(p, "PS2P", 4) != 0) {
        fprintf(stderr, "assetfs: the pack linked in is not one\n");
        return;
    }
    u32 h = 2166136261u;
    for (const u8 *q = p; q < end; q++) {
        h = (h ^ *q) * 16777619u;
    }
    g_pack_id = h;
    u32 count = pack_u32(p + 4);
    p += 8;
    for (u32 i = 0; i < count && g_pack_count < PACK_MAX; i++) {
        if (end - p < 2) {
            break;
        }
        size_t plen = (size_t)p[0] | ((size_t)p[1] << 8);
        p += 2;
        if ((size_t)(end - p) < plen + 4) {
            break;
        }
        const char *path = (const char *)p;
        p += plen;
        size_t size = pack_u32(p);
        p += 4;
        if ((size_t)(end - p) < size + 1) {
            break;
        }
        g_pack[g_pack_count].path = path;
        g_pack[g_pack_count].data = p;
        g_pack[g_pack_count].size = size;
        g_pack_count++;
        p += size + 1;
    }
#endif
}

static bool read_file(const char *path, u8 **data, size_t *size);

/* (build 138b) "assets/" -- the part of a pack path the mods folder
 * stands for. */
#define MODS_ROOT     "assets/"
#define MODS_ROOT_LEN 7

void assetfs_set_mods(const char *dir)
{
    snprintf(g_mods_dir, sizeof g_mods_dir, "%s", (dir != NULL) ? dir : "");
}

/* (build 138c) The mod for an asset path: the file at the same place under
 * the mods folder, read the first time the path is asked for. NULL when
 * there is none, it was set aside, or there is no mods folder. */
static const Mod *mod_find(const char *path)
{
    if (g_mods_dir[0] == '\0'
        || strncmp(path, MODS_ROOT, MODS_ROOT_LEN) != 0) {
        return NULL;
    }
    for (int i = 0; i < g_mod_count; i++) {
        if (strcmp(g_mods[i].path, path) == 0) {
            return (g_mods[i].data != NULL) ? &g_mods[i] : NULL;
        }
    }
    if (g_mod_count >= MODS_MAX) {
        return NULL;
    }
    size_t len  = strlen(path) + 1;
    char  *copy = malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, path, len);
    Mod *m  = &g_mods[g_mod_count++];
    m->path = copy;
    m->data = NULL;
    m->size = 0;
    char file[MODS_DIR_MAX + 512];
    snprintf(file, sizeof file, "%s/%s", g_mods_dir, path + MODS_ROOT_LEN);
    if (!read_file(file, &m->data, &m->size)) {
        m->data = NULL;
        m->size = 0;
    }
    return (m->data != NULL) ? m : NULL;
}

int assetfs_mods_count(void)
{
    int n = 0;
    for (int i = 0; i < g_mod_count; i++) {
        if (g_mods[i].data != NULL) {
            n++;
        }
    }
    return n;
}

const char *assetfs_mods_path(int i)
{
    for (int k = 0; k < g_mod_count; k++) {
        if (g_mods[k].data == NULL) {
            continue;
        }
        if (i == 0) {
            return g_mods[k].path;
        }
        i--;
    }
    return NULL;
}

/* The pack's own bytes for `path`; NULL when there is no pack or the file
 * is not in it. */
static const PackEntry *pack_entry_for(const char *path)
{
    pack_open();
    for (int i = 0; i < g_pack_count; i++) {
        /* (build 138b) Never NULL in a pack; said for a tree build's
         * compiler, which sees an empty table. */
        if (g_pack[i].path != NULL && strcmp(g_pack[i].path, path) == 0) {
            return &g_pack[i];
        }
    }
    return NULL;
}

const u8 *assetfs_pack_find(const char *path, size_t *size)
{
    /* (build 138c) A mod first, in every build: this is the reader the
     * song text, the instruments, the drums and the tables ask before
     * they read the disk themselves. */
    const Mod *m = mod_find(path);
    if (m != NULL) {
        if (size != NULL) {
            *size = m->size;
        }
        return m->data;
    }
    const PackEntry *pe = pack_entry_for(path);
    if (pe == NULL) {
        return NULL;
    }
    if (size != NULL) {
        *size = pe->size;
    }
    return pe->data;
}

int assetfs_pack_count(void)
{
    pack_open();
    return g_pack_count;
}

bool assetfs_pack_entry(int i, const char **path, const u8 **data,
                        size_t *size)
{
    pack_open();
    if (i < 0 || i >= g_pack_count) {
        return false;
    }
    *path = g_pack[i].path;
    *data = g_pack[i].data;
    *size = g_pack[i].size;
    return true;
}

u32 assetfs_pack_id(void)
{
    pack_open();
    return g_pack_id;
}

static bool read_file(const char *path, u8 **data, size_t *size)
{
    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return false;
    }

    fseek(fh, 0, SEEK_END);
    long length = ftell(fh);
    fseek(fh, 0, SEEK_SET);

    u8 *buf = (length >= 0) ? malloc((size_t)length + 1) : NULL;
    bool ok = buf != NULL
           && fread(buf, 1, (size_t)length, fh) == (size_t)length;
    fclose(fh);

    if (!ok) {
        free(buf);
        return false;
    }
    buf[length] = '\0';
    *data = buf;
    *size = (size_t)length;
    return true;
}

static Entry *store(const char *path)
{
    Entry *e = find_slot(path);
    if (e == NULL) {
        fprintf(stderr, "assetfs: table full at %s\n", path);
        return NULL;
    }
    if (e->path != NULL) {
        return e;
    }

    u8    *data = NULL;
    size_t size = 0;
    const Mod *m = mod_find(path);          /* (build 138c) a mod first */
    if (m != NULL) {
        data = m->data;
        size = m->size;
    } else if (!read_file(path, &data, &size)) {
        return NULL;
    }

    size_t len = strlen(path) + 1;
    e->path = malloc(len);
    if (e->path == NULL) {
        free(data);
        return NULL;
    }
    memcpy(e->path, path, len);
    e->data = data;
    e->size = size;

    g_files++;
    g_bytes += size;
    return e;
}

/* (build 125) A pack file into the cache: its bytes where they are --
 * (build 138b) or its mod's. Neither is written or freed through the
 * cache. */
static void store_packed(int i)
{
    const PackEntry *pe = &g_pack[i];
    Entry *e = find_slot(pe->path);
    if (e == NULL || e->path != NULL) {
        return;
    }
    const Mod *m = mod_find(pe->path);
    e->path = (char *)pe->path;
    e->data = (m != NULL) ? m->data : (u8 *)pe->data;
    e->size = (m != NULL) ? m->size : pe->size;
    g_files++;
    g_bytes += e->size;
}

/* One mod set aside: the cache's copy back to the game's own -- the
 * pack's, or (a tree build, 138c) the file under assets/ -- said, and the
 * mod's bytes let go. */
static void set_aside(Mod *m)
{
    Entry *e = find_slot(m->path);
    if (e != NULL && e->path != NULL && e->data == m->data) {
        const PackEntry *pe = pack_entry_for(m->path);
        u8    *own  = NULL;
        size_t size = 0;
        if (pe != NULL) {
            own  = (u8 *)pe->data;
            size = pe->size;
        } else if (!read_file(m->path, &own, &size)) {
            own  = NULL;
            size = 0;
        }
        g_bytes = g_bytes - e->size + size;
        e->data = own;
        e->size = size;
    }
    fprintf(stderr, "mods: %s/%s set aside\n", g_mods_dir,
            m->path + MODS_ROOT_LEN);
    free(m->data);
    m->data = NULL;
    m->size = 0;
}

int assetfs_mods_drop(const char *prefix)
{
    int    dropped = 0;
    size_t len     = strlen(prefix);
    for (int i = 0; i < g_mod_count; i++) {
        Mod *m = &g_mods[i];
        if (m->data == NULL || strncmp(m->path, prefix, len) != 0) {
            continue;
        }
        set_aside(m);
        dropped++;
    }
    return dropped;
}

bool assetfs_mod_refused(const char *path)
{
    for (int i = 0; i < g_mod_count; i++) {
        Mod *m = &g_mods[i];
        if (m->data != NULL && strcmp(m->path, path) == 0) {
            set_aside(m);
            fprintf(stderr, "mods: it was refused (above); the game's own "
                            "%s is read in its place\n", path);
            return true;
        }
    }
    return false;
}

bool assetfs_is_mod(const char *path)
{
    return mod_find(path) != NULL;
}

int assetfs_mods_refused_under(const char *folder)
{
    int dropped = assetfs_mods_drop(folder);
    if (dropped > 0) {
        fprintf(stderr, "mods: refused (above); the game's own files in %s "
                        "are read in their place\n", folder);
    }
    return dropped;
}

u8 *assetfs_read_whole(const char *path, size_t *size, bool *owned)
{
    const u8 *packed = assetfs_pack_find(path, size);
    if (packed != NULL) {
        *owned = false;
        return (u8 *)packed;
    }
    u8 *data = NULL;
    if (!read_file(path, &data, size)) {
        return NULL;
    }
    *owned = true;
    return data;
}

bool asset_open(AssetReader *r, const char *path)
{
    size_t size = 0;
    r->p    = assetfs_pack_find(path, &size);
    r->left = size;
    r->fh   = NULL;
    if (r->p != NULL) {
        return true;
    }
    r->fh = fopen(path, "rb");
    return r->fh != NULL;
}

size_t asset_read(AssetReader *r, void *dst, size_t n)
{
    if (r->fh != NULL) {
        return fread(dst, 1, n, r->fh);
    }
    size_t k = (n < r->left) ? n : r->left;
    memcpy(dst, r->p, k);
    r->p    += k;
    r->left -= k;
    return k;
}

void asset_close(AssetReader *r)
{
    if (r->fh != NULL) {
        fclose(r->fh);
        r->fh = NULL;
    }
}

bool assetfs_preload(const char *manifest_path)
{
    u8    *text = NULL;
    size_t size = 0;

    /* (build 125) The pack's files, the manifest's place: nothing read --
     * (build 138b) but the mods folder's, in place of theirs. */
    pack_open();
    if (g_pack_count > 0) {
        for (int i = 0; i < g_pack_count; i++) {
            store_packed(i);
        }
        g_preloaded = true;
        return true;
    }

    if (!read_file(manifest_path, &text, &size)) {
        fprintf(stderr, "assetfs: no %s; assets will be read from disk as "
                        "they are first used (run `make manifest`)\n",
                manifest_path);
        return false;
    }

    const char *cursor = (const char *)text;
    char line[512];
    int  missing = 0;

    while (assetfs_next_line(&cursor, line, sizeof line)) {
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        if (store(line) == NULL) {
            missing++;
        }
    }
    free(text);

    if (missing != 0) {
        fprintf(stderr, "assetfs: %d files in %s could not be read; the "
                        "manifest is out of date\n", missing, manifest_path);
    }
    g_preloaded = true;
    return true;
}

const u8 *assetfs_get(const char *path, size_t *size)
{
    Entry *e = find_slot(path);

    if (e == NULL || e->path == NULL) {
        e = store(path);
        if (e == NULL) {
            return NULL;
        }
        g_late++;
        if (g_preloaded) {
            fprintf(stderr, "assetfs: %s was not preloaded; read from disk "
                            "now (run `make manifest`)\n", path);
        }
    }

    if (size != NULL) {
        *size = e->size;
    }
    return e->data;
}

bool assetfs_exists(const char *path)
{
    Entry *e = find_slot(path);
    if (e != NULL && e->path != NULL) {
        return true;
    }
    if (assetfs_pack_find(path, NULL) != NULL) {
        return true;
    }
    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return false;
    }
    fclose(fh);
    return true;
}

char *assetfs_copy_text(const char *path)
{
    size_t size = 0;
    const u8 *data = assetfs_get(path, &size);
    if (data == NULL) {
        return NULL;
    }

    char *copy = malloc(size + 1);
    if (copy != NULL) {
        memcpy(copy, data, size + 1);
    }
    return copy;
}

bool assetfs_next_line(const char **cursor, char *out, size_t cap)
{
    const char *p = *cursor;
    if (p == NULL || *p == '\0') {
        return false;
    }

    size_t n = 0;
    while (*p != '\0' && *p != '\n') {
        if (*p != '\r' && n + 1 < cap) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';

    if (*p == '\n') {
        p++;
    }
    *cursor = p;
    return true;
}

void assetfs_stats(int *files, size_t *bytes, int *late)
{
    if (files != NULL) {
        *files = g_files;
    }
    if (bytes != NULL) {
        *bytes = g_bytes;
    }
    if (late != NULL) {
        *late = g_late;
    }
}
