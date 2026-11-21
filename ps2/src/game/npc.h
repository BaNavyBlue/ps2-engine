#ifndef PS2_NPC_H
#define PS2_NPC_H

#include "../core/rng.h"
#include "../core/tileset.h"
#include "../core/video.h"

/* The people in a town: LoadSpritesInLevel (ps2.asm:799), the Obj_Mota*
 * objects (5762...) and the hit test everything on the map shares,
 * Level_ChkTargetInteract (10505). The treasure chests
 * (Obj_DezoTreasureChest, Obj_MotaTreasureChest, 7856) are objects in the
 * same slots: they stand in the way of the party and the people alike,
 * and the A button finds them the same way.
 *
 * All nine Motavian types run one of two routines that differ only in
 * axis. Every $20 frames (the type's own count the first time) a person
 * rolls a number 0-15: four values turn them to face a direction, two
 * walk them one 16px cell one way and two the other, the rest do nothing.
 * They never reverse without turning first, never stray more than five
 * cells from where they started, and do not step onto a solid quadrant,
 * the party, or one another. Roron's Motas ($1B), the Dezolians ($1A) and
 * the musk cats ($1C) each have a four-way routine of their own (npc.c). */

#define NPC_MAX        32
#define NPC_TYPES_MAX  16
#define NPC_FRAMES     20     /* loc_5A394, the musk cat's */
#define NPC_TYPE_PIECES 2     /* loc_59EBA: a Dezolian is two pieces */
#define NPC_ART_MAX    32
#define NPC_LISTS_MAX  16
#define NPC_LIST_ART   8
#define NPC_LEVELS_MAX 128

#define NPC_INTERACT_EVENT   0
#define NPC_INTERACT_PEOPLE  1
#define NPC_INTERACT_CHEST   2
#define NPC_INTERACT_TEIM    3      /* ObjType_TeimDarum */

#define NPC_CHEST_PIECES     2
#define NPC_CHESTS_MAX       128    /* treasure_chest_flags: $C780-$C7FF */

#define NPC_SCENE_FRAMES     12     /* Map_JetScooter */
#define NPC_SCENE_PIECES     47     /* the ending UFO's last frame; 33
                                     * (Map_PalmExplosion_4) before build
                                     * 93a, 6 (the jet scooter's) before
                                     * build 82 */

/* The Teim and Darum scene's objects, by ObjID_. */
#define NPC_OBJ_DARUM        0x29
#define NPC_OBJ_TEIM         0x2A
#define NPC_OBJ_BURST        0x2B

/* Obj_DeadBody: the scoundrels on Shure's top floor. It has no routine
 * of its own beyond its init, so it rides with the scene objects. */
#define NPC_OBJ_DEAD_BODY    0x2F

/* Obj_OpenableDoor ($3B, ps2.asm:7915) and Obj_DestroyableDoor ($3C,
 * 7961): the locked doors a key or card opens and the ones Dynamite
 * blows. Both stand in a doorway with hit-box row 6 until event_flags
 * [$2F(a0)] is set -- by the item action in the menu -- and the demo
 * walk that follows ends in $FD, which puts routine 2 in the level's
 * first object ($FFFFE822). $3B's routine 2 swings the door open over
 * two frames of $F ticks; $3C's spawns the burst ($2B) on itself and
 * shows its blown frame. Routine 3 clears the box and the $2E word, so
 * nothing bumps into or talks to it again. Placed already flagged, a
 * door starts on its open or blown frame in routine 3. */
#define NPC_OBJ_INVISIBLE_BLOCK  0x37  /* Obj_InvisibleBlock, $2C = 9   */
#define NPC_OBJ_INVISIBLE_BLOCK2 0x38  /* Obj_InvisibleBlock2, $2C = 7  */
#define NPC_OBJ_OPENABLE_DOOR    0x3B
#define NPC_OBJ_DESTROYABLE_DOOR 0x3C
#define NPC_DOOR_SLOTS           16     /* loc_57C6: $E800 + $40 * 0..$F */

/* Obj_JetScooter (ps2.asm:6767): placed first on Motavia, and put back in
 * slot 0 at the parking place ($C654/$C656) by LoadSpritesInLevel's tail.
 * Parked it is an event object, $2E = 6 (boarding, loc_DAA4). Its init
 * runs every frame until it finds jet_scooter_flag set; from then on its
 * main routine, the one that drives it, is walk.c's (walk_tick while
 * Walk.riding). npc.c runs the init and RunObject's tail for it. */
#define NPC_OBJ_JET_SCOOTER  0x22

/* Object $1B (loc_4288, ps2.asm:6273), the Mota people in Roron. They
 * wander four ways rather than along one axis: every $30 frames a roll
 * of 0-15 turns them (1 down, 3 up, 5 left, 7 right -- not from the
 * opposite facing), steps them a cell ($A/$B up, $C/$D down, 8/9 left,
 * $E/$F right -- not straight back from the way they face), or does
 * nothing, within three cells up or down and five either side of where
 * they started. The two on RoronF5 with dialogue $86 and $87 -- the
 * scooter's maker and his friend -- delete themselves in their init once
 * $C716 is 2, the scooter ridden. */
#define NPC_OBJ_MOTA_RORON   0x1B

/* Obj_Neifirst ($32, ps2.asm:7699): one 16x32 piece at tile $450, hit-box
 * row 4, standing on Climatrol F7 with $2E = $0025 until her init finds
 * $C737 set or slot 0 empty ($FFFFE400: the death scene's reloads clear
 * it before the objects run), when it deletes her.
 * Obj_RandomExplosion ($2D, 7509): the sixteen flashes loc_DE52 puts in
 * slots 16-31 ($FFFFEC00 on) during the shaking of Climatrol. Each is
 * screen-relative (bit 4 of 2(a0): $A/$E are sprite coordinates, no
 * camera, no $80 bias) and hidden (bit 1) until its timer runs out, then
 * shows its five frames eight fields each, ahead of every row (bit 5),
 * with SXFID_Explosion on the first, and hides again at a new spot up to
 * 15 pixels from its base for a new random count (loc_51F8). */
#define NPC_OBJ_RANDOM_EXPLOSION 0x2D
#define NPC_OBJ_PUMP             0x3D   /* loc_580A: Motavia's pump */
#define NPC_OBJ_NEIFIRST         0x32

/* The Esper mansion (build 87; npc.c has the routines). */
#define NPC_OBJ_MANSION_B1_FIRST 0x23   /* $23-$25 slide apart, $26 has
                                         * the box, $27 walks and opens
                                         * the building */
#define NPC_OBJ_MANSION_B1_BOX   0x26
#define NPC_OBJ_MANSION_B1_LUTZ  0x27
#define NPC_OBJ_ESPER            0x2E
#define NPC_OBJ_MOVING_ESPER     0x30
#define NPC_OBJ_MANSION_DOOR     0x31
#define NPC_OBJ_MANSION_DOOR2    0x33
#define NPC_OBJ_MOTHER_BRAIN_IMAGE 0x2C /* loc_50AE: the arrival scene's */
#define NPC_OBJ_ENERGY_ARC       0x45   /* loc_59FA: two bring her in    */
/* The Earthmen's leader (object $28, loc_4DD8, ps2.asm:7166; build 97d):
 * the only object on Noah F1's list, standing at 33,7 -- at the far end
 * of the corridor, where the walk to the Earthmen stops two cells below
 * him -- from the level's load, whatever the flags. His $2E is 0, so A
 * at him is event 0, "nothing unusual here" ($1708). */
#define NPC_OBJ_EARTHMAN_LEADER  0x28
#define NPC_MOTHER_BRAIN_IMAGE_SLOT 16  /* $EC00                         */
#define NPC_ENERGY_ARC_SLOT      17     /* $EC40 and $EC80               */
#define NPC_MOTHER_BRAIN_IMAGE_FORMED 3 /* its $22 once she stands: what
                                           the confrontation waits for */
#define NPC_EXPLOSION_SLOT       16
#define NPC_EXPLOSIONS           16
#define NPC_SFX_EXPLOSION        0xA3
#define NPC_SFX_SCOOTER_ON   0xD8   /* the engine: special SFX 0 */

typedef struct {
    s8   x, y;
    u8   w, h;
    u16  tile;              /* offset from the type's first tile */
    bool flip_h, flip_v;
} NpcPiece;

/* Which wandering routine a type runs. */
enum {
    NPC_WANDER_VERTICAL,    /* loc_3C06 */
    NPC_WANDER_HORIZONTAL,  /* loc_3DCA */
    NPC_WANDER_FOUR,        /* object $1B, loc_42F8 */
    NPC_WANDER_DEZOLIAN,    /* Obj_Dezolian, loc_402C */
    NPC_WANDER_MUSKCAT      /* Obj_MuskCat, loc_45A0 */
};

typedef struct {
    int      object;        /* ObjID_ */
    int      tile;          /* where its art sits in VRAM */
    int      line;
    bool     horizontal;
    bool     four;          /* object $1B's four-way wander (loc_42F8) */
    int      wander;        /* NPC_WANDER_* */
    int      first_wait;
    int      pieces[NPC_FRAMES];
    NpcPiece frame[NPC_FRAMES][NPC_TYPE_PIECES];
} NpcType;

/* A chest: closed is frame 0, open frame 1. */
typedef struct {
    int      object;
    int      tile, line;
    int      box;               /* $2C: the loc_7278 row it is hit by */
    int      pieces[2];
    NpcPiece frame[2][NPC_CHEST_PIECES];
    int      alt_level_type;    /* -1: none */
    int      alt_pieces[2];
    NpcPiece alt_frame[2][NPC_CHEST_PIECES];
} NpcChestType;

/* Darum, Teim, the burst and the dead bodies: an object with its own
 * routine and a few multi-piece frames. */
typedef struct {
    int      object;
    int      tile, line;
    int      box;               /* $2C; 0 cannot be bumped into */
    bool     front;             /* bit 5 of 2(a0): ahead of every row */
    int      frames;
    int      pieces[NPC_SCENE_FRAMES];
    NpcPiece frame[NPC_SCENE_FRAMES][NPC_SCENE_PIECES];
} NpcSceneType;

typedef struct {
    bool                free;       /* an empty slot: (a0) = 0           */
    const NpcType      *type;       /* a person, or NULL */
    const NpcChestType *chest;      /* a chest, or NULL  */
    const NpcSceneType *scene;      /* Darum, Teim, the burst, a body, or
                                       NULL */
    s16  hold;              /* $34 */
    int  box;               /* $2C */
    bool alt;               /* a chest drawn with its alt_frame set */
    int  interaction;       /* high byte of $2E */
    int  index;             /* low byte of $2E  */
    s16  x, y, vx, vy;
    s16  screen_x, screen_y;
    bool on_screen;         /* bit 7 of 2(a0) */
    bool frozen;            /* 3(a0): someone is talking to them */
    int  routine;           /* $22: 1 deciding, 2 walking */
    int  facing;            /* $2A */
    int  frame;             /* $24 */
    s16  wait;              /* $28 */
    s16  anim;              /* $26 */
    int  anim_index;        /* $32 */
    s8   range;             /* $34 / $35; for a four-way walker, $35 */
    s8   range_x;           /* a four-way walker's $34 */
    bool hidden;            /* bit 1 of 2(a0): not drawn */
    bool screen_fixed;      /* bit 4: x, y are sprite coordinates */
    bool front;             /* bit 5, set at run time (the explosions) */
    s16  base_x, base_y;    /* $C / $10: an explosion's base */
    bool doomed;            /* bit 0 of 2(a0) set from outside: its
                               routine runs once more, then it is deleted
                               (RunObject's loc_3DC) */
} Npc;

/* The array is object RAM from $FFFFE800: entry n is slot n, objects
 * the port does not run keep theirs as free entries, so the scene's
 * fixed-slot addressing ($E800, $E840, $E880) holds. */
typedef struct {
    Npc npc[NPC_MAX];
    int count;
    int level_type;         /* $FFFFF710, for the chests' mappings  */
    int level_index;
    const u8 *c700;         /* the $C700 block the chests look at   */

    /* What the scene's objects reach outside themselves: $FFFFDE70,
     * which they step on, and sound_queue. Set by the caller after
     * npc_spawn; NULL is allowed for both. */
    u16  *event_step;
    void (*sound)(void *ctx, u8 id);
    void *sound_ctx;

    /* jet_scooter_flag ($FFFFC652), which the scooter's init looks at
     * every frame. NULL reads as clear. */
    const u16 *jet_scooter;

    /* $FFFFE400 empty when the objects' inits run: loc_7E02's cases 1
     * and 2 clear it, and Neifirst's init deletes her on it. Set before
     * npc_setup. */
    bool party_gone;

    /* d6 as the objects find it. Obj_Dezolian's sideways steps call
     * Sprites_ChkCanMove once without loading d6 (loc_4164, loc_41C8),
     * and Level_ChkTargetInteract takes d6 as the loc_7278 row of the
     * mover's box, so what that check tests is whatever the last code to
     * touch d6 left there: BuildSprites (2 + 2 x the objects in the last
     * row it drew), the leader's CharSprites_ChkMove* or the A button's
     * probe (a moveq, then $FFFF after a clean scan or the loop's count
     * at a hit), or an earlier object's check in the same RunObjects.
     * -1 is $FFFF. */
    s16  d6;

    /* loc_4DBE: B1's scene is over -- building_index =
     * BuildingID_EsperMansion, portrait_index = $39, ScreenID_Building.
     * Set every frame from then on; game.c acts on it. */
    bool esper_building;
} NpcSet;

/* The arrival scene before Mother Brain (loc_7E02 case $E): her image
 * at $EC00 and the two energy arcs at $EC40 ($20C,$1B1) and $EC80
 * ($214,$1B1). */
void npc_spawn_mother_brain_image(NpcSet *set);

/* A mover's view of the map: can this cell be stood on? */
typedef bool (*NpcMapTest)(void *ctx, int x, int y);

bool npc_load_tables(void);

/* LoadSpritesInLevel, and the art LoadLevelData decompresses for it.
 * `c700` is GameData.c700: the chests' flags, and the three LoadSpritesInLevel
 * spawns only while a flag is clear. */
void npc_spawn(NpcSet *set, int level_index, int level_type, const u8 *c700,
               TileSet *vram);

/* One frame for everyone. party_x/party_y are the party's positions, the
 * leader first; people do not walk into them. */
void npc_tick(NpcSet *set, Rng *rng, NpcMapTest map_ok, void *map_ctx,
              int width, int height, int cam_x, int cam_y,
              const s16 *party_x, const s16 *party_y, int party_count);

/* LevelScreen's RunObjects before the display comes on: each object's
 * init once, and its place on screen, without anyone taking a step. Call
 * once the camera is set, so the first frame shown has everyone in it. */
void npc_setup(NpcSet *set, int width, int height, int cam_x, int cam_y);

/* Level_ChkTargetInteract for a mover at (x, y) looking one cell the way
 * `dir` says -- 0 up, 1 down, 2 left, 3 right. The person in the way, or
 * NULL. `self` is skipped. */
Npc *npc_in_the_way(NpcSet *set, const Npc *self, int x, int y, int dir);

void npc_unfreeze_all(NpcSet *set);

/* PS2_ATRACE: the probe a mover at (x, y) facing `dir` would test, and
 * every slot with what the hit test makes of it, on stderr. */
void npc_trace(const NpcSet *set, int x, int y, int dir);

/* Slot `slot` ($FFFFE800 + $40 slot) as object `object`, at (x, y) with
 * $24 = frame and everything else zero -- what ObjType_TeimDarum writes
 * to bring Teim in. The slot's init runs on the next npc_tick. */
Npc *npc_put(NpcSet *set, int slot, int object, int x, int y, int frame);

/* Slot `slot`, or NULL when it is empty or out of range. */
Npc *npc_slot(NpcSet *set, int slot);

/* loc_DE52 (ps2.asm:20376): sixteen Obj_RandomExplosion in slots 16-31 at
 * RandomExpl_PosOffsets, each with a timer of GenerateRandomNumber & $3F,
 * plus $40 when `late` (ObjType_Events $29; $2A spawns them without). */
void npc_spawn_explosions(NpcSet *set, Rng *rng, bool late);

/* Slot 0 when it holds the jet scooter, else NULL. */
Npc *npc_jet_scooter(NpcSet *set);

/* TreasureChestContentArray[index - 1]: $8000 | an item id, meseta, or 0
 * for garbage. 0 as well past the end of the table. */
u16 npc_chest_content(int index);

/* ObjType_People (ps2.asm:20500): the script a dialogue number plays.
 * `past_climatrol` is $FFFFC737; `cap` is what loc_E584 finds on a live
 * Rolf's head ($C020) for the Dezolians and the musk cats, dialogue
 * $5D-$7F: NPC_CAP_MAGIC, NPC_CAP_MOGIC, or NPC_CAP_NONE (no cap, or Rolf
 * dead). */
enum { NPC_CAP_NONE, NPC_CAP_MAGIC, NPC_CAP_MOGIC };
u16 npc_dialogue_script(int index, bool past_climatrol, int cap);

/* The leader's move check (CharSprites_ChkMove*, through
 * Sprites_ChkCanMove) as it leaves d6: `dir` from its moveq, then $FFFF
 * after a clean scan of $E800's 32 slots or $1F - slot at a hit.
 * `scanned` is false when the map refused the cell first (loc_6DA4), which
 * leaves the moveq's value. The A button's probe (ProcessAButAction) leaves
 * the same. */
void npc_note_leader_check(NpcSet *set, int dir, bool scanned,
                           const Npc *hit);

/* One person's or chest's sprite. The caller orders people and party by
 * screen row. */
void npc_push_sprite(const Npc *n, Video *v);

/* A scene type from scene_objects.json by object id, or NULL: for
 * objects that live outside the level screen (Tyler's monitor's
 * explosion, object 7, on the building screen). */
const NpcSceneType *npc_scene_type(int object);

#endif /* PS2_NPC_H */
