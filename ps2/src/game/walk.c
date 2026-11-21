#include "walk.h"
#include "rom_ids.h"

#include "../core/assetfs.h"
#include "../core/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Demo_HouseToTower / Demo_TowerToHouse (ps2.asm:5416, 5440). The leading
 * zero is one idle read. */
const u8 DEMO_HOUSE_TO_TOWER[] = {
    0x00, 0x02,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04,
    0x01, 0x01, 0x01,
    0x00, DEMO_END
};

/* Demo_TylerSpcToTower (DemoScriptPtrs entry 8), demo 8: from Paseo's
 * $3D0,$170, where loc_CE5E puts the party after Tyler's spaceship, to
 * the Central Tower's door. */
const u8 DEMO_TYLER_SPC_TO_TOWER[] = {
    0x00, 0x00, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x01, 0x01, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x01, 0x01, 0x01,
    0x01, 0x01, DEMO_END
};

/* Demo_CloneLabToTower (ps2.asm:5538), demo 7: after Nei, from the clone
 * lab to the Central Tower. */
const u8 DEMO_CLONE_LAB_TO_TOWER[] = {
    0x00, 0x00,
    0x08, 0x08, 0x08,
    0x01, 0x01,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x00, DEMO_END
};

/* Demo_ClimatrolToCloneLab (ps2.asm:5518), demo 6: from Paseo's west
 * edge at 2,23 (ObjType_Events $2A) six steps right, five up, and the
 * clone lab's door at the end. */
const u8 DEMO_CLIMATROL_TO_CLONE_LAB[] = {
    0x00, 0x00,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x08, 0x08, 0x08, 0x08, 0x08,
    0x01, 0x00, DEMO_END
};

/* Demo_TeimScene and Demo_AfterTeimDeath (ps2.asm:5464), demos 2 and 3:
 * into the tube towards Darum, and on past where he stood. Both end in
 * $FE, which hands the pad back and moves the scene on. $44 is left with
 * the camera forced left. */
const u8 DEMO_TEIM_SCENE[] = {
    0x00, 0x04, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x00, DEMO_STOP
};

const u8 DEMO_AFTER_TEIM_DEATH[] = {
    0x00, 0x00, 0x00, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, DEMO_STOP
};

/* Demo_DynamiteUsed and Demo_KeyUsed (ps2.asm:5492, 5504), demos 4 and
 * 5: after the item is used on a door. Three idle reads, then back away
 * -- down three, right, up -- for the dynamite; seven idle reads for a
 * key or card. Both end in $FD: the door SFX, and routine 2 into the
 * level's first object, which is the door. */
const u8 DEMO_DYNAMITE_USED[] = {
    0x00, 0x00, 0x00, 0x02, 0x02, 0x02, 0x08, 0x01, 0x00, DEMO_DOOR
};

const u8 DEMO_KEY_USED[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, DEMO_DOOR
};

/* The two walks after Mother Brain (DemoScriptPtrs entries 9 and 10,
 * loc_3AC4 and loc_3AD8). The first is two walks in one script: three
 * steps back down the corridor, a stop byte, then eleven north with the
 * camera forced up ($11 is up plus bit 4) and another stop byte. A stop
 * byte leaves the read position on itself, so the aftermath steps past it
 * with walk_resume_demo. The second is thirteen north with the camera
 * forced up, four more plain, three idle reads, and a stop. */
const u8 DEMO_AFTER_MOTHER_BRAIN[] = {
    0x00, 0x02, 0x02, 0x02, 0x00, DEMO_STOP,
    0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x00, DEMO_STOP
};

const u8 DEMO_TO_THE_EARTHMEN[] = {
    0x00, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, DEMO_STOP
};

const u8 DEMO_TOWER_TO_HOUSE[] = {
    0x00, 0x02, 0x02, 0x02,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08,
    0x01,
    0x00, DEMO_END
};

#define STEP_FRAMES   0x0F
#define ANIM_FRAMES   7
#define SPRITE_BIAS   0x80

/* Where on screen the camera starts to follow (ObjCL_Main, loc_3956). */
#define FOLLOW_UP     0x0C8
#define FOLLOW_DOWN   0x118
#define FOLLOW_LEFT   0x0D8
#define FOLLOW_RIGHT  0x168

#define SCREEN_PX_W   0x140
#define SCREEN_PX_H   0x0E0

static bool load_table(u8 table[WALK_BLOCKS_MAX], const char *path)
{
    const char *cursor = (const char *)assetfs_get(path, NULL);
    if (cursor == NULL) {
        fprintf(stderr, "walk: cannot open %s\n", path);
        return false;
    }

    memset(table, 0, WALK_BLOCKS_MAX);

    char line[128];
    while (assetfs_next_line(&cursor, line, sizeof line)) {
        unsigned block;
        char solid[8], special[8];

        if (line[0] == '#'
            || sscanf(line, "%x %4s %4s", &block, solid, special) != 3
            || block >= WALK_BLOCKS_MAX) {
            continue;
        }

        u8 byte = 0;
        for (int q = 0; q < 4; q++) {
            if (solid[q] == '#') {
                byte |= (u8)(1 << q);
            }
            if (special[q] == '#') {
                byte |= (u8)(0x10 << q);
            }
        }
        table[block] = byte;
    }
    return true;
}

bool walk_load_collision(Walk *w, const char *path)
{
    return load_table(w->attr, path);
}

bool walk_load_scooter_collision(Walk *w, const char *path)
{
    w->have_scooter_table = load_table(w->attr_scooter, path);
    return w->have_scooter_table;
}

bool walk_load_doors(Walk *w, const char *path)
{
    w->door_count = 0;

    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return false;
    }
    const JsonValue *list = json_get(root, "entries");
    if (json_length(list) > WALK_DOORS_MAX) {
        fprintf(stderr, "walk: %s has %d doors, the first %d kept\n", path,
                json_length(list), WALK_DOORS_MAX);
    }
    for (const JsonValue *e = (list != NULL) ? list->child : NULL;
         e != NULL && w->door_count < WALK_DOORS_MAX; e = e->next) {
        WalkDoor *d = &w->door[w->door_count++];
        d->y = (u8)json_int(e, "y", 0);
        d->x = (u8)json_int(e, "x", 0);
        if (json_get(e, "to_level") != NULL) {
            d->kind = 0;
            d->a = (u8)json_int(e, "to_level", 0);
            d->b = (u8)json_int(e, "to_y", 0);
            d->c = (u8)json_int(e, "to_x", 0);
        } else {
            d->kind = (u8)json_int(e, "building", 0);
            d->a = (u8)json_int(e, "portrait", 0);
            d->b = (u8)json_int(e, "f764", 0);
            d->c = (u8)json_int(e, "f766", 0);
        }
    }
    json_free(root);
    return true;
}

bool walk_load_exits(WalkExits *ex, const char *path)
{
    ex->count = 0;

    JsonValue *root = json_parse_file(path);
    if (root == NULL) {
        return false;
    }
    const JsonValue *list = json_get(root, "entries");
    for (const JsonValue *e = (list != NULL) ? list->child : NULL;
         e != NULL && ex->count < WALK_EXITS_MAX; e = e->next) {
        ex->level[ex->count] = (u8)json_int(e, "to_level", 0);
        ex->y[ex->count]     = (u8)json_int(e, "to_y", 0);
        ex->x[ex->count]     = (u8)json_int(e, "to_x", 0);
        ex->count++;
    }
    json_free(root);
    return true;
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

/* loc_6DE8: the attribute byte of the block under a point, and which of
 * its quadrant bits the point falls in, from `table` ($FFFFF72E). */
static u8 attr_in(const Walk *w, const u8 *table, LayerId layer, int x,
                  int y, int *quadrant, u8 *block_out)
{
    x = wrap(x, w->width);
    y = wrap(y, w->height);

    int cols  = w->width >> 5;
    int index = (y >> 5) * cols + (x >> 5);
    u8  block = w->level->map[layer][index];

    *quadrant = ((y >> 3) & 2) + ((x >> 4) & 1);
    if (block_out != NULL) {
        *block_out = block;
    }
    return table[block];
}

/* The same through the level's own table, which is what $F72E holds
 * whenever nobody is riding the scooter. */
static u8 attr_at(const Walk *w, LayerId layer, int x, int y, int *quadrant,
                  u8 *block_out)
{
    return attr_in(w, w->attr, layer, x, y, quadrant, block_out);
}

/* loc_6FA6: arrival cell to pixels. */
static WalkResult change_level(Walk *w, int level, int y16, int x16)
{
    w->go_level = level;
    w->end_y    = (s16)(y16 << 4);
    w->end_x    = (s16)(x16 << 4);
    w->go_sfx   = SFX_LEVEL_CHANGED;     /* loc_6F56, loc_6E4E */
    w->go_scene = 0;
    return WALK_LEVEL_CHANGE;
}

/* A fall to a fixed spot (loc_6FCA's two Crevice cases), in pixels. */
static WalkResult fall_to(Walk *w, int level, int y, int x)
{
    w->go_level = level;
    w->end_y    = (s16)y;
    w->end_x    = (s16)x;
    w->go_sfx   = SFX_FELL_IN_HOLE;      /* loc_6FCA */
    w->go_scene = 0;
    return WALK_LEVEL_CHANGE;
}

/* loc_7082: a hole. loc_718A is indexed by level_index - $4E, clamped
 * at 0, so every level below Ikuto B4 takes the first entry: one level
 * down, same spot. Each entry is a level delta and a y and x delta in
 * 32px blocks from the cell underfoot. */
static WalkResult fall_through(Walk *w)
{
    static const s8 HOLE[23][3] = {
        { -1,  0,  0 }, { -1, 12,  0 }, { -1,  0, 16 }, { -1,  0, 11 },
        { -1,  2,  0 }, { -1,  0,  0 }, { -1,  1, -2 }, { -1,  3, 13 },
        {  2,  0,  0 }, {  2,  0,  0 }, { -3,  0,  0 }, { -3,  0,  0 },
        { -3,  0,-13 }, { -1, -2, 16 }, { -1,  3, -3 }, {  2,  0,  0 },
        {  2,  0,  0 }, { -3,  0,  0 }, { -3,  0,  0 }, { -3,  0,  0 },
        { -1,  0,  0 }, { -1,  0,  0 }, { -1,-16,  0 }
    };
    int i = (w->level->index >= 0x4E) ? w->level->index - 0x4E : 0;
    if (i > 22) {
        i = 22;
    }
    return fall_to(w, w->level->index + HOLE[i][0],
                   (w->y & 0xFFF0) + HOLE[i][1] * 32,
                   (w->x & 0xFFF0) + HOLE[i][2] * 32);
}

/* loc_7108: only on arriving by a step; the rest is the caller's. */
static WalkResult dangerous_floor(Walk *w)
{
    if (w->hurt_pending) {
        w->hurt_pending = false;
        w->hurt_now     = true;
    }
    return WALK_RUNNING;
}

#define DOOR_ROLF_HOUSE  0x37    /* Paseo: Rolf's own door            */
#define DOOR_PIATA_TOWER 0x3E    /* Piata: the tower in the square    */

/* loc_6E4E: look (px, py) up in the interaction list. Every outcome
 * blocks the step; the ones that lead somewhere also change the screen.
 *
 * loc_6EA6 sets aside two records with building 1 before anything reads
 * them as a building. Rolf's door, with Rolf at 0 HP, does not open: it
 * starts ObjType_TeimDarum 3 ("{char} is dead."). Piata's tower is shut
 * (moveq #-1, d4) until the Governor's debrief unlocks it ($C712); after that it goes to loc_6F1C
 * like any other door, its last three bytes a level and a cell --
 * ControlTowerGroundF, 46,41. */
static WalkResult interact(Walk *w, int px, int py)
{
    int cy = (u8)(py >> 4);
    int cx = (u8)(px >> 4);

    for (int i = 0; i < w->door_count; i++) {
        const WalkDoor *d = &w->door[i];
        if (d->y < cy || d->y > cy + 2 || d->x < cx || d->x > cx + 2) {
            continue;
        }

        bool tower = (d->kind == 1 && d->a == DOOR_PIATA_TOWER);
        if (d->kind == 1 && d->a == DOOR_ROLF_HOUSE && w->rolf_hp == 0) {
            w->go_event = EVENT_KIND_TEIM_DARUM | TEIM_DARUM_ROLF_DEAD;
            return WALK_EVENT;
        }
        if (tower && !w->piata_tower_unlocked) {
            return WALK_RUNNING;
        }

        if (d->kind != 0 && !tower) {
            w->go_building    = d->kind;
            w->go_portrait    = d->a;
            w->go_store_max   = d->b;
            w->go_store_index = d->c;
            w->end_y = (s16)(w->y & 0xFFF0);
            w->end_x = (s16)(w->x & 0xFFF0);
            return WALK_BUILDING;
        }

        if (w->level->index < 0x10 && d->a >= 0x10) {
            w->set_return   = true;
            w->return_level = w->level->index;
            w->return_y     = (s16)(w->y & 0xFFF0);
            w->return_x     = (s16)(w->x & 0xFFF0);
        }
        return change_level(w, d->a, d->b, d->c);
    }

    /* Nothing matched. Outdoors that is just a wall; in a dungeon it is
     * the way out, through the same table the town edges use. */
    int from = w->level->index - 4;
    if (w->level->index >= 0x10 && w->exits != NULL
        && from < w->exits->count) {
        return change_level(w, w->exits->level[from], w->exits->y[from],
                            w->exits->x[from]);
    }
    return WALK_RUNNING;
}

/* The approach to Mother Brain (loc_70D6, ps2.asm:10398): NoahF1's
 * corridor floor, block $33. Only with the leader at y $218 or above
 * (cell 33 and north) and before the fight (the mother_brain_fought
 * flag, $C747): the same level again with the party at x $210 on the
 * leader's row, and event_flags $E00 -- the arrival scene. No sound is
 * queued, and the interaction list is not consulted either way. */
static WalkResult approach_mother_brain(Walk *w)
{
    if (w->y > 0x218 || w->mother_brain_fought) {
        return WALK_RUNNING;
    }
    w->go_level = w->level->index;
    w->end_y    = (s16)(w->y & 0xFFF0);
    w->end_x    = 0x210;
    w->go_sfx   = 0;
    w->go_scene = SCENE_MOTHER_BRAIN_ARRIVAL;
    return WALK_LEVEL_CHANGE;
}

/* loc_6F56, run on the cell underfoot each time the character is at rest.
 * Only a special quadrant does anything. Then, by level type ($FFFFF710)
 * and the block id:
 *
 *   2  $53/$72: a town's edge, loc_28546
 *   3  $63: damage floor; $50-$62 in Crevice B1/ground: a hole
 *   5  $1A/$46/$77/$78: damage floor; $3A: a hole
 *   6  $33: the approach to Mother Brain (loc_70D6); $04: a hole
 *
 * and anything else goes to the interaction list (loc_7104 -> loc_6E4E),
 * which is where stairs are. A damage floor with nothing owed returns
 * without looking at the list. */
static WalkResult check_underfoot(Walk *w)
{
    LayerId layer = (w->level_type == 2) ? LAYER_B : w->coll_layer;
    int quadrant;
    u8  block;
    u8  attr = attr_at(w, layer, w->x, w->y, &quadrant, &block);

    if ((attr & (0x10 << quadrant)) == 0) {
        return WALK_RUNNING;
    }

    int level = w->level->index;
    switch (w->level_type) {
        case 2: {
            if (block == 0x53 || block == 0x72) {
                int from = level - 4;
                if (w->exits != NULL && from >= 0
                    && from < w->exits->count) {
                    return change_level(w, w->exits->level[from],
                                        w->exits->y[from],
                                        w->exits->x[from]);
                }
                return WALK_RUNNING;
            }
            break;
        }
        case 3: {
            if (block == 0x63) {
                return dangerous_floor(w);
            }
            if (block >= 0x50 && block <= 0x62) {
                if (level == 0x15) {
                    return fall_to(w, 0x14, 0xC0, 0x60);
                }
                if (level == 0x16) {
                    return (w->y >= 0x100) ? fall_to(w, 0x15, 0x220, 0x2C0)
                                           : fall_to(w, 0x15, 0xA0, 0x340);
                }
            }
            break;
        }
        case 5: {
            if (block == 0x1A || block == 0x46 || block == 0x77
                || block == 0x78) {
                return dangerous_floor(w);
            }
            if (block == 0x3A) {
                return fall_through(w);
            }
            break;
        }
        case 6: {
            if (block == 0x33) {
                return approach_mother_brain(w);
            }
            if (block == 0x04) {
                return fall_through(w);
            }
            break;
        }
        default: {
            break;
        }
    }
    return interact(w, w->x, w->y);
}

/* Sprites_ChkCanMove, for the party. A solid quadrant that is also
 * special goes to the interaction list (loc_6DB2) -- in a town only when
 * walking up, which is why a door is only ever entered from below. */
static bool can_move(Walk *w, int dx, int dy, WalkResult *result)
{
    int quadrant;
    int tx   = w->x + dx;
    int ty   = w->y + dy;
    u8  attr = attr_at(w, w->coll_layer, tx, ty, &quadrant, NULL);

    w->blocker = NULL;
    int dir = (dy < 0) ? 0 : (dy > 0) ? 1 : (dx < 0) ? 2 : 3;
    if ((attr & (1 << quadrant)) == 0) {
        /* The cell is walkable; is somebody standing in it?
         * (Level_ChkTargetInteract, called from loc_6D92.) */
        if (w->npcs != NULL) {
            w->blocker = npc_in_the_way(w->npcs, NULL, w->x, w->y, dir);
            /* The d6 the objects after the leader find (npc.h). */
            npc_note_leader_check(w->npcs, dir, true, w->blocker);
            if (w->blocker != NULL) {
                return false;
            }
        }
        return true;
    }
    if (w->npcs != NULL) {
        npc_note_leader_check(w->npcs, dir, false, NULL);
    }
    bool walking_up = (dy < 0);
    if ((attr & (0x10 << quadrant)) != 0
        && (w->level_type != 2 || walking_up)) {
        *result = interact(w, tx, ty);
    }
    return false;
}

bool walk_cell_open(void *walk, int x, int y)
{
    const Walk *w = walk;
    int quadrant;
    u8  attr = attr_at(w, w->coll_layer, x, y, &quadrant, NULL);
    return (attr & (1 << quadrant)) == 0;
}

int walk_party_positions(const Walk *w, s16 *xs, s16 *ys, int cap)
{
    int n = 0;
    if (n < cap) {
        xs[n] = w->x;
        ys[n] = w->y;
        n++;
    }
    for (int i = 0; i < w->follower_count && n < cap; i++) {
        xs[n] = w->follower[i].x;
        ys[n] = w->follower[i].y;
        n++;
    }
    return n;
}

static void update_screen_pos(Walk *w)
{
    int sx = w->x - w->cam_x;
    int sy = w->y - w->cam_y;

    if (sx < 0) {
        sx += w->width;
    }
    if (sy < 0) {
        sy += w->height;
    }
    w->screen_x = (s16)(sx + SPRITE_BIAS);
    w->screen_y = (s16)(sy + SPRITE_BIAS);
}

/* LoadLevelData, ps2.asm:13371: the camera starts with the character
 * $A0 from the left and $80 from the top, held inside the map when the
 * level does not wrap. */
static int camera_start(int pos, int lead, int extent, int screen, bool clamp)
{
    int cam = pos - lead;
    if (cam < 0) {
        cam += extent;
        if (clamp) {
            cam = 0;
        }
    }
    if (cam >= extent - screen && clamp) {
        cam = extent - screen;
    }
    return cam;
}

void walk_begin(Walk *w, const Level *level, int level_type,
                int x, int y, int facing, const u8 *demo, int followers)
{
    w->level        = level;
    w->width        = level->cols * 32;
    w->height       = level->rows * 32;
    w->level_type   = level_type;
    w->clamp_camera = (level->flags & 0x0F) != 0;
    /* LoadLevelData (13310): $A800, or $9000 when bits 4-6 of the
     * collision pointer's flag byte are set. Every dungeon sets them,
     * because its plane A is the backdrop overlay. */
    w->coll_layer   = (level->flags & 0x70) ? LAYER_B : LAYER_A;

    w->cam_x = (s16)camera_start(x, 0xA0, w->width, SCREEN_PX_W,
                                 w->clamp_camera);
    w->cam_y = (s16)camera_start(y, 0x80, w->height, SCREEN_PX_H,
                                 w->clamp_camera);

    /* loc_91AA-loc_91F2: plane A's camera starts where the main one
     * does, or at 0,0 over a backdrop. */
    w->parallax   = (level->extra_art & 0x80) != 0;
    w->backdrop_a = (level->flags & LEVEL_FLAG_A_BACKDROP) != 0;
    w->plane_a_x  = w->backdrop_a ? 0 : w->cam_x;
    w->plane_a_y  = w->backdrop_a ? 0 : w->cam_y;

    /* PS2_LEVELINFO=1: what the level screen was set up from, one line
     * per entry -- the flag byte, the extra-art byte, and what they made
     * of the cameras and the collision map. */
    if (getenv("PS2_LEVELINFO") != NULL) {
        fprintf(stderr, "level $%02X: flags $%02X extra_art $%02X  "
                        "parallax %s  plane A %s  collision plane %c\n",
                level->index, level->flags, level->extra_art,
                w->parallax ? "x2" : "x1",
                w->backdrop_a ? "backdrop" : "map",
                (w->coll_layer == LAYER_B) ? 'B' : 'A');
    }

    /* ObjCL_Init: the object sits at the centre of its 16px cell. Not
     * while the party rides the scooter onto the level: the object hides
     * before its init can run, and the scooter's copy (loc_4AE2) is what
     * moves it. */
    int in = w->riding ? 0 : 8;
    w->x  = (s16)(x + in);
    w->y  = (s16)(y + in);
    w->reinit = false;
    w->vx = 0;
    w->vy = 0;
    w->facing      = facing;
    w->frame       = facing;
    w->step_frames = 0;
    w->anim_timer  = 0;
    w->anim_index  = 0;

    w->hurt_pending = false;       /* ObjCL_Init: $30 starts clear */
    w->hurt_now     = false;
    w->demo_stopped = false;
    w->demo_door    = false;

    w->demo     = demo;
    w->demo_pos = 0;
    w->force    = 0;
    update_screen_pos(w);

    /* ObjCL_Init fills the whole trail with the start position, so the
     * party begins stacked on the leader and fans out as he walks. */
    for (int i = 0; i < WALK_TRAIL; i++) {
        w->trail[i][0] = w->x;
        w->trail[i][1] = w->y;
    }
    w->trail_pos = 0;

    w->follower_count = (followers < WALK_FOLLOWERS) ? followers
                                                     : WALK_FOLLOWERS;
    for (int i = 0; i < w->follower_count; i++) {
        WalkFollower *f = &w->follower[i];
        memset(f, 0, sizeof *f);
        f->x        = w->x;
        f->y        = w->y;
        f->facing   = facing;
        f->frame    = facing;
        f->screen_x = w->screen_x;
        f->screen_y = w->screen_y;
    }
}

/* loc_8C28 / loc_8C94 / loc_8D14 / loc_8D80, for one axis: a pixel a
 * frame, wrapping with the map or stopping at its edge. */
static int scroll_axis(int cam, int dir, int extent, int screen, bool clamp)
{
    cam += dir;
    if (cam < 0) {
        cam = clamp ? 0 : cam + extent;
    }
    if (clamp) {
        if (cam > extent - screen) {
            cam = extent - screen;
        }
    } else if (cam >= extent) {
        cam -= extent;
    }
    return cam;
}

/* The second half of loc_8C28 / loc_8C94 / loc_8D14 / loc_8D80: plane
 * A's camera after the main one took its step. `held` is the main camera
 * refusing to move -- already at the edge ($d4 = 1). On a plane-A
 * backdrop there is no wrap and no clamp, only the backdrop's own size
 * (`andi.w #$FF` / `#$1FF`). Every backdrop level is a clamped one, so
 * `held` means exactly "the main camera did not change". */
static int scroll_plane_a(const Walk *w, int cam, int dir, int extent,
                          int screen, int mask, bool held)
{
    if (w->parallax) {
        dir += dir;
    }
    if (!w->backdrop_a) {
        return scroll_axis(cam, dir, extent, screen, w->clamp_camera);
    }
    return held ? cam : ((cam + dir) & mask);
}

static bool try_step(Walk *w, int facing, int dx, int dy,
                     WalkResult *result)
{
    w->facing = facing;
    if (!can_move(w, dx * 16, dy * 16, result)) {
        return false;
    }
    w->vx = (s16)dx;
    w->vy = (s16)dy;
    return true;
}

static void move_camera(Walk *w, int scroll_y, int scroll_x);

static const u8 CYCLE[4] = { 0, 1, 0, 2 };      /* Level_SpriteMappingsArray */

/* The part of ObjCL_Main that runs every frame of a step (loc_390E). */
static void stepping(Walk *w)
{
    u8 force = w->force;

    /* Record where the leader is, before this frame's velocity lands. */
    w->trail[w->trail_pos][0] = w->x;
    w->trail[w->trail_pos][1] = w->y;
    w->trail_pos = (w->trail_pos + 1) & (WALK_TRAIL - 1);

    w->anim_timer--;
    if (w->anim_timer < 0) {
        w->anim_timer = ANIM_FRAMES;
        w->frame      = w->facing + CYCLE[w->anim_index];
        w->anim_index = (w->anim_index + 1) & 3;
    }

    int scroll_y = 0;
    int scroll_x = 0;

    if ((force & 0x10)
        || (w->screen_y <= FOLLOW_UP && w->facing == FACE_UP)) {
        scroll_y = -1;
    }
    if (w->screen_y >= FOLLOW_DOWN && w->facing == FACE_DOWN) {
        scroll_y = 1;
    }
    if ((force & 0x40)
        || (w->screen_x <= FOLLOW_LEFT && w->facing == FACE_LEFT)) {
        scroll_x = -1;
    }
    if (w->screen_x >= FOLLOW_RIGHT && w->facing == FACE_RIGHT) {
        scroll_x = 1;
    }

    move_camera(w, scroll_y, scroll_x);
}

/* loc_8BBA: y, then x; each moves the main camera, then plane A's. The
 * party asks for a pixel a frame, the scooter for two. */
static void move_camera(Walk *w, int scroll_y, int scroll_x)
{
    if (scroll_y != 0) {
        s16 was  = w->cam_y;
        w->cam_y = (s16)scroll_axis(w->cam_y, scroll_y, w->height,
                                    SCREEN_PX_H, w->clamp_camera);
        w->plane_a_y = (s16)scroll_plane_a(w, w->plane_a_y, scroll_y,
                                           w->height, SCREEN_PX_H, 0xFF,
                                           w->cam_y == was);
    }
    if (scroll_x != 0) {
        s16 was  = w->cam_x;
        w->cam_x = (s16)scroll_axis(w->cam_x, scroll_x, w->width,
                                    SCREEN_PX_W, w->clamp_camera);
        w->plane_a_x = (s16)scroll_plane_a(w, w->plane_a_x, scroll_x,
                                           w->width, SCREEN_PX_W, 0x1FF,
                                           w->cam_x == was);
    }
}

static void follower_screen_pos(const Walk *w, WalkFollower *f)
{
    int sx = f->x - w->cam_x;
    int sy = f->y - w->cam_y;

    if (sx < 0) {
        sx += w->width;
    }
    if (sy < 0) {
        sy += w->height;
    }
    f->screen_x = (s16)(sx + SPRITE_BIAS);
    f->screen_y = (s16)(sy + SPRITE_BIAS);
}

/* ObjFlwngChar_Main. The objects run after the leader's, so they see
 * this frame's trail index, velocity and step counter. */
static void run_followers(Walk *w)
{
    for (int i = 0; i < w->follower_count; i++) {
        WalkFollower *f = &w->follower[i];
        int at = (w->trail_pos - WALK_TRAIL_LAG * (i + 1))
               & (WALK_TRAIL - 1);
        s16 tx = w->trail[at][0];
        s16 ty = w->trail[at][1];

        if (ty != f->y) {
            f->facing = ((u16)ty > (u16)f->y) ? FACE_DOWN : FACE_UP;
        } else if (tx != f->x) {
            f->facing = ((u16)tx > (u16)f->x) ? FACE_RIGHT : FACE_LEFT;
        }
        f->x = tx;
        f->y = ty;

        if (w->vx == 0 && w->vy == 0) {
            f->frame = f->facing;
        } else if (w->step_frames != 0) {
            f->anim_timer--;
            if (f->anim_timer < 0) {
                f->anim_timer = ANIM_FRAMES;
                f->frame      = f->facing + CYCLE[f->anim_index];
                f->anim_index = (f->anim_index + 1) & 3;
            }
        }
        follower_screen_pos(w, f);
    }
}

/* RunObject's tail (loc_3E4): velocity, wrap, screen position. */
static void run_object(Walk *w)
{
    w->x = (s16)wrap(w->x + w->vx, w->width);
    w->y = (s16)wrap(w->y + w->vy, w->height);
    update_screen_pos(w);
    run_followers(w);
}

void walk_start_demo(Walk *w, const u8 *demo)
{
    w->demo     = demo;
    w->demo_pos = 0;
}

/* The same script again from one byte past where its stop byte was read
 * (`addq.w #1, (demo_input_index).w` with demo_flag set again, loc_E288). */
void walk_resume_demo(Walk *w, const u8 *demo)
{
    w->demo = demo;
    w->demo_pos++;
}

/* ------------------------------------------------------------------ */
/* The jet scooter: ObjJetSctr_Main (ps2.asm:6801)                     */
/* ------------------------------------------------------------------ */

#define SCOOTER_STEP_FRAMES 7       /* $28: 8 frames of 2px, a 16px step */
#define SCOOTER_SPEED       2
#define SCOOTER_ANIM_FRAMES 3       /* $26 */

/* loc_4A7A: how near an edge, in sprite space, the scooter has to be
 * before the camera follows it -- nearer the middle than on foot. */
#define SCOOTER_FOLLOW_UP    0x0E0
#define SCOOTER_FOLLOW_DOWN  0x100
#define SCOOTER_FOLLOW_LEFT  0x100
#define SCOOTER_FOLLOW_RIGHT 0x140

static Npc *scooter_of(Walk *w)
{
    return (w->npcs != NULL) ? npc_jet_scooter(w->npcs) : NULL;
}

/* JetScooter_ChkCanMove (ps2.asm:10131), a probe (dy, dx) from the
 * scooter: loc_6DE8 through loc_29E16, the solid bit, and for a solid
 * quadrant that is also special loc_6E4E, the interaction list, exactly
 * as a party member's step would reach it but in any direction and on
 * any level type. No Level_ChkTargetInteract: nobody stands in its way.
 * Every outcome of the list blocks the probe (the building path returns
 * with whatever `move.w d0, (level_x_pos)` left in the flags, which is
 * non-zero anywhere but the map's first column). */
static bool scooter_blocked(Walk *w, const Npc *s, int dy, int dx,
                            WalkResult *result)
{
    int px = wrap(s->x + dx, w->width);
    int py = wrap(s->y + dy, w->height);
    int quadrant;
    u8  attr = attr_in(w, w->attr_scooter, w->coll_layer, px, py,
                       &quadrant, NULL);

    if ((attr & (1 << quadrant)) == 0) {
        return false;
    }
    if ((attr & (0x10 << quadrant)) != 0) {
        *result = interact(w, px, py);
    }
    return true;
}

/* loc_4AF4: the hover, two frames of the facing, the next every four
 * frames -- moving or not. The frame is picked from $32 before $26 has
 * counted it on. */
static void scooter_anim(Npc *s)
{
    int d0 = s->anim_index;
    s->anim--;
    if (s->anim < 0) {
        s->anim = SCOOTER_ANIM_FRAMES;
        s->anim_index++;
    }
    s->frame = s->facing + (d0 & 1);
}

/* loc_4AE2: the leader's position and facing are the scooter's, as they
 * stand before RunObject adds this frame's velocity. */
static void scooter_copy(Walk *w, const Npc *s)
{
    w->x      = s->x;
    w->y      = s->y;
    w->facing = s->facing;
    update_screen_pos(w);
}

/* One direction of ObjJetSctr_Main: face it, then the two probes along
 * the edge the scooter is about to cross, the second only if the first
 * is clear. */
static bool scooter_try(Walk *w, Npc *s, int facing, int dy1, int dx1,
                        int dy2, int dx2, int vx, int vy, WalkResult *result)
{
    s->facing = facing;
    if (scooter_blocked(w, s, dy1, dx1, result)
        || scooter_blocked(w, s, dy2, dx2, result)) {
        return false;
    }
    s->vx = (s16)vx;
    s->vy = (s16)vy;
    return true;
}

/* ObjJetSctr_Main. The scooter's origin is a 16px cell corner and it
 * covers the two cells up and two left of it, so each direction probes
 * the pair of cells beyond that edge: up (-32, 0) and (-32, -16), down
 * (+16, 0) and (+16, -16), left (0, -32) and (-16, -32), right (0, +16)
 * and (-16, +16), as (y, x). The pad is read only between steps and with
 * no window or active object; a step already under way finishes regardless.
 * Up, down, left, right, the first that is clear wins, and a blocked one
 * still turns the scooter. RunObject (npc.c) moves it. */
static WalkResult scooter_tick(Walk *w, const Input *in, bool blocked)
{
    Npc *s = scooter_of(w);

    update_screen_pos(w);               /* the hidden leader's RunObject */
    if (s == NULL || s->routine != 1) {
        return WALK_RUNNING;            /* its init has yet to see the flag */
    }

    WalkResult result = WALK_RUNNING;
    bool moving = false;
    if (s->wait != 0) {
        s->wait--;
        moving = (s->wait >= 0);
    }

    if (!moving) {
        if (blocked) {
            scooter_anim(s);            /* loc_4B18: no copy */
            s->vx = 0;
            s->vy = 0;
            return WALK_RUNNING;
        }
        s->vx = 0;
        s->vy = 0;

        bool go = false;
        if (in->held.up) {
            go = scooter_try(w, s, FACE_UP, -32, 0, -32, -16,
                             0, -SCOOTER_SPEED, &result);
        }
        if (!go && in->held.down) {
            go = scooter_try(w, s, FACE_DOWN, 16, 0, 16, -16,
                             0, SCOOTER_SPEED, &result);
        }
        if (!go && in->held.left) {
            go = scooter_try(w, s, FACE_LEFT, 0, -32, -16, -32,
                             -SCOOTER_SPEED, 0, &result);
        }
        if (!go && in->held.right) {
            go = scooter_try(w, s, FACE_RIGHT, 0, 16, -16, 16,
                             SCOOTER_SPEED, 0, &result);
        }
        if (!go) {
            s->frame = s->facing;       /* loc_4A64, then loc_4AE2 */
            scooter_copy(w, s);
            scooter_anim(s);
            return result;
        }
        s->wait    = SCOOTER_STEP_FRAMES;   /* loc_4A6E */
        w->stepped = true;                  /* $FFFFCB0A = 1 */
    }

    /* loc_4A7A: the camera by the scooter's place on screen last frame. */
    int scroll_y = 0;
    int scroll_x = 0;
    if (s->screen_y <= SCOOTER_FOLLOW_UP && s->facing == FACE_UP) {
        scroll_y = -SCOOTER_SPEED;
    }
    if (s->screen_y >= SCOOTER_FOLLOW_DOWN && s->facing == FACE_DOWN) {
        scroll_y = SCOOTER_SPEED;
    }
    if (s->screen_x <= SCOOTER_FOLLOW_LEFT && s->facing == FACE_LEFT) {
        scroll_x = -SCOOTER_SPEED;
    }
    if (s->screen_x >= SCOOTER_FOLLOW_RIGHT && s->facing == FACE_RIGHT) {
        scroll_x = SCOOTER_SPEED;
    }
    move_camera(w, scroll_y, scroll_x);

    scooter_copy(w, s);
    scooter_anim(s);
    return result;
}

/* JetScooter_CharOffPosOffsets (ps2.asm:20010), by the scooter's $2A / 3:
 * where the party steps off, as (y, x) from the scooter's origin. */
static const s8 DISMOUNT[4][2] = {
    { -32, 0 }, { 16, 0 }, { 0, -32 }, { 0, 16 }
};

bool walk_scooter_can_dismount(Walk *w)
{
    Npc *s = scooter_of(w);
    if (s == NULL) {
        return false;
    }
    int d = (s->facing / 3) & 3;
    w->y = (s16)(w->y + DISMOUNT[d][0]);
    w->x = (s16)(w->x + DISMOUNT[d][1]);
    return walk_cell_open(w, s->x + DISMOUNT[d][1], s->y + DISMOUNT[d][0]);
}

void walk_scooter_dismount(Walk *w, s16 *park_x, s16 *park_y)
{
    Npc *s = scooter_of(w);
    if (s != NULL) {
        *park_x = (s16)(s->x & 0xFFF0);
        *park_y = (s16)(s->y & 0xFFF0);
        for (int i = 0; i < w->follower_count; i++) {
            w->follower[i].facing = s->facing;      /* loc_DA4C: E42A */
        }
    }
    w->reinit = true;
}

/* ObjCL_Init (ps2.asm:5157) run again after loc_DA4C cleared $22, and
 * ObjFlwngChar_Init falling into its main routine the same frame. The
 * leader's $2A comes from his $24, which nothing touched while he rode:
 * he faces the way he did when he got on, or down if the ride began at a
 * level entry (LevelScreen's $24 = 3). The followers' $2A was the
 * scooter's; they turn toward the leader as they land on him. */
static void party_reinit(Walk *w)
{
    w->reinit = false;
    w->x = (s16)(w->x + 8);
    w->y = (s16)(w->y + 8);
    w->facing = w->frame;
    for (int i = 0; i < WALK_TRAIL; i++) {
        w->trail[i][0] = w->x;
        w->trail[i][1] = w->y;
    }
    run_object(w);
}

WalkResult walk_tick(Walk *w, const Input *in, bool blocked)
{
    w->hurt_now     = false;
    w->demo_stopped = false;
    w->demo_door    = false;
    w->bump_a       = false;

    if (w->riding) {
        return scooter_tick(w, in, blocked);
    }
    if (w->reinit) {
        party_reinit(w);
        return WALK_RUNNING;
    }

    if (w->step_frames != 0) {
        w->step_frames--;
        stepping(w);
        run_object(w);
        return WALK_RUNNING;
    }

    if (blocked) {
        w->frame = w->facing;
        w->vx = 0;
        w->vy = 0;
        run_object(w);
        return WALK_RUNNING;
    }

    bool up, down, left, right;

    if (w->demo != NULL) {
        u8 byte = w->demo[w->demo_pos];
        if (byte == DEMO_END) {
            w->end_x = (s16)(w->x & 0xFFF0);
            w->end_y = (s16)(w->y & 0xFFF0);
            return WALK_DEMO_ENDED;
        }
        if (byte == DEMO_STOP) {
            /* loc_37EC: $FFFFDE70 + 1, demo_flag = 0, and nothing more
             * from ObjCL_Main this frame. */
            w->demo         = NULL;
            w->demo_stopped = true;
            run_object(w);
            return WALK_RUNNING;
        }
        if (byte == DEMO_DOOR) {
            /* loc_37FE: SXFID_DoorOpen, $FFFFE822 = 2, demo_flag = 0, and
             * nothing more this frame either. */
            w->demo      = NULL;
            w->demo_door = true;
            run_object(w);
            return WALK_RUNNING;
        }
        w->demo_pos++;
        w->force = byte;
        up    = (byte & DEMO_UP) != 0;
        down  = (byte & DEMO_DOWN) != 0;
        left  = (byte & DEMO_LEFT) != 0;
        right = (byte & DEMO_RIGHT) != 0;
    } else {
        w->force = 0;
        up    = in->held.up;
        down  = in->held.down;
        left  = in->held.left;
        right = in->held.right;
    }

    /* loc_382A: the tile underfoot is looked at before the pad is. */
    WalkResult result = check_underfoot(w);
    if (result != WALK_RUNNING) {
        w->frame = w->facing;
        return result;
    }

    w->vx = 0;
    w->vy = 0;

    /* A blocked direction still turns the character, and the test falls
     * through to the next direction held. */
    bool stepped = false;
    if (up) {
        stepped = try_step(w, FACE_UP, 0, -1, &result);
    }
    if (!stepped && down) {
        stepped = try_step(w, FACE_DOWN, 0, 1, &result);
    }
    if (!stepped && left) {
        stepped = try_step(w, FACE_LEFT, -1, 0, &result);
        if (!stepped && w->blocker != NULL
            && w->blocker->interaction == NPC_INTERACT_TEIM) {
            /* $2E(a1) type 3: the pad becomes an A press (the caller
             * writes it), and `bra.s loc_38F4` skips the right test. */
            w->bump_a = true;
            right     = false;
        }
    }
    if (!stepped && right) {
        stepped = try_step(w, FACE_RIGHT, 1, 0, &result);
    }

    if (!stepped) {
        w->frame = w->facing;
        run_object(w);
        return result;
    }

    w->step_frames  = STEP_FRAMES;
    w->stepped      = true;         /* loc_38FC: $FFFFCB0A = 1 */
    w->hurt_pending = true;         /*           $30(a0) = 1   */
    stepping(w);
    run_object(w);
    return WALK_RUNNING;
}

int walk_frame_tiles(int frame)
{
    static const u8 TILES[12] = {
        0x18, 0x20, 0x28,       /* up    */
        0x00, 0x08, 0x10,       /* down  */
        0x30, 0x38, 0x40,       /* left  */
        0x30, 0x38, 0x40        /* right: the left frames, mirrored */
    };
    return TILES[frame % 12];
}

bool walk_frame_flipped(int frame)
{
    return frame >= FACE_RIGHT;
}

int walk_step_frames(Walk *w)
{
    if (w->riding) {
        Npc *s = scooter_of(w);
        return (s != NULL) ? s->wait : 0;
    }
    return w->step_frames;
}
