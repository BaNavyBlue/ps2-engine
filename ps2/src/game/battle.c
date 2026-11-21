#include "battle.h"
#include "battle_tables.h"

#include "../core/json.h"
#include "rom_ids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tables from assets/data/battle.json                                */
/* ------------------------------------------------------------------ */

#define ENEMY_MAX       128
#define FORMATION_MAX   260
#define GROUP_MAX       96
#define MAP_SETS_MAX    96
#define CHAR_MAPS       8
#define WEAPON_MAX      48
#define TECH_MAX_PROPS  56
#define EFFECT_ART_MAX  32

typedef struct {
    u16 cram[15];
    char art_file[32];
    u16 exp, meseta, attack, defense, hp;
    u8  type, agility, technique, technique_rate, success_index;
    u8  escape, anim_duration, sfx_attack, sfx_technique, anim, object;
    u8  anim_bytes[4];
    int map;                    /* index into g_maps */
    u8  name[BATTLE_NAME_LEN];  /* CHARSET codes */
    char label[32];             /* (build 163) battle.json's, for names.json */
} EnemyInfo;

typedef struct {
    u8 enemy1, count1, enemy2, count2, layout;
} Formation;

/* The party's battle art and mappings (Battle_CharObjectPtrs). */
typedef struct {
    char    map[32];
    char    art[32];
    int     palette_line;
    TileSet tiles;
} CharSprite;

/* WeaponTechArtPtrs: art blobs and where they go. */
typedef struct {
    int  count;
    char file[2][32];
    int  tile[2];
    TileSet tiles[2];
    bool loaded[2];
} EffectArt;

static void palette_effects(Battle *b, Video *video);
static EnemyInfo  g_enemy[ENEMY_MAX];
static int        g_enemy_count;
static Formation  g_formation[FORMATION_MAX];
static int        g_formation_count;
static u8         g_group[GROUP_MAX][8];
static int        g_group_count;
static u8         g_region[5][96];       /* motavia x4, skure */
static u16        g_enemy_x[16][5];
static int        g_char_pos[8];
static MapSet     g_maps[MAP_SETS_MAX];
static int        g_map_count;
static CharSprite g_char[CHAR_MAPS];
static int        g_char_count;
static AnimTable  g_anim_tables[ANIM_TABLE_COUNT];
static WeaponProp g_weapon[WEAPON_MAX];
static int        g_weapon_count;
static TechProp   g_tech[TECH_MAX_PROPS];
static int        g_tech_count;
static EffectArt  g_effect_art[EFFECT_ART_MAX];
static int        g_effect_art_count;
static bool       g_tables_ready;

u8 g_success_rate[32];
u8 g_target_chances[8];
u8 g_hit_anim[28];

enum { REGION_MOTAVIA, REGION_MOTAVIA_SCOOTER, REGION_MOTAVIA_LATE,
       REGION_MOTAVIA_LATE_SCOOTER, REGION_SKURE };

static void read_bytes(const JsonValue *arr, u8 *out, int cap)
{
    int n = json_length(arr);
    for (int i = 0; i < n && i < cap; i++) {
        out[i] = (u8)json_at(arr, i)->number;
    }
}

static int map_index(const char *label)
{
    for (int i = 0; i < g_map_count; i++) {
        if (strcmp(g_maps[i].label, label) == 0) {
            return i;
        }
    }
    return -1;
}

const MapSet *battle_map_set(const char *label)
{
    if (label == NULL || label[0] == '\0') {
        return NULL;
    }
    int i = map_index(label);
    return (i >= 0) ? &g_maps[i] : NULL;
}

static void read_piece(const JsonValue *p, Piece *out)
{
    out->y    = (s16)json_int(p, "y", 0);
    out->size = (u8)json_int(p, "size", 0);
    out->tile = (u16)json_int(p, "tile", 0);
    out->x    = (s16)json_int(p, "x", 0);
}

/* A frame is either {pieces: [...]} or a bare list of pieces (the
 * party's maps), or a nametable block {x, y, w, h, cells}. */
static void read_frame(const JsonValue *f, MapFrame *fr)
{
    memset(fr, 0, sizeof *fr);
    const JsonValue *pieces = json_get(f, "pieces");
    if (pieces == NULL && f->type == JSON_ARRAY) {
        pieces = f;
    }
    if (pieces != NULL) {
        fr->block = false;
        int pc = json_length(pieces);
        if (pc > PIECES_MAX) {
            fprintf(stderr, "battle: a frame of %d pieces, %d kept "
                            "(PIECES_MAX)\n", pc, PIECES_MAX);
        }
        for (int k = 0; k < pc && k < PIECES_MAX; k++) {
            read_piece(json_at(pieces, k), &fr->piece[fr->piece_count++]);
        }
        return;
    }
    fr->block = true;
    fr->x = (s16)json_int(f, "x", 0);
    fr->y = (s16)json_int(f, "y", 0);
    fr->w = (u8)json_int(f, "w", 0);
    fr->h = (u8)json_int(f, "h", 0);
    fr->cell = calloc((size_t)fr->w * fr->h + 1, sizeof *fr->cell);
    const JsonValue *cells = json_get(f, "cells");
    for (int r = 0; r < fr->h; r++) {
        const JsonValue *row = json_at(cells, r);
        for (int c = 0; c < fr->w; c++) {
            const JsonValue *v = json_at(row, c);
            fr->cell[r * fr->w + c] = (v != NULL) ? (u16)v->number : 0;
        }
    }
}

static void read_maps(const JsonValue *maps)
{
    for (const JsonValue *m = (maps != NULL) ? maps->child : NULL;
         m != NULL; m = m->next) {
        if (g_map_count >= MAP_SETS_MAX) {
            break;
        }
        MapSet *set = &g_maps[g_map_count++];
        snprintf(set->label, sizeof set->label, "%s", m->key);
        int n = json_length(m);
        set->frame = calloc((size_t)n + 1, sizeof *set->frame);
        for (int i = 0; i < n; i++) {
            read_frame(json_at(m, i), &set->frame[set->frame_count++]);
        }
    }
}

static void read_anim_table(const JsonValue *t, AnimTable *out)
{
    const JsonValue *index   = json_get(t, "index");
    const JsonValue *scripts = json_get(t, "scripts");
    int n = json_length(index);
    out->count  = n;
    out->script = calloc((size_t)n + 1, sizeof *out->script);
    for (int i = 0; i < n; i++) {
        const JsonValue *label = json_at(index, i);
        const JsonValue *bytes = (label != NULL && label->string != NULL)
                               ? json_get(scripts, label->string) : NULL;
        if (bytes != NULL) {
            int len = json_length(bytes);
            out->script[i].len = (len < 256) ? len : 256;
            read_bytes(bytes, out->script[i].byte, out->script[i].len);
        }
    }
}

const AnimTable *anim_table(int which)
{
    return (which >= 0 && which < ANIM_TABLE_COUNT) ? &g_anim_tables[which]
                                                    : NULL;
}

const AnimScript *anim_script(const AnimTable *t, int index)
{
    if (t == NULL || index < 0 || index >= t->count) {
        return NULL;
    }
    return &t->script[index];
}

const WeaponProp *weapon_prop(int index)
{
    return (index >= 0 && index < g_weapon_count) ? &g_weapon[index] : NULL;
}

const TechProp *tech_prop(int id)
{
    int i = (id & 0x3F) - 1;
    return (i >= 0 && i < g_tech_count) ? &g_tech[i] : NULL;
}

static u8 charset_code(char ch)
{
    return win_charset(ch);
}

bool battle_tables_load(void)
{
    if (g_tables_ready) {
        return true;
    }
    JsonValue *root = json_parse_file("assets/data/battle.json");
    if (root == NULL) {
        fprintf(stderr, "battle: assets/data/battle.json missing\n");
        return false;
    }

    read_maps(json_get(root, "maps"));
    read_maps(json_get(root, "effect_maps"));
    read_maps(json_get(root, "sprites"));

    const JsonValue *enemies = json_get(root, "enemies");
    int n = json_length(enemies);
    for (int i = 0; i < n && i + 1 < ENEMY_MAX; i++) {
        const JsonValue *e = json_at(enemies, i);
        int id = (int)json_int(e, "id", i + 1);
        EnemyInfo *info = &g_enemy[id];
        const JsonValue *pal = json_get(e, "palette");
        for (int k = 0; k < 15; k++) {
            const JsonValue *p = json_at(pal, k);
            info->cram[k] = (p != NULL && p->string != NULL)
                          ? (u16)strtoul(p->string, NULL, 16) : 0;
        }
        snprintf(info->art_file, sizeof info->art_file, "%s",
                 json_str(e, "art_file", ""));
        snprintf(info->label, sizeof info->label, "%s",
                 json_str(e, "label", ""));
        info->exp            = (u16)json_int(e, "exp", 0);
        info->meseta         = (u16)json_int(e, "meseta", 0);
        info->attack         = (u16)json_int(e, "attack", 0);
        info->defense        = (u16)json_int(e, "defense", 0);
        info->hp             = (u16)json_int(e, "hp", 0);
        info->type           = (u8)json_int(e, "type", 0);
        info->agility        = (u8)json_int(e, "agility", 0);
        info->technique      = (u8)json_int(e, "technique", 0);
        info->technique_rate = (u8)json_int(e, "technique_rate", 0);
        info->success_index  = (u8)json_int(e, "success_index", 0);
        info->escape         = (u8)json_int(e, "escape", 0);
        info->object         = (u8)json_int(e, "object", 0xF);
        info->anim_duration  = (u8)json_int(e, "anim_duration", 0);
        info->sfx_attack     = (u8)json_int(e, "sfx_attack", 0);
        info->sfx_technique  = (u8)json_int(e, "sfx_technique", 0);
        info->anim           = (u8)json_int(e, "anim", 0);
        read_bytes(json_get(e, "anim_bytes"), info->anim_bytes, 4);
        info->map = map_index(json_str(e, "map", ""));
        /* nametxt: the codes, then $C4 when the name is short. */
        const char *name = json_str(e, "name", "");
        int len = (int)strlen(name);
        for (int k = 0; k < BATTLE_NAME_LEN; k++) {
            info->name[k] = (k < len) ? charset_code(name[k])
                          : (k == len) ? TXT_END : 0;
        }
        if (id + 1 > g_enemy_count) {
            g_enemy_count = id + 1;
        }
    }

    const JsonValue *forms = json_get(root, "formations");
    n = json_length(forms);
    for (int i = 0; i < n && i < FORMATION_MAX; i++) {
        const JsonValue *f = json_at(forms, i);
        g_formation[i].enemy1 = (u8)json_int(f, "enemy1", 0);
        g_formation[i].count1 = (u8)json_int(f, "count1", 0);
        g_formation[i].enemy2 = (u8)json_int(f, "enemy2", 0);
        g_formation[i].count2 = (u8)json_int(f, "count2", 0);
        g_formation[i].layout = (u8)json_int(f, "layout", 0);
        g_formation_count = i + 1;
    }

    const JsonValue *groups = json_get(root, "groups");
    n = json_length(groups);
    for (int i = 0; i < n && i < GROUP_MAX; i++) {
        read_bytes(json_at(groups, i), g_group[i], 8);
        g_group_count = i + 1;
    }

    const JsonValue *regions = json_get(root, "regions");
    read_bytes(json_get(regions, "motavia"), g_region[0], 96);
    read_bytes(json_get(regions, "motavia_scooter"), g_region[1], 96);
    read_bytes(json_get(regions, "motavia_late"), g_region[2], 96);
    read_bytes(json_get(regions, "motavia_late_scooter"), g_region[3], 96);
    read_bytes(json_get(regions, "skure"), g_region[4], 64);

    const JsonValue *ex = json_get(root, "enemy_x");
    n = json_length(ex);
    for (int i = 0; i < n && i < 16; i++) {
        const JsonValue *row = json_at(ex, i);
        for (int k = 0; k < 5; k++) {
            const JsonValue *v = json_at(row, k);
            g_enemy_x[i][k] = (v != NULL) ? (u16)v->number : 0;
        }
    }

    const JsonValue *cp = json_get(root, "char_pos");
    for (int i = 0; i < 8; i++) {
        const JsonValue *v = json_at(cp, i);
        g_char_pos[i] = (v != NULL) ? (int)v->number : 0;
    }

    const JsonValue *tables = json_get(root, "anim_tables");
    static const char *const TABLE_KEY[ANIM_TABLE_COUNT] = {
        "idle", "enemy_attack", "enemy_tech", "enemy_child_a",
        "enemy_child_b", "chars", "effect", "effect2",
    };
    for (int i = 0; i < ANIM_TABLE_COUNT; i++) {
        const JsonValue *t = json_get(tables, TABLE_KEY[i]);
        if (t != NULL) {
            read_anim_table(t, &g_anim_tables[i]);
        }
    }

    const JsonValue *chars = json_get(root, "char_sprites");
    n = json_length(chars);
    for (int i = 0; i < n && i < CHAR_MAPS; i++) {
        const JsonValue *c = json_at(chars, i);
        CharSprite *cs = &g_char[i];
        snprintf(cs->map, sizeof cs->map, "%s", json_str(c, "map", ""));
        snprintf(cs->art, sizeof cs->art, "%s", json_str(c, "art", ""));
        cs->palette_line = (int)json_int(c, "palette_line", 0);
        g_char_count = i + 1;
    }

    const JsonValue *wp = json_get(root, "weapon_props");
    n = json_length(wp);
    for (int i = 0; i < n && i < WEAPON_MAX; i++) {
        const JsonValue *w = json_at(wp, i);
        const JsonValue *bytes = json_get(w, "bytes");
        WeaponProp *out = &g_weapon[i];
        out->kind   = (u8)json_at(bytes, 0)->number;
        out->spread = (u8)json_at(bytes, 1)->number;
        out->sound  = (u8)json_at(bytes, 2)->number;
        out->art    = (int)json_int(w, "art", 0);
        out->pal    = json_int(w, "pal", 0) != 0;
        out->art2   = (int)json_int(w, "art2", 0);
        out->pal2   = json_int(w, "pal2", 0) != 0;
        snprintf(out->map, sizeof out->map, "%s", json_str(w, "map", ""));
        snprintf(out->map2, sizeof out->map2, "%s", json_str(w, "map2", ""));
        g_weapon_count = i + 1;
    }

    const JsonValue *tp = json_get(root, "tech_props");
    n = json_length(tp);
    for (int i = 0; i < n && i < TECH_MAX_PROPS; i++) {
        const JsonValue *t = json_at(tp, i);
        const JsonValue *bytes = json_get(t, "bytes");
        TechProp *out = &g_tech[i];
        out->rate  = (u8)json_at(bytes, 0)->number;
        out->sound = (u8)json_at(bytes, 1)->number;
        out->art   = (int)json_int(t, "art", 0);
        out->pal   = json_int(t, "pal", 0) != 0;
        out->art2  = (int)json_int(t, "art2", 0);
        out->pal2  = json_int(t, "pal2", 0) != 0;
        snprintf(out->map, sizeof out->map, "%s", json_str(t, "map", ""));
        snprintf(out->map2, sizeof out->map2, "%s", json_str(t, "map2", ""));
        g_tech_count = i + 1;
    }

    const JsonValue *ea = json_get(root, "effect_art");
    n = json_length(ea);
    for (int i = 0; i < n && i < EFFECT_ART_MAX; i++) {
        const JsonValue *e = json_at(ea, i);
        const JsonValue *parts = json_get(e, "parts");
        EffectArt *out = &g_effect_art[i];
        int pc = json_length(parts);
        out->count = 0;
        for (int k = 0; k < pc && k < 2; k++) {
            const JsonValue *p = json_at(parts, k);
            snprintf(out->file[k], sizeof out->file[k], "%s",
                     json_str(p, "file", ""));
            out->tile[k] = (int)(json_int(p, "vram", 0x8000) / 0x20);
            out->count++;
        }
        g_effect_art_count = i + 1;
    }

    read_bytes(json_get(root, "success_rate"), g_success_rate, 32);
    read_bytes(json_get(root, "target_chances"), g_target_chances, 8);
    read_bytes(json_get(root, "hit_anim"), g_hit_anim, 28);

    json_free(root);
    g_tables_ready = true;
    return true;
}

/* Battle_RolfArt is a binclude named battle_rolf_art.bin; the PNG is
 * named from that stem. */
static const TileSet *char_art(int slot)
{
    CharSprite *cs = &g_char[slot & 7];
    if (cs->tiles.count == 0) {
        char path[96];
        const char *label = cs->art;
        char core[24] = { 0 };
        if (strncmp(label, "Battle_", 7) == 0) {
            /* Battle_AmyShirArt -> amy_shir: the stem of the binclude
             * (battle_amy_shir_art.bin) the PNG is named after. Drop
             * "Art", then a '_' before each capital but the first. */
            char camel[24] = { 0 };
            snprintf(camel, sizeof camel, "%s", label + 7);
            size_t len = strlen(camel);
            if (len > 3) {
                camel[len - 3] = '\0';
            }
            size_t o = 0;
            for (size_t i = 0; camel[i] != '\0' && o + 2 < sizeof core; i++) {
                char ch = camel[i];
                if (ch >= 'A' && ch <= 'Z') {
                    if (i > 0) {
                        core[o++] = '_';
                    }
                    ch = (char)(ch - 'A' + 'a');
                }
                core[o++] = ch;
            }
            core[o] = '\0';
        }
        snprintf(path, sizeof path, "assets/tiles/battle_%s_art.png", core);
        if (!tileset_load(&cs->tiles, path)) {
            fprintf(stderr, "battle: cannot load %s\n", path);
        }
    }
    return &cs->tiles;
}

const u8 *battle_enemy_name(int id)
{
    static const u8 NONE[BATTLE_NAME_LEN] = { TXT_END };
    return (id > 0 && id < g_enemy_count) ? g_enemy[id].name : NONE;
}

const char *battle_enemy_label(int id)
{
    return (id > 0 && id < g_enemy_count) ? g_enemy[id].label : NULL;
}

const MapSet *battle_char_map(int c)
{
    return battle_map_set(g_char[c & 7].map);
}

int battle_char_palette_line(int c)
{
    return g_char[c & 7].palette_line;
}

/* loc_608A and the VBlank's loc_5F74 together: the effect's art at the
 * tiles WeaponTechArtPtrs names ($8000 is tile $400, $9000 is $480). */
void battle_effect_art_load(TileSet *vram, int index)
{
    if (index < 0 || index >= g_effect_art_count) {
        return;
    }
    EffectArt *ea = &g_effect_art[index];
    for (int k = 0; k < ea->count; k++) {
        if (!ea->loaded[k]) {
            char path[96];
            snprintf(path, sizeof path, "assets/tiles/%s.png", ea->file[k]);
            ea->loaded[k] = tileset_load(&ea->tiles[k], path);
            if (!ea->loaded[k]) {
                fprintf(stderr, "battle: cannot load %s (run `make "
                                "assets`)\n", path);
            }
        }
        if (ea->loaded[k]) {
            tileset_place(vram, &ea->tiles[k], ea->tile[k]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* ProcessRandomBattle (ps2.asm:26535)                                */
/* ------------------------------------------------------------------ */

bool battle_encounter_check(Encounter *enc, Rng *rng, const GameData *gd,
                            const Level *lv, Walk *w, bool blocked)
{
    if (blocked || !w->stepped || w->demo != NULL) {
        return false;
    }
    battle_tables_load();

    /* The roll waits for the character to be back on a cell. On foot
     * the object sits eight in from the cell corner (ObjCL_Init). */
    bool scooter = (gd->jet_scooter != 0);
    if (scooter && lv->index == 0) {
        if ((w->y & 0xF) != 0 || (w->x & 0xF) != 0) {
            return false;
        }
    } else {
        if (((w->y + 8) & 0xF) != 0 || ((w->x + 8) & 0xF) != 0) {
            return false;
        }
    }
    w->stepped = false;

    bool late = (gd->c700[0x37] != 0);      /* $FFFFC737: after Climatrol */
    int  group;

    if (lv->index == 0) {
        /* Motavia: one byte per 256x256 region, 12 columns by 8 rows.
         * loc_23C3A, +$60 on the scooter, +$C0 after Climatrol, and
         * both when both apply. */
        int table = REGION_MOTAVIA;
        if (late && scooter) {
            table = REGION_MOTAVIA_LATE_SCOOTER;
        } else if (late) {
            table = REGION_MOTAVIA_LATE;
        } else if (scooter) {
            table = REGION_MOTAVIA_SCOOTER;
        }
        int row = (w->y >> 8) & 7;
        int col = (w->x >> 8) & 0xF;
        group = g_region[table][row * 12 + col];

        enc->counter += 1;
        if (!scooter) {
            enc->counter += 1;
        }
        if (enc->hidapipe == 0) {
            enc->counter += 2;
        }
    } else if (lv->index == 3) {
        /* LevelID_DezolisSkure: 8 by 8 regions (loc_23DBA). */
        int row = (w->y >> 8) & 7;
        int col = (w->x >> 8) & 7;
        group = g_region[REGION_SKURE][row * 8 + col];

        enc->counter += 2;
        if (enc->hidapipe == 0) {
            enc->counter += 2;
        }
    } else {
        group = late ? lv->enemy_group_late : lv->enemy_group;
        if (group == 0) {
            return false;               /* a town: no counter, no roll */
        }
        enc->counter += 2;
    }

    enc->counter &= 0xFF;
    u32 d0  = rng_next32(rng);
    u16 low = (u16)(d0 & 0xFF);
    if (low >= (enc->counter >> 3)) {
        return false;
    }

    enc->counter = 0;
    int slot = (int)((d0 >> 16) & 7);
    if (group >= g_group_count) {
        return false;
    }
    enc->formation = g_group[group][slot];
    return enc->formation != 0;
}


/* ------------------------------------------------------------------ */
/* BattleScreen (12047) and loc_796 (558)                              */
/* ------------------------------------------------------------------ */

static const EnemyInfo *enemy_info(int id)
{
    return (id > 0 && id < g_enemy_count) ? &g_enemy[id] : NULL;
}

/* Decompresses (loads) a group's art into the RAM image at the tile
 * loc_796 names: 1 for the first group, $101 for the second. */
static void place_group_art(Battle *b, int group, const EnemyInfo *info)
{
    char path[96];
    TileSet art;
    memset(&art, 0, sizeof art);
    snprintf(path, sizeof path, "assets/tiles/%s.png", info->art_file);
    if (!tileset_load(&art, path)) {
        fprintf(stderr, "battle: cannot load %s (run `make assets`)\n", path);
        return;
    }
    tileset_place(&b->enemy_art, &art, 1 + group * 0x100);
    tileset_free(&art);
}

void battle_enter(Battle *b, int formation, GameData *gd, WinSys *ws,
                  Rng *rng, TileSet *vram, Video *video, Palette *loaded)
{
    battle_tables_load();

    /* loc_796 clears the object RAM and the enemy stats. */
    memset(b->obj, 0, sizeof b->obj);
    memset(b->stat, 0, sizeof b->stat);
    b->formation      = formation;
    b->phase          = BP_FADE_IN;
    b->fade_frames    = 21;
    b->dissolve_step  = 0;
    b->main_routine   = 0;
    b->fight_active   = false;
    b->fight_interrupted = false;
    b->cc06           = 0;
    b->cc90           = 0;
    b->cc92           = 0;
    b->cc94           = 0;
    b->palette_effect           = 0;
    b->chase_step      = 0;
    b->glow_step       = 0;
    b->rainbow_step    = 0;
    b->chase_lapped    = false;
    b->glow_cycling    = false;
    b->rainbow_cycling = false;
    b->battle_script  = 0;
    b->command_used   = 0;
    b->info[0]        = 0;
    b->info[1]        = 0;
    b->exp_total      = 0;
    b->meseta_total   = 0;
    b->group2_start   = 0;
    b->group_count[0] = -1;
    b->group_count[1] = -1;
    b->group_id[0]    = 0;
    b->group_id[1]    = 0;
    b->to_level_change = false;
    b->levelup_char   = 0;

    if (b->enemy_art.count == 0) {
        tileset_alloc(&b->enemy_art, 0x200);
    }
    tileset_clear(&b->enemy_art);

    /* The palette: PalettePtrs $2F, all four lines, into the loaded
     * palette the display fades toward. */
    video_colours_clear(video, loaded);
    video_colours_load(video, loaded, 0, PALETTE_BATTLE);

    /* loc_796. The ambush roll: 1 in 8, except against Mother Brain. */
    b->ambushed = false;
    if (formation != 0x103) {
        b->ambushed = ((rng_next(rng) & 7) == 0);
    }

    /* battle_character_data_buffer: a copy to restore from. */
    memcpy(b->saved, gd->chr, sizeof b->saved);

    /* Commands checked against what everyone holds (loc_F4E8). */
    battle_reset_commands(b, gd, false);
    ws->battle_cmd = (const u16 (*)[8])b->cmd;

    /* The party's objects at $E400 + $80 * id: Battle_CharCoordinates by
     * party position, Battle_CharObjectPtrs by id, art at $4000 +
     * $1000 n (tile $200 + $80 n) with the palette line in 8(a4). */
    for (int i = 0; i <= gd->party_members_num && i < 4; i++) {
        int member = gd->party_member_id[i] & 7;
        BObj *o = &b->obj[BOBJ_CHARS + 2 * member];
        memset(o, 0, sizeof *o);
        o->id   = 0xA;
        o->x    = (s16)g_char_pos[i * 2];
        o->y    = (s16)g_char_pos[i * 2 + 1];
        o->tile = (u16)((0x200 + 0x80 * i)
                        | (battle_char_palette_line(member) << 13));
        o->who  = (s16)member;
        o->map  = battle_char_map(member);
        tileset_place(vram, char_art(member), 0x200 + 0x80 * i);
    }

    /* The formation: id, count, id, count, layout. */
    const Formation *f = (formation >= 1 && formation <= g_formation_count)
                       ? &g_formation[formation - 1] : NULL;
    if (f == NULL) {
        fprintf(stderr, "battle: formation %d out of range\n", formation);
        return;
    }
    b->layout = f->layout & 0xF;
    int ids[2]    = { f->enemy1, f->enemy2 };
    int counts[2] = { f->count1, f->count2 };
    int slot = 0;
    b->escape_rate = 0x100;

    for (int g = 0; g < 2; g++) {
        const EnemyInfo *info = enemy_info(ids[g]);
        if (info == NULL || counts[g] == 0) {
            break;
        }
        b->group_id[g]    = ids[g];
        b->group_count[g] = counts[g] - 1;
        memcpy(b->name[g], info->name, BATTLE_NAME_LEN);
        int who = 0;
        if (g == 1) {
            /* The second group's objects carry the slot it starts at, and
             * any stored target of "the other group" is pointed at it. */
            who = b->group_count[0] + 1;
            b->group2_start = who;
            for (int c = 0; c < GD_CHARS; c++) {
                if (b->cmd[c][2] != 0) {
                    b->cmd[c][2] = (u16)who;
                }
            }
        }
        /* The lower of the two groups' escape rates is the battle's. */
        if (info->escape < b->escape_rate) {
            b->escape_rate = info->escape;
        }

        /* The group's fifteen colours into entries 1-15 of line 2 or 3
         * of the loaded palette ($FFFFFBC2 / $FFFFFBE2). */
        for (int k = 0; k < 15; k++) {
            video_colours_set(video, loaded, (2 + g) * PAL_ENTRIES + 1 + k,
                              info->cram[k]);
        }
        place_group_art(b, g, info);

        for (int n = 0; n < counts[g] && slot < BATTLE_ENEMIES_MAX; n++) {
            EnemyStats *st = &b->stat[slot];
            st->exp           = info->exp;
            st->meseta        = info->meseta;
            st->attack        = info->attack;
            st->defense       = info->defense;
            st->word0         = info->type;
            st->agility       = info->agility;
            st->hp            = info->hp;
            st->max_hp        = info->hp;
            st->technique     = info->technique;
            st->tech_rate     = info->technique_rate;
            st->success_index = info->success_index;

            BObj *o = &b->obj[BOBJ_ENEMIES + 2 * slot];
            memset(o, 0, sizeof *o);
            o->who        = (s16)who;
            o->id         = info->object;
            o->dur        = info->anim_duration;
            o->d12        = (s16)(info->anim_bytes[0] * 2);
            o->d1C        = (s16)(-(info->anim_bytes[1] * 2));
            o->d38        = (s16)(info->anim_bytes[2] * 2);
            o->d3A        = (s16)(-(info->anim_bytes[3] * 2));
            o->sfx_attack = info->sfx_attack;
            o->sfx_tech   = info->sfx_technique;
            o->map        = (info->map >= 0) ? &g_maps[info->map] : NULL;
            o->anim       = info->anim;
            o->x          = (s16)g_enemy_x[b->layout][slot];
            o->y          = 0xF8;
            o->tile       = (g == 0) ? 0x4000 : 0x6100;
            slot++;
        }
    }

    /* Mother Brain: the loaded palette's entry 0 cleared. */
    if (formation == 0x103) {
        video_colours_set(video, loaded, 0, 0);
    }

    /* The names the enemy boxes will show. */
    ws->enemy_name[0] = (b->group_count[0] >= 0) ? b->name[0] : NULL;
    ws->enemy_name[1] = (b->group_count[1] >= 0) ? b->name[1] : NULL;
    ws->enemy_group_id[0] = b->group_id[0];     /* (build 164) */
    ws->enemy_group_id[1] = b->group_id[1];
    ws->enemy_info    = b->info;
    ws->enemy_index   = (u8)b->group_id[0];

    /* The window queue as loc_86A2 finds it: the four stat boxes with
     * $0600 and the FIGHT/STGY box with $0700 -- already marked
     * processed, so it is drawn without its cursor. */
    for (int i = 0; i < 4; i++) {
        win_open(ws, (u16)(0x0600 | (WIN_BATTLE_CHAR_STATS_1 + i)));
    }
    win_open(ws, (u16)(0x0700 | WIN_BATTLE_OPTIONS));

    /* UpdateSoundQueue with the battle music, Rise or Fall ($92), or for a
     * boss The Place of Death ($93) (12179): queued here, before the
     * display comes back on, so the music is playing through the fade from
     * white and the dissolve. */
    b->music = (formation >= 0x100) ? MUSIC_THE_PLACE_OF_DEATH
                                    : MUSIC_RISE_OR_FALL;

    /* The revealed art starts blank: tiles 0-$1FF of VRAM. */
    for (int i = 0; i < 0x200 && i < vram->count; i++) {
        memset(vram->tiles[i].px, 0, sizeof vram->tiles[i].px);
    }
    b->art_hidden = ~(u64)0;
    b->art_ready = true;
}

/* ------------------------------------------------------------------ */
/* BattleScreen_CheckRoutine (21714)                                   */
/* ------------------------------------------------------------------ */

static const u8 FADE_ORDER_FALLBACK[64] = {
     0, 32, 16, 48,  8, 40, 24, 56,  4, 36, 20, 52, 12, 44, 28, 60,
     2, 34, 18, 50, 10, 42, 26, 58,  6, 38, 22, 54, 14, 46, 30, 62,
     1, 33, 17, 49,  9, 41, 25, 57,  5, 37, 21, 53, 13, 45, 29, 61,
     3, 35, 19, 51, 11, 43, 27, 59,  7, 39, 23, 55, 15, 47, 31, 63,
};
static const u8 *g_fade_order = FADE_ORDER_FALLBACK;

void battle_set_fade_order(const u8 *order)
{
    if (order != NULL) {
        g_fade_order = order;
    }
}

/* DetectLeadingCharacter: the first living member, in character_index. */
static void name_leader(WinSys *ws, GameData *gd)
{
    for (int i = 0; i <= gd->party_members_num && i < GD_PARTY_MAX; i++) {
        int c = gd->party_member_id[i] & 7;
        ws->character_index = (u16)c;
        if (gd->chr[c].hp != 0) {
            break;
        }
    }
}

/* RestoreCharDataAfterBattle: everything but HP, TP, experience and
 * the equipment back from the copy taken at battle start; the status
 * keeps poison and the defending bit. */
static void restore_chars(Battle *b, GameData *gd)
{
    for (int c = 0; c < GD_CHARS; c++) {
        CharData *ch = &gd->chr[c];
        const CharData *s = &b->saved[c];
        ch->status     &= 0x8100;
        ch->agility     = s->agility;
        ch->luck        = s->luck;
        ch->dexterity   = s->dexterity;
        ch->attack_base = s->attack_base;
        ch->attack      = s->attack;
        ch->defense     = s->defense;
    }
}

/* Battle_CharacterIdIndex: which party position the char-select
 * cursor's stop names. With one or two members the table starts one
 * in. */
static int cursor_stop_member(const GameData *gd, int stop)
{
    static const u8 IDX[4] = { 4, 0, 2, 6 };
    int i = stop;
    if (gd->party_members_num < 2) {
        i++;
    }
    int pos = (i < 4) ? IDX[i] / 2 : 0;
    return gd->party_member_id[pos & 3] & 7;
}

/* loc_EB78's second half, loc_EDD0, CommandAction_ItemSelectCharacter:
 * the triangle cursor over the party. */
static void spawn_char_cursor(WinSys *ws, const GameData *gd)
{
    win_spawn_triangle(ws, TRI_CHAR_SELECT, ws->depth + 1,
                       gd->party_members_num, 0x90, 0x148);
}

/* loc_EBE8's B, loc_EE4C, loc_EF84: the cursor at slot depth + 1 gone. */
static void kill_char_cursor(WinSys *ws)
{
    win_kill_triangle(ws, ws->depth + 1);
}

/* CommandAction_Defense: the command record written and the stat boxes
 * redrawn, then back to the character select. */
static void store_command(Battle *b, WinSys *ws, GameData *gd, u16 param,
                          u16 target)
{
    u16 *rec = b->cmd[ws->character_index & 7];
    rec[0] = ws->battle_cmd_pick;
    rec[1] = param;
    rec[2] = target;
    for (int i = 0; i <= gd->party_members_num && i < 4; i++) {
        win_open(ws, (u16)(0x0600 | (WIN_BATTLE_CHAR_STATS_1 + i)));
    }
    win_open(ws, 0x8402);
    ws->event_routine = (u16)(ws->event_routine - 4);
}

/* Battle_CommandActionIndex (21875): the chosen command's own states,
 * counted by event_routine_sub_2. */
static void command_action(Battle *b, WinSys *ws, GameData *gd,
                           const Input *in)
{
    CharData *ch = &gd->chr[ws->character_index & 7];
    int sub2 = ws->event_routine_sub_2;

    switch (ws->battle_cmd_pick & 3) {
        case 0: {
            /* CommandAction_Attack */
            if (sub2 == 0) {
                if (ch->equip[EQUIP_RIGHT] == 0 && ch->equip[EQUIP_LEFT] == 0) {
                    win_open(ws, WIN_BATTLE_MESSAGE);
                    win_script_set(ws, 0x1205);
                    ws->event_routine_sub++;
                    return;
                }
                if (b->group_count[0] >= 0 && b->group_count[1] >= 0) {
                    win_open(ws, WIN_ENEMY_GROUPS);
                }
                ws->character_index_2 = 0;
                ws->event_routine_sub_2++;
                return;
            }
            if (sub2 == 1) {
                if (b->group_count[0] >= 0 && b->group_count[1] >= 0) {
                    win_open(ws, 0x8001);
                }
                ws->event_routine_sub_2++;
                return;
            }
            u16 d2 = ws->character_index_2;
            if (d2 != 0) {
                d2 = (u16)b->group2_start;
            }
            store_command(b, ws, gd, 0, d2);
            return;
        }
        case 1: {
            /* CommandAction_Technique */
            if (sub2 == 0) {
                if ((ch->techs_a & 0x1F) == 0) {
                    win_open(ws, WIN_BATTLE_MESSAGE);
                    win_script_set(ws, 0x1207);
                    ws->event_routine_sub++;
                    return;
                }
                win_open(ws, WIN_BATTLE_TECH_LIST);
                ws->tech_page = 0;
                ws->event_routine_sub_2++;
                return;
            }
            if (sub2 == 1) {
                int  id   = ws->technique_index & 0x3F;
                u8   cost = gamedata_tech_tp(id);
                if (ch->tp < cost) {
                    win_open(ws, 0x8001);
                    win_open(ws, WIN_BATTLE_MESSAGE);
                    win_script_set(ws, 0x103);
                    ws->event_routine_sub++;
                    return;
                }
                u8 flags = gamedata_tech_flags(id);
                if ((flags & 3) == 0) {
                    win_open(ws, 0x8001);
                    ws->event_routine_sub_2 += 3;
                    return;
                }
                if (!(flags & 0x80)) {
                    spawn_char_cursor(ws, gd);
                    ws->event_routine_sub_2++;
                    return;
                }
                if (b->group_count[0] >= 0 && b->group_count[1] >= 0) {
                    win_open(ws, WIN_ENEMY_GROUPS);
                }
                ws->character_index_2 = 0;
                ws->event_routine_sub_2 += 2;
                return;
            }
            if (sub2 == 2) {
                /* loc_EE00: the character picked, or B back to the list. */
                if (in->pressed.c) {
                    ws->character_index_2 =
                        (u16)cursor_stop_member(gd, ws->cursor_index);
                    ws->event_routine_sub_2++;
                    kill_char_cursor(ws);
                } else if (in->pressed.b) {
                    ws->window_index_saved = WIN_BATTLE_TECH_LIST;
                    ws->event_routine_sub_2--;
                    kill_char_cursor(ws);
                }
                return;
            }
            if (sub2 == 3) {
                /* loc_EE60: the list closed, and the groups box too. */
                if (gamedata_tech_flags(ws->technique_index) & 0x80) {
                    win_open(ws, 0x8002);
                } else {
                    win_open(ws, 0x8001);
                }
                ws->event_routine_sub_2++;
                return;
            }
            store_command(b, ws, gd, (u16)(ws->technique_index & 0x3F),
                          ws->character_index_2);
            return;
        }
        case 2: {
            /* CommandAction_Item */
            if (sub2 == 0) {
                if (ch->item_count == 0) {
                    win_open(ws, WIN_BATTLE_MESSAGE);
                    win_script_set(ws, 0x23);
                    ws->event_routine_sub++;
                    return;
                }
                win_open(ws, WIN_BATTLE_ITEM_LIST);
                ws->item_page = 0;
                ws->event_routine_sub_2++;
                return;
            }
            if (sub2 == 1) {
                int item = ws->item_index & 0x7F;
                bool on_someone = (item >= gamedata_item_id("Monomate")
                                   && item <= gamedata_item_id("Antidote"))
                               || item == gamedata_item_id("MoonDew");
                if (!on_someone) {
                    win_open(ws, 0x8001);
                    ws->event_routine_sub_2 += 2;
                    return;
                }
                spawn_char_cursor(ws, gd);
                ws->event_routine_sub_2++;
                return;
            }
            if (sub2 == 2) {
                if (in->pressed.c) {
                    ws->character_index_2 =
                        (u16)cursor_stop_member(gd, ws->cursor_index);
                    win_open(ws, 0x8001);
                    ws->event_routine_sub_2++;
                    kill_char_cursor(ws);
                } else if (in->pressed.b) {
                    ws->window_index_saved = WIN_BATTLE_ITEM_LIST;
                    ws->event_routine_sub_2--;
                    kill_char_cursor(ws);
                }
                return;
            }
            store_command(b, ws, gd, (u16)(ws->item_index & 0x7F),
                          ws->character_index_2);
            return;
        }
        default: {
            store_command(b, ws, gd, 0, 0);
            return;
        }
    }
}

/* Battle_EventIndex_Standby (21743). */
static BattleResult standby(Battle *b, GameData *gd, WinSys *ws,
                            const Input *in)
{
    switch (ws->event_routine) {
        case 1: {
            /* loc_EB46 */
            win_open(ws, 0x0600 | WIN_BATTLE_OPTIONS);
            ws->event_routine = 2;
            break;
        }
        case 2: {
            /* loc_EB52: FIGHT starts the fighting routine; STGY opens
             * ORDR / RUN. */
            if (ws->battle_option == 0) {
                ws->event_routine = 0;
                b->main_routine   = 1;
                b->fight_active   = true;
                break;
            }
            win_open(ws, 0x0600 | WIN_BATTLE_OPTIONS2);
            ws->event_routine = 3;
            break;
        }
        case 3: {
            /* loc_EB78: RUN hands over to the run routine; ORDR spawns the
             * triangle cursor over the party. */
            if (ws->battle_option2 != 0) {
                ws->event_routine = 0;
                b->main_routine   = 2;
                break;
            }
            spawn_char_cursor(ws, gd);
            ws->event_routine = 4;
            break;
        }
        case 4: {
            /* loc_EBBC: a character chosen with C, or B back to FIGHT /
             * STGY. A message (sub != 0) closes itself first. */
            if (ws->event_routine_sub != 0) {
                win_open(ws, 0x8001);
                ws->event_routine_sub = 0;
                break;
            }
            if (in->pressed.b) {
                ws->event_routine = 2;
                kill_char_cursor(ws);
                break;
            }
            if (!in->pressed.c) {
                break;
            }
            /* loc_EBF8 */
            ws->char_pick = ws->cursor_index;
            int c = cursor_stop_member(gd, ws->cursor_index);
            ws->character_index = (u16)c;
            if (gd->chr[c].hp == 0) {
                win_open(ws, WIN_BATTLE_MESSAGE);
                win_script_set(ws, 6);
                ws->event_routine_sub++;
                break;
            }
            /* `move.l #((WinID_BattleCharName<<$10)|WinID_BattleCommands)`:
             * the name box first, which lets go at once, then the
             * commands, which hold the pad. */
            win_open(ws, WIN_BATTLE_CHAR_NAME);
            win_open(ws, WIN_BATTLE_COMMANDS);
            ws->event_routine_sub_2 = 0;
            ws->event_routine = 5;
            break;
        }
        case 5: {
            /* loc_EC5C: the command's own states; a message closes and
             * the commands window takes the pad again. */
            if (ws->event_routine_sub != 0) {
                win_open(ws, 0x8001);
                ws->window_index_saved  = WIN_BATTLE_COMMANDS;
                ws->event_routine_sub   = 0;
                ws->event_routine_sub_2 = 0;
                break;
            }
            command_action(b, ws, gd, in);
            break;
        }
        default: {
            ws->event_routine = 1;
            break;
        }
    }
    return BATTLE_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Battle_EventIndex_Fighting (22170)                                 */
/* ------------------------------------------------------------------ */

/* loc_F004: the turn order -- every living member by agility plus 0-15,
 * twice for the attack command, every living enemy likewise, sorted
 * highest first. Skipped when the enemies have the jump. */
static void build_turn_list(Battle *b, GameData *gd, Rng *rng)
{
    memset(b->turn, 0, sizeof b->turn);
    int n = 0;

    if (!b->ambushed) {
        for (int i = 0; i <= gd->party_members_num && i < 4; i++) {
            int c = gd->party_member_id[i] & 7;
            const CharData *ch = &gd->chr[c];
            if (ch->hp == 0) {
                continue;
            }
            u16 agi = (u16)((rng_next(rng) & 0xF) + ch->agility);
            if (n < 16) {
                b->turn[n][0] = (u16)c;
                b->turn[n][1] = agi;
                n++;
            }
            if (b->cmd[c][0] == 0 && n < 16) {
                b->turn[n][0] = (u16)c;
                b->turn[n][1] = agi;
                n++;
            }
        }
    }
    b->ambushed = false;

    /* Twelve enemy slots are scanned: the five in use and the rest
     * blank, which have no HP and are skipped. */
    for (int e = 0; e < 8; e++) {
        if (b->stat[e].hp == 0) {
            continue;
        }
        u16 agi = (u16)((rng_next(rng) & 0xF) + b->stat[e].agility);
        if (n < 16) {
            b->turn[n][0] = (u16)(e + 8);
            b->turn[n][1] = agi;
            n++;
        }
    }

    /* The sort, as written: fourteen passes of a swap with every later
     * entry, comparing agility only. */
    for (int i = 0; i < 14; i++) {
        for (int k = i + 1; k < 16; k++) {
            if (b->turn[k][1] < b->turn[i][1]) {
                continue;
            }
            u16 t0 = b->turn[i][0], t1 = b->turn[i][1];
            b->turn[i][0] = b->turn[k][0];
            b->turn[i][1] = b->turn[k][1];
            b->turn[k][0] = t0;
            b->turn[k][1] = t1;
        }
    }
    b->cc90 = 0;
}

static bool any_effect_running(const Battle *b)
{
    for (int i = BOBJ_EFFECTS; i < BOBJ_COUNT; i++) {
        if (b->obj[i].id != 0) {
            return true;
        }
    }
    return false;
}

static bool any_enemy_left(const Battle *b)
{
    for (int e = 0; e < 8; e++) {
        if (b->obj[BOBJ_ENEMIES + 2 * e].id != 0) {
            return true;
        }
    }
    return false;
}

static bool party_standing(const GameData *gd)
{
    for (int i = 0; i <= gd->party_members_num && i < 4; i++) {
        const CharData *ch = &gd->chr[gd->party_member_id[i] & 7];
        if (!(ch->status & 0x4000) && ch->hp != 0) {
            return true;
        }
    }
    return false;
}

/* GenerateRandStatIncrease: the gain times 128-383, over 256 -- ext.w
 * on the gain, mulu.w, lsr.w #8 on the low word. No gain in the level
 * tables reaches $80, so the sign and the wrap never show. */
static u16 rand_gain(Rng *rng, u8 gain)
{
    u16 d0 = (u16)((rng_next(rng) & 0xFF) + 0x80);
    u16 d2 = (u16)(s16)(s8)gain;
    return (u16)((((u32)d2 * d0) & 0xFFFF) >> 8);
}

void battle_apply_level_row(GameData *gd, int c, const LevelRow *row,
                            Rng *rng, bool *hp_up, bool *tp_up,
                            bool *techs_up)
{
    CharData *ch = &gd->chr[c];
    ch->level = row->level;

    u16 gain = rand_gain(rng, row->hp);
    *hp_up = (gain != 0);
    ch->max_hp = (u16)(ch->max_hp + gain);
    gain = rand_gain(rng, row->tp);
    *tp_up = (gain != 0);
    ch->max_tp    = (u16)(ch->max_tp + gain);
    ch->strength  = (u16)(ch->strength + rand_gain(rng, row->strength));
    ch->mental    = (u16)(ch->mental + rand_gain(rng, row->mental));
    ch->agility   = (u16)(ch->agility + rand_gain(rng, row->agility));
    ch->luck      = (u16)(ch->luck + rand_gain(rng, row->luck));
    ch->dexterity = (u16)(ch->dexterity + rand_gain(rng, row->dexterity));
    ch->attack_base = (u16)(ch->attack_base + rand_gain(rng, row->attack));
    ch->defense   = (u16)(ch->defense + rand_gain(rng, row->defense));
    *techs_up = (row->techs_a != 0 || row->techs_b != 0);
    if (*techs_up) {
        ch->techs_a = (u8)(ch->techs_a + row->techs_a);
        ch->techs_b = (u8)(ch->techs_b + row->techs_b);
    }
}

/* loc_F328 / Character_ProcessLevelUp: one level a call, the equipment
 * bonuses off around the check. A level gained returns through
 * Character_ProcessLevelUp's own rts with $FFFFC602 left alone, so the
 * same character is looked at again once the message is done; only a
 * call that finds nothing to gain (loc_F372) moves on to the next. */
static void level_up_step(Battle *b, GameData *gd, WinSys *ws, Rng *rng,
                          void (*sound)(void *ctx, u8 id), void *ctx)
{
    int c = b->levelup_char;
    CharData *ch = &gd->chr[c];
    gamedata_equip_bonuses_remove(gd, c);
    ws->character_index = (u16)c;

    if (ch->level != 0x32) {
        u32 need = gamedata_level_exp(c, ch->level) & 0xFFFFFF;
        LevelRow row;
        if (need <= ch->experience
            && gamedata_level_row(c, ch->level, &row)) {
            if (sound != NULL) {
                sound(ctx, 0xA1);           /* SXFID_LevelUp */
            }
            win_script_set(ws, 0x1214);

            /* The gains are rolled in the ROM's order; the scripts only
             * queue, so taking them after changes nothing. */
            bool hp_up, tp_up, techs_up;
            battle_apply_level_row(gd, c, &row, rng, &hp_up, &tp_up,
                                   &techs_up);
            if (hp_up) {
                win_script(ws, 0x1215);
            }
            if (tp_up) {
                win_script(ws, 0x1216);
            }
            if (techs_up) {
                win_script(ws, 0x1217);
            }

            /* loc_F41E: bonuses back on, and out without moving on. */
            gamedata_equip_bonuses_add(gd, c);
            return;
        }
    }

    /* loc_F372 */
    gamedata_equip_bonuses_add(gd, c);
    b->levelup_char++;
    if (b->levelup_char == 8) {
        ws->event_routine++;
    }
}

static BattleResult fighting(Battle *b, GameData *gd, WinSys *ws, Rng *rng,
                             void (*sound)(void *ctx, u8 id), void *ctx)
{
    switch (ws->event_routine) {
        case 1: {
            build_turn_list(b, gd, rng);
            ws->event_routine = 2;
            break;
        }
        case 2: {
            /* loc_F0C0: nothing in flight, the boxes redrawn, then is the
             * party standing and are enemies left. */
            if (any_effect_running(b)) {
                break;
            }
            win_open(ws, 0x8002);
            for (int i = 0; i < 4; i++) {
                win_open(ws, (u16)(0x0600 | (WIN_BATTLE_CHAR_STATS_1 + i)));
            }
            if (b->formation < 0x102) {
                win_open(ws, 0x0600 | WIN_ENEMY_INFO_1);
                win_open(ws, 0x0600 | WIN_ENEMY_INFO_2);
            }
            b->info[0] = 0;
            b->info[1] = 0;
            if (!party_standing(gd)) {
                b->fight_active      = false;
                b->fight_interrupted = false;
                ws->event_routine    = 5;
                break;
            }
            if (!any_enemy_left(b)) {
                b->fight_active      = false;
                b->fight_interrupted = false;
                ws->event_routine    = 6;
                b->cc94              = 0x1E;
                if (b->formation == 0x100) {
                    /* Neifirst (loc_F134): `move.b #$87, $FFFFF640.w` --
                     * the latch alone, nothing queued -- so the level's
                     * Advanced matches it on the reload and loc_7E02's
                     * Power plays on. */
                    if (ws->music_latch != NULL) {
                        ws->music_latch(ws->ctx, 0x87);
                    }
                }
                break;
            }
            for (int c = 0; c < GD_CHARS; c++) {
                b->obj[BOBJ_CHARS + 2 * c].f = 0;
            }
            ws->event_routine = 3;
            break;
        }
        case 3: {
            /* loc_F188: the next entry of the turn list acts. */
            int i = b->cc90 & 0xF;
            u16 id  = b->turn[i][0];
            u16 agi = b->turn[i][1];
            if (id == 0 && agi == 0) {
                ws->event_routine = 4;
                break;
            }
            BObj *o = &b->obj[BOBJ_CHARS + 2 * (id & 0x1F)];
            b->cc90 = (u16)((b->cc90 + 1) & 0xF);
            if (o->id == 0 || o->state == 3) {
                break;
            }
            o->f |= 0x01;
            b->cc06 = 1;
            ws->event_routine = 2;
            break;
        }
        case 4: {
            /* loc_F1D0: a button during the round stops the fight; else
             * the same commands run again. */
            if (b->fight_interrupted) {
                b->fight_active      = false;
                b->fight_interrupted = false;
                b->main_routine      = 0;
                ws->event_routine    = 0;
                break;
            }
            ws->event_routine = 1;
            break;
        }
        case 5: {
            /* loc_F1F6: everyone down. */
            if (ws->event_routine_sub != 0) {
                restore_chars(b, gd);
                return BATTLE_GAME_OVER;
            }
            ws->character_index = 0;
            if (b->formation == 0x100) {
                /* Neifirst (loc_F1F6, 22375): the party ObjType_Events
                 * $25 saved to $C618/$C61C/$C624 (GD_PARTY_HELD,
                 * GD_PARTY_HELD_NUM) comes back before anyone is counted,
                 * so Nei alone going down is not a wipe. */
                for (int i = 0; i < GD_PARTY_MAX; i++) {
                    gd->party_member_id[i] =
                        gamedata_word(gd, GD_PARTY_HELD + 2 * i);
                }
                gd->party_members_num = gamedata_word(gd, GD_PARTY_HELD_NUM);
            }
            win_open(ws, WIN_BATTLE_MESSAGE);
            win_script_set(ws, 0x121C);
            ws->event_routine_sub++;
            bool alive = false;
            for (int i = 0; i <= gd->party_members_num && i < 4; i++) {
                if (gd->chr[gd->party_member_id[i] & 7].hp != 0) {
                    alive = true;
                }
            }
            if (!alive) {
                win_script_set(ws, 0x1208);
                break;
            }
            if (b->formation == 0x100) {
                /* loc_F24E: the latch again, then ScreenID_Level. */
                if (ws->music_latch != NULL) {
                    ws->music_latch(ws->ctx, 0x87);
                }
                restore_chars(b, gd);
                return BATTLE_TO_LEVEL;
            }
            break;
        }
        case 6: {
            /* loc_F270: thirty frames, then the victory. */
            if (b->cc94 != 0) {
                b->cc94--;
                if (b->cc94 != 0) {
                    break;
                }
            }
            if (sound != NULL) {
                sound(ctx, 0x94);               /* MusicID_Movement */
            }
            name_leader(ws, gd);
            win_open(ws, WIN_BATTLE_MESSAGE);
            win_script_set2(ws, 0x1211, 0x1212);
            int alive = 0;
            for (int i = 0; i <= gd->party_members_num && i < 4; i++) {
                if (gd->chr[gd->party_member_id[i] & 7].hp != 0) {
                    alive++;
                }
            }
            if (alive != 0) {
                /* exp_points_buffer is meseta_value: {meseta} shows it. */
                u32 share = (b->exp_total / (u32)alive) & 0xFFFF;
                share++;
                ws->meseta_value = share;
                for (int i = 0; i <= gd->party_members_num && i < 4; i++) {
                    CharData *ch = &gd->chr[gd->party_member_id[i] & 7];
                    if (ch->hp == 0) {
                        continue;
                    }
                    ch->experience += share;
                    if (ch->experience >= 0x98967F) {
                        ch->experience = 0x98967F;
                    }
                }
            }
            ws->event_routine = 7;
            break;
        }
        case 7: {
            /* loc_F308: the meseta. */
            win_script_set(ws, 0x1213);
            gd->money += b->meseta_total;
            ws->meseta_value = b->meseta_total;
            b->levelup_char = 0;
            ws->event_routine = 8;
            restore_chars(b, gd);
            break;
        }
        case 8: {
            level_up_step(b, gd, ws, rng, sound, ctx);
            break;
        }
        default: {
            /* loc_F428 */
            return BATTLE_TO_LEVEL;
        }
    }
    return BATTLE_RUNNING;
}

/* Battle_EventIndex_RunOption (22580). */
static BattleResult run_option(Battle *b, GameData *gd, WinSys *ws, Rng *rng)
{
    switch (ws->event_routine) {
        case 1: {
            /* Run_EventIndex_TryRun: "<leader> and friends run away!",
             * then the roll against the escape rate. */
            name_leader(ws, gd);
            win_script_set(ws, 0x1202);
            win_open(ws, WIN_BATTLE_MESSAGE);
            u16 roll = (u16)(rng_next(rng) & 0xFF);
            if (roll > b->escape_rate) {
                ws->event_routine++;        /* failed */
            }
            ws->event_routine++;
            break;
        }
        case 2: {
            /* Run_EventIndex_RunSuccessful */
            restore_chars(b, gd);
            return BATTLE_TO_LEVEL;
        }
        case 3: {
            /* Run_EventIndex_RunFailed: "But they're trapped!" */
            win_script_set(ws, 0x1203);
            win_open(ws, WIN_BATTLE_MESSAGE);
            ws->event_routine++;
            break;
        }
        case 4: {
            /* Run_EventIndex_RunEnemiesAttack: one round of the enemies
             * only, interrupted after it so the commands come back. */
            b->ambushed          = true;
            b->main_routine      = 1;
            b->fight_active      = true;
            b->fight_interrupted = true;
            ws->event_routine    = 1;
            break;
        }
        default: {
            ws->event_routine = 1;
            break;
        }
    }
    return BATTLE_RUNNING;
}

static BattleResult check_routine(Battle *b, GameData *gd, WinSys *ws,
                                  Rng *rng, const Input *in,
                                  void (*sound)(void *ctx, u8 id), void *ctx)
{
    if (!win_idle(ws) || b->cc06 != 0) {
        return BATTLE_RUNNING;
    }
    if (ws->event_routine == 0) {
        ws->event_routine     = 1;
        ws->event_routine_sub = 0;
        ws->event_routine_sub_2 = 0;
        return BATTLE_RUNNING;
    }
    switch (b->main_routine) {
        case 0:
            return standby(b, gd, ws, in);
        case 1:
            return fighting(b, gd, ws, rng, sound, ctx);
        default:
            return run_option(b, gd, ws, rng);
    }
}

BattleResult battle_tick(Battle *b, GameData *gd, WinSys *ws, Rng *rng,
                         TileSet *vram, Video *video, const Palette *loaded,
                         const Input *in,
                         void (*sound)(void *ctx, u8 id), void *ctx)
{
    switch (b->phase) {
        case BP_FADE_IN: {
            /* loc_62CC: 21 frames down from white. The stat boxes were
             * drained before this began. */
            video_fade_step(video, VIDEO_FX_FROM_WHITE, loaded);
            if (--b->fade_frames <= 0) {
                video_shown_from_target(video, loaded);
                b->phase = BP_DISSOLVE;
            }
            return BATTLE_RUNNING;
        }
        case BP_DISSOLVE: {
            /* loc_86DA: RunObjects and one FadeArtTiles pass a frame,
             * over the 64 positions of FadeEffectArray. */
            bobj_run(b, gd, ws, rng, vram, video, sound, ctx);
            tileset_reveal_pixel(vram, &b->enemy_art, 0, 0x200,
                                 g_fade_order[b->dissolve_step]);
            b->art_hidden &= ~((u64)1 << g_fade_order[b->dissolve_step]);
            if (++b->dissolve_step >= 64) {
                b->phase = BP_LOOP;
                /* 12214-12220: Mother Brain's palette effect, then the
                 * four enemy boxes -- both groups, whether or not there
                 * is a second -- except for Dark Force and Mother Brain. */
                if (b->formation == 0x103) {
                    b->palette_effect = BATTLE_FX_MOTHER_BRAIN;
                }
                if (b->formation < 0x102) {
                    win_open(ws, 0x0400 | WIN_ENEMY_NAME_1);
                    win_open(ws, 0x0400 | WIN_ENEMY_NAME_2);
                    win_open(ws, 0x0400 | WIN_ENEMY_INFO_1);
                    win_open(ws, 0x0400 | WIN_ENEMY_INFO_2);
                }
                ws->event_routine = 0;
            }
            return BATTLE_RUNNING;
        }
        default: {
            /* BattleScreenLoop: RunObjects, then the event routine, then
             * a button during a round interrupts it. */
            bobj_run(b, gd, ws, rng, vram, video, sound, ctx);
            if (getenv("PS2_FTRACE") != NULL && b->main_routine == 1) {
                fprintf(stderr, "ft ev %d sub %d cc06 %d cc90 %d act %d int %d"
                                " q %04X saved %02X hp", ws->event_routine,
                        ws->event_routine_sub, b->cc06, b->cc90,
                        b->fight_active, b->fight_interrupted,
                        ws->window_index[0], ws->window_index_saved);
                for (int i = 0; i <= gd->party_members_num && i < 4; i++) {
                    fprintf(stderr, " %d", gd->chr[gd->party_member_id[i] & 7].hp);
                }
                fprintf(stderr, " | ");
                for (int e = 0; e < 5; e++) {
                    const BObj *o = &b->obj[BOBJ_ENEMIES + 2 * e];
                    if (o->id != 0 || b->stat[e].hp != 0) {
                        fprintf(stderr, "e%d:%d/%02X/s%d/f%02X ", e,
                                b->stat[e].hp, o->r, o->state, o->f);
                    }
                }
                for (int c = 0; c < 8; c++) {
                    const BObj *o = &b->obj[BOBJ_CHARS + 2 * c];
                    if (o->id != 0) {
                        fprintf(stderr, "c%d:%02X/s%d/f%02X/x%X/a%d ", c,
                                o->r, o->state, o->f, o->x, o->anim);
                    }
                    const BObj *k = &b->obj[BOBJ_CHARS + 2 * c + 1];
                    if (k->id != 0) {
                        fprintf(stderr, "k%d:id%X/s%d/p%d ", c, k->id,
                                k->state, k->pos);
                    }
                }
                for (int i = BOBJ_EFFECTS; i < BOBJ_COUNT; i++) {
                    if (b->obj[i].id != 0) {
                        fprintf(stderr, "fx%d:id%X/a%d/p%d ", i - BOBJ_EFFECTS,
                                b->obj[i].id, b->obj[i].anim, b->obj[i].pos);
                    }
                }
                fprintf(stderr, "\n");
            }
            BattleResult r = check_routine(b, gd, ws, rng, in, sound, ctx);
            palette_effects(b, video);
            if (b->fight_active
                && (in->pressed.up || in->pressed.down || in->pressed.left
                    || in->pressed.right || in->pressed.a || in->pressed.b
                    || in->pressed.c)) {
                b->fight_interrupted = true;
            }
            return r;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The battle's palette effects (loc_67B8, ps2.asm:9590)              */
/* ------------------------------------------------------------------ */

/* The Neisword's light: the backdrop colour, one step every other frame
 * (loc_694C), then the effect ends -- white-hot and back to dark blue. */
static const u16 NEI_SWORD_LIGHT[6] = {
    0x0620, 0x0A62, 0x0EA6, 0x0EEA, 0x0EEE, 0x0200,
};

/* Mother Brain's lights. The chase: one of line 3's entries 8-15 lit at
 * a time, from the first table for its first lap (loc_6958) and the
 * second after (loc_6978). */
static const u16 CHASE_FIRST_LAP[16] = {
    0x0400, 0x0400, 0x0400, 0x0400, 0x0640, 0x0640, 0x0640, 0x0640,
    0x0862, 0x0862, 0x0862, 0x0862, 0x0C84, 0x0C84, 0x0C84, 0x0C84,
};
static const u16 CHASE_AFTER[16] = {
    0x0862, 0x0C84, 0x0862, 0x0C84, 0x0862, 0x0C84, 0x0862, 0x0C84,
    0x0862, 0x0C84, 0x0862, 0x0C84, 0x0862, 0x0C84, 0x0862, 0x0C84,
};
/* The glow: line 2's entries 8-15, eight rows fading in from dark blue
 * to near-white (loc_6998), then a window of eight sliding back and forth
 * along a white-cyan-blue ramp (loc_6A18). */
static const u16 GLOW_FADE_IN[8][8] = {
    { 0x0400, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
    { 0x0600, 0x0400, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
    { 0x0A00, 0x0600, 0x0400, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
    { 0x0E20, 0x0A00, 0x0600, 0x0400, 0x0000, 0x0000, 0x0000, 0x0000 },
    { 0x0E60, 0x0E20, 0x0A00, 0x0600, 0x0400, 0x0000, 0x0000, 0x0000 },
    { 0x0EA0, 0x0E60, 0x0E20, 0x0A00, 0x0600, 0x0400, 0x0000, 0x0000 },
    { 0x0EE0, 0x0EA0, 0x0E60, 0x0E20, 0x0A00, 0x0600, 0x0400, 0x0000 },
    { 0x0EE6, 0x0EE0, 0x0EA0, 0x0E60, 0x0E20, 0x0A00, 0x0600, 0x0400 },
};
static const u16 GLOW_PULSE[22] = {
    0x0EEE, 0x0EE6, 0x0EE0, 0x0EA0, 0x0E60, 0x0E20, 0x0A00, 0x0600,
    0x0A00, 0x0E20, 0x0E60, 0x0EA0, 0x0EE0, 0x0EE6, 0x0EEE, 0x0EE6,
    0x0EE0, 0x0EA0, 0x0E60, 0x0E20, 0x0A00, 0x0600,
};
/* The rainbow: line 2's entries 1-6, six rows fading in through the
 * blues (loc_6A44), then a window of six sliding back along the rainbow
 * -- blue, cyan, green, yellow, orange, red, pink, purple -- one colour
 * a step (loc_6A8C). */
static const u16 RAINBOW_FADE_IN[6][6] = {
    { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0200 },
    { 0x0000, 0x0000, 0x0000, 0x0000, 0x0200, 0x0600 },
    { 0x0000, 0x0000, 0x0000, 0x0200, 0x0600, 0x0820 },
    { 0x0000, 0x0000, 0x0200, 0x0600, 0x0820, 0x0C40 },
    { 0x0000, 0x0200, 0x0600, 0x0820, 0x0C40, 0x0E60 },
    { 0x0200, 0x0600, 0x0820, 0x0C40, 0x0E60, 0x0E80 },
};
static const u16 RAINBOW[42] = {
    0x0600, 0x0820, 0x0C40, 0x0E60, 0x0E80, 0x0EA2, 0x08EC, 0x04E8,
    0x00C6, 0x00A4, 0x0062, 0x0040, 0x0026, 0x0048, 0x006A, 0x00AE,
    0x02EE, 0x08EE, 0x04EE, 0x00AE, 0x006E, 0x004C, 0x0028, 0x0006,
    0x0004, 0x0006, 0x0008, 0x002A, 0x024C, 0x048E, 0x0C8E, 0x084C,
    0x060A, 0x0408, 0x0206, 0x0004, 0x0600, 0x0820, 0x0C40, 0x0E60,
    0x0E80, 0x0EA2,
};

#define PAL_BACKDROP      0x00  /* palette_table, $FB00 */
#define PAL_CHASE         0x38  /* $FB70: line 3, entries 8-15 */
#define PAL_GLOW          0x28  /* $FB50: line 2, entries 8-15 */
#define PAL_RAINBOW       0x21  /* $FB42: line 2, entries 1-6  */
#define PAL_HIT_FLASH     0x0E  /* the backdrop while a hit flashes red */

/* loc_67FE: every fourth frame. The chase first -- unless a hit on the
 * party has the backdrop red: the last lit entry off, the next on, and
 * the first lap's end starts the rest. Then the glow's fade-in, eight
 * steps; after it, the glow's slide and the rainbow together: the
 * rainbow's fade-in (six steps) and then its slide. A slide steps back
 * two bytes a time and starts again from its far end when it passes 0. */
static void mother_brain_lights(Battle *b, Video *video)
{
    if (--b->cycle_wait >= 0) {
        return;
    }
    b->cycle_wait = 3;

    if (!video_backdrop_is(video, PAL_HIT_FLASH)) {
        u16 d1 = b->chase_step;
        b->chase_step = (u16)(b->chase_step + 2);
        const u16 *table = b->chase_lapped ? CHASE_AFTER : CHASE_FIRST_LAP;
        video_glow(video, PAL_CHASE + (((d1 - 2) & 0xE) >> 1), 0);
        if ((d1 & 0x1E) == 0x1E) {
            b->chase_lapped = true;
        }
        video_glow(video, PAL_CHASE + ((d1 & 0xE) >> 1),
                   table[(d1 & 0x1E) >> 1]);
    }
    if (!b->chase_lapped) {
        return;
    }

    if (!b->glow_cycling) {
        u16 d0 = b->glow_step;
        b->glow_step = (u16)(b->glow_step + 2);
        if (d0 == 0xE) {
            b->glow_step    = 0;
            b->glow_cycling = true;
        }
        for (int i = 0; i < 8; i++) {
            video_glow(video, PAL_GLOW + i, GLOW_FADE_IN[(d0 & 0xE) >> 1][i]);
        }
        return;
    }

    u16 d0 = b->glow_step;
    if (b->glow_step < 2) {
        b->glow_step = 0x1A;
    } else {
        b->glow_step = (u16)(b->glow_step - 2);
    }
    for (int i = 0; i < 8; i++) {
        video_glow(video, PAL_GLOW + i, GLOW_PULSE[((d0 & 0x1E) >> 1) + i]);
    }

    if (!b->rainbow_cycling) {
        d0 = b->rainbow_step;
        b->rainbow_step = (u16)(b->rainbow_step + 2);
        if (b->rainbow_step == 0xC) {
            b->rainbow_step    = 0;
            b->rainbow_cycling = true;
        }
        for (int i = 0; i < 6; i++) {
            video_glow(video, PAL_RAINBOW + i,
                       RAINBOW_FADE_IN[(d0 & 0xE) >> 1][i]);
        }
        return;
    }
    d0 = b->rainbow_step;
    if (b->rainbow_step < 2) {
        b->rainbow_step = 0x44;
    } else {
        b->rainbow_step = (u16)(b->rainbow_step - 2);
    }
    for (int i = 0; i < 6; i++) {
        video_glow(video, PAL_RAINBOW + i, RAINBOW[((d0 & 0xFE) >> 1) + i]);
    }
}

/* loc_67B8, once a frame of the battle's loop, after its routine. */
static void palette_effects(Battle *b, Video *video)
{
    if (b->palette_effect == BATTLE_FX_NEI_SWORD) {
        /* The Neisword's light: every other frame the next backdrop
         * colour; the seventh step ends the effect. */
        if (--b->cycle_wait >= 0) {
            return;
        }
        b->cycle_wait = 1;
        u16 d0 = b->cycle_step++;
        if ((d0 & 7) == 6) {
            b->palette_effect = 0;
            return;
        }
        video_backdrop(video, NEI_SWORD_LIGHT[(d0 & 7) % 6]);
        return;
    }
    if (b->palette_effect == BATTLE_FX_MOTHER_BRAIN) {
        mother_brain_lights(b, video);
    }
}

bool battle_in_loop(const Battle *b)
{
    return b->phase == BP_LOOP;
}

/* ------------------------------------------------------------------ */
/* Drawing: BuildSprites (343)                                        */
/* ------------------------------------------------------------------ */

/* loc_572: the block at (x + dx - $80) / 8, (y + dy - $80) / 8, cells
 * that are zero left alone, the rest with the object's tile word. */
static void draw_block(Video *v, const BObj *e, const MapFrame *f)
{
    int col0 = (e->x + f->x - 0x80) >> 3;
    int row0 = (e->y + f->y - 0x80) >> 3;

    for (int r = 0; r < f->h; r++) {
        for (int c = 0; c < f->w; c++) {
            u16 word = f->cell[r * f->w + c];
            if (word == 0) {
                continue;
            }
            word = (u16)(word + e->tile);
            Cell cell;
            cell.tile   = (u16)(word & 0x07FF);
            cell.pal    = (u8)((word >> 13) & 3);
            cell.prio   = (u8)(word >> 15);
            cell.flip_h = (u8)((word >> 11) & 1);
            cell.flip_v = (u8)((word >> 12) & 1);
            int col = col0 + c;
            int row = row0 + r;
            if (col >= 0 && col < PLANE_COLS && row >= 0 && row < 17) {
                video_set_tile(v, LAYER_A, col, row, cell);
            }
        }
    }
}

/* loc_526: sprite pieces around the object's position. */
static void draw_pieces(Video *v, int x, int y, u16 tile_word,
                        const Piece *piece, int count)
{
    for (int i = 0; i < count; i++) {
        const Piece *p = &piece[i];
        u16 word = (u16)(p->tile + tile_word);
        Sprite s;
        memset(&s, 0, sizeof s);
        s.x      = (s16)(x + p->x - 0x80);
        s.y      = (s16)(y + p->y - 0x80);
        s.tile   = (u16)(word & 0x07FF);
        s.w      = (u8)(((p->size >> 2) & 3) + 1);
        s.h      = (u8)((p->size & 3) + 1);
        s.pal    = (u8)((word >> 13) & 3);
        s.prio   = (u8)(word >> 15);
        s.flip_h = (u8)((word >> 11) & 1);
        s.flip_v = (u8)((word >> 12) & 1);
        video_sprite_push(v, &s);
    }
}

/* RunObject's tail: is the object on screen, and which of the 64 y
 * buckets does it list in -- 0 for the front-most, else higher y first. */
static int bucket_of(const BObj *o)
{
    if (o->id == 0 || (o->r & 0x02)) {
        return -1;
    }
    if (o->x <= 0x40 || o->x >= 0x1E0) {
        return -1;
    }
    if (o->y <= 0x40 || o->y < 0 || o->y >= 0x180) {
        return -1;
    }
    if (o->r & 0x20) {
        return 0;
    }
    return ((~(o->y - 0x80)) & 0xFC) >> 2;
}

static void draw_object(Video *v, const BObj *o)
{
    if (o->map == NULL || o->frame < 0 || o->frame >= o->map->frame_count) {
        return;
    }
    const MapFrame *f = &o->map->frame[o->frame];
    if (f->block) {
        draw_block(v, o, f);
    } else {
        draw_pieces(v, o->x, o->y, o->tile, f->piece, f->piece_count);
    }
}

void battle_render(const Battle *b, Video *v, TileSet *vram)
{
    (void)vram;

    /* Plane A rows 0-16 are the objects' and nothing else's: the buffer
     * at $FF6000 is cleared and rebuilt every frame. */
    Cell blank;
    memset(&blank, 0, sizeof blank);
    for (int r = 0; r < 17; r++) {
        for (int c = 0; c < PLANE_COLS; c++) {
            video_set_tile(v, LAYER_A, c, r, blank);
        }
    }

    /* BuildSprites walks the buckets from 0, each in RunObjects order:
     * the first sprite built is the one in front. */
    for (int bucket = 0; bucket < 64; bucket++) {
        for (int i = BOBJ_CHARS; i < BOBJ_COUNT; i++) {
            const BObj *o = &b->obj[i];
            if (bucket_of(o) == bucket) {
                draw_object(v, o);
            }
        }
    }
}
