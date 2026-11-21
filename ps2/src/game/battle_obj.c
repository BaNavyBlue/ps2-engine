#include "battle.h"
#include "battle_tables.h"

#include <stdio.h>
#include <string.h>

/* The battle objects (ps2.asm:2548-5160): Obj_CharactersBattle,
 * Obj_EnemiesBattle, the effect objects $B-$D and $10-$13, and
 * Battle_AnimateSprites with its marks. Everything is written against
 * the 64-byte object layout, so a field is named for its offset in the
 * comment beside it and a routine for the label it was.
 *
 * What a routine sees: a0 is the object, a3 the acting character's or
 * enemy's stats, a1/a2 the target's stats and object, a5 the command
 * record, a6 the weapon or technique properties. The port passes the
 * same things as arguments, with the Battle for the RAM around them. */

#define SFX_HURT        0xB3    /* SXFID_Sword: "the generic sound when
                                   characters/enemies get hurt"      */
#define SFX_ENEMY_KILLED 0xD4
#define SFX_RED_SCREEN  0xBA

/* Object ids (ObjectTable). */
enum { OBJ_CHAR = 0xA, OBJ_FX_B = 0xB, OBJ_FX_C = 0xC, OBJ_FX_D = 0xD,
       OBJ_ENEMY = 0xF, OBJ_ENM_CHILD_A = 0x10, OBJ_ENM_CHILD_B = 0x11,
       OBJ_FX_12 = 0x12, OBJ_FX_13 = 0x13 };

/* 2(a0) */
#define R_DESTROY   0x01
#define R_HIDDEN    0x02
#define R_SCREEN    0x10
#define R_FRONT     0x20
#define R_LISTED    0x80
/* 3(a0) */
#define F_ACTING    0x01
#define F_ALT       0x02
#define F_QUIET     0x08    /* dies without "is dead!" (Sak)         */
#define F_MISS      0x10
#define F_KILLED    0x20
#define F_HIT       0x40
#define F_SHOWN     0x80

/* CharData +0, the status word. The ROM tests the high byte with byte
 * operations: `_btst #n, 0(a1)` is bit n + 8 of the word. */
#define ST_DEFEND   0x0100  /* bit 8:  defending this round           */
#define ST_DEBAN    0x0200  /* bit 9:  Deban                          */
#define ST_SEALED   0x0400  /* bit 10: techniques fail                */
#define ST_HALF_HIT 0x0800  /* bit 11: hit rate halved                */
#define ST_HALF_DMG 0x1000  /* bit 12: (enemies) damage lost half the time */
#define ST_HALF_ATK 0x1000  /* bit 12: (characters) weapon hit rate halved:
                             * CalculateHitRate's _btst #4, 0(a3) -- byte 0,
                             * the status word's high byte -- where loc_27C8's
                             * technique rolls test #3 (bit 11) */
#define ST_SLEEP    0x4000  /* bit 14                                 */
#define ST_POISON   0x8000  /* bit 15                                 */

typedef struct {
    Battle   *b;
    GameData *gd;
    WinSys   *ws;
    Rng      *rng;
    TileSet  *vram;
    Video    *video;
    void    (*sound)(void *ctx, u8 id);
    void     *ctx;
} Run;

static void play(Run *r, u8 id)
{
    /* PlayMusic with $80 borrows out at `subi.b #$81` and does nothing;
     * the enemies' "no sound" is $80. */
    if (id != 0x80 && r->sound != NULL) {
        r->sound(r->ctx, id);
    }
}

static u16 rnd(Run *r)
{
    return (u16)rng_next(r->rng);
}

static CharData *chr(Run *r, int c)
{
    return &r->gd->chr[c & 7];
}

static inline BObj *obj_of_char(Battle *b, int c)
{
    return &b->obj[BOBJ_CHARS + 2 * (c & 7)];
}

/* For the probes: enemies ObjEnmBattle_Dead has brought in -- a copy
 * loc_2F6C made that the hit landed on. */
static u32 g_debug_joined;

static inline BObj *obj_of_enemy(Battle *b, int n)
{
    return &b->obj[BOBJ_ENEMIES + 2 * (n & 7)];
}

/* $E400 + $2C(a0) << 7: characters are 0-7, enemies 8 up. */
static inline BObj *obj_of_target(Battle *b, int t)
{
    int i = BOBJ_CHARS + 2 * (t & 0x1F);
    return (i < BOBJ_COUNT) ? &b->obj[i] : &b->obj[BOBJ_COUNT - 1];
}

static inline int obj_index(const Battle *b, const BObj *o)
{
    return (int)(o - b->obj);
}

/* The parent of a child at an odd slot: -$40(a0). */
static inline BObj *parent_of(Battle *b, BObj *o)
{
    return &b->obj[obj_index(b, o) - 1];
}

/* ------------------------------------------------------------------ */
/* Sound and art for an action                                        */
/* ------------------------------------------------------------------ */

/* loc_608A: the effect art queued for the next VBlank; here placed at
 * once. */
static void load_effect_art(Run *r, int index)
{
    battle_effect_art_load(r->vram, index);
}

/* ------------------------------------------------------------------ */
/* Battle_AnimateSprites (4727)                                       */
/* ------------------------------------------------------------------ */

static void spawn_on_target(Run *r, BObj *o, BObj *target, BObj **spawned);
static void hit_target(Run *r, BObj *o);

static BObj *free_effect(Battle *b)
{
    for (int i = BOBJ_EFFECTS; i < BOBJ_COUNT; i++) {
        if (b->obj[i].id == 0) {
            return &b->obj[i];
        }
    }
    return NULL;
}

/* loc_32AC: a script byte is a frame plus one; zero hides the object. */
static void show_frame(BObj *o, int byte)
{
    o->r |= R_HIDDEN;
    if (byte - 1 < 0) {
        return;
    }
    o->r &= (u8)~R_HIDDEN;
    o->frame = (s16)(byte - 1);
}

static void animate(Run *r, BObj *o, const AnimTable *t)
{
    Battle *b = r->b;

    if (--o->timer >= 0) {
        return;
    }
    o->timer = o->dur;

    const AnimScript *s = anim_script(t, o->anim);
    if (s == NULL || s->len == 0) {
        /* Past the table or an empty script: the object never reaches
         * $FF or $FE, so if it was counted in $CC06 the fight waits on
         * it for good. Build 87's loc_27364 was one entry short (the
         * extractor dropped `(loc_274C6+6)`) and $47 did exactly that. */
        if (!o->warned) {
            o->warned = 1;
            fprintf(stderr, "battle: object %X has no animation script %d\n",
                    o->id, o->anim);
        }
        return;
    }
    int start = s->byte[0];
    int pos   = o->pos;
    int byte  = (pos < s->len) ? s->byte[pos] : 0xFF;
    o->pos = (s16)(pos + 1);

    if (byte < 0x80) {
        show_frame(o, byte);
        return;
    }
    if (byte == 0xFF) {
        /* loc_32C2: the script is over. */
        o->pos = 0;
        if (o->f & F_ACTING) {
            b->cc06--;
            o->f &= (u8)~F_ACTING;
            return;
        }
        if (start >= 0x80) {
            return;
        }
        o->pos = 1;
        show_frame(o, start);
        return;
    }
    if (byte == 0xFE) {
        /* loc_32EE: the object destroys itself. */
        o->r = R_DESTROY;
        b->cc06--;
        return;
    }
    if (byte == 0xFD) {
        hit_target(r, o);
        return;
    }
    if (byte == 0xFC) {
        /* loc_338E: the target, if it is of this object's group, gets a
         * one-frame-step effect; then the next target. */
        BObj *a2 = obj_of_target(b, o->target);
        if (a2->id != 0 && a2->who == o->who) {
            BObj *a3 = NULL;
            spawn_on_target(r, o, a2, &a3);
            if (a3 != NULL) {
                a3->dur = 1;
            }
        }
        o->target++;
        return;
    }
    if (byte == 0xFB) {
        /* loc_33BE: an effect on every shown object from the target, or
         * from the party when the target is below 8. */
        int t = o->target;
        if (t < 8) {
            t = 0;
            o->target = 0;
        }
        int count = (r->ws->technique_index == TECH_MEGID) ? 16 : 8;
        for (int k = 0; k < count; k++) {
            BObj *a2 = obj_of_target(b, o->target);
            if (a2->id != 0 && (a2->f & F_SHOWN)) {
                BObj *a3 = NULL;
                spawn_on_target(r, o, a2, &a3);
                int kind = o->d3A & 0x7F;
                if (kind == 0x2B || kind == 0x2C) {
                    /* Gra's one column of fire, for the whole group. */
                    if (a3 != NULL) {
                        a3->x      = 0x120;
                        a3->target = 8;
                    }
                    return;
                }
            }
            o->target++;
        }
        return;
    }
    if (byte == 0xFA) {
        /* loc_3428: the next enemy from the target still owed a hit. */
        for (int k = 0; k < 5; k++) {
            BObj *a2 = obj_of_target(b, o->target);
            if (a2->id != 0 && a2->d32 != 0) {
                a2->d32--;
                BObj *a3 = NULL;
                spawn_on_target(r, o, a2, &a3);
                return;
            }
            o->target++;
        }
        o->target = 8;
        return;
    }
    if (byte == 0xF9) {
        /* loc_3462: an effect on every shown object, its animation picked
         * from loc_34EE by where the target stands. */
        int t = o->target;
        if (t < 8) {
            o->target = 0;
        }
        for (int k = 0; k < 8; k++) {
            BObj *a2 = obj_of_target(b, o->target);
            if (a2->id != 0 && (a2->f & F_SHOWN)) {
                BObj *a3 = NULL;
                spawn_on_target(r, o, a2, &a3);
                int col = 0;
                int x   = a2->x;
                if (x >= 0xB0) {
                    col = 1;
                    if (x != 0xE0) {
                        col = 2;
                        if (x != 0xF8) {
                            col = 3;
                            if (x != 0x120) {
                                col = 4;
                                if (x != 0x148) {
                                    col = 5;
                                    if (x != 0x160) {
                                        col = 6;
                                    }
                                }
                            }
                        }
                    }
                }
                int idx = (o->d3A & 0x7F) * 7 + col;
                if (a3 != NULL && idx < 28) {
                    a3->anim = g_hit_anim[idx];
                }
            }
            o->target++;
        }
        return;
    }
    if (byte == 0xF8) {
        /* loc_350A: everyone from the target gets hit. */
        for (int k = 0; k < 8; k++) {
            hit_target(r, o);
            o->target++;
        }
        return;
    }
    if (byte == 0xF7) {
        /* loc_3520: an overlay ($12) at the parent's position, its
         * script the next byte. */
        BObj *a3 = free_effect(b);
        if (a3 == NULL) {
            return;
        }
        BObj *p = parent_of(b, o);
        o->pos++;
        memset(a3, 0, sizeof *a3);
        a3->anim  = (pos + 1 < s->len) ? s->byte[pos + 1] : 0;
        a3->id    = OBJ_FX_12;
        a3->tile  = o->tile;
        a3->map   = o->map;
        a3->x     = (s16)(p->x + p->d1C);
        a3->y     = p->d12;
        a3->timer = 0;
        a3->dur   = 1;
        b->cc06++;
        return;
    }
    if (byte == 0xF6) {
        /* loc_357C: a $13 somewhere random over the enemies. */
        BObj *a3 = free_effect(b);
        if (a3 == NULL) {
            return;
        }
        memset(a3, 0, sizeof *a3);
        a3->id    = OBJ_FX_13;
        a3->tile  = o->tile;
        a3->map   = o->map;
        a3->anim  = 0x7F;
        a3->x     = (s16)((rnd(r) & 0xF8) + 0xA0);
        a3->y     = (s16)((rnd(r) & 0x38) + 0xB0);
        a3->timer = 0;
        a3->dur   = 7;
        b->cc06++;
        return;
    }
    /* loc_35DE: any other mark launches the child object with the id
     * the byte negates -- $F5 is $B, $F4 is $C, $F3 is $D, $F0 and $EF
     * the enemies' $10 and $11. */
    BObj *c = &b->obj[obj_index(b, o) + 1];
    c->id     = (u16)((-byte) & 0xFF);
    c->d40    = c->id;
    c->x      = (s16)(o->x + o->d1C);
    c->y      = 0xC8;
    c->tile   = c->pal ? 0x2400 : 0x0400;
    c->target = o->target;
    c->dur    = 1;
    c->state  = 0;
    b->cc06++;
    play(r, b->effect_sound);
}

/* loc_3308: the object at $2C takes the hit it was dealt -- the sound,
 * the blink, and the windows the results show in. */
static void hit_target(Run *r, BObj *o)
{
    Battle *b  = r->b;
    WinSys *ws = r->ws;
    BObj   *a2 = obj_of_target(b, o->target);

    if (a2->id == 0 || !(a2->f & F_SHOWN)) {
        return;
    }
    if (a2->f & F_HIT) {
        b->battle_script = 0;
        return;
    }
    a2->f |= F_HIT;
    play(r, SFX_HURT);
    a2->blink = 0x30;
    b->cc06++;

    if (b->command_used >= 0) {
        if (b->command_used == 1) {
            win_open(ws, WIN_BATTLE_TECH_USED);
        } else if (b->command_used == 2) {
            win_open(ws, WIN_BATTLE_ITEM_USED);
        }
        for (int i = 0; i < 4; i++) {
            win_open(ws, (u16)(0x0600 | (WIN_BATTLE_CHAR_STATS_1 + i)));
        }
        if (b->formation < 0x102) {
            win_open(ws, 0x0600 | WIN_ENEMY_INFO_1);
            win_open(ws, 0x0600 | WIN_ENEMY_INFO_2);
        }
        b->command_used = -1;
        if (b->battle_script != 0) {
            win_open(ws, WIN_BATTLE_MESSAGE);
            win_script_set(ws, b->battle_script);
        }
    }
    b->battle_script = 0;
}

/* loc_3622: an effect over the target object -- a $B with the child's
 * second map and script when a character (or its child) is acting, a
 * $12 with the enemy's own map when an enemy is. */
static void spawn_on_target(Run *r, BObj *o, BObj *target, BObj **spawned)
{
    Battle *b  = r->b;
    BObj   *a3 = free_effect(b);
    *spawned = NULL;
    if (a3 == NULL) {
        return;
    }
    memset(a3, 0, sizeof *a3);
    a3->id     = OBJ_FX_B;
    a3->x      = (s16)(target->x - 0x18);
    a3->y      = 0xC8;
    a3->target = o->target;
    if (o->id < OBJ_ENEMY) {
        a3->tile = (o->d3A & 0x80) ? 0x2400 : 0x0400;
        a3->anim = (s16)(o->d3A & 0x7F);
        a3->map  = o->map2;
        a3->dur  = 2;
    } else {
        a3->id   = OBJ_FX_12;
        a3->tile = o->tile;
        a3->anim = o->anim;
        if (o->anim == 0x40 || o->anim == 0x41 || o->anim == 0x42) {
            a3->anim = 0x81;
        }
        a3->map   = o->map;
        a3->timer = 0;
        a3->dur   = 1;
    }
    b->cc06++;
    *spawned = a3;
}

/* ------------------------------------------------------------------ */
/* Targets, hit rates and damage (3607-3878)                          */
/* ------------------------------------------------------------------ */

/* DisplayCharacterWhileFighting: the character's sprite shows for the
 * action. Returns the object. */
static BObj *show_char(Run *r, int c)
{
    BObj *a2 = obj_of_char(r->b, c);
    if (chr(r, c)->hp != 0) {
        a2->f |= F_SHOWN;
    }
    return a2;
}

/* loc_2736: which group the command targets, falling back to the other
 * when that group is gone. Sets enemy_index for {enemy}. */
static int target_group(Run *r, u16 target_word)
{
    Battle *b = r->b;
    int d0 = b->group_count[0];
    int d5 = target_word;
    if (d5 != 0) {
        d0 = b->group_count[1];
    }
    if (d0 < 0) {
        d5 = 0;
        if (target_word == 0) {
            d5 = b->group2_start;
        }
    }
    r->ws->enemy_index = (u8)b->group_id[(d5 != 0) ? 1 : 0];
    return d5;
}

/* loc_270E: a random living enemy of the group. Returns the slot. */
static int random_enemy(Run *r, int d5)
{
    for (int guard = 0; guard < 4096; guard++) {
        int n = rnd(r) & 7;
        if (r->b->stat[n].hp == 0) {
            continue;
        }
        if (obj_of_enemy(r->b, n)->who != d5) {
            continue;
        }
        return n;
    }
    return -1;
}

/* loc_270C */
static int pick_enemy(Run *r, u16 target_word, int *group)
{
    *group = target_group(r, target_word);
    return random_enemy(r, *group);
}

/* CalculateHitRate: dexterity against agility. Returns -1 on a miss,
 * marking the target's object. */
static int hit_rate(Run *r, const CharData *a3, const EnemyStats *a1, BObj *a2)
{
    u16 d0 = (u16)(a3->dexterity << 8);
    if (a1->agility != 0) {
        d0 = (u16)(d0 / a1->agility);
    }
    int d2 = 0xCC;
    if (d0 >= 0x80) {
        d2 = 0xE5;
        if (d0 >= 0x180) {
            d2 = 0xFF;
        }
    }
    if (a3->status & ST_HALF_ATK) {
        d2 >>= 1;
    }
    /* loc_2790 */
    if (d2 != 0) {
        int roll = rnd(r) & 0xFF;
        if (d2 >= roll) {
            return roll;
        }
    }
    a2->f |= F_MISS;
    return -1;
}

/* loc_2790 alone, for the technique rolls. */
static int roll_rate(Run *r, int d2, BObj *a2)
{
    if (d2 != 0) {
        int roll = rnd(r) & 0xFF;
        if (d2 >= roll) {
            return roll;
        }
    }
    a2->f |= F_MISS;
    return -1;
}

/* loc_27C8: the caster's halved hit rate, then the roll. */
static int roll_tech_char(Run *r, const CharData *a3, int d2, BObj *a2)
{
    if (a3->status & ST_HALF_HIT) {
        d2 >>= 1;
    }
    return roll_rate(r, d2, a2);
}

/* loc_27AA: a technique against an enemy -- bosses shrug off the
 * "always" techniques, and a type the technique does not touch makes
 * it not work. */
static int roll_tech_enemy(Run *r, const CharData *a3, const EnemyStats *a1,
                           BObj *a2, int d2, int d6)
{
    Battle *b = r->b;
    bool works = true;
    if (b->escape_rate == 0 && d6 == 0x1F4) {
        works = false;
    } else if ((b->tech_type & (a1->word0 & 0xFF)) == 0) {
        works = false;
    }
    if (!works) {
        b->battle_script = 0x1204;
        d2 = 0;
    }
    return roll_tech_char(r, a3, d2, a2);
}

/* CheckEnemyAlive: the damage off the enemy's HP, the kill marked and
 * its EXP and meseta banked. */
static void enemy_take(Run *r, EnemyStats *a1, BObj *a2, u16 d0)
{
    Battle *b = r->b;
    u16 was = a1->hp;
    a1->hp = (u16)(a1->hp - d0);
    if (d0 < was) {
        return;
    }
    a1->hp = 0;
    a2->f |= F_KILLED;
    b->exp_total    += a1->exp;
    b->meseta_total += a1->meseta;
}

/* CalculateAttackDamage */
static u16 attack_damage(Run *r, const CharData *a3, EnemyStats *a1, BObj *a2,
                         int weapon)
{
    u32 d0 = (u32)((rnd(r) & 0x1F) + 0x54 + a3->attack_base) & 0xFFFF;
    d0 <<= 8;
    u32 d1 = (u32)(a1->defense * 5 + 100) & 0xFFFF;
    if (d1 != 0) {
        u32 q = d0 / d1;
        d0 = (q > 0xFFFF) ? d0 : q;      /* divu overflow leaves d0 alone */
    }
    u16 atk = (u16)(gamedata_item_attack(weapon & 0x7F) + 2);
    /* mulu.w d1, d0 then lsr.w #8, d0: the product's low word only is
     * shifted, and sub.w takes the low word -- a product of $10000 or
     * more wraps. */
    u16 dmg = (u16)((((d0 & 0xFFFF) * atk) & 0xFFFF) >> 8);
    enemy_take(r, a1, a2, dmg);
    return dmg;
}

/* CalculateTechniqueDamage */
static u16 tech_damage(Run *r, EnemyStats *a1, BObj *a2, int d6)
{
    u32 d0 = (u32)((rnd(r) & 0x1F) + 0x54) * (u32)d6;
    u16 dmg = (u16)((d0 / 100) & 0xFFFF);
    enemy_take(r, a1, a2, dmg);
    return dmg;
}

/* loc_284C: a gun's damage. */
static u16 gun_damage(Run *r, EnemyStats *a1, BObj *a2, int d6)
{
    u32 d0 = (u32)((rnd(r) & 0xF) + 0x5C) * (u32)d6;
    u16 dmg = (u16)((d0 / 100) & 0xFFFF);
    enemy_take(r, a1, a2, dmg);
    return dmg;
}

/* loc_2862: is anyone of group d5 left standing? */
static bool group_alive(Battle *b, int d5)
{
    for (int n = 0; n < 5; n++) {
        if (b->stat[n].hp != 0 && obj_of_enemy(b, n)->who == d5) {
            return true;
        }
    }
    return false;
}

/* loc_288E: the damage into the group's info word -- a miss's -1 makes
 * it $FFFF, which the box shows as "miss". */
static void note_damage(Battle *b, int d5, u16 d0)
{
    b->info[(d5 != 0) ? 1 : 0] = (u16)(b->info[(d5 != 0) ? 1 : 0] + d0);
}

/* loc_2D64: one to three turns of paralysis. */
static int paralysis_turns(Run *r)
{
    int roll = rnd(r) & 0xFF;
    int d1 = 1;
    if (roll >= 0x4C) {
        d1++;
        if (roll >= 0x99) {
            d1++;
        }
    }
    return d1;
}

/* An enemy is paralysed (loc_1EB4 / loc_22AC): the turns go into the
 * type byte's low three bits, with bit 3 to say they were clear. */
static void paralyse_enemy(Run *r, EnemyStats *a1)
{
    int d1 = paralysis_turns(r);
    if ((a1->word0 & 7) == 0) {
        d1 |= 8;
    }
    a1->word0 |= (u16)d1;
    r->b->battle_script = 0x120C;
}

/* ------------------------------------------------------------------ */
/* Obj_CharactersBattle (2548)                                        */
/* ------------------------------------------------------------------ */

/* loc_1F62: nothing to do this turn. */
static void char_done(Run *r, BObj *o)
{
    r->b->cc06 = 0;
    o->f &= (u8)~F_ACTING;
}

/* loc_1F30 / loc_2540: the child loaded from the properties' two longs
 * and the character sent into its action animation. */
static void start_action(Run *r, BObj *o, u8 sound, int art1, bool pal1,
                         const char *map1, int art2, bool pal2,
                         const char *map2, int d5)
{
    Battle *b = r->b;
    BObj   *c = &b->obj[obj_index(b, o) + 1];

    b->effect_sound = sound;
    load_effect_art(r, art1);
    c->map  = battle_map_set(map1);
    c->pal  = pal1;
    c->who  = (s16)d5;
    c->d3A  = (s16)(art2 | (pal2 ? 0x80 : 0));
    c->map2 = battle_map_set(map2);
    o->pos   = 0;
    o->state = 2;
}

static void start_weapon_action(Run *r, BObj *o, const WeaponProp *w, int d5)
{
    start_action(r, o, (u8)w->sound, w->art, w->pal, w->map, w->art2, w->pal2,
                 w->map2, d5);
}

static void start_tech_action(Run *r, BObj *o, const TechProp *t, int d5)
{
    start_action(r, o, (u8)t->sound, t->art, t->pal, t->map, t->art2, t->pal2,
                 t->map2, d5);
}

/* loc_F4F8 for one character. */
static void reset_command(Run *r, int c)
{
    Battle *b = r->b;
    u16 *a1 = b->cmd[c & 7];
    const CharData *ch = chr(r, c);
    int d0 = a1[0];

    if (d0 == 1) {
        d0 = 0;
    }
    if (d0 == 2) {
        /* An item stays only while it is in the bag and its kind byte
         * has bit 6 clear. */
        bool found = false;
        for (int i = 0; i < ch->item_count && i < GD_ITEM_SLOTS; i++) {
            if ((ch->items[i] & 0x7F) == a1[1]) {
                found = true;
                break;
            }
        }
        if (found && !(gamedata_item_action_flags(a1[1]) & 0x40)) {
            return;
        }
        d0 = 0;
    }
    /* loc_F584 */
    if (ch->equip[EQUIP_RIGHT] == 0 && ch->equip[EQUIP_LEFT] == 0) {
        d0 = 3;
    }
    a1[0] = (u16)d0;
    memset(&a1[1], 0, 7 * sizeof a1[0]);
}

/* loc_F570 for one character: CharStartCommandsArray, or defend when
 * both hands are empty. */
static void start_command(Battle *b, const GameData *gd, int c)
{
    static const u8 START[GD_CHARS] = { 0, 0, 0, 3, 3, 0, 0, 3 };
    const CharData *ch = &gd->chr[c & 7];
    u16 *a1 = b->cmd[c & 7];
    int d0 = START[c & 7];
    if (ch->equip[EQUIP_RIGHT] == 0 && ch->equip[EQUIP_LEFT] == 0) {
        d0 = 3;
    }
    memset(a1, 0, 8 * sizeof a1[0]);
    a1[0] = (u16)d0;
}

void battle_reset_commands(Battle *b, const GameData *gd, bool from_start_table)
{
    for (int c = 0; c < GD_CHARS; c++) {
        if (from_start_table) {
            start_command(b, gd, c);
            continue;
        }
        Run r;
        memset(&r, 0, sizeof r);
        r.b  = b;
        r.gd = (GameData *)gd;
        reset_command(&r, c);
    }
}

static void process_technique(Run *r, BObj *o, CharData *a3, int d3);

/* loc_1D34 and Battle_CommandUsedIndex. a5 is the command record. */
static void char_command(Run *r, BObj *o);

/* loc_1CF2: a status side effect shown as a message. */
static void char_status_message(Run *r, BObj *o)
{
    o->target = o->who;
    o->f |= F_SHOWN | F_MISS;
    o->anim  = 0xD;
    o->state = 2;
    r->b->command_used = 0;
}

/* loc_1C1C: the character's turn. */
static void char_act(Run *r, BObj *o)
{
    Battle   *b  = r->b;
    CharData *a3 = chr(r, o->who);

    o->x = o->hx;
    if (!(o->f & F_ACTING)) {
        return;
    }
    r->ws->character_index = (u16)o->who;
    u16 d0 = a3->status;

    if ((d0 & 0xFF) != 0) {
        /* Paralysed, or under one of Dark Force's side effects. */
        u16 *a5 = b->cmd[o->who & 7];
        if (a5[0] == 0) {
            a5[2] = (u16)((a5[2] + 1) & 1);
            if (a5[2] != 0) {
                char_done(r, o);
                return;
            }
        }
        if (!(d0 & 0x10)) {
            /* loc_1D18: one turn less of paralysis. */
            a3->status = (u16)((a3->status & 0xFF00) | ((a3->status - 1) & 0xFF));
            if ((a3->status & 0xFF) == 0) {
                win_open(r->ws, WIN_BATTLE_MESSAGE);
                win_script_set(r->ws, 0x1222);
            }
            char_done(r, o);
            return;
        }
        /* The Nei Sword's cure or a side effect (loc_1C9A). */
        if ((rnd(r) & 0xF) == 0) {
            b->battle_script = 0x122A;
            b->palette_effect = BATTLE_FX_NEI_SWORD;
            b->cycle_step     = 0;          /* $FFFFF632 = 0 */
            for (int c = 0; c < GD_CHARS; c++) {
                r->gd->chr[c].status &= 0xFBE0;
            }
            char_status_message(r, o);
            return;
        }
        int d2 = d0 & 7;
        if (d2 == 0) {
            char_command(r, o);
            return;
        }
        b->battle_script = (u16)(0x1223 + d2);
        if (d2 == 4) {
            a3->status &= 0xFFF0;
            a3->attack_base = (a3->attack_base >= 0x28)
                            ? (u16)(a3->attack_base - 0x28) : 0;
        } else if (d2 == 5) {
            a3->status = (u16)((a3->status & 0xFFF0) | ST_SEALED);
        } else if (d2 == 6) {
            a3->status &= 0xFFF0;
            a3->agility = (a3->agility >= 0x28) ? (u16)(a3->agility - 0x28)
                                                : 0;
        }
        char_status_message(r, o);
        return;
    }
    if (d0 & ST_SLEEP) {
        char_done(r, o);
        return;
    }
    char_command(r, o);
}

/* CommandUsed_Attack */
static void command_attack(Run *r, BObj *o, CharData *a3, u16 *a5)
{
    Battle *b = r->b;
    a3->status &= (u16)~ST_DEFEND;

    int d3 = a3->equip[EQUIP_RIGHT];
    a5[1] = (u16)((a5[1] + 1) & 1);
    if (a5[1] == 0) {
        d3 = a3->equip[EQUIP_LEFT];
        if ((gamedata_item_action_flags(d3) & 7) == 3) {
            char_done(r, o);            /* a two-handed weapon: once */
            return;
        }
    }
    /* loc_1D9E */
    int w = d3 - gamedata_item_id("Knife");
    if (w < 0 || w > 0x42) {
        char_done(r, o);
        return;
    }
    b->obj[obj_index(b, o) + 1].anim = (s16)w;
    const WeaponProp *wp = weapon_prop(w);
    if (wp == NULL) {
        char_done(r, o);
        return;
    }
    int d6 = wp->kind;
    int d5;

    if (d6 == 0) {
        /* A hand weapon: the character's own swing, or a slasher's. */
        o->anim = o->who;
        int spread = wp->spread;
        if (spread != 0) {
            o->anim = 9;
        }
        if (spread == 1) {
            /* loc_1E06: a slasher over the whole group. */
            d5 = target_group(r, a5[2]);
            for (int n = 4; n >= 0; n--) {
                EnemyStats *a1 = &b->stat[n];
                BObj *a2 = obj_of_enemy(b, n);
                if (a1->hp == 0 || a2->who != d5) {
                    continue;
                }
                o->target = (s16)(n + 8);
                a2->f |= F_SHOWN;
                o->x = a2->x;
                u16 d0;
                if (hit_rate(r, a3, a1, a2) < 0) {
                    d0 = 0xFFFF;
                } else {
                    d0 = attack_damage(r, a3, a1, a2, d3);
                }
                note_damage(b, d5, d0);
            }
        } else {
            /* loc_1DDE: one enemy. */
            int n = pick_enemy(r, a5[2], &d5);
            if (n < 0) {
                char_done(r, o);
                return;
            }
            EnemyStats *a1 = &b->stat[n];
            BObj *a2 = obj_of_enemy(b, n);
            o->x = a2->x;
            o->target = (s16)(n + 8);
            a2->f |= F_SHOWN;
            u16 d0;
            if (hit_rate(r, a3, a1, a2) < 0) {
                d0 = 0xFFFF;
            } else {
                d0 = attack_damage(r, a3, a1, a2, d3);
            }
            note_damage(b, d5, d0);
        }
        start_weapon_action(r, o, wp, d5);
        return;
    }

    /* loc_1E54: a gun. */
    o->anim = 8;
    int d4 = wp->spread - 1;
    if (d4 == 0) {
        int n = pick_enemy(r, a5[2], &d5);
        if (n < 0) {
            char_done(r, o);
            return;
        }
        EnemyStats *a1 = &b->stat[n];
        BObj *a2 = obj_of_enemy(b, n);
        o->x = a2->x;
        o->target = (s16)(n + 8);
        a2->f |= F_SHOWN;
        u16 d0 = 0;
        if (d6 == 1) {
            /* The Silent Shot: paralysis, 60% of the time, on
             * biomonsters. Against a boss the ROM notes the roll itself
             * as damage (loc_1ED4 with d0 still the roll). */
            b->tech_type = 0x20;
            int roll = roll_tech_enemy(r, a3, a1, a2, 0x99, d6);
            if (roll >= 0) {
                a2->f |= F_MISS;
                if (b->escape_rate != 0) {
                    a2->f &= (u8)~F_MISS;
                    paralyse_enemy(r, a1);
                    start_weapon_action(r, o, wp, d5);
                    return;
                }
                d0 = (u16)roll;
            } else {
                d0 = 0xFFFF;
            }
        } else {
            if (hit_rate(r, a3, a1, a2) < 0) {
                d0 = 0xFFFF;
            } else {
                d0 = gun_damage(r, a1, a2, d6);
            }
            if (d3 == 0x7A) {
                a2->d32++;
            }
        }
        note_damage(b, d5, d0);
        start_weapon_action(r, o, wp, d5);
        return;
    }

    /* loc_1EDA: several shots over the group, the leftmost target sets
     * the character's x. */
    o->anim   = 0xB;
    o->target = 8;
    o->x      = 0x1C0;
    d5 = 0;
    for (;;) {
        int n = pick_enemy(r, a5[2], &d5);
        if (n < 0) {
            break;
        }
        EnemyStats *a1 = &b->stat[n];
        BObj *a2 = obj_of_enemy(b, n);
        if (a2->x < o->x) {
            o->x = a2->x;
        }
        a2->f |= F_SHOWN;
        u16 d0 = 0xFFFF;
        if (!(a2->f & F_MISS)) {
            if (a2->d32 != 0 || hit_rate(r, a3, a1, a2) >= 0) {
                d0 = gun_damage(r, a1, a2, d6);
            }
        }
        a2->d32++;
        note_damage(b, d5, d0);
        if (!group_alive(b, d5)) {
            break;
        }
        if (--d4 < 0) {
            break;
        }
    }
    start_weapon_action(r, o, wp, d5);
}

/* CommandUsed_Defense */
static void command_defend(Run *r, BObj *o, CharData *a3)
{
    a3->status |= ST_DEFEND;
    r->b->cc06 = 0;
    o->f &= (u8)~F_ACTING;
}

/* loc_1FEA: the command falls back to attack (or defend) and runs. */
static void command_fallback(Run *r, BObj *o)
{
    reset_command(r, o->who);
    char_command(r, o);
}

/* loc_24D0 / loc_251C: an item spent, TP paid, the command kept or
 * reset, then the animation. */
static void finish_tech(Run *r, BObj *o, CharData *a3, const TechProp *t,
                        int d5, bool from_item)
{
    Battle *b = r->b;
    WinSys *ws = r->ws;

    if (from_item) {
        /* loc_24D0 */
        int item = ws->item_index;
        if (item < gamedata_item_id("Headgear")) {
            /* Used up: found in the bag and removed. */
            u8 *count;
            u8 *bag = gamedata_bag(r->gd, o->who, &count);
            int slot = 0;
            for (int i = 0; i < GD_ITEM_SLOTS; i++) {
                if (bag[i] == (u8)item) {
                    break;
                }
                slot++;
            }
            ws->item_slot = (u16)slot;
            if (*count > 0 && slot < *count) {
                (*count)--;
                for (int i = slot; i < GD_ITEM_SLOTS - 1; i++) {
                    bag[i] = bag[i + 1];
                }
                bag[GD_ITEM_SLOTS - 1] = 0;
            }
        }
        reset_command(r, o->who);
    } else {
        /* loc_251C */
        a3->tp = (u16)(a3->tp - b->tech_tp);
        if (a3->tp < b->tech_tp) {
            reset_command(r, o->who);
        }
    }
    start_tech_action(r, o, t, d5);
}

/* ------------------------------------------------------------------ */
/* The technique actions (TechniqueActionIndex, 2981-3396)            */
/* ------------------------------------------------------------------ */

/* What every action works from: ProcessTechnique's registers, kept
 * together so the bodies below take one argument. */
typedef struct {
    CharData       *caster;     /* a3                                   */
    u16            *command;    /* a5: [2] is the target chosen         */
    const TechProp *prop;       /* a6: the properties row               */
    int             hit;        /* d2: the success rate rolled against  */
    int             power;      /* d6: the attack rate, $FF read as 500 */
    int             group;      /* d5: the target group, for the child  */
    bool            from_item;  /* battle_command_used != 1             */
} TechCast;

/* Every action returns whether the technique goes on to its animation
 * (loc_24D0 / loc_251C, finish_tech). false means the turn ended
 * already: nothing to do (loc_1F62, char_done) or the command fallen
 * back (loc_1FEA, command_fallback). */

/* loc_270C for the one-enemy actions: the target picked from the
 * command's group and shown, the caster over it. Returns the slot, or
 * -1 when the group has nobody left (random_enemy's guard; the ROM
 * would loop). */
static int tech_target(Run *r, BObj *o, TechCast *tc, EnemyStats **a1,
                       BObj **a2)
{
    int n = pick_enemy(r, tc->command[2], &tc->group);
    if (n < 0) {
        return -1;
    }
    *a1 = &r->b->stat[n];
    *a2 = obj_of_enemy(r->b, n);
    o->x = (*a2)->x;
    o->target = (s16)(n + 8);
    (*a2)->f |= F_SHOWN;
    return n;
}

/* loc_27AA, CalculateTechniqueDamage and loc_288E on one enemy: the
 * roll, the damage on a hit, and either into the group's info word (a
 * miss's -1 shows as "miss"). */
static void tech_hit_enemy(Run *r, TechCast *tc, EnemyStats *a1, BObj *a2,
                           int group)
{
    u16 d0 = 0xFFFF;
    if (roll_tech_enemy(r, tc->caster, a1, a2, tc->hit, tc->power) >= 0) {
        d0 = tech_damage(r, a1, a2, tc->power);
    }
    note_damage(r->b, group, d0);
}

/* loc_2148: every living enemy rolled and hit, with the effect over the
 * middle of the screen; the second group's damage moved to its own word
 * and a missing first group's cleared. TechAction_Gra's body, and
 * TechAction_Megid's first half. */
static void tech_hit_every_enemy(Run *r, BObj *o, TechCast *tc)
{
    Battle *b = r->b;
    tc->group = 0;
    o->x = 0x120;
    o->target = 8;
    for (int n = 0; n < 5; n++) {
        EnemyStats *a1 = &b->stat[n];
        BObj *a2 = obj_of_enemy(b, n);
        if (a1->hp == 0) {
            continue;
        }
        a2->f |= F_SHOWN;
        tech_hit_enemy(r, tc, a1, a2, 0);
    }
    if (b->group_count[1] >= 0) {
        b->info[1] = b->info[0];
    }
    if (b->group_count[0] < 0) {
        b->info[0] = 0;
    }
}

/* TechAction_Foi: one enemy of the chosen group -- Foi, Tsu, Brose, Gaj,
 * Gen, Vol and their Gi- and Na- forms. */
static bool tech_one_enemy(Run *r, BObj *o, TechCast *tc)
{
    EnemyStats *a1;
    BObj *a2;
    if (tech_target(r, o, tc, &a1, &a2) < 0) {
        char_done(r, o);
        return false;
    }
    tech_hit_enemy(r, tc, a1, a2, tc->group);
    return true;
}

/* TechAction_Gra: every enemy -- Gra, Sag, Sagen and the Gi- and Na-
 * forms. */
static bool tech_every_enemy(Run *r, BObj *o, TechCast *tc)
{
    tech_hit_every_enemy(r, o, tc);
    return true;
}

/* TechAction_Zan: the chosen group -- Zan, Eijia, Savol and the Gi- and
 * Na- forms -- or, when the technique came from an item rather than the
 * Technique command, every enemy (its `bne.s TechAction_Gra`). */
static bool tech_group(Run *r, BObj *o, TechCast *tc)
{
    Battle *b = r->b;
    if (tc->from_item) {
        return tech_every_enemy(r, o, tc);
    }
    tc->group = target_group(r, tc->command[2]);
    o->x = 0x120;
    o->target = 8;
    for (int n = 0; n < 5; n++) {
        EnemyStats *a1 = &b->stat[n];
        BObj *a2 = obj_of_enemy(b, n);
        if (a1->hp == 0 || a2->who != tc->group) {
            continue;
        }
        a2->f |= F_SHOWN;
        tech_hit_enemy(r, tc, a1, a2, tc->group);
    }
    return true;
}

/* TechAction_Shift: the caster's own agility, dexterity and both attack
 * words up by 20 for the fight ("Fighting ability increases!"). */
static bool tech_shift(Run *r, BObj *o, TechCast *tc)
{
    CharData *a3 = tc->caster;
    o->target = o->who;
    BObj *a2 = show_char(r, o->who);
    if (roll_tech_char(r, a3, tc->hit, a2) >= 0) {
        a2->f |= F_MISS;
        r->b->battle_script = 0x1209;
        a3->agility     = (u16)(a3->agility + 0x14);
        a3->dexterity   = (u16)(a3->dexterity + 0x14);
        a3->attack      = (u16)(a3->attack + 0x14);
        a3->attack_base = (u16)(a3->attack_base + 0x14);
    }
    return true;
}

/* TechAction_Fanbi: one enemy, the damage dealt drained into the
 * caster's HP up to the maximum. */
static bool tech_fanbi(Run *r, BObj *o, TechCast *tc)
{
    CharData *a3 = tc->caster;
    EnemyStats *a1;
    BObj *a2;
    if (tech_target(r, o, tc, &a1, &a2) < 0) {
        char_done(r, o);
        return false;
    }
    u16 d0 = 0xFFFF;
    if (roll_tech_enemy(r, a3, a1, a2, tc->hit, tc->power) >= 0) {
        d0 = tech_damage(r, a1, a2, tc->power);
        a3->hp = (u16)(a3->hp + d0);
        if (a3->hp > a3->max_hp) {
            a3->hp = a3->max_hp;
        }
    }
    note_damage(r->b, tc->group, d0);
    return true;
}

/* TechAction_Conte, Doran and Forsa share loc_223C: one enemy rolled
 * for, and on a hit a bit of its status byte set with a message --
 * bit 3, "is quiet" ($120A: Conte, Shiza) or bit 4, "has gone insane"
 * ($120B: Doran, Forsa) -- unless the fight is one that cannot be run
 * from (a boss), when the hit shows as a miss. Forsa's effect plays over
 * the middle of the screen rather than the target. */
static bool tech_afflict_enemy(Run *r, BObj *o, TechCast *tc, u16 script,
                               int bit, bool from_the_middle)
{
    Battle *b = r->b;
    EnemyStats *a1;
    BObj *a2;
    if (tech_target(r, o, tc, &a1, &a2) < 0) {
        char_done(r, o);
        return false;
    }
    if (from_the_middle) {
        o->x = 0x120;
    }
    if (roll_tech_enemy(r, tc->caster, a1, a2, tc->hit, tc->power) >= 0) {
        a2->f |= F_MISS;
        if (b->escape_rate != 0) {
            a2->f &= (u8)~F_MISS;
            a1->word0 |= (u16)(1 << (bit + 8));
            b->battle_script = script;
        }
    }
    return true;
}

/* TechAction_Rimit (Rimit, Rimet): one enemy paralysed for one to three
 * turns ("is paralyzed!"), with the same boss exception. */
static bool tech_paralyse(Run *r, BObj *o, TechCast *tc)
{
    Battle *b = r->b;
    EnemyStats *a1;
    BObj *a2;
    if (tech_target(r, o, tc, &a1, &a2) < 0) {
        char_done(r, o);
        return false;
    }
    if (roll_tech_enemy(r, tc->caster, a1, a2, tc->hit, tc->power) >= 0) {
        a2->f |= F_MISS;
        if (b->escape_rate != 0) {
            a2->f &= (u8)~F_MISS;
            paralyse_enemy(r, a1);
        }
    }
    return true;
}

/* TechAction_Shinb: the party runs away, from a fight that allows it. */
static bool tech_shinb(Run *r, BObj *o, TechCast *tc)
{
    Battle *b = r->b;
    o->target = o->who;
    BObj *a2 = show_char(r, o->who);
    a2->f |= F_MISS;
    if (b->escape_rate != 0) {
        /* The ROM branches to loc_27AA here, which the disassembly
         * notes is the wrong place and is why Shinb never works; it
         * lands in data. The port takes the disassembly's fix, loc_27C8:
         * the roll alone. */
        if (roll_tech_char(r, tc->caster, tc->hit, a2) >= 0) {
            b->battle_script = 0x1202;
            b->main_routine  = 2;
            r->ws->event_routine = 2;
        }
    }
    return true;
}

/* The party boosts, TechAction_Shu/Sashu (loc_22FA), Ner/Saner
 * (loc_235C) and Deban (loc_2330), one loop: the chosen member (Shu and
 * Ner leave a5 on the command's target word) or the whole party (Sashu,
 * Saner and Deban point it at the party list) rolled for, each success
 * given the boost. Up to build 95 the port gave Shu and Ner to the first
 * member whoever was chosen. */
enum { BOOST_DEFENSE, BOOST_AGILITY, BOOST_DEBAN };

static bool tech_boost_party(Run *r, BObj *o, TechCast *tc, bool whole_party,
                             int boost)
{
    Battle *b = r->b;
    int count = whole_party ? r->gd->party_members_num : 0;
    for (int i = 0; i <= count; i++) {
        int c = whole_party ? r->gd->party_member_id[i]
                            : (int)tc->command[2];
        if (i == 0) {
            o->target = (s16)c;
        }
        BObj *a2 = show_char(r, c);
        if (roll_tech_char(r, tc->caster, tc->hit, a2) < 0) {
            continue;
        }
        a2->f |= F_MISS;
        CharData *a1 = chr(r, c);
        if (boost == BOOST_DEFENSE) {
            b->battle_script = 0x120D;      /* "Defense ability increases!" */
            a1->defense = (u16)(a1->defense + 0x14);
        } else if (boost == BOOST_AGILITY) {
            b->battle_script = 0x120E;      /* "Agility increases!" */
            a1->agility = (u16)(a1->agility + 0xA);
        } else {
            a1->status |= ST_DEBAN;
        }
    }
    return true;
}

/* The heals, TechAction_Res/Sar (loc_2392) and Sak/Nasak (loc_23EC),
 * one loop: the chosen member or the whole party rolled for, and every
 * living success healed by the power (Nares and Nasar, rate $FF, by
 * 10000) or in full -- shown healed but given nothing when poisoned
 * (`tst.w (a1); bmi`). Nobody healed means the command falls back
 * (loc_1FEA). */
static bool tech_heal(Run *r, BObj *o, TechCast *tc, bool whole_party,
                      bool in_full)
{
    if (!in_full && tc->power == 0x1F4) {
        tc->power = 0x2710;
    }
    bool any = false;
    int count = whole_party ? r->gd->party_members_num : 0;
    for (int i = 0; i <= count; i++) {
        int c = whole_party ? r->gd->party_member_id[i]
                            : (int)tc->command[2];
        if (i == 0) {
            o->target = (s16)c;
        }
        CharData *a1 = chr(r, c);
        BObj *a2 = show_char(r, c);
        if (roll_tech_char(r, tc->caster, tc->hit, a2) < 0 || a1->hp == 0) {
            continue;
        }
        any = true;
        a2->f |= F_MISS;
        if (a1->status & ST_POISON) {
            continue;
        }
        if (in_full) {
            a1->hp = a1->max_hp;
        } else {
            a1->hp = (u16)(a1->hp + tc->power);
            if (a1->hp > a1->max_hp) {
                a1->hp = a1->max_hp;
            }
        }
    }
    if (!any) {
        command_fallback(r, o);
        return false;
    }
    return true;
}

/* TechAction_Sak / Nasak: the heal in full, paid for with the caster's
 * life ("gives up a life!"): HP 0, and the object marked shown, killed
 * and quiet. */
static bool tech_give_up_life(Run *r, BObj *o, TechCast *tc, bool whole_party)
{
    if (!tech_heal(r, o, tc, whole_party, true)) {
        return false;
    }
    r->b->battle_script = 0x120F;
    tc->caster->hp = 0;
    o->f = (u8)((o->f & 7) | (F_SHOWN | F_KILLED | F_QUIET));
    return true;
}

/* TechAction_Anti: the chosen member's poison cleared; a dead one falls
 * back. */
static bool tech_anti(Run *r, BObj *o, TechCast *tc)
{
    int c = tc->command[2];
    o->target = (s16)c;
    CharData *a1 = chr(r, c);
    BObj *a2 = show_char(r, c);
    if (a1->hp == 0) {
        command_fallback(r, o);
        return false;
    }
    a2->f |= F_MISS;
    a1->status &= (u16)~ST_POISON;
    return true;
}

/* TechAction_Rever: the chosen dead member back to full HP and standing
 * (routine 1); a living one falls back. */
static bool tech_rever(Run *r, BObj *o, TechCast *tc)
{
    int c = tc->command[2];
    o->target = (s16)c;
    CharData *a1 = chr(r, c);
    BObj *a2 = show_char(r, c);
    if (a1->hp != 0) {
        command_fallback(r, o);
        return false;
    }
    a2->x = a2->hx;
    a2->f |= F_SHOWN | F_MISS;
    a1->hp = a1->max_hp;
    a2->state = 1;
    return true;
}

/* TechAction_Megid (Ryuka, Hinas, Musik and Megid in a fight): every
 * enemy at rate 400, then everyone in the party but Rolf halved (the
 * loop's `beq` skips member 0, as written), never below 1. */
static bool tech_megid(Run *r, BObj *o, TechCast *tc)
{
    tc->power = 0x190;
    tech_hit_every_enemy(r, o, tc);
    o->x = o->hx;
    o->target = 0;
    for (int i = 0; i <= r->gd->party_members_num; i++) {
        int c = r->gd->party_member_id[i];
        if (c == 0) {
            continue;
        }
        CharData *a1 = chr(r, c);
        show_char(r, c);
        if (a1->hp == 0) {
            continue;
        }
        a1->hp >>= 1;
        if (a1->hp == 0) {
            a1->hp = 1;
        }
    }
    return true;
}

/* ProcessTechnique (2937): the technique's cost, type and power looked
 * up, the child told which technique, the TP checked when it came from
 * the Technique command, then the action by id. */
static void process_technique(Run *r, BObj *o, CharData *a3, int d3)
{
    Battle *b  = r->b;
    WinSys *ws = r->ws;
    TechCast tc;

    tc.caster    = a3;
    tc.command   = b->cmd[o->who & 7];
    tc.from_item = (b->command_used != 1);
    tc.group     = 0;

    ws->technique_index = (u8)d3;
    b->tech_tp    = gamedata_tech_tp(d3);
    b->tech_type  = (u8)(gamedata_tech_flags(d3) & 0x60);
    tc.power = gamedata_tech_rate(d3);
    if (tc.power == 0xFF) {
        tc.power = 0x1F4;
    }
    tc.prop = tech_prop(d3);
    if (tc.prop == NULL) {
        char_done(r, o);
        return;
    }
    b->obj[obj_index(b, o) + 1].anim = (s16)d3;
    o->anim = 0xA;
    if (!tc.from_item && a3->tp < b->tech_tp) {
        command_fallback(r, o);
        return;
    }
    tc.hit = tc.prop->rate;

    bool goes_on;
    switch (d3) {
        case TECH_FOI: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_GIFOI: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_NAFOI: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_ZAN: {
            goes_on = tech_group(r, o, &tc);
            break;
        }
        case TECH_GIZAN: {
            goes_on = tech_group(r, o, &tc);
            break;
        }
        case TECH_NAZAN: {
            goes_on = tech_group(r, o, &tc);
            break;
        }
        case TECH_GRA: {
            goes_on = tech_every_enemy(r, o, &tc);
            break;
        }
        case TECH_GIGRA: {
            goes_on = tech_every_enemy(r, o, &tc);
            break;
        }
        case TECH_NAGRA: {
            goes_on = tech_every_enemy(r, o, &tc);
            break;
        }
        case TECH_TSU: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_GITHU: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_NATHU: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_SHIFT: {
            goes_on = tech_shift(r, o, &tc);
            break;
        }
        case TECH_FANBI: {
            goes_on = tech_fanbi(r, o, &tc);
            break;
        }
        case TECH_EIJIA: {
            goes_on = tech_group(r, o, &tc);
            break;
        }
        case TECH_BROSE: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_CONTE: {
            goes_on = tech_afflict_enemy(r, o, &tc, 0x120A, 3, false);
            break;
        }
        case TECH_GAJ: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_GIGAJ: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_NAGAJ: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_SAG: {
            goes_on = tech_every_enemy(r, o, &tc);
            break;
        }
        case TECH_GISAG: {
            goes_on = tech_every_enemy(r, o, &tc);
            break;
        }
        case TECH_NASAG: {
            goes_on = tech_every_enemy(r, o, &tc);
            break;
        }
        case TECH_GEN: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_SAGEN: {
            goes_on = tech_every_enemy(r, o, &tc);
            break;
        }
        case TECH_VOL: {
            goes_on = tech_one_enemy(r, o, &tc);
            break;
        }
        case TECH_SAVOL: {
            goes_on = tech_group(r, o, &tc);
            break;
        }
        case TECH_SHIZA: {
            goes_on = tech_afflict_enemy(r, o, &tc, 0x120A, 3, false);
            break;
        }
        case TECH_DORAN: {
            goes_on = tech_afflict_enemy(r, o, &tc, 0x120B, 4, false);
            break;
        }
        case TECH_RIMIT: {
            goes_on = tech_paralyse(r, o, &tc);
            break;
        }
        case TECH_SHINB: {
            goes_on = tech_shinb(r, o, &tc);
            break;
        }
        case TECH_FORSA: {
            goes_on = tech_afflict_enemy(r, o, &tc, 0x120B, 4, true);
            break;
        }
        case TECH_RIMET: {
            goes_on = tech_paralyse(r, o, &tc);
            break;
        }
        case TECH_SHU: {
            goes_on = tech_boost_party(r, o, &tc, false, BOOST_DEFENSE);
            break;
        }
        case TECH_SASHU: {
            goes_on = tech_boost_party(r, o, &tc, true, BOOST_DEFENSE);
            break;
        }
        case TECH_DEBAN: {
            goes_on = tech_boost_party(r, o, &tc, true, BOOST_DEBAN);
            break;
        }
        case TECH_NER: {
            goes_on = tech_boost_party(r, o, &tc, false, BOOST_AGILITY);
            break;
        }
        case TECH_SANER: {
            goes_on = tech_boost_party(r, o, &tc, true, BOOST_AGILITY);
            break;
        }
        case TECH_RES: {
            goes_on = tech_heal(r, o, &tc, false, false);
            break;
        }
        case TECH_GIRES: {
            goes_on = tech_heal(r, o, &tc, false, false);
            break;
        }
        case TECH_NARES: {
            goes_on = tech_heal(r, o, &tc, false, false);
            break;
        }
        case TECH_SAR: {
            goes_on = tech_heal(r, o, &tc, true, false);
            break;
        }
        case TECH_GISAR: {
            goes_on = tech_heal(r, o, &tc, true, false);
            break;
        }
        case TECH_NASAR: {
            goes_on = tech_heal(r, o, &tc, true, false);
            break;
        }
        case TECH_SAK: {
            goes_on = tech_give_up_life(r, o, &tc, false);
            break;
        }
        case TECH_NASAK: {
            goes_on = tech_give_up_life(r, o, &tc, true);
            break;
        }
        case TECH_ANTI: {
            goes_on = tech_anti(r, o, &tc);
            break;
        }
        case TECH_REVER: {
            goes_on = tech_rever(r, o, &tc);
            break;
        }
        default: {
            /* Ryuka, Hinas, Musik, Megid, and anything past the table. */
            goes_on = tech_megid(r, o, &tc);
            break;
        }
    }
    if (goes_on) {
        finish_tech(r, o, a3, tc.prop, tc.group, tc.from_item);
    }
}

/* CommandUsed_Technique */
static void command_technique(Run *r, BObj *o, CharData *a3, u16 *a5)
{
    if (a3->status & ST_SEALED) {
        command_fallback(r, o);
        return;
    }
    process_technique(r, o, a3, a5[1]);
}

/* CommandUsed_Item */
static void command_item(Run *r, BObj *o, CharData *a3, u16 *a5)
{
    Battle *b  = r->b;
    WinSys *ws = r->ws;
    int item = a5[1];
    ws->item_index = (u8)item;

    int tech = 0;
    if (gamedata_item_action_flags(item) & 0x10) {
        /* ItemUsedIndex, by id: $11-$16 the mates, Antidote, Star Mist
         * and Moon Dew; $20-$24 Crescegear, Snow Crown, Wind Scarf,
         * Color Scarf and Storm Gear; then the named ones. */
        static const struct { const char *key; int tech; bool self; } TABLE[] = {
            { "Monomate",   TECH_RES,   false },
            { "Dimate",     TECH_GIRES, false },
            { "Trimate",    TECH_NARES, false },
            { "Antidote",   TECH_ANTI,  false },
            { "StarMist",   TECH_NASAR, false },
            { "MoonDew",    TECH_REVER, false },
            { "Crescegear", TECH_GIRES, true },
            { "SnowCrown",  TECH_DEBAN, false },
            { "WindScarf",  TECH_ZAN,   false },
            { "ColorScarf", TECH_SANER, false },
            { "StormGear",  TECH_GIZAN, false },
            { "AmberRobe",  TECH_GIRES, true },
            { "Crystanish", TECH_GRA,   false },
            { "CrystCape",  TECH_GRA,   false },
            { "CrystChest", TECH_GRA,   false },
            { "Aegis",      TECH_GIRES, true },
            { "GrSleeves",  TECH_SHINB, false },
            { "TruthSlvs",  TECH_GIRES, true },
            { "FireStaff",  TECH_FOI,   false },
        };
        for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++) {
            if (gamedata_item_id(TABLE[i].key) == item) {
                tech = TABLE[i].tech;
                if (TABLE[i].self) {
                    a5[2] = (u16)o->who;    /* ItemUsed_GiresGear */
                }
                break;
            }
        }
    }
    if (tech == 0) {
        /* ItemUsed_NoEffect */
        win_open(ws, WIN_BATTLE_ITEM_USED);
        start_command(b, r->gd, o->who);    /* loc_F570, the user only */
        o->anim  = 0xC;
        o->state = 2;
        return;
    }
    process_technique(r, o, a3, tech);
}

static u32 g_debug_commands[4];

void battle_debug_commands(u32 out[4])
{
    memcpy(out, g_debug_commands, sizeof g_debug_commands);
}

u32 battle_debug_enemies_joined(void)
{
    return g_debug_joined;
}

static void char_command(Run *r, BObj *o)
{
    Battle   *b  = r->b;
    CharData *a3 = chr(r, o->who);
    u16      *a5 = b->cmd[o->who & 7];
    int d0 = a5[0];

    b->command_used = (s16)d0;
    g_debug_commands[d0 & 3]++;
    switch (d0 & 3) {
        case 0: {
            command_attack(r, o, a3, a5);
            break;
        }
        case 1: {
            command_technique(r, o, a3, a5);
            break;
        }
        case 2: {
            command_item(r, o, a3, a5);
            break;
        }
        default: {
            command_defend(r, o, a3);
            break;
        }
    }
}

/* loc_1B2E: standing by, or the fight's turn. */
static void char_main(Run *r, BObj *o)
{
    Battle *b = r->b;

    if (!b->fight_active) {
        o->x = o->hx;
        o->y = o->hy;
        o->r = R_SCREEN;
        o->frame = 0;
        return;
    }
    o->y = 0x128;
    o->r = R_SCREEN | R_HIDDEN;
    if (!(o->f & F_SHOWN)) {
        char_act(r, o);
        return;
    }
    if (!(o->f & F_HIT)) {
        o->frame = 0;
        o->r = R_SCREEN;
        return;
    }
    if (--o->blink < 0) {
        if (o->f & F_KILLED) {
            o->state = 3;
            bool quiet = (o->f & F_QUIET) != 0;
            o->f &= (u8)~F_QUIET;
            if (!quiet) {
                r->ws->character_index = (u16)o->who;
                win_open(r->ws, WIN_BATTLE_MESSAGE);
                win_script_set(r->ws, 0x1206);
            }
        }
        o->f = 0;
        b->cc06--;
        return;
    }
    /* loc_1BA8 */
    if (!(o->f & F_MISS)) {
        int d0 = o->blink & 0xF;
        if (d0 == 8) {
            video_backdrop(r->video, 0x00E);
            if (b->formation == 0x103) {
                for (int i = 0x38; i < 0x40; i++) {
                    video_glow(r->video, i, 0x00E);
                }
            }
            return;
        }
        if (d0 != 0) {
            return;
        }
        play(r, SFX_RED_SCREEN);
        video_backdrop(r->video, 0x200);
        if (b->formation == 0x103) {
            for (int i = 0x38; i < 0x40; i++) {
                video_glow(r->video, i, 0);
            }
            video_backdrop(r->video, 0);
        }
    }
    o->frame = 0;
    o->r = R_SCREEN;
}

static void run_char(Run *r, BObj *o)
{
    switch (o->state) {
        case 0: {
            /* ObjCB_Init */
            o->r    = R_SCREEN | R_HIDDEN;
            o->hx   = o->x;
            o->hy   = o->y;
            o->d1C  = -0x18;
            o->anim = o->who;
            o->pos  = 0;
            o->dur  = 3;
            o->state = 1;
            if (chr(r, o->who)->hp == 0) {
                o->state = 3;
            }
            break;
        }
        case 1: {
            char_main(r, o);
            break;
        }
        case 2: {
            /* loc_26B4 */
            animate(r, o, anim_table(ANIM_CHARS));
            if (o->anim < 8) {
                o->y = 0x100;
            }
            if (!(o->f & F_ACTING)) {
                o->y = 0x128;
                o->state = 1;
            }
            break;
        }
        default: {
            /* ObjCB_Dead */
            o->r = R_SCREEN | R_HIDDEN;
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Obj_EnemiesBattle (3879)                                           */
/* ------------------------------------------------------------------ */

static EnemyStats *enemy_stat_of(Battle *b, const BObj *o)
{
    return &b->stat[((obj_index(b, o) - BOBJ_ENEMIES) / 2) & 7];
}

/* Enemy_PickCharToAttack: a party position from Battle_CharTargetChances,
 * a living one, made to show at the enemy's x. Returns the id. */
static int pick_char(Run *r, BObj *o)
{
    for (int guard = 0; guard < 4096; guard++) {
        int idx = g_target_chances[rnd(r) & 7];
        if (idx > r->gd->party_members_num) {
            continue;
        }
        int c = r->gd->party_member_id[idx];
        o->target = (s16)c;
        if (chr(r, c)->hp == 0) {
            continue;
        }
        BObj *a2 = obj_of_char(r->b, c);
        a2->x = o->x;
        a2->f |= F_SHOWN;
        a2->f &= (u8)~F_HIT;
        o->pos = 0;
        r->ws->character_index = (u16)c;
        return c;
    }
    return r->gd->party_member_id[0];
}

/* loc_2AA8: an enemy's blow. */
static void enemy_strike(Run *r, const EnemyStats *a3, CharData *a1, BObj *a2)
{
    u32 d0 = (u32)((rnd(r) & 0x3F) + 0x44 + a3->attack) & 0xFFFF;
    d0 <<= 8;
    u32 d1 = (u32)(a1->defense * 5 + 100) & 0xFFFF;
    if (d1 != 0) {
        u32 q = d0 / d1;
        d0 = (q > 0xFFFF) ? d0 : q;
    }
    /* mulu.w then lsr.w #8: the low word of the product, as in
     * CalculateAttackDamage -- a strong enemy against light armour
     * wraps. */
    u16 dmg = (u16)((((d0 & 0xFFFF) * a3->attack) & 0xFFFF) >> 8);
    if (dmg == 0) {
        a2->f |= F_MISS;
        return;
    }
    if (a1->status & (ST_DEBAN | ST_DEFEND)) {
        dmg >>= 1;
        if (dmg == 0) {
            a2->f |= F_MISS;
            return;
        }
    }
    if (a3->word0 & ST_HALF_DMG) {
        if ((rnd(r) & 0xFF) >= 0x80) {
            a2->f |= F_MISS;
            return;
        }
    }
    if (dmg >= a1->hp) {
        a1->hp = 0;
        a2->f |= F_KILLED;
        return;
    }
    a1->hp = (u16)(a1->hp - dmg);
}

/* Enemy_CalculateAttackDamage: a technique's damage at rate d6. */
static u16 enemy_tech_strike(Run *r, CharData *a1, BObj *a2, int d6)
{
    u32 d0 = (u32)((rnd(r) & 0x1F) + 0x54) * (u32)d6 / 100;
    u16 dmg = (u16)(d0 & 0xFFFF);
    if (a1->status & (ST_DEBAN | ST_DEFEND)) {
        dmg >>= 1;
        if (dmg == 0) {
            a2->f |= F_MISS;
            return 0;
        }
    }
    if (dmg >= a1->hp) {
        a1->hp = 0;
        a2->f |= F_KILLED;
        return dmg;
    }
    a1->hp = (u16)(a1->hp - dmg);
    return dmg;
}

/* loc_2C28: whether an enemy's technique lands on the character,
 * from EnemyTechSuccessRate by the character's luck. */
static int enemy_tech_roll_luck(Run *r, const EnemyStats *a3, int luck,
                                BObj *a2)
{
    int d0 = luck;
    if (d0 != 0) {
        d0 /= 50;
    }
    if (d0 >= 3) {
        d0 = 3;
    }
    int idx = a3->success_index + d0;
    int d2 = (idx < 32) ? g_success_rate[idx] : 0;
    if (a3->word0 & ST_HALF_HIT) {
        d2 >>= 1;                       /* Conte on the caster */
    }
    int roll = rnd(r) & 0xFF;
    if (d2 > roll) {
        return roll;
    }
    a2->f |= F_MISS;
    return -1;
}

static int enemy_tech_roll(Run *r, const EnemyStats *a3, const CharData *a1,
                           BObj *a2)
{
    return enemy_tech_roll_luck(r, a3, a1->luck, a2);
}

/* loc_2B86: a random living enemy of the other group... of this one's
 * group, as written: d5 is this object's own group. */
static int pick_enemy_ally(Run *r, BObj *o)
{
    Battle *b = r->b;
    int d0 = b->group_count[0];
    int d5 = o->who;
    if (d5 != 0) {
        d0 = b->group_count[1];
    }
    if (d0 < 0) {
        d5 = 0;
        if (o->who == 0) {
            d5 = b->group2_start;
        }
    }
    int n = random_enemy(r, d5);
    if (n < 0) {
        return -1;
    }
    o->target = (s16)(n + 8);
    BObj *a2 = obj_of_enemy(b, n);
    a2->f |= F_SHOWN;
    a2->f &= (u8)~F_HIT;
    o->pos = 0;
    return n;
}

/* loc_2D08: the whole party. */
static void enemy_tech_all(Run *r, BObj *o, EnemyStats *a3, int d6)
{
    o->target = 0;
    o->pos = 0;
    for (int i = 0; i <= r->gd->party_members_num; i++) {
        int c = r->gd->party_member_id[i];
        CharData *a1 = chr(r, c);
        BObj *a2 = show_char(r, c);
        if (a1->hp == 0) {
            continue;
        }
        if (enemy_tech_roll(r, a3, a1, a2) < 0) {
            continue;
        }
        enemy_tech_strike(r, a1, a2, d6);
    }
    o->state = 3;
}

/* loc_2A98 */
static void enemy_attack(Run *r, BObj *o, EnemyStats *a3)
{
    int c = pick_char(r, o);
    o->state = 2;
    play(r, (u8)o->sfx_attack);
    enemy_strike(r, a3, chr(r, c), obj_of_char(r->b, c));
}

/* loc_2FA4, for loc_2F6C: enemy slot `to` takes a copy of `o` if it
 * is a dead one -- id 0 and routine 4, which is what ObjEnmBattle_Dead
 * leaves -- and its address is not below $E800 (slot -1 fails the bcs).
 * The object's 64 bytes are copied over but the copy keeps its own $A
 * long (x, and home x at $C) and the child's word at $40; 3(a2) = $80
 * (shown, nothing else) and routine 4, so ObjEnmBattle_Dead brings it
 * in when the hit lands. The stats' 64 bytes follow, and $2C(a0) points
 * at the new slot. Slot 8 would be $EC00, the first effect object, with
 * its stats past enemy_stat_buffer's eight; five columns never reach
 * it, so the port stops at slot 7. Returns the slot, or -1. */
static int enemy_clone_into(Run *r, BObj *o, EnemyStats *a3, int to)
{
    Battle *b = r->b;
    if (to < 0 || to > 7) {
        return -1;
    }
    BObj *a2 = obj_of_enemy(b, to);
    if (a2->id != 0 || a2->state != 4) {
        return -1;
    }
    s16 x   = a2->x;
    s16 hx  = a2->hx;
    s16 d40 = a2->d40;
    *a2 = *o;
    a2->x   = x;
    a2->hx  = hx;
    a2->d40 = d40;
    a2->f     = F_SHOWN;
    a2->state = 4;
    b->stat[to] = *a3;
    o->target = (s16)(to + 8);
    return to;
}

/* EnemyTechniqueTable (4218) by the enemy's technique byte
 * (EnemyData byte 47): the table is entered at the byte times four less
 * eight, so 2 is its first entry. A byte with bit 7 set is a damage
 * technique (loc_2CEA), handled before the table. */
enum {
    ENEMY_TECH_ALL            = 2,  /* loc_2D08: the technique on everyone  */
    ENEMY_TECH_PARALYSE       = 3,  /* EnemyTechnique_Paralyze              */
    ENEMY_TECH_POISON         = 4,  /* loc_2D7C                             */
    ENEMY_TECH_SLEEP          = 5,  /* loc_2DA4                             */
    ENEMY_TECH_DRAIN          = 6,  /* loc_2DCC: HP drained into the enemy  */
    ENEMY_TECH_ARMY_EYE       = 7,  /* loc_2E24: the ninth use, to Gaira    */
    ENEMY_TECH_DARK_FORCE     = 8,  /* loc_2E82: the curse or the blast     */
    ENEMY_TECH_HEAL_ALLY      = 9,  /* loc_2F10: 20 HP to an ally           */
    ENEMY_TECH_HEAL_ALLY_FULL = 10, /* loc_2F44                             */
    ENEMY_TECH_STRIKE         = 11, /* loc_2F5C: an attack, animated as one */
    ENEMY_TECH_CLONE          = 12, /* loc_2F6C: the enemy copies itself    */
    ENEMY_TECH_STRIKE_B       = 13, /* loc_3004: loc_2F5C again             */
    ENEMY_TECH_EMPTY_TP       = 14, /* loc_3014                             */
    ENEMY_TECH_ATTACK_DOWN    = 15, /* loc_302C: both attack words by 20    */
    ENEMY_TECH_DEFENSE_ZERO   = 16, /* loc_3060                             */
    ENEMY_TECH_DEFENSE_DOWN   = 17, /* loc_307E: defense by 20, $121E       */
    ENEMY_TECH_DEFENSE_DOWN_B = 18, /* loc_30A4: the same, $121F            */
    ENEMY_TECH_KILL           = 19  /* loc_30CA                             */
};

/* loc_2F5C and loc_3004, the same routine twice: an attack on a random
 * member with the technique's animation and sound. */
static void enemy_tech_strike_one(Run *r, BObj *o, EnemyStats *a3)
{
    int c = pick_char(r, o);
    enemy_strike(r, a3, chr(r, c), obj_of_char(r->b, c));
    o->state = 3;
}

/* loc_307E and loc_30A4, the same routine with two messages: a random
 * member's defense down by 20, not below 0 ("Defense ability
 * decreased!", $121E, or "Agility decreased!", $121F -- the second says
 * the wrong thing, as written). */
static void enemy_tech_defense_down(Run *r, BObj *o, EnemyStats *a3,
                                    u16 script)
{
    int c = pick_char(r, o);
    CharData *a1 = chr(r, c);
    BObj *a2 = obj_of_char(r->b, c);
    if (enemy_tech_roll(r, a3, a1, a2) >= 0) {
        r->b->battle_script = script;
        a1->defense = (a1->defense >= 0x14) ? (u16)(a1->defense - 0x14) : 0;
    }
    o->state = 3;
}

/* Enemy_ProcessTechnique (4210) and EnemyTechniqueTable. */
static void enemy_technique(Run *r, BObj *o, EnemyStats *a3, int d6)
{
    Battle *b = r->b;
    play(r, (u8)o->sfx_tech);

    if (d6 & 0x80) {
        /* loc_2CEA: a damage technique -- odd rates hit everyone. */
        d6 = -(s8)d6;
        if (d6 & 1) {
            enemy_tech_all(r, o, a3, d6 & ~1);
            return;
        }
        int c = pick_char(r, o);
        CharData *a1 = chr(r, c);
        BObj *a2 = obj_of_char(b, c);
        if (enemy_tech_roll(r, a3, a1, a2) >= 0) {
            enemy_tech_strike(r, a1, a2, d6);
        }
        o->state = 3;
        return;
    }

    switch (d6) {
        case ENEMY_TECH_ALL: {
            enemy_tech_all(r, o, a3, d6);
            return;
        }
        case ENEMY_TECH_PARALYSE: {
            /* EnemyTechnique_Paralyze */
            int c = pick_char(r, o);
            CharData *a1 = chr(r, c);
            BObj *a2 = obj_of_char(b, c);
            if ((a1->status & 0xFF) != 0) {
                o->state = 2;
                play(r, (u8)o->sfx_attack);
                enemy_strike(r, a3, a1, a2);
                return;
            }
            if (enemy_tech_roll(r, a3, a1, a2) >= 0) {
                int d1 = paralysis_turns(r);
                a1->status = (u16)((a1->status & 0xFF00) | d1);
                b->battle_script = 0x1221;
            }
            o->state = 3;
            return;
        }
        case ENEMY_TECH_POISON: {
            /* loc_2D7C: poison */
            int c = pick_char(r, o);
            CharData *a1 = chr(r, c);
            BObj *a2 = obj_of_char(b, c);
            if (a1->status & ST_POISON) {
                o->state = 2;
                play(r, (u8)o->sfx_attack);
                enemy_strike(r, a3, a1, a2);
                return;
            }
            if (enemy_tech_roll(r, a3, a1, a2) >= 0) {
                a1->status |= ST_POISON;
                b->battle_script = 0x121D;
            }
            o->state = 3;
            return;
        }
        case ENEMY_TECH_SLEEP: {
            /* loc_2DA4: sleep */
            int c = pick_char(r, o);
            CharData *a1 = chr(r, c);
            BObj *a2 = obj_of_char(b, c);
            if (a1->status & ST_SLEEP) {
                o->state = 2;
                play(r, (u8)o->sfx_attack);
                enemy_strike(r, a3, a1, a2);
                return;
            }
            if (enemy_tech_roll(r, a3, a1, a2) >= 0) {
                a1->status |= ST_SLEEP;
                b->battle_script = 0x1201;
            }
            o->state = 3;
            return;
        }
        case ENEMY_TECH_DRAIN: {
            /* loc_2DCC: HP drained into the enemy. */
            int c = pick_char(r, o);
            CharData *a1 = chr(r, c);
            BObj *a2 = obj_of_char(b, c);
            if (enemy_tech_roll(r, a3, a1, a2) >= 0) {
                int id = b->group_id[(o->who != 0) ? 1 : 0];
                int rate = 6;
                if (id == 4) {
                    rate = 2;
                } else if (id == 5) {
                    rate = 4;
                }
                u16 d0 = enemy_tech_strike(r, a1, a2, rate);
                if (d0 != 0) {
                    a3->hp = (u16)(a3->hp + d0);
                    if (a3->hp > a3->max_hp) {
                        a3->hp = a3->max_hp;
                    }
                    note_damage(b, o->who, (u16)(d0 | 0x4000));
                }
            }
            o->state = 3;
            return;
        }
        case ENEMY_TECH_ARMY_EYE: {
            /* loc_2E24: the Army Eyes' technique (formation $101, three
             * of them, 3000 HP): $CC92 counts every use, and the ninth --
             * round three -- carries the party to Gaira at $50,$2B0 with
             * $1219 and `move.w #$A00, (event_flags).w`, loc_7E02 case $A
             * on arrival (game.c, from to_level_change). */
            b->cc92++;
            if (b->cc92 < 9) {
                enemy_attack(r, o, a3);
                return;
            }
            o->target = 0;
            o->pos = 0;
            for (int i = 0; i <= r->gd->party_members_num; i++) {
                show_char(r, r->gd->party_member_id[i]);
            }
            b->battle_script = 0x1219;
            b->main_routine  = 2;
            r->ws->event_routine = 2;
            o->state = 3;
            r->gd->level_index = 0x41;      /* LevelID_Gaira */
            r->gd->level_y     = 0x2B0;
            r->gd->level_x     = 0x50;
            b->to_level_change = true;
            return;
        }
        case ENEMY_TECH_DARK_FORCE: {
            /* loc_2E82: Dark Force's side effects on the slowest living
             * character not already marked (cmp.w $14(a1), d3 / bcs:
             * anyone faster than the pick so far is passed over; a tie
             * goes to the later slot). */
            d6 = 0x5A;
            u16 roll = rnd(r);
            if ((roll & 3) == 0) {
                enemy_tech_all(r, o, a3, d6);
                return;
            }
            int best = -1;
            int who  = -1;
            for (int i = 0; i <= r->gd->party_members_num; i++) {
                int c = r->gd->party_member_id[i];
                CharData *a1 = chr(r, c);
                BObj *a2 = show_char(r, c);
                bool was = (a2->f & F_SHOWN) != 0;
                a2->f &= (u8)~F_SHOWN;
                if (!was || (a1->status & 0x10)) {
                    continue;
                }
                if (best >= 0 && (u16)best < a1->agility) {
                    continue;
                }
                best = a1->agility;
                who  = c;
            }
            int d5 = (roll >> 8) & 7;
            if (best < 0 || d5 == 0 || d5 == 7) {
                enemy_tech_all(r, o, a3, d6);
                return;
            }
            CharData *a1 = chr(r, who);
            BObj *a2 = show_char(r, who);
            a1->status = (u16)((a1->status & 0xFF00) | 0x10 | d5);
            a2->f |= F_SHOWN;
            o->target = (s16)who;
            r->ws->character_index = (u16)who;
            a2->x = o->x;
            o->pos = 0;
            b->battle_script = 0x1223;
            o->state = 2;
            return;
        }
        case ENEMY_TECH_HEAL_ALLY: {
            /* loc_2F10: 20 HP to an ally. */
            int n = pick_enemy_ally(r, o);
            if (n >= 0) {
                EnemyStats *a1 = &b->stat[n];
                BObj *a2 = obj_of_enemy(b, n);
                /* loc_2C28 reads the target's +$16, which for an enemy
                 * is an unused word: luck 0. */
                if (enemy_tech_roll_luck(r, a3, 0, a2) >= 0) {
                    u16 d1 = (u16)(a1->hp + 0x14);
                    if (d1 >= a1->max_hp) {
                        d1 = a1->max_hp;
                    }
                    a1->hp = d1;
                    note_damage(b, o->who, 0x14 | 0x4000);
                }
            }
            o->state = 3;
            return;
        }
        case ENEMY_TECH_HEAL_ALLY_FULL: {
            /* loc_2F44: an ally healed in full. */
            int n = pick_enemy_ally(r, o);
            if (n >= 0) {
                EnemyStats *a1 = &b->stat[n];
                BObj *a2 = obj_of_enemy(b, n);
                if (enemy_tech_roll_luck(r, a3, 0, a2) >= 0) {
                    a1->hp = a1->max_hp;
                }
            }
            o->state = 3;
            return;
        }
        case ENEMY_TECH_STRIKE: {
            enemy_tech_strike_one(r, o, a3);
            return;
        }
        case ENEMY_TECH_CLONE: {
            /* loc_2F6C: the enemy copies itself into the slot before it
             * (F_ALT set) or, failing that, the one after (F_ALT clear);
             * with neither free, loc_2A98's attack. */
            int n = (obj_index(b, o) - BOBJ_ENEMIES) / 2;
            o->f |= F_ALT;
            int to = enemy_clone_into(r, o, a3, n - 1);
            if (to < 0) {
                o->f &= (u8)~F_ALT;
                to = enemy_clone_into(r, o, a3, n + 1);
            }
            if (to < 0) {
                enemy_attack(r, o, a3);
                return;
            }
            /* loc_2F92: a1 and a2 are the copy's; its +$16 is the
             * caster's, the unused word (luck 0, as loc_2F10's). A miss
             * marks the copy, and ObjEnmBattle_Dead takes it off again
             * when the script's hit lands on it. */
            enemy_tech_roll_luck(r, a3, 0, obj_of_enemy(b, to));
            o->pos   = 0;
            o->state = 3;
            return;
        }
        case ENEMY_TECH_STRIKE_B: {
            enemy_tech_strike_one(r, o, a3);
            return;
        }
        case ENEMY_TECH_EMPTY_TP: {
            int c = pick_char(r, o);
            CharData *a1 = chr(r, c);
            BObj *a2 = obj_of_char(b, c);
            if (enemy_tech_roll(r, a3, a1, a2) >= 0) {
                a1->tp = 0;
            }
            o->state = 3;
            return;
        }
        case ENEMY_TECH_ATTACK_DOWN: {
            int c = pick_char(r, o);
            CharData *a1 = chr(r, c);
            BObj *a2 = obj_of_char(b, c);
            if (enemy_tech_roll(r, a3, a1, a2) >= 0) {
                b->battle_script = 0x1220;
                a1->attack_base = (a1->attack_base >= 0x14)
                                ? (u16)(a1->attack_base - 0x14) : 0;
                a1->attack = (a1->attack >= 0x14)
                           ? (u16)(a1->attack - 0x14) : 0;
            }
            o->state = 3;
            return;
        }
        case ENEMY_TECH_DEFENSE_ZERO: {
            int c = pick_char(r, o);
            CharData *a1 = chr(r, c);
            BObj *a2 = obj_of_char(b, c);
            if (enemy_tech_roll(r, a3, a1, a2) >= 0) {
                a1->defense = 0;
                b->battle_script = 0x121E;
            }
            o->state = 3;
            return;
        }
        case ENEMY_TECH_DEFENSE_DOWN: {
            enemy_tech_defense_down(r, o, a3, 0x121E);
            return;
        }
        case ENEMY_TECH_DEFENSE_DOWN_B: {
            enemy_tech_defense_down(r, o, a3, 0x121F);
            return;
        }
        case ENEMY_TECH_KILL: {
            int c = pick_char(r, o);
            CharData *a1 = chr(r, c);
            BObj *a2 = obj_of_char(b, c);
            if (enemy_tech_roll(r, a3, a1, a2) >= 0) {
                a1->hp = 0;
                a2->f |= F_KILLED;
            }
            o->state = 3;
            return;
        }
        default: {
            enemy_attack(r, o, a3);
            return;
        }
    }
}

static bool enemy_idles(u16 word0)
{
    return (word0 & 8) || (word0 & 7) == 0;
}

/* loc_29A4 */
static void enemy_main(Run *r, BObj *o)
{
    Battle     *b  = r->b;
    EnemyStats *a3 = enemy_stat_of(b, o);
    u16 d0 = a3->word0;

    if (o->f & F_HIT) {
        o->r = R_SCREEN;
        if (--o->blink < 0) {
            if (o->f & F_KILLED) {
                b->group_count[(o->who != 0) ? 1 : 0]--;
                o->id = 0;
                o->state = 4;
                a3->word0 &= 0xE0;
                play(r, SFX_ENEMY_KILLED);
            }
            a3->word0 &= 0xFFF3;
            o->f = 0;
            b->cc06--;
            return;
        }
        if (enemy_idles(d0)) {
            animate(r, o, anim_table(ANIM_IDLE));
        }
        if (!(o->f & F_MISS) && (o->blink & 0xF) < 8) {
            o->r = R_SCREEN | R_HIDDEN;
        }
        return;
    }
    if (!(o->f & F_ACTING)) {
        if (enemy_idles(d0)) {
            animate(r, o, anim_table(ANIM_IDLE));
        }
        return;
    }
    /* loc_2A58: the enemy's turn. */
    b->effect_sound  = 0x80;
    b->command_used  = 0;
    if ((d0 & 7) != 0) {
        a3->word0 = (u16)((a3->word0 & 0xFF00) | ((a3->word0 - 1) & 0xFF));
        b->cc06 = 0;
        o->f &= (u8)~F_ACTING;
        return;
    }
    int d6 = a3->technique;
    if (d6 != 0) {
        int d2 = a3->tech_rate;
        if (d2 == 0xFF || d2 > (rnd(r) & 0xFF)) {
            enemy_technique(r, o, a3, d6);
            return;
        }
    }
    enemy_attack(r, o, a3);
}

static void run_enemy(Run *r, BObj *o)
{
    switch (o->state) {
        case 0: {
            o->r  = R_SCREEN;
            o->hx = o->x;
            o->hy = o->y;
            o->pos = 0;
            o->state = 1;
            break;
        }
        case 1: {
            enemy_main(r, o);
            break;
        }
        case 2: {
            animate(r, o, anim_table(ANIM_ENEMY_ATTACK));
            if (!(o->f & F_ACTING)) {
                o->state = 1;
            }
            break;
        }
        case 3: {
            animate(r, o, anim_table(ANIM_ENEMY_TECH));
            if (!(o->f & F_ACTING)) {
                o->state = 1;
            }
            break;
        }
        default: {
            /* ObjEnmBattle_Dead */
            o->r = R_HIDDEN;
            if (!(o->f & F_HIT)) {
                break;
            }
            if (o->f & F_MISS) {
                o->id = 0;
                o->state = 4;
                o->f = 0;
                r->b->cc06--;
                break;
            }
            o->r = R_SCREEN;
            r->b->group_count[(o->who != 0) ? 1 : 0]++;
            o->state = 1;
            g_debug_joined++;
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The effect objects                                                 */
/* ------------------------------------------------------------------ */

/* loc_289E ($B), loc_28F2 ($C): a weapon's or technique's effect over
 * loc_27364; loc_292A ($D): the cast over loc_27226. */
static void run_effect_b(Run *r, BObj *o, bool always_front)
{
    if (o->state == 0) {
        o->r = R_SCREEN;
        if (always_front) {
            o->r = R_SCREEN | R_FRONT;
        } else if (r->b->command_used != 0) {
            int d1 = o->anim;
            if (d1 != 0x24 && (d1 < 0x22 || d1 >= 0x2F)) {
                o->r = R_SCREEN | R_FRONT;
            }
        }
        o->pos = 0;
        if (always_front) {
            o->dur = 1;
        }
        o->state = 1;
    }
    animate(r, o, anim_table(ANIM_EFFECT));
}

static void run_effect_d(Run *r, BObj *o)
{
    if (o->state == 0) {
        o->r = R_SCREEN;
        o->pos = 0;
        o->dur = 1;
        o->state = 1;
    }
    animate(r, o, anim_table(ANIM_EFFECT2));
}

/* loc_3166 ($10), loc_31BC ($11): an enemy's overlays, drawn with its
 * own map. */
static void run_enemy_child(Run *r, BObj *o, bool second)
{
    if (o->state == 0) {
        BObj *p = parent_of(r->b, o);
        o->r    = R_SCREEN;
        o->y    = second ? p->d38 : p->d12;
        o->tile = p->tile;
        o->anim = p->anim;
        if (second && (p->f & F_ALT)) {
            p->f &= (u8)~F_ALT;
            o->anim++;
        }
        o->map   = p->map;
        o->pos   = 0;
        o->timer = 0;
        o->dur   = 1;
        o->state = 1;
    }
    animate(r, o, anim_table(second ? ANIM_ENEMY_CHILD_B : ANIM_ENEMY_CHILD_A));
}

/* loc_321E ($12) over loc_26414, loc_3250 ($13) over loc_26314. */
static void run_effect_12(Run *r, BObj *o, bool table_b)
{
    if (o->state == 0) {
        o->r = R_SCREEN;
        o->pos = 0;
        o->state = 1;
    }
    animate(r, o, anim_table(table_b ? ANIM_ENEMY_CHILD_B : ANIM_ENEMY_CHILD_A));
}

/* ------------------------------------------------------------------ */
/* RunObjects                                                         */
/* ------------------------------------------------------------------ */

void bobj_run(Battle *b, GameData *gd, WinSys *ws, Rng *rng, TileSet *vram,
              Video *video, void (*sound)(void *ctx, u8 id), void *ctx)
{
    Run r;
    r.b     = b;
    r.gd    = gd;
    r.ws    = ws;
    r.rng   = rng;
    r.vram  = vram;
    r.video = video;
    r.sound = sound;
    r.ctx   = ctx;

    for (int i = BOBJ_CHARS; i < BOBJ_COUNT; i++) {
        BObj *o = &b->obj[i];
        if (o->id == 0) {
            continue;
        }
        switch (o->id) {
            case OBJ_CHAR: {
                run_char(&r, o);
                break;
            }
            case OBJ_FX_B: {
                run_effect_b(&r, o, false);
                break;
            }
            case OBJ_FX_C: {
                run_effect_b(&r, o, true);
                break;
            }
            case OBJ_FX_D: {
                run_effect_d(&r, o);
                break;
            }
            case OBJ_ENEMY: {
                run_enemy(&r, o);
                break;
            }
            case OBJ_ENM_CHILD_A: {
                run_enemy_child(&r, o, false);
                break;
            }
            case OBJ_ENM_CHILD_B: {
                run_enemy_child(&r, o, true);
                break;
            }
            case OBJ_FX_12: {
                run_effect_12(&r, o, true);
                break;
            }
            case OBJ_FX_13: {
                run_effect_12(&r, o, false);
                break;
            }
            default: {
                break;
            }
        }
        /* RunObject's tail: a destroyed object is cleared (loc_6D38). */
        if (o->r & R_DESTROY) {
            memset(o, 0, sizeof *o);
        }
    }
}
