#include "gamedata.h"

#include "lang.h"
#include "../core/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#else
#include <sys/stat.h>
#define make_dir(path) mkdir(path, 0755)
#endif

/* Where the slots live: "saves" beside the program, unless a tool points
 * it elsewhere (gamedata_set_save_dir). */
#define SAVE_DIR_DEFAULT "saves"
#define SAVE_PATH_MAX    256

static char g_save_dir[SAVE_PATH_MAX - 32] = SAVE_DIR_DEFAULT;
static char g_save_home[SAVE_PATH_MAX - 32] = SAVE_DIR_DEFAULT;  /* (123) */
#define SAVE_FORMAT  2        /* 2: the flag bytes by name (1: hex rows) */
#define ITEM_MAX     128
#define LEVEL_ID_PASEO 4

static const char *const CHAR_ID[GD_CHARS] = {
    "Rolf", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"
};

static const char *const EQUIP_KEY[EQUIP_COUNT] = {
    "head", "right_hand", "left_hand", "body", "legs"
};

static const char *const PLANET[2] = { "Motavia", "Dezolis" };

/* ------------------------------------------------------------------ */
/* Tables from assets/data                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    char key[24];
    char name[12];
    u16  price;
    u8   action_flags, equip_mask;
    u8   attack, defense;
    u8   agility;               /* footwear only */
} ItemInfo;

static ItemInfo g_item[ITEM_MAX];
static int      g_item_count;
static CharData g_initial[GD_CHARS];
/* (build 163) The cartridge's default names, as characters.json has them:
 * what gamedata_language_names knows a default name by. */
static char     g_initial_name[GD_CHARS][GD_NAME_MAX + 1];

/* CharExperiencePtrs: the experience each level starts at, absolute,
 * one row per level from level 1. */
#define LEVEL_ROWS 50
static u32      g_exp[GD_CHARS][LEVEL_ROWS];
static LevelRow g_level_row[GD_CHARS][LEVEL_ROWS];

/* TechniqueData and CharAvailableTechniques, from techniques.json. */
#define TECH_MAX   64
#define TECH_MUSIK 0x33
static char g_tech_name[TECH_MAX][8];
static char g_tech_key[TECH_MAX][16];   /* (build 163) for names.json */
static u8   g_tech_tp[TECH_MAX];       /* TechniqueData+5 */
static u8   g_tech_flags[TECH_MAX];    /* +6: target kind, type, side */
static u8   g_tech_rate[TECH_MAX];     /* +7: damage / healing rate  */
static u8   g_tech_avail[GD_CHARS][32];     /* map 16, then battle 16 */

static int item_by_key(const char *key)
{
    if (key != NULL) {
        for (int i = 0; i < g_item_count; i++) {
            if (strcmp(g_item[i].key, key) == 0) {
                return i;
            }
        }
        fprintf(stderr, "gamedata: unknown item \"%s\"\n", key);
    }
    return 0;
}

const char *gamedata_item_key(int id)
{
    id &= 0x7F;
    return (id < g_item_count) ? g_item[id].key : "None";
}

/* The script charset (ps2.asm:25907), names only need this much. */
static u8 code_from_ascii(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (u8)(1 + (ch - '0'));
    }
    if (ch >= 'A' && ch <= 'Z') {
        return (u8)(11 + (ch - 'A'));
    }
    if (ch >= 'a' && ch <= 'z') {
        return (u8)(37 + (ch - 'a'));
    }
    return 0;
}

static char ascii_from_code(u8 code)
{
    if (code >= 1 && code <= 10) {
        return (char)('0' + (code - 1));
    }
    if (code >= 11 && code <= 36) {
        return (char)('A' + (code - 11));
    }
    if (code >= 37 && code <= 62) {
        return (char)('a' + (code - 37));
    }
    return ' ';
}

/* A name shorter than the field ends in $C4 and is zero-filled after,
 * as the nametxt macro lays it out. (build 163) `size` the field's:
 * GD_NAME_MAX for a character's, GD_NAME_LEN for the slot label's. */
static void name_from_ascii(u8 *out, size_t size, const char *text)
{
    size_t len = strlen(text);
    memset(out, 0, size);

    for (size_t i = 0; i < size && i < len; i++) {
        out[i] = code_from_ascii(text[i]);
    }
    if (len < size) {
        out[len] = 0xC4;
    }
}

static void name_to_ascii(char *out, const u8 *name, int size)
{
    int n = 0;
    while (n < size && name[n] != 0xC4) {
        out[n] = ascii_from_code(name[n]);
        n++;
    }
    out[n] = '\0';
}

static void load_flag_names(void);

/* (build 165) A name as text and back in the language's letters: a
 * language whose charset is its own whole (the Japanese cartridge's)
 * through lang.h; every other by the script charset's ASCII, as ever. */
static void name_from_text(u8 *out, size_t size, const char *text)
{
    if (!lang_charset_full()) {
        name_from_ascii(out, size, text);
        return;
    }
    u8  codes[GD_NAME_MAX];
    int n = lang_codes(text, codes, (int)size);
    memset(out, 0, size);
    memcpy(out, codes, (size_t)n);
    if ((size_t)n < size) {
        out[n] = 0xC4;
    }
}

static void name_to_text(char *out, size_t out_size, const u8 *name,
                         int size)
{
    if (!lang_charset_full()) {
        name_to_ascii(out, name, size);
        return;
    }
    lang_text_of(name, size, out, out_size);
}

/* The levels by the names levels.json gives them ("Paseo",
 * "MotaviaOutside", "ShureGroundF"): how a save names a map. */
#define LEVEL_NAME_MAX  128
static char g_level_name[LEVEL_NAME_MAX][32];
static int  g_level_count;

static void load_level_names(void)
{
    JsonValue *root = json_parse_file("assets/levels/levels.json");
    const JsonValue *levels = json_get(root, "levels");
    g_level_count = 0;
    for (int i = 0; i < json_length(levels) && i < LEVEL_NAME_MAX; i++) {
        const JsonValue *lv = json_at(levels, i);
        int index = (int)json_int(lv, "index", i);
        if (index >= 0 && index < LEVEL_NAME_MAX) {
            snprintf(g_level_name[index], sizeof g_level_name[index], "%s",
                     json_str(lv, "name", ""));
            if (index + 1 > g_level_count) {
                g_level_count = index + 1;
            }
        }
    }
    json_free(root);
}

const char *gamedata_level_name(int id)
{
    if (id < 0 || id >= g_level_count || g_level_name[id][0] == '\0') {
        return NULL;
    }
    return g_level_name[id];
}

static bool same_name(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) {
            return false;
        }
    }
    return *a == *b;
}

int gamedata_level_id(const char *name)
{
    for (int i = 0; name != NULL && i < g_level_count; i++) {
        if (g_level_name[i][0] != '\0' && same_name(g_level_name[i], name)) {
            return i;
        }
    }
    return -1;
}

bool gamedata_load_tables(void)
{
    load_flag_names();
    load_level_names();
    JsonValue *items = json_parse_file("assets/data/items.json");
    JsonValue *chars = json_parse_file("assets/data/characters.json");
    JsonValue *agil  = json_parse_file("assets/data/agility_bonus.json");
    bool ok = (items != NULL && chars != NULL && agil != NULL);

    if (ok) {
        g_item_count = json_length(items);
        if (g_item_count > ITEM_MAX) {
            g_item_count = ITEM_MAX;
        }
        for (int i = 0; i < g_item_count; i++) {
            const JsonValue *it = json_at(items, i);
            snprintf(g_item[i].key, sizeof g_item[i].key, "%s",
                     json_str(it, "key", "None"));
            snprintf(g_item[i].name, sizeof g_item[i].name, "%s",
                     json_str(it, "name", ""));
            g_item[i].price        = (u16)json_int(it, "price", 0);
            g_item[i].action_flags = (u8)json_int(it, "action_flags", 0);
            g_item[i].equip_mask   = (u8)json_int(it, "equip_mask", 0);
            g_item[i].attack  = (u8)json_int(it, "attack", 0);
            g_item[i].defense = (u8)json_int(it, "defense", 0);
            g_item[i].agility = (u8)json_int(agil, g_item[i].key, 0);
        }

        memset(g_initial, 0, sizeof g_initial);
        for (int c = 0; c < GD_CHARS && c < json_length(chars); c++) {
            const JsonValue *src = json_at(chars, c);
            const JsonValue *lv  = json_at(json_get(src, "levels"), 0);
            const JsonValue *eq  = json_get(src, "equipment");
            const JsonValue *inv = json_get(src, "items");
            CharData *d = &g_initial[c];

            name_from_ascii(d->name, GD_NAME_MAX, json_str(src, "name", ""));
            snprintf(g_initial_name[c], sizeof g_initial_name[c], "%s",
                     json_str(src, "name", ""));

            const JsonValue *levels = json_get(src, "levels");
            for (int r = 0; r < LEVEL_ROWS; r++) {
                const JsonValue *row = json_at(levels, r);
                LevelRow *lr = &g_level_row[c][r];
                g_exp[c][r]    = (u32)json_int(row, "exp", 0);
                lr->level      = (u8)json_int(row, "level", r + 1);
                lr->hp         = (u8)json_int(row, "hp", 0);
                lr->tp         = (u8)json_int(row, "tp", 0);
                lr->strength   = (u8)json_int(row, "strength", 0);
                lr->mental     = (u8)json_int(row, "mental", 0);
                lr->agility    = (u8)json_int(row, "agility", 0);
                lr->luck       = (u8)json_int(row, "luck", 0);
                lr->dexterity  = (u8)json_int(row, "dexterity", 0);
                lr->attack     = (u8)json_int(row, "attack", 0);
                lr->defense    = (u8)json_int(row, "defense", 0);
                lr->techs_a    = (u8)json_int(row, "techs_a", 0);
                lr->techs_b    = (u8)json_int(row, "techs_b", 0);
            }

            /* SetCharInitialStats: the first row of the level table is
             * absolute, and ATTACK fills both the base and the total. */
            d->level       = (u16)json_int(lv, "level", 1);
            d->hp          = (u16)json_int(lv, "hp", 0);
            d->max_hp      = d->hp;
            d->tp          = (u16)json_int(lv, "tp", 0);
            d->max_tp      = d->tp;
            d->strength    = (u16)json_int(lv, "strength", 0);
            d->mental      = (u16)json_int(lv, "mental", 0);
            d->agility     = (u16)json_int(lv, "agility", 0);
            d->luck        = (u16)json_int(lv, "luck", 0);
            d->dexterity   = (u16)json_int(lv, "dexterity", 0);
            d->attack_base = (u16)json_int(lv, "attack", 0);
            d->attack      = d->attack_base;
            d->defense     = (u16)json_int(lv, "defense", 0);
            d->techs_a     = (u8)json_int(lv, "techs_a", 0);
            d->techs_b     = (u8)json_int(lv, "techs_b", 0);

            for (int s = 0; s < EQUIP_COUNT; s++) {
                d->equip[s] = (u8)item_by_key(json_str(eq, EQUIP_KEY[s],
                                                       "None"));
            }
            d->item_count = (u8)json_length(inv);
            for (int i = 0; i < d->item_count && i < GD_ITEM_SLOTS; i++) {
                const JsonValue *e = json_at(inv, i);
                int id = item_by_key(json_str(e, "item", "None"));
                d->items[i] = (u8)(id | (json_bool(e, "equipped", false)
                                         ? 0x80 : 0));
            }
        }
    }

    json_free(items);
    json_free(chars);
    json_free(agil);

    JsonValue *techs = json_parse_file("assets/data/techniques.json");
    const JsonValue *list = json_get(techs, "techniques");
    for (int i = 0; i < TECH_MAX && i < json_length(list); i++) {
        snprintf(g_tech_name[i], sizeof g_tech_name[i], "%s",
                 json_str(json_at(list, i), "name", ""));
        snprintf(g_tech_key[i], sizeof g_tech_key[i], "%s",
                 json_str(json_at(list, i), "key", ""));
        g_tech_tp[i]    = (u8)json_int(json_at(list, i), "tp", 0);
        g_tech_flags[i] = (u8)json_int(json_at(list, i), "flags", 0);
        g_tech_rate[i]  = (u8)json_int(json_at(list, i), "rate", 0);
    }
    const JsonValue *avail = json_get(techs, "available");
    for (int c = 0; c < GD_CHARS; c++) {
        const JsonValue *who = json_get(avail, CHAR_ID[c]);
        for (int half = 0; half < 2; half++) {
            const JsonValue *keys = json_get(who, half ? "battle" : "map");
            for (int i = 0; i < 16; i++) {
                const JsonValue *k = json_at(keys, i);
                const char *key = (k != NULL && k->type == JSON_STRING)
                                ? k->string : "None";
                int id = 0;
                for (int t = 0; t < json_length(list); t++) {
                    if (strcmp(json_str(json_at(list, t), "key", ""), key)
                        == 0) {
                        id = t;
                        break;
                    }
                }
                g_tech_avail[c][half * 16 + i] = (u8)id;
            }
        }
    }
    if (techs == NULL) {
        ok = false;
    }
    json_free(techs);
    return ok;
}

/* ------------------------------------------------------------------ */
/* New game                                                           */
/* ------------------------------------------------------------------ */

void gamedata_equip_bonuses_add(GameData *gd, int c)
{
    CharData *d = &gd->chr[c % GD_CHARS];
    d->attack = d->attack_base;

    /* Every slot adds, so a two-handed weapon listed in both hands
     * counts twice -- as it does in the original. */
    for (int s = 0; s < EQUIP_COUNT; s++) {
        int id = d->equip[s] & 0x7F;
        if (id != 0 && id < g_item_count) {
            d->attack  = (u16)(d->attack + g_item[id].attack);
            d->defense = (u16)(d->defense + g_item[id].defense);
        }
    }

    int legs = d->equip[EQUIP_LEGS] & 0x7F;
    if (legs < g_item_count) {
        d->agility = (u16)(d->agility + g_item[legs].agility);
    }
}

/* loc_ADBE does not touch attack: loc_AD4C reloads it from the base. */
void gamedata_equip_bonuses_remove(GameData *gd, int c)
{
    CharData *d = &gd->chr[c % GD_CHARS];

    for (int s = 0; s < EQUIP_COUNT; s++) {
        int id = d->equip[s] & 0x7F;
        if (id != 0 && id < g_item_count) {
            d->attack  = (u16)(d->attack - g_item[id].attack);
            d->defense = (u16)(d->defense - g_item[id].defense);
        }
    }

    int legs = d->equip[EQUIP_LEGS] & 0x7F;
    if (legs < g_item_count) {
        d->agility = (u16)(d->agility - g_item[legs].agility);
    }
}

void gamedata_apply_equipment(GameData *gd)
{
    for (int c = 0; c < GD_CHARS; c++) {
        gamedata_equip_bonuses_add(gd, c);
    }
}

u8 *gamedata_bag(GameData *gd, int c, u8 **count)
{
    if (c == GD_KEEPER) {
        *count = &gd->c600_raw[GD_KEEPER_COUNT];
        return &gd->c600_raw[GD_KEEPER_ITEMS];
    }
    CharData *d = &gd->chr[c % GD_CHARS];
    *count = &d->item_count;
    return d->items;
}

const char *gamedata_tech_name(int id)
{
    return g_tech_name[(id & 0x3F) % TECH_MAX];
}

const char *gamedata_tech_text(int id)
{
    int i = (id & 0x3F) % TECH_MAX;
    return (g_tech_name[i][0] != '\0') ? lang_tech_name(g_tech_key[i]) : NULL;
}

u8 gamedata_tech_tp(int id)
{
    return g_tech_tp[(id & 0x3F) % TECH_MAX];
}

u8 gamedata_tech_flags(int id)
{
    return g_tech_flags[(id & 0x3F) % TECH_MAX];
}

u8 gamedata_tech_rate(int id)
{
    return g_tech_rate[(id & 0x3F) % TECH_MAX];
}

/* One row of CharExperiencePtrs past the first: the gains a level
 * brings, in the order the level-up code walks them. */
bool gamedata_level_row(int c, int row, LevelRow *out)
{
    if (row < 0 || row >= LEVEL_ROWS || c < 0 || c >= GD_CHARS) {
        return false;
    }
    *out = g_level_row[c][row];
    return true;
}

/* loc_112FC, once for the map half (count at +$26) and once for the
 * battle half (+$25). */
void gamedata_known_techs(const GameData *gd, int c, u8 out[32])
{
    const CharData *d = &gd->chr[c % GD_CHARS];
    const u8 count_byte[2] = { d->techs_b, d->techs_a };

    memset(out, 0, 32);
    for (int half = 0; half < 2; half++) {
        int n = count_byte[half] & 0x1F;
        if (n > 16) {
            n = 16;
        }
        memcpy(&out[half * 16], &g_tech_avail[c % GD_CHARS][half * 16],
               (size_t)n);
        if (n > 0 && (count_byte[half] & 0x80)) {
            out[half * 16 + n - 1] = TECH_MUSIK;
        }
    }
}

u32 gamedata_level_exp(int c, int row)
{
    if (row < 0 || row >= LEVEL_ROWS) {
        return 0;
    }
    return g_exp[c % GD_CHARS][row];
}

int gamedata_item_id(const char *key)
{
    return item_by_key(key);
}

const char *gamedata_item_name(int id)
{
    id &= 0x7F;
    return (id < g_item_count) ? g_item[id].name : "";
}

const char *gamedata_item_text(int id)
{
    id &= 0x7F;
    return (id > 0 && id < g_item_count) ? lang_item_name(g_item[id].key)
                                         : NULL;
}

u16 gamedata_item_price(int id)
{
    id &= 0x7F;
    return (id < g_item_count) ? g_item[id].price : 0;
}

u8 gamedata_item_attack(int id)
{
    id &= 0x7F;
    return (id < g_item_count) ? g_item[id].attack : 0;
}

u8 gamedata_item_action_flags(int id)
{
    id &= 0x7F;
    return (id < g_item_count) ? g_item[id].action_flags : 0;
}

u8 gamedata_item_equip_mask(int id)
{
    id &= 0x7F;
    return (id < g_item_count) ? g_item[id].equip_mask : 0;
}

void gamedata_new_game(GameData *gd)
{
    memset(gd, 0, sizeof *gd);

    gd->level_index = LEVEL_ID_PASEO;
    gd->level_y     = 0x120;
    gd->level_x     = 0x2D0;
    gd->party_member_join_next = 1;
    gd->money       = 200;

    /* loc_89EE copies the start into both the last save's place (where
     * the Telepipe and Ryuka land) and the way into the last dungeon
     * (where the Escapipe and Hinas go back to): a pipe before the first
     * save or the first dungeon comes back to Rolf's door. */
    gamedata_set_word(gd, GD_LAST_SAVE_MAP, gd->level_index);
    gamedata_set_word(gd, GD_LAST_SAVE_Y,   gd->level_y);
    gamedata_set_word(gd, GD_LAST_SAVE_X,   gd->level_x);
    gamedata_set_word(gd, GD_ENTRANCE_MAP,  gd->level_index);
    gamedata_set_word(gd, GD_ENTRANCE_Y,    gd->level_y);
    gamedata_set_word(gd, GD_ENTRANCE_X,    gd->level_x);

    /* The chests in Shure start locked. */
    gd->c700[0x8E] = 0x01;
    gd->c700[0x8F] = 0x01;
    memset(&gd->c700[0x90], 0x80, 4);

    memcpy(gd->chr, g_initial, sizeof gd->chr);
    gamedata_apply_equipment(gd);
    /* (build 163) The chosen language's default names; (165) from its
     * text, in its own letters. */
    for (int c = 0; c < GD_CHARS; c++) {
        const char *want = lang_char_name(c);
        name_from_text(gd->chr[c].name, GD_NAME_MAX,
                       (want != NULL) ? want : g_initial_name[c]);
    }
}

void gamedata_names_text(const GameData *gd, char text[GD_CHARS][GD_NAME_TEXT],
                         char label[GD_NAME_TEXT])
{
    for (int c = 0; c < GD_CHARS; c++) {
        name_to_text(text[c], GD_NAME_TEXT, gd->chr[c].name, GD_NAME_MAX);
    }
    name_to_text(label, GD_NAME_TEXT, gd->save_name, GD_NAME_MAX);
}

void gamedata_names_set_text(GameData *gd,
                             char text[GD_CHARS][GD_NAME_TEXT],
                             const char *label)
{
    for (int c = 0; c < GD_CHARS; c++) {
        /* A default name of any language is the chosen one's, as text:
         * made codes first, kana would be spaces in the script charset
         * and Latin letters gaps in the cartridge's. */
        const char *t = text[c];
        if (strcmp(t, g_initial_name[c]) == 0 || lang_is_default_name(c, t)) {
            const char *want = lang_char_name(c);
            t = (want != NULL) ? want : g_initial_name[c];
        }
        name_from_text(gd->chr[c].name, GD_NAME_MAX, t);
    }
    name_from_text(gd->save_name, GD_NAME_MAX, label);
}

void gamedata_set_name(u8 name[GD_NAME_MAX], const u8 *entry, int n)
{
    memset(name, 0, GD_NAME_MAX);
    memcpy(name, entry, (size_t)n);
    if (n < GD_NAME_MAX) {
        name[n] = 0xC4;
    }
}

void gamedata_language_names(GameData *gd)
{
    for (int c = 0; c < GD_CHARS; c++) {
        char now[GD_NAME_TEXT];
        name_to_text(now, sizeof now, gd->chr[c].name, GD_NAME_MAX);
        if (strcmp(now, g_initial_name[c]) != 0
            && !lang_is_default_name(c, now)) {
            continue;
        }
        const char *want = lang_char_name(c);
        name_from_text(gd->chr[c].name, GD_NAME_MAX,
                       (want != NULL) ? want : g_initial_name[c]);
    }
}

/* ------------------------------------------------------------------ */
/* The $C600 block                                                    */
/* ------------------------------------------------------------------ */

static void put16(u8 *raw, int off, u16 v)
{
    raw[off]     = (u8)(v >> 8);
    raw[off + 1] = (u8)v;
}

static u16 get16(const u8 *raw, int off)
{
    return (u16)(raw[off] << 8 | raw[off + 1]);
}

u16 gamedata_word(const GameData *gd, int off)
{
    return get16(gd->c600_raw, off);
}

void gamedata_set_word(GameData *gd, int off, u16 v)
{
    put16(gd->c600_raw, off, v);
}

static void pack_c600(const GameData *gd, u8 raw[GD_C600_SIZE])
{
    memcpy(raw, gd->c600_raw, GD_C600_SIZE);

    put16(raw, 0x00, gd->party_members_num);
    put16(raw, 0x04, gd->party_members_joined);
    put16(raw, 0x06, gd->party_member_join_next);
    for (int i = 0; i < GD_PARTY_MAX; i++) {
        put16(raw, 0x08 + i * 2, gd->party_member_id[i]);
    }
    put16(raw, 0x20, (u16)(gd->money >> 16));
    put16(raw, 0x22, (u16)gd->money);
    put16(raw, 0x40, gd->level_index);
    put16(raw, 0x42, gd->level_y);
    put16(raw, 0x44, gd->level_x);
    put16(raw, 0x52, gd->jet_scooter);
    put16(raw, 0x58, gd->planet);
    for (int c = 0; c < GD_CHARS; c++) {
        memcpy(&raw[0x60 + c * GD_NAME_LEN], gd->chr[c].name, GD_NAME_LEN);
    }
    memcpy(&raw[0x80], gd->save_name, GD_NAME_LEN);
    put16(raw, 0x84, gd->save_level);
}

static void unpack_c600(GameData *gd)
{
    const u8 *raw = gd->c600_raw;

    gd->party_members_num      = get16(raw, 0x00);
    gd->party_members_joined   = get16(raw, 0x04);
    gd->party_member_join_next = get16(raw, 0x06);
    for (int i = 0; i < GD_PARTY_MAX; i++) {
        gd->party_member_id[i] = get16(raw, 0x08 + i * 2);
    }
    gd->money       = (u32)get16(raw, 0x20) << 16 | get16(raw, 0x22);
    gd->level_index = get16(raw, 0x40);
    gd->level_y     = get16(raw, 0x42);
    gd->level_x     = get16(raw, 0x44);
    gd->jet_scooter = get16(raw, 0x52);
    gd->planet      = get16(raw, 0x58);
    for (int c = 0; c < GD_CHARS; c++) {
        memcpy(gd->chr[c].name, &raw[0x60 + c * GD_NAME_LEN], GD_NAME_LEN);
        /* (build 163) Four letters are the whole of a name the block
         * holds; a save's "name" gives the rest. */
        gd->chr[c].name[GD_NAME_LEN] = 0xC4;
    }
    memcpy(gd->save_name, &raw[0x80], GD_NAME_LEN);
    gd->save_name[GD_NAME_LEN] = 0xC4;          /* (build 164) */
    gd->save_level  = get16(raw, 0x84);
}

/* ------------------------------------------------------------------ */
/* Files                                                              */
/* ------------------------------------------------------------------ */

void gamedata_set_save_dir(const char *dir)
{
    snprintf(g_save_dir, sizeof g_save_dir, "%s",
             (dir != NULL && dir[0] != '\0') ? dir : g_save_home);
}

void gamedata_set_save_home(const char *dir)
{
    snprintf(g_save_home, sizeof g_save_home, "%s",
             (dir != NULL && dir[0] != '\0') ? dir : SAVE_DIR_DEFAULT);
    snprintf(g_save_dir, sizeof g_save_dir, "%s", g_save_home);
}

static void slot_path(char *out, size_t size, int slot, const char *suffix)
{
    snprintf(out, size, "%s/slot%d.json%s", g_save_dir, slot + 1, suffix);
}

static void write_hex(FILE *fh, const char *indent, const char *key,
                      const u8 *data, int count, bool comma)
{
    fprintf(fh, "%s\"%s\": [\n", indent, key);
    for (int row = 0; row < count; row += 16) {
        fprintf(fh, "%s  \"", indent);
        for (int i = row; i < row + 16 && i < count; i++) {
            fprintf(fh, "%02X", data[i]);
        }
        fprintf(fh, "\"%s\n", (row + 16 < count) ? "," : "");
    }
    fprintf(fh, "%s]%s\n", indent, comma ? "," : "");
}

static int hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

/* assets/data/flag_names.json (tools/extract_flag_names.py): a name for
 * each byte of $C700-$C7FF. Optional -- without it the keys are just
 * the addresses. */
#define FLAG_NAME_LEN 64
static char g_flag_name[0x100][FLAG_NAME_LEN];

static void load_flag_names(void)
{
    memset(g_flag_name, 0, sizeof g_flag_name);
    JsonValue *root = json_parse_file("assets/data/flag_names.json");
    if (root == NULL) {
        return;
    }
    for (const JsonValue *f = (json_get(root, "flags") != NULL)
                              ? json_get(root, "flags")->child : NULL;
         f != NULL; f = f->next) {
        const char *addr = json_str(f, "addr", "");
        const char *name = json_str(f, "name", "");
        long a = (addr[0] == '$') ? strtol(addr + 1, NULL, 16) : -1;
        if (a >= 0xC700 && a <= 0xC7FF) {
            snprintf(g_flag_name[a - 0xC700], FLAG_NAME_LEN, "%s", name);
        }
    }
    json_free(root);
}

/* The flag bytes as a JSON object, one member a byte: the key is the
 * address and the name ("c727_teim_given_the_letter"), the value the
 * byte. Only the "c7XX" part is read back, so the rest of a key can be
 * renamed freely. treasure_chest_flags' bytes are written by what
 * ObjType_TreasureChests makes of them: 0 "shut", $80 (bit 7, bmi)
 * "locked", 1 (move.b #1 on taking it) "taken"; anything else as a
 * number. */
static void write_named(FILE *fh, const char *key, const u8 *c700, int from,
                        int count, bool chests, bool comma)
{
    fprintf(fh, "  \"%s\": {\n", key);
    for (int i = from; i < from + count; i++) {
        const char *name = g_flag_name[i][0] != '\0' ? g_flag_name[i] : "";
        fprintf(fh, "    \"c%03X%s%s\": ", 0x700 + i, name[0] ? "_" : "", name);
        u8 v = c700[i];
        if (chests && (v == 0 || v == 0x80 || v == 1)) {
            fprintf(fh, "\"%s\"", v == 0 ? "shut" : v == 1 ? "taken" : "locked");
        } else {
            fprintf(fh, "%u", v);
        }
        fprintf(fh, "%s\n", (i + 1 < from + count) ? "," : "");
    }
    fprintf(fh, "  }%s\n", comma ? "," : "");
}

/* Either form: an object by name (format 2, above) or rows of hex
 * (format 1). A member's value may be a number, true / false, "shut",
 * "locked", "taken", or "$XX". A key that does not start "c7XX" is
 * reported and skipped. */
static void read_hex(const JsonValue *rows, u8 *data, int count);

static void read_flags(const JsonValue *v, u8 *c700, int from, int count)
{
    if (v == NULL) {
        return;
    }
    if (v->type == JSON_ARRAY) {
        read_hex(v, &c700[from], count);
        return;
    }
    if (v->type != JSON_OBJECT) {
        return;
    }
    for (const JsonValue *m = v->child; m != NULL; m = m->next) {
        const char *k = (m->key != NULL) ? m->key : "";
        char hex[4] = { 0 };
        int  i = -1;
        if ((k[0] == 'c' || k[0] == 'C') && strlen(k) >= 4) {
            memcpy(hex, k + 1, 3);
            char *end = NULL;
            long a = strtol(hex, &end, 16);
            if (end == hex + 3 && a >= 0x700 && a <= 0x7FF) {
                i = (int)(a - 0x700);
            }
        }
        if (i < from || i >= from + count) {
            fprintf(stderr, "save: flag key \"%s\" is not a c7XX address "
                            "in this section; skipped\n", k);
            continue;
        }
        long b = -1;
        if (m->type == JSON_NUMBER) {
            b = (long)m->number;
        } else if (m->type == JSON_BOOL) {
            b = m->boolean ? 1 : 0;
        } else if (m->type == JSON_STRING && m->string != NULL) {
            const char *s = m->string;
            if (strcmp(s, "shut") == 0) {
                b = 0;
            } else if (strcmp(s, "taken") == 0) {
                b = 1;
            } else if (strcmp(s, "locked") == 0) {
                b = 0x80;
            } else if (s[0] == '$') {
                b = strtol(s + 1, NULL, 16);
            }
        }
        if (b < 0 || b > 0xFF) {
            fprintf(stderr, "save: flag \"%s\" has a value that is not a "
                            "byte; skipped\n", k);
            continue;
        }
        c700[i] = (u8)b;
    }
}

/* Rows of hex, sixteen bytes a row, so an offset is easy to find. */
static void read_hex(const JsonValue *rows, u8 *data, int count)
{
    int n = 0;
    for (const JsonValue *row = (rows != NULL) ? rows->child : NULL;
         row != NULL; row = row->next) {
        if (row->type != JSON_STRING) {
            continue;
        }
        for (const char *p = row->string; p[0] != '\0' && p[1] != '\0'; p += 2) {
            int hi = hex_nibble(p[0]);
            int lo = hex_nibble(p[1]);
            if (hi >= 0 && lo >= 0 && n < count) {
                data[n++] = (u8)(hi << 4 | lo);
            }
        }
    }
}

/* The recruits' two counters ($C604, $C606) as the recruit they have
 * reached: each counts characters in their order of joining, Nei 1 to
 * Shir 7 (loc_B04E, loc_B282), so the value names one -- 0 is "none"
 * (before the dream's Nei), and $C606's 8 is "Shir away": her theft
 * (ProcessStealItem) sent her off, and Rolf's house takes it back to 7
 * when she returns. Anything else is written as its number. */
static void write_recruit(FILE *fh, u16 value)
{
    if (value == 0) {
        fprintf(fh, "\"none\"");
    } else if (value < GD_CHARS) {
        fprintf(fh, "\"%s\"", CHAR_ID[value]);
    } else if (value == GD_CHARS) {
        fprintf(fh, "\"Shir away\"");
    } else {
        fprintf(fh, "%u", value);
    }
}

static u16 read_recruit(const JsonValue *party, const char *key,
                        const char *old_key, u16 fallback)
{
    const JsonValue *v = json_get(party, key);
    if (v == NULL) {
        return (u16)json_int(party, old_key, fallback);
    }
    if (v->type != JSON_STRING) {
        return (u16)json_int(party, key, fallback);
    }
    if (strcmp(v->string, "none") == 0) {
        return 0;
    }
    if (strcmp(v->string, "Shir away") == 0) {
        return GD_CHARS;
    }
    for (int c = 1; c < GD_CHARS; c++) {
        if (strcmp(v->string, CHAR_ID[c]) == 0) {
            return (u16)c;
        }
    }
    return fallback;
}

static void write_char(FILE *fh, const CharData *d, int index, bool comma)
{
    char name[GD_NAME_TEXT];
    name_to_text(name, sizeof name, d->name, GD_NAME_MAX);

    fprintf(fh, "    {\n");
    fprintf(fh, "      \"id\": \"%s\",\n", CHAR_ID[index]);
    fprintf(fh, "      \"name\": \"%s\",\n", name);
    fprintf(fh, "      \"level\": %u,\n", d->level);
    fprintf(fh, "      \"experience\": %lu,\n", (unsigned long)d->experience);
    fprintf(fh, "      \"hp\": %u, \"max_hp\": %u,\n", d->hp, d->max_hp);
    fprintf(fh, "      \"tp\": %u, \"max_tp\": %u,\n", d->tp, d->max_tp);
    fprintf(fh, "      \"strength\": %u, \"mental\": %u, \"agility\": %u,\n",
            d->strength, d->mental, d->agility);
    fprintf(fh, "      \"luck\": %u, \"dexterity\": %u,\n",
            d->luck, d->dexterity);
    fprintf(fh, "      \"attack_base\": %u, \"attack\": %u, \"defense\": %u,\n",
            d->attack_base, d->attack, d->defense);
    /* +$25 and +$26: how many of the character's battle and map
     * techniques are known -- the first so many of each half of
     * CharAvailableTechniques (loc_112C4), raised by a level-up's
     * nibbles (Character_ProcessLevelUp) -- and bit 7, Musik in that
     * half's last known place (loc_112FC): the map byte's once
     * Ustvestia's lesson is learned (loc_C7D6, which also counts it).
     * Nothing sets the battle byte's; an edited save's is kept. */
    fprintf(fh, "      \"battle_techniques_known\": %u, "
                "\"map_techniques_known\": %u, \"knows_musik\": %s,\n",
            d->techs_a & 0x7F, d->techs_b & 0x7F,
            (d->techs_b & 0x80) ? "true" : "false");
    if (d->techs_a & 0x80) {
        fprintf(fh, "      \"battle_musik\": true,\n");
    }
    fprintf(fh, "      \"status\": %u,\n", d->status);

    fprintf(fh, "      \"equipment\": {");
    for (int s = 0; s < EQUIP_COUNT; s++) {
        fprintf(fh, "%s\"%s\": \"%s\"", (s == 0) ? " " : ", ",
                EQUIP_KEY[s], gamedata_item_key(d->equip[s]));
    }
    fprintf(fh, " },\n");

    fprintf(fh, "      \"inventory\": [");
    for (int i = 0; i < d->item_count && i < GD_ITEM_SLOTS; i++) {
        fprintf(fh, "%s\n        { \"item\": \"%s\", \"equipped\": %s }",
                (i == 0) ? "" : ",", gamedata_item_key(d->items[i]),
                (d->items[i] & 0x80) ? "true" : "false");
    }
    fprintf(fh, "%s]\n", (d->item_count != 0) ? "\n      " : "");
    fprintf(fh, "    }%s\n", comma ? "," : "");
}

/* A map as the save writes it: its name in quotes, or its number when
 * levels.json has no name for it. */
static void map_value(char *out, size_t size, int id)
{
    const char *name = gamedata_level_name(id);
    if (name != NULL) {
        snprintf(out, size, "\"%s\"", name);
    } else {
        snprintf(out, size, "%d", id);
    }
}

/* A map read back: a name (any case) or a number. */
static u16 read_map(const JsonValue *obj, const char *key, u16 fallback)
{
    const JsonValue *v = json_get(obj, key);
    if (v == NULL) {
        return fallback;
    }
    if (v->type == JSON_NUMBER) {
        return (u16)v->number;
    }
    if (v->type == JSON_STRING) {
        int id = gamedata_level_id(v->string);
        if (id >= 0) {
            return (u16)id;
        }
        fprintf(stderr, "save: \"%s\" is not a map's name; kept %u\n",
                (v->string != NULL) ? v->string : "", fallback);
    }
    return fallback;
}

/* A place kept as three words of the $C600 block: map, y, x. */
static void write_place(FILE *fh, const char *key, const u8 *raw, int off)
{
    char map[40];
    map_value(map, sizeof map, get16(raw, off));
    fprintf(fh, "  \"%s\": { \"map\": %s, \"x\": %u, \"y\": %u },\n",
            key, map, get16(raw, off + 4), get16(raw, off + 2));
}

static void read_place(GameData *gd, const JsonValue *root, const char *key,
                       int off)
{
    const JsonValue *p = json_get(root, key);
    if (p == NULL) {
        return;
    }
    gamedata_set_word(gd, off, read_map(p, "map", gamedata_word(gd, off)));
    gamedata_set_word(gd, off + 2,
                      (u16)json_int(p, "y", gamedata_word(gd, off + 2)));
    gamedata_set_word(gd, off + 4,
                      (u16)json_int(p, "x", gamedata_word(gd, off + 4)));
}

bool gamedata_save(const GameData *gd, int slot)
{
    char path[SAVE_PATH_MAX];
    u8   raw[GD_C600_SIZE];
    char name[GD_NAME_TEXT];

    make_dir(g_save_dir);
    slot_path(path, sizeof path, slot, "");

    /* (build 113) "wb", as settings.json: Windows's text mode writes
     * "\r\n", and the harness copies saves into the tracked checkpoints
     * (routes/saves/story_<stage>.json), which stay the same bytes on
     * every OS. */
    FILE *fh = fopen(path, "wb");
    if (fh == NULL) {
        fprintf(stderr, "gamedata: cannot write %s\n", path);
        return false;
    }

    pack_c600(gd, raw);
    name_to_text(name, sizeof name, gd->save_name, GD_NAME_MAX);

    fprintf(fh, "{\n");
    fprintf(fh, "  \"format\": %d,\n", SAVE_FORMAT);
    fprintf(fh, "  \"slot_label\": { \"name\": \"%s\", \"level\": %u },\n",
            name, gd->save_level);
    fprintf(fh, "  \"money\": %lu,\n", (unsigned long)gd->money);

    /* The scooter's parking place is $C656 (x) and $C654 (y), a cell
     * corner on Motavia; whether there is a scooter at all is event flag
     * 6, $C716 (0 none, 1 given, 2 ridden once). */
    char map[40];
    map_value(map, sizeof map, gd->level_index);
    fprintf(fh, "  \"location\": { \"planet\": \"%s\", \"map\": %s, "
                "\"x\": %u, \"y\": %u, \"on_jet_scooter\": %s,\n"
                "                \"jet_scooter_parked\": { \"x\": %u, "
                "\"y\": %u } },\n",
            PLANET[gd->planet & 1], map, gd->level_x,
            gd->level_y, gd->jet_scooter ? "true" : "false",
            get16(raw, GD_SCOOTER_PARKED_X), get16(raw, GD_SCOOTER_PARKED_Y));

    /* The Data Memory's save (loc_B5A4) keeps where and how it was made;
     * the Telepipe and Ryuka land at that place when the party is on the
     * same planet and the save was not made with the Visiphone, else at
     * the planet's own spot (docs/game.md, "The $C600 block"). */
    fprintf(fh, "  \"last_save\": { \"planet\": \"%s\", "
                "\"by_visiphone\": %s },\n",
            PLANET[get16(raw, GD_LAST_SAVE_PLANET) & 1],
            get16(raw, GD_LAST_SAVE_VISIPHONE) != 0 ? "true" : "false");
    write_place(fh, "telepipe_lands_at", raw, GD_LAST_SAVE_MAP);
    write_place(fh, "dungeon_entrance", raw, GD_ENTRANCE_MAP);

    fprintf(fh, "  \"party\": {\n    \"members\": [");
    for (int i = 0; i <= gd->party_members_num && i < GD_PARTY_MAX; i++) {
        fprintf(fh, "%s\"%s\"", (i == 0) ? "" : ", ",
                CHAR_ID[gd->party_member_id[i] % GD_CHARS]);
    }
    fprintf(fh, "],\n");
    /* $C604 and $C606: the last recruit to come to Rolf's house and the
     * last the story has sent there (docs/game.md, "The recruits"). */
    fprintf(fh, "    \"last_recruit_joined\": ");
    write_recruit(fh, gd->party_members_joined);
    fprintf(fh, ",\n    \"last_recruit_sent\": ");
    write_recruit(fh, gd->party_member_join_next);
    fprintf(fh, "\n  },\n");

    /* The Central Tower room's keeper: what the party left there. */
    fprintf(fh, "  \"tower_room_items\": [");
    int kept = raw[GD_KEEPER_COUNT];
    for (int i = 0; i < kept && i < GD_ITEM_SLOTS; i++) {
        fprintf(fh, "%s\"%s\"", (i == 0) ? "" : ", ",
                gamedata_item_key(raw[GD_KEEPER_ITEMS + i] & 0x7F));
    }
    fprintf(fh, "],\n");

    fprintf(fh, "  \"characters\": [\n");
    for (int c = 0; c < GD_CHARS; c++) {
        write_char(fh, &gd->chr[c], c, c + 1 < GD_CHARS);
    }
    fprintf(fh, "  ],\n");

    /* $C700-$C70F is not named in the disassembly; $C710 is event_flags
     * and $C780 treasure_chest_flags. */
    write_named(fh, "flags_c700", gd->c700, 0x00, 0x10, false, true);
    write_named(fh, "event_flags", gd->c700, 0x10, 0x70, false, true);
    write_named(fh, "treasure_chest_flags", gd->c700, 0x80, 0x80, true, true);

    /* The whole $C600 block. Named fields above win over these bytes on
     * load; this is here so the unnamed ones survive. */
    write_hex(fh, "  ", "raw_c600", raw, GD_C600_SIZE, false);
    fprintf(fh, "}\n");

    fclose(fh);
    return true;
}

static int char_index(const char *id)
{
    for (int c = 0; c < GD_CHARS; c++) {
        if (id != NULL && strcmp(id, CHAR_ID[c]) == 0) {
            return c;
        }
    }
    return -1;
}

static void read_char(const JsonValue *src, CharData *d, int c)
{
    const JsonValue *eq  = json_get(src, "equipment");
    const JsonValue *inv = json_get(src, "inventory");

    /* (build 165) A default name of any language is the chosen one's
     * before it is made codes: the Japanese cartridge's kana would be
     * spaces in another language's, and lost. */
    const char *text = json_str(src, "name", "");
    if (strcmp(text, g_initial_name[c]) == 0
        || lang_is_default_name(c, text)) {
        const char *want = lang_char_name(c);
        text = (want != NULL) ? want : g_initial_name[c];
    }
    name_from_text(d->name, GD_NAME_MAX, text);
    d->level       = (u16)json_int(src, "level", d->level);
    d->experience  = (u32)json_int(src, "experience", 0);
    d->hp          = (u16)json_int(src, "hp", 0);
    d->max_hp      = (u16)json_int(src, "max_hp", d->hp);
    d->tp          = (u16)json_int(src, "tp", 0);
    d->max_tp      = (u16)json_int(src, "max_tp", d->tp);
    d->strength    = (u16)json_int(src, "strength", 0);
    d->mental      = (u16)json_int(src, "mental", 0);
    d->agility     = (u16)json_int(src, "agility", 0);
    d->luck        = (u16)json_int(src, "luck", 0);
    d->dexterity   = (u16)json_int(src, "dexterity", 0);
    d->attack_base = (u16)json_int(src, "attack_base", 0);
    d->attack      = (u16)json_int(src, "attack", d->attack_base);
    d->defense     = (u16)json_int(src, "defense", 0);
    if (json_get(src, "battle_techniques_known") != NULL) {
        d->techs_a = (u8)((json_int(src, "battle_techniques_known", 0) & 0x7F)
                          | (json_bool(src, "battle_musik", false) ? 0x80 : 0));
    } else {
        d->techs_a = (u8)json_int(src, "techniques_a", 0);     /* up to 105 */
    }
    if (json_get(src, "map_techniques_known") != NULL) {
        d->techs_b = (u8)((json_int(src, "map_techniques_known", 0) & 0x7F)
                          | (json_bool(src, "knows_musik", false) ? 0x80 : 0));
    } else {
        d->techs_b = (u8)json_int(src, "techniques_b", 0);     /* up to 105 */
    }
    d->status      = (u16)json_int(src, "status", 0);

    for (int s = 0; s < EQUIP_COUNT; s++) {
        d->equip[s] = (u8)item_by_key(json_str(eq, EQUIP_KEY[s], "None"));
    }

    memset(d->items, 0, sizeof d->items);
    d->item_count = 0;
    for (const JsonValue *e = (inv != NULL) ? inv->child : NULL;
         e != NULL && d->item_count < GD_ITEM_SLOTS; e = e->next) {
        int id = item_by_key(json_str(e, "item", "None"));
        d->items[d->item_count++] =
            (u8)(id | (json_bool(e, "equipped", false) ? 0x80 : 0));
    }
}

bool gamedata_load(GameData *gd, int slot)
{
    char path[SAVE_PATH_MAX];
    slot_path(path, sizeof path, slot, "");

    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return false;
    }

    memset(gd, 0, sizeof *gd);

    read_hex(json_get(root, "raw_c600"), gd->c600_raw, GD_C600_SIZE);
    unpack_c600(gd);

    read_flags(json_get(root, "flags_c700"), gd->c700, 0x00, 0x10);
    read_flags(json_get(root, "event_flags"), gd->c700, 0x10, 0x70);
    read_flags(json_get(root, "treasure_chest_flags"), gd->c700, 0x80, 0x80);

    const JsonValue *label = json_get(root, "slot_label");
    name_from_text(gd->save_name, GD_NAME_MAX, json_str(label, "name", ""));
    gd->save_level = (u16)json_int(label, "level", gd->save_level);

    gd->money = (u32)json_int(root, "money", (long)gd->money);

    const JsonValue *loc = json_get(root, "location");
    const char *planet = json_str(loc, "planet", PLANET[gd->planet & 1]);
    gd->planet      = (strcmp(planet, PLANET[1]) == 0) ? 1 : 0;
    gd->level_index = read_map(loc, "map", gd->level_index);
    gd->level_x     = (u16)json_int(loc, "x", gd->level_x);
    gd->level_y     = (u16)json_int(loc, "y", gd->level_y);
    gd->jet_scooter = json_bool(loc, "on_jet_scooter", gd->jet_scooter != 0)
                    ? 1 : 0;
    const JsonValue *parked = json_get(loc, "jet_scooter_parked");
    if (parked != NULL) {
        gamedata_set_word(gd, GD_SCOOTER_PARKED_X,
            (u16)json_int(parked, "x", gamedata_word(gd, GD_SCOOTER_PARKED_X)));
        gamedata_set_word(gd, GD_SCOOTER_PARKED_Y,
            (u16)json_int(parked, "y", gamedata_word(gd, GD_SCOOTER_PARKED_Y)));
    }

    /* The named parts of the $C600 block win over raw_c600's bytes. */
    const JsonValue *last = json_get(root, "last_save");
    if (last != NULL) {
        const char *lp = json_str(last, "planet",
            PLANET[gamedata_word(gd, GD_LAST_SAVE_PLANET) & 1]);
        gamedata_set_word(gd, GD_LAST_SAVE_PLANET,
                          (strcmp(lp, PLANET[1]) == 0) ? 1 : 0);
        gamedata_set_word(gd, GD_LAST_SAVE_VISIPHONE,
            json_bool(last, "by_visiphone",
                      gamedata_word(gd, GD_LAST_SAVE_VISIPHONE) != 0) ? 1 : 0);
    }
    read_place(gd, root, "telepipe_lands_at", GD_LAST_SAVE_MAP);
    read_place(gd, root, "dungeon_entrance", GD_ENTRANCE_MAP);
    const JsonValue *kept = json_get(root, "tower_room_items");
    if (kept != NULL && kept->type == JSON_ARRAY) {
        int n = 0;
        memset(&gd->c600_raw[GD_KEEPER_ITEMS], 0, GD_ITEM_SLOTS);
        for (const JsonValue *e = kept->child;
             e != NULL && n < GD_ITEM_SLOTS; e = e->next) {
            /* item_by_key reports a name it does not know. */
            int id = (e->type == JSON_STRING) ? item_by_key(e->string) : 0;
            if (id <= 0) {
                continue;
            }
            gd->c600_raw[GD_KEEPER_ITEMS + n++] = (u8)id;
        }
        gd->c600_raw[GD_KEEPER_COUNT] = (u8)n;
    }

    const JsonValue *party   = json_get(root, "party");
    const JsonValue *members = json_get(party, "members");
    int count = json_length(members);
    if (count > 0) {
        if (count > GD_PARTY_MAX) {
            count = GD_PARTY_MAX;
        }
        memset(gd->party_member_id, 0, sizeof gd->party_member_id);
        for (int i = 0; i < count; i++) {
            const JsonValue *m = json_at(members, i);
            int c = char_index((m->type == JSON_STRING) ? m->string : NULL);
            gd->party_member_id[i] = (u16)((c >= 0) ? c : 0);
        }
        gd->party_members_num = (u16)(count - 1);
    }
    /* Up to build 105 both were numbers under older names. */
    gd->party_members_joined = read_recruit(party, "last_recruit_joined",
        "recruits_joined", gd->party_members_joined);
    gd->party_member_join_next = read_recruit(party, "last_recruit_sent",
        "next_recruit_event", gd->party_member_join_next);

    const JsonValue *chars = json_get(root, "characters");
    for (const JsonValue *src = (chars != NULL) ? chars->child : NULL;
         src != NULL; src = src->next) {
        int c = char_index(json_str(src, "id", NULL));
        if (c >= 0) {
            read_char(src, &gd->chr[c], c);
        }
    }

    gamedata_language_names(gd);        /* (build 163) */
    pack_c600(gd, gd->c600_raw);
    json_free(root);
    return true;
}

void gamedata_slot_summary(int slot, SlotSummary *out)
{
    char path[SAVE_PATH_MAX];
    slot_path(path, sizeof path, slot, "");
    memset(out, 0, sizeof *out);

    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return;
    }
    fclose(fh);
    out->exists = true;

    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return;
    }
    out->valid = true;

    const JsonValue *label = json_get(root, "slot_label");
    name_from_text(out->name, GD_NAME_MAX, json_str(label, "name", ""));
    out->level = (int)json_int(label, "level", 0);
    json_free(root);
}

void gamedata_erase(int slot)
{
    char path[SAVE_PATH_MAX];
    slot_path(path, sizeof path, slot, "");
    remove(path);
}

void gamedata_quarantine(int slot)
{
    char from[SAVE_PATH_MAX], to[SAVE_PATH_MAX];
    slot_path(from, sizeof from, slot, "");
    slot_path(to, sizeof to, slot, ".bad");
    remove(to);
    rename(from, to);
}

/* $FFFFDA00: the party as the Gaira prison found it. */
static CharData g_da00[GD_CHARS];

void gamedata_prison(GameData *gd)
{
    memcpy(g_da00, gd->chr, sizeof g_da00);
    for (int c = 0; c < GD_CHARS; c++) {
        gamedata_equip_bonuses_remove(gd, c);
    }
    for (int c = 0; c < GD_CHARS; c++) {
        CharData *ch = &gd->chr[c];
        ch->tp = 0;                                 /* +$06 */
        memset(ch->equip, 0, sizeof ch->equip);     /* +$20..+$24 */
        ch->equip[EQUIP_BODY] = 0x7B;               /* ItemID_PrsnClths */
        ch->item_count = 2;                         /* +$27 */
        memset(ch->items, 0, sizeof ch->items);
        ch->items[0] = 0x0C;                        /* ItemID_PlsmRing */
        ch->items[1] = 0x80 | 0x7B;                 /* PrsnClths, worn */
    }
    for (int c = 0; c < GD_CHARS; c++) {
        gamedata_equip_bonuses_add(gd, c);
    }
}

void gamedata_prison_release(GameData *gd)
{
    for (int c = 0; c < GD_CHARS; c++) {
        gamedata_equip_bonuses_remove(gd, c);
    }
    for (int c = 0; c < GD_CHARS; c++) {
        CharData *ch = &gd->chr[c];
        const CharData *was = &g_da00[c];
        ch->hp = ch->max_hp;
        ch->tp = ch->max_tp;
        memcpy(ch->equip, was->equip, sizeof ch->equip);
        ch->item_count = was->item_count;
        memcpy(ch->items, was->items, sizeof ch->items);
    }
    for (int c = 0; c < GD_CHARS; c++) {
        gamedata_equip_bonuses_add(gd, c);
    }
}
