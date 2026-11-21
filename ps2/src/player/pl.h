#ifndef PS2_PLAYER_PL_H
#define PS2_PLAYER_PL_H

/* (build 171) COMPUTER PLAYS's player in C: tools/play.py, tools/story.py
 * and tools/playthrough.py ported a stage group at a time, playing the
 * Python's own games command for command (docs/c_player.md). The player
 * speaks the route language as the Python does: each command a line given
 * to the probe's interpreter (probe_run_text), the `state` line after
 * them read back from the log. The names here are the Python's, so a
 * function is found by its name in either.
 *
 * Python's None is PL_NONE (-1) for the numbers that are never negative
 * (a level, a cell, a menu, the meseta). A harness error -- a KeyError or
 * an IndexError in the Python, which loses the try -- is pl_error, which
 * jumps back to the try's loop. */

#include "player.h"

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ---- little things (util.c) ---- */

#define PL_NONE (-1)

/* Python's % and // for a positive divisor. */
static inline int pymod(int a, int b)
{
    int r = a % b;
    return (r < 0) ? r + b : r;
}

static inline int pydiv(int a, int b)
{
    int q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

static inline bool streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static inline bool starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* A harness error: the message, then a jump to the trap (the try's loop
 * in run.c). The Python's KeyError, IndexError, ValueError. */
extern jmp_buf *pl_trap;
extern char     pl_error_text[512];
_Noreturn void pl_error(const char *fmt, ...);

void *pl_alloc(size_t n);               /* calloc, or a harness error */
void *pl_grow(void *p, size_t n);       /* realloc, or a harness error */
char *pl_strdup(const char *s);

/* Lines of text, malloc'd, in order. */
typedef struct {
    char **v;
    int    n, cap;
} Lines;

void lines_add(Lines *l, const char *text);
void lines_addf(Lines *l, const char *fmt, ...);
void lines_extend(Lines *l, const Lines *more);
void lines_clear(Lines *l);
void lines_free(Lines *l);
bool lines_any(const Lines *l, const char *needle);   /* any(needle in ln) */
bool lines_any2(const Lines *l, const char *a, const char *b);

/* A list of ints, appended to. */
typedef struct {
    int *v;
    int  n, cap;
} Ints;

void ints_add(Ints *l, int v);
void ints_free(Ints *l);
bool ints_has(const Ints *l, int v);

/* A cell. Packed into a key for the sets: (x + 32768) << 16 | (y + 32768),
 * so the cells a door record's 3x3 reaches off the map's top keep. */
typedef struct {
    int x, y;
} Cell;

static inline u32 cell_key(int x, int y)
{
    return ((u32)(x + 32768) << 16) | (u32)(y + 32768);
}

static inline Cell key_cell(u32 k)
{
    Cell c = { (int)(k >> 16) - 32768, (int)(k & 0xFFFF) - 32768 };
    return c;
}

/* Python's order for cells: (x, y) tuples. */
int cell_cmp(const void *a, const void *b);

/* A set of cells in the order added, with an int each: a Python set, or a
 * dict's keys and values (door_cells' sides) in its insertion order. */
typedef struct {
    u64 *key;
    int *val;
    int  n, cap;
    int *slot;      /* open addressing: index + 1, 0 empty */
    int  slots;
} CSet;

void cset_clear(CSet *s);
void cset_free(CSet *s);
bool cset_add(CSet *s, int x, int y, int val);   /* false: already there */
void cset_put(CSet *s, int x, int y, int val);   /* add, or set the value */
int  cset_find(const CSet *s, int x, int y);     /* its index, or -1 */
bool cset_has(const CSet *s, int x, int y);
int  cset_get(const CSet *s, int x, int y, int fallback);
Cell cset_cell(const CSet *s, int i);
/* Keys of any kind (plan()'s (level, cell) nodes). */
bool kset_add(CSet *s, u64 key, int val);
int  kset_find(const CSet *s, u64 key);
void cset_copy(CSet *dst, const CSet *src);
void cset_union(CSet *dst, const CSet *add);
/* The cells sorted as Python sorts tuples, into out[s->n]. */
void cset_sorted(const CSet *s, Cell *out);

/* Cells as Python prints a list of tuples: "[(1, 2), (3, 4)]". */
void cells_repr(const Cell *c, int n, char *out, size_t cap);
/* sorted(goals)[:k], as printed. */
void cset_repr(const CSet *s, int k, char *out, size_t cap);
/* (x, y) as Python prints it, None for PL_NONE. */
const char *xy_repr(int x, int y, char *out, size_t cap);

/* A list of cells. */
typedef struct {
    Cell *v;
    int   n, cap;
} Cells;

void cells_add(Cells *l, int x, int y);
void cells_free(Cells *l);

/* A list of script lists: goto's `fights`. */
typedef struct {
    Ints *v;
    int   n, cap;
} IntsList;

void intslist_add(IntsList *l, const Ints *v);
void intslist_free(IntsList *l);

/* The story's log (story.py's Report); NULL is print(). */
typedef struct Report {
    Lines  lines;
    double t0;
} Report;
void say(Report *log, const char *fmt, ...);
/* (build 179) Where say() prints as well: stdout, or the window's
 * driver.log (the Python's stdout went there). */
extern FILE *pl_echo;

/* The whole of a file, NUL-terminated, malloc'd: the game's pack first,
 * then the disk. NULL if neither has it. */
char *pl_read_file(const char *path, size_t *size);
bool  pl_file_exists(const char *path);
bool  pl_copy_file(const char *from, const char *to);
bool  pl_write_text(const char *path, const char *text);   /* LF ends */
bool  pl_make_dirs(const char *path);

/* ---- the state line (play.py's STATE_RE, MEMBER_RE, parse_state) ---- */

#define PL_PARTY_MAX   8
#define PL_ENEMIES_MAX 16

typedef struct {
    char name[16];
    int  level, hp, max_hp, tp, max_tp;
    bool dead, poison;
    int  curse;             /* PL_NONE, 0 marked, else the kind */
    bool sealed, deban;
} Member;

typedef struct {
    int id, hp, max_hp;
} Enemy;

typedef struct {
    bool   valid;           /* false: the Python's {} before any state */
    u32    tick;
    char   screen[24];
    char   kind[8];         /* waiting, busy */
    char   wait[64];
    int    level;           /* PL_NONE off the map */
    int    x, y;            /* PL_NONE off the map */
    char   facing[8];
    bool   mid_step, riding;
    int    building;        /* PL_NONE */
    int    menu;            /* PL_NONE */
    int    menu_stop;       /* PL_NONE */
    int    menu_stops;      /* PL_NONE */
    bool   menu_across, menu_party;
    Member party[PL_PARTY_MAX];
    int    party_n;
    int    meseta;          /* PL_NONE */
    int    formation;       /* PL_NONE */
    Enemy  enemy[PL_ENEMIES_MAX];
    int    enemy_n;
} State;

/* A log line's state, if it is one (STATE_RE.search): true, *st filled. */
bool parse_state_line(const char *line, State *st);
/* str(s.state): the dict as Python prints it ({} before any state). */
const char *state_repr(const State *st, char *out, size_t n);

static inline bool st_screen(const State *st, const char *screen)
{
    return streq(st->screen, screen);
}

static inline bool st_waiting(const State *st)
{
    return streq(st->kind, "waiting");
}

static inline bool st_wait(const State *st, const char *wait)
{
    return streq(st->wait, wait);
}

/* screen level, waiting on the pad for the party to walk */
static inline bool st_control(const State *st, const char *screen)
{
    return st_screen(st, screen) && st_waiting(st) && st_wait(st, "control");
}

/* ---- the pauses' stream (play.py's Jitter) ---- */

typedef struct {
    u64  state;
    bool on;                /* false: Session.jitter is None */
} Jitter;

void   jitter_init(Jitter *j, const char *name);
u64    jitter_next64(Jitter *j);
double jitter_random(Jitter *j);
int    jitter_randint(Jitter *j, int a, int b);

/* ---- the session (play.py's Session) ---- */

/* fight_smart's record of each fight (Session.fights). */
typedef struct {
    u32  tick;
    int  formation;
    int  ticks;
    int  victory_ticks;     /* PL_NONE: no victory */
    bool won;
    bool ran;
    int  dead_after;
    int  tail[8];           /* (build 178) its scripts' last 8 */
    int  tail_n;
} FightRec;

/* A down or a revival (Session.downs, Session.revivals). */
typedef struct {
    char name[16];
    char screen[24];
    int  level, building;
    u32  tick;
    int  scripts[4];
    int  script_n;
} DownRec;

typedef struct {
    char who[16];
    char tech[16];
    int  formation;
} TechOrder;

/* fight_smart's keywords. */
typedef struct {
    double run_below, mate_below, heal_below;
    int    limit;
    bool   stop_on_level, use_techs, boss_techs, deban, shift, reset_techs,
           stop_on_gameover;
    const void *type_techs;         /* None */
} FightOpts;

FightOpts fight_defaults(void);

/* trip_marks: each member's level by name. */
typedef struct {
    char name[PL_PARTY_MAX][16];
    int  level[PL_PARTY_MAX];
    int  n;
} Marks;

struct Session {
    PlayerIO io;
    char     name[160];
    bool     attached;
    int      grind_depth;
    char     saves_dir[256];

    State    state;
    Lines    pending;       /* logged and not yet read */
    int      pending_head;  /* ... from this one */
    Lines    out;           /* the last send's */
    char     last[3][256];  /* events[-3:], for a stall's message */
    int      last_n;
    Lines    stderr_lines;  /* events with "    |     " in them */

    Ints     shown;         /* every script id shown, in order */
    Ints     shown_ticks;
    int      last_script;

    FightRec *fights;
    int      fights_n, fights_cap;
    TechOrder *tech_orders;
    int      tech_orders_n, tech_orders_cap;

    int      save_town;
    Ints     looted;
    bool     continued;
    bool     careful;
    double   walk_mend;     /* < 0: None */
    int      dew_reserve;
    bool     dews_gave_up;  /* (build 177) a restock came back empty */
    bool     hurt;
    char     why[512];      /* "" : None */
    bool     why_set;
    FightOpts fight_opts;
    bool     fight_opts_set;    /* s.fight_opts is not None */
    char     expendable[PL_PARTY_MAX][16];
    int      expendable_n;

    DownRec *downs;
    int      downs_n, downs_cap;
    DownRec *revivals;
    int      revivals_n, revivals_cap;
    char     dead[PL_PARTY_MAX][16];
    int      dead_n;
    char     prev_screen[24];
    bool     prev_screen_set;

    Jitter   jitter;
    long     jitter_ticks;
    char     pace[8];
    int      fight_poll;
    bool     pad_waits;
    int      fight_jitter_max;
    int      victory_hold;
    int      victory_slack;
    long     ticks;
    long     last_tick;     /* -1: None */
    long     jitter_fight_ticks;
    int      route_depth;
    bool     buying_gear;   /* inside buying_gear(): `@gear off` owed */
    struct Bags *worn_bags;    /* weapon_blow's s._worn: NULL None */
    int      worn_at;       /* ... read at this many fights */
    Cell     scooter_at;    /* where the scooter was last seen parked */
    bool     scooter_known; /* ... false: getattr's None */

    /* playthrough's stage_begins */
    char     stage[32];
    Marks    save_marks;
    bool     story_point;
    Lines    progress;
};

#define JITTER_P   0.25
#define JITTER_MAX 24

void session_init(Session *s, const char *name, const PlayerIO *io);
void session_free(Session *s);
void set_pace(Session *s, const char *name);

/* The host's log line, stamped "%7u  %s" as route_probe writes it. */
void session_log_line(Session *s, u32 tick, const char *line);

/* Run commands, then `state`; the lines they produced (s->out, until the
 * next send). */
const Lines *sendv(Session *s, int n, const char *const *cmds);
const Lines *send1(Session *s, const char *cmd);
const Lines *sendf(Session *s, const char *fmt, ...);
const Lines *send2(Session *s, const char *a, const char *b);
const Lines *send3(Session *s, const char *a, const char *b, const char *c);

void        host_line(Session *s, const char *line);
void        in_grind_begin(Session *s);
void        in_grind_end(Session *s);
const char *fight_wait(Session *s, char *buf, size_t n);
const Lines *answer_page(Session *s);
bool        may_hurry(const Session *s);
void        scripts_of(const Lines *lines, Ints *out);
void        new_game(Session *s);
void        press_through(Session *s, const char *screen, Ints *shown);
bool        ready(Session *s);
bool        ready_n(Session *s, int limit);    /* s.ready(limit=...) */
void        session_fight(Session *s, Ints *shown);

/* ---- the maps (map.c) ---- */

#define MOTAVIA 0x00
#define PASEO   0x04

/* door_cells' sides: the push into a solid door's cell, or none. */
enum { SIDE_NONE = 0, SIDE_UP, SIDE_DOWN, SIDE_LEFT, SIDE_RIGHT };
const char *side_name(int side);        /* NULL for SIDE_NONE */

#define LM_PLANES 4

typedef struct {
    int   index;
    int   cols, rows, w, h, type;
    int   nplanes;
    char  pname[LM_PLANES][16];
    int  *plane[LM_PLANES];
    int   plane_len[LM_PLANES];
    int   coll;                 /* the collision plane's slot */
    u8    attr[256];
    /* per cell, on the collision plane, once the patches are in */
    u8   *solid, *special;
    u8   *spec;                 /* specials(): open and special */
    u8   *damage;               /* damage_floors() */
    u8   *edge;                 /* town_edges() */
    CSet  edges;                /* ... as a set, row by row */
    int  *piece;                /* pieces(): -1 for none */
    /* path()'s working space */
    int  *cost, *prev;
    u32  *stamp, gen;
} LevelMap;

int         levels_count(void);
int         map_index_name(const char *name);
const char *level_name(int index, char *buf, size_t n);
void        held_set(const char *const *names, int n);   /* play.HELD */
void        held_add(const char *name);                  /* HELD.add */
LevelMap   *level_map(int level);
int         level_enemy_group(int level, bool late);
int         lm_block(const LevelMap *lm, int plane, int cx, int cy);
void        lm_bits(const LevelMap *lm, int cx, int cy, int plane,
                    bool *solid, bool *special);
bool        lm_open(const LevelMap *lm, int cx, int cy);
bool        lm_barrier(const LevelMap *lm, int c);
bool        lm_special_open(const LevelMap *lm, int x, int y);
bool        lm_town_edge(const LevelMap *lm, int x, int y);
bool        lm_damage(const LevelMap *lm, int x, int y);
bool        lm_reaches(const LevelMap *lm, int sx, int sy, const CSet *goals);
/* path(start, goals, avoid | avoid2): the cells, or false for None. */
bool        lm_path(LevelMap *lm, int sx, int sy, const CSet *goals,
                    const CSet *avoid, const CSet *avoid2, Cells *out);

typedef struct {
    IntsList   *fights;         /* each fight's scripts, or NULL */
    int         tries;          /* 30 */
    Report     *log;
    const CSet *keep_off;
    void      (*after_fight)(Session *s, void *ctx);
    void       *after_ctx;
} GotoOpts;

GotoOpts goto_defaults(void);
bool     goto_cells(Session *s, const CSet *goals);
bool     goto_opts(Session *s, const CSet *goals, const GotoOpts *o);

typedef struct {
    int  y, x;
    bool has_to_level;
    int  to_level, to_x, to_y;
    int  building;
    int  f764, f766;
} Door;

const Door *level_doors(int level, int *n);
void        door_cells(int level, int ry, int rx, CSet *out);

typedef struct {
    int x, y;
    int to_level, to_x, to_y;
} Hole;

int  holes(int level, Hole **out);      /* the caller frees *out */
bool on_hole(int level, int x, int y);

typedef struct {
    CSet cells;
    int  to_level, ax, ay;
} Way;

const Way *ways_out(int level, int *n);

typedef struct {
    int  level;
    bool is_goals;              /* the plan's last leg: the goals */
    CSet cells;
} Leg;

typedef struct {
    Leg *v;
    int  n;
} Legs;

bool plan(int level, int cx, int cy, int target_level, const CSet *goals,
          Legs *out);
void legs_free(Legs *l);
void ride_holes(Session *s, Report *log);
bool travel(Session *s, int target_level, const CSet *goals, Report *log);
/* town_exits()[level]'s landing on Motavia; false for none. */
bool town_exit_to(int level, int *to_x, int *to_y);

/* _runs: a path's cells as walks of one direction. */
typedef struct {
    char d[8];
    int  n;
} Run;

int runs_of(int sx, int sy, const Cells *cells, int w, int h, Run *out);

/* (build 173) the jet scooter (scooter.c) */
void walk_region(int level, const CSet *cells, CSet *out);
void landing_region(int target_level, const CSet *goals, CSet *region);
bool ride_ashore(Session *s, const CSet *targets, const CSet *region,
                 Report *log);
bool ride_to_face(Session *s, const CSet *cells, Report *log);
bool board_scooter(Session *s, Report *log);
/* (build 174) ride to the door record into `to_level` (a dam) and push. */
bool ride_into(Session *s, int to_level, Report *log);
bool town_exit(Session *s);
void town_arrivals(int level, CSet *out);
bool enter_town(Session *s, int level);

/* ---- fights and mending (fight.c, gear.c) ---- */

void fight_smart(Session *s, const FightOpts *o, Ints *shown);
bool needs_healing(Session *s, double frac);
int  revive(Session *s);                /* how many came back */
int  revive_x(Session *s, bool spend_reserve);  /* revive(spend_reserve=) */
void cure_poison(Session *s);
void mend(Session *s, double below);
int  cast_res(Session *s, double below);

/* ---- items, bags and techniques (gear.c) ---- */

/* items.json's entry. */
typedef struct {
    char key[24];
    int  price, action_flags, equip_mask, attack, defense;
} Item;

const Item *item_get(const char *key);          /* NULL: not in items() */
/* store_stock(index)[:count] into out; how many. */
int store_stock(int index, int count, char (*out)[24]);

/* InventoryData's kind (action_flags & 7): where it is worn. */
#define KIND_HEAD  1
#define KIND_HAND  2
#define KIND_BOTH  3
#define KIND_BODY  4
#define KIND_LEGS  5
#define KIND_SLOTS 8
int kind_of(const char *name);
/* (build 177) sold_anywhere(): some store sells it; CHAR_ID[name] (-1:
 * none); wearable(name, char_id): its equip mask has the bit. */
bool sold_anywhere(const char *name);
int  char_id(const char *name);
bool wearable(const char *name, int cid);

#define BAG_MAX 24

typedef struct {
    char name[24];
    bool worn;
} BagItem;

typedef struct {
    BagItem v[BAG_MAX];
    int     n;
} Bag;

/* bags(s): each member's bag by party position ([] when no line). */
typedef struct Bags {
    Bag m[PL_PARTY_MAX];
    int n;
} Bags;

void bags(Session *s, Bags *out);
int  count_mates(Session *s);
int  carried(Session *s, const char *item);
bool find_item(Session *s, const char *item, int *holder, int *slot);

/* map_techs' (stop, key, tp). */
typedef struct {
    int  stop;
    char key[16];
    int  tp;
} Tech;

bool find_tech(Session *s, const char *key, int *who, int *stop);

/* plan_gear's sources: ("bag", slot) a=slot; ("give", holder, slot) and
 * ("stow", holder, slot) a=holder b=slot; (building, stock) a=building
 * b=stock. */
enum { SRC_BAG = 1, SRC_GIVE, SRC_STOW, SRC_SHOP };

typedef struct {
    int kind, a, b;
} Src;

typedef struct {
    Src  src;
    int  m;
    char name[24];
    char old[4][24];
    int  old_n;
} GearTodo;

/* worn_head(s, name): the head piece the member wears, NULL for none. */
const char *worn_head(Session *s, const char *name);

/* DEZOLIS_LEVELS */
static inline bool is_dezolis_level(int level)
{
    return level == 0x01 || level == 0x02 || level == 0x03 || level == 0x0A
        || level == 0x0B || level == 0x0C;
}

int  plan_gear(Session *s, int level, int reserve, Report *log,
               GearTodo *todo, int cap);
bool geared(Session *s, int town);
void stock_mates(Session *s, int level, Report *log);
/* stock_mates(s, level, want, trimates): how many were bought. */
int  stock_mates_x(Session *s, int level, int want, bool trimates,
                   Report *log);
/* stock_item: up to `want` of `item` carried, bought at `level`'s item
 * store into whichever bags have room; how many were bought. */
int  stock_item(Session *s, int level, const char *item, int want,
                Report *log);
/* resupply(s, door, level): the way home and mates to eight from the item
 * store at door (dy, dx). */
void resupply(Session *s, int level, int dy, int dx);

/* c["name"].upper() == name.upper() */
static inline bool same_upper(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        char x = (*a >= 'a' && *a <= 'z') ? (char)(*a - 'a' + 'A') : *a;
        char y = (*b >= 'a' && *b <= 'z') ? (char)(*b - 'a' + 'A') : *b;
        if (x != y) {
            return false;
        }
    }
    return *a == *b;
}

/* battle.json (fight.c): the tree, an enemy by id (NULL: none), a
 * formation by id (NULL: none). */
typedef struct JsonValue JsonValue;
const JsonValue *battle_data(void);
const JsonValue *enemy_of(int eid);
const JsonValue *formation_of(int f);

/* The screens a wipe-out leaves the game on (play.py's WIPED). */
bool wiped(const State *st);

/* ---- towns and their buildings (town.c) ---- */

#define WIN_YESNO        0x09
#define WIN_YESNO2       0x1F
#define WIN_YESNO3       0x20
#define WIN_BUYSELL      0x22
#define WIN_NAME_INPUT   0x24
#define WIN_SAVE_SLOTS   0x25
#define WIN_HEAL_CURE    0x27
#define WIN_STORE_STOCK  0x28
#define WIN_STORE_CHARS  0x5B
#define WIN_BATTLE_OPTS  0x60
#define WIN_BATTLE_OPTS2 0x61
#define WIN_MENU_ITEM_LIST  0x03
#define WIN_MENU_ITEM_LIST2 0x04

/* visit's policy: what to pick at a window -- a stop, or PICK_C (None:
 * press C) or PICK_B ("b": back out). */
typedef int (*PolicyFn)(Session *s, void *ctx);
#define PICK_C (-1)
#define PICK_B (-2)

/* visit(s, level, door, policy, limit): false for the Python's None. */
bool visit(Session *s, int level, int dy, int dx, PolicyFn policy, void *ctx,
           int limit, Ints *shown);

/* buy's (stock index, party member); sell's (party member, bag slot). */
typedef struct {
    int stock, who;
} Purchase;

typedef struct {
    int who, slot;
} Sale;

void buy(Session *s, int level, int dy, int dx, const Purchase *todo, int n);
void sell(Session *s, int level, int dy, int dx, const Sale *todo, int n);
bool rolf_alive_for_shops(Session *s, int level);

/* The C menu. Each fills `shown` (when not NULL) with the scripts shown
 * since it began, s.shown[before:]; false for the Python's None. */
bool menu_escape(Session *s);
bool list_pick(Session *s, int slot);
bool equip(Session *s, int member, int slot, int hand, Ints *shown);
bool use_item(Session *s, int holder, int slot, int target, Ints *shown);
bool give_item(Session *s, int holder, int slot, int to, Ints *shown);
bool toss_item(Session *s, int holder, int slot, Ints *shown);
bool use_tech(Session *s, int caster, int stop, int target, Ints *shown);

#define BUILDING_CLONE_LAB   3
#define BUILDING_DATA_MEMORY 2
#define BUILDING_HOSPITAL    4
#define BUILDING_WEAPON      5
#define BUILDING_ARMOR       6
#define BUILDING_ITEM        7

/* town_buildings(level)[building]: the first door of it. */
typedef struct {
    int y, x;
    int f764, f766;
} TownBuilding;

bool town_building(int level, int building, TownBuilding *out);
bool continue_game(Session *s, const char *save_file);
/* save(s, slot, level, door) */
void save_at(Session *s, int slot, int level, int y, int x);
void save_paseo(Session *s, int slot);
void heal_here(Session *s, int town);
bool retreat(Session *s, int town, Report *log);   /* town PL_NONE: None */
#define GEAR_RESERVE 120         /* gear_up's reserve= */
void gear_up(Session *s, int town, int reserve, Report *log);
/* gear_up's return: what it put on, the plan's todo (*got, until the next
 * gear_up); how many. */
int  gear_up_got(Session *s, int town, int reserve, Report *log,
                 const GearTodo **got);
const char *gear_todo_repr(const GearTodo *todo, int n, char *out,
                           size_t cap);
/* play.PIN_HEAD.add(name) */
void pin_head_add(const char *name);
bool rolf_house(Session *s, const int *take, int n, Ints *shown);
bool rolf_house_regroup(Session *s, const int *take, int n, Ints *shown);
void clone_lab(Session *s, int level, int y, int x);
/* script_name(id): its SCRIPT_ name, else $XXXX into buf (names.c). */
const char *script_name(int id, char *buf, size_t n);
const char *scripts_text(const Ints *ids, char *out, size_t n);
const char *bags_repr(Session *s, char *out, size_t n);
const char *bag_repr(const Bag *bag, char *out, size_t n);

/* ---- things on the map (things.c) ---- */

/* talk(s, limit): A, then C until the pad is back; s.shown[before:] onto
 * `shown`. */
void talk(Session *s, int limit, Ints *shown);
void neighbours(int level, int cx, int cy, CSet *out);   /* cell: side */
/* search(s, level, cell, key, cells): next to the cell (or on one of
 * `cells`, {cell: side}; NULL for the neighbours), facing it, A and C --
 * and with `key` the Small Key on a locked chest. The scripts onto
 * `shown`; false for the Python's None. */
bool search(Session *s, int level, int cx, int cy, Report *log, bool key,
            const CSet *cells, Ints *shown);

/* level_objects(level): by cell, (object, interaction, index), in the
 * dict's order. */
typedef struct {
    int x, y;                   /* the cell */
    int px, py;                 /* the pixel x, y */
    int object, interaction, index;
} LevelObject;

int         level_objects(int level, const LevelObject **out);
int         chests(int level, Cells *out);
const char *chest_item(int level, int x, int y);    /* NULL: None */
int         flag_byte(Session *s, int addr);
/* event_block(level, index): the cells of the invisible block ($37/$38)
 * with that ObjType_Events index; false for None. */
bool        event_block(int level, int index, CSet *out);
/* facing_cells: open cells outside `cells` next to one, {cell: side}. */
void        facing_cells(int level, const CSet *cells, CSet *out);
/* search_event: A on the invisible block with that index. */
bool        search_event(Session *s, int level, int index, Report *log,
                         Ints *shown);
extern const int BOX_ROWS[14][4];

/* objects(s): every live object slot on the level. */
typedef struct {
    int  slot, object, type, index, x, y;
    bool moving;
} ObjectSlot;

int  objects(Session *s, ObjectSlot *out, int cap);
/* talk_to(s, index, want, tries): up to the person with that dialogue
 * number, facing, A, until one of `want` (NULL: anything) is shown; the
 * scripts onto `shown`; false for None. */
bool talk_to(Session *s, int index, const int *want, int nwant, int tries,
             Report *log, Ints *shown);
/* person_name(level, index): "MuskCat at 47,27", by level_sprites.json. */
const char *person_name(int level, int index, char *buf, size_t n);

/* ---- who goes (party.c) ---- */

#define PL_ROSTER_MAX 8
#define CATCH_UP_GAP  5

typedef struct {
    char name[16];
    int  level, hp, max_hp, tp, max_tp;
    bool in_party;
} RosterEntry;

typedef struct {
    RosterEntry v[PL_ROSTER_MAX];
    int         n;
} Roster;

int roster(Session *s, Roster *out);
/* pick_party(s): the party chosen and made so at Rolf's house; its
 * names into out, how many. */
int pick_party(Session *s, Report *log, char (*out)[16]);
/* pick_party(s, want=...): the caller's party, NULL to choose it. */
int pick_party_want(Session *s, Report *log, const char *const *want,
                    int nwant, char (*out)[16]);

/* (build 176) dungeon_mix's {type: share}: $20 living, $40 robot, $60
 * both; set false for None. */
typedef struct {
    double v[3];
    bool   set;
} Mix;

/* dungeon_mix(levels): the HP-weighted type shares over the levels'
 * late groups; false (out->set) for None. */
bool        dungeon_mix(const int *levels, int n, Mix *out);
/* " ".join(f"{name}:{v:.2f}" for the shares that are not 0) */
const char *mix_repr(const Mix *mix, char *out, size_t cap);
/* tech_power(name, level, mix): the best damage technique against it. */
double      tech_power(const char *name, int level, const Mix *mix);

/* (build 176) choose_party(pickable(s), catch_up, mix=dungeon_mix(ahead),
 * core): the names into out, how many, the reason into why. */
int choose_party_ahead(Session *s, const int *ahead, int nahead,
                       bool catch_up, const char *const *core, int ncore,
                       char (*out)[16], char *why, size_t wn);
/* pick_party(s, catch_up, want, ahead, core) */
int pick_party_x(Session *s, Report *log, const char *const *want, int nwant,
                 const int *ahead, int nahead, bool catch_up,
                 const char *const *core, int ncore, char (*out)[16]);
/* member_value(name, level) with no mix */
double member_value(const char *name, int level);

/* fight.c's tables, for party.c */
const char      *ok_word(const char *ln, const char *word);
int              expected_stat(const char *name, int level, const char *key);
const JsonValue *techs_data(void);
bool        chest_shut(Session *s, int level, int x, int y);
/* make_room(s, who): a TOSS_FIRST item tossed from the fullest living
 * bag (who PL_NONE) or who's; its name, or NULL. */
const char *make_room(Session *s, Report *log, int who);

/* ---- Shir (shir.c) ---- */

/* shir_thefts' wants: {item: count}, in order. */
typedef struct {
    const char *item;
    int         count;
} Want;

/* shir_join(s, keep): Shir into the party with the best `keep` others. */
bool shir_join(Session *s, int keep, Report *log);
/* raise_member(s, name, target, town, trips): fights by the town until
 * `name` is at `target`. */
bool raise_member(Session *s, const char *name, int target, int town,
                  Report *log, int trips);
/* shir_thefts(s, wants, max_exits, on_full): Shir in and out of Paseo's
 * stores until the party carries `wants`; the tally logged, and (build
 * 177) its "items" as Python prints the dict into `items` (NULL: not
 * wanted). */
void shir_thefts(Session *s, const Want *wants, int nwants, Report *log,
                 int max_exits, void (*on_full)(Session *s, Report *log),
                 char *items, size_t items_cap);
/* shir_bag(s): her bag while she is in the party (else empty). */
void shir_bag(Session *s, Bag *out);

/* ---- the grinds (ground.c) ---- */

/* play.Ground: one level of creature's cells, the walks' anchors. */
typedef struct {
    int   level;
    CSet  cells;
    Cell *anchors;
    int   anchor_n;
    CSet  keep_off;
} Ground;

Ground *ground_new(int level, const int *groups, int ngroups, int sx, int sy,
                   const Cell *anchors, int nanchors);

typedef struct {
    int     fights_per_trip;    /* 8 */
    int     trips;              /* 60 */
    int     reserve;            /* 120 */
    Report *log;
    bool    until_geared, lowest;
} RoamOpts;

RoamOpts roam_defaults(void);
bool     roam_and_gear(Session *s, int town, int target_level, Ground *g,
                       const RoamOpts *o);

/* grind_and_gear's keywords. */
typedef struct {
    int                fights_per_trip;     /* 5 */
    int                trips;               /* 60 */
    const char *const *walks;               /* down, left, up, right 3 */
    int                nwalks;
    int                reserve;             /* 120 */
    Report            *log;
    bool               until_geared, lowest;
    bool               has_spot;            /* spot=None: false */
    Cell               spot;
} GrindOpts;

GrindOpts grind_defaults(void);
bool      grind_and_gear(Session *s, int town, int target_level,
                         const GrindOpts *o);

/* dungeon_grind's keywords. */
typedef struct {
    bool               lowest;              /* true */
    int                trips;               /* 60 */
    int                fights_per_trip;     /* 8 */
    const char *const *walks;               /* left 3, right 3 */
    int                nwalks;
    Report            *log;
    Ground            *ground;              /* NULL: None */
    bool               gear;
} DungeonOpts;

DungeonOpts dungeon_defaults(void);
bool        dungeon_grind(Session *s, int level, int spot_x, int spot_y,
                          int town, int target_level, const DungeonOpts *o);
void trip_marks(const Session *s, Marks *out);
bool trip_save(Session *s, int town, Report *log, bool saved);
bool lean_short(Session *s, int town, int target_level, Report *log);
/* wander: the pattern walked until `fights` fights or the party needs
 * the hospital (with `mates`, the mates first); how many fights. */
int  wander(Session *s, const char *const *walks, int nwalks, int fights,
            bool mates);
/* A trip line's party: "Rolf L16 130/130 ...; meseta N", at line+used. */
void trip_party(const State *st, char *line, size_t used, size_t cap);
int  encounter_group(int level, int x, int y, bool riding, bool late);
/* field_spot(level, near, reach): open ground near `near`; false: None. */
bool field_spot(int level, int nx, int ny, int reach, Cell *out);

/* ---- the story (story.c, run.c) ---- */

extern bool pl_lean;
extern bool pl_squeak;
extern char pl_saves[256];
extern char pl_mid_saves[256];

double      pl_now(void);
double      pl_mtime(const char *path);
const char *party_line(const Session *s, char *out, size_t n);
bool        party_dead(const Session *s);
bool        party_has(const Session *s, const char *name);
void        checkpoint(Session *s, const char *name, Report *log);
bool        recover(Session *s, Report *log);
bool        revive_party(Session *s, int town, Report *log);
void        squeak(void);
bool        top_up(Session *s, const char *name, Report *log, int attempt);
bool        stage_paseo(Session *s, Report *log);
bool        stage_arima(Session *s, Report *log);
bool        stage_gear(Session *s, Report *log);
bool        stage_shure(Session *s, Report *log);
bool        stage_nido(Session *s, Report *log);
bool        stage_darum(Session *s, Report *log);
bool        stage_oputa(Session *s, Report *log);
bool        stage_amy(Session *s, Report *log);
bool        stage_lab_in(Session *s, Report *log);
bool        stage_lab(Session *s, Report *log);
bool        stage_lab_b1(Session *s, Report *log);
bool        stage_hugh(Session *s, Report *log);
bool        stage_key_tube(Session *s, Report *log);
bool        stage_zema(Session *s, Report *log);
bool        stage_roron(Session *s, Report *log);
bool        stage_uzo(Session *s, Report *log);
bool        stage_gum(Session *s, Report *log);
bool        stage_climatrol(Session *s, Report *log);
bool        stage_dam_party(Session *s, Report *log);
bool        stage_cards(Session *s, Report *log);
bool        stage_dam_grind(Session *s, Report *log);
bool        stage_dams(Session *s, Report *log);
bool        stage_roof(Session *s, Report *log);
bool        stage_skure(Session *s, Report *log);
bool        stage_dezolis_towns(Session *s, Report *log);
bool        stage_esper(Session *s, Report *log);
bool        stage_dezolis_grind(Session *s, Report *log);
bool        stage_motavia(Session *s, Report *log);
bool        stage_shir(Session *s, Report *log);
bool        stage_menobe(Session *s, Report *log);
bool        stage_ikuto(Session *s, Report *log);
bool        stage_guaron(Session *s, Report *log);
bool        stage_naval(Session *s, Report *log);
bool        stage_nei_sword(Session *s, Report *log);
bool        stage_kain(Session *s, Report *log);
bool        stage_kain_grind(Session *s, Report *log);
bool        stage_noah_grind(Session *s, Report *log);
bool        stage_boss_party(Session *s, Report *log);
bool        stage_dark_force(Session *s, Report *log);
bool        stage_mother_brain(Session *s, Report *log);
void        no_kain(void);
extern bool pl_no_kain;
int         stage_index(const char *name);
const char *stage_name(int i);

/* (build 179) story.STAGES, and playthrough.py's helpers that
 * computer_plays.py calls too (run.c); story_reset (story.c), pin_head_clear
 * (gear.c): a new Python process's module state, for the window's next
 * COMPUTER PLAYS in the same process. */
typedef bool (*StageFn)(Session *s, Report *log);
int         stage_count(void);
StageFn     stage_fn(int i);
bool        raise_dead(Session *s, Report *log);
const char *resume_point(const char *name, const char *cp, char *buf,
                         size_t n, bool *mid);
void        stage_begins(Session *s, const char *name, int attempt,
                         const char *resumed);
void        party_levels(const Session *s, char *out, size_t n);
void        tick_totals(const Session *s, char *out, size_t n);
void        stage_unwind(Session *s);
void        story_reset(void);
void        pin_head_clear(void);
/* play.ON_SAVE: COMPUTER PLAYS's note of a save on the way (trip_save);
 * NULL for None. */
extern void (*pl_on_save)(const char *kind, const char *stage,
                          const char *path);


bool language_is(Session *s, const char *key);
bool japanese_board(Session *s);
void type_end(Session *s);
bool menu_pick(Session *s, int stop);
bool leave_building(Session *s);

#endif /* PS2_PLAYER_PL_H */
