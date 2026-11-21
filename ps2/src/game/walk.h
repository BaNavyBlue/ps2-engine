#ifndef PS2_WALK_H
#define PS2_WALK_H

#include "../core/level.h"
#include "npc.h"
#include "../platform/platform.h"

/* The lead character on the level screen: Obj_CharactersInLevel
 * (ps2.asm:5157), the collision test it calls (Sprites_ChkCanMove, 10103),
 * the camera it drives (loc_8BBA, 12723) and the scripted input that
 * replaces the pad while demo_flag is set (DemoScriptPtrs, 5398).
 *
 * Movement is on a 16px grid. A direction is only read while standing
 * still; taking a step sets a 15-frame counter and a one-pixel velocity,
 * and RunObject adds the velocity every frame, the first included, so a
 * step is 16 frames and 16 pixels. The pad is not looked at again until
 * it ends. */

#define WALK_BLOCKS_MAX 256

/* Facing is also the base sprite frame: three frames a direction. */
enum { FACE_UP = 0, FACE_DOWN = 3, FACE_LEFT = 6, FACE_RIGHT = 9 };

/* Demo script bytes. Low nibble is the pad; bits 4 and 6 force the
 * camera up or left whatever the character's position on screen. */
#define DEMO_UP     0x01
#define DEMO_DOWN   0x02
#define DEMO_LEFT   0x04
#define DEMO_RIGHT  0x08
#define DEMO_END    0xFF    /* go to ScreenID_Building */
#define DEMO_STOP   0xFE    /* $FFFFDE70 + 1 and the pad back (loc_37EC) */
#define DEMO_DOOR   0xFD    /* SXFID_DoorOpen, routine 2 into the level's
                               first object, and the pad back (loc_37FE) */

#define WALK_FOLLOWERS 3
#define WALK_TRAIL     64       /* $FFFFDD00: 64 (x, y) pairs */
#define WALK_TRAIL_LAG 16       /* a follower reads $40 bytes back */

/* An interaction list has no length in the ROM, only the $FF that ends
 * it. The longest is ControlTowerGroundF's, 70 entries (its row of
 * stairs up); build 131's round-trip found the port's old 64 dropping the
 * last six, whose cells then fell through to the exit table and out of
 * the tower. */
#define WALK_DOORS_MAX 128

#define WALK_EXITS_MAX 128

typedef enum {
    WALK_RUNNING = 0,
    WALK_DEMO_ENDED,        /* $FF: end_x/end_y hold where it stopped     */
    WALK_LEVEL_CHANGE,      /* loc_6FA6: go_level, end_x/end_y = arrival  */
    WALK_BUILDING,          /* loc_6EE6: go_building / go_portrait        */
    WALK_EVENT              /* loc_6EA6: $DE70 = 1, $DE72 = 0, $DE6E =
                               go_event                                   */
} WalkResult;

/* Obj_FollowingChars (ps2.asm:5682). A follower has no movement of its
 * own: each frame it takes the position the leader recorded sixteen
 * entries ago, turns to face the way that moved it, and borrows the
 * leader's walk cycle timing. */
typedef struct {
    s16 x, y;
    s16 screen_x, screen_y;
    int facing;
    int frame;
    s16 anim_timer;
    int anim_index;
} WalkFollower;

/* One entry of a level's interaction list ($FFFFF736), 16px cells. */
typedef struct {
    u8 y, x;
    u8 kind;                /* 0: another level; else a BuildingID_ */
    u8 a, b, c;             /* level, y, x -- or portrait, $F764, $F766 */
} WalkDoor;

/* loc_28546, from level 4 on. */
typedef struct {
    int count;
    u8  level[WALK_EXITS_MAX], y[WALK_EXITS_MAX], x[WALK_EXITS_MAX];
} WalkExits;

typedef struct {
    /* the object */
    s16 x, y;               /* $A / $E: world position, feet centre */
    s16 vx, vy;             /* $14 / $18 */
    s16 screen_x, screen_y; /* $20 / $1E: VDP sprite space */
    int facing;             /* $2A */
    int frame;              /* $24 */
    s16 step_frames;        /* $28 */
    s16 anim_timer;         /* $26 */
    int anim_index;         /* $32 */

    /* the rest of the party */
    WalkFollower follower[WALK_FOLLOWERS];
    int  follower_count;
    s16  trail[WALK_TRAIL][2];
    int  trail_pos;         /* $FFFFF740 / 4 */

    /* the camera: $FFFFF71E / $FFFFF71C. It scrolls plane B (and plane
     * A's collision, the sprites and the windows' open check). */
    s16 cam_x, cam_y;

    /* plane A's camera: $FFFFF71A / $FFFFF718. It takes the same step as
     * the main one, twice over when `parallax` ($FFFFF73C); on a plane-A
     * backdrop it starts at 0,0, wraps in the backdrop's 512 x 256 and
     * stands still while the main camera is held at an edge. Plane A and
     * the windows are drawn from it. */
    s16  plane_a_x, plane_a_y;
    bool parallax;
    bool backdrop_a;

    /* the level */
    const Level *level;
    int  width, height;     /* $FFFFF722 / $FFFFF720, pixels */
    int  level_type;        /* $FFFFF710 */
    bool clamp_camera;      /* $FFFFF712 != 0 */
    LayerId coll_layer;     /* $FFFFF732: plane B when the collision
                               pointer's flag byte has bits 4-6 set */
    u8   attr[WALK_BLOCKS_MAX];
    NpcSet *npcs;           /* the townspeople, who are also in the way */
    WalkDoor door[WALK_DOORS_MAX];
    int  door_count;
    const WalkExits *exits;

    /* $FFFFCB0A: a step has begun since ProcessRandomBattle last
     * looked. Set at loc_38FC, cleared by the roll. */
    bool stepped;

    /* $30(a0): set at loc_38FC, so a damage floor (loc_7108) bites once
     * per step taken onto it, never while standing there. */
    bool hurt_pending;
    /* This frame's loc_7108 fired: the caller flashes, stalls and takes
     * the HP (walk_tick does not know the party). Cleared every tick. */
    bool hurt_now;

    /* The object the last collision test stopped the leader at, or NULL
     * (a1 after Level_ChkTargetInteract). */
    Npc *blocker;

    /* CharSprites_ChkMoveLeft (ps2.asm:5290): a step LEFT stopped by an
     * object whose $2E type is 3 -- Darum, Teim -- ends in
     * `move.w #$40, (joypad_held).w`: nothing held, and A pressed in
     * joypad_pressed. ProcessAButtonPress then talks to it later in the
     * same frame, which is how walking into Darum brings up "I've seen
     * that face!" with no button. Only left does this. walk_tick cannot
     * write the pad, so it sets this and the caller does. Cleared every
     * tick. */
    bool bump_a;

    /* demo playback */
    const u8 *demo;         /* NULL: the pad drives */
    int  demo_pos;          /* demo_input_index */
    bool demo_stopped;      /* this tick read DEMO_STOP: the caller steps
                               $FFFFDE70. Cleared every tick. */
    bool demo_door;         /* this tick read DEMO_DOOR: the caller queues
                               the door SFX and puts routine 2 in object
                               slot 0. Cleared every tick. */
    u8   force;             /* high byte of $FFFFF756: the last byte read */

    /* level_x_pos / level_y_pos as the last result leaves them */
    s16 end_x, end_y;
    int go_level;
    u8  go_sfx;             /* SXFID_LevelChanged or SXFID_FellInHole */
    int go_building, go_portrait;
    int go_store_max, go_store_index;   /* $FFFFF764 / $FFFFF766 */
    int go_event;           /* $DE6E for WALK_EVENT */
    u8  go_scene;           /* the one-shot scene the reload runs (event_
                               flags' high byte), 0 for none. Only the
                               approach to Mother Brain sets it. */

    /* What loc_6EA6 reads for the two kind-1 doors that are not
     * buildings. game.c copies them in before every walk_tick. */
    bool piata_tower_unlocked;  /* FLAG_PIATA_TOWER_UNLOCKED ($C712): set at the Governor's debrief */
    u16  rolf_hp;           /* character_data_buffer+2                  */
    bool mother_brain_fought;   /* $C747: the approach stays quiet */

    /* loc_6F1C: stepping from an outdoor level into a dungeon (index
     * $10 and up) records where you were, in $C64C-$C650. */
    bool set_return;
    int  return_level;
    s16  return_x, return_y;

    /* The jet scooter. `riding` is jet_scooter_flag on level 0, which is
     * what Obj_CharactersInLevel and Obj_FollowingChars test: game.c sets
     * it before every walk_tick. While it is set the party objects hide
     * (bit 1 of 2(a0)) and do nothing else, and walk_tick runs
     * ObjJetSctr_Main on the Npc in slot 0 instead -- `x`, `y` and
     * `facing` above are then $E40A/$E40E/$E42A as the scooter copies
     * them (loc_4AE2), before RunObject has added this frame's velocity
     * to it. attr_scooter is loc_29E16, the table it probes. */
    bool riding;
    u8   attr_scooter[WALK_BLOCKS_MAX];
    bool have_scooter_table;

    /* The party's $22 = 0 from getting off the scooter (loc_DA4C): on the
     * next walk_tick ObjCL_Init runs again -- 8 in from the cell corner,
     * $2A = $24, the trail refilled -- and the followers take their place
     * from it. */
    bool reinit;
} Walk;

/* "<dir>/<stem>.collision.txt"; see tools/extract_collision.py. */
bool walk_load_collision(Walk *w, const char *path);

/* "assets/levels/jet_scooter.collision.txt", loc_29E16. */
bool walk_load_scooter_collision(Walk *w, const char *path);

/* "<dir>/<stem>.doors.json" and "<dir>/exits.json"; see
 * tools/extract_interactions.py. */
bool walk_load_doors(Walk *w, const char *path);
bool walk_load_exits(WalkExits *ex, const char *path);

/* LevelScreen's setup for the lead character, and LoadLevelData's for
 * the camera. (x, y) are level_x_pos / level_y_pos. */
void walk_begin(Walk *w, const Level *level, int level_type,
                int x, int y, int facing, const u8 *demo, int followers);

/* One frame. `blocked` is the object's own early-out: event_flags, an
 * open window, or a window on the stack. */
WalkResult walk_tick(Walk *w, const Input *in, bool blocked);

/* Frame to tile offset within one character's 72 tiles (loc_118E8), and
 * whether the frame is drawn mirrored (loc_12B5E). */
int  walk_frame_tiles(int frame);
bool walk_frame_flipped(int frame);

/* The map half of Sprites_ChkCanMove, for movers that cannot use doors:
 * is the quadrant under (x, y) free of the solid bit? An NpcMapTest, with
 * the Walk as its context. */
bool walk_cell_open(void *walk, int x, int y);

/* The party's world positions, leader first; returns how many. */
int walk_party_positions(const Walk *w, s16 *xs, s16 *ys, int cap);

/* loc_D9C6's test with a0 = the scooter: the walking table's solid bit
 * (loc_6DA4, which for an object from $E800 on never reaches the list)
 * at the scooter plus loc_DA64's offset for the way it faces. The offset
 * is added to the leader's copy of the position either way, as the ROM
 * does to $E40E/$E40A. True: the party can get off there. */
bool walk_scooter_can_dismount(Walk *w);

/* loc_DA26: the scooter parked at its own cell, returned in *park_x and
 * *park_y ($C656 / $C654, & $FFF0), and the party's $22 = 0 with $2A =
 * the leader's: see `reinit`. The caller clears jet_scooter_flag and puts
 * the scooter back to its init ($E822 = 0). */
void walk_scooter_dismount(Walk *w, s16 *park_x, s16 *park_y);

/* $28 of whatever the pad drives: the leader's step counter, or the
 * scooter's while riding. 0 at rest. */
int  walk_step_frames(Walk *w);

/* Start a demo walk in the middle of a level, as ObjType_TeimDarum does:
 * demo_flag = 1, demo_input_index = 0. */
void walk_start_demo(Walk *w, const u8 *demo);
void walk_resume_demo(Walk *w, const u8 *demo);

extern const u8 DEMO_HOUSE_TO_TOWER[];
extern const u8 DEMO_TEIM_SCENE[];
extern const u8 DEMO_AFTER_MOTHER_BRAIN[];
extern const u8 DEMO_TO_THE_EARTHMEN[];
extern const u8 DEMO_AFTER_TEIM_DEATH[];
extern const u8 DEMO_TOWER_TO_HOUSE[];
extern const u8 DEMO_CLONE_LAB_TO_TOWER[];
extern const u8 DEMO_TYLER_SPC_TO_TOWER[];
extern const u8 DEMO_CLIMATROL_TO_CLONE_LAB[];
extern const u8 DEMO_DYNAMITE_USED[];
extern const u8 DEMO_KEY_USED[];

#endif /* PS2_WALK_H */
