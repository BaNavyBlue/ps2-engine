#ifndef PS2_BATTLE_H
#define PS2_BATTLE_H

#define BATTLE_FX_NEI_SWORD     1   /* the Neisword's light ($CC98 = 1)  */
#define BATTLE_FX_MOTHER_BRAIN  2   /* Mother Brain's lights ($CC98 = 2) */

#include "../core/level.h"
#include "../core/palette.h"
#include "../core/rng.h"
#include "../core/tileset.h"
#include "../core/video.h"
#include "gamedata.h"
#include "walk.h"
#include "window.h"

/* Random encounters and the battle screen.
 *
 * ProcessRandomBattle (ps2.asm:26535) runs every tick of the level
 * screen and rolls once per completed step. BattleScreen (12047) builds
 * the screen: the flash to white, loc_796 loading the formation and the
 * enemies' stats, the party's battle sprites, the stat boxes, the fade
 * back from white, the 64-frame dissolve of the enemy art, the enemy
 * name boxes and the music; then BattleScreenLoop (12223) runs
 * RunObjects, BattleScreen_CheckRoutine (21714) and the windows.
 *
 * The fight itself is the ROM's object system, ported as it is: 64
 * slots of 64 bytes from $FFFFE000, the party at $E400 (128 bytes a
 * character, by character id), the enemies at $E800 (128 bytes a slot)
 * and the effects at $EC00 (16 slots). A character's or an enemy's
 * second 64 bytes is a child object -- the thrown weapon, the spell in
 * flight, an enemy's overlay -- so the ROM's `-$36(a0)` reads of a
 * parent from its child are `obj[i - 1]` here.
 *
 *   $A  Obj_CharactersBattle (2548)    $F   Obj_EnemiesBattle (3879)
 *   $B/$C/$D  the weapon and technique effects (loc_289E, 28F2, 292A)
 *   $10/$11   an enemy's own overlays (loc_3166, 31BC)
 *   $12/$13   effects spawned on a target (loc_321E, 3250)
 *
 * Every one of them animates through Battle_AnimateSprites (4727) over
 * a script of frame numbers and marks; the marks are where the hits
 * land, the sounds play and the windows redraw, so the timing of a
 * round is the scripts', not the port's. */

#define BATTLE_ENEMIES_MAX  5       /* EnemyXPosArray has five columns */
#define BATTLE_NAME_LEN     10
#define BOBJ_COUNT          64
#define BOBJ_CHARS          16      /* $E400: slot 16 + 2c, child at +1 */
#define BOBJ_ENEMIES        32      /* $E800: slot 32 + 2n, child at +1 */
#define BOBJ_EFFECTS        48      /* $EC00: sixteen 64-byte slots    */

typedef struct {
    u16  counter;       /* enemy_data_buffer+$C: zeroed on LevelScreen */
    u16  hidapipe;      /* $FFFFCB0E                                   */
    int  formation;     /* what the last roll produced, 1-based        */
} Encounter;

typedef struct {
    s16 y;
    u8  size;
    u16 tile;
    s16 x;
} Piece;

/* One map frame: a nametable block (loc_572) or sprite pieces (loc_526).
 * (build 131c) PIECES_MAX was 16 and seventeen frames have more -- up
 * to 36 (Dark Force's frame 28), 28 in Gra's strike -- whose pieces past
 * the sixteenth were dropped. BuildSprites takes a frame's whole count
 * (the 80-sprite cap is the screen's, video.c's); battle.c says on stderr
 * when a frame has more than this keeps. */
#define PIECES_MAX 40
typedef struct {
    bool  block;
    s16   x, y;
    u8    w, h;
    u16  *cell;                 /* w * h entries */
    int   piece_count;
    Piece piece[PIECES_MAX];
} MapFrame;

typedef struct {
    char     label[40];
    int      frame_count;
    MapFrame *frame;
} MapSet;

/* One 64-byte object, the fields the battle routines touch, by their
 * offsets. */
typedef struct {
    u16 id;             /* (a0): 0 is free                            */
    s16 state;          /* $22: the routine index                     */
    u8  r;              /* 2(a0): bit 0 destroy, 1 hidden, 4 screen
                           space, 5 front of the sprite list, 7 listed */
    u8  f;              /* 3(a0): 0 acting, 1 alt, 3 pending message,
                           4 missed, 5 killed, 6 hit blink, 7 shown  */
    const MapSet *map;  /* 4(a0)                                      */
    bool pal;           /* bit 7 of 4(a0)'s high byte: palette line 1 */
    u16 tile;           /* 8(a0)                                      */
    s16 x, y;           /* $A / $E                                    */
    s16 hx, hy;         /* $C / $10: home                             */
    s16 d12;            /* $12: an enemy's child x offset             */
    s16 anim;           /* $16: script index                          */
    s16 pos;            /* $1A: script position                       */
    s16 d1C;            /* $1C: another x offset                      */
    s16 frame;          /* $24                                        */
    s16 timer;          /* $26                                        */
    s16 dur;            /* $28: frames per step, minus one            */
    s16 d2A;
    s16 target;         /* $2C: a character id, or an enemy slot + 8  */
    s16 blink;          /* $30                                        */
    s16 d32;            /* $32: extra hits owed to an enemy           */
    s16 who;            /* $36: character id / enemy group start      */
    s16 d38;            /* $38                                        */
    s16 d3A;            /* $3A: a child's second script index         */
    const MapSet *map2; /* $3C: a child's second map                  */
    u16 sfx_attack;     /* $3C: an enemy's attack sound               */
    u16 sfx_tech;       /* $3E: an enemy's technique sound            */
    s16 d40;            /* $40 (loc_35DE's copy of the mark)          */
    u8  warned;         /* port: the missing-script line said once    */
} BObj;

/* enemy_stat_buffer + $40 n */
typedef struct {
    u16 word0;          /* +0: status flags in the high byte, the type
                           byte in the low, whose low three bits count
                           paralysis turns                            */
    u16 hp, max_hp;     /* +2 +4                                      */
    u16 meseta, exp;    /* +$A +$C                                    */
    u16 agility;        /* +$14                                       */
    u16 attack, defense;/* +$1C +$1E                                  */
    u8  technique;      /* +$25                                       */
    u8  tech_rate;      /* +$26                                       */
    u8  success_index;  /* +$27                                       */
} EnemyStats;

typedef enum { BP_FADE_IN, BP_DISSOLVE, BP_LOOP } BattlePhase;

typedef enum {
    BATTLE_RUNNING = 0,
    BATTLE_TO_LEVEL,            /* ScreenID_Level: back to the map     */
    BATTLE_GAME_OVER            /* ScreenID_Sega through GameOverScreen */
} BattleResult;

typedef struct {
    /* enemy_data_buffer ($FFFFCB00) */
    int  formation;             /* +0, 1-based                         */
    int  group2_start;          /* +2: the first slot of group 2       */
    bool ambushed;              /* $FFFFCC0A: the enemies act first    */
    int  group_id[2];           /* +$12 / +$16                         */
    int  group_count[2];        /* +$10 / +$14: count - 1, -1 if none  */
    int  escape_rate;           /* +$18                                */
    u16  info[2];               /* +$20 / +$22: what the info boxes show */
    u32  exp_total, meseta_total;   /* +$30 / +$34                     */
    int  layout;

    u8   name[2][BATTLE_NAME_LEN];
    EnemyStats stat[8];         /* enemy_stat_buffer                   */
    BObj obj[BOBJ_COUNT];       /* object_ram                          */

    /* char_battle_command_index ($FFFFCC10): 8 words a character --
     * command, parameter (technique or item), target group. Kept here
     * because it outlives the screen: loc_F4E8 rereads it at every
     * battle start. */
    u16  cmd[GD_CHARS][8];

    /* entry sequence */
    BattlePhase phase;
    int  fade_frames;
    int  dissolve_step;

    /* BattleScreen_CheckRoutine's state */
    int  main_routine;          /* battle_main_routine_index: 0 standby,
                                   1 fighting, 2 run                   */
    bool fight_active;          /* fight_active_flag                   */
    bool fight_interrupted;     /* fight_interrupted_flag              */
    u16  cc06;                  /* $FFFFCC06: actions still animating  */
    u16  cc90;                  /* $FFFFCC90: where in the turn list   */
    u16  cc92;                  /* $FFFFCC92: Neifirst's turn count    */
    u16  cc94;                  /* $FFFFCC94: frames before victory    */
    /* The battle's palette effects (loc_67B8, every frame of the loop):
     * 0 none, BATTLE_FX_NEI_SWORD the Neisword's light, BATTLE_FX_MOTHER_
     * BRAIN her lights. $FFFFCC98. */
    u16  palette_effect;
    /* The two counters the Neisword's light steps with -- the level's
     * palette cycle's own ($FFFFF632 step, $F634 wait), which no screen
     * resets: game.c hands them in at the battle's start and takes them
     * back after. */
    u16  cycle_step;
    s16  cycle_wait;
    /* Mother Brain's lights ($FFFFF650-$F65A, zeroed as the battle
     * starts): the chase's step, the glow's and the rainbow's, whether the
     * chase has made its first lap (the other two wait for it), and
     * whether the glow and the rainbow have finished fading in. */
    u16  chase_step;            /* $F650 */
    u16  glow_step;             /* $F652 */
    u16  rainbow_step;          /* $F654 */
    bool chase_lapped;          /* $F656 */
    bool glow_cycling;          /* $F658 */
    bool rainbow_cycling;       /* $F65A */
    u16  battle_script;         /* battle_script_id ($FFFFCC0C)        */
    s16  command_used;          /* battle_command_used ($FFFFCC0E)     */
    u16  turn[16][2];           /* $FFFFCCA0: (id, agility)            */
    u16  levelup_char;          /* $FFFFC602 while handing out levels  */
    u8   tech_type;             /* $FFFFF73A: the technique's type bits */
    u8   effect_sound;          /* $FFFFF642                           */
    u8   tech_tp;               /* tech_tp_consumption                 */

    CharData saved[GD_CHARS];   /* battle_character_data_buffer        */

    u8   music;                 /* what battle_enter chose: $92 or $93 */

    /* art: the RAM image FadeArtTiles reveals, tiles 0-$1FF */
    TileSet enemy_art;
    bool    art_ready;
    /* (build 155) The reveal mask of those tiles (VideoPicture's
     * `hidden`): all hidden as battle_enter blanks them, a place cleared a
     * dissolve step. game.c's game over erase sets its bytes back. */
    u64     art_hidden;

    /* what the fighting routine asks the screen for */
    bool to_level_change;       /* loc_2E24: the Army Eyes carry the
                                   party to Gaira, event_flags = $A00 */
} Battle;

/* assets/data/battle.json and the party's battle art. Once. */
bool battle_tables_load(void);

/* ProcessRandomBattle. Returns true when a battle starts: enc->formation
 * holds the formation and the counter is reset. `blocked` is the
 * routine's own early-out: a window open, a talk running, or a demo. */
bool battle_encounter_check(Encounter *enc, Rng *rng, const GameData *gd,
                            const Level *lv, Walk *w, bool blocked);

/* The part of BattleScreen that runs with the display off, once the
 * flash to white is over: everything up to loc_86A2's window queue.
 * `loaded` is the palette the display then fades to from white ($FB80);
 * (build 141) loads into it go through `video`, the colour path's too.
 * The window queue is left with the four stat boxes and the option box
 * for the caller to drain. */
void battle_enter(Battle *b, int formation, GameData *gd, WinSys *ws,
                  Rng *rng, TileSet *vram, Video *video, Palette *loaded);

/* One frame of the entry sequence and, later, BattleScreenLoop. Runs
 * the objects, the dissolve and the event routine. The caller runs the
 * window system around it exactly as the level does. */
BattleResult battle_tick(Battle *b, GameData *gd, WinSys *ws, Rng *rng,
                         TileSet *vram, Video *video, const Palette *loaded,
                         const Input *in,
                         void (*sound)(void *ctx, u8 id), void *ctx);

/* Plane A rows 0-16 rebuilt from the objects (VBlankSub_18's DMA of
 * $FF6000), the floor in plane B, the sprites in BuildSprites' order. */
void battle_render(const Battle *b, Video *v, TileSet *vram);

/* FadeEffectArray's 64 pixel positions, for the dissolve. */
void battle_set_fade_order(const u8 *order);

/* Is the entry sequence over -- can the pad reach the windows? */
bool battle_in_loop(const Battle *b);

/* loc_F4E8: every character's stored command checked against what
 * they hold, at battle start. Also what loc_F550 seeds a new party
 * with (CharStartCommandsArray): attack, or defend for the unarmed. */
void battle_reset_commands(Battle *b, const GameData *gd,
                           bool from_start_table);

/* ---- the object engine (battle_obj.c) ---- */

/* RunObjects over the battle slots, in slot order. */
void bobj_run(Battle *b, GameData *gd, WinSys *ws, Rng *rng, TileSet *vram,
              Video *video, void (*sound)(void *ctx, u8 id), void *ctx);

/* For the probes: the commands characters have begun in fights since
 * the program started, by kind -- attack, technique, item, defend --
 * one for each call of Battle_CommandUsedIndex (loc_1D34). An attack
 * with a weapon in each hand is two; a command that falls back through
 * loc_F4F8 counts again as what it became. */
void battle_debug_commands(u32 out[4]);

/* For the probes: enemies that have joined a fight since the program
 * started -- ObjEnmBattle_Dead's way in, which only an enemy's copy of
 * itself (loc_2F6C) takes. */
u32 battle_debug_enemies_joined(void);

/* Character_ProcessLevelUp's middle for character c: level = row->level
 * and every gain rolled through GenerateRandStatIncrease, in the ROM's
 * order, with equipment bonuses already off. The flags say which of
 * $1215, $1216 and $1217 the battle shows. Shared with the harness's
 * `party` command, which levels characters the way fights do. */
void battle_apply_level_row(GameData *gd, int c, const LevelRow *row,
                            Rng *rng, bool *hp_up, bool *tp_up,
                            bool *techs_up);

/* EnemyNames[id]: ten CHARSET codes, $C4-terminated when shorter. */
const u8 *battle_enemy_name(int id);
/* (build 163) Enemy id's label in battle.json ("Enemy_FireAnt"), what a
 * language's names.json names it by; NULL for no enemy. */
const char *battle_enemy_label(int id);

/* The tables battle_obj.c needs from battle.json. */
const MapSet *battle_map_set(const char *label);
const MapSet *battle_char_map(int c);
int           battle_char_palette_line(int c);
void          battle_effect_art_load(TileSet *vram, int index);

#endif /* PS2_BATTLE_H */
