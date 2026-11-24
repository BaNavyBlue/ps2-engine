/* (build 188) tools/extract_flag_names.py in C: a name for each of the
 * save's flag bytes, $C700-$C7FF (assets/data/flag_names.json). A byte's
 * name comes, in order, from the hand-checked table below (each entry
 * read in ps2.asm; extract_flag_names.py says how), the chest or event
 * object the levels place with that index, or the first routine that
 * names the address -- found by decoding every label's block as 68000
 * code (ex68k.c): a block is code when it decodes to the next label. */

#include "ex68k.h"
#include "excommon.h"

#include <ctype.h>
#include <string.h>

void ex_step_flag_names(Extract *x, const ExArgs *a);

typedef struct {
    int         addr;
    const char *name;
    const char *note;
} Manual;

/* extract_flag_names.py's MANUAL */
static const Manual MANUAL[] = {
    { 0xC704, "teleport_town_1",
      "loc_10F2A: the towns the teleport stations list, $C704-$C709, a level id each" },
    { 0xC705, "teleport_town_2",
      "loc_10F2A" },
    { 0xC706, "teleport_town_3",
      "loc_10F2A" },
    { 0xC707, "teleport_town_4",
      "loc_10F2A" },
    { 0xC708, "teleport_town_5",
      "loc_10F2A" },
    { 0xC709, "teleport_town_6",
      "loc_10F2A" },
    { 0xC70A, "dezolis_station_1",
      "loc_10F2A with $F764 set: the Dezolis stations, $C70A-$C70C" },
    { 0xC70B, "dezolis_station_2",
      "loc_10F2A" },
    { 0xC70C, "dezolis_station_3",
      "loc_10F2A" },
    { 0xC716, "jet_scooter_owned",
      "ObjJetSctr_Init and LoadSpritesInLevel test it (2: a second state); Ustvestia_MusicPointers gates Secret Ways and Mystery on it" },
    { 0xC721, "shure_f3_body_searched",
      "ObjType_Events $11 (loc_DBB2): 2 once the key and the Letter are taken" },
    { 0xC727, "teim_given_the_letter",
      "ObjType_Events $17 (loc_DC64) sets it; Obj_Teim's init removes her once set" },
    { 0xC736, "governor_errand_step",
      "Building_CentralTowerGovernor: 0 before the Recorder is handed in, 1 after the first debrief" },
    { 0xC737, "nei_gone",
      "the library's news and the battle music read it (after Climatrol)" },
    { 0xC78E, "chest_esper_mansion_f1_13_15_first",
      "LoadSpritesInLevel's tail (level $11): the mansion's two chests share one slot; while this is 0 the chest there is chest $0E, the Prism. Building_EsperMansion clears it (loc_CF0A) as Lutz wakes. A new game starts it at 1" },
    { 0xC78F, "chest_esper_mansion_f1_nei_sword_first",
      "LoadSpritesInLevel's tail (level $11): while this is 0 the chest there is $0F, the Nei Sword (TreasureChestContentArray). Building_EsperMansion clears it (loc_CF38) when Lutz sees all eight Nei items. A new game starts it at 1" },
    { 0xC745, "pandora_box_opened",
      "Noah's box (ObjType_Events $35, loc_E0BE) sets it before Dark Force; LoadSpritesInLevel stops placing the box once set. The disassembly's SkipDarumTeimEvent also writes it" },
    { 0xC747, "mother_brain_fought",
      "set by the confrontation (ObjType_Events $37, loc_E1B6) as her battle starts; the approach (loc_70D6) no longer fires and LoadLevelData's tail (loc_92EE) puts block $28 on NoahF1 at $1B0" },
    { 0xC73F, "central_tower_roof_open",
      "the tower's options list ROOF once set; Ustvestia_MusicPointers gates five songs on it" },
    { 0xC711, "scene_camera",
      "event_flags' low byte: loc_809C's camera case" },
    { 0xC712, "piata_tower_unlocked",
      "set at the Governor's first debrief (loc_CA58); Piata's tower door stays shut until then (loc_6ED6)" },
    { 0xC715, "darum_scene_done",
      "set as the Darum scene ends (loc_EAB0); Darum's init (loc_4E2A) keeps him away after" },
    { 0xC722, "recorder_found",
      "event RECORDER's own byte; the library's news reads it (loc_C406)" },
    { 0xC735, "climatrol_destroyed",
      "set by the one-shot CLIMATROL_EXPLODES (loc_7F44); Motavia's map patch (loc_9258) and loc_585C read it" },
    { 0xC73E, "gaira_off_orbit",
      "event GAIRA_EXPLOSION's own byte (loc_DFD0); the ring (loc_64A6) and the drifting scroll read it" },
    { 0xC742, "door_espers_let_rolf_in",
      "event DOOR_ESPERS (loc_E05A) with Rolf alive; the door Espers step aside (loc_532C)" },
    { 0xC743, "esper_b1_talks",
      "event ESPER_B1 (loc_E07A) counts the talks; Building_EsperMansion sets 3 (loc_4B40 and the Espers read it)" },
    { 0xC744, "lutz_saw_nei_items",
      "set when Lutz sees all eight Nei items (loc_CF38): the Nei Sword" },
    { 0xC713, "shure_bare_bodies_unused",
      "event DEAD_BODY_BARE ($03, loc_DA8C: $170B), Shure F3 57,33 and 82,37; nothing reads or writes it" },
    { 0xC714, "uzo_fake_maruera_trees_unused",
      "event FAKE_MARUERA_TREE ($04, loc_DA98: $170F), six on Uzo; nothing reads or writes it" },
    { 0xC717, "tower_keyboard_unused",
      "event KEYBOARD ($07, loc_DAD4: $1711, YES back to the saved place), Control Tower F1 48,26; nothing reads or writes it" },
    { 0xC718, "skure_newspaper_1_opened",
      "event SKURE_NEWSPAPER_1 ($08, loc_DB22), Skure B1 47,45: a Motavian chest object whose loc_5656 shows it open while this is set (bits 0-6); nothing sets it, so it stays shut" },
    { 0xC719, "skure_newspaper_2_opened",
      "event SKURE_NEWSPAPER_2 ($09, loc_DB30), Skure B1 25,49; as $C718" },
    { 0xC71A, "skure_newspaper_3_opened",
      "event SKURE_NEWSPAPER_3 ($0A, loc_DB3E), Skure B1 25,69; as $C718" },
    { 0xC71B, "skure_newspaper_4_opened",
      "event SKURE_NEWSPAPER_4 ($0B, loc_DB4C), Skure B1 65,73; as $C718" },
    { 0xC71C, "skure_newspaper_5_opened",
      "event SKURE_NEWSPAPER_5 ($0C, loc_DB5A), Skure B2 43,23; as $C718" },
    { 0xC71D, "skure_newspaper_6_opened",
      "event SKURE_NEWSPAPER_6 ($0D, loc_DB68), Skure B2 39,23; as $C718" },
    { 0xC71E, "skure_newspaper_7_opened",
      "event SKURE_NEWSPAPER_7 ($0E, loc_DB76), Skure B2 41,27; as $C718" },
    { 0xC71F, "underwater_passage_water_unused",
      "event WATER_COLOUR ($0F, loc_DB84: $1720), Motavia 159,12-13; the Maruera gum tests the event's number (loc_9C12), not its byte; nothing reads or writes it" },
    { 0xC720, "maruera_leaf_taken",
      "event MARUERA_TREE ($10, loc_DB90), Uzo 141,17: loc_E84A adds 1 as it hands over the leaf; set, the tree answers $1708" },
    { 0xC723, "tower_green_card_taken",
      "event TOWER_GREEN_CARD ($13, loc_DC1E), Control Tower F1 41,6: loc_E84A adds 1 as it hands over the card; set, the box answers $1708" },
    { 0xC724, "tower_blue_card_taken",
      "event TOWER_BLUE_CARD ($14, loc_DC1E), Control Tower F1 45,6; as $C723" },
    { 0xC725, "tower_yellow_card_taken",
      "event TOWER_YELLOW_CARD ($15, loc_DC1E), Control Tower F1 49,6; as $C723" },
    { 0xC726, "tower_red_card_taken",
      "event TOWER_RED_CARD ($16, loc_DC1E), Control Tower F1 53,6; as $C723" },
    { 0xC728, "nido_door_blown",
      "event DYNAMITE_DOOR_1 ($18), Nido ground floor 53,45: ItemAction_Dynamite's `bset #0`; the door there (Obj_DestroyableDoor) reads it" },
    { 0xC729, "biosystems_lab_door_blown",
      "event DYNAMITE_DOOR_2 ($19), Biosystems Lab ground floor 47,73; as $C728" },
    { 0xC72A, "biosystems_lab_f2_door_blown",
      "event DYNAMITE_DOOR_3 ($1A), Biosystems Lab F2 47,47; as $C728" },
    { 0xC72B, "tube_shutter_opened",
      "event STRONG_SHUTTER ($1B), the tube's locked door 9,15: ItemAction_KeyTube's `bset #0`; the door there (Obj_OpenableDoor) reads it" },
    { 0xC73B, "mount_uzo_unused",
      "event MOUNT_UZO ($2B, loc_DF24: $171F), Motavia 173,96; nothing reads or writes it" },
    { 0xC72C, "green_dam_door",
      "the green card on the Green Dam's door (event $1C, ground floor 95,25; loc_9D3E)" },
    { 0xC72D, "green_dam_panel",
      "the green card on the Green Dam's panel (event $1D, F1 113,28; loc_9D70); with Climatrol destroyed, patches Motavia's map (loc_92BC)" },
    { 0xC72E, "blue_dam_door",
      "the blue card on the Blue Dam's door (event $1E, ground floor 53,27)" },
    { 0xC72F, "blue_dam_panel",
      "the blue card on the Blue Dam's panel (event $1F, F3 21,32); with Climatrol destroyed, patches Motavia's map (loc_9258)" },
    { 0xC730, "yellow_dam_door",
      "the yellow card on the Yellow Dam's door (event $20, ground floor 17,27)" },
    { 0xC731, "yellow_dam_panel",
      "the yellow card on the Yellow Dam's panel (event $21, F2 51,24); with Climatrol destroyed, patches Motavia's map (loc_92AE)" },
    { 0xC732, "red_dam_door",
      "the red card on the Red Dam's door (event $22, F1 47,17)" },
    { 0xC733, "red_dam_panel",
      "the red card on the Red Dam's panel (event $23, F2 45,20); with Climatrol destroyed, patches Motavia's map (loc_92A0)" },
};

#define MANUAL_COUNT ((int)(sizeof MANUAL / sizeof MANUAL[0]))

static const Manual *manual(int addr)
{
    for (int i = 0; i < MANUAL_COUNT; i++) {
        if (MANUAL[i].addr == addr) {
            return &MANUAL[i];
        }
    }
    return NULL;
}

/* snake(): an underscore between a lower-case letter or digit and a
 * capital, lower case, every run of anything else one underscore, none
 * at either end. */
static char *snake(Extract *x, const char *s)
{
    ExStr a;
    ex_str_init(x, &a);
    for (size_t i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (i > 0 && (islower((unsigned char)s[i - 1])
                      || isdigit((unsigned char)s[i - 1]))
            && isupper((unsigned char)c)) {
            ex_str_addc(&a, '_');
        }
        ex_str_addc(&a, (char)tolower((unsigned char)c));
    }
    ExStr b;
    ex_str_init(x, &b);
    bool gap = false;
    for (size_t i = 0; i < a.len; i++) {
        char c = a.buf[i];
        bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (keep) {
            if (gap && b.len > 0) {
                ex_str_addc(&b, '_');
            }
            gap = false;
            ex_str_addc(&b, c);
        } else {
            gap = true;
        }
    }
    return (b.buf != NULL) ? b.buf : ex_strdup(x, "");
}

/* A list of strings for each flag byte. */
typedef struct {
    const char **at;
    int          count;
    int          cap;
} Names;

static void names_add(Extract *x, Names *n, const char *s)
{
    if (n->count == n->cap) {
        int cap = (n->cap > 0) ? n->cap * 2 : 4;
        const char **more = ex_alloc(x, sizeof *more * (size_t)cap);
        if (n->count > 0) {
            memcpy(more, n->at, sizeof *more * (size_t)n->count);
        }
        n->at  = more;
        n->cap = cap;
    }
    n->at[n->count++] = s;
}

static char *joined(Extract *x, const Names *n, int from, int upto,
                    const char *sep)
{
    ExStr s;
    ex_str_init(x, &s);
    for (int i = from; i < n->count && i < upto; i++) {
        ex_str_addf(&s, "%s%s", (i > from) ? sep : "", n->at[i]);
    }
    return (s.buf != NULL) ? s.buf : ex_strdup(x, "");
}

#define MAX_FOUND 4096

/* rom_refs: the labels whose blocks decode as code and name a flag byte
 * by its absolute address, in the ROM's order, a byte at a time. */
static void rom_refs(Extract *x, Names *refs)
{
    long long bases[2] = {
        ex_const(x, "event_flags") & 0xFFFFFF,
        ex_const(x, "treasure_chest_flags") & 0xFFFFFF,
    };
    ExMap *m = &x->map;
    static int found[MAX_FOUND];
    for (int i = 0; i < m->nlabels; i++) {
        long long start = m->labels[i].addr;
        long long end = (i + 1 < m->nlabels) ? (long long)m->labels[i + 1].addr
                                              : (long long)m->size;
        int nfound = 0;
        long long at = start;
        bool code = true;
        while (at < end) {
            ExInsn in;
            const char *why = NULL;
            if (!ex_m68k_try(x, at, &in, &why)) {
                code = false;
                break;
            }
            for (int k = 0; k < in.nops; k++) {
                long long v = in.ops[k].value;
                if (in.ops[k].kind == EX_EA_ABS && v >= 0xFFC700
                    && v < 0xFFC800 && v != bases[0] && v != bases[1]) {
                    if (nfound == MAX_FOUND) {
                        ex_fail(x, "%s: more than %d flag references",
                                m->labels[i].name, MAX_FOUND);
                    }
                    found[nfound++] = 0xC700 + (int)(v & 0xFF);
                }
            }
            at += in.length;
        }
        if (!code || at != end) {
            continue;
        }
        for (int k = 0; k < nfound; k++) {
            names_add(x, &refs[found[k] - 0xC700], m->labels[i].name);
        }
    }
}

/* level id -> its LevelID_ constant's name, a value two share left out */
static const char *level_name(Extract *x, long long value)
{
    ExMap *m = &x->map;
    const char *name = NULL;
    int count = 0;
    for (int i = 0; i < m->nconsts; i++) {
        const ExConst *c = &m->consts[m->const_by_name[i].rank];
        if (ex_starts(c->name, "LevelID_") && c->value == value) {
            if (count == 0) {
                name = c->name + 8;
            }
            count++;
        }
    }
    return (count == 1) ? name : NULL;
}

static long long num(Extract *x, const JsonValue *o, const char *key)
{
    const JsonValue *v = json_get(o, key);
    if (v == NULL || v->type != JSON_NUMBER) {
        ex_fail(x, "an entry with no \"%s\"", key);
    }
    return (long long)v->number;
}

static const JsonValue *content_of(const JsonValue *contents, long long index)
{
    for (const JsonValue *c = contents->child; c != NULL; c = c->next) {
        const JsonValue *i = json_get(c, "index");
        if (i != NULL && i->type == JSON_NUMBER
            && (long long)i->number == index) {
            return c;
        }
    }
    return NULL;
}

void ex_step_flag_names(Extract *x, const ExArgs *a)
{
    const char *assets = (a->assets != NULL) ? a->assets : "assets";
    Names refs[256];
    Names placed[256];
    memset(refs, 0, sizeof refs);
    memset(placed, 0, sizeof placed);
    rom_refs(x, refs);
    const JsonValue *sprites = ex_read_json(x, ex_path(x, assets,
                                                       "data/level_sprites.json"));
    const JsonValue *chests = ex_read_json(x, ex_path(x, assets,
                                                      "data/chests.json"));
    const JsonValue *contents = json_get(chests, "contents");
    const JsonValue *levels = json_get(sprites, "levels");
    if (contents == NULL || levels == NULL) {
        ex_fail(x, "level_sprites.json or chests.json is not extract_npcs'");
    }

    for (const JsonValue *lv = levels->child; lv != NULL; lv = lv->next) {
        long long level = num(x, lv, "level");
        const char *lname = level_name(x, level);
        if (lname == NULL) {
            const JsonValue *n = json_get(lv, "name");
            lname = (n != NULL && n->type == JSON_STRING)
                  ? n->string : ex_fmt(x, "level_%02llx", level);
        }
        lname = snake(x, lname);
        const JsonValue *objects = json_get(lv, "objects");
        for (const JsonValue *o = (objects != NULL) ? objects->child : NULL;
             o != NULL; o = o->next) {
            long long kind  = num(x, o, "interaction");
            long long index = num(x, o, "index");
            char *where = ex_fmt(x, "%s_%lld_%lld", lname,
                                 ex_floordiv(num(x, o, "x"), 16),
                                 ex_floordiv(num(x, o, "y"), 16));
            if (kind == 2 && index > 0 && index < 0x80) {
                const JsonValue *c = content_of(contents, index);
                const JsonValue *item = json_get(c, "item");
                const JsonValue *meseta = json_get(c, "meseta");
                const char *what = "nothing";
                if (c != NULL && item != NULL && item->type == JSON_STRING) {
                    what = snake(x, item->string);
                } else if (c != NULL && meseta != NULL) {
                    what = ex_fmt(x, "%lld_meseta", (long long)meseta->number);
                }
                names_add(x, &placed[0x80 + index],
                          ex_fmt(x, "chest_%s_%s", where, what));
            } else if (kind == 0 && index > 0 && index < 0x70) {
                names_add(x, &placed[0x10 + index],
                          ex_fmt(x, "event%02llx_%s", index, where));
            }
        }
    }

    JOut *out = jo_arr(x);
    int named = 0;
    for (int addr = 0xC700; addr < 0xC800; addr++) {
        int b = addr - 0xC700;
        const char *kind = (addr < 0xC710) ? "misc"
                         : (addr < 0xC780) ? "event" : "chest";
        Names notes = { NULL, 0, 0 };
        bool from_placed = false;
        const char *name = NULL;
        const Manual *hand = manual(addr);
        if (hand != NULL) {
            name = hand->name;
            names_add(x, &notes, hand->note);
        } else if (strcmp(kind, "chest") == 0 && placed[b].count > 0) {
            name = placed[b].at[0];
            from_placed = true;
        } else if (refs[b].count > 0) {
            const char *lab = refs[b].at[0];
            name = ex_fmt(x, "flag_%s", ex_starts(lab, "loc_")
                                        ? ex_lower(x, lab) : snake(x, lab));
        } else if (placed[b].count > 0) {
            name = placed[b].at[0];
            from_placed = true;
        } else {
            name = "unused";
        }
        int others_from = from_placed ? 1 : 0;
        if (strcmp(kind, "event") == 0 && placed[b].count > 0) {
            names_add(x, &notes, ex_fmt(x, "event objects with this index "
                                           "(their routine may not read the "
                                           "flag): %s",
                                        joined(x, &placed[b], 0, 6, ", ")));
        } else if (placed[b].count > others_from) {
            names_add(x, &notes, ex_fmt(x, "also placed: %s",
                                        joined(x, &placed[b], others_from,
                                               placed[b].count, ", ")));
        }
        Names seen = { NULL, 0, 0 };
        for (int i = 0; i < refs[b].count; i++) {
            bool dup = false;
            for (int k = 0; k < seen.count; k++) {
                if (strcmp(seen.at[k], refs[b].at[i]) == 0) {
                    dup = true;
                }
            }
            if (!dup) {
                names_add(x, &seen, refs[b].at[i]);
            }
        }
        if (seen.count > 0) {
            names_add(x, &notes, ex_fmt(x, "read/written in: %s",
                                        joined(x, &seen, 0, 6, "; ")));
        }
        JOut *f = jo_obj(x);
        jo_sets(x, f, "addr", ex_fmt(x, "$%04X", addr));
        jo_sets(x, f, "kind", kind);
        jo_sets(x, f, "name", name);
        jo_sets(x, f, "notes", joined(x, &notes, 0, notes.count, " | "));
        jo_add(out, f);
        if (strcmp(name, "unused") != 0) {
            named++;
        }
    }

    ex_mkdirs(x, a->out);
    JOut *doc = jo_obj(x);
    /* "ps2.asm references": the routines go by the disassembly's labels
     * (the map's), though read from the ROM's code */
    jo_sets(x, doc, "source",
            "ps2.asm references, level_sprites.json, chests.json");
    jo_set(x, doc, "flags", out);
    ex_write_text(x, ex_path(x, a->out, "flag_names.json"),
                  ex_fmt(x, "%s\n", jo_dump(x, doc, 1, false)));
    ex_say(x, "flag_names.json: %d of 256 named", named);
}
