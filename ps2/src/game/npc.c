#include "npc.h"
#include "rom_ids.h"
#include "script_ids.h"

#include "../core/json.h"
#include "gamedata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WAIT_FRAMES   0x20
#define STEP_FRAMES   0x0F
#define ANIM_FRAMES   4
#define RANGE_CELLS   5
#define SPRITE_BIAS   0x80

enum { FACE_UP = 0, FACE_DOWN = 3, FACE_LEFT = 6, FACE_RIGHT = 9 };
enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

typedef struct {
    int object, interaction, index, x, y;
} Placement;

typedef struct {
    int       art_list;
    int       count;
    Placement at[NPC_MAX];
} LevelPeople;

typedef struct {
    char    label[24];
    TileSet tiles;
} PeopleArt;

typedef struct {
    int count;
    int art[NPC_LIST_ART];      /* index into g_art */
    int tile[NPC_LIST_ART];
} ArtList;

static NpcType     g_type[NPC_TYPES_MAX];
static int         g_type_count;
static LevelPeople g_level[NPC_LEVELS_MAX];
static PeopleArt   g_art[NPC_ART_MAX];
static int         g_art_count;
static ArtList     g_list[NPC_LISTS_MAX];
static int         g_list_count;

static u8  g_talk_after[256], g_talk_before[256], g_talk_later[256];
static int g_talk_after_n, g_talk_before_n, g_talk_later_n;

#define CHEST_TYPES_MAX 4
static NpcChestType g_chest_type[CHEST_TYPES_MAX];
static int          g_chest_type_count;
static u16          g_chest_content[NPC_CHESTS_MAX];

#define SCENE_TYPES_MAX 32     /* 22 since build 87 (was 16) */
static NpcSceneType g_scene_type[SCENE_TYPES_MAX];
static int          g_scene_type_count;

/* loc_7278: a box a row -- x offset, width - 1, y offset, height - 1 --
 * about an object's position. Rows 0-3 are the cell a mover is about to
 * enter (up, down, left, right); from 4 on, what an object puts in $2C
 * to say how big it is to everyone else. */
static const s16 BOX[14][4] = {
    {  -8,  15, -24, 15 }, {  -8,  15,   8, 15 },
    { -24,  15,  -8, 15 }, {   8,  15,  -8, 15 },
    {  -8,  15,  -8, 15 }, { -16,  31, -16, 31 },
    { -16,  31,  -8,  8 }, { -16,  31,   0, 15 },
    {  -8,  63, -48, 95 }, { -32,  63, -64, 63 },
    { -88, 176, -24, 40 }, { -32,  56,  -8, 16 },
    { -16,  31,  -8,  7 }, { -16,  31,  -8, 15 },
};
#define BOX_PERSON 4

/* Row -1, what `adda.w d6, a2` reaches with d6 = $FFFF: the four bytes
 * before loc_7278, `moveq #-2, d4` ($78FE) and `rts` ($4E75), the tail of
 * Level_ChkTargetInteract itself. A box well below and to the right. */
static const s16 BOX_BEFORE[4] = { 0x78, 0xFE, 0x4E, 0x75 };

/* ------------------------------------------------------------------ */
/* Tables                                                             */
/* ------------------------------------------------------------------ */

static int art_index(const char *label)
{
    for (int i = 0; i < g_art_count; i++) {
        if (strcmp(g_art[i].label, label) == 0) {
            return i;
        }
    }
    if (g_art_count >= NPC_ART_MAX) {
        return -1;
    }

    char path[96];
    snprintf(path, sizeof path, "assets/tiles/sprite_%s.png", label);
    PeopleArt *a = &g_art[g_art_count];
    snprintf(a->label, sizeof a->label, "%s", label);
    if (!tileset_load(&a->tiles, path)) {
        return -1;
    }
    return g_art_count++;
}

static void read_piece(const JsonValue *p, NpcPiece *piece)
{
    piece->x      = (s8)json_int(p, "x", 0);
    piece->y      = (s8)json_int(p, "y", 0);
    piece->w      = (u8)json_int(p, "w", 1);
    piece->h      = (u8)json_int(p, "h", 1);
    piece->tile   = (u16)json_int(p, "tile", 0);
    piece->flip_h = json_bool(p, "flip_h", false);
    piece->flip_v = json_bool(p, "flip_v", false);
}

/* Two frames of up to NPC_CHEST_PIECES pieces each. */
static void read_chest_frames(const JsonValue *frames, int pieces[2],
                              NpcPiece out[2][NPC_CHEST_PIECES])
{
    int f = 0;
    for (const JsonValue *fr = (frames != NULL) ? frames->child : NULL;
         fr != NULL && f < 2; fr = fr->next, f++) {
        int n = 0;
        for (const JsonValue *p = fr->child;
             p != NULL && n < NPC_CHEST_PIECES; p = p->next, n++) {
            read_piece(p, &out[f][n]);
        }
        pieces[f] = n;
    }
}

/* assets/data/chests.json: the two chest objects and every chest's
 * contents, by index. */
static bool load_chests(void)
{
    JsonValue *root = json_parse_file("assets/data/chests.json");
    if (root == NULL) {
        fprintf(stderr, "npc: assets/data/chests.json is missing or will "
                        "not parse -- rerun tools/extract_npcs.py "
                        "(make assets)\n");
        return false;
    }

    g_chest_type_count = 0;
    const JsonValue *types = json_get(root, "types");
    for (const JsonValue *t = (types != NULL) ? types->child : NULL;
         t != NULL && g_chest_type_count < CHEST_TYPES_MAX; t = t->next) {
        NpcChestType *ct = &g_chest_type[g_chest_type_count++];
        memset(ct, 0, sizeof *ct);
        ct->object = (int)json_int(t, "object", 0);
        ct->tile   = (int)json_int(t, "tile", 0);
        ct->line   = (int)json_int(t, "palette_line", 0);
        ct->box    = (int)json_int(t, "box", BOX_PERSON);
        if (ct->box < 0 || ct->box >= 14) {
            ct->box = BOX_PERSON;
        }
        read_chest_frames(json_get(t, "frames"), ct->pieces, ct->frame);
        ct->alt_level_type = (int)json_int(t, "alt_level_type", -1);
        read_chest_frames(json_get(t, "alt_frames"), ct->alt_pieces,
                          ct->alt_frame);
    }

    memset(g_chest_content, 0, sizeof g_chest_content);
    const JsonValue *contents = json_get(root, "contents");
    for (const JsonValue *c = (contents != NULL) ? contents->child : NULL;
         c != NULL; c = c->next) {
        long index = json_int(c, "index", 0);
        if (index <= 0 || index >= NPC_CHESTS_MAX) {
            continue;
        }
        u16 word = 0;
        const char *item = json_str(c, "item", NULL);
        if (item != NULL) {
            word = (u16)(0x8000 | (gamedata_item_id(item) & 0xFF));
        } else if (!json_bool(c, "garbage", false)) {
            word = (u16)json_int(c, "meseta", 0);
        }
        g_chest_content[index] = word;
    }

    json_free(root);
    return true;
}

/* assets/data/scene_objects.json: Darum, Teim, the burst, the dead
 * bodies, the jet scooter and the two doors. Optional: without it the
 * tube and Shure's top floor are simply empty, and the rest of the game
 * runs. */
static void load_scene(void)
{
    g_scene_type_count = 0;
    JsonValue *root = json_parse_file("assets/data/scene_objects.json");
    if (root == NULL) {
        fprintf(stderr, "npc: assets/data/scene_objects.json is missing -- "
                        "rerun tools/extract_npcs.py (make assets); no "
                        "Darum or Teim until then\n");
        return;
    }
    for (const JsonValue *t = root->child;
         t != NULL && g_scene_type_count < SCENE_TYPES_MAX; t = t->next) {
        NpcSceneType *st = &g_scene_type[g_scene_type_count++];
        memset(st, 0, sizeof *st);
        st->object = (int)json_int(t, "object", 0);
        st->tile   = (int)json_int(t, "tile", 0);
        st->line   = (int)json_int(t, "palette_line", 0);
        st->box    = (int)json_int(t, "box", 0);
        if (st->box < 0 || st->box >= 14) {
            st->box = 0;
        }
        st->front  = json_bool(t, "front", false);

        int f = 0;
        const JsonValue *frames = json_get(t, "frames");
        for (const JsonValue *fr = (frames != NULL) ? frames->child : NULL;
             fr != NULL && f < NPC_SCENE_FRAMES; fr = fr->next, f++) {
            int n = 0;
            for (const JsonValue *p = fr->child;
                 p != NULL && n < NPC_SCENE_PIECES; p = p->next, n++) {
                read_piece(p, &st->frame[f][n]);
            }
            st->pieces[f] = n;
        }
        st->frames = f;
    }
    if (g_scene_type_count == SCENE_TYPES_MAX && root->child != NULL) {
        int total = 0;
        for (const JsonValue *t = root->child; t != NULL; t = t->next) {
            total++;
        }
        if (total > SCENE_TYPES_MAX) {
            fprintf(stderr, "npc: scene_objects.json has %d types, only %d "
                            "kept -- raise SCENE_TYPES_MAX\n", total,
                    SCENE_TYPES_MAX);
        }
    }
    json_free(root);
}

/* Obj_InvisibleBlock ($37) and Obj_InvisibleBlock2 ($38), ps2.asm:7820:
 * the init writes 2(a0) = 0, mappings loc_5D98C, tile word $2000, $2C =
 * 9 or 7 and routine 1, and routine 1 does nothing. Never drawn, but
 * RunObject's tail still marks it on screen, so it is bumped into and A
 * finds it -- its $2E, from the level's sprite data, is the ObjType_Events
 * index: the towns' control towers (2), Arima's blown doors (1), Skure's
 * old newspapers ($08-$0E) and the rest. No art, so not in
 * scene_objects.json: the two types are the port's own. */
static const NpcSceneType g_block_type[2] = {
    { .object = NPC_OBJ_INVISIBLE_BLOCK,  .box = 9 },
    { .object = NPC_OBJ_INVISIBLE_BLOCK2, .box = 7 },
};

static const NpcSceneType *scene_of(int object)
{
    for (int i = 0; i < g_scene_type_count; i++) {
        if (g_scene_type[i].object == object) {
            return &g_scene_type[i];
        }
    }
    for (int i = 0; i < 2; i++) {
        if (g_block_type[i].object == object) {
            return &g_block_type[i];
        }
    }
    return NULL;
}

u16 npc_chest_content(int index)
{
    return (index > 0 && index < NPC_CHESTS_MAX) ? g_chest_content[index] : 0;
}

static int read_bytes(const JsonValue *arr, u8 *out, int cap)
{
    int n = 0;
    for (const JsonValue *v = (arr != NULL) ? arr->child : NULL;
         v != NULL && n < cap; v = v->next) {
        out[n++] = (u8)v->number;
    }
    return n;
}

bool npc_load_tables(void)
{
    JsonValue *types   = json_parse_file("assets/data/npc_types.json");
    JsonValue *sprites = json_parse_file("assets/data/level_sprites.json");
    JsonValue *talk    = json_parse_file("assets/data/people_dialogue.json");
    bool ok = (types != NULL && sprites != NULL && talk != NULL);

    g_type_count = 0;
    for (const JsonValue *t = ok ? types->child : NULL;
         t != NULL && g_type_count < NPC_TYPES_MAX; t = t->next) {
        NpcType *ty = &g_type[g_type_count++];
        memset(ty, 0, sizeof *ty);
        ty->object     = (int)json_int(t, "object", 0);
        ty->tile       = (int)json_int(t, "tile", 0);
        ty->line       = (int)json_int(t, "palette_line", 0);
        ty->first_wait = (int)json_int(t, "first_wait", WAIT_FRAMES);
        const char *wander = json_str(t, "wander", "");
        ty->horizontal = strcmp(wander, "horizontal") == 0;
        ty->four       = strcmp(wander, "four") == 0;
        if (ty->horizontal) {
            ty->wander = NPC_WANDER_HORIZONTAL;
        } else if (ty->four) {
            ty->wander = NPC_WANDER_FOUR;
        } else if (strcmp(wander, "dezolian") == 0) {
            ty->wander = NPC_WANDER_DEZOLIAN;
        } else if (strcmp(wander, "muskcat") == 0) {
            ty->wander = NPC_WANDER_MUSKCAT;
        } else {
            ty->wander = NPC_WANDER_VERTICAL;
        }

        int f = 0;
        const JsonValue *frames = json_get(t, "frames");
        for (const JsonValue *fr = (frames != NULL) ? frames->child : NULL;
             fr != NULL && f < NPC_FRAMES; fr = fr->next, f++) {
            /* A Motavian's or a musk cat's frame is one piece, a
             * Dezolian's two (loc_59EBA: 16x32 over 16x8). */
            int n = 0;
            for (const JsonValue *pc = fr->child;
                 pc != NULL && n < NPC_TYPE_PIECES; pc = pc->next, n++) {
                read_piece(pc, &ty->frame[f][n]);
            }
            ty->pieces[f] = n;
        }
    }

    g_list_count = 0;
    const JsonValue *lists = json_get(sprites, "art_lists");
    for (const JsonValue *l = (lists != NULL) ? lists->child : NULL;
         l != NULL && g_list_count < NPC_LISTS_MAX; l = l->next) {
        ArtList *al = &g_list[g_list_count++];
        al->count = 0;
        for (const JsonValue *e = l->child;
             e != NULL && al->count < NPC_LIST_ART; e = e->next) {
            int a = art_index(json_str(e, "art", ""));
            if (a >= 0) {
                al->art[al->count]  = a;
                al->tile[al->count] = (int)json_int(e, "tile", 0);
                al->count++;
            }
        }
    }

    memset(g_level, 0, sizeof g_level);
    const JsonValue *levels = json_get(sprites, "levels");
    for (const JsonValue *lv = (levels != NULL) ? levels->child : NULL;
         lv != NULL; lv = lv->next) {
        long index = json_int(lv, "level", -1);
        if (index < 0 || index >= NPC_LEVELS_MAX) {
            continue;
        }
        LevelPeople *lp = &g_level[index];
        lp->art_list = (int)json_int(lv, "art_list", 0);

        const JsonValue *objects = json_get(lv, "objects");
        for (const JsonValue *o = (objects != NULL) ? objects->child : NULL;
             o != NULL && lp->count < NPC_MAX; o = o->next) {
            Placement *pl = &lp->at[lp->count++];
            pl->object      = (int)json_int(o, "object", 0);
            pl->interaction = (int)json_int(o, "interaction", 0);
            pl->index       = (int)json_int(o, "index", 0);
            pl->x           = (int)json_int(o, "x", 0);
            pl->y           = (int)json_int(o, "y", 0);
        }
    }

    g_talk_after_n  = read_bytes(json_get(talk, "after_climatrol"),
                                 g_talk_after, 256);
    g_talk_before_n = read_bytes(json_get(talk, "before_climatrol"),
                                 g_talk_before, 256);
    g_talk_later_n  = read_bytes(json_get(talk, "later"), g_talk_later, 256);

    json_free(types);
    json_free(sprites);
    json_free(talk);
    load_scene();
    return load_chests() && ok;
}

static const NpcType *type_of(int object)
{
    for (int i = 0; i < g_type_count; i++) {
        if (g_type[i].object == object) {
            return &g_type[i];
        }
    }
    return NULL;
}

static const NpcChestType *chest_of(int object)
{
    for (int i = 0; i < g_chest_type_count; i++) {
        if (g_chest_type[i].object == object) {
            return &g_chest_type[i];
        }
    }
    return NULL;
}

/* loc_5656's lookup: a chest with interaction type 0 reads event_flags
 * ($C710), any other treasure_chest_flags ($C780), by its index byte. */
static u8 chest_flag(const NpcSet *set, const Npc *n)
{
    int at = ((n->interaction != 0) ? FLAG_CHESTS : FLAG_EVENT_SLOTS) + n->index;
    return (set->c700 != NULL && at < 0x100) ? set->c700[at] : 0;
}

/* Obj_DezoTreasureChest / Obj_MotaTreasureChest's init: frame 0, the
 * Motavian one's second mappings on level type 1. */
static void spawn_chest(NpcSet *set, const NpcChestType *ct, int object_2e,
                        int x, int y)
{
    if (ct == NULL || set->count >= NPC_MAX) {
        return;
    }
    Npc *n = &set->npc[set->count++];
    memset(n, 0, sizeof *n);
    n->chest       = ct;
    n->box         = ct->box;
    n->alt         = (set->level_type == ct->alt_level_type);
    n->interaction = (object_2e >> 8) & 3;
    n->index       = object_2e & 0xFF;
    n->x           = (s16)x;
    n->y           = (s16)y;
    n->frame       = 0;
    n->routine     = 1;
}

/* A free slot at the end, standing in for an object the port does not run
 * so that the ones after it keep their slot numbers. */
static void spawn_free(NpcSet *set)
{
    if (set->count < NPC_MAX) {
        Npc *n = &set->npc[set->count++];
        memset(n, 0, sizeof *n);
        n->free = true;
    }
}

/* Slot `slot` from nothing: object `object` at (x, y), $24 = frame, the
 * rest zero, routine 0 -- its init runs on the next tick. */
static Npc *put_scene(NpcSet *set, int slot, const NpcSceneType *st, int x,
                      int y, int frame)
{
    if (st == NULL || slot < 0 || slot >= NPC_MAX) {
        return NULL;
    }
    while (set->count <= slot) {
        spawn_free(set);
    }
    Npc *n = &set->npc[slot];
    memset(n, 0, sizeof *n);
    n->scene = st;
    n->x     = (s16)x;
    n->y     = (s16)y;
    n->frame = frame;
    return n;
}

Npc *npc_put(NpcSet *set, int slot, int object, int x, int y, int frame)
{
    return put_scene(set, slot, scene_of(object), x, y, frame);
}

Npc *npc_slot(NpcSet *set, int slot)
{
    if (slot < 0 || slot >= set->count || set->npc[slot].free) {
        return NULL;
    }
    return &set->npc[slot];
}

Npc *npc_jet_scooter(NpcSet *set)
{
    Npc *n = npc_slot(set, 0);
    if (n == NULL || n->scene == NULL
        || n->scene->object != NPC_OBJ_JET_SCOOTER) {
        return NULL;
    }
    return n;
}

void npc_spawn(NpcSet *set, int level_index, int level_type, const u8 *c700,
               TileSet *vram)
{
    memset(set, 0, sizeof *set);
    set->d6          = -1;
    set->level_type  = level_type;
    set->level_index = level_index;
    set->c700        = c700;
    if (level_index < 0 || level_index >= NPC_LEVELS_MAX) {
        return;
    }
    const LevelPeople *lp = &g_level[level_index];

    if (lp->art_list < g_list_count) {
        const ArtList *al = &g_list[lp->art_list];
        for (int i = 0; i < al->count; i++) {
            tileset_place(vram, &g_art[al->art[i]].tiles, al->tile[i]);
        }
    }

    for (int i = 0; i < lp->count; i++) {
        const Placement *pl = &lp->at[i];
        const NpcChestType *chest = chest_of(pl->object);
        if (chest != NULL) {
            spawn_chest(set, chest, pl->interaction << 8 | pl->index,
                        pl->x, pl->y);
            continue;
        }
        const NpcSceneType *scene = scene_of(pl->object);
        if (scene != NULL) {
            Npc *n = put_scene(set, set->count, scene, pl->x, pl->y, 0);
            if (n != NULL) {
                n->interaction = pl->interaction;
                n->index       = pl->index;
            }
            continue;
        }
        const NpcType *type = type_of(pl->object);
        if (type != NULL && type->four && pl->interaction == 1
            && (pl->index == 0x86 || pl->index == 0x87)
            && c700 != NULL && c700[FLAG_JET_SCOOTER_OWNED] == 2) {
            /* loc_42A6: $2E = $186 or $187 with $C716 = 2 is
             * `move.b #1, 2(a0)`, deleted before it is ever drawn. */
            spawn_free(set);
            continue;
        }
        if (type == NULL || set->count >= NPC_MAX) {
            /* An object the port does not run keeps its slot, as in the
             * original, so the scene's fixed slots stay where they are.
             * (Dezolians and musk cats were among them until build 85.) */
            spawn_free(set);
            continue;
        }

        Npc *n = &set->npc[set->count++];
        n->type        = type;
        n->box         = BOX_PERSON;
        n->interaction = pl->interaction;
        n->index       = pl->index;
        n->x           = (s16)pl->x;
        n->y           = (s16)pl->y;

        /* The init copies the high word of the mappings' address into
         * $2A -- `move.w 4(a0), $2A(a0)` -- which is 5, not a facing. It
         * is never drawn: the first decision overwrites the frame. It
         * does mean a person will take any first turn offered. */
        n->facing  = 5;
        n->frame   = 0;
        n->wait    = (s16)type->first_wait;
        n->routine = 1;
    }

    /* LoadSpritesInLevel's tail (ps2.asm:912). The Esper mansion's two
     * chests are written to the same slot, so while both are shut the
     * second is the one there; Noah's opens an event rather than a
     * chest. The object is $39, Obj_DezoTreasureChest, each time. The
     * chests are $0E and $0F, whose own bytes are FLAG_CHEST_PRISM and
     * FLAG_CHEST_NEI_SWORD. */
    if (c700 != NULL && level_index == LEVEL_ESPER_MANSION_F1) {
        int which = -1;
        if (c700[FLAG_CHEST_PRISM] == 0) {
            which = EVENT_KIND_CHESTS | (FLAG_CHEST_PRISM - FLAG_CHESTS);
        }
        if (c700[FLAG_CHEST_NEI_SWORD] == 0) {
            which = EVENT_KIND_CHESTS | (FLAG_CHEST_NEI_SWORD - FLAG_CHESTS);
        }
        if (which >= 0) {
            spawn_chest(set, chest_of(0x39), which, 0xD8, 0xF8);
        }
    }
    if (c700 != NULL && level_index == LEVEL_NOAH_GROUND_F
        && c700[FLAG_PANDORA_BOX_OPENED] == 0) {
        spawn_chest(set, chest_of(0x39), EVENT_KIND_EVENTS | EVENT_PANDORAS_BOX,
                    0x210, 0xD8);
    }
}

/* ------------------------------------------------------------------ */
/* The hit test                                                       */
/* ------------------------------------------------------------------ */

/* loc_7278, rows 0-3: the cell a mover is about to enter. */
static void probe_box(int x, int y, int dir, int *l, int *r, int *t, int *b)
{
    *l = x + BOX[dir][0];
    *r = *l + BOX[dir][1];
    *t = y + BOX[dir][2];
    *b = *t + BOX[dir][3];
}

/* loc_7224: does the box of the thing at (ox, oy), row `row` of
 * loc_7278, overlap [l, r] x [t, b]? */
static bool box_hit(int l, int r, int t, int b, int ox, int oy, int row)
{
    int ol  = ox + BOX[row][0];
    int or_ = ol + BOX[row][1];
    int ot  = oy + BOX[row][2];
    int ob  = ot + BOX[row][3];
    return ol <= r && or_ >= l && ot <= b && ob >= t;
}

Npc *npc_in_the_way(NpcSet *set, const Npc *self, int x, int y, int dir)
{
    int l, r, t, b;
    probe_box(x, y, dir, &l, &r, &t, &b);

    for (int i = 0; i < set->count; i++) {
        Npc *n = &set->npc[i];
        /* Only what was drawn last frame can be bumped into: the test
         * reads screen positions, which stop updating off screen. */
        if (n == self || n->free || !n->on_screen || n->hidden
            || n->box == 0) {
            /* loc_7224 also passes over anything whose $2C is 0. A hidden
             * object (bit 1 of 2(a0)) never gets bit 7 from RunObject
             * (loc_3E4), so it is not there either: the pump before
             * $C737, with its row-4 box. */
            continue;
        }
        if (box_hit(l, r, t, b, n->x, n->y, n->box)) {
            return n;
        }
    }
    return NULL;
}

void npc_trace(const NpcSet *set, int x, int y, int dir)
{
    int l, r, t, b;
    probe_box(x, y, dir & 3, &l, &r, &t, &b);
    fprintf(stderr, "  probe: row %d, x %d..%d, y %d..%d\n",
            dir & 3, l, r, t, b);

    for (int i = 0; i < set->count; i++) {
        const Npc *n = &set->npc[i];
        if (n->free) {
            fprintf(stderr, "  slot %2d: free\n", i);
            continue;
        }
        const char *kind = "?";
        int object = -1;
        if (n->type != NULL) {
            kind   = "person";
            object = n->type->object;
        } else if (n->chest != NULL) {
            kind   = "chest";
            object = n->chest->object;
        } else if (n->scene != NULL) {
            kind   = "scene";
            object = n->scene->object;
        }
        bool hit = n->box != 0 && box_hit(l, r, t, b, n->x, n->y, n->box);
        fprintf(stderr, "  slot %2d: %-6s obj $%02X  $2E %d/$%02X  at %d,%d"
                        "  box %d  %s  v %d,%d  routine %d%s%s\n",
                i, kind, object & 0xFF, n->interaction, n->index & 0xFF,
                n->x, n->y, n->box, n->on_screen ? "on screen" : "OFF screen",
                n->vx, n->vy, n->routine, n->frozen ? "  frozen" : "",
                hit ? (n->on_screen ? "  <- HIT" : "  <- overlaps, but off "
                                                   "screen")
                    : "");
    }
}

void npc_unfreeze_all(NpcSet *set)
{
    for (int i = 0; i < set->count; i++) {
        set->npc[i].frozen = false;
    }
}

/* ------------------------------------------------------------------ */
/* Wandering                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    NpcSet     *set;
    NpcMapTest  map_ok;
    void       *map_ctx;
    const s16  *party_x, *party_y;
    int         party_count;
} World;

static bool can_step(const World *w, const Npc *n, int dir)
{
    static const s8 DX[4] = { 0, 0, -16, 16 };
    static const s8 DY[4] = { -16, 16, 0, 0 };

    if (!w->map_ok(w->map_ctx, n->x + DX[dir], n->y + DY[dir])) {
        return false;
    }
    if (npc_in_the_way(w->set, n, n->x, n->y, dir) != NULL) {
        return false;
    }

    int l, r, t, b;
    probe_box(n->x, n->y, dir, &l, &r, &t, &b);
    for (int i = 0; i < w->party_count; i++) {
        if (box_hit(l, r, t, b, w->party_x[i], w->party_y[i], BOX_PERSON)) {
            return false;
        }
    }
    return true;
}

static void start_step(Npc *n, int dx, int dy, int range_delta)
{
    n->wait    = STEP_FRAMES;
    n->vx      = (s16)dx;
    n->vy      = (s16)dy;
    n->anim    = ANIM_FRAMES;
    n->range   = (s8)(n->range + range_delta);
    n->frame   = n->facing;
    n->routine = 2;
}

/* loc_3C06 (vertical) and loc_3DCA (horizontal) are the same routine with
 * the axis swapped: `back` is up or left, `forth` is down or right. */
static void decide(const World *w, Npc *n, Rng *rng)
{
    n->wait--;
    if (n->wait >= 0) {
        return;
    }

    int roll = rng_next(rng) & 0xF;

    int  back_face  = n->type->horizontal ? FACE_LEFT : FACE_UP;
    int  forth_face = n->type->horizontal ? FACE_RIGHT : FACE_DOWN;
    int  back_dir   = n->type->horizontal ? DIR_LEFT : DIR_UP;
    int  forth_dir  = n->type->horizontal ? DIR_RIGHT : DIR_DOWN;
    bool stepped    = false;

    if (n->facing != FACE_DOWN && roll == 0) {
        n->facing = FACE_UP;
    } else if (n->facing != FACE_UP && roll == 4) {
        n->facing = FACE_DOWN;
    } else if (n->facing != FACE_RIGHT && roll == 8) {
        n->facing = FACE_LEFT;
    } else if (n->facing != FACE_LEFT && roll == 0xF) {
        n->facing = FACE_RIGHT;
    } else if ((roll == 1 || roll == 5) && n->facing != forth_face) {
        n->facing = back_face;
        if (n->range != -RANGE_CELLS && can_step(w, n, back_dir)) {
            start_step(n, n->type->horizontal ? -1 : 0,
                       n->type->horizontal ? 0 : -1, -1);
            stepped = true;
        }
    } else if ((roll == 9 || roll == 0xA) && n->facing != back_face) {
        n->facing = forth_face;
        if (n->range != RANGE_CELLS && can_step(w, n, forth_dir)) {
            start_step(n, n->type->horizontal ? 1 : 0,
                       n->type->horizontal ? 0 : 1, 1);
            stepped = true;
        }
    }

    if (!stepped) {
        n->frame = n->facing;
        n->wait  = WAIT_FRAMES;
    }
}

/* loc_3D30 / loc_3EF4, and Obj_Dezolian's loc_4234, which is the same
 * but for the wait it sets at the end: $28, where the town types' is $20.
 * Level_SpriteMappingsArray (0, 1, 0, 2), a frame every five. */
static void walk(Npc *n, int rest)
{
    static const u8 CYCLE[4] = { 0, 1, 0, 2 };

    n->wait--;
    if (n->wait < 0) {
        n->vx      = 0;
        n->vy      = 0;
        n->wait    = (s16)rest;
        n->frame   = n->facing;
        n->routine = 1;
        return;
    }

    n->anim--;
    if (n->anim < 0) {
        n->frame      = n->facing + CYCLE[n->anim_index];
        n->anim_index = (n->anim_index + 1) & 3;
        n->anim       = ANIM_FRAMES;
    }
}

/* loc_42F8: object $1B's decision. The facings are frame bases: 0 and
 * 1 up, 3 and 4 down, 6 left, 9 right; a step up or down leaves $2A on
 * 1 or 4, the first walking frame. A refused step still leaves the new
 * $2A as the frame shown (loc_44D8). $35 counts cells down, $34 right. */
static void decide_four(const World *w, Npc *n, Rng *rng)
{
    n->wait--;
    if (n->wait >= 0) {
        return;
    }

    int  roll    = rng_next(rng) & 0xF;
    int  f       = n->facing;
    bool up      = (f == 0 || f == 1);
    bool down    = (f == 3 || f == 4);
    bool stepped = false;

    if (!up && roll == 1) {
        n->facing = 3;
    } else if (!down && roll == 3) {
        n->facing = 0;
    } else if (f != 9 && roll == 5) {
        n->facing = 6;
    } else if (f != 6 && roll == 7) {
        n->facing = 9;
    } else if (roll == 0xA || roll == 0xB) {
        if (!down) {
            n->facing = 1;
            if (n->range != -3 && can_step(w, n, DIR_UP)) {
                n->vy = -1;
                n->range--;
                stepped = true;
            }
        }
    } else if (roll == 0xC || roll == 0xD) {
        if (!up) {
            n->facing = 4;
            if (n->range != 3 && can_step(w, n, DIR_DOWN)) {
                n->vy = 1;
                n->range++;
                stepped = true;
            }
        }
    } else if (roll == 8 || roll == 9) {
        if (f != 9) {
            n->facing = 6;
            if (n->range_x != -5 && can_step(w, n, DIR_LEFT)) {
                n->vx = -1;
                n->range_x--;
                stepped = true;
            }
        }
    } else if (roll == 0xE || roll == 0xF) {
        if (f != 6) {
            n->facing = 9;
            if (n->range_x != 5 && can_step(w, n, DIR_RIGHT)) {
                n->vx = 1;
                n->range_x++;
                stepped = true;
            }
        }
    }

    n->frame = n->facing;               /* $24 = $2A */
    if (stepped) {
        n->wait    = STEP_FRAMES;       /* loc_44E6 */
        n->anim    = 3;
        n->routine = 2;
    } else {
        n->wait = n->type->first_wait;  /* loc_44D8: $30 */
    }
}

/* loc_44F6: sixteen frames of step, a frame every four from
 * Level_SpriteMappingsArray2 (0, 1, 0, 1) over the facing. */
static void walk_four(Npc *n)
{
    static const u8 CYCLE2[4] = { 0, 1, 0, 1 };

    n->wait--;
    if (n->wait < 0) {
        n->vx      = 0;
        n->vy      = 0;
        n->wait    = n->type->first_wait;
        n->frame   = n->facing;
        n->routine = 1;
        return;
    }

    n->anim--;
    if (n->anim < 0) {
        n->frame      = n->facing + CYCLE2[n->anim_index];
        n->anim_index = (n->anim_index + 1) & 3;
        n->anim       = 3;
    }
}

static int wrap(int v, int extent);

/* ------------------------------------------------------------------ */
/* Obj_Dezolian and Obj_MuskCat (build 85)                            */
/* ------------------------------------------------------------------ */

/* The mover's box for loc_7278 row `row` about (x, y), or false for a row
 * the port cannot say: rows 0-13 are the table, -1 the four bytes before
 * it; anything else reads on into SegaScreen's code, which is not data
 * the port has, and is taken to hit nothing. */
static bool row_box(int x, int y, int row, int *l, int *r, int *t, int *b)
{
    const s16 *bx;
    if (row >= 0 && row < 14) {
        bx = BOX[row];
    } else if (row == -1) {
        bx = BOX_BEFORE;
    } else {
        return false;
    }
    *l = x + (s8)bx[0];
    *r = *l + bx[1];
    *t = y + (s8)bx[2];
    *b = *t + bx[3];
    return true;
}

/* Level_ChkTargetInteract for an object ($E800 on): from $E400, 48
 * slots -- the party first, then every object, the mover itself among
 * them (nothing skips a1 = a0) -- anything drawn last frame whose $2C box
 * overlaps the mover's row-`row` box. The index of the first hit in that
 * order ($E400 + $40 k), or -1. */
static int scan_hit(const World *w, const Npc *n, int row)
{
    int l, r, t, b;
    if (!row_box(n->x, n->y, row, &l, &r, &t, &b)) {
        return -1;
    }
    for (int p = 0; p < w->party_count; p++) {
        if (box_hit(l, r, t, b, w->party_x[p], w->party_y[p], BOX_PERSON)) {
            return p;
        }
    }
    for (int i = 0; i < w->set->count; i++) {
        const Npc *o = &w->set->npc[i];
        if (o->free || !o->on_screen || o->hidden || o->box == 0) {
            continue;
        }
        if (box_hit(l, r, t, b, o->x, o->y, o->box)) {
            return 16 + i;
        }
    }
    return -1;
}

/* Sprites_ChkCanMove (ps2.asm:10103) for an object, with `row` in d6:
 * loc_6DA4 against the map at (x + dx, y + dy) -- a solid quadrant stops
 * an object outright -- and only then Level_ChkTargetInteract. d6 is left
 * as the ROM leaves it: the row when the map refuses (no scan), $FFFF
 * after a clean scan (dbf ran out), $2F - k at a hit. */
static bool obj_can_move(const World *w, const Npc *n, int dy, int dx,
                         int row)
{
    w->set->d6 = (s16)row;
    if (!w->map_ok(w->map_ctx, n->x + dx, n->y + dy)) {
        return false;
    }
    int k = scan_hit(w, n, row);
    if (k >= 0) {
        w->set->d6 = (s16)(0x2F - k);
        return false;
    }
    w->set->d6 = -1;
    return true;
}

/* PS2_NPCLOG: one stderr line a decision by a Dezolian or a musk cat --
 * slot, roll, the d6 it found, the step or the facing it ended on -- for
 * checking against the emulator. */
static void log_decision(const World *w, const Npc *n, int roll, int d6_in,
                         bool stepped)
{
    static int on = -1;
    if (on < 0) {
        on = getenv("PS2_NPCLOG") != NULL;
    }
    if (!on) {
        return;
    }
    fprintf(stderr, "npc: slot %d $%02X roll %X d6 in %d out %d at %d,%d "
                    "$2A %d %s%s\n", (int)(n - w->set->npc), n->type->object,
            roll, d6_in, w->set->d6, n->x / 16, n->y / 16, n->facing,
            stepped ? "step" : "wait", n->on_screen ? "" : " (off screen)");
}

/* loc_402C, ps2.asm:6108: a Dezolian's decision, every $28 frames. Not
 * $1B's (loc_42F8), though it looks it:
 *   1 faces up unless facing down; 2 down unless up; 3 left unless
 *   right; 4 right unless left.
 *   8/9 up: not when facing down (no turn either); turns up ($2A 0, not
 *   $1B's 1), then $35 to -5 (not -3), loc_6DA4 two cells up (the map
 *   only: the head), then Sprites_ChkCanMove one up with d6 = 0.
 *   A/B down: not when facing up; turns down, $35 to 5, d6 = 1.
 *   C/D left: not when facing right; the cell up and to the left, with
 *   d6 as it is (no moveq), then the cell to the left with d6 = 2; only
 *   when both pass does it turn left, and then $34 to -5.
 *   E/0 right: the same to the right, d6 = 3.
 *   5, 6, 7, F: nothing.
 * A step is $F frames and $26 = 4 (loc_4224); anything else shows the
 * facing and waits $28 again (loc_4216). */
static void decide_dezolian(const World *w, Npc *n, Rng *rng)
{
    n->wait--;
    if (n->wait >= 0) {
        return;
    }

    int  roll    = rng_next(rng) & 0xF;
    int  f       = n->facing;
    int  d6_in   = w->set->d6;
    bool stepped = false;

    if (f != FACE_DOWN && roll == 1) {
        n->facing = FACE_UP;
    } else if (f != FACE_UP && roll == 2) {
        n->facing = FACE_DOWN;
    } else if (f != FACE_RIGHT && roll == 3) {
        n->facing = FACE_LEFT;
    } else if (f != FACE_LEFT && roll == 4) {
        n->facing = FACE_RIGHT;
    } else if (roll == 8 || roll == 9) {
        if (f != FACE_DOWN) {
            n->facing = FACE_UP;
            if (n->range != -5 && w->map_ok(w->map_ctx, n->x, n->y - 32)
                && obj_can_move(w, n, -16, 0, DIR_UP)) {
                n->vy = -1;
                n->range--;
                stepped = true;
            }
        }
    } else if (roll == 0xA || roll == 0xB) {
        if (f != FACE_UP) {
            n->facing = FACE_DOWN;
            if (n->range != 5 && obj_can_move(w, n, 16, 0, DIR_DOWN)) {
                n->vy = 1;
                n->range++;
                stepped = true;
            }
        }
    } else if (roll == 0xC || roll == 0xD) {
        if (f != FACE_RIGHT && obj_can_move(w, n, -16, -16, w->set->d6)
            && obj_can_move(w, n, 0, -16, DIR_LEFT)) {
            n->facing = FACE_LEFT;
            if (n->range_x != -5) {
                n->vx = -1;
                n->range_x--;
                stepped = true;
            }
        }
    } else if (roll == 0xE || roll == 0) {
        if (f != FACE_LEFT && obj_can_move(w, n, -16, 16, w->set->d6)
            && obj_can_move(w, n, 0, 16, DIR_RIGHT)) {
            n->facing = FACE_RIGHT;
            if (n->range_x != 5) {
                n->vx = 1;
                n->range_x++;
                stepped = true;
            }
        }
    }

    n->frame = n->facing;                   /* $24 = $2A */
    if (stepped) {
        n->wait    = STEP_FRAMES;
        n->anim    = 4;
        n->routine = 2;
    } else {
        n->wait = (s16)n->type->first_wait; /* $28 */
    }
    log_decision(w, n, roll, d6_in, stepped);
}

/* loc_45A0, ps2.asm:6517: a musk cat's decision, every $30 frames. The
 * facings are 0 up, 3 down, $C left, $10 right; the walking bases are 0,
 * 3, $D and $11.
 *   1 faces up unless facing down; 3 down unless up; 5 left unless
 *   right; 7 right unless left.
 *   B up: not when facing down; $35 to -4, one up with d6 = 0, and only
 *   then the turn. D down: not when facing up, $35 to 4, d6 = 1.
 *   9 left and F right: their guards compare $2A with 9 and 6, which a
 *   cat never has, so a cat facing right steps straight off to the left
 *   (and the reverse). $34 to -5 and 5, d6 = 2 and 3.
 * A step is $F frames and $26 = 5 (loc_474C); else $30 (loc_473E). */
static void decide_muskcat(const World *w, Npc *n, Rng *rng)
{
    n->wait--;
    if (n->wait >= 0) {
        return;
    }

    int  roll    = rng_next(rng) & 0xF;
    int  f       = n->facing;
    int  d6_in   = w->set->d6;
    bool stepped = false;

    if (f != 0 && roll == 3) {
        n->facing = 3;
    } else if (f != 3 && roll == 1) {
        n->facing = 0;
    } else if (f != 0x10 && roll == 5) {
        n->facing = 0xC;
    } else if (f != 0xC && roll == 7) {
        n->facing = 0x10;
    } else if (f != 3 && roll == 0xB) {
        if (n->range != -4 && obj_can_move(w, n, -16, 0, DIR_UP)) {
            n->facing = 0;
            n->vy     = -1;
            n->range--;
            stepped = true;
        }
    } else if (f != 0 && roll == 0xD) {
        if (n->range != 4 && obj_can_move(w, n, 16, 0, DIR_DOWN)) {
            n->facing = 3;
            n->vy     = 1;
            n->range++;
            stepped = true;
        }
    } else if (f != 9 && roll == 9) {
        if (n->range_x != -5 && obj_can_move(w, n, 0, -16, DIR_LEFT)) {
            n->facing = 0xD;
            n->vx     = -1;
            n->range_x--;
            stepped = true;
        }
    } else if (f != 6 && roll == 0xF) {
        if (n->range_x != 5 && obj_can_move(w, n, 0, 16, DIR_RIGHT)) {
            n->facing = 0x11;
            n->vx     = 1;
            n->range_x++;
            stepped = true;
        }
    }

    n->frame = n->facing;                   /* $24 = $2A */
    if (stepped) {
        n->wait    = STEP_FRAMES;
        n->anim    = 5;
        n->routine = 2;
    } else {
        n->wait = (s16)n->type->first_wait; /* $30 */
    }
    log_decision(w, n, roll, d6_in, stepped);
}

/* loc_475C: the cat's step. Level_SpriteMappingsArray3 (0, 1, 2, 0) over
 * $2A, a frame every six; at the end a sideways walker's $2A drops from
 * $D or $11 to the standing $C or $10. */
static void walk_muskcat(Npc *n)
{
    static const u8 CYCLE3[4] = { 0, 1, 2, 0 };

    n->wait--;
    if (n->wait < 0) {
        n->vx   = 0;
        n->vy   = 0;
        n->wait = (s16)n->type->first_wait;
        if (n->facing == 0xD || n->facing == 0x11) {
            n->facing--;
        }
        n->frame   = n->facing;
        n->routine = 1;
        return;
    }

    n->anim--;
    if (n->anim < 0) {
        n->anim       = 5;
        n->frame      = n->facing + CYCLE3[n->anim_index];
        n->anim_index = (n->anim_index + 1) & 3;
    }
}

/* What BuildSprites (ps2.asm:343) leaves in d6: it walks RunObject's 64
 * rows (16 bytes each from $F000: a count, then up to seven objects) and
 * sets d6 to 2 for every row with anyone in it, adding 2 an object, so it
 * ends at 2 + 2 x the objects in the last row it drew. RunObject files an
 * object under (~($1E - $80) & $FC) / 4 -- the top of the screen is the
 * last row -- spilling into the next row once one holds seven, and under
 * row 0 with bit 5 of 2(a0). Nothing drawn leaves d6 alone. The party
 * ($E400 on) is filed first, then the objects, in slot order. */
static void build_sprites_d6(NpcSet *set, const World *w, int width,
                             int height, int cam_x, int cam_y)
{
    u8 rows[64];
    memset(rows, 0, sizeof rows);

    int total = w->party_count + set->count;
    for (int i = 0; i < total; i++) {
        int sy;
        int row;
        if (i < w->party_count) {
            int sx = wrap(w->party_x[i] - cam_x, width) + SPRITE_BIAS;
            sy = wrap(w->party_y[i] - cam_y, height) + SPRITE_BIAS;
            if (!(sx > 0x40 && sx < 0x1E0 && sy > 0x40 && sy < 0x180)) {
                continue;
            }
            row = ((~(sy - 0x80)) & 0xFC) >> 2;
        } else {
            const Npc *n = &set->npc[i - w->party_count];
            if (n->free || !n->on_screen || n->hidden) {
                continue;
            }
            bool front = (n->scene != NULL && n->scene->front) || n->front;
            sy  = n->screen_y;
            row = front ? 0 : ((~(sy - 0x80)) & 0xFC) >> 2;
        }
        while (row < 63 && rows[row] >= 7) {
            row++;
        }
        rows[row]++;
    }
    for (int row = 63; row >= 0; row--) {
        if (rows[row] != 0) {
            set->d6 = (s16)(2 + 2 * rows[row]);
            return;
        }
    }
}

void npc_note_leader_check(NpcSet *set, int dir, bool scanned,
                           const Npc *hit)
{
    if (set == NULL) {
        return;
    }
    if (!scanned) {
        set->d6 = (s16)dir;
    } else if (hit != NULL) {
        set->d6 = (s16)(0x1F - (int)(hit - set->npc));
    } else {
        set->d6 = -1;
    }
}

static int wrap(int v, int extent)
{
    if (v < 0) {
        return v + extent;
    }
    if (v >= extent) {
        return v - extent;
    }
    return v;
}

/* ------------------------------------------------------------------ */
/* The Teim and Darum scene (ps2.asm:4E08-50AC)                        */
/* ------------------------------------------------------------------ */

static const u8 SCENE_CYCLE[4] = { 0, 1, 0, 2 };   /* Level_SpriteMappingsArray */

static void scene_step_event(NpcSet *set)
{
    if (set->event_step != NULL) {
        (*set->event_step)++;
    }
}

/* $26 counts down; at -1 the next walking frame from $2A and $32. */
static void scene_walk_frame(Npc *n)
{
    n->anim--;
    if (n->anim < 0) {
        n->frame      = n->facing + SCENE_CYCLE[n->anim_index];
        n->anim_index = (n->anim_index + 1) & 3;
        n->anim       = 7;
    }
}

/* Obj_Darum. Returns false when the object deletes itself. */
static bool darum_tick(NpcSet *set, Npc *n)
{
    switch (n->routine) {
        case 0: {
            /* loc_4E2A, every frame until the scene moves him on: gone
             * for good once the Darum scene is done ($C715). */
            n->frame  = 0;
            n->facing = 0;
            n->box    = 8;
            n->wait   = 0x27;
            if (set->c700 != NULL && set->c700[FLAG_DARUM_SCENE_DONE] != 0) {
                return false;
            }
            break;
        }
        case 1: {
            /* loc_4E5E: a forty-frame walk right, then still. */
            n->wait--;
            if (n->wait < 0) {
                n->wait = 0;
                n->vx   = 0;
                n->anim = 0x1C;
                break;
            }
            n->anim--;
            if (n->anim < 0) {
                n->frame      = n->facing + SCENE_CYCLE[n->anim_index];
                n->anim_index = (n->anim_index + 1) & 3;
                n->anim       = 7;
                n->vx         = 1;
            }
            break;
        }
        case 2: {
            /* loc_4EAC */
            n->vx = 0;
            n->anim--;
            if (n->anim < 0) {
                n->anim    = 0xA;
                n->frame   = 0;
                n->routine = 3;
            }
            break;
        }
        case 3: {
            /* loc_4ECC: the swing, a frame every eleven; at the end the
             * scene is moved on. */
            n->anim--;
            if (n->anim < 0) {
                n->frame++;
                if (n->frame == 6) {
                    n->routine = 4;
                    n->frame   = 5;
                    scene_step_event(set);
                    break;
                }
                n->anim = 0xA;
            }
            break;
        }
        default: {
            /* loc_4EFA: gone the frame slot 0 (Teim) is. */
            if (npc_slot(set, 0) == NULL) {
                return false;
            }
            break;
        }
    }
    return true;
}

/* Object $2B (loc_504C): a five-frame burst, eight frames each. */
static bool burst_tick(NpcSet *set, Npc *n)
{
    if (n->routine == 0) {
        /* 2(a0) = $20 -- ahead of every row; NpcSceneType.front. */
        n->frame   = 0;
        n->anim    = 7;
        n->routine = 1;
        if (set->sound != NULL) {
            set->sound(set->sound_ctx, SFX_EXPLOSION);
        }
        return true;
    }
    n->anim--;
    if (n->anim < 0) {
        n->anim = 7;
        n->frame++;
        if (n->frame == 5) {
            return false;
        }
    }
    return true;
}

/* Obj_Teim. */
static bool teim_tick(NpcSet *set, Npc *n)
{
    switch (n->routine) {
        case 0: {
            /* loc_4F36, every frame until the scene moves her on. In the
             * tube she exists only when placed with $24 set -- the one in
             * LevelSpriteData has 0 and goes at once; elsewhere she goes
             * once she has the Letter ($C727). */
            n->wait = 0x37;
            n->box  = 4;
            if (set->level_index == LEVEL_DARUM_TUBE) {
                if (n->frame == 0) {
                    return false;
                }
            } else if (set->c700 != NULL && set->c700[FLAG_TEIM_GIVEN_THE_LETTER] != 0) {
                return false;
            }
            break;
        }
        case 1: {
            /* loc_4F6E */
            n->frame   = 1;
            n->facing  = 1;
            n->anim    = 7;
            n->vx      = -1;
            n->routine = 2;
            break;
        }
        case 2: {
            /* loc_4F8E: $37 + 1 frames walking left. */
            n->wait--;
            if (n->wait < 0) {
                n->vx      = 0;
                n->routine = 3;
                scene_step_event(set);
                break;
            }
            scene_walk_frame(n);
            break;
        }
        case 3: {
            /* loc_4FD4: struck on Darum's frame 3 ($FFFFE864). */
            Npc *darum = npc_slot(set, 1);
            if (darum != NULL && darum->frame == 3) {
                n->frame   = 4;
                n->anim    = 0xB;
                n->routine = 4;
            }
            break;
        }
        case 4: {
            /* loc_4FF2: frames 5-9, twelve frames each. */
            n->anim--;
            if (n->anim < 0) {
                n->frame++;
                if (n->frame == 0xA) {
                    n->frame   = 9;
                    n->hold    = 0x6E;
                    n->routine = 5;
                }
                n->anim = 0xB;
            }
            break;
        }
        case 5: {
            /* loc_501C: until the scene says. */
            break;
        }
        default: {
            /* loc_501E: $6E + 1 frames, then the burst in slot 2 where
             * Darum stands (x + 4), and she is gone. */
            n->hold--;
            if (n->hold < 0) {
                Npc *darum = npc_slot(set, 1);
                int  bx = (darum != NULL) ? darum->x + 4 : n->x;
                int  by = (darum != NULL) ? darum->y : n->y;
                put_scene(set, 2, scene_of(NPC_OBJ_BURST), bx, by, 0);
                scene_step_event(set);
                return false;
            }
            break;
        }
    }
    return true;
}

/* Obj_DeadBody (ps2.asm:7583), object $2F: the scoundrels on Shure's top
 * floor. Its init, loc_52DC, clears $12 and 2(a0) -- zero already in a
 * slot npc_spawn has just filled -- sets its tile word and mappings (the
 * scene type's), puts row 6 of loc_7278 in $2C and moves on to routine
 * 1, which does nothing. It checks no flag and never deletes itself: a
 * body stays after its pocket has been emptied. A on it is
 * ObjType_Events, by the index byte of its $2E (game.c). */
static bool dead_body_tick(Npc *n)
{
    if (n->routine == 0) {
        n->box     = n->scene->box;
        n->routine = 1;
    }
    return true;
}

/* ObjJetSctr_Init (ps2.asm:6780), every frame while parked: its tile word
 * and mappings (the scene type's), 2(a0) = 0, $2C = 5, $FFFFE82E = 6 --
 * the absolute address, slot 0's $2E, whatever slot this is -- and $28 =
 * 0. With $C716 clear, 2(a0) = 1: RunObject deletes it (loc_3DC), which
 * is how the one SpriteData_MotaWorldMap places goes away before anyone
 * owns it. Once jet_scooter_flag is set: $FFFFF72E = loc_29E16 (walk.c
 * keeps both tables and probes the scooter's on its own), routine 1, $2C
 * = 0 so nothing bumps into it, and $D8, the engine, in sound_queue.
 * Nothing here touches $24, $26 or $32: parked, it shows whatever frame it
 * was left on. */
static bool jet_scooter_tick(NpcSet *set, Npc *n)
{
    if (n->routine != 0) {
        return true;                    /* ObjJetSctr_Main: walk.c's */
    }
    n->box  = n->scene->box;
    n->wait = 0;
    Npc *slot0 = npc_slot(set, 0);
    if (slot0 != NULL) {
        slot0->interaction = NPC_INTERACT_EVENT;
        slot0->index       = EVENT_JET_SCOOTER_PARKED;
    }
    bool owned = set->c700 != NULL && set->c700[FLAG_JET_SCOOTER_OWNED] != 0;
    if (set->jet_scooter != NULL && *set->jet_scooter != 0) {
        n->routine = 1;
        n->box     = 0;
        if (set->sound != NULL) {
            set->sound(set->sound_ctx, NPC_SFX_SCOOTER_ON);
        }
    }
    return owned;
}

/* loc_57C6: the first empty slot of the sixteen from $FFFFE800, or -1.
 * The port's array only reaches `count`; anything past it is empty. */
static int first_free_slot(const NpcSet *set)
{
    for (int i = 0; i < NPC_DOOR_SLOTS && i < NPC_MAX; i++) {
        if (i >= set->count || set->npc[i].free) {
            return i;
        }
    }
    return -1;
}

/* Obj_OpenableDoor (ps2.asm:7915) and Obj_DestroyableDoor (7961), objects
 * $3B and $3C. `tst.b 3(a0)` first: frozen, nothing runs. Routine 0
 * (loc_56E4 / loc_5780): 2(a0) = 0, the tile word and mappings (the
 * scene type's), $2C = 6, $24 = 0 -- and for $3B $26 = $F, its swing
 * timer. With event_flags[$2F(a0)] already set the door is placed open
 * ($24 = 2) or blown ($24 = 3) in routine 3; otherwise routine 1, which
 * is `rts`. Routine 2 is reached only from the demo's $FD (game.c, on
 * slot 0): $3B's (loc_5730) counts $26 down from $F and steps $24 on at
 * -1, twice, into routine 3; $3C's (loc_57C6) puts object $2B on its own
 * x and y in the first empty slot -- none free and it tries again next
 * frame -- then $24 = 3 and routine 3. Routine 3 (loc_5750 / loc_57FC):
 * $2C = 0 and $2E = 0, every frame. Neither ever deletes itself. */
static bool door_tick(NpcSet *set, Npc *n)
{
    bool destroyable = (n->scene->object == NPC_OBJ_DESTROYABLE_DOOR);
    if (n->frozen) {
        return true;
    }
    switch (n->routine) {
        case 0: {
            n->box   = n->scene->box;
            n->frame = 0;
            n->anim  = destroyable ? 0 : 0xF;
            int at = 0x10 + n->index;
            bool flagged = set->c700 != NULL && at < 0x100
                        && set->c700[at] != 0;
            if (flagged) {
                n->frame   = destroyable ? 3 : 2;
                n->routine = 3;
            } else {
                n->routine = 1;
            }
            break;
        }
        case 1: {
            break;
        }
        case 2: {
            if (destroyable) {
                int slot = first_free_slot(set);
                if (slot < 0) {
                    break;
                }
                put_scene(set, slot, scene_of(NPC_OBJ_BURST), n->x, n->y, 0);
                n->frame   = 3;
                n->routine = 3;
                break;
            }
            n->anim--;
            if (n->anim < 0) {
                n->anim = 0xF;
                n->frame++;
                if (n->frame == 2) {
                    n->routine = 3;
                }
            }
            break;
        }
        default: {
            n->box         = 0;
            n->interaction = 0;
            n->index       = 0;
            break;
        }
    }
    return true;
}

/* loc_55BC / loc_55F8, then loc_561A (rts) every frame after. */
static bool block_tick(Npc *n)
{
    if (n->routine == 0) {
        n->box     = n->scene->box;
        n->routine = 1;
    }
    return true;
}

/* Object $3D (loc_580A, ps2.asm:8013): the pump SpriteData_MotaWorldMap
 * puts on Motavia at 1168,1168, in view of loc_7E02's cases 4 and 6
 * ($490,$4A0). loc_5822, the init: 2(a0) = 2 (hidden), tile word $4437,
 * mappings loc_5E516, $2C = 4, $32 = $2A = 0; with $C737 set, 2(a0) = 0
 * and $22 = 1 -- clear, $22 stays 0 and the init runs again next frame.
 * loc_585C, every frame: $24 = Level_SpriteMappingsArray2[$32] (0, 1, 0,
 * 1) + $2A, $32 + 1 & 3 -- the pumping; once $C735 is set, $22 = 2.
 * loc_588A, every frame: $24 = loc_58AC[$32] (0, 1, 2, 3) + $2A. There is
 * no timer: a new frame every field. */
static bool pump_tick(NpcSet *set, Npc *n)
{
    static const u8 PUMPING[4] = { 0, 1, 0, 1 };
    static const u8 FULL[4]    = { 0, 1, 2, 3 };
    bool nei_gone = set->c700 != NULL && set->c700[FLAG_NEI_GONE] != 0;
    bool climatrol_destroyed = set->c700 != NULL
                               && set->c700[FLAG_CLIMATROL_DESTROYED] != 0;

    switch (n->routine) {
        case 0: {
            n->hidden     = true;
            n->box        = n->scene->box;
            n->anim_index = 0;
            n->facing     = 0;
            n->frame      = 0;
            if (nei_gone) {
                n->hidden  = false;
                n->routine = 1;
            }
            break;
        }
        case 1: {
            n->frame      = PUMPING[n->anim_index] + n->facing;
            n->anim_index = (n->anim_index + 1) & 3;
            if (climatrol_destroyed) {
                n->routine = 2;
            }
            break;
        }
        default: {
            n->frame      = FULL[n->anim_index] + n->facing;
            n->anim_index = (n->anim_index + 1) & 3;
            break;
        }
    }
    return true;
}

/* ObjNeifirst_Init (ps2.asm:7713): 2(a0) = 0, tile word $450, mappings
 * loc_5D17A, $2C = 4, routine 1; then `tst.w ($FFFFE400).w / beq` and
 * `tst.b $C737 / beq Main` -- with no party object, or Nei gone, 2(a0) =
 * 1 and RunObject deletes her. ObjNeifirst_Main is rts. */
static bool neifirst_tick(NpcSet *set, Npc *n)
{
    if (n->routine != 0) {
        return true;
    }
    n->box     = n->scene->box;
    n->frame   = 0;
    n->routine = 1;
    if (set->party_gone) {
        return false;
    }
    if (set->c700 != NULL && set->c700[FLAG_NEI_GONE] != 0) {
        return false;
    }
    return true;
}

const NpcSceneType *npc_scene_type(int object)
{
    return scene_of(object);
}

/* Obj_RandomExplosion (ps2.asm:7509). loc_51CA, the init: $C/$10 from
 * $A/$E, 2(a0) = $12 (hidden, screen-relative), tile word $400, mappings
 * loc_5B574, $24 = -1, routine 1. loc_51F8, every frame after: $26 down,
 * and once it is below zero, $26 = 7 and 2(a0) = $30 (shown, in front);
 * SXFID_Explosion if $24 was -1; $24 + 1; at 5 a new place from the base
 * (GenerateRandomNumber & $1F - $F on each axis), a new count
 * (GenerateRandomNumber & $1F), $24 = -1 and 2(a0) = $12 again. Three
 * RNG calls on the reset, none otherwise. */
static bool explosion_tick(NpcSet *set, Npc *n, Rng *rng)
{
    if (n->routine == 0) {
        n->base_x       = n->x;
        n->base_y       = n->y;
        n->hidden       = true;
        n->screen_fixed = true;
        n->front        = false;
        n->box          = 0;
        n->frame        = -1;
        n->routine      = 1;
        return true;
    }
    n->anim--;
    if (n->anim >= 0) {
        return true;
    }
    n->anim   = 7;
    n->hidden = false;
    n->front  = true;
    if (n->frame < 0 && set->sound != NULL) {
        set->sound(set->sound_ctx, NPC_SFX_EXPLOSION);
    }
    n->frame++;
    if (n->frame == 5) {
        if (rng != NULL) {
            n->x    = (s16)(n->base_x + (rng_next(rng) & 0x1F) - 0xF);
            n->y    = (s16)(n->base_y + (rng_next(rng) & 0x1F) - 0xF);
            n->anim = (s16)(rng_next(rng) & 0x1F);
        } else {
            n->anim = 0;
        }
        n->frame  = -1;
        n->hidden = true;
        n->front  = false;
    }
    return true;
}

/* RandomExpl_PosOffsets (ps2.asm:20403), in 16-pixel units of the
 * sprite plane: $80,$80 is the screen's top-left corner. */
static const u8 EXPLOSION_AT[NPC_EXPLOSIONS][2] = {
    { 0x0E, 0x0A }, { 0x12, 0x0A }, { 0x16, 0x0A }, { 0x0B, 0x0D },
    { 0x0F, 0x0D }, { 0x15, 0x0D }, { 0x19, 0x0D }, { 0x0A, 0x10 },
    { 0x0E, 0x10 }, { 0x16, 0x10 }, { 0x1A, 0x10 }, { 0x0A, 0x12 },
    { 0x0F, 0x15 }, { 0x12, 0x13 }, { 0x15, 0x15 }, { 0x1A, 0x12 },
};

void npc_spawn_explosions(NpcSet *set, Rng *rng, bool late)
{
    const NpcSceneType *st = scene_of(NPC_OBJ_RANDOM_EXPLOSION);
    if (st == NULL) {
        fprintf(stderr, "npc: no RandomExplosion in scene_objects.json -- "
                        "rerun make extract-npcs, then make manifest\n");
        return;
    }
    for (int i = 0; i < NPC_EXPLOSIONS; i++) {
        Npc *n = put_scene(set, NPC_EXPLOSION_SLOT + i, st,
                           EXPLOSION_AT[i][0] << 4, EXPLOSION_AT[i][1] << 4,
                           0);
        if (n == NULL) {
            continue;
        }
        u16 roll = (rng != NULL) ? rng_next(rng) : 0;
        n->anim = (s16)((roll & 0x3F) + (late ? 0x40 : 0));
    }
}


/* ------------------------------------------------------------------ */
/* The Esper mansion (build 87)                                       */
/* ------------------------------------------------------------------ */

static u8 flag_byte(const NpcSet *set, int at)
{
    return (set->c700 != NULL) ? set->c700[at] : 0;
}

/* Objects $23, $24 and $25 (loc_4B28, loc_4BB0, loc_4C38; ps2.asm:6936).
 * The init -- every frame until $C743 is 2 -- sets the frame, a wait and
 * a step count; routine 1 waits out $28, then moves a pixel every third
 * ($23, $24) or fourth ($25) frame -- $14 set for one frame, RunObject
 * adding it -- until x is the stop, tested on the frame of a move before
 * that move lands. Routine 2 is `rts`. None looks at 3(a0). */
static const struct {
    int frame, wait, every, dir, stop;
} MANSION_SLIDE[3] = {
    { 3, 0x0D0, 2,  1, 0x158 },     /* $23: loc_4B40, loc_4B78 */
    { 2, 0x160, 2,  1, 0x138 },     /* $24: loc_4BC8, loc_4C00 */
    { 1, 0x1F8, 3, -1, 0x0F8 },     /* $25: loc_4C50, loc_4C88 */
};

static bool mansion_slide_tick(NpcSet *set, Npc *n)
{
    int k = n->scene->object - NPC_OBJ_MANSION_B1_FIRST;
    switch (n->routine) {
        case 0: {
            n->frame = MANSION_SLIDE[k].frame;
            n->wait  = (s16)MANSION_SLIDE[k].wait;
            n->anim  = (s16)MANSION_SLIDE[k].every;
            if (flag_byte(set, FLAG_ESPER_B1_TALKS) == 2) {
                n->routine = 1;
            }
            break;
        }
        case 1: {
            n->wait--;
            if (n->wait >= 0) {
                break;
            }
            n->anim--;
            if (n->anim >= 0) {
                n->vx = 0;
                break;
            }
            n->vx   = (s16)MANSION_SLIDE[k].dir;
            n->anim = (s16)MANSION_SLIDE[k].every;
            if (n->x == MANSION_SLIDE[k].stop) {
                n->vx      = 0;
                n->routine = 2;
            }
            break;
        }
        default: {
            break;
        }
    }
    return true;
}

/* Object $26 (loc_4CC0): its only routine is the init, so it runs every
 * frame -- $24 never set (frame 0), hit-box row $A, or $B while $C743 is
 * 3. Its $2E is 0: ObjType_Events 0, $1708. */
static bool mansion_box_tick(NpcSet *set, Npc *n)
{
    n->box = (flag_byte(set, FLAG_ESPER_B1_TALKS) == 3) ? 0xB : 0xA;
    return true;
}

/* Object $27 (loc_4CFC). Init as the others' (frame left at 0, $28 =
 * $240, $26 = 1). Routine 1: after $28, a pixel left every second frame
 * until x is $D0, then $28 = $78, $26 = 7, $32 = $50. Routine 2
 * (loc_4D92): after $28, the frame steps every eighth frame, and at 2
 * routine 3. Routine 3 (loc_4DBE): $32 counts down, then every frame the
 * building screen: BuildingID_EsperMansion with portrait $39. */
static bool mansion_lutz_tick(NpcSet *set, Npc *n)
{
    switch (n->routine) {
        case 0: {
            n->wait = 0x240;
            n->anim = 1;
            if (flag_byte(set, FLAG_ESPER_B1_TALKS) == 2) {
                n->routine = 1;
            }
            break;
        }
        case 1: {
            n->wait--;
            if (n->wait >= 0) {
                break;
            }
            n->anim--;
            if (n->anim >= 0) {
                n->vx = 0;
                break;
            }
            n->vx   = -1;
            n->anim = 1;
            if (n->x == 0xD0) {
                n->vx         = 0;
                n->wait       = 0x78;
                n->anim       = 7;
                n->anim_index = 0x50;
                n->routine    = 2;
            }
            break;
        }
        case 2: {
            n->wait--;
            if (n->wait >= 0) {
                break;
            }
            n->anim--;
            if (n->anim >= 0) {
                break;
            }
            n->frame++;
            n->anim = 7;
            if (n->frame == 2) {
                n->vx      = 0;
                n->routine = 3;
            }
            break;
        }
        default: {
            n->anim_index--;
            if (n->anim_index < 0) {
                set->esper_building = true;
            }
            break;
        }
    }
    return true;
}

/* The Earthmen's leader (object $28, loc_4DD8). Nothing while frozen
 * (`tst.b 3(a0)`, which A never sets for him: his $2E is an event's, not
 * a person's). Otherwise his only routine is the init (loc_4DEE), so it
 * runs every frame: tile word $44C, mappings loc_5D986, hit-box row 4.
 * It never sets $24, so of the mappings' three frames only the first is
 * ever shown, and he never moves. */
static bool earthman_leader_tick(Npc *n)
{
    if (n->frozen) {
        return true;
    }
    n->box = n->scene->box;
    return true;
}

/* Obj_Esper ($2E, ps2.asm:7555). Nothing while someone talks to it
 * (3(a0)). Init: frame 3, box 4, routine 1 (`rts`); on B1 with $C743 = 3,
 * 2(a0) = 1 and RunObject deletes it. */
static bool esper_tick(NpcSet *set, Npc *n)
{
    if (n->frozen || n->routine != 0) {
        return true;
    }
    n->frame   = 3;
    n->box     = n->scene->box;
    n->routine = 1;
    return !(flag_byte(set, FLAG_ESPER_B1_TALKS) == 3
             && set->level_index == LEVEL_ESPER_MANSION_B1);
}

/* Obj_MovingEsper ($30, ps2.asm:7604), the two at F1's door. Nothing
 * while frozen. Init: frame 3, $34 = 0, box 4, routine 1; with $C742
 * already set, routine 4 at x $118 frame 6 -- or, for the one that did
 * not start at $110, x $E8 frame 9. Routine 1 waits for $C742 (loc_E05A
 * sets it). Routine 2: the one at $F0 faces 6 and moves -1, the other
 * faces 9 and +1. Routine 3, every frame: frame = the walk cycle
 * (Level_SpriteMappingsArray, 0 1 0 2) + facing, and after eight frames
 * -- eight pixels -- stopped on frame 3, routine 4 (`rts`). */
static bool moving_esper_tick(NpcSet *set, Npc *n)
{
    if (n->frozen) {
        return true;
    }
    switch (n->routine) {
        case 0: {
            n->frame   = 3;
            n->hold    = 0;
            n->box     = n->scene->box;
            n->routine = 1;
            if (flag_byte(set, FLAG_DOOR_ESPERS_LET_ROLF_IN) != 0) {
                s16 was    = n->x;
                n->routine = 4;
                n->x       = 0x118;
                n->frame   = 6;
                if (was != 0x110) {
                    n->x     = 0xE8;
                    n->frame = 9;
                }
            }
            break;
        }
        case 1: {
            if (flag_byte(set, FLAG_DOOR_ESPERS_LET_ROLF_IN) != 0) {
                n->routine = 2;
            }
            break;
        }
        case 2: {
            if (n->x == 0xF0) {
                n->facing = 6;
                n->vx     = -1;
            } else {
                n->facing = 9;
                n->vx     = 1;
            }
            n->routine = 3;
            break;
        }
        case 3: {
            int i         = n->anim_index;
            n->anim_index = (n->anim_index + 1) & 3;
            n->frame      = SCENE_CYCLE[i] + n->facing;
            n->anim       = 4;
            n->hold++;
            if (n->hold == 8) {
                n->vx      = 0;
                n->frame   = 3;
                n->routine = 4;
            }
            break;
        }
        default: {
            break;
        }
    }
    return true;
}

/* $31 (loc_5408) and $33 (loc_549A), both at F1 $110,$FF. Nothing while
 * frozen. $31: 2(a0) = 0, box $C, frame as placed (0); its $2E is 0, so
 * A there is $1708. $33: hidden (2(a0) = 2), frame 1, box $C -- shown
 * once, at its init, if $C743 is 3; its $2E is $34, loc_E0AA: Lutz. A
 * hidden object is never on screen, so nothing hits it. */
static bool mansion_door_tick(NpcSet *set, Npc *n)
{
    if (n->frozen || n->routine != 0) {
        return true;
    }
    n->box     = n->scene->box;
    n->routine = 1;
    if (n->scene->object == NPC_OBJ_MANSION_DOOR2) {
        n->frame  = 1;
        n->hidden = (flag_byte(set, FLAG_ESPER_B1_TALKS) != 3);
    } else {
        n->hidden = false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Mother Brain's image and the energy arcs (build 92)                */
/* ------------------------------------------------------------------ */

/* loc_51A0: the image forming, frame offsets from 3 -- the small and the
 * larger sparkle in turn, then the two big ones. Twenty are used. */
static const u8 IMAGE_FORMING[22] = {
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 3, 0,
};
static const u8 IMAGE_FLICKER[4] = { 0, 1, 0, 1 };  /* Level_SpriteMappingsArray2 */
static const u8 ARC_FLICKER[4]   = { 0, 1, 2, 0 };  /* Level_SpriteMappingsArray3 */

/* Object $2C, Mother Brain's image (loc_50AE, ps2.asm:7420). Nothing while
 * 3(a0) is set. Routine 0 (loc_50D0): at $210,$1F0, hidden, frame 3,
 * $2A = 3, $28 = $1C, $26 = $A. Routine 1 (loc_5110): $28 down, and when
 * it passes 0 she is shown and routine 2 starts. Routine 2 (loc_5124):
 * $26 down once -- it is never reset, so after eleven frames it stays
 * negative -- then one IMAGE_FORMING frame a frame (+ $2A) until twenty
 * have gone; then routine 3 with $2A = 7. Routine 3 (loc_5166): y $208,
 * frames 7 and 8 in turn every frame, and whenever $28 passes 0 it is
 * $27 again and that frame is 9 instead -- which is the first frame,
 * since $28 is already below 0 from routine 1. */
static bool mother_brain_image_tick(Npc *n)
{
    if (n->frozen) {
        return !n->doomed;
    }
    switch (n->routine) {
        case 0: {
            n->x          = 0x210;
            n->y          = 0x1F0;
            n->hidden     = true;
            n->box        = 0;
            n->frame      = 3;
            n->facing     = 3;
            n->wait       = 0x1C;
            n->anim       = 0x0A;
            n->anim_index = 0;
            n->routine    = 1;
            break;
        }
        case 1: {
            n->wait--;
            if (n->wait < 0) {
                n->hidden  = false;
                n->routine = 2;
            }
            break;
        }
        case 2: {
            n->anim--;
            if (n->anim >= 0) {
                break;
            }
            n->frame = IMAGE_FORMING[n->anim_index] + n->facing;
            n->anim_index++;
            if (n->anim_index == 0x14) {
                n->routine    = NPC_MOTHER_BRAIN_IMAGE_FORMED;
                n->facing     = 7;
                n->anim       = 0x0A;
                n->anim_index = 0;
            }
            break;
        }
        default: {
            n->y = 0x208;
            int i = n->anim_index;
            n->anim_index = (n->anim_index + 1) & 3;
            n->frame = IMAGE_FLICKER[i] + n->facing;
            n->wait--;
            if (n->wait < 0) {
                n->wait  = 0x27;
                n->frame = 9;
            }
            break;
        }
    }
    return !n->doomed;
}

/* Object $45, an energy arc (loc_59FA, ps2.asm:8196). Routine 0
 * (loc_5A12): hidden, $28 = $19. Routine 1 (loc_5A3A): $28 down; past 0
 * it is shown, $28 = $5F, routine 2. Routine 2 (loc_5A54): $28 down --
 * past 0 it deletes itself (2(a0) = 1) -- else frames 0, 1, 2, 0 in
 * turn (+ $2A, which nothing sets) and $28 down a second time, so it
 * lasts about half of $5F frames. */
static bool energy_arc_tick(Npc *n)
{
    switch (n->routine) {
        case 0: {
            n->hidden     = true;
            n->box        = 0;
            n->wait       = 0x19;
            n->anim_index = 0;
            n->routine    = 1;
            break;
        }
        case 1: {
            n->wait--;
            if (n->wait < 0) {
                n->hidden  = false;
                n->wait    = 0x5F;
                n->routine = 2;
            }
            break;
        }
        default: {
            n->wait--;
            if (n->wait < 0) {
                return false;
            }
            int i = n->anim_index;
            n->anim_index = (n->anim_index + 1) & 3;
            n->frame = ARC_FLICKER[i] + n->facing;
            n->wait--;
            break;
        }
    }
    return !n->doomed;
}

void npc_spawn_mother_brain_image(NpcSet *set)
{
    if (scene_of(NPC_OBJ_MOTHER_BRAIN_IMAGE) == NULL
        || scene_of(NPC_OBJ_ENERGY_ARC) == NULL) {
        fprintf(stderr, "npc: no MotherBrainImage/EnergyArc in "
                        "scene_objects.json -- rerun make extract-npcs, "
                        "then make manifest\n");
        return;
    }
    npc_put(set, NPC_MOTHER_BRAIN_IMAGE_SLOT, NPC_OBJ_MOTHER_BRAIN_IMAGE, 0,
            0, 0);
    npc_put(set, NPC_ENERGY_ARC_SLOT, NPC_OBJ_ENERGY_ARC, 0x20C, 0x1B1, 0);
    npc_put(set, NPC_ENERGY_ARC_SLOT + 1, NPC_OBJ_ENERGY_ARC, 0x214, 0x1B1,
            0);
}

static bool scene_tick(NpcSet *set, Npc *n, Rng *rng)
{
    switch (n->scene->object) {
        case NPC_OBJ_MOTHER_BRAIN_IMAGE: {
            return mother_brain_image_tick(n);
        }
        case NPC_OBJ_ENERGY_ARC: {
            return energy_arc_tick(n);
        }
        case NPC_OBJ_EARTHMAN_LEADER: {
            return earthman_leader_tick(n);
        }
        case NPC_OBJ_NEIFIRST: {
            return neifirst_tick(set, n);
        }
        case NPC_OBJ_RANDOM_EXPLOSION: {
            return explosion_tick(set, n, rng);
        }
        case NPC_OBJ_INVISIBLE_BLOCK: {
            return block_tick(n);
        }
        case NPC_OBJ_INVISIBLE_BLOCK2: {
            return block_tick(n);
        }
        case NPC_OBJ_OPENABLE_DOOR: {
            return door_tick(set, n);
        }
        case NPC_OBJ_DESTROYABLE_DOOR: {
            return door_tick(set, n);
        }
        case NPC_OBJ_DARUM: {
            return darum_tick(set, n);
        }
        case NPC_OBJ_TEIM: {
            return teim_tick(set, n);
        }
        case NPC_OBJ_BURST: {
            return burst_tick(set, n);
        }
        case NPC_OBJ_DEAD_BODY: {
            return dead_body_tick(n);
        }
        case NPC_OBJ_JET_SCOOTER: {
            return jet_scooter_tick(set, n);
        }
        case NPC_OBJ_PUMP: {
            return pump_tick(set, n);
        }
        case NPC_OBJ_MANSION_B1_FIRST: {
            return mansion_slide_tick(set, n);
        }
        case NPC_OBJ_MANSION_B1_FIRST + 1: {
            return mansion_slide_tick(set, n);
        }
        case NPC_OBJ_MANSION_B1_FIRST + 2: {
            return mansion_slide_tick(set, n);
        }
        case NPC_OBJ_MANSION_B1_BOX: {
            return mansion_box_tick(set, n);
        }
        case NPC_OBJ_MANSION_B1_LUTZ: {
            return mansion_lutz_tick(set, n);
        }
        case NPC_OBJ_ESPER: {
            return esper_tick(set, n);
        }
        case NPC_OBJ_MOVING_ESPER: {
            return moving_esper_tick(set, n);
        }
        case NPC_OBJ_MANSION_DOOR: {
            return mansion_door_tick(set, n);
        }
        case NPC_OBJ_MANSION_DOOR2: {
            return mansion_door_tick(set, n);
        }
        default: {
            return true;
        }
    }
}

/* RunObjects over the level's objects. `setup` is LevelScreen's own pass
 * before the display comes on (ps2.asm:11418): every object's routine runs
 * once -- a chest's init shows it open or shut, Darum's and Teim's may
 * delete them -- and each is placed on screen, so the first frame shown
 * already has them. The people's init is npc_spawn's; in the setup pass
 * they only take their place. */
static void run_objects(NpcSet *set, World *w, Rng *rng, int width,
                        int height, int cam_x, int cam_y, bool setup)
{
    for (int i = 0; i < set->count; i++) {
        Npc *n = &set->npc[i];

        if (n->free) {
            n->on_screen = false;
            continue;
        }
        if (n->chest != NULL) {
            /* loc_5656: open once its flag says so. */
            if ((chest_flag(set, n) & 0x7F) != 0) {
                n->frame = 1;
            }
        } else if (n->scene != NULL) {
            /* RunObject: a deleted object is cleared before it moves or
             * is drawn (loc_3DC). */
            if (!scene_tick(set, n, rng)) {
                memset(n, 0, sizeof *n);
                n->free = true;
                continue;
            }
        } else if (!setup && n->type != NULL && !n->frozen) {
            int kind = n->type->wander;
            if (kind == NPC_WANDER_DEZOLIAN && n->routine == 1) {
                decide_dezolian(w, n, rng);
            } else if (kind == NPC_WANDER_DEZOLIAN) {
                walk(n, n->type->first_wait);       /* loc_4234 */
            } else if (kind == NPC_WANDER_MUSKCAT && n->routine == 1) {
                decide_muskcat(w, n, rng);
            } else if (kind == NPC_WANDER_MUSKCAT) {
                walk_muskcat(n);
            } else if (n->type->four && n->routine == 1) {
                decide_four(w, n, rng);
            } else if (n->type->four) {
                walk_four(n);
            } else if (n->routine == 1) {
                decide(w, n, rng);
            } else {
                walk(n, WAIT_FRAMES);
            }
        }

        /* RunObject's tail: velocity, wrap, and whether it is on screen
         * (loc_3E4: $40 < x < $1E0, $40 < y < $180 in sprite space). */
        n->x = (s16)wrap(n->x + n->vx, width);
        n->y = (s16)wrap(n->y + n->vy, height);

        int sx, sy;
        if (n->screen_fixed) {
            /* bit 4 of 2(a0): no camera, and no $80 either. */
            sx = n->x;
            sy = n->y;
        } else {
            sx = wrap(n->x - cam_x, width) + SPRITE_BIAS;
            sy = wrap(n->y - cam_y, height) + SPRITE_BIAS;
        }
        n->on_screen = sx > 0x40 && sx < 0x1E0 && sy > 0x40 && sy < 0x180;
        if (n->on_screen) {
            n->screen_x = (s16)sx;
            n->screen_y = (s16)sy;
        }
    }
}

void npc_tick(NpcSet *set, Rng *rng, NpcMapTest map_ok, void *map_ctx,
              int width, int height, int cam_x, int cam_y,
              const s16 *party_x, const s16 *party_y, int party_count)
{
    World w = { set, map_ok, map_ctx, party_x, party_y, party_count };
    run_objects(set, &w, rng, width, height, cam_x, cam_y, false);
    build_sprites_d6(set, &w, width, height, cam_x, cam_y);
}

void npc_setup(NpcSet *set, int width, int height, int cam_x, int cam_y)
{
    World w = { set, NULL, NULL, NULL, NULL, 0 };
    run_objects(set, &w, NULL, width, height, cam_x, cam_y, true);
    build_sprites_d6(set, &w, width, height, cam_x, cam_y);
}

/* ------------------------------------------------------------------ */
/* Talking                                                            */
/* ------------------------------------------------------------------ */

u16 npc_dialogue_script(int index, bool past_climatrol, int cap)
{
    if (index == 0) {
        return SCRIPT_ACTION_THERE_SEEMS_NOTHING_UNUSUAL;      /* "There seems to be nothing unusual here." */
    }

    if (index < 0x5D) {
        /* `lea (table - 1), a0; adda.w d1, a0`. The earlier table is
         * shorter than the index range and simply runs on into the one
         * after it in the ROM. */
        if (past_climatrol) {
            return (index - 1 < g_talk_after_n)
                 ? (u16)(0x1600 | g_talk_after[index - 1]) : SCRIPT_ACTION_THERE_SEEMS_NOTHING_UNUSUAL;
        }
        int at = index - 1;
        if (at < g_talk_before_n) {
            return (u16)(0x1600 | g_talk_before[at]);
        }
        at -= g_talk_before_n;
        return (at < g_talk_later_n) ? (u16)(0x1600 | g_talk_later[at])
                                     : SCRIPT_ACTION_THERE_SEEMS_NOTHING_UNUSUAL;
    }

    if (index < 0x80) {
        /* loc_E584: the Dezolians ($5D-$7A) and the musk cats ($7B-$7F)
         * make sense only to a live Rolf in a cap. A Magic Cap reads
         * loc_E70C - 1 + index, which runs on into loc_E730 as above; a
         * Mogic Cap reads loc_E730 - 1 + index. */
        if (cap == NPC_CAP_MAGIC) {
            int at = index - 1;
            if (at < g_talk_before_n) {
                return (u16)(0x1600 | g_talk_before[at]);
            }
            at -= g_talk_before_n;
            return (at < g_talk_later_n) ? (u16)(0x1600 | g_talk_later[at])
                                         : SCRIPT_ACTION_THERE_SEEMS_NOTHING_UNUSUAL;
        }
        if (cap == NPC_CAP_MOGIC) {
            return (index - 1 < g_talk_later_n)
                 ? (u16)(0x1600 | g_talk_later[index - 1]) : SCRIPT_ACTION_THERE_SEEMS_NOTHING_UNUSUAL;
        }
        /* loc_E5B8: without one. */
        return (index >= 0x7B) ? SCRIPT_PEOPLE_MEW_MEEYAW : SCRIPT_PEOPLE_GA_DOTE_MEW_BANAPUT;
    }

    /* $85, $87, $88 and $8F start their own little scenes (loc_E5FE and
     * friends); those are not ported and fall through to the table. */
    return (index < g_talk_later_n) ? (u16)(0x1600 | g_talk_later[index])
                                    : SCRIPT_ACTION_THERE_SEEMS_NOTHING_UNUSUAL;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static void push_piece(const Npc *n, const NpcPiece *p, int tile, int line,
                       Video *v)
{
    Sprite s;
    memset(&s, 0, sizeof s);
    s.x      = (s16)(n->screen_x - SPRITE_BIAS + p->x);
    s.y      = (s16)(n->screen_y - SPRITE_BIAS + p->y);
    s.tile   = (u16)(tile + p->tile);
    s.w      = p->w;
    s.h      = p->h;
    s.pal    = (u8)line;
    s.flip_h = p->flip_h;
    s.flip_v = p->flip_v;
    video_sprite_push(v, &s);
}

void npc_push_sprite(const Npc *n, Video *v)
{
    if (!n->on_screen || n->hidden) {
        return;
    }
    if (n->chest != NULL) {
        const NpcChestType *ct = n->chest;
        int f = n->frame & 1;
        int count = n->alt ? ct->alt_pieces[f] : ct->pieces[f];
        for (int i = 0; i < count; i++) {
            push_piece(n, n->alt ? &ct->alt_frame[f][i] : &ct->frame[f][i],
                       ct->tile, ct->line, v);
        }
        return;
    }
    if (n->scene != NULL) {
        const NpcSceneType *st = n->scene;
        int f = (n->frame >= 0 && n->frame < st->frames) ? n->frame : 0;
        for (int i = 0; i < st->pieces[f]; i++) {
            push_piece(n, &st->frame[f][i], st->tile, st->line, v);
        }
        return;
    }
    if (n->type == NULL) {
        return;
    }
    int f = n->frame % NPC_FRAMES;
    for (int i = 0; i < n->type->pieces[f]; i++) {
        push_piece(n, &n->type->frame[f][i], n->type->tile, n->type->line,
                   v);
    }
}
