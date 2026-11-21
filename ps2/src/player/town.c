/* (build 171) play.py's buildings: visit() and the policies it plays a
 * building by -- the hospital, the shops, Data Memory, the clone lab,
 * Rolf's house -- and the C menu's ITEM, TECH and EQP. */

#include "pl.h"

#include <stdlib.h>

/* town_buildings(level)[building]: the first door of it. */
bool town_building(int level, int building, TownBuilding *out)
{
    int n;
    const Door *d = level_doors(level, &n);
    for (int i = 0; i < n; i++) {
        if (d[i].building != PL_NONE && d[i].building == building) {
            out->y = d[i].y;
            out->x = d[i].x;
            out->f764 = d[i].f764;
            out->f766 = d[i].f766;
            return true;
        }
    }
    return false;
}

/* visit: onto the door's cells, then the building by `policy` until the
 * street. False for the Python's None (the door never reached); the
 * scripts shown inside into *shown. */
bool visit(Session *s, int level, int dy, int dx, PolicyFn policy, void *ctx,
           int limit, Ints *shown)
{
    CSet cells = { 0 };
    door_cells(level, dy, dx, &cells);
    if (!goto_cells(s, &cells)) {
        cset_free(&cells);
        return false;
    }
    int before = s->shown.n;
    if (s->state.level == level && ready(s)) {
        const char *d = side_name(cset_get(&cells, s->state.x, s->state.y,
                                           SIDE_NONE));
        sendf(s, "walk %s 1", d ? d : "up");
    }
    cset_free(&cells);
    for (int i = 0; i < limit; i++) {
        const State *st = &s->state;
        if (st_screen(st, "level")) {
            if (st_control(st, "level")) {
                break;
            }
            if (st_waiting(st)) {
                send1(s, "press c");
            } else {
                send1(s, "wait idle");
            }
            continue;
        }
        if (st_screen(st, "battle")) {
            session_fight(s, NULL);
            continue;
        }
        if (!st_waiting(st)) {
            send1(s, "wait idle");
            continue;
        }
        if (st_wait(st, "menu")) {
            if (st->menu == WIN_NAME_INPUT) {
                type_end(s);
                continue;
            }
            int pick = policy(s, ctx);
            if (pick == PICK_B) {
                send1(s, "press b");
            } else if (pick == PICK_C) {
                send1(s, "press c");
            } else {
                menu_pick(s, pick);
            }
            continue;
        }
        send1(s, "press c");
    }
    if (shown != NULL) {
        for (int i = before; i < s->shown.n; i++) {
            ints_add(shown, s->shown.v[i]);
        }
    }
    return true;
}

static bool shown_since(const Session *s, int before, int id)
{
    for (int i = before; i < s->shown.n; i++) {
        if (s->shown.v[i] == id) {
            return true;
        }
    }
    return false;
}

/* ---- the hospital ---- */

static int hospital_policy(Session *s, void *ctx)
{
    (void)ctx;
    const State *st = &s->state;
    if (st->menu == WIN_HEAL_CURE) {
        return 0;                               /* HEAL */
    }
    if (st->menu == WIN_YESNO2) {
        return (s->last_script == 0x0503) ? 0 : 1;
    }
    return PICK_C;
}

static int cure_policy(Session *s, void *ctx)
{
    int target = *(const int *)ctx;
    const State *st = &s->state;
    if (st->menu == WIN_HEAL_CURE) {
        return 1;                               /* CURE */
    }
    if (st->menu == WIN_STORE_CHARS) {
        return target;
    }
    if (st->menu == WIN_YESNO2) {
        return (s->last_script == 0x0503) ? 0 : 1;
    }
    return PICK_C;
}

/* (party or [{}] * (i + 1))[i].get("poison") */
static bool poisoned_at(const Session *s, int i)
{
    if (s->state.party_n == 0) {
        return false;
    }
    if (i >= s->state.party_n) {
        pl_error("IndexError: list index out of range (party)");
    }
    return s->state.party[i].poison;
}

static int first_sick(const Session *s)
{
    for (int i = 0; i < s->state.party_n; i++) {
        if (s->state.party[i].poison && !s->state.party[i].dead) {
            return i;
        }
    }
    return -1;
}

static void cure_poisoned(Session *s, int level, int dy, int dx)
{
    for (int k = 0; k < 4; k++) {
        int sick = first_sick(s);
        if (sick < 0) {
            return;
        }
        visit(s, level, dy, dx, cure_policy, &sick, 80, NULL);
        ready(s);
        if (poisoned_at(s, sick)) {
            return;
        }
    }
}

void heal_here(Session *s, int level)
{
    TownBuilding b;
    if (!town_building(level, BUILDING_HOSPITAL, &b)) {
        return;
    }
    visit(s, level, b.y, b.x, hospital_policy, NULL, 80, NULL);
    cure_poisoned(s, level, b.y, b.x);
}

/* ---- the shops ---- */

typedef struct {
    const Purchase *todo;
    int             n, at;
} ShopCtx;

static int shop_policy(Session *s, void *ctx)
{
    ShopCtx *c = ctx;
    const State *st = &s->state;
    int last = s->last_script, low = last & 0xFF;
    bool left = c->at < c->n;
    if (st->menu == WIN_BUYSELL) {
        return 0;                               /* BUY */
    }
    if (st->menu == WIN_STORE_STOCK) {
        return left ? c->todo[c->at].stock : PICK_B;
    }
    if (st->menu == WIN_STORE_CHARS) {
        return left ? c->todo[c->at].who : PICK_B;
    }
    if (st->menu == WIN_YESNO2) {
        if (last == 0x080A
            || (low == 0x07 && ((last >> 8) == 6 || (last >> 8) == 7))) {
            if (!left) {
                pl_error("IndexError: pop from empty list (shop)");
            }
            c->at++;                            /* bought */
            return (c->at < c->n) ? 0 : 1;
        }
        if (low == 0x03 && ((last >> 8) == 6 || (last >> 8) == 7)) {
            return 1;                           /* "are you joking?" */
        }
        return 1;
    }
    return PICK_C;
}

void buy(Session *s, int level, int dy, int dx, const Purchase *todo, int n)
{
    ShopCtx c = { todo, n, 0 };
    visit(s, level, dy, dx, shop_policy, &c, 40 + 12 * n, NULL);
}

typedef struct {
    const Sale *todo;
    int         n, at;
} SellCtx;

static int sell_policy(Session *s, void *ctx)
{
    SellCtx *c = ctx;
    const State *st = &s->state;
    int last = s->last_script;
    if (st->menu == WIN_BUYSELL) {
        return (c->at < c->n) ? 1 : PICK_B;     /* SELL */
    }
    if (st->menu == WIN_STORE_STOCK) {
        return PICK_B;
    }
    if (st->menu == WIN_STORE_CHARS) {
        return (c->at < c->n) ? c->todo[c->at].who : PICK_B;
    }
    if ((st->menu == WIN_MENU_ITEM_LIST || st->menu == WIN_MENU_ITEM_LIST2)
        && (last == 0x0805 || last == 0x080E) && c->at < c->n) {
        c->at++;                                /* a key item: skip it */
        s->last_script = 0;
    }
    if (st->menu == WIN_MENU_ITEM_LIST) {
        if (c->at >= c->n) {
            return PICK_B;
        }
        int slot = c->todo[c->at].slot;
        if (st->menu_stops != 9) {
            return slot;
        }
        return (slot < 8) ? slot + 1 : 0;       /* 0: NEXT */
    }
    if (st->menu == WIN_MENU_ITEM_LIST2) {
        if (c->at >= c->n) {
            return PICK_B;
        }
        int slot = c->todo[c->at].slot;
        return (slot >= 8) ? slot - 8 + 1 : 0;
    }
    if (st->menu == WIN_YESNO2) {
        if (last == 0x0803) {
            return 0;                           /* the offer: yes */
        }
        if (c->at < c->n) {
            c->at++;
        }
        return (c->at < c->n) ? 0 : 1;          /* anything else? */
    }
    return PICK_C;
}

void sell(Session *s, int level, int dy, int dx, const Sale *todo, int n)
{
    SellCtx c = { todo, n, 0 };
    visit(s, level, dy, dx, sell_policy, &c, 40 + 12 * n, NULL);
}

/* ---- Data Memory ---- */

static int save_policy(Session *s, void *ctx)
{
    int slot = *(const int *)ctx;
    const State *st = &s->state;
    if (st->menu == WIN_YESNO3) {
        return 0;               /* save, overwrite, more adventure: yes */
    }
    if (st->menu == WIN_SAVE_SLOTS) {
        return slot - 1;
    }
    return PICK_C;
}

static bool is_dezolis(int level)
{
    return level == 0x01 || level == 0x02 || level == 0x03 || level == 0x0A
        || level == 0x0B || level == 0x0C;
}

/* rolf_alive_for_shops: on Dezolis a dead Rolf is refused; the clone lab
 * first. */
bool rolf_alive_for_shops(Session *s, int level)
{
    if (!is_dezolis(level)) {
        return true;
    }
    bool dead_rolf = false;
    for (int i = 0; i < s->state.party_n; i++) {
        if (streq(s->state.party[i].name, "Rolf") && s->state.party[i].dead) {
            dead_rolf = true;
        }
    }
    if (s->state.party_n == 0 || !dead_rolf) {
        return true;
    }
    TownBuilding b;
    if (town_building(level, BUILDING_CLONE_LAB, &b)) {
        clone_lab(s, level, b.y, b.x);
    }
    for (int i = 0; i < s->state.party_n; i++) {
        if (streq(s->state.party[i].name, "Rolf") && s->state.party[i].dead) {
            return false;
        }
    }
    return true;
}

void save_at(Session *s, int slot, int level, int dy, int dx)
{
    rolf_alive_for_shops(s, level);
    int before = s->shown.n;
    if (visit(s, level, dy, dx, save_policy, &slot, 80, NULL)
        && shown_since(s, before, 0x0307)) {
        s->save_town = level;
        s->looted.n = 0;
    }
}

void save_paseo(Session *s, int slot)
{
    save_at(s, slot, PASEO, 7, 17);             /* PASEO_DOORS data_memory */
}

/* ---- the clone lab ---- */

static int clone_policy(Session *s, void *ctx)
{
    (void)ctx;
    const State *st = &s->state;
    int dead = -1;
    for (int i = 0; i < st->party_n; i++) {
        if (st->party[i].dead) {
            dead = i;
            break;
        }
    }
    if (st->menu == WIN_STORE_CHARS) {
        return (dead >= 0) ? dead : PICK_B;
    }
    if (st->menu == WIN_YESNO2) {
        if (s->last_script == 0x0403) {
            return 0;
        }
        return (dead >= 0) ? 0 : 1;
    }
    return PICK_C;
}

void clone_lab(Session *s, int level, int dy, int dx)
{
    visit(s, level, dy, dx, clone_policy, NULL, 80, NULL);
}

/* ---- Rolf's house ---- */

#define WIN_HOUSE_OPTIONS 0x29
#define WIN_REGROUP_LIST  0x33

typedef struct {
    const int *take;
    int        n, at;
    bool       regroup;
    bool       seen_knock;
    int        start;           /* -1: None */
    bool       done;
} HouseCtx;

static int house_policy(Session *s, void *ctx)
{
    HouseCtx *c = ctx;
    const State *st = &s->state;
    int last = s->last_script;
    bool todo = c->at < c->n;
    if (c->start < 0) {
        c->start = (s->shown.n - 8 > 0) ? s->shown.n - 8 : 0;
    }
    for (int i = c->start; i < s->shown.n; i++) {
        if (s->shown.v[i] == 0x090C || s->shown.v[i] == 0x0927) {
            c->seen_knock = true;
        }
    }
    if (st->menu == WIN_YESNO3) {
        if (last == 0x090C || last == 0x0927
            || (last >= 0x0913 && last <= 0x0926)) {
            return 1;                           /* keep their name */
        }
        if (last == 0x0911) {
            return todo ? 0 : 1;                /* anyone else? */
        }
        if ((last == 0x090F || last == 0x092E) && c->regroup && todo
            && !c->done) {
            return 1;                           /* not yet: the options */
        }
        return 0;                               /* out again */
    }
    if (st->menu == WIN_HOUSE_OPTIONS) {
        if ((c->seen_knock || c->regroup) && todo && !c->done) {
            c->seen_knock = false;
            c->done = true;
            return 1;                           /* REORGANIZE */
        }
        return 2;                               /* OUTSIDE */
    }
    if (st->menu == WIN_REGROUP_LIST) {
        return todo ? c->take[c->at++] : PICK_B;
    }
    return PICK_C;
}

bool rolf_house(Session *s, const int *take, int n, Ints *shown)
{
    HouseCtx c = { take, n, 0, false, false, -1, false };
    return visit(s, PASEO, 17, 45, house_policy, &c, 80, shown);
}

/* rolf_house(s, take, regroup=True): with nobody new, NO and
 * REORGANIZE. */
bool rolf_house_regroup(Session *s, const int *take, int n, Ints *shown)
{
    HouseCtx c = { take, n, 0, true, false, -1, false };
    return visit(s, PASEO, 17, 45, house_policy, &c, 80, shown);
}

/* ---- the C menu ---- */

#define WIN_PLAYER_MENU      0x01
#define WIN_MENU_ITEM_CHAR   0x02
#define WIN_ITEM_ACTION      0x05
#define WIN_CHOSEN_ITEM_CHAR 0x06
#define WIN_LEVEL_TECH_LIST  0x12
#define WIN_ITEM_LIST2       0x1A
#define WIN_RIGHT_LEFT       0x39
#define WIN_STRNG_CHARS      0x3A
#define MENU_ITEM 0
#define MENU_TECH 2
#define MENU_EQP  4

bool menu_escape(Session *s)
{
    for (int i = 0; i < 8; i++) {
        const State *st = &s->state;
        if (st_control(st, "level")) {
            return true;
        }
        if (st_waiting(st)) {
            send2(s, "press b", "wait idle");
        } else {
            send1(s, "wait idle");
        }
    }
    return false;
}

bool list_pick(Session *s, int slot)
{
    int stops = (s->state.menu_stops == PL_NONE) ? 0 : s->state.menu_stops;
    if (stops != 9) {
        return menu_pick(s, slot);
    }
    if (slot < 8) {
        return menu_pick(s, slot + 1);
    }
    if (!menu_pick(s, 0)) {                     /* NEXT */
        return false;
    }
    send1(s, "wait idle");
    return menu_pick(s, slot - 8 + 1);
}

/* The menus' tail: text answered, the menu backed out of. */
static void menu_tail(Session *s, int rounds)
{
    for (int i = 0; i < rounds; i++) {
        if (st_wait(&s->state, "text")) {
            send2(s, "press c", "wait idle");
        } else if (!st_waiting(&s->state)) {
            send1(s, "wait idle");
        } else {
            break;
        }
    }
    menu_escape(s);
}

static void shown_from(const Session *s, int before, Ints *out)
{
    if (out != NULL) {
        for (int i = before; i < s->shown.n; i++) {
            ints_add(out, s->shown.v[i]);
        }
    }
}

bool equip(Session *s, int member, int slot, int hand, Ints *shown)
{
    if (!ready(s)) {
        return false;
    }
    int before = s->shown.n;
    send2(s, "press c", "wait idle");
    if (s->state.menu != WIN_PLAYER_MENU) {
        menu_escape(s);
        return false;
    }
    menu_pick(s, MENU_EQP);
    send1(s, "wait idle");
    if (s->state.menu == WIN_STRNG_CHARS) {
        menu_pick(s, member);
        send1(s, "wait idle");
    }
    if (s->state.menu == WIN_ITEM_LIST2) {
        list_pick(s, slot);
        send1(s, "wait idle");
    }
    if (s->state.menu == WIN_RIGHT_LEFT) {
        menu_pick(s, hand);
        send1(s, "wait idle");
    }
    if (st_wait(&s->state, "text")) {
        send2(s, "press c", "wait idle");
    }
    menu_escape(s);
    shown_from(s, before, shown);
    return true;
}

/* use_item and give_item: C, ITEM, the holder, the slot, USE or GIVE. */
static bool item_action(Session *s, int holder, int slot, int action,
                        int target, Ints *shown)
{
    if (!ready(s)) {
        return false;
    }
    int before = s->shown.n;
    send2(s, "press c", "wait idle");
    if (s->state.menu != WIN_PLAYER_MENU) {
        menu_escape(s);
        return false;
    }
    menu_pick(s, MENU_ITEM);
    send1(s, "wait idle");
    if (s->state.menu == WIN_MENU_ITEM_CHAR) {
        menu_pick(s, holder);
        send1(s, "wait idle");
    }
    if (s->state.menu == WIN_MENU_ITEM_LIST) {
        list_pick(s, slot);
        send1(s, "wait idle");
    }
    if (s->state.menu == WIN_ITEM_ACTION) {
        menu_pick(s, action);                   /* USE, GIVE */
        send1(s, "wait idle");
    }
    if (s->state.menu == WIN_CHOSEN_ITEM_CHAR) {
        menu_pick(s, target);
        send1(s, "wait idle");
    }
    menu_tail(s, 6);
    shown_from(s, before, shown);
    return true;
}

bool use_item(Session *s, int holder, int slot, int target, Ints *shown)
{
    return item_action(s, holder, slot, 0, (target == PL_NONE) ? holder
                                                                : target,
                       shown);
}

bool give_item(Session *s, int holder, int slot, int to, Ints *shown)
{
    return item_action(s, holder, slot, 1, to, shown);
}

/* (build 172) toss_item: C, ITEM, the holder, the slot, TOSS, YES
 * ($000C "Do you really want to drop", $000B "not a good idea", $000E
 * once dropped). */
bool toss_item(Session *s, int holder, int slot, Ints *shown)
{
    if (!ready(s)) {
        return false;
    }
    int before = s->shown.n;
    send2(s, "press c", "wait idle");
    if (s->state.menu != WIN_PLAYER_MENU) {
        menu_escape(s);
        return false;
    }
    menu_pick(s, MENU_ITEM);
    send1(s, "wait idle");
    if (s->state.menu == WIN_MENU_ITEM_CHAR) {
        menu_pick(s, holder);
        send1(s, "wait idle");
    }
    if (s->state.menu == WIN_MENU_ITEM_LIST) {
        list_pick(s, slot);
        send1(s, "wait idle");
    }
    if (s->state.menu == WIN_ITEM_ACTION) {
        menu_pick(s, 2);                        /* TOSS */
        send1(s, "wait idle");
    }
    for (int i = 0; i < 6; i++) {
        if (s->state.menu == WIN_YESNO) {
            menu_pick(s, 0);                    /* YES */
            send1(s, "wait idle");
        } else if (st_wait(&s->state, "text")) {
            send2(s, "press c", "wait idle");
        } else if (!st_waiting(&s->state)) {
            send1(s, "wait idle");
        } else {
            break;
        }
    }
    menu_escape(s);
    shown_from(s, before, shown);
    return true;
}

bool use_tech(Session *s, int caster, int stop, int target, Ints *shown)
{
    if (!ready(s)) {
        return false;
    }
    int before = s->shown.n;
    send2(s, "press c", "wait idle");
    if (s->state.menu != WIN_PLAYER_MENU) {
        menu_escape(s);
        return false;
    }
    menu_pick(s, MENU_TECH);
    send1(s, "wait idle");
    if (s->state.menu == WIN_MENU_ITEM_CHAR) {
        menu_pick(s, caster);
        send1(s, "wait idle");
    }
    if (s->state.menu == WIN_LEVEL_TECH_LIST) {
        list_pick(s, stop);
        send1(s, "wait idle");
    }
    if (s->state.menu == WIN_CHOSEN_ITEM_CHAR) {
        menu_pick(s, (target == PL_NONE) ? caster : target);
        send1(s, "wait idle");
    }
    menu_tail(s, 8);
    shown_from(s, before, shown);
    return true;
}
