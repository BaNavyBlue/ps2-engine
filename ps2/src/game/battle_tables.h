#ifndef PS2_BATTLE_TABLES_H
#define PS2_BATTLE_TABLES_H

#include "../core/ps2_types.h"

/* The parts of assets/data/battle.json the fight runs on, loaded by
 * battle_tables_load() in battle.c and read by battle_obj.c. */

typedef struct {
    int len;
    u8  byte[256];
} AnimScript;

typedef struct {
    int         count;
    AnimScript *script;     /* one per table entry, shared scripts copied */
} AnimTable;

enum {
    ANIM_IDLE,              /* loc_2601A: the enemies' standby loops     */
    ANIM_ENEMY_ATTACK,      /* loc_26118                                 */
    ANIM_ENEMY_TECH,        /* loc_26216                                 */
    ANIM_ENEMY_CHILD_A,     /* loc_26314: objects $10 and $13            */
    ANIM_ENEMY_CHILD_B,     /* loc_26414: objects $11 and $12            */
    ANIM_CHARS,             /* CharBattle_AnimOffsets                    */
    ANIM_EFFECT,            /* loc_27364: objects $B and $C              */
    ANIM_EFFECT2,           /* loc_27226: object $D, the cast            */
    ANIM_TABLE_COUNT
};

const AnimTable  *anim_table(int which);
const AnimScript *anim_script(const AnimTable *t, int index);

/* WeaponProperties (45563): 12 bytes from the Knife. */
typedef struct {
    u8   kind;          /* 0 a hand weapon, else a gun's damage rate  */
    u8   spread;        /* 0 one, 1 the group (slashers), n shots     */
    u8   sound;
    int  art, art2;     /* WeaponTechArtPtrs indices                  */
    bool pal, pal2;     /* palette line 1 for the effect              */
    char map[32], map2[32];
} WeaponProp;

/* TechniqueProperties (45762): 10 bytes from Foi. */
typedef struct {
    u8   rate;          /* the success rate rolled against            */
    u8   sound;
    int  art, art2;
    bool pal, pal2;
    char map[32], map2[32];
} TechProp;

const WeaponProp *weapon_prop(int index);       /* item id - Knife */
const TechProp   *tech_prop(int id);            /* technique id, 1-based */

extern u8 g_success_rate[32];       /* EnemyTechSuccessRate (4165)   */
extern u8 g_target_chances[8];      /* Battle_CharTargetChances       */
extern u8 g_hit_anim[28];           /* loc_34EE                       */

#endif /* PS2_BATTLE_TABLES_H */
