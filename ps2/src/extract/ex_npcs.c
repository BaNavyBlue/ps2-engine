/* (build 188) tools/extract_npcs.py in C: who stands where in each level
 * (LevelSpriteData, six bytes an object), the art lists LoadLevelData
 * decompresses for them (loc_283EE), the townspeople's types, the chests
 * and the scene objects, each from its object's init in the 68000 code
 * (ex68k.c): the `move #value, n(a0)` instructions it runs before the
 * next label, `bra` or `rts`. Out: assets/data/level_sprites.json,
 * npc_types.json, people_dialogue.json, chests.json, scene_objects.json
 * and assets/tiles/sprite_<name>.png. */

#include "ex68k.h"
#include "exart.h"
#include "excommon.h"

#include <stdlib.h>
#include <string.h>

void ex_step_npcs(Extract *x, const ExArgs *a);

#define FIRST_LEVEL_OBJECT 0x14
#define FIELD_MAPPINGS     4
#define FIELD_TILE         8
#define FIELD_WAIT         0x28
#define FIELD_BOX          0x2C
#define FIELD_FLAGS        2
#define LEVEL_TYPE         0xF710

/* ---- reading the code ---- */

/* The one label at an address starting with `prefix`; none or two stop
 * the run (label_of). */
static const char *label_of(Extract *x, u32 address, const char *prefix)
{
    int n = 0;
    const ExLabel **names = ex_names_at(x, address, &n);
    const char *found = NULL;
    int count = 0;
    ExStr s;
    ex_str_init(x, &s);
    for (int i = 0; i < n; i++) {
        if (ex_starts(names[i]->name, prefix)) {
            ex_str_addf(&s, "%s%s", (count > 0) ? ", " : "", names[i]->name);
            found = names[i]->name;
            count++;
        }
    }
    if (count != 1) {
        ex_fail(x, "$%X: %s%s%s", (unsigned)address,
                (count > 0) ? s.buf : "no label",
                (prefix[0] != '\0') ? " starting " : "", prefix);
    }
    return found;
}

static long long bra_target(Extract *x, long long at)
{
    ExInsn in = ex_m68k(x, at);
    if (in.op != 0x6000) {
        ex_fail(x, "$%llX is not a bra.w", (unsigned long long)at);
    }
    return in.target;
}

/* The table a routine's `jsr table(pc,d0.w)` jumps through, or -1. */
static long long jump_table(Extract *x, long long routine)
{
    long long at = routine;
    for (int k = 0; k < 8; k++) {
        ExInsn in = ex_m68k(x, at);
        if ((in.op & 0xFFC0) == 0x4E80 && in.ops[0].kind == EX_EA_PCX) {
            return in.ops[0].value;
        }
        if (ex_insn_rts(&in) || ex_insn_bra(&in)
            || (in.op & 0xFFC0) == 0x4EC0) {
            return -1;
        }
        at += in.length;
    }
    return -1;
}

static long long init_of(Extract *x, long long routine)
{
    long long table = jump_table(x, routine);
    if (table < 0) {
        ex_fail(x, "$%llX has no jump table", (unsigned long long)routine);
    }
    return bra_target(x, table);
}

typedef struct {
    ExInsn *at;
    int     count;
} Code;

static Code init_code(Extract *x, long long init)
{
    Code c;
    int cap = 32;
    c.at    = ex_alloc(x, sizeof *c.at * (size_t)cap);
    c.count = 0;
    long long at = init;
    for (;;) {
        int n = 0;
        if (at != init) {
            ex_names_at(x, (u32)at, &n);
            if (n > 0) {
                break;
            }
        }
        ExInsn in = ex_m68k(x, at);
        if (ex_insn_bra(&in) || ex_insn_rts(&in)) {
            break;
        }
        if (c.count == cap) {
            ExInsn *more = ex_alloc(x, sizeof *more * (size_t)cap * 2);
            memcpy(more, c.at, sizeof *more * (size_t)cap);
            c.at = more;
            cap *= 2;
        }
        c.at[c.count++] = in;
        at += in.length;
    }
    return c;
}

typedef struct {
    int       size;
    long long value;
} Store;

/* The immediates the code puts in field(a0), in order. */
static int stores(Extract *x, const Code *c, int field, Store **out)
{
    Store *s = ex_alloc(x, sizeof *s * (size_t)(c->count + 1));
    int n = 0;
    for (int i = 0; i < c->count; i++) {
        const ExInsn *in = &c->at[i];
        int top = in->op >> 12;
        if (top >= 1 && top <= 3 && in->nops == 2
            && in->ops[0].kind == EX_EA_IMM && in->ops[1].kind == 5
            && in->ops[1].reg == 0 && in->ops[1].value == field) {
            s[n].size  = in->size;
            s[n].value = in->ops[0].value;
            n++;
        }
    }
    *out = s;
    return n;
}

static long long first_store(Extract *x, const Code *c, int field,
                             const char *what)
{
    Store *s = NULL;
    if (stores(x, c, field, &s) == 0) {
        ex_fail(x, "the init at $%llX sets no %s",
                (unsigned long long)((c->count > 0) ? c->at[0].at : 0), what);
    }
    return s[0].value;
}

/* The labels of the mappings the code puts in 4(a0), in order. */
static const char **mapping_stores(Extract *x, const Code *c, int *count)
{
    Store *s = NULL;
    int n = stores(x, c, FIELD_MAPPINGS, &s);
    const char **out = ex_alloc(x, sizeof *out * (size_t)(n + 1));
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (s[i].size == 4) {
            out[k++] = label_of(x, (u32)s[i].value, "");
        }
    }
    *count = k;
    return out;
}

/* ---- the tables ---- */

/* Word offsets from a label to the next: the addresses they point to. */
static long long *offset_table(Extract *x, const char *label, int *count)
{
    long long base = ex_addr(x, label);
    size_t len = 0;
    const u8 *data = ex_blob(x, label, &len);
    long long *out = ex_alloc(x, sizeof *out * (len / 2 + 1));
    int n = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        out[n++] = base + (data[i] << 8 | data[i + 1]);
    }
    *count = n;
    return out;
}

static long long object_routine(Extract *x, long long obj_id)
{
    return bra_target(x, ex_addr(x, "ObjectTable") + 4 * (obj_id - 1));
}

static long long obj_value(Extract *x, const char *name)
{
    return ex_const(x, ex_fmt(x, "ObjID_%s", name));
}

static JOut *parse_mappings(Extract *x, const char *label)
{
    int n = 0;
    long long *frames_at = offset_table(x, label, &n);
    JOut *frames = jo_arr(x);
    for (int f = 0; f < n; f++) {
        long long at = frames_at[f];
        JOut *pieces = jo_arr(x);
        int count = ex_u8(x, at);
        for (int i = 0; i < count; i++) {
            const u8 *p = ex_read(x, at + 1 + i * 5, 5);
            int y    = p[0];
            int size = p[1];
            int word = p[2] << 8 | p[3];
            int xx   = p[4];
            JOut *piece = jo_obj(x);
            jo_seti(x, piece, "x", (xx > 127) ? xx - 256 : xx);
            jo_seti(x, piece, "y", (y > 127) ? y - 256 : y);
            jo_seti(x, piece, "w", ((size >> 2) & 3) + 1);
            jo_seti(x, piece, "h", (size & 3) + 1);
            jo_seti(x, piece, "tile", word & 0x7FF);
            jo_setb(x, piece, "flip_h", (word & 0x0800) != 0);
            jo_setb(x, piece, "flip_v", (word & 0x1000) != 0);
            jo_add(pieces, piece);
        }
        jo_add(frames, pieces);
    }
    return frames;
}

static JOut *head(Extract *x, long long obj_id, const char *name,
                  long long tile_word)
{
    JOut *j = jo_obj(x);
    jo_seti(x, j, "object", obj_id);
    jo_sets(x, j, "name", name);
    jo_seti(x, j, "tile", tile_word & 0x7FF);
    jo_seti(x, j, "palette_line", (tile_word >> 13) & 3);
    return j;
}

/* A chest's init: tile word, hit-box row, mappings, and a second mappings
 * pointer under a `cmpi.w #n, ($FFFFF710).w` for level type n. */
static JOut *parse_chest(Extract *x, const char *name, long long value)
{
    Code code = init_code(x, init_of(x, object_routine(x, value)));
    int nmaps = 0;
    const char **maps = mapping_stores(x, &code, &nmaps);
    long long tile_word = first_store(x, &code, FIELD_TILE, "tile word");
    JOut *chest = head(x, value, name, tile_word);
    jo_seti(x, chest, "box", first_store(x, &code, FIELD_BOX, "hit-box row"));
    if (nmaps == 0) {
        ex_fail(x, "%s: no mappings", name);
    }
    jo_set(x, chest, "frames", parse_mappings(x, maps[0]));
    for (int i = 0; i < code.count; i++) {
        const ExInsn *in = &code.at[i];
        if (in->op == 0x0C78 && in->nops == 2
            && in->ops[1].kind == EX_EA_ABS && in->ops[1].reg == 0
            && in->ops[1].value == (0xFF0000 | LEVEL_TYPE)) {
            if (nmaps > 1) {
                jo_seti(x, chest, "alt_level_type", in->ops[0].value);
                jo_set(x, chest, "alt_frames", parse_mappings(x, maps[1]));
            }
            break;
        }
    }
    return chest;
}

/* A scene object's init: the hit-box row may be left 0, and bit 5 of a
 * byte put in 2(a0) asks to be drawn in front. */
static JOut *parse_scene(Extract *x, long long obj_id, const char *name)
{
    Code code = init_code(x, init_of(x, object_routine(x, obj_id)));
    long long tile_word = first_store(x, &code, FIELD_TILE, "tile word");
    Store *box = NULL;
    int nbox = stores(x, &code, FIELD_BOX, &box);
    Store *flags = NULL;
    int nflags = stores(x, &code, FIELD_FLAGS, &flags);
    long long first_flag = -1;
    for (int i = 0; i < nflags && first_flag < 0; i++) {
        if (flags[i].size == 1) {
            first_flag = flags[i].value;
        }
    }
    JOut *j = head(x, obj_id, name, tile_word);
    jo_seti(x, j, "box", (nbox > 0) ? box[0].value : 0);
    jo_setb(x, j, "front", first_flag >= 0 && (first_flag & 0x20) != 0);
    int nmaps = 0;
    const char **maps = mapping_stores(x, &code, &nmaps);
    if (nmaps == 0) {
        ex_fail(x, "%s: no mappings", name);
    }
    jo_set(x, j, "frames", parse_mappings(x, maps[0]));
    return j;
}

static JOut *person(Extract *x, long long routine, long long obj_id,
                    const char *name, const char *wander)
{
    Code code = init_code(x, init_of(x, routine));
    long long tile_word = first_store(x, &code, FIELD_TILE, "tile word");
    JOut *j = head(x, obj_id, name, tile_word);
    jo_sets(x, j, "wander", wander);
    jo_seti(x, j, "first_wait", first_store(x, &code, FIELD_WAIT,
                                            "first wait"));
    int nmaps = 0;
    const char **maps = mapping_stores(x, &code, &nmaps);
    if (nmaps == 0) {
        ex_fail(x, "%s: no mappings", name);
    }
    jo_set(x, j, "frames", parse_mappings(x, maps[0]));
    return j;
}

static JOut *chest_contents(Extract *x)
{
    ExValueNames items = ex_constant_names(x, "ItemID_", NULL);
    size_t len = 0;
    const u8 *data = ex_blob(x, "TreasureChestContentArray", &len);
    JOut *out = jo_arr(x);
    for (size_t n = 1; n <= len / 2; n++) {
        int value = data[2 * n - 2] << 8 | data[2 * n - 1];
        JOut *c = jo_obj(x);
        jo_seti(x, c, "index", (long long)n);
        if (value & 0x8000) {
            const char *item = ex_value_name(&items, value & 0x7FFF);
            if (item == NULL) {
                ex_fail(x, "chest %zu: no ItemID_ is %d", n, value & 0x7FFF);
            }
            jo_sets(x, c, "item", item);
        } else if (value == 0) {
            jo_setb(x, c, "garbage", true);
        } else {
            jo_seti(x, c, "meseta", value);
        }
        jo_add(out, c);
    }
    return out;
}

/* ---- the step ---- */

static const JsonValue *level_record(const JsonValue *levels, int index)
{
    for (const JsonValue *lv = levels->child; lv != NULL; lv = lv->next) {
        const JsonValue *i = json_get(lv, "index");
        if (i != NULL && i->type == JSON_NUMBER && (int)i->number == index) {
            return lv;
        }
    }
    return NULL;
}

typedef struct {
    const char *name;
    long long   value;
    int         rank;
} Obj;

static int cmp_obj(const void *a, const void *b)
{
    const Obj *p = a;
    const Obj *q = b;
    if (p->value != q->value) {
        return (p->value < q->value) ? -1 : 1;
    }
    return (p->rank > q->rank) - (p->rank < q->rank);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static JOut *sprite_levels(Extract *x, const JsonValue *levels,
                           long long *objects)
{
    int n = 0;
    long long *tables = offset_table(x, "LevelSpriteData", &n);
    JOut *sprites = jo_arr(x);
    for (int index = 0; index < n; index++) {
        const char *label = label_of(x, (u32)tables[index], "");
        size_t len = 0;
        const u8 *raw = ex_blob(x, label, &len);
        JOut *objs = jo_arr(x);
        for (size_t at = 0; at + 5 < len; at += 6) {
            u8 b0 = raw[at];
            u8 b1 = raw[at + 1];
            if (b0 == 0 && b1 == 0) {
                break;
            }
            JOut *o = jo_obj(x);
            jo_seti(x, o, "object", FIRST_LEVEL_OBJECT + (b0 >> 2));
            jo_seti(x, o, "interaction", b0 & 3);
            jo_seti(x, o, "index", b1);
            jo_seti(x, o, "x", raw[at + 2] << 8 | raw[at + 3]);
            jo_seti(x, o, "y", raw[at + 4] << 8 | raw[at + 5]);
            jo_add(objs, o);
            (*objects)++;
        }
        const JsonValue *record = level_record(levels, index);
        const char *name = "?";
        long extra = 0;
        if (record != NULL) {
            name  = json_str(record, "name", "?");
            extra = json_int(record, "extra_art", 0);
        }
        JOut *s = jo_obj(x);
        jo_seti(x, s, "level", index);
        jo_sets(x, s, "name", name);
        jo_sets(x, s, "source", label);
        jo_seti(x, s, "art_list", extra & 0x7F);
        jo_set(x, s, "objects", objs);
        jo_add(sprites, s);
    }
    return sprites;
}

void ex_step_npcs(Extract *x, const ExArgs *a)
{
    const JsonValue *manifest = ex_read_json(x, ex_path(x, a->out,
                                                        "levels/levels.json"));
    const JsonValue *levels = json_get(manifest, "levels");
    if (levels == NULL || levels->type != JSON_ARRAY) {
        ex_fail(x, "levels.json has no \"levels\"");
    }

    /* ---- who stands where */
    long long objects = 0;
    JOut *sprites = sprite_levels(x, levels, &objects);

    /* ---- their art: a count less one, then an art pointer and a VRAM
     * address an entry */
    int nlists = 0;
    long long *lists = offset_table(x, "loc_283EE", &nlists);
    JOut *art_lists = jo_arr(x);
    int cap = 64;
    int nwanted = 0;
    const char **wanted = ex_alloc(x, sizeof *wanted * (size_t)cap);
    for (int l = 0; l < nlists; l++) {
        long long at = lists[l];
        JOut *entries = jo_arr(x);
        int count = ex_u16(x, at) + 1;
        for (int n = 0; n < count; n++) {
            long long entry = at + 2 + 6 * n;
            const char *label = label_of(x, ex_u32(x, entry) & 0xFFFFFF, "");
            const char *art = ex_pair(EX_SPRITE_ART_NAMES, label);
            if (art == NULL) {
                ex_fail(x, "%s has no name in tools/asset_names.py", label);
            }
            JOut *e = jo_obj(x);
            jo_sets(x, e, "art", art);
            jo_sets(x, e, "source", label);
            jo_seti(x, e, "tile", ex_u16(x, entry + 4) / 32);
            jo_add(entries, e);
            bool seen = false;
            for (int k = 0; k < nwanted; k++) {
                if (strcmp(wanted[k], label) == 0) {
                    seen = true;
                }
            }
            if (!seen) {
                if (nwanted == cap) {
                    const char **more = ex_alloc(x, sizeof *more
                                                    * (size_t)cap * 2);
                    memcpy(more, wanted, sizeof *more * (size_t)cap);
                    wanted = more;
                    cap *= 2;
                }
                wanted[nwanted++] = label;
            }
        }
        jo_add(art_lists, entries);
    }

    char *tiles_dir = ex_path(x, a->out, "tiles");
    ex_mkdirs(x, tiles_dir);
    qsort(wanted, (size_t)nwanted, sizeof *wanted, cmp_str);
    ExRgb grey;
    memset(&grey, 0, sizeof grey);
    ex_rgb_grey_ramp(&grey);
    for (int k = 0; k < nwanted; k++) {
        long long at = ex_addr(x, wanted[k]);
        ExTiles tiles = ex_art(x, x->map.rom + at, x->map.size - (size_t)at);
        char *file = ex_fmt(x, "sprite_%s.png",
                            ex_pair(EX_SPRITE_ART_NAMES, wanted[k]));
        ex_png_tiles(x, ex_path(x, tiles_dir, file), tiles.data, tiles.count,
                     &grey, 16, true, NULL, NULL);
    }

    /* ---- the townspeople's types: the Mota objects whose second routine
     * is one of the two wanders, in value order */
    long long vertical   = ex_addr(x, "loc_3C06");
    long long horizontal = ex_addr(x, "loc_3DCA");
    ExMap *m = &x->map;
    Obj *objs = ex_alloc(x, sizeof *objs * (size_t)(m->nconsts + 1));
    int nobjs = 0;
    for (int i = 0; i < m->nconsts; i++) {
        const ExConst *c = &m->consts[m->const_by_name[i].rank];
        if (ex_starts(c->name, "ObjID_")) {
            objs[nobjs].name  = c->name + 6;
            objs[nobjs].value = c->value;
            objs[nobjs].rank  = nobjs;
            nobjs++;
        }
    }
    qsort(objs, (size_t)nobjs, sizeof *objs, cmp_obj);
    JOut *types = jo_arr(x);
    for (int i = 0; i < nobjs; i++) {
        if (!ex_starts(objs[i].name, "Mota")) {
            continue;
        }
        long long routine = object_routine(x, objs[i].value);
        long long table = jump_table(x, routine);
        if (table < 0) {
            continue;
        }
        ExInsn second = ex_m68k(x, table + 4);
        if (second.op != 0x6000) {
            continue;
        }
        const char *wander = (second.target == vertical) ? "vertical"
                           : (second.target == horizontal) ? "horizontal"
                           : NULL;
        if (wander == NULL) {
            continue;
        }
        jo_add(types, person(x, routine, objs[i].value, objs[i].name, wander));
    }
    /* object $1B (loc_4288): the Mota people in Roron, a four-way wander */
    jo_add(types, person(x, ex_addr(x, "loc_4288"), 0x1B, "MotaRoron", "four"));
    jo_add(types, person(x, ex_addr(x, "Obj_Dezolian"),
                         obj_value(x, "Dezolian"), "Dezolian", "dezolian"));
    jo_add(types, person(x, ex_addr(x, "Obj_MuskCat"),
                         obj_value(x, "MuskCat"), "MuskCat", "muskcat"));

    /* ---- what they say */
    JOut *dialogue = jo_obj(x);
    jo_seti(x, dialogue, "bank", 0x16);
    static const char *const SAYS[3][2] = {
        { "after_climatrol", "loc_E6B0" },
        { "before_climatrol", "loc_E70C" },
        { "later", "loc_E730" },
    };
    for (int k = 0; k < 3; k++) {
        size_t len = 0;
        const u8 *b = ex_blob(x, SAYS[k][1], &len);
        JOut *arr = jo_arr(x);
        for (size_t i = 0; i < len; i++) {
            jo_add(arr, jo_int(x, b[i]));
        }
        jo_set(x, dialogue, SAYS[k][0], arr);
    }

    /* ---- the chests */
    JOut *chest_types = jo_arr(x);
    jo_add(chest_types, parse_chest(x, "DezoTreasureChest",
                                    obj_value(x, "DezoTreasureChest")));
    jo_add(chest_types, parse_chest(x, "MotaTreasureChest",
                                    obj_value(x, "MotaTreasureChest")));
    JOut *chests = jo_obj(x);
    jo_sets(x, chests, "source", "TreasureChestContentArray");
    jo_set(x, chests, "types", chest_types);
    JOut *contents = chest_contents(x);
    jo_set(x, chests, "contents", contents);

    /* ---- the scene objects (extract_npcs.py says what each is): by
     * name an ObjID_ constant, by number one with none */
    static const struct {
        const char *name;
        int         id;         /* -1: ObjID_<name> */
    } SCENE[] = {
        { "Darum", -1 },            { "Neifirst", -1 },
        { "RandomExplosion", -1 },  { "Teim", -1 },
        { "Burst", 0x2B },          { "DeadBody", -1 },
        { "JetScooter", -1 },       { "OpenableDoor", -1 },
        { "DestroyableDoor", -1 },  { "Pump", 0x3D },
        { "MonitorExplosion", 7 },  { "Spaceship", -1 },
        { "Esper", 0x2E },          { "MovingEsper", 0x30 },
        { "MansionB1_23", 0x23 },   { "MansionB1_24", 0x24 },
        { "MansionB1_25", 0x25 },   { "MansionB1_26", 0x26 },
        { "MansionB1_27", 0x27 },   { "MansionDoor", 0x31 },
        { "MansionDoorOpen", 0x33 }, { "MotherBrainImage", 0x2C },
        { "EnergyArc", 0x45 },      { "EarthmanLeader", 0x28 },
        { "EndingUfo", 8 },
    };
    JOut *scene = jo_arr(x);
    for (size_t k = 0; k < sizeof SCENE / sizeof SCENE[0]; k++) {
        long long id = (SCENE[k].id >= 0) ? SCENE[k].id
                                          : obj_value(x, SCENE[k].name);
        jo_add(scene, parse_scene(x, id, SCENE[k].name));
    }

    char *data = ex_path(x, a->out, "data");
    ex_mkdirs(x, data);
    JOut *ls = jo_obj(x);
    jo_set(x, ls, "levels", sprites);
    jo_set(x, ls, "art_lists", art_lists);
    ex_write_text(x, ex_path(x, data, "level_sprites.json"),
                  jo_dump(x, ls, 1, false));
    ex_write_text(x, ex_path(x, data, "npc_types.json"),
                  jo_dump(x, types, 1, false));
    ex_write_text(x, ex_path(x, data, "people_dialogue.json"),
                  jo_dump(x, dialogue, 1, false));
    ex_write_text(x, ex_path(x, data, "chests.json"),
                  jo_dump(x, chests, 1, false));
    ex_write_text(x, ex_path(x, data, "scene_objects.json"),
                  jo_dump(x, scene, 1, false));
    ex_say(x, "%lld objects in %d levels, %d types, %d art blocks, %d chests "
              "-> %s", objects, jo_len(sprites), jo_len(types), nwanted,
           jo_len(contents), a->out);
}
