/* (build 187) The game-data extractors in C: tools/extract_gamedata.py
 * (items, characters, the footwear's agility), extract_techs.py (the
 * techniques and who learns which), extract_stores.py (the stores' stock
 * and Shir's steal lists) and extract_buildings.py (the portraits, the
 * buildings' music, the portraits' windows). Each table is read from the
 * image by the labels and constants of tools/rom_map.json, as the Python
 * reads it. */

#include "excommon.h"

#include <stdlib.h>
#include <string.h>

void ex_step_gamedata(Extract *x, const ExArgs *a);
void ex_step_techs(Extract *x, const ExArgs *a);
void ex_step_stores(Extract *x, const ExArgs *a);
void ex_step_buildings(Extract *x, const ExArgs *a);

static const char *key_of(Extract *x, const char **keys, int count, int i,
                          const char *what)
{
    if (i < 0 || i >= count) {
        ex_fail(x, "%s %d: no such item", what, i);
    }
    return keys[i];
}

static void write_json(Extract *x, const ExArgs *a, const char *file,
                       const JOut *v)
{
    ex_write_text(x, ex_path(x, a->out, file), jo_dump(x, v, 1, false));
}

/* ---- extract_gamedata.py ---- */

#define ITEM_SIZE  16
#define SETUP_SIZE 16
#define EXP_ROW    14

void ex_step_gamedata(Extract *x, const ExArgs *a)
{
    int nkeys = 0;
    const char **item_keys = ex_id_keys(x, "ItemID_", &nkeys);
    long long start = ex_addr(x, "InventoryData");
    long long size  = (long long)ex_addr(x, "TechniqueData") - start;
    if (size != (long long)ITEM_SIZE * nkeys) {
        ex_fail(x, "InventoryData is %lld bytes, expected %d", size,
                ITEM_SIZE * nkeys);
    }
    const u8 *raw = ex_read(x, start, size);

    JOut *items = jo_arr(x);
    for (int i = 0; i < nkeys; i++) {
        const u8 *rec = raw + (size_t)i * ITEM_SIZE;
        JOut *it = jo_obj(x);
        jo_seti(x, it, "id", i);
        jo_sets(x, it, "key", item_keys[i]);
        jo_sets(x, it, "name", ex_decode_name(x, rec, 10));
        jo_seti(x, it, "price", rec[10] << 8 | rec[11]);
        jo_seti(x, it, "action_flags", rec[12]);
        jo_seti(x, it, "equip_mask", rec[13]);
        jo_seti(x, it, "attack", rec[14]);
        jo_seti(x, it, "defense", rec[15]);
        jo_add(items, it);
    }

    /* the characters */
    long long names_at = ex_addr(x, "CharNames");
    long long names_len = (long long)ex_addr(x, "CharNamesEnd") - names_at;
    const u8 *names_raw = ex_read(x, names_at, names_len);
    long long setup_at = ex_addr(x, "CharInitialSetup");
    long long setup_len = (long long)ex_addr(x, "CharInitialSetupEnd")
                        - setup_at;
    const u8 *setup_raw = ex_read(x, setup_at, setup_len);
    long long ptrs = ex_addr(x, "CharExperiencePtrs");
    long long ptrs_end = ex_addr(x, "CharExperiencePtrsEnd");

    static const char *const SLOTS[5] = {
        "head", "right_hand", "left_hand", "body", "legs"
    };
    static const char *const STAT_KEYS[9] = {
        "hp", "tp", "strength", "mental", "agility", "luck", "dexterity",
        "attack", "defense"
    };

    JOut *chars = jo_arr(x);
    int i = 0;
    for (long long p = ptrs; p < ptrs_end; p += 4, i++) {
        u32 table = ex_u32(x, p) & 0xFFFFFF;
        long long off = (long long)i * SETUP_SIZE;
        if (off + 8 > setup_len) {
            ex_fail(x, "CharInitialSetup has no record %d", i);
        }
        const u8 *setup = setup_raw + off;
        long long have = setup_len - off;
        if (have > SETUP_SIZE) {
            have = SETUP_SIZE;
        }
        int count = setup[7];
        /* A table runs to the next label; past its last whole row (an
         * `even` byte) is no row. */
        size_t rows_len = 0;
        const u8 *rows = ex_block_at(x, table, &rows_len);
        JOut *levels = jo_arr(x);
        for (size_t r = 0; r + EXP_ROW <= rows_len; r += EXP_ROW) {
            const u8 *row = rows + r;
            JOut *e = jo_obj(x);
            jo_seti(x, e, "level", row[0]);
            jo_seti(x, e, "exp", row[1] << 16 | row[2] << 8 | row[3]);
            for (int k = 0; k < 9; k++) {
                jo_seti(x, e, STAT_KEYS[k], row[4 + k]);
            }
            jo_seti(x, e, "techs_a", row[13] >> 4);
            jo_seti(x, e, "techs_b", row[13] & 0xF);
            jo_add(levels, e);
        }
        JOut *c = jo_obj(x);
        long long noff = (long long)i * 4;
        long long nlen = names_len - noff;
        if (nlen > 4) {
            nlen = 4;
        }
        if (nlen < 0) {
            nlen = 0;
        }
        jo_sets(x, c, "name", ex_decode_name(x, names_raw + noff,
                                             (size_t)nlen));
        JOut *equipment = jo_obj(x);
        for (int j = 0; j < 5; j++) {
            jo_sets(x, equipment, SLOTS[j],
                    key_of(x, item_keys, nkeys, setup[j] & 0x7F, "equipment"));
        }
        jo_set(x, c, "equipment", equipment);
        JOut *inv = jo_arr(x);
        for (long long b = 8; b < 8 + count && b < have; b++) {
            JOut *slot = jo_obj(x);
            jo_sets(x, slot, "item",
                    key_of(x, item_keys, nkeys, setup[b] & 0x7F, "item"));
            jo_setb(x, slot, "equipped", (setup[b] & 0x80) != 0);
            jo_add(inv, slot);
        }
        jo_set(x, c, "items", inv);
        jo_set(x, c, "levels", levels);
        jo_add(chars, c);
    }

    /* A bonus per footwear item, Shoes to GardaBoots in id order. */
    int first = -1;
    int last  = -1;
    for (int k = 0; k < nkeys; k++) {
        if (first < 0 && strcmp(item_keys[k], "Shoes") == 0) {
            first = k;
        }
        if (last < 0 && strcmp(item_keys[k], "GardaBoots") == 0) {
            last = k;
        }
    }
    if (first < 0 || last < 0) {
        ex_fail(x, "no ItemID_Shoes or ItemID_GardaBoots");
    }
    JOut *agility = jo_obj(x);
    if (last >= first) {
        const u8 *bonus = ex_run_of(x, "AgilityIncreasesArray",
                                    (size_t)(last - first + 1));
        for (int k = 0; k <= last - first; k++) {
            jo_seti(x, agility, item_keys[first + k], bonus[k]);
        }
    }

    ex_mkdirs(x, a->out);
    write_json(x, a, "items.json", items);
    write_json(x, a, "characters.json", chars);
    write_json(x, a, "agility_bonus.json", agility);
    ex_say(x, "%d items, %d characters -> %s", jo_len(items), jo_len(chars),
           a->out);
}

/* ---- extract_techs.py ---- */

#define TECH_SIZE  8
#define AVAIL_SIZE 32

static int by_value(const void *a, const void *b)
{
    const ExValueName *p = a;
    const ExValueName *q = b;
    return (p->value > q->value) - (p->value < q->value);
}

void ex_step_techs(Extract *x, const ExArgs *a)
{
    long long start = ex_addr(x, "TechniqueData");
    int n = 0;
    u32 end = 0;
    const ExLabel **entries = ex_run_of_labels(x, "TechniqueData",
                                               ex_part_prefix, "Tech_", &n,
                                               &end);
    const char **labels = ex_alloc(x, sizeof *labels * (size_t)(n + 1));
    bool placed = true;
    for (int i = 0; i < n; i++) {
        labels[i] = entries[i]->name + strlen("Tech_");
        if (entries[i]->addr != start + (long long)TECH_SIZE * i) {
            placed = false;
        }
    }
    if ((long long)end - start != (long long)TECH_SIZE * n || !placed) {
        ex_fail(x, "TechniqueData is %lld bytes for %d entries",
                (long long)end - start, n);
    }
    /* sorted(constant_names(rom, "TechID_").items()): by value */
    ExValueNames ids = ex_constant_names(x, "TechID_", NULL);
    qsort(ids.at, (size_t)ids.count, sizeof *ids.at, by_value);
    for (int k = 0; k < ids.count; k++) {
        long long v = ids.at[k].value;
        if (v >= n || strcmp(labels[v], ids.at[k].name) != 0) {
            ex_fail(x, "TechID_%s is not entry %lld", ids.at[k].name, v);
        }
    }
    const u8 *raw = ex_read(x, start, (long long)end - start);

    JOut *techs = jo_arr(x);
    for (int i = 0; i < n; i++) {
        const u8 *rec = raw + (size_t)i * TECH_SIZE;
        JOut *t = jo_obj(x);
        jo_seti(x, t, "id", i);
        jo_sets(x, t, "key", labels[i]);
        jo_sets(x, t, "name", ex_decode_name(x, rec, 5));
        jo_seti(x, t, "tp", rec[5]);
        jo_seti(x, t, "flags", rec[6]);
        jo_seti(x, t, "rate", rec[7]);
        jo_add(techs, t);
    }

    long long astart = ex_addr(x, "CharAvailableTechniques");
    int an = 0;
    u32 aend = 0;
    ex_run_of_labels(x, "CharAvailableTechniques", ex_part_contains, "_Tech",
                     &an, &aend);
    long long span = (long long)aend - astart;
    long long count = ex_floordiv(span, AVAIL_SIZE);
    if (ex_mod(span, AVAIL_SIZE) > 1) {
        ex_fail(x, "CharAvailableTechniques is %lld bytes", span);
    }
    JOut *available = jo_obj(x);
    for (long long c = 0; c < count; c++) {
        long long at = astart + c * AVAIL_SIZE;
        int nn = 0;
        const ExLabel **names = ex_names_at(x, (u32)at, &nn);
        const char *who = NULL;
        int whos = 0;
        for (int k = 0; k < nn; k++) {
            if (ex_ends(names[k]->name, "_TechLevel")) {
                who = names[k]->name;
                whos++;
            }
        }
        if (whos != 1) {
            ex_fail(x, "no one <Who>_TechLevel label at $%llX",
                    (unsigned long long)at);
        }
        const u8 *block = ex_read(x, at, AVAIL_SIZE);
        JOut *map = jo_arr(x);
        JOut *battle = jo_arr(x);
        for (int k = 0; k < 32; k++) {
            if (block[k] >= n) {
                ex_fail(x, "a technique %d past the table", block[k]);
            }
            jo_add((k < 16) ? map : battle, jo_str(x, labels[block[k]]));
        }
        JOut *e = jo_obj(x);
        jo_set(x, e, "map", map);
        jo_set(x, e, "battle", battle);
        jo_set(x, available, ex_strndup(x, who, strlen(who)
                                                 - strlen("_TechLevel")), e);
    }

    ex_mkdirs(x, a->out);
    JOut *doc = jo_obj(x);
    jo_sets(x, doc, "source", "TechniqueData, CharAvailableTechniques");
    jo_set(x, doc, "techniques", techs);
    jo_set(x, doc, "available", available);
    write_json(x, a, "techniques.json", doc);
    ex_say(x, "%d techniques, %d characters", jo_len(techs),
           jo_len(available));
}

/* ---- extract_stores.py ---- */

#define STOCK      6
#define STEAL      8
#define LEVEL_SIZE 20
#define DOOR_SIZE  6
#define DOOR_LIST  14

/* `ItemStore` -> `Item Store`: re.findall(r"[A-Z][a-z0-9]*|[a-z0-9]+"). */
static char *words(Extract *x, const char *name)
{
    ExStr s;
    ex_str_init(x, &s);
    const char *c = name;
    while (*c != '\0') {
        bool upper = *c >= 'A' && *c <= 'Z';
        bool lower = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9');
        if (!upper && !lower) {
            c++;
            continue;
        }
        if (s.len > 0) {
            ex_str_addc(&s, ' ');
        }
        ex_str_addc(&s, *c);
        c++;
        while ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9')) {
            ex_str_addc(&s, *c);
            c++;
        }
    }
    return (s.buf != NULL) ? s.buf : ex_strdup(x, "");
}

typedef struct {
    int         store;
    const char *name;
} StoreName;

/* {store: "<level> <building>"} from every door into a store. */
static StoreName *store_doors(Extract *x, int *count)
{
    ExValueNames buildings = ex_constant_names(x, "BuildingID_", NULL);
    long long start = ex_addr(x, "LevelData");
    int n = 0;
    u32 end = 0;
    const ExLabel **levels = ex_run_of_labels(x, "LevelData", ex_part_prefix,
                                              "Level_", &n, &end);
    StoreName *names = ex_alloc(x, sizeof *names * 256);
    int nnames = 0;
    for (int i = 0; i < n; i++) {
        if (levels[i]->addr != start + (long long)LEVEL_SIZE * i) {
            ex_fail(x, "%s is not LevelData record %d", levels[i]->name, i);
        }
        long long door = ex_u32(x, (long long)levels[i]->addr + DOOR_LIST)
                       & 0xFFFFFF;
        while (ex_u8(x, door) != 0xFF) {
            const u8 *rec = ex_read(x, door, DOOR_SIZE);
            int kind  = rec[2];
            int store = rec[5];
            door += DOOR_SIZE;
            const char *building = ex_value_name(&buildings, kind);
            if (building == NULL) {
                building = "";
            }
            if (kind == 0 || !ex_ends(building, "Store")) {
                continue;
            }
            char *name = ex_fmt(x, "%s %s",
                                levels[i]->name + strlen("Level_"),
                                words(x, building));
            const char *had = NULL;
            for (int k = 0; k < nnames; k++) {
                if (names[k].store == store) {
                    had = names[k].name;
                }
            }
            if (had == NULL) {
                names[nnames].store = store;
                names[nnames].name  = name;
                nnames++;
            } else if (strcmp(had, name) != 0) {
                ex_fail(x, "store %d is both %s and %s", store, had, name);
            }
        }
    }
    *count = nnames;
    return names;
}

void ex_step_stores(Extract *x, const ExArgs *a)
{
    int nkeys = 0;
    const char **keys = ex_id_keys(x, "ItemID_", &nkeys);

    size_t len = 0;
    const u8 *raw = ex_blob(x, "StoreEquipItemArray", &len);
    if (len % STOCK > 1) {
        ex_fail(x, "StoreEquipItemArray is %zu bytes", len);
    }
    int nnames = 0;
    StoreName *names = store_doors(x, &nnames);
    JOut *stores = jo_arr(x);
    for (size_t i = 0; i < len / STOCK; i++) {
        const char *name = NULL;
        for (int k = 0; k < nnames; k++) {
            if (names[k].store == (int)i) {
                name = names[k].name;
            }
        }
        if (name == NULL) {
            ex_fail(x, "no door opens store %zu, so it has no name", i);
        }
        JOut *s = jo_obj(x);
        jo_seti(x, s, "index", (long long)i);
        jo_sets(x, s, "name", name);
        JOut *stock = jo_arr(x);
        for (int k = 0; k < STOCK; k++) {
            jo_add(stock, jo_str(x, key_of(x, keys, nkeys,
                                           raw[i * STOCK + (size_t)k],
                                           "stock")));
        }
        jo_set(x, s, "items", stock);
        jo_add(stores, s);
    }

    /* Shir */
    JOut *steal = jo_obj(x);
    int n = 0;
    u32 end = 0;
    const ExLabel **lists = ex_run_of_labels(x, "StealItemArray",
                                             ex_part_prefix,
                                             "StealItemArray_", &n, &end);
    for (int i = 0; i < n; i++) {
        size_t blen = 0;
        ex_blob(x, lists[i]->name, &blen);
        if (blen < STEAL) {
            ex_fail(x, "%s is %zu entries", lists[i]->name, blen);
        }
        const u8 *picks = ex_read(x, lists[i]->addr, STEAL);
        JOut *list = jo_arr(x);
        for (int k = 0; k < STEAL; k++) {
            jo_add(list, jo_str(x, key_of(x, keys, nkeys, picks[k], "steal")));
        }
        jo_set(x, steal, ex_lower(x, lists[i]->name + strlen("StealItemArray_")),
               list);
    }

    ex_mkdirs(x, a->out);
    JOut *doc = jo_obj(x);
    jo_sets(x, doc, "source", "StoreEquipItemArray");
    jo_set(x, doc, "stores", stores);
    write_json(x, a, "stores.json", doc);
    doc = jo_obj(x);
    jo_sets(x, doc, "source", "StealItemArray");
    jo_set(x, doc, "lists", steal);
    write_json(x, a, "steal.json", doc);
    ex_say(x, "%d stores, %d steal lists", jo_len(stores), jo_len(steal));
}

/* ---- extract_buildings.py ---- */

#define PAL_PTR_SIZE          8
#define WINDOW_SIZE           8
#define MAX_PORTRAIT_WINDOWS  0x38

/* The PalettePtrs entry a portrait's palette id loads: the first that
 * points at the same colours. */
static int palette_entry(Extract *x, int pal_id)
{
    long long table = ex_addr(x, "PalettePtrs");
    u32 want = ex_u32(x, table + PAL_PTR_SIZE * pal_id) & 0xFFFFFF;
    for (int k = 0; k <= pal_id; k++) {
        if ((ex_u32(x, table + PAL_PTR_SIZE * k) & 0xFFFFFF) == want) {
            return k;
        }
    }
    ex_fail(x, "palette %d: no entry", pal_id);
}

/* The PNG stem for the art at an address: its binclude'd file's, or the
 * label's when the art is written out in place. */
static const char *art_name(Extract *x, u32 address)
{
    const char *file = ex_file_at(x, address);
    if (file != NULL) {
        char *key = ex_strdup(x, file);
        for (char *c = key; *c != '\0'; c++) {
            if (*c == '\\') {
                *c = '/';
            }
        }
        if (ex_starts(key, "art/") && ex_ends(file, ".bin")) {
            return ex_art_name(ex_file_stem(x, file));
        }
    }
    const char *label = ex_label_at(x, address, "");
    if (label == NULL) {
        ex_fail(x, "no art at $%X", (unsigned)address);
    }
    return ex_art_name(label);
}

void ex_step_buildings(Extract *x, const ExArgs *a)
{
    int n = 0;
    u32 end = 0;
    const ExLabel **entries = ex_run_of_labels(x, "PortraitTable",
                                               ex_part_prefix, "PtrPortrait_",
                                               &n, &end);
    JOut *portraits = jo_arr(x);
    for (int i = 0; i < n; i++) {
        u32 long_ = ex_u32(x, entries[i]->addr);
        JOut *p = jo_obj(x);
        jo_seti(x, p, "index", i + 1);
        jo_sets(x, p, "name", entries[i]->name + strlen("PtrPortrait_"));
        jo_sets(x, p, "art", art_name(x, long_ & 0xFFFFFF));
        jo_seti(x, p, "palette", palette_entry(x, (int)(long_ >> 24)));
        jo_add(portraits, p);
    }

    int nnames = 0;
    const char **names = ex_id_keys(x, "BuildingID_", &nnames);
    ExValueNames music = ex_constant_names(x, "MusicID_", NULL);
    size_t len = 0;
    const u8 *raw = ex_blob(x, "BuildingMusicPtrs", &len);
    if (!((size_t)nnames <= len && len <= (size_t)nnames + 1)) {
        ex_fail(x, "BuildingMusicPtrs is %zu bytes for %d buildings", len,
                nnames);
    }
    JOut *buildings = jo_arr(x);
    for (int i = 0; i < nnames; i++) {
        const char *music_name = ex_value_name(&music, raw[i]);
        if (music_name == NULL) {
            ex_fail(x, "building %s: music $%02X is no MusicID_", names[i],
                    raw[i]);
        }
        JOut *b = jo_obj(x);
        jo_seti(x, b, "index", i);
        jo_sets(x, b, "name", names[i]);
        jo_seti(x, b, "music", raw[i]);
        jo_sets(x, b, "music_name", music_name);
        jo_add(buildings, b);
    }

    /* The portraits' windows: WindowArtLayoutPtrs entries from
     * PtrWin_PortraitStart on, to the first whose art is no WinArt_. */
    long long at = ex_addr(x, "PtrWin_PortraitStart");
    JOut *windows = jo_arr(x);
    while (jo_len(windows) < MAX_PORTRAIT_WINDOWS) {
        u16 pos = ex_u16(x, at);
        u32 art = ex_u32(x, at + 2);
        u8  w   = ex_u8(x, at + 6);
        u8  h   = ex_u8(x, at + 7);
        const char *label = (art < x->map.size)
                          ? ex_label_at(x, art, "WinArt_") : NULL;
        if (label == NULL) {
            break;
        }
        JOut *e = jo_obj(x);
        jo_seti(x, e, "portrait", jo_len(windows));
        jo_seti(x, e, "pos", pos);
        jo_sets(x, e, "art", label);
        jo_seti(x, e, "w", w);
        jo_seti(x, e, "h", h);
        jo_add(windows, e);
        at += WINDOW_SIZE;
    }

    ex_mkdirs(x, a->out);
    write_json(x, a, "portrait_windows.json", windows);
    write_json(x, a, "portraits.json", portraits);
    write_json(x, a, "buildings.json", buildings);
    ex_say(x, "%d portraits, %d buildings -> %s", jo_len(portraits),
           jo_len(buildings), a->out);
}
