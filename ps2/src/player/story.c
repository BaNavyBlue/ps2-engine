/* (build 171) story.py's stages in C, in its order, paseo to mother_brain
 * (docs/c_player.md), and what they stand on -- the walk to Arima, the
 * mild ground's grind, the chests (loot), the lab, Climatrol and the
 * dams, Skure's caps and the Dezolis towns, Shir's thefts and the Prism's
 * dungeons, the Nei Sword, Kain and Noah's grind, the bosses and the
 * ending, squeak-by and its top-ups, revive_party. */

#include "pl.h"

#include <stdlib.h>

#define ARIMA 0x05

/* (build 179) Each target's own value once, for story_reset too. */
#define ARIMA_TARGET     8
#define LAB_TARGET       17
#define DEZOLIS_GRIND    30
#define DEZOLIS_GRIND_2  33
#define PRISM_TARGETS    { 32, 34, 36, 36 }
#define KAIN_TARGET      20
#define NOAH_TARGET      { { "Rolf", "Kain" }, { 41, 30 }, 2 }
#define CLIMATROL_TARGET { { "Rolf", "Rudo", "Anna" }, { 22, 22, 19 }, 3 }
#define DAM_GRIND_TARGET { { "Rolf", "Rudo", "Amy", "Anna" }, \
                           { 24, 24, 20, 22 }, 4 }

static int g_arima_target = ARIMA_TARGET;
static int g_lab_target = LAB_TARGET;
static int g_dezolis_grind = DEZOLIS_GRIND;     /* Skure's */
static int g_dezolis_grind_2 = DEZOLIS_GRIND_2; /* before Menobe */
/* PRISM_DUNGEONS' targets: menobe, ikuto, guaron, naval */
static const int PRISM_TARGET[4] = PRISM_TARGETS;
static int g_prism_target[4] = PRISM_TARGETS;

/* (build 174) a {name: level} target: CLIMATROL_TARGET, DAM_GRIND_TARGET */
typedef struct {
    const char *name[4];
    int         level[4];
    int         n;
} Target;

/* (build 177) Kain, and Noah's grind */
static int g_kain_target = KAIN_TARGET;
static Target g_noah_target = NOAH_TARGET;
/* KAIN_PARTY: no_kain() makes it NO_KAIN_PARTY (Noah and the bosses with
 * the party the Nei Sword came with) */
static const char *const KAIN_PARTY_KAIN[4] = { "Rolf", "Rudo", "Anna", "Kain" };
static const char *const NO_KAIN_PARTY[4] = { "Rolf", "Rudo", "Amy", "Anna" };
static const char *const *g_kain_party = KAIN_PARTY_KAIN;
/* (build 178) BOSS_PARTY, from tools/boss_trials.py (build 94): Dark Force
 * 11 of 20 and Mother Brain 9 of 10 at the Noah checkpoint's levels --
 * Amy's Deban is what wins. no_kain() makes it NO_KAIN_PARTY too. */
static const char *const BOSS_PARTY_KAIN[4] = { "Rolf", "Rudo", "Amy", "Kain" };
static const char *const *g_boss_party = BOSS_PARTY_KAIN;
bool pl_no_kain;                        /* NO_KAIN */

/* tools/party_trials.py, build 79: Rudo and Anna the pair for Neifirst */
static Target g_climatrol_target = CLIMATROL_TARGET;
/* dam_trials' 450-row levels (build 83) */
static Target g_dam_grind_target = DAM_GRIND_TARGET;

/* (build 179) story.py's module as a new process has it: the story's own
 * targets and parties, no squeak-by, no no_kain, nothing HELD or pinned.
 * The window's COMPUTER PLAYS runs again in the same process. */
void story_reset(void)
{
    pl_squeak = false;
    pl_no_kain = false;
    g_arima_target = ARIMA_TARGET;
    g_lab_target = LAB_TARGET;
    g_dezolis_grind = DEZOLIS_GRIND;
    g_dezolis_grind_2 = DEZOLIS_GRIND_2;
    memcpy(g_prism_target, PRISM_TARGET, sizeof g_prism_target);
    g_kain_target = KAIN_TARGET;
    g_noah_target = (Target)NOAH_TARGET;
    g_climatrol_target = (Target)CLIMATROL_TARGET;
    g_dam_grind_target = (Target)DAM_GRIND_TARGET;
    g_kain_party = KAIN_PARTY_KAIN;
    g_boss_party = BOSS_PARTY_KAIN;
    held_set(NULL, 0);
    pin_head_clear();
}

/* squeak(): every level target down to squeak-by's. */
void squeak(void)
{
    if (pl_squeak) {
        return;
    }
    pl_squeak = true;
    g_arima_target = 7;
    g_lab_target = 15;
    g_climatrol_target = (Target){
        { "Rolf", "Rudo", "Anna" }, { 20, 20, 17 }, 3
    };
    g_dam_grind_target = (Target){
        { "Rolf", "Rudo", "Amy", "Anna" }, { 22, 22, 18, 20 }, 4
    };
    g_dezolis_grind = 28;
    g_dezolis_grind_2 = 31;
    g_kain_target = 18;
    g_noah_target = (Target){ { "Rolf", "Kain" }, { 35, 24 }, 2 };
    for (int i = 0; i < 4; i++) {
        g_prism_target[i] -= 2;
    }
}

/* (build 177) no_kain(): Noah and the bosses with NO_KAIN_PARTY -- the
 * kain stage only Shir's thefts, kain_grind nothing, Noah's grind until
 * Rolf is where Kain's grind left him (NOAH_KAIN_ROLF: 37 under
 * squeak-by, else 41), boss_party nothing to change. After squeak(), or
 * without it. */
void no_kain(void)
{
    if (pl_no_kain) {
        return;
    }
    pl_no_kain = true;
    g_kain_party = NO_KAIN_PARTY;
    g_boss_party = NO_KAIN_PARTY;
    g_noah_target = (Target){ { "Rolf" }, { pl_squeak ? 37 : 41 }, 1 };
}

/* walk_to_arima: Paseo to Arima's edge, fighting what comes. */
static bool walk_to_arima(Session *s, Report *log)
{
    char pl[512];
    const CSet *goals = &level_map(ARIMA)->edges;
    const int tries = 8;                /* ARIMA_WALKS */
    for (int n = 0; n < tries; n++) {
        bool careful = s->careful;
        s->careful = true;
        bool ok = travel(s, ARIMA, goals, log);
        s->careful = careful;
        if (ok) {
            say(log, "the walk to Arima: there, walk %d -- %s", n + 1,
                party_line(s, pl, sizeof pl));
            return true;
        }
        if (wiped(&s->state)) {
            if (!recover(s, log)) {
                return false;
            }
            continue;
        }
        say(log, "the walk to Arima: walk %d back to Paseo -- %s", n + 1,
            party_line(s, pl, sizeof pl));
        if (!retreat(s, PASEO, log)) {
            return false;
        }
    }
    say(log, "the walk to Arima: not there in %d walks", tries);
    return false;
}

bool stage_paseo(Session *s, Report *log)
{
    char pl[512], b[2048];
    new_game(s);
    say(log, "Paseo, a new game: %s", party_line(s, pl, sizeof pl));
    gear_up(s, PASEO, GEAR_RESERVE, log);
    save_paseo(s, 1);
    s->save_town = PASEO;
    if (!walk_to_arima(s, log)) {
        return false;
    }
    heal_here(s, ARIMA);
    gear_up(s, ARIMA, GEAR_RESERVE, log);
    say(log, "Arima: %s", party_line(s, pl, sizeof pl));
    say(log, "bags: %s", bags_repr(s, b, sizeof b));
    return true;
}

/* mild_ground: Motavia's encounter groups 0-2 north of Paseo's tube. */
static Ground *mild_ground(void)
{
    static Ground *g;
    if (g == NULL) {
        static const int GROUPS[3] = { 0, 1, 2 };
        static const Cell ANCHORS[5] = {
            { 66, 22 }, { 112, 10 }, { 112, 63 }, { 73, 46 }, { 89, 52 }
        };
        g = ground_new(MOTAVIA, GROUPS, 3, 73, 46, ANCHORS, 5);
    }
    return g;
}

bool stage_arima(Session *s, Report *log)
{
    char pl[512], b[2048];
    if (!enter_town(s, PASEO)) {
        say(log, "could not get back to Paseo");
        return false;
    }
    Ints shown = { 0 };
    static const int TAKE[2] = { 0, 0 };
    if (party_has(s, "Rudo")) {
        say(log, "Rudo in the party already: the grind goes on");
    } else {
        bool any = rolf_house(s, TAKE, 2, &shown);
        say(log, "Rolf's house: %s", any ? scripts_text(&shown, b, sizeof b)
                                         : "");
    }
    if (!party_has(s, "Rudo")) {
        say(log, "no Rudo at Rolf's house: Arima not reached yet");
        if (!walk_to_arima(s, log) || !enter_town(s, PASEO)) {
            ints_free(&shown);
            return false;
        }
        shown.n = 0;
        bool any = rolf_house(s, TAKE, 2, &shown);
        say(log, "Rolf's house: %s", any ? scripts_text(&shown, b, sizeof b)
                                         : "");
    }
    ints_free(&shown);
    say(log, "party: %s", party_line(s, pl, sizeof pl));
    gear_up(s, PASEO, GEAR_RESERVE, log);
    RoamOpts o = roam_defaults();
    o.log = log;
    bool ok = roam_and_gear(s, PASEO, g_arima_target, mild_ground(), &o);
    char target[32];
    snprintf(target, sizeof target, "reached L%d", g_arima_target);
    say(log, "Paseo grind with Rudo: %s -- %s", ok ? target : "gave up",
        party_line(s, pl, sizeof pl));
    say(log, "bags: %s", bags_repr(s, b, sizeof b));
    return ok;
}

/* ---- (build 172) the chests ---- */

#define LOOT_WIPES 2            /* CONTINUEs one loot() call may make */

typedef struct {
    int level, x, y;
} Chest;

typedef struct {
    Chest *v;
    int    n, cap;
} ChestList;

static void chests_add(ChestList *l, int level, int x, int y)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? 2 * l->cap : 16;
        l->v = pl_grow(l->v, (size_t)l->cap * sizeof *l->v);
    }
    Chest c = { level, x, y };
    l->v[l->n++] = c;
}

static bool chests_has(const ChestList *l, int level, int x, int y)
{
    for (int i = 0; i < l->n; i++) {
        if (l->v[i].level == level && l->v[i].x == x && l->v[i].y == y) {
            return true;
        }
    }
    return false;
}

/* loot's keywords. */
typedef struct {
    bool               key;            /* true */
    int                town;           /* PASEO */
    bool             (*tired)(Session *s);     /* NULL: None */
    const char *const *wanted;
    int                nwanted;
    CSet              *out;            /* each chest's scripts, counted */
} LootOpts;

static LootOpts loot_defaults(void)
{
    LootOpts o;
    memset(&o, 0, sizeof o);
    o.key = true;
    o.town = PASEO;
    return o;
}

static bool is_wanted(const LootOpts *o, const char *item)
{
    for (int i = 0; item != NULL && i < o->nwanted; i++) {
        if (streq(o->wanted[i], item)) {
            return true;
        }
    }
    return false;
}

/* shut_again: after a CONTINUE, the chests holding a wanted item nobody
 * carries, on the floors looted since the last save other than `level`,
 * that the save has shut again -- onto `out`, in the floors' order. */
static void shut_again(Session *s, int level, Report *log, const LootOpts *o,
                       ChestList *out)
{
    char nb[32], c[32];
    s->continued = false;
    int first = out->n;
    Cells cells = { 0 };
    for (int i = 0; i < s->looted.n; i++) {
        int lv = s->looted.v[i];
        if (lv == level) {
            continue;
        }
        chests(lv, &cells);
        for (int k = 0; k < cells.n; k++) {
            const char *item = chest_item(lv, cells.v[k].x, cells.v[k].y);
            if (is_wanted(o, item) && !carried(s, item)
                && chest_shut(s, lv, cells.v[k].x, cells.v[k].y)) {
                chests_add(out, lv, cells.v[k].x, cells.v[k].y);
            }
        }
    }
    cells_free(&cells);
    if (out->n > first) {
        char line[2048];
        size_t used = (size_t)snprintf(line, sizeof line,
                                       "loot %s: after the CONTINUE, shut "
                                       "again: ",
                                       level_name(level, nb, sizeof nb));
        for (int i = first; i < out->n && used < sizeof line; i++) {
            used += (size_t)snprintf(line + used, sizeof line - used,
                                     "%s%s %s", (i > first) ? ", " : "",
                                     level_name(out->v[i].level, nb,
                                                sizeof nb),
                                     xy_repr(out->v[i].x, out->v[i].y, c,
                                             sizeof c));
        }
        say(log, "%s", line);
    }
}

/* loot: every chest on a level, travelling there first. Hurt with no
 * mates left, back to the town and on from there; wiped out, the last
 * save CONTINUEd and the list again (LOOT_WIPES a call). */
static void loot(Session *s, int level, Report *log, const LootOpts *o)
{
    char nb[32], c[32], t[2048], pl[512];
    Cells cells = { 0 };
    chests(level, &cells);
    if (!ints_has(&s->looted, level)) {
        ints_add(&s->looted, level);
    }
    ChestList queue = { 0 };
    if (s->continued) {
        shut_again(s, level, log, o, &queue);
    }
    for (int i = 0; i < cells.n; i++) {
        chests_add(&queue, level, cells.v[i].x, cells.v[i].y);
    }
    int wipes = 0;
    ChestList again_once = { 0 };
    while (queue.n > 0 || wiped(&s->state)) {
        if (wiped(&s->state)) {
            if (queue.n == 0) {
                say(log, "loot %s: wiped out on the way to the last chest",
                    level_name(level, nb, sizeof nb));
            }
            if (wipes >= LOOT_WIPES || !recover(s, log)) {
                break;
            }
            wipes++;
            say(log, "loot %s: the list again after a CONTINUE",
                level_name(level, nb, sizeof nb));
            queue.n = 0;
            shut_again(s, level, log, o, &queue);
            for (int i = 0; i < cells.n; i++) {
                chests_add(&queue, level, cells.v[i].x, cells.v[i].y);
            }
        }
        if (queue.n == 0) {
            pl_error("IndexError: pop from empty list");
        }
        Chest ch = queue.v[0];
        memmove(queue.v, queue.v + 1, (size_t)(queue.n - 1) * sizeof *queue.v);
        queue.n--;
        Ints shown = { 0 };
        bool got = search(s, ch.level, ch.x, ch.y, log, o->key, NULL, &shown);
        if ((!got || shown.n == 0) && s->hurt) {
            say(log, "hurt with nothing left on the way; retreating");
            retreat(s, o->town, log);
            shown.n = 0;
            got = search(s, ch.level, ch.x, ch.y, log, o->key, NULL, &shown);
        }
        if (got && shown.n > 0 && ints_has(&shown, 0x1704)
            && make_room(s, log, PL_NONE) != NULL) {
            /* "But they can't hold any more": a mate tossed, and the
             * chest again */
            search(s, ch.level, ch.x, ch.y, log, o->key, NULL, &shown);
        }
        bool any = got && shown.n > 0;
        if (ch.level == level && o->out != NULL) {
            cset_put(o->out, ch.x, ch.y,
                     cset_get(o->out, ch.x, ch.y, 0) + shown.n);
        }
        say(log, "chest on %s %s: %s", level_name(ch.level, nb, sizeof nb),
            xy_repr(ch.x, ch.y, c, sizeof c),
            any ? scripts_text(&shown, t, sizeof t) : "unreached");
        ints_free(&shown);
        if (!any && s->hurt
            && is_wanted(o, chest_item(ch.level, ch.x, ch.y))
            && !chests_has(&again_once, ch.level, ch.x, ch.y)
            && !wiped(&s->state)) {
            chests_add(&again_once, ch.level, ch.x, ch.y);
            chests_add(&queue, ch.level, ch.x, ch.y);
        }
        if (needs_healing(s, 0.5) && !find_item(s, "Monomate", NULL, NULL)
            && !find_item(s, "Dimate", NULL, NULL)) {
            say(log, "hurt with nothing left; retreating");
            retreat(s, o->town, log);
        } else if (o->tired != NULL && o->tired(s)) {
            say(log, "worn out; retreating -- %s",
                party_line(s, pl, sizeof pl));
            retreat(s, o->town, log);
        }
    }
    free(queue.v);
    free(again_once.v);
    cells_free(&cells);
}

/* ---- (build 172) gear, Shure, Nido, Darum, Oputa, Amy ---- */

#define OPUTA      0x06
#define DARUM_TUBE 0x0E
#define SHURE_G    0x17
#define SHURE_F1   0x18
#define SHURE_F2   0x19
#define SHURE_F3   0x1A
#define NIDO_G     0x1B
#define NIDO_F1    0x1C
#define NIDO_F2    0x1D

/* An int as Python prints it, None for PL_NONE. */
static const char *num(int v, char *buf, size_t n)
{
    if (v == PL_NONE) {
        return "None";
    }
    snprintf(buf, n, "%d", v);
    return buf;
}

/* The scripts shown since `before`. */
static const char *shown_since(const Session *s, int before, char *out,
                               size_t n)
{
    Ints tail = { 0 };
    for (int i = before; i < s->shown.n; i++) {
        ints_add(&tail, s->shown.v[i]);
    }
    scripts_text(&tail, out, n);
    ints_free(&tail);
    return out;
}

/* Arima's weapon store before Shure, a stretch of Arima's field each
 * round, then Paseo's stores and the bags handed round, while Arima's
 * shelf still holds an upgrade. */
static bool stage_gear_rounds(Session *s, Report *log, int rounds)
{
    char pl[512], b[2048];
    bool ok = false;
    for (int rnd = 0; rnd < rounds; rnd++) {
        if (!enter_town(s, ARIMA)) {
            say(log, "could not reach Arima");
            return false;
        }
        RoamOpts o = roam_defaults();
        o.trips = 30;
        o.log = log;
        o.until_geared = true;
        roam_and_gear(s, ARIMA, 0, mild_ground(), &o);
        say(log, "Arima, round %d: %s", rnd, party_line(s, pl, sizeof pl));
        if (!enter_town(s, PASEO)) {
            say(log, "could not get back to Paseo");
            return false;
        }
        heal_here(s, PASEO);
        gear_up(s, PASEO, GEAR_RESERVE, log);
        if (geared(s, ARIMA) && geared(s, PASEO)) {
            ok = true;
            break;
        }
    }
    say(log, "Arima gear: %s -- %s", ok ? "geared" : "not yet",
        party_line(s, pl, sizeof pl));
    say(log, "bags: %s", bags_repr(s, b, sizeof b));
    return ok;
}

bool stage_gear(Session *s, Report *log)
{
    return stage_gear_rounds(s, log, 6);
}

/* The first object on a level of that type (and index, unless -1). */
static bool object_cell(int level, int object, int index, Cell *out)
{
    const LevelObject *v;
    int n = level_objects(level, &v);
    for (int i = 0; i < n; i++) {
        if (v[i].object == object && (index < 0 || v[i].index == index)) {
            out->x = v[i].x;
            out->y = v[i].y;
            return true;
        }
    }
    return false;
}

bool stage_shure(Session *s, Report *log)
{
    char pl[512], b[2048], t[2048];
    /* F3's pocket body first: the Small Key opens the locked chests. */
    Cell body;
    if (!object_cell(SHURE_F3, 0x2F, 0x11, &body)) {
        say(log, "no body on Shure F3?");
        return false;
    }
    Ints shown = { 0 };
    search(s, SHURE_F3, body.x, body.y, log, false, NULL, &shown);
    say(log, "Shure F3 body: %s", scripts_text(&shown, t, sizeof t));
    shown.n = 0;
    search(s, SHURE_F3, body.x, body.y, log, false, NULL, &shown);
    say(log, "Shure F3 body again: %s", scripts_text(&shown, t, sizeof t));
    ints_free(&shown);
    static const int FLOORS[4] = { SHURE_F3, SHURE_F2, SHURE_F1, SHURE_G };
    LootOpts o = loot_defaults();
    for (int i = 0; i < 4; i++) {
        loot(s, FLOORS[i], log, &o);
    }
    say(log, "after Shure: %s", party_line(s, pl, sizeof pl));
    say(log, "bags: %s", bags_repr(s, b, sizeof b));
    return find_item(s, "Dynamite", NULL, NULL);
}

bool stage_nido(Session *s, Report *log)
{
    char pl[512], b[2048], t[2048], nb[32], xs[16], ys[16];
    /* Blasters wipe an under-geared party: Arima's shelf again with
     * Shure's money, then Paseo for the bags. */
    stage_gear(s, log);
    /* The door: stand south of it, face it, Dynamite from the menu. */
    CSet goal = { 0 };
    cset_add(&goal, 53, 46, 0);
    bool there = travel(s, NIDO_G, &goal, log);
    cset_free(&goal);
    if (!there) {
        say(log, "could not reach Nido's door");
        return false;
    }
    send2(s, "press up", "wait idle");
    ready(s);
    Ints shown = { 0 };
    talk(s, 6, &shown);
    say(log, "A on Nido's door: %s", scripts_text(&shown, t, sizeof t));
    int h, sl;
    if (!find_item(s, "Dynamite", &h, &sl)) {
        say(log, "no Dynamite in any bag");
        ints_free(&shown);
        return false;
    }
    shown.n = 0;
    use_item(s, h, sl, PL_NONE, &shown);
    say(log, "Dynamite: %s", scripts_text(&shown, t, sizeof t));
    ints_free(&shown);
    send1(s, "wait idle");
    ready(s);
    {
        int lv = s->state.level, x = s->state.x, y = s->state.y;
        const char *name = level_name(lv, nb, sizeof nb);
        say(log, "after the blast: %s %s %s bags %s", name,
            num(x, xs, sizeof xs), num(y, ys, sizeof ys),
            bags_repr(s, b, sizeof b));
    }
    /* Through the gap: the party is on 54,48 after backing away. */
    cset_add(&goal, 53, 44, 0);
    there = travel(s, NIDO_G, &goal, log);
    cset_free(&goal);
    if (!there) {
        say(log, "could not walk through the blown door");
        return false;
    }
    say(log, "through the door: %s", party_line(s, pl, sizeof pl));
    LootOpts o = loot_defaults();
    loot(s, NIDO_F1, log, &o);
    /* F2: Teim's scene object (object $2A, event $17). */
    Cell scene;
    if (!object_cell(NIDO_F2, 0x2A, -1, &scene)) {
        say(log, "no scene object on Nido F2?");
        return false;
    }
    /* Teim is an A-button event: walk up to her, face her, A, and C
     * through the scene. */
    int before = s->shown.n;
    if (!search(s, NIDO_F2, scene.x, scene.y, log, false, NULL, NULL)) {
        say(log, "could not reach Teim's room");
        return false;
    }
    for (int i = 0; i < 20; i++) {
        if (st_waiting(&s->state) && st_wait(&s->state, "control")) {
            break;
        }
        press_through(s, "level", NULL);
    }
    say(log, "Nido F2 scene: %s", shown_since(s, before, t, sizeof t));
    say(log, "after Nido: %s %s", level_name(s->state.level, nb, sizeof nb),
        party_line(s, pl, sizeof pl));
    say(log, "bags: %s", bags_repr(s, b, sizeof b));
    return true;
}

bool stage_darum(Session *s, Report *log)
{
    char pl[512], t[2048], nb[32], xs[16], ys[16];
    /* Teim in anyone's bag and the tube loaded: LoadSpritesInLevel's tail
     * takes her out and runs Demo_TeimScene. Walk in and play it
     * through. */
    if (!find_item(s, "Teim", NULL, NULL)) {
        say(log, "nobody carries Teim");
        return false;
    }
    int before = s->shown.n;
    if (!enter_town(s, DARUM_TUBE) && s->state.level != DARUM_TUBE) {
        say(log, "could not reach the tube: %s",
            level_name(s->state.level, nb, sizeof nb));
        return false;
    }
    for (int i = 0; i < 40; i++) {
        if (st_control(&s->state, "level")) {
            break;
        }
        press_through(s, "level", NULL);
    }
    say(log, "the tube: %s", shown_since(s, before, t, sizeof t));
    say(log, "after the scene: %s %s %s %s",
        level_name(s->state.level, nb, sizeof nb),
        num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys),
        party_line(s, pl, sizeof pl));
    return !find_item(s, "Teim", NULL, NULL);
}

bool stage_oputa(Session *s, Report *log)
{
    char pl[512], b[2048], nb[32], xs[16], ys[16];
    /* West through the tube to Oputa: its armour store, its weapons, an
     * item store to sell to, its Data Memory for the checkpoint. */
    if (!enter_town(s, OPUTA)) {
        say(log, "could not reach Oputa: %s %s %s",
            level_name(s->state.level, nb, sizeof nb),
            num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys));
        return false;
    }
    say(log, "Oputa: %s", party_line(s, pl, sizeof pl));
    heal_here(s, OPUTA);
    gear_up(s, OPUTA, GEAR_RESERVE, log);
    GrindOpts o = grind_defaults();
    o.trips = 60;
    o.log = log;
    o.until_geared = true;
    bool ok = grind_and_gear(s, OPUTA, 0, &o);
    say(log, "Oputa gear: %s -- %s", ok ? "geared" : "not yet",
        party_line(s, pl, sizeof pl));
    say(log, "bags: %s", bags_repr(s, b, sizeof b));
    return ok;
}

bool stage_amy(Session *s, Report *log)
{
    char pl[512], b[2048];
    /* Oputa seen: the next member knocks at Rolf's house. Into the party
     * (four places), then everyone geared in Oputa again. */
    if (s->state.party_n >= 4) {
        say(log, "four in the party already: the grind goes on");
    } else {
        if (!enter_town(s, PASEO)) {
            say(log, "could not get back to Paseo");
            return false;
        }
        /* The regroup list is everyone joined bar Rolf and Nei, each pick
         * leaving it: stop 0 is Rudo, then stop 0 again the newcomer. */
        static const int TAKE[2] = { 0, 0 };
        Ints shown = { 0 };
        bool any = rolf_house(s, TAKE, 2, &shown);
        say(log, "Rolf's house: %s", any ? scripts_text(&shown, b, sizeof b)
                                         : "");
        ints_free(&shown);
        say(log, "party: %s", party_line(s, pl, sizeof pl));
        if (s->state.party_n < 4) {
            say(log, "nobody new joined");
            return false;
        }
        gear_up(s, PASEO, GEAR_RESERVE, log);
    }
    if (!enter_town(s, OPUTA)) {
        say(log, "could not reach Oputa");
        return false;
    }
    heal_here(s, OPUTA);
    GrindOpts o = grind_defaults();
    o.trips = 60;
    o.log = log;
    o.until_geared = true;
    bool ok = grind_and_gear(s, OPUTA, 0, &o);
    say(log, "Oputa gear with four: %s -- %s", ok ? "geared" : "not yet",
        party_line(s, pl, sizeof pl));
    say(log, "bags: %s", bags_repr(s, b, sizeof b));
    return ok;
}

/* ---- (build 173) the Biosystems Lab ---- */

#define LAB_B1 0x32
#define LAB_G  0x33
#define LAB_F1 0x34
#define LAB_F2 0x35

#define LAB_TRIPS     400
#define LAB_DOOR_FLAG 0xC729            /* biosystems_lab_door_blown */
#define LAB_SPOT_X    47                /* inside the blown door (47,74) */
#define LAB_SPOT_Y    70

/* lab_ground: every cell reached from inside the blown door without a
 * stair or hole, the level's own encounter group; anchors round the
 * floor clockwise. */
static Ground *lab_ground(void)
{
    static Ground *g;
    if (g == NULL) {
        int group = level_enemy_group(LAB_G, false);
        static const Cell ANCHORS[6] = {
            { 47, 64 }, { 75, 57 }, { 22, 57 }, { 2, 36 }, { 12, 22 },
            { 37, 12 }
        };
        g = ground_new(LAB_G, &group, 1, LAB_SPOT_X, LAB_SPOT_Y, ANCHORS, 6);
    }
    return g;
}

/* flag_set: one byte of the $C700 block is 1 (the probe's expect). */
static bool flag_set(Session *s, int addr)
{
    char cmd[32];
    snprintf(cmd, sizeof cmd, "expect flag $%04X 1", addr);
    return !lines_any(send1(s, cmd), "FAIL");
}

static void resupply_at(Session *s, int town)
{
    TownBuilding b;
    if (!town_building(town, BUILDING_ITEM, &b)) {
        pl_error("KeyError: %d", BUILDING_ITEM);
    }
    resupply(s, town, b.y, b.x);
}

/* blow_door: Obj_DestroyableDoor -- stand below it, face it, A ($170D),
 * and Dynamite from the menu. */
static bool blow_door(Session *s, int level, int sx, int sy, Report *log)
{
    char nb[32], t[2048];
    if (!find_item(s, "Dynamite", NULL, NULL)) {
        say(log, "no Dynamite for the door on %s",
            level_name(level, nb, sizeof nb));
        return false;
    }
    CSet goal = { 0 };
    cset_add(&goal, sx, sy, 0);
    bool there = travel(s, level, &goal, log);
    cset_free(&goal);
    if (!there) {
        say(log, "could not reach the door on %s",
            level_name(level, nb, sizeof nb));
        return false;
    }
    send2(s, "press up", "wait idle");
    ready(s);
    Ints shown = { 0 };
    talk(s, 6, &shown);
    say(log, "A on the door on %s: %s", level_name(level, nb, sizeof nb),
        scripts_text(&shown, t, sizeof t));
    int h, sl;
    if (!find_item(s, "Dynamite", &h, &sl)) {
        ints_free(&shown);
        pl_error("TypeError: 'NoneType' object is not subscriptable");
    }
    shown.n = 0;
    use_item(s, h, sl, PL_NONE, &shown);
    say(log, "Dynamite: %s", scripts_text(&shown, t, sizeof t));
    ints_free(&shown);
    s->story_point = true;              /* mark_story: a save on the way */
    send1(s, "wait idle");
    ready(s);
    return true;
}

bool stage_lab_in(Session *s, Report *log)
{
    char pl[512], what[64];
    /* The whole grind from Amy's joining is inside the lab: long walks
     * over the ground floor, gear in Oputa at every return, until
     * everyone is LAB_TARGET. */
    if (!enter_town(s, OPUTA)) {
        say(log, "could not reach Oputa");
        return false;
    }
    heal_here(s, OPUTA);
    gear_up(s, OPUTA, GEAR_RESERVE, log);
    resupply_at(s, OPUTA);
    if (!flag_set(s, LAB_DOOR_FLAG) && !blow_door(s, LAB_G, 47, 74, log)) {
        return false;
    }
    say(log, "inside the lab: %s", party_line(s, pl, sizeof pl));
    DungeonOpts o = dungeon_defaults();
    o.trips = LAB_TRIPS;
    o.log = log;
    o.ground = lab_ground();
    o.gear = true;
    bool ok = dungeon_grind(s, LAB_G, LAB_SPOT_X, LAB_SPOT_Y, OPUTA,
                            g_lab_target, &o);
    if (ok) {
        snprintf(what, sizeof what, "lab grind: everyone L%d", g_lab_target);
    } else {
        snprintf(what, sizeof what, "lab grind: gave up");
    }
    say(log, "%s -- %s", what, party_line(s, pl, sizeof pl));
    retreat(s, OPUTA, log);
    gear_up(s, OPUTA, GEAR_RESERVE, log);
    /* The level is a target, not a gate. */
    return s->state.level == OPUTA;
}

bool stage_lab(Session *s, Report *log)
{
    char pl[512], b[2048], t[2048];
    /* The floors' chests, F2's door (its Dynamite in F2's chest), then
     * the Recorder -- ObjType_Events $12 in B1 -- and straight back
     * out. */
    resupply_at(s, OPUTA);
    s->careful = true;
    LootOpts lo = loot_defaults();
    lo.town = OPUTA;
    loot(s, LAB_F1, log, &lo);
    loot(s, LAB_F2, log, &lo);
    if (!blow_door(s, LAB_F2, 47, 48, log)) {
        s->careful = false;
        return false;
    }
    for (int attempt = 0; attempt < 6; attempt++) {
        if (needs_healing(s, 0.5) && count_mates(s) == 0) {
            retreat(s, OPUTA, log);
        }
        Ints shown = { 0 };
        search_event(s, LAB_B1, 0x12, log, &shown);
        say(log, "the Recorder, try %d: %s -- %s", attempt,
            scripts_text(&shown, t, sizeof t), party_line(s, pl, sizeof pl));
        ints_free(&shown);
        if (find_item(s, "Recorder", NULL, NULL)) {
            break;
        }
        if (!st_screen(&s->state, "level")
            && !st_screen(&s->state, "battle")) {
            say(log, "lost: %s", s->state.valid ? s->state.screen : "None");
            s->careful = false;
            return false;
        }
        retreat(s, OPUTA, log);
    }
    s->careful = false;
    retreat(s, OPUTA, log);
    say(log, "after the lab: %s", party_line(s, pl, sizeof pl));
    say(log, "bags: %s", bags_repr(s, b, sizeof b));
    return find_item(s, "Recorder", NULL, NULL);
}

bool stage_lab_b1(Session *s, Report *log)
{
    char pl[512], c[2048];
    /* B1's three chests, from Oputa each time the mates run out. */
    resupply_at(s, OPUTA);
    s->careful = true;
    CSet out = { 0 };
    LootOpts lo = loot_defaults();
    lo.town = OPUTA;
    lo.out = &out;
    loot(s, LAB_B1, log, &lo);
    s->careful = false;
    retreat(s, OPUTA, log);
    say(log, "after B1: %s", party_line(s, pl, sizeof pl));
    /* The chests are worth having, not the way on. */
    Cell missed[64];
    int nm = 0;
    for (int i = 0; i < out.n && nm < 64; i++) {
        if (out.val[i] == 0) {
            missed[nm++] = cset_cell(&out, i);
        }
    }
    if (nm > 0) {
        cells_repr(missed, nm, c, sizeof c);
        say(log, "B1 chests unreached: %s", c);
    }
    cset_free(&out);
    return s->state.level == OPUTA;
}

static bool top_up_lab(Session *s, Report *log, int trips)
{
    if (!enter_town(s, OPUTA)) {
        return false;
    }
    heal_here(s, OPUTA);
    DungeonOpts o = dungeon_defaults();
    o.trips = trips;
    o.log = log;
    o.ground = lab_ground();
    o.gear = true;
    bool ok = dungeon_grind(s, LAB_G, LAB_SPOT_X, LAB_SPOT_Y, OPUTA, 99, &o);
    retreat(s, OPUTA, log);
    return ok;
}

/* ---- (build 173) Hugh, the Key Tube, Zema, Roron ---- */

#define ZEMA      0x07
#define TUBE_DOOR 0x0F
#define RORON_F5  0x1E
#define RORON_F4  0x1F
#define RORON_F3  0x20
#define RORON_F2  0x21
#define RORON_F1  0x22
#define RORON_G   0x23

/* YES to anything the Governor asks; C through the rest. */
static int gov_policy(Session *s, void *ctx)
{
    (void)s;
    (void)ctx;
    return 0;
}

bool stage_hugh(Session *s, Report *log)
{
    char pl[512], t[2048];
    /* The Recorder raised party_member_join_next to 4: Hugh knocks at
     * Rolf's house. Keep Rudo and Amy (stop 0 twice), then the Recorder
     * to the Governor, who trades it for the Key Tube. */
    if (!enter_town(s, PASEO)) {
        say(log, "could not get back to Paseo");
        return false;
    }
    static const int TAKE[2] = { 0, 0 };
    Ints shown = { 0 };
    rolf_house(s, TAKE, 2, &shown);
    say(log, "Rolf's house: %s", scripts_text(&shown, t, sizeof t));
    bool knocked = ints_has(&shown, 0x090C) || ints_has(&shown, 0x0927);
    say(log, "party: %s", party_line(s, pl, sizeof pl));
    shown.n = 0;
    visit(s, PASEO, 15, 32, gov_policy, NULL, 80, &shown);   /* tower */
    say(log, "the Governor: %s", scripts_text(&shown, t, sizeof t));
    /* $1013, the message the library asks for last (the Japanese ends
     * it there, the US runs on into $1014) */
    bool ok = knocked && ints_has(&shown, 0x1013)
           && find_item(s, "KeyTube", NULL, NULL)
           && !find_item(s, "Recorder", NULL, NULL);
    ints_free(&shown);
    return ok;
}

bool stage_key_tube(Session *s, Report *log)
{
    char pl[512], t[2048], nb[32], xs[16], ys[16];
    /* Who goes first, then the Key Tube on the tube's locked door (level
     * $0F, at 9,15), through, and on to Zema. */
    char party[PL_ROSTER_MAX][16], names[128] = "";
    int np = pick_party(s, log, party);
    size_t used = 0;
    for (int i = 0; i < np && used < sizeof names; i++) {
        used += (size_t)snprintf(names + used, sizeof names - used, "%s%s",
                                 i ? " " : "", party[i]);
    }
    say(log, "party: %s -- %s", names, party_line(s, pl, sizeof pl));
    if (s->state.level == PASEO) {
        heal_here(s, PASEO);
        gear_up(s, PASEO, GEAR_RESERVE, log);
    }
    Ints shown = { 0 };
    search(s, TUBE_DOOR, 9, 15, log, false, NULL, &shown);
    say(log, "the tube's door: %s", scripts_text(&shown, t, sizeof t));
    int h, sl;
    if (find_item(s, "KeyTube", &h, &sl)) {
        shown.n = 0;
        use_item(s, h, sl, PL_NONE, &shown);
        say(log, "Key Tube: %s", scripts_text(&shown, t, sizeof t));
        send1(s, "wait idle");
        ready(s);
    }
    ints_free(&shown);
    if (!enter_town(s, ZEMA)) {
        say(log, "could not reach Zema: %s %s %s",
            level_name(s->state.level, nb, sizeof nb),
            num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys));
        return false;
    }
    say(log, "Zema: %s", party_line(s, pl, sizeof pl));
    return true;
}

bool stage_zema(Session *s, Report *log)
{
    char pl[512], b[2048];
    /* Zema's shelves at once with the lab's money, then its field until
     * the member catching up is within CATCH_UP_GAP of the others. */
    if (!enter_town(s, ZEMA)) {
        say(log, "could not reach Zema");
        return false;
    }
    heal_here(s, ZEMA);
    gear_up(s, ZEMA, GEAR_RESERVE, log);
    const State *st = &s->state;
    int others[PL_PARTY_MAX];
    int n = 0;
    for (int i = 0; i < st->party_n; i++) {
        int v = st->party[i].level;
        int j = n++;
        for (; j > 0 && others[j - 1] > v; j--) {
            others[j] = others[j - 1];
        }
        others[j] = v;
    }
    int target = (n > 1) ? others[1] - CATCH_UP_GAP + 1 : 1;
    GrindOpts o = grind_defaults();
    o.trips = 150;
    o.log = log;
    o.until_geared = true;
    o.lowest = true;
    bool ok = grind_and_gear(s, ZEMA, target, &o);
    say(log, "Zema grind to L%d: %s -- %s", target, ok ? "done" : "gave up",
        party_line(s, pl, sizeof pl));
    say(log, "bags: %s", bags_repr(s, b, sizeof b));
    return ok;
}

bool stage_roron(Session *s, Report *log)
{
    char pl[512], b[2048], t[2048];
    /* The party chosen again, then Roron: every floor's chests, from
     * Zema each time the mates run out, and the jet scooter's maker on
     * F5 (dialogue $87: $1685, the scooter parked outside Paseo). */
    char party[PL_ROSTER_MAX][16];
    pick_party(s, log, party);
    if (!enter_town(s, ZEMA)) {
        say(log, "could not reach Zema");
        return false;
    }
    heal_here(s, ZEMA);
    gear_up(s, ZEMA, GEAR_RESERVE, log);
    resupply_at(s, ZEMA);
    s->careful = true;
    bool got = false;
    static const int FLOORS[6] = {
        RORON_G, RORON_F1, RORON_F2, RORON_F3, RORON_F4, RORON_F5
    };
    LootOpts lo = loot_defaults();
    lo.town = ZEMA;
    for (int i = 0; i < 6; i++) {
        loot(s, FLOORS[i], log, &lo);
    }
    static const int WANT[1] = { 0x1685 };
    for (int attempt = 0; attempt < 4; attempt++) {
        if (needs_healing(s, 0.5) && count_mates(s) == 0) {
            retreat(s, ZEMA, log);
        }
        CSet goal = { 0 };
        cset_add(&goal, 19, 55, 0);
        bool there = travel(s, RORON_F5, &goal, log);
        cset_free(&goal);
        if (!there) {
            say(log, "the maker, try %d: could not reach F5 -- %s", attempt,
                party_line(s, pl, sizeof pl));
            if (!st_screen(&s->state, "level")
                && !st_screen(&s->state, "battle")) {
                s->careful = false;
                return false;
            }
            retreat(s, ZEMA, log);
            continue;
        }
        Ints shown = { 0 };
        talk_to(s, 0x87, WANT, 1, 12, log, &shown);
        say(log, "the maker, try %d: %s", attempt,
            scripts_text(&shown, t, sizeof t));
        bool hit = ints_has(&shown, 0x1685);
        ints_free(&shown);
        if (hit) {
            got = true;
            break;
        }
    }
    s->careful = false;
    retreat(s, ZEMA, log);
    say(log, "after Roron: %s", party_line(s, pl, sizeof pl));
    say(log, "bags: %s", bags_repr(s, b, sizeof b));
    return got;
}

static bool top_up_zema(Session *s, Report *log, int trips)
{
    if (!enter_town(s, ZEMA)) {
        return false;
    }
    GrindOpts o = grind_defaults();
    o.trips = trips;
    o.log = log;
    return grind_and_gear(s, ZEMA, 99, &o);
}

/* ---- (build 173) Uzo and the gum ---- */

#define KUERI       0x08
#define UZO         0x12
#define UNDERWATER  0x13
#define CLIMATROL_G 0x36

/* YES to anything asked (the inventor's "Please give it to me!"). */
static int yes_policy(Session *s, void *ctx)
{
    (void)s;
    (void)ctx;
    return 0;
}

/* find_item's (holder, slot) as Python prints it, None when nobody. */
static const char *item_repr(Session *s, const char *item, char *buf,
                             size_t n)
{
    int h, sl;
    if (!find_item(s, item, &h, &sl)) {
        return "None";
    }
    snprintf(buf, n, "(%d, %d)", h, sl);
    return buf;
}

bool stage_uzo(Session *s, Report *log)
{
    char pl[512], t[2048], b[2048], st[2048], it[32];
    /* From Roron's end: on foot to the scooter, A on it; ride to Uzo's
     * island and get off; the Maruera tree (event $10, the leaf); back
     * on; ride to ground connected to Kueri, get off, and the inventor
     * turns the leaf into Mruragum. */
    char party[PL_ROSTER_MAX][16];
    pick_party(s, log, party);
    if (!board_scooter(s, log)) {
        say(log, "uzo: could not board the scooter: %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    CSet cells = { 0 };
    door_cells(MOTAVIA, 97, 173, &cells);               /* UZO_RECORD */
    bool ok = ride_ashore(s, &cells, NULL, log);
    cset_free(&cells);
    if (!ok) {
        say(log, "uzo: no landing on Uzo's island: %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    bool got = false;
    for (int attempt = 0; attempt < 3; attempt++) {
        Ints shown = { 0 };
        search_event(s, UZO, 0x10, log, &shown);
        say(log, "the Maruera tree, try %d: %s", attempt,
            scripts_text(&shown, t, sizeof t));
        ints_free(&shown);
        if (find_item(s, "MruraLeaf", NULL, NULL)) {
            got = true;
            break;
        }
        if (needs_healing(s, 0.5) && count_mates(s) == 0) {
            say(log, "uzo: hurt with nothing left");
            break;
        }
    }
    if (!got) {
        return false;
    }
    item_repr(s, "MruraLeaf", it, sizeof it);
    say(log, "the leaf: %s %s", it, party_line(s, pl, sizeof pl));
    if (!board_scooter(s, log)) {
        say(log, "uzo: could not get back on the scooter: %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    door_cells(MOTAVIA, 85, 37, &cells);                /* KUERI_RECORD */
    ok = ride_ashore(s, &cells, NULL, log);
    cset_free(&cells);
    if (!ok) {
        say(log, "uzo: no landing for Kueri: %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    if (!enter_town(s, KUERI)) {
        say(log, "could not reach Kueri: %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    heal_here(s, KUERI);
    Ints shown = { 0 };
    visit(s, KUERI, 39, 15, yes_policy, NULL, 80, &shown);  /* inventor */
    say(log, "the inventor: %s", scripts_text(&shown, t, sizeof t));
    ints_free(&shown);
    int gh, gs;
    bool gum = find_item(s, "MruraGum", &gh, &gs);
    if (gum) {
        snprintf(it, sizeof it, "(%d, %d)", gh, gs);
    } else {
        snprintf(it, sizeof it, "None");
    }
    say(log, "the gum: %s bags: %s", it, bags_repr(s, b, sizeof b));
    if (gum) {
        gear_up(s, KUERI, GEAR_RESERVE, log);
        TownBuilding ib;
        if (town_building(KUERI, BUILDING_ITEM, &ib)) {
            resupply(s, KUERI, ib.y, ib.x);
        }
    }
    return gum;
}

bool stage_gum(Session *s, Report *log)
{
    char pl[512], t[2048], st[2048], nb[32], xs[16], ys[16], ls[16];
    /* From Kueri with the gum: on the scooter to the ocean floor's event
     * $F block, Mruragum used facing it (the Underwater Passage), through
     * the passage to Climatrol's ground floor, and back to Kueri. */
    if (party_dead(s)) {
        retreat(s, KUERI, log);
    }
    char party[PL_ROSTER_MAX][16];
    pick_party(s, log, party);
    if (!board_scooter(s, log)) {
        say(log, "gum: could not board the scooter: %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    CSet block = { 0 };
    if (!event_block(MOTAVIA, 0x0F, &block)) {
        pl_error("TypeError: 'NoneType' object is not iterable");
    }
    bool faced = ride_to_face(s, &block, log);
    cset_free(&block);
    if (!faced) {
        say(log, "gum: could not face the ocean floor's spot: %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    int h, sl;
    if (!find_item(s, "MruraGum", &h, &sl)) {
        say(log, "gum: no gum");
        return false;
    }
    Ints shown = { 0 };
    use_item(s, h, sl, PL_NONE, &shown);
    ready(s);
    say(log, "the gum: %s (%s, %s, %s)", scripts_text(&shown, t, sizeof t),
        num(s->state.level, ls, sizeof ls), num(s->state.x, xs, sizeof xs),
        num(s->state.y, ys, sizeof ys));
    ints_free(&shown);
    if (s->state.level != UNDERWATER) {
        return false;
    }
    LootOpts lo = loot_defaults();
    lo.town = KUERI;
    loot(s, UNDERWATER, log, &lo);
    CSet goal = { 0 };
    cset_add(&goal, 82, 8, 0);
    bool ok = travel(s, CLIMATROL_G, &goal, log);
    cset_free(&goal);
    say(log, "Climatrol: %s ('%s', %s, %s) %s", ok ? "True" : "False",
        level_name(s->state.level, nb, sizeof nb),
        num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys),
        party_line(s, pl, sizeof pl));
    if (ok) {
        send1(s, "shot climatrol_arrival");
    }
    retreat(s, KUERI, log);
    return ok;
}

/* ---- (build 174) Climatrol and Neifirst ---- */

#define CLIMATROL_F7 0x3D
#define GRIND_FLOOR  UNDERWATER     /* the passage beside its pipe out */
#define GRIND_SPOT_X 104            /* 101-107 on row 25, clear of the pipe */
#define GRIND_SPOT_Y 25

static const char *const CLIMATROL_PARTY[4] = { "Rolf", "Nei", "Rudo",
                                                "Anna" };
static const char *const CLIMATROL_WALKS[2] = { "walk left 3",
                                                "walk right 3" };

/* reached: everyone named in `t` at their level. */
static bool reached(const Session *s, const Target *t)
{
    const State *st = &s->state;
    for (int k = 0; k < t->n; k++) {
        int lv = 0;
        for (int i = 0; i < st->party_n; i++) {
            if (streq(st->party[i].name, t->name[k])) {
                lv = st->party[i].level;
            }
        }
        if (lv < t->level[k]) {
            return false;
        }
    }
    return true;
}

/* A {name: level} as Python prints the dict. */
static const char *target_repr(const Target *t, char *out, size_t n)
{
    size_t used = (size_t)snprintf(out, n, "{");
    for (int k = 0; k < t->n && used < n; k++) {
        used += (size_t)snprintf(out + used, n - used, "%s'%s': %d",
                                 k ? ", " : "", t->name[k], t->level[k]);
    }
    if (used < n) {
        snprintf(out + used, n - used, "}");
    }
    return out;
}

/* (level_name(level), x, y) as Python prints the tuple. */
static const char *where_repr(const State *st, char *out, size_t n)
{
    char nb[32], xs[16], ys[16];
    snprintf(out, n, "('%s', %s, %s)", level_name(st->level, nb, sizeof nb),
             num(st->x, xs, sizeof xs), num(st->y, ys, sizeof ys));
    return out;
}

/* `id in s.shown[before:]` (before clamped at 0: s.shown[-k:]). */
static bool seen_since(const Session *s, int before, int id)
{
    for (int i = (before > 0) ? before : 0; i < s->shown.n; i++) {
        if (s->shown.v[i] == id) {
            return true;
        }
    }
    return false;
}

/* The last `k` of s.shown with lo <= id < hi (or id == also), as text. */
static const char *shown_tail(const Session *s, int lo, int hi, int also,
                              int k, char *out, size_t n)
{
    Ints pick = { 0 };
    for (int i = 0; i < s->shown.n; i++) {
        int v = s->shown.v[i];
        if ((lo <= v && v < hi) || v == also) {
            ints_add(&pick, v);
        }
    }
    Ints tail = { 0 };
    for (int i = (pick.n > k) ? pick.n - k : 0; i < pick.n; i++) {
        ints_add(&tail, pick.v[i]);
    }
    scripts_text(&tail, out, n);
    ints_free(&pick);
    ints_free(&tail);
    return out;
}

static bool progress_has(const Session *s, const char *what)
{
    for (int i = 0; i < s->progress.n; i++) {
        if (streq(s->progress.v[i], what)) {
            return true;
        }
    }
    return false;
}

/* to_climatrol: from Kueri, the scooter, the gum facing the ocean floor's
 * event $F block, the Underwater Passage. The gum is not eaten
 * (ItemAction_MruraGum: the script and a step), so one lasts. */
static bool to_climatrol(Session *s, Report *log)
{
    char st[2048];
    if (!board_scooter(s, log)) {
        say(log, "to_climatrol: could not board the scooter: %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    CSet block = { 0 };
    if (!event_block(MOTAVIA, 0x0F, &block)) {
        pl_error("TypeError: 'NoneType' object is not iterable");
    }
    bool faced = ride_to_face(s, &block, log);
    cset_free(&block);
    if (!faced) {
        say(log, "to_climatrol: could not face the ocean floor's spot: %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    int h, sl;
    if (!find_item(s, "MruraGum", &h, &sl)) {
        say(log, "to_climatrol: no gum");
        return false;
    }
    use_item(s, h, sl, PL_NONE, NULL);
    ready(s);
    if (s->state.level != UNDERWATER) {
        say(log, "to_climatrol: the gum did not open the passage: %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    return true;
}

/* back_to_climatrol: from anywhere but Climatrol or the passage, Kueri
 * first -- mended, geared, stocked (Trimates: the way out of Climatrol
 * is a long walk) and saved there, so a wipe goes back to Kueri -- then
 * to_climatrol. */
static bool back_to_climatrol(Session *s, Report *log)
{
    int lv = s->state.level;
    if (lv != PL_NONE && ((0x36 <= lv && lv <= 0x3D) || lv == UNDERWATER)) {
        return true;
    }
    if (lv != KUERI && !retreat(s, KUERI, log)) {
        return false;
    }
    heal_here(s, KUERI);
    gear_up(s, KUERI, GEAR_RESERVE, log);
    stock_mates_x(s, KUERI, 12, true, log);
    stock_item(s, KUERI, "Antidote", 2, log);
    TownBuilding dm;
    if (town_building(KUERI, BUILDING_DATA_MEMORY, &dm)) {
        save_at(s, 1, KUERI, dm.y, dm.x);
        trip_save(s, KUERI, log, true);
    }
    return to_climatrol(s, log);
}

/* climatrol_grind: the passage's landing by its pipe, with the way back
 * from Kueri (to_climatrol, which plan() cannot walk); a wipe-out
 * CONTINUEs Kueri's save. True once each member named in `target` is at
 * their level. */
static bool climatrol_grind_body(Session *s, Report *log, const Target *target,
                                 int trips, int fights_per_trip)
{
    char pl[512], st[2048], tr[128], line[1024], slot[300];
    int wipes = 0;
    for (int trip = 0; trip < trips; trip++) {
        if (wiped(&s->state)) {
            /* wiped out: CONTINUE from the last save, Kueri's */
            wipes++;
            snprintf(slot, sizeof slot, "%s/slot1.json", s->saves_dir);
            if (wipes > 5 || !pl_file_exists(slot)
                || !continue_game(s, slot)) {
                say(log, "climatrol_grind: wiped out, and no way back in");
                return false;
            }
            say(log, "climatrol_grind: wiped out (%d); continued the last "
                "save -- %s", wipes, party_line(s, pl, sizeof pl));
        }
        if (reached(s, target)) {
            return true;
        }
        if (!back_to_climatrol(s, log)) {
            return false;
        }
        if (s->state.level != GRIND_FLOOR || s->state.x != GRIND_SPOT_X
            || s->state.y != GRIND_SPOT_Y) {
            CSet goal = { 0 };
            cset_add(&goal, GRIND_SPOT_X, GRIND_SPOT_Y, 0);
            bool there = travel(s, GRIND_FLOOR, &goal, log);
            cset_free(&goal);
            if (!there) {
                say(log, "climatrol_grind: could not reach the landing: %s",
                    state_repr(&s->state, st, sizeof st));
                int lv = (s->state.level == PL_NONE) ? 0 : s->state.level;
                if (!(0x36 <= lv && lv <= 0x3D)) {
                    continue;
                }
                return false;
            }
        }
        int before = s->fights_n;
        wander(s, CLIMATROL_WALKS, 2, fights_per_trip, true);
        mend(s, 0.8);
        size_t used = (size_t)snprintf(line, sizeof line,
                                       "trip %d: %d fights; ", trip,
                                       s->fights_n - before);
        trip_party(&s->state, line, used, sizeof line);
        say(log, "%s", line);
        if (!st_screen(&s->state, "level") && !st_screen(&s->state, "battle")) {
            say(log, "climatrol_grind: lost: %s",
                s->state.valid ? s->state.screen : "?");
            return false;
        }
        /* Out while there is still something in the bags: the way out is
         * on foot through F1, the ground floor and the passage. */
        if (party_dead(s) || needs_healing(s, 0.7) || count_mates(s) < 6) {
            if (!retreat(s, KUERI, log)) {
                if (wiped(&s->state)) {
                    continue;                   /* the top of the loop */
                }
                say(log, "climatrol_grind: stuck on the way out");
                return false;
            }
        }
    }
    if (pl_lean && st_screen(&s->state, "level")) {
        /* (build 98) Out of trips short of the target: F7 with what it
         * has. */
        say(log, "lean: climatrol_grind short of %s -- %s",
            target_repr(target, tr, sizeof tr), party_line(s, pl, sizeof pl));
        return true;
    }
    return false;
}

static bool climatrol_grind(Session *s, Report *log, const Target *target,
                            int trips)
{
    in_grind_begin(s);
    bool ok = climatrol_grind_body(s, log, target, trips, 8);
    in_grind_end(s);
    return ok;
}

/* face_neifirst: on F7's 48,24, face Neifirst, A, C through the talk to
 * the question, YES, and both fights -- Nei alone (she loses: the story),
 * then the party once the picture's $183D/$183E have run. True back on
 * the level with $1827 up. */
static bool face_neifirst(Session *s, Report *log)
{
    char t[2048];
    send2(s, "press down", "wait idle");
    send1(s, "shot neifirst");
    int before = s->shown.n;
    send2(s, "press a", "wait idle");
    for (int i = 0; i < 20; i++) {
        const State *st = &s->state;
        if (st->menu == 0x1F) {
            break;
        }
        if (!st_screen(st, "level") || seen_since(s, before, 0x180E)
            || seen_since(s, before, 0x1822)) {
            say(log, "neifirst: no fight offered: %s",
                shown_since(s, before, t, sizeof t));
            return false;
        }
        send2(s, "press c", "wait idle");
    }
    send2(s, "press c", "wait idle");                   /* YES: $1823 */
    static const char *const INTO[4] = {
        "press c", "limit 3000", "wait screen battle", "wait idle"
    };
    sendv(s, 4, INTO);
    FightOpts fo = fight_defaults();
    fo.run_below = 0.0;
    fo.limit = 400;
    fo.stop_on_level = true;
    Ints shown = { 0 };
    fight_smart(s, &fo, &shown);
    send2(s, "limit 3000", "wait idle");
    if (!st_screen(&s->state, "level")) {
        say(log, "neifirst: after Nei's fight: %s",
            s->state.valid ? s->state.screen : "None");
        ints_free(&shown);
        return false;
    }
    /* The picture: $183D, $183E (Nei lost) -- C through them to the
     * party's fight -- or $1826 (she won; not the story). */
    for (int i = 0; i < 12; i++) {
        if (!st_screen(&s->state, "level") || seen_since(s, before, 0x1826)) {
            break;
        }
        send3(s, "press c", "limit 3000", "wait idle");
    }
    if (st_screen(&s->state, "battle")) {
        fo.limit = 600;
        shown.n = 0;
        fight_smart(s, &fo, &shown);
        send2(s, "limit 3000", "wait idle");
    }
    ints_free(&shown);
    return st_screen(&s->state, "level") && seen_since(s, before, 0x1827);
}

bool stage_climatrol(Session *s, Report *log)
{
    char pl[512], nb[32], tr[128], w[96], t[2048], bs[16];
    /* From Kueri with the gum: the passage to Climatrol, the chests on F2,
     * F3, F5 and F6, the grind, F7 and A on Neifirst, and the death of
     * Nei through to the clone lab in Paseo ($408). */
    if (party_dead(s)) {
        retreat(s, KUERI, log);
    }
    char party[PL_ROSTER_MAX][16];
    pick_party_want(s, log, CLIMATROL_PARTY, 4, party);
    if (!back_to_climatrol(s, log)) {
        return false;
    }
    /* The chests once; nor when this try resumes a save on the way made
     * after them (s.progress). */
    if (!progress_has(s, "climatrol_chests")) {
        static const int FLOORS[4] = { 0x38, 0x39, 0x3B, 0x3C };
        LootOpts lo = loot_defaults();
        lo.town = KUERI;
        for (int i = 0; i < 4; i++) {
            loot(s, FLOORS[i], log, &lo);
            say(log, "%s looted: %s", level_name(FLOORS[i], nb, sizeof nb),
                party_line(s, pl, sizeof pl));
        }
        lines_add(&s->progress, "climatrol_chests");
        s->story_point = true;                  /* mark_story */
    }
    if (!reached(s, &g_climatrol_target)) {
        bool ok = climatrol_grind(s, log, &g_climatrol_target, 120);
        say(log, "Climatrol grind: %s %s -- %s", ok ? "reached" : "gave up",
            target_repr(&g_climatrol_target, tr, sizeof tr),
            party_line(s, pl, sizeof pl));
        if (!ok) {
            return false;
        }
    }
    /* Full, and the bags topped up, before F7. */
    retreat(s, KUERI, log);
    if (needs_healing(s, 0.5)) {
        retreat(s, KUERI, log);
    }
    if (!back_to_climatrol(s, log)) {
        return false;
    }
    CSet goal = { 0 };
    cset_add(&goal, 48, 24, 0);                 /* NEIFIRST_SPOT */
    bool ok = travel(s, CLIMATROL_F7, &goal, log);
    cset_free(&goal);
    say(log, "F7: %s %s %s", ok ? "True" : "False",
        where_repr(&s->state, w, sizeof w), party_line(s, pl, sizeof pl));
    if (!ok) {
        return false;
    }
    if (!face_neifirst(s, log)) {
        return false;
    }
    /* The scene: C through $1827 / $1825 / $1828 to the clone lab. */
    for (int i = 0; i < 30; i++) {
        const State *st = &s->state;
        if (st_screen(st, "building")) {
            break;
        }
        if (st_waiting(st) && st_wait(st, "text")) {
            if (seen_since(s, s->shown.n - 2, 0x1826)
                || seen_since(s, s->shown.n - 2, 0x1827)) {
                sendf(s, "shot nei_death_%d", i);
            }
            send1(s, "press c");
        }
        send2(s, "limit 12000", "wait idle");
    }
    const State *st = &s->state;
    say(log, "after the scene: %s %s %s %s",
        st->valid ? st->screen : "None", num(st->building, bs, sizeof bs),
        shown_since(s, (s->shown.n > 6) ? s->shown.n - 6 : 0, t, sizeof t),
        party_line(s, pl, sizeof pl));
    bool building = st_screen(st, "building");
    send1(s, "shot clone_lab");
    return building && seen_since(s, s->shown.n - 4, 0x0408);
}

static bool top_up_climatrol(Session *s, Report *log, int trips)
{
    static const Target ROLF_99 = { { "Rolf" }, { 99 }, 1 };
    bool ok = climatrol_grind(s, log, &ROLF_99, trips);
    retreat(s, KUERI, log);
    return ok;
}

/* ---- (build 174) the dam party, the cards ---- */

#define PIATA    0x09
#define TOWER_F1 0x3F
#define GAIRA    0x41

/* After Climatrol: tools/dam_trials.py's party (build 80). */
static const char *const DAM_PARTY[4] = { "Rolf", "Rudo", "Amy", "Anna" };

/* A list of the log's lines as Python prints it. */
static const char *lines_repr(const Lines *l, char *out, size_t n)
{
    size_t used = (size_t)snprintf(out, n, "[");
    for (int i = 0; i < l->n && used + 8 < n; i++) {
        const char *v = l->v[i];
        char q = (strchr(v, '\'') != NULL && strchr(v, '"') == NULL)
               ? '"' : '\'';
        used += (size_t)snprintf(out + used, n - used, "%s%c",
                                 i ? ", " : "", q);
        for (const char *p = v; *p != '\0' && used + 8 < n; p++) {
            unsigned char c = (unsigned char)*p;
            if (c == '\\' || c == (unsigned char)q) {
                used += (size_t)snprintf(out + used, n - used, "\\%c", c);
            } else if (c == '\n') {
                used += (size_t)snprintf(out + used, n - used, "\\n");
            } else if (c == '\t') {
                used += (size_t)snprintf(out + used, n - used, "\\t");
            } else if (c == '\r') {
                used += (size_t)snprintf(out + used, n - used, "\\r");
            } else if (c < 0x20 || c == 0x7F) {
                used += (size_t)snprintf(out + used, n - used, "\\x%02x", c);
            } else {
                out[used++] = (char)c;
                out[used] = '\0';
            }
        }
        used += (size_t)snprintf(out + used, n - used, "%c", q);
    }
    if (used < n) {
        snprintf(out + used, n - used, "]");
    }
    return out;
}

bool stage_dam_party(Session *s, Report *log)
{
    char pl[512], r[4096];
    /* From the Climatrol checkpoint (Paseo): the debrief is already done
     * (demo 7, the Governor's $C736 and $C712), so the flags are checked,
     * then Rolf's house for Rudo, Amy and Anna, the hospital and a save. */
    const Lines *out = send2(s, "expect flag $C736 1", "expect flag $C712 1");
    if (lines_any(out, "FAIL")) {
        say(log, "the debrief has not happened: %s",
            lines_repr(out, r, sizeof r));
        return false;
    }
    char party[PL_ROSTER_MAX][16], names[128] = "";
    int np = pick_party_want(s, log, DAM_PARTY, 4, party);
    heal_here(s, PASEO);
    save_paseo(s, 1);
    size_t used = 0;
    for (int i = 0; i < np && used < sizeof names; i++) {
        used += (size_t)snprintf(names + used, sizeof names - used, "%s%s",
                                 i ? " " : "", party[i]);
    }
    say(log, "party: %s -- %s", names, party_line(s, pl, sizeof pl));
    const State *st = &s->state;
    if (st->party_n != 4) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (!streq(st->party[i].name, DAM_PARTY[i])) {
            return false;
        }
    }
    return true;
}

/* to_piata: Piata is not on Paseo's side of the water -- the scooter,
 * ashore by its door record, and in. */
static bool to_piata(Session *s, Report *log)
{
    char st[2048];
    if (s->state.level == PIATA) {
        return true;
    }
    int lv = s->state.valid ? s->state.level : 0;  /* .get("level", 0) */
    if (lv == PL_NONE) {
        pl_error("TypeError: '<' not supported between instances of "
                 "'NoneType' and 'int'");
    }
    if (lv < 0x10 && lv != PIATA) {
        if (!s->state.riding && !board_scooter(s, log)) {
            say(log, "to_piata: could not board the scooter: %s",
                state_repr(&s->state, st, sizeof st));
            return false;
        }
        CSet cells = { 0 };
        door_cells(MOTAVIA, 83, 91, &cells);            /* PIATA_RECORD */
        bool ok = ride_ashore(s, &cells, NULL, log);
        cset_free(&cells);
        if (!ok) {
            say(log, "to_piata: no landing for Piata: %s",
                state_repr(&s->state, st, sizeof st));
            return false;
        }
    }
    return enter_town(s, PIATA);
}

/* NO to hearing him play ($A01); YES to the rest; Rolf in the list. */
static int ustvestia_policy(Session *s, void *ctx)
{
    (void)ctx;
    return (s->last_script == 0x0A01) ? 1 : 0;
}

/* learn_musik: Ustvestia's lesson in Oputa -- Rolf, 2000 meseta, Musik,
 * which the tower's keyboard needs (TechEffect_Musik at event 7 sets
 * $C734, and demo 5 slides F1's shutter open). */
static bool learn_musik(Session *s, Report *log)
{
    char t[2048];
    if (ints_has(&s->shown, 0x0A0C)) {
        return true;
    }
    if (!enter_town(s, OPUTA)) {
        say(log, "musik: could not reach Oputa");
        return false;
    }
    Ints shown = { 0 };
    visit(s, OPUTA, 7, 7, ustvestia_policy, NULL, 80, &shown);
    say(log, "Ustvestia: %s", scripts_text(&shown, t, sizeof t));
    bool ok = ints_has(&shown, 0x0A0C);
    ints_free(&shown);
    return ok;
}

/* play_keyboard: face the keyboard from 47,27 and cast Musik, Rolf's last
 * map technique (a learned Musik goes last: loc_112C4). */
static bool play_keyboard(Session *s, Report *log)
{
    char t[2048];
    CSet stand = { 0 };
    cset_add(&stand, 47, 27, 0);                        /* KEYBOARD_STAND */
    bool there = goto_cells(s, &stand);
    cset_free(&stand);
    if (!there) {
        say(log, "keyboard: could not reach it: %s",
            s->why_set ? s->why : "None");
        return false;
    }
    send2(s, "press up", "wait idle");
    ready(s);
    send2(s, "press c", "wait idle");
    static const char *const TECH[4] = {
        "press down", "press down", "press c", "wait idle"
    };
    sendv(s, 4, TECH);
    send2(s, "press c", "wait idle");                   /* Rolf */
    int stops = s->state.menu_stops;
    if (stops == PL_NONE || stops == 0) {
        stops = 1;
    }
    static const char *const BACK[6] = {
        "press b", "wait idle", "press b", "wait idle", "press b",
        "wait idle"
    };
    sendv(s, 6, BACK);
    ready(s);
    Ints shown = { 0 };
    use_tech(s, 0, stops - 1, PL_NONE, &shown);
    send1(s, "wait idle");
    ready(s);
    say(log, "Musik at the keyboard: %s (stop %d)",
        scripts_text(&shown, t, sizeof t), stops - 1);
    ints_free(&shown);
    return !lines_any(send1(s, "expect flag $C734 1"), "FAIL");
}

bool stage_cards(Session *s, Report *log)
{
    char pl[512], nb[32], t[2048], w[96], it[32], got_s[128] = "";
    /* From Paseo with $C712 set: Piata, mended and geared, and Control
     * Tower F1's four boxes for the Green, Blue, Yellow and Red cards; back
     * to Piata. */
    static const struct {
        int         event;
        const char *card;
    } TOWER_CARDS[4] = {
        { 0x13, "GreenCard" }, { 0x14, "BlueCard" }, { 0x15, "YellowCard" },
        { 0x16, "RedCard" }
    };
    if (!learn_musik(s, log)) {
        return false;
    }
    if (!to_piata(s, log)) {
        say(log, "cards: could not reach Piata: %s",
            level_name(s->state.level, nb, sizeof nb));
        return false;
    }
    heal_here(s, PIATA);
    gear_up(s, PIATA, GEAR_RESERVE, log);
    /* The tower's door (record $3E, building 1, which interact() sets
     * aside): stand at 9,24 and push up. */
    CSet stand = { 0 };
    cset_add(&stand, 9, 24, 0);                     /* PIATA_TOWER_STAND */
    bool there = goto_cells(s, &stand);
    cset_free(&stand);
    if (!there) {
        say(log, "cards: could not reach the tower's door: %s",
            s->why_set ? s->why : "None");
        return false;
    }
    send1(s, "walk up 1");
    ready(s);
    say(log, "the tower: %s", where_repr(&s->state, w, sizeof w));
    CSet goal = { 0 };
    cset_add(&goal, 47, 27, 0);                     /* KEYBOARD_STAND */
    bool ok = travel(s, TOWER_F1, &goal, log);
    cset_free(&goal);
    if (!ok || !play_keyboard(s, log)) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (find_item(s, TOWER_CARDS[i].card, NULL, NULL)) {
            continue;
        }
        Ints shown = { 0 };
        search_event(s, TOWER_F1, TOWER_CARDS[i].event, log, &shown);
        ready(s);
        scripts_text(&shown, t, sizeof t);
        say(log, "the %s's box: %s %s", TOWER_CARDS[i].card, t,
            item_repr(s, TOWER_CARDS[i].card, it, sizeof it));
        ints_free(&shown);
    }
    int got = 0;
    size_t used = 0;
    for (int i = 0; i < 4; i++) {
        if (find_item(s, TOWER_CARDS[i].card, NULL, NULL)) {
            used += (size_t)snprintf(got_s + used, sizeof got_s - used,
                                     "%s%s", got ? " " : "",
                                     TOWER_CARDS[i].card);
            got++;
        }
    }
    say(log, "cards: %s -- %s", got_s, party_line(s, pl, sizeof pl));
    if (!enter_town(s, PIATA)) {
        say(log, "cards: could not get back to Piata");
        return false;
    }
    heal_here(s, PIATA);
    return got == 4;
}

/* ---- (build 174) the dam grind, the dams, Gaira ---- */

/* Each dam: its card; the floor the scooter's push lands on, with the
 * card's door there (Obj_OpenableDoor); the floor with the panel
 * (Obj_InvisibleBlock2, the door's index + 1). ItemAction_*Card. */
typedef struct {
    const char *card;
    int         entry, door_x, door_y, panel_level, panel_event;
    int         out_x, out_y;   /* DAM_ARRIVAL: where the push lands */
} Dam;

static const Dam DAMS[4] = {
    { "GreenCard", 0x30, 95, 25, 0x31, 0x1D, 93, 17 },
    { "BlueCard", 0x2B, 53, 27, 0x2E, 0x1F, 54, 31 },
    { "YellowCard", 0x24, 17, 27, 0x26, 0x21, 10, 25 },
    { "RedCard", 0x29, 47, 17, 0x2A, 0x23, 47, 25 }
};

enum { DAM_FAILED, DAM_DONE, DAM_HURT };    /* "", "done", "hurt" */

/* open_dam: scooter to the dam, the card on its door (unless an earlier
 * trip opened it), the card on its panel ($C72C + index - $1C, the door's
 * flag the byte before). DAM_HURT when s.careful made goto give up. */
static int open_dam(Session *s, const Dam *dam, Report *log)
{
    char pl[512], st[2048], nb[32], t[2048], ls[16], xs[16], ys[16];
    int panel_flag = 0xC72C + dam->panel_event - 0x1C;
    int door_flag = panel_flag - 1;
    if (flag_set(s, panel_flag)) {
        return DAM_DONE;
    }
    if (!find_item(s, dam->card, NULL, NULL)) {
        say(log, "%s: not carried", dam->card);
        return DAM_FAILED;
    }
    s->hurt = false;
    int lv = (s->state.level == PL_NONE) ? 0 : s->state.level;
    if (lv < 0x10) {
        if (!s->state.riding && !board_scooter(s, log)) {
            say(log, "%s: could not board the scooter: %s", dam->card,
                state_repr(&s->state, st, sizeof st));
            return DAM_FAILED;
        }
        if (!ride_into(s, dam->entry, log)) {
            say(log, "%s: could not ride into %s: (%s, %s, %s)", dam->card,
                level_name(dam->entry, nb, sizeof nb),
                num(s->state.level, ls, sizeof ls),
                num(s->state.x, xs, sizeof xs),
                num(s->state.y, ys, sizeof ys));
            return DAM_FAILED;
        }
    }
    Ints shown = { 0 };
    int h, sl;
    if (!flag_set(s, door_flag)) {
        search(s, dam->entry, dam->door_x, dam->door_y, log, false, NULL,
               &shown);
        if (s->hurt) {
            ints_free(&shown);
            return DAM_HURT;
        }
        if (!find_item(s, dam->card, &h, &sl)) {
            say(log, "%s: gone before the door", dam->card);
            ints_free(&shown);
            return DAM_FAILED;
        }
        use_item(s, h, sl, PL_NONE, &shown);
        send1(s, "wait idle");
        ready(s);
        say(log, "%s door: %s", dam->card, scripts_text(&shown, t, sizeof t));
        if (!flag_set(s, door_flag)) {
            say(log, "%s: the door did not open -- %s", dam->card,
                party_line(s, pl, sizeof pl));
            ints_free(&shown);
            return s->hurt ? DAM_HURT : DAM_FAILED;
        }
    }
    shown.n = 0;
    search_event(s, dam->panel_level, dam->panel_event, log, &shown);
    if (s->hurt) {
        ints_free(&shown);
        return DAM_HURT;
    }
    if (!find_item(s, dam->card, &h, &sl)) {
        say(log, "%s: gone before the panel", dam->card);
        ints_free(&shown);
        return DAM_FAILED;
    }
    int before = s->shown.n;
    use_item(s, h, sl, PL_NONE, &shown);
    /* shown + s.shown[before:]: use_item's scripts are in both */
    for (int i = before; i < s->shown.n; i++) {
        ints_add(&shown, s->shown.v[i]);
    }
    say(log, "%s panel: %s %s", dam->card, scripts_text(&shown, t, sizeof t),
        party_line(s, pl, sizeof pl));
    ints_free(&shown);
    return flag_set(s, panel_flag) ? DAM_DONE : DAM_FAILED;
}

/* leave_dam: on foot to where the push landed, and one step down: back
 * out on the water, riding. */
static bool leave_dam(Session *s, const Dam *dam, Report *log)
{
    char pl[512], nb[32], w[96];
    CSet goal = { 0 };
    cset_add(&goal, dam->out_x, dam->out_y, 0);
    bool ok = travel(s, dam->entry, &goal, log);
    cset_free(&goal);
    if (!ok) {
        say(log, "leave_dam: could not get back to %s's way out",
            level_name(dam->entry, nb, sizeof nb));
        return false;
    }
    send1(s, "walk down 1");
    ready(s);
    say(log, "out of the dam: %s %s", where_repr(&s->state, w, sizeof w),
        party_line(s, pl, sizeof pl));
    return s->state.level == MOTAVIA;
}

/* army_eyes_to_gaira: the battle after the fourth panel ($101) nobody
 * wins; the ninth use of their technique is $1219 and Gaira. C through. */
static bool army_eyes_to_gaira(Session *s)
{
    for (int i = 0; i < 400; i++) {
        const State *st = &s->state;
        if (st_screen(st, "level") && st->level == GAIRA) {
            return true;
        }
        if (st_screen(st, "battle") && !st_waiting(st)) {
            send1(s, "wait ticks 240");
        } else if (st_waiting(st)) {
            send1(s, "press c");
        } else {
            send1(s, "wait idle");
        }
    }
    return false;
}

/* escape_gaira: the prison ($1839, 30 s, $1812); to the panel with only
 * RUN in the fights (no weapon, no TP); A on it; the control panel and
 * Tyler's spaceship C'd through; Paseo and the Governor ($1111). */
static bool escape_gaira(Session *s, Report *log)
{
    char pl[512], t[2048], b[2048], xy[32];
    send1(s, "limit 4000");
    for (int i = 0; i < 60; i++) {
        const State *st = &s->state;
        if (ints_has(&s->shown, 0x1812)) {
            break;
        }
        if (st_waiting(st) && !st_wait(st, "control")) {
            send1(s, "press c");
        } else {
            send1(s, "wait ticks 120");
        }
    }
    shown_tail(s, 0x1800, 0x1900, -1, 4, t, sizeof t);
    say(log, "the prison: %s bags: %s", t, bags_repr(s, b, sizeof b));
    ready(s);
    FightOpts run = fight_defaults();
    run.run_below = 2.0;
    run.use_techs = false;
    s->fight_opts = run;
    s->fight_opts_set = true;
    FightOpts out = run;
    out.stop_on_level = true;
    out.stop_on_gameover = true;
    bool ok = false;
    for (int i = 0; i < 12; i++) {
        CSet panel = { 0 };
        cset_add(&panel, 53, 26, 0);                    /* GAIRA_PANEL */
        bool there = goto_cells(s, &panel);
        cset_free(&panel);
        if (there) {
            /* (build 98) A fight can start on the last step: RUN out of
             * it and walk back if it moved. */
            if (st_screen(&s->state, "battle")) {
                Ints shown = { 0 };
                fight_smart(s, &out, &shown);
                ints_free(&shown);
                if (!st_screen(&s->state, "level")) {
                    break;                  /* wiped out, or still in it */
                }
                ready(s);
                if (s->state.x != 53 || s->state.y != 26) {
                    continue;
                }
            }
            ok = st_screen(&s->state, "level");
            break;
        }
        if (st_screen(&s->state, "gameover")) {
            break;
        }
    }
    s->fight_opts_set = false;
    say(log, "Gaira's panel: %s %s %s", ok ? "True" : "False",
        xy_repr(s->state.x, s->state.y, xy, sizeof xy),
        party_line(s, pl, sizeof pl));
    if (!ok) {
        return false;
    }
    static const char *const PANEL[4] = {
        "press up", "wait idle", "press a", "limit 3000"
    };
    sendv(s, 4, PANEL);
    int where = GAIRA;                  /* the battle state names no level */
    for (int i = 0; i < 200; i++) {
        const State *st = &s->state;
        if (st->level != PL_NONE) {
            where = st->level;
        }
        if (st_screen(st, "level") && st->level == PASEO
            && ints_has(&s->shown, 0x1111) && st_control(st, "level")) {
            break;
        }
        if (st_screen(st, "battle") && where == GAIRA) {
            /* (build 98) Still on Gaira, a fight: RUN, not FIGHT. */
            Ints shown = { 0 };
            fight_smart(s, &out, &shown);
            ints_free(&shown);
            continue;
        }
        send1(s, st_waiting(st) ? "press c" : "wait idle");
    }
    shown_tail(s, 0x1900, 0x1920, 0x1111, 6, t, sizeof t);
    say(log, "home: %s bags: %s", t, bags_repr(s, b, sizeof b));
    return s->state.level == PASEO && ints_has(&s->shown, 0x1111);
}

/* out_of_dam: the Escapipe first (back on the scooter at the dam's mouth,
 * route dam_escapipe); without one, leave_dam's walk. */
static bool out_of_dam(Session *s, const Dam *dam, Report *log)
{
    char t[2048], nb[32], xs[16], ys[16];
    int lv = (s->state.level == PL_NONE) ? 0 : s->state.level;
    if (lv < 0x10) {
        return true;
    }
    int h, sl;
    if (find_item(s, "Escapipe", &h, &sl)) {
        Ints shown = { 0 };
        use_item(s, h, sl, PL_NONE, &shown);
        ready(s);
        say(log, "Escapipe: %s ('%s', %s, %s, %s)",
            scripts_text(&shown, t, sizeof t),
            level_name(s->state.level, nb, sizeof nb),
            num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys),
            s->state.riding ? "True" : "False");
        ints_free(&shown);
    }
    lv = (s->state.level == PL_NONE) ? 0 : s->state.level;
    if (lv >= 0x10) {
        return leave_dam(s, dam, log);
    }
    return s->state.level == MOTAVIA;
}

#define DAM_TRIMATES  12
#define DAM_ESCAPIPES 2

/* piata_supply: Piata between trips -- the clone lab for anyone dead, the
 * hospital, Trimates and Escapipes, and a save at the Data Memory, so a
 * wipe continues here and a Telepipe lands here. */
static bool piata_supply(Session *s, Report *log)
{
    if (!to_piata(s, log)) {
        return false;
    }
    TownBuilding cl, dm;
    if (party_dead(s) && town_building(PIATA, BUILDING_CLONE_LAB, &cl)) {
        clone_lab(s, PIATA, cl.y, cl.x);
    }
    heal_here(s, PIATA);
    stock_item(s, PIATA, "Trimate", DAM_TRIMATES, log);
    stock_item(s, PIATA, "Escapipe", DAM_ESCAPIPES, log);
    if (town_building(PIATA, BUILDING_DATA_MEMORY, &dm)) {
        save_at(s, 1, PIATA, dm.y, dm.x);
        s->save_town = PIATA;
    }
    return s->state.level == PIATA;
}

static const char *const WALKS_FIELD[2] = { "walk left 3", "walk right 3" };

/* dam_grind: from Piata, the field by its gate, ten fights a trip, the
 * hospital between, until each member is at `target`. Then Trimates,
 * Escapipes, a save. */
static bool dam_grind_body(Session *s, Report *log, const Target *target,
                           int trips, int fights_per_trip)
{
    char pl[512], tr[128];
    if (!piata_supply(s, log)) {
        return false;
    }
    int wipes = 0;
    for (int trip = 0; trip < trips; trip++) {
        if (wiped(&s->state)) {
            wipes++;
            if (wipes > 5 || !recover(s, log)) {
                return false;
            }
        }
        if (reached(s, target)) {
            break;
        }
        if (s->state.level == PIATA) {
            heal_here(s, PIATA);
            if (!town_exit(s)) {
                say(log, "dam_grind: could not leave Piata");
                return false;
            }
        }
        CSet field = { 0 };
        cset_add(&field, 94, 85, 0);                    /* PIATA_FIELD */
        bool there = goto_cells(s, &field);
        cset_free(&field);
        if (!there) {
            say(log, "dam_grind: could not reach the field: %s",
                s->why_set ? s->why : "None");
            if (st_screen(&s->state, "level") && !enter_town(s, PIATA)) {
                return false;
            }
            continue;
        }
        int before = s->fights_n;
        wander(s, WALKS_FIELD, 2, fights_per_trip, true);
        say(log, "trip %d: %d fights -- %s", trip, s->fights_n - before,
            party_line(s, pl, sizeof pl));
        if (st_screen(&s->state, "level") && !enter_town(s, PIATA)) {
            say(log, "dam_grind: could not get back into Piata");
            return false;
        }
    }
    bool ok = reached(s, target);
    say(log, "dam_grind: %s %s -- %s", ok ? "reached" : "short of",
        target_repr(target, tr, sizeof tr), party_line(s, pl, sizeof pl));
    if (!ok && pl_lean && st_screen(&s->state, "level")) {
        say(log, "lean: going on to the dams");          /* (build 98) */
        ok = true;
    }
    return ok && piata_supply(s, log);
}

static bool dam_grind(Session *s, Report *log, const Target *target,
                      int trips)
{
    in_grind_begin(s);
    bool ok = dam_grind_body(s, log, target, trips, 10);
    in_grind_end(s);
    return ok;
}

bool stage_dam_grind(Session *s, Report *log)
{
    return dam_grind(s, log, &g_dam_grind_target, 80);
}

static bool top_up_piata(Session *s, Report *log, int trips)
{
    static const Target ROLF_99 = { { "Rolf" }, { 99 }, 1 };
    return dam_grind(s, log, &ROLF_99, trips);
}

bool stage_dams(Session *s, Report *log)
{
    char pl[512], st[2048];
    /* The four dams by jet scooter from Piata, each trip supplied there
     * first, with s.careful so goto hands back when someone is hurt and
     * nothing is left to mend with: out by Escapipe, back to Piata, and
     * in again. The Army Eyes after the fourth panel; Gaira; Paseo. */
    const int tries = 8;
    s->save_town = PIATA;
    for (int n = 0; n < 4; n++) {
        const Dam *dam = &DAMS[n];
        bool opened = false;
        for (int attempt = 0; attempt < tries; attempt++) {
            if (wiped(&s->state) && !recover(s, log)) {
                return false;
            }
            if (!piata_supply(s, log)) {
                say(log, "%s: could not get back to Piata", dam->card);
                return false;
            }
            s->careful = true;
            int r = open_dam(s, dam, log);
            s->careful = false;
            if (r == DAM_DONE) {
                say(log, "%s: panel open on trip %d -- %s", dam->card,
                    attempt + 1, party_line(s, pl, sizeof pl));
                opened = true;
                break;
            }
            if (!st_screen(&s->state, "level")
                && !st_screen(&s->state, "battle")) {
                continue;
            }
            if (r != DAM_HURT) {
                return false;
            }
            say(log, "%s: hurt on trip %d, out and back -- %s", dam->card,
                attempt + 1, party_line(s, pl, sizeof pl));
            if (!out_of_dam(s, dam, log)) {
                return false;
            }
        }
        if (!opened) {
            return false;
        }
        if (n < 3 && !out_of_dam(s, dam, log)) {
            return false;
        }
    }
    if (!army_eyes_to_gaira(s)) {
        say(log, "the Army Eyes: not carried off -- %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    return escape_gaira(s, log);
}

/* ---- (build 174) the roof ---- */

#define ROOF_DESK_MENU 0x36     /* WinID_TowerOptions2: ROOM LIBRARY ROOF OUTSIDE */
#define DEZOLIS_SKURE  3

static int roof_policy(Session *s, void *ctx)
{
    (void)ctx;
    int m = s->state.menu;
    if (m == ROOF_DESK_MENU) {
        return 2;                               /* ROOF */
    }
    if (m == WIN_YESNO2) {
        return 0;                               /* YES */
    }
    return PICK_C;
}

bool stage_roof(Session *s, Report *log)
{
    char pl[512], t[2048], nb[32], xs[16], ys[16];
    /* From story_dams.json (Paseo's street after $1111): the Central
     * Tower's desk, ROOF ($C73F, set by Gaira's $2F), $F01's pages, YES,
     * $F02 and the flight to DezolisSkure 60,79. */
    Ints shown = { 0 };
    visit(s, PASEO, 15, 32, roof_policy, NULL, 40, &shown);
    say(log, "the tower's desk and roof: %s",
        scripts_text(&shown, t, sizeof t));
    const State *st = &s->state;
    bool landed = st->level == DEZOLIS_SKURE && st->x == 60 && st->y == 79;
    say(log, "%s %s %s %s %s", landed ? "landed:" : "not landed:",
        level_name(st->level, nb, sizeof nb), num(st->x, xs, sizeof xs),
        num(st->y, ys, sizeof ys), party_line(s, pl, sizeof pl));
    bool ok = landed && ints_has(&shown, 0x0F02) && !ints_has(&shown, 0x0F03);
    ints_free(&shown);
    return ok;
}

/* ---- (build 175) Skure, Aukba and Ryuon, the Esper mansion ---- */

#define SKURE_B2    0x01
#define SKURE_B1    0x02
#define AUKBA       0x0A
#define ZOSA        0x0B
#define RYUON       0x0C
#define ESPER_B1    0x10
#define ESPER_F1    0x11
#define ZOSA_GATE_X 119
#define ZOSA_GATE_Y 24

/* The party position of the member named `who`, or -1. */
static int member_at(const State *st, const char *who)
{
    for (int m = 0; m < st->party_n; m++) {
        if (streq(st->party[m].name, who)) {
            return m;
        }
    }
    return -1;
}

/* wear: `who` puts on the unworn `item` from their own bag, as a person
 * would with EQP; true when they wear it after. */
static bool wear(Session *s, const char *who, const char *item, Report *log)
{
    char t[1024];
    int m = member_at(&s->state, who);
    if (m < 0) {
        return false;
    }
    Bags b;
    bags(s, &b);
    if (m >= b.n) {
        pl_error("IndexError: list index out of range (wear)");
    }
    int slot = -1;
    for (int i = 0; i < b.m[m].n && slot < 0; i++) {
        if (streq(b.m[m].v[i].name, item) && !b.m[m].v[i].worn) {
            slot = i;
        }
    }
    if (slot >= 0) {
        Ints shown = { 0 };
        equip(s, m, slot, 0, &shown);
        say(log, "%s puts on %s: %s", who, item,
            scripts_text(&shown, t, sizeof t));
        ints_free(&shown);
    }
    const char *h = worn_head(s, who);
    return h != NULL && streq(h, item);
}

/* fetch_to: `item` into `who`'s bag from whoever carries it (GIVE). */
static bool fetch_to(Session *s, const char *who, const char *item,
                     Report *log)
{
    char t[1024];
    const State st = s->state;          /* the party before the bags */
    int target = member_at(&st, who);
    int holder, slot;
    bool found = find_item(s, item, &holder, &slot);
    if (target < 0 || !found) {
        return false;
    }
    if (holder == target) {
        return true;
    }
    if (holder >= st.party_n) {
        pl_error("IndexError: list index out of range (fetch_to)");
    }
    Ints shown = { 0 };
    give_item(s, holder, slot, target, &shown);
    say(log, "%s from %s to %s: %s", item, st.party[holder].name, who,
        scripts_text(&shown, t, sizeof t));
    ints_free(&shown);
    Bags b;
    bags(s, &b);
    if (target >= b.n) {
        pl_error("IndexError: list index out of range (fetch_to)");
    }
    for (int i = 0; i < b.m[target].n; i++) {
        if (streq(b.m[target].v[i].name, item)) {
            return true;
        }
    }
    return false;
}

/* cap_for_shops: Rolf alive and in a cap before a Dezolis town's errands
 * (CheckPlanetAndCaps) -- the town's clone lab first if he is dead, then
 * the cap `prefer`red, or the other, fetched to him and put on. The cap
 * he wears after, or NULL. */
static const char *cap_for_shops(Session *s, int level, Report *log,
                                 const char *prefer)
{
    rolf_alive_for_shops(s, level);
    const char *caps[2] = {
        prefer, streq(prefer, "MogicCap") ? "MagicCap" : "MogicCap"
    };
    for (int i = 0; i < 2; i++) {
        const char *h = worn_head(s, "Rolf");
        if (h != NULL && streq(h, caps[i])) {
            return caps[i];
        }
        if (fetch_to(s, "Rolf", caps[i], log)
            && wear(s, "Rolf", caps[i], log)) {
            return caps[i];
        }
    }
    return NULL;
}

/* caps_carried: a Magic or Mogic Cap anywhere in the bags, worn or not,
 * a dead member's too. */
static bool caps_carried(Session *s)
{
    Bags b;
    bags(s, &b);
    for (int m = 0; m < b.n; m++) {
        for (int i = 0; i < b.m[m].n; i++) {
            if (streq(b.m[m].v[i].name, "MagicCap")
                || streq(b.m[m].v[i].name, "MogicCap")) {
                return true;
            }
        }
    }
    return false;
}

/* talk_round: each dialogue number on this level talked to, logged by who
 * said it; how many made sense (something said, and not $1651). */
static int talk_round(Session *s, const int *indices, int n, Report *log)
{
    char who[128], t[2048];
    int level = s->state.level;         /* the round's, wiped out or not */
    int sense = 0;
    for (int i = 0; i < n; i++) {
        Ints shown = { 0 };
        talk_to(s, indices[i], NULL, 0, 12, log, &shown);
        sense += shown.n > 0 && !ints_has(&shown, 0x1651);
        say(log, "  %s: %s", person_name(level, indices[i], who, sizeof who),
            (shown.n > 0) ? scripts_text(&shown, t, sizeof t)
                          : "not reached");
        ints_free(&shown);
    }
    return sense;
}

static int int_cmp(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

/* people_here: the dialogue numbers of the people (type 1) on this level,
 * in order; how many. */
static int people_here(Session *s, int *out, int cap)
{
    static ObjectSlot objs[128];
    int no = objects(s, objs, 128);
    int n = 0;
    for (int i = 0; i < no; i++) {
        if (objs[i].type != 1) {
            continue;
        }
        bool seen = false;
        for (int k = 0; k < n; k++) {
            seen = seen || out[k] == objs[i].index;
        }
        if (!seen && n < cap) {
            out[n++] = objs[i].index;
        }
    }
    qsort(out, (size_t)n, sizeof *out, int_cmp);
    return n;
}

/* zosa_visit: into Zosa; Rolf in `shop_cap` for the shelves (a Magic Cap
 * doubles the prices), the Magic Cap back on after; mates, Trimates,
 * Escapipes; the hospital; Data Memory. */
static bool zosa_visit(Session *s, Report *log, const char *shop_cap)
{
    char nb[32], xs[16], ys[16], pl[512], t[4096];
    if (!enter_town(s, ZOSA)) {
        say(log, "could not reach Zosa: %s %s %s",
            level_name(s->state.level, nb, sizeof nb),
            num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys));
        return false;
    }
    const char *cap = cap_for_shops(s, ZOSA, log, shop_cap);
    if (cap != NULL && streq(cap, shop_cap)) {
        say(log, "shopping in the %s", shop_cap);
    } else {
        say(log, "shopping in the cap on");
    }
    const GearTodo *todo;
    int n = gear_up_got(s, ZOSA, GEAR_RESERVE, log, &todo);
    say(log, "Zosa gear: %s", gear_todo_repr(todo, n, t, sizeof t));
    stock_mates_x(s, ZOSA, 8, true, log);
    stock_item(s, ZOSA, "Escapipe", 3, log);
    wear(s, "Rolf", "MagicCap", log);
    heal_here(s, ZOSA);
    TownBuilding b;
    if (town_building(ZOSA, BUILDING_DATA_MEMORY, &b)) {
        save_at(s, 1, ZOSA, b.y, b.x);
    }
    say(log, "Zosa: %s", party_line(s, pl, sizeof pl));
    return true;
}

bool stage_skure(Session *s, Report *log)
{
    char pl[512], b[2048];
    /* From story_roof.json (DezolisSkure, where the flight lands): B1's
     * chests and the Magic Cap on Rolf -- the Dezolians, the musk cats,
     * the shops, the hospital and Data Memory answer only to a live Rolf
     * in a Magic or Mogic Cap -- B1's cats; Zosa and a grind; B2's chests
     * and cats; Zosa's shelves in the Mogic Cap, the Magic Cap back on,
     * every Zosa Dezolian talked to. */
    pin_head_add("Rolf");
    CSet b1 = { 0 }, b2 = { 0 };
    LootOpts o = loot_defaults();
    o.town = ZOSA;
    o.out = &b1;
    loot(s, SKURE_B1, log, &o);
    fetch_to(s, "Rolf", "MagicCap", log);
    bool capped = wear(s, "Rolf", "MagicCap", log);
    say(log, "%s %s", capped ? "Rolf in the Magic Cap:"
                             : "no Magic Cap on Rolf:",
        party_line(s, pl, sizeof pl));
    say(log, "B1's cats:");
    static const int B1_CATS[3] = { 0x7D, 0x7E, 0x7F };
    talk_round(s, B1_CATS, 3, log);

    /* B2 killed the party from story_roof's supplies: Zosa first, in the
     * cap, and a grind until the leader is DEZOLIS_GRIND. */
    if (!zosa_visit(s, log, "MagicCap")) {
        cset_free(&b1);
        return false;
    }
    GrindOpts g = grind_defaults();
    g.log = log;
    grind_and_gear(s, ZOSA, g_dezolis_grind, &g);
    zosa_visit(s, log, "MagicCap");
    say(log, "after the grind: %s", party_line(s, pl, sizeof pl));

    o.out = &b2;
    loot(s, SKURE_B2, log, &o);
    say(log, "B2's cats:");
    static const int B2_CATS[2] = { 0x7B, 0x7C };
    talk_round(s, B2_CATS, 2, log);
    fetch_to(s, "Rolf", "MogicCap", log);
    fetch_to(s, "Rolf", "MagicCap", log);
    say(log, "chests: %d bags: %s", b1.n + b2.n, bags_repr(s, b, sizeof b));
    cset_free(&b1);
    cset_free(&b2);

    if (!zosa_visit(s, log, "MogicCap")) {
        return false;
    }
    capped = wear(s, "Rolf", "MagicCap", log);
    say(log, "Zosa's Dezolians:");
    int people[8];
    for (int i = 0; i < 8; i++) {
        people[i] = 0x5D + i;
    }
    int sense = talk_round(s, people, 8, log);
    heal_here(s, ZOSA);
    say(log, "made sense: %d of %d %s", sense, 8, party_line(s, pl, sizeof pl));
    return capped && s->state.level == ZOSA;
}

/* dezolis_town_visit: a Dezolis town as the standing rules have it -- in,
 * Rolf in the Mogic Cap for the shelves, mates, Trimates, Escapipes, the
 * hospital, with `talk_too` every Dezolian talked to, then the Magic Cap
 * back on and Data Memory. False for the Python's None. */
static bool dezolis_town_visit(Session *s, int town, Report *log,
                               bool talk_too)
{
    char nb[32], tb[32], xs[16], ys[16], pl[512], t[4096];
    const char *tn = level_name(town, tb, sizeof tb);
    if (!enter_town(s, town)) {
        say(log, "could not reach %s: %s %s %s", tn,
            level_name(s->state.level, nb, sizeof nb),
            num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys));
        return false;
    }
    const char *cap = cap_for_shops(s, town, log, "MogicCap");
    const char *which = (cap == NULL) ? "wrong"
                      : streq(cap, "MogicCap") ? "Mogic" : "Magic";
    say(log, "%s: Rolf in the %s Cap -- %s", tn, which,
        party_line(s, pl, sizeof pl));
    const GearTodo *todo;
    int n = gear_up_got(s, town, GEAR_RESERVE, log, &todo);
    say(log, "%s gear: %s", tn, gear_todo_repr(todo, n, t, sizeof t));
    stock_mates_x(s, town, 8, true, log);
    stock_item(s, town, "Escapipe", 3, log);
    heal_here(s, town);
    if (talk_too) {
        if (st_screen(&s->state, "building")) {
            leave_building(s);
        }
        ready(s);
        int people[128];
        int np = people_here(s, people, 128);
        int sense = talk_round(s, people, np, log);
        say(log, "%s: made sense %d of %d", tn, sense, np);
    }
    fetch_to(s, "Rolf", "MagicCap", log);
    wear(s, "Rolf", "MagicCap", log);
    heal_here(s, town);
    TownBuilding b;
    if (town_building(town, BUILDING_DATA_MEMORY, &b)) {
        save_at(s, 1, town, b.y, b.x);
    }
    return true;
}

bool stage_dezolis_towns(Session *s, Report *log)
{
    /* From story_skure.json (Zosa): Aukba, then Ryuon. */
    pin_head_add("Rolf");
    static const int TOWNS[2] = { AUKBA, RYUON };
    for (int i = 0; i < 2; i++) {
        if (!dezolis_town_visit(s, TOWNS[i], log, true)) {
            return false;
        }
    }
    return s->state.level == RYUON;
}

/* nearest_town: the Dezolis town the fewest level changes from the
 * dungeon's ground floor (plan). */
static int nearest_town(int level)
{
    CSet arrive = { 0 };
    town_arrivals(level, &arrive);
    static const int TOWNS[3] = { ZOSA, AUKBA, RYUON };
    int best = -1, best_n = 0;
    for (int i = 0; i < 3; i++) {
        if (arrive.n == 0) {
            continue;
        }
        /* the least cell, not the set's first */
        Cell start = key_cell((u32)arrive.key[0]);
        for (int k = 1; k < arrive.n; k++) {
            Cell c = key_cell((u32)arrive.key[k]);
            if (c.x < start.x || (c.x == start.x && c.y < start.y)) {
                start = c;
            }
        }
        CSet goals = { 0 };
        town_arrivals(TOWNS[i], &goals);
        Legs legs;
        if (plan(level, start.x, start.y, TOWNS[i], &goals, &legs)) {
            if (best < 0 || legs.n < best_n) {
                best = TOWNS[i];
                best_n = legs.n;
            }
            legs_free(&legs);
        }
        cset_free(&goals);
    }
    cset_free(&arrive);
    return (best < 0) ? ZOSA : best;
}

bool stage_esper(Session *s, Report *log)
{
    char nb[32], xs[16], ys[16], t[2048], b[2048];
    /* The Esper mansion: F1's door Espers ($88: $1687 with Rolf alive and
     * standing), B1's Esper ($8F) twice -- $1697 or $1698, $C743 to 1
     * then 2 -- the scene, Lutz, F1 16,16, and F1's chest: the Prism.
     * Then out to the nearest town's Data Memory. */
    pin_head_add("Rolf");
    for (int i = 0; i < 3; i++) {
        CSet arrive = { 0 };
        town_arrivals(ESPER_F1, &arrive);
        bool there = travel(s, ESPER_F1, &arrive, log);
        cset_free(&arrive);
        if (!there && s->state.level != ESPER_F1) {
            say(log, "could not reach the Esper mansion: %s %s %s",
                level_name(s->state.level, nb, sizeof nb),
                num(s->state.x, xs, sizeof xs),
                num(s->state.y, ys, sizeof ys));
            return false;
        }
        if (s->state.party_n > 0 && !s->state.party[0].dead) {
            break;
        }
        /* Rolf dead at the door: the door Espers stay put */
        revive_party(s, nearest_town(ESPER_F1), log);
    }
    say(log, "Esper mansion F1: %s %s", num(s->state.x, xs, sizeof xs),
        num(s->state.y, ys, sizeof ys));
    static const int DOOR_WANT[2] = { 0x1687, 0x1688 };
    Ints shown = { 0 };
    talk_to(s, 0x88, DOOR_WANT, 2, 12, log, &shown);
    say(log, "door Esper: %s", scripts_text(&shown, t, sizeof t));
    send1(s, "wait ticks 60");
    CSet stand = { 0 };
    cset_add(&stand, 16, 14, 0);
    bool there = travel(s, ESPER_B1, &stand, log);
    cset_free(&stand);
    if (!there && s->state.level != ESPER_B1) {
        say(log, "could not reach B1: %s %s %s",
            level_name(s->state.level, nb, sizeof nb),
            num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys));
        ints_free(&shown);
        return false;
    }
    for (int n = 0; n < 2; n++) {
        shown.n = 0;
        talk_to(s, 0x8F, NULL, 0, 12, log, &shown);
        say(log, "B1's Esper, talk %d: %s", n + 1,
            scripts_text(&shown, t, sizeof t));
        if (ints_has(&shown, 0x1698)) {
            /* $C743 = 2 holds the leader while the scene plays (about
             * $2C0 frames) and opens the building: wait for the screen */
            send2(s, "limit 4000", "wait screen building");
            say(log, "the scene: %s at tick %u", s->state.screen,
                s->state.tick);
            ready_n(s, 200);
            send1(s, "limit 3600");
        }
        if (s->state.level == ESPER_F1) {
            break;
        }
    }
    say(log, "after Lutz: %s %s %s", level_name(s->state.level, nb, sizeof nb),
        num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys));
    if (s->state.level != ESPER_F1) {
        say(log, "no scene: still on %s",
            level_name(s->state.level, nb, sizeof nb));
        ints_free(&shown);
        return false;
    }
    /* The chest is LoadSpritesInLevel's tail's, not in the level's chest
     * list: searched by its cell, 13,15. */
    shown.n = 0;
    search(s, ESPER_F1, 13, 15, log, true, NULL, &shown);
    say(log, "F1's chest: %s", scripts_text(&shown, t, sizeof t));
    ints_free(&shown);
    bool prism = find_item(s, "Prism", NULL, NULL);
    say(log, "the Prism: %s -- %s", prism ? "in the bags" : "not found",
        bags_repr(s, b, sizeof b));
    if (!prism) {
        return false;
    }
    static const int TOWNS[3] = { AUKBA, ZOSA, RYUON };
    for (int i = 0; i < 3; i++) {
        if (enter_town(s, TOWNS[i])) {
            break;
        }
    }
    if (s->state.level == PL_NONE) {
        /* wiped out on the way: a try that did not finish */
        say(log, "the Prism: wiped out on the way -- %s", s->state.screen);
        return false;
    }
    fetch_to(s, "Rolf", "MagicCap", log);
    wear(s, "Rolf", "MagicCap", log);
    heal_here(s, s->state.level);
    /* not saved with anyone dead */
    revive_party(s, s->state.level, log);
    return prism;
}

static bool dezolis_grind_body(Session *s, Report *log, int trips)
{
    char xy[32], pl[512];
    /* Zosa's field before the Prism's dungeons, from open ground near the
     * gate (field_spot): walked from the gate's own cell the trips fought
     * nothing. */
    pin_head_add("Rolf");
    GrindOpts o = grind_defaults();
    o.has_spot = field_spot(DEZOLIS_SKURE, ZOSA_GATE_X, ZOSA_GATE_Y, 3,
                            &o.spot);
    say(log, "grind spot on DezolisSkure: %s",
        o.has_spot ? xy_repr(o.spot.x, o.spot.y, xy, sizeof xy) : "None");
    if (!dezolis_town_visit(s, ZOSA, log, false)) {
        return false;
    }
    o.fights_per_trip = 8;
    o.trips = trips;
    o.log = log;
    bool ok = grind_and_gear(s, ZOSA, g_dezolis_grind_2, &o);
    dezolis_town_visit(s, ZOSA, log, false);
    say(log, "%s L%d: %s", ok ? "reached" : "short of", g_dezolis_grind_2,
        party_line(s, pl, sizeof pl));
    return s->state.level == ZOSA;
}

bool stage_dezolis_grind(Session *s, Report *log)
{
    in_grind_begin(s);
    bool ok = dezolis_grind_body(s, log, 25);
    in_grind_end(s);
    return ok;
}

/* ---- (build 176) Shir, and the Prism's dungeons ---- */

static const char *const MOTAVIA_KEEP[2] = { "Prism", "MogicCap" };

/* What Shir is to steal before the Prism's dungeons: a Visiphone (a save
 * anywhere; the room's list is all Visiphones and wants her at level 10),
 * and the item store's Star Mist and Moon Dew. */
static const Want SHIR_WANTS[3] = {
    { "Visiphone", 1 }, { "StarMist", 1 }, { "MoonDew", 1 }
};
#define SHIR_EXITS 400          /* exits, not seconds: a seed replays */

/* What Shir stole, not what she wears; and (build 99) the commonest
 * things a bag holds, handed to her for room. */
static const char *const STOLEN[5] = {
    "Visiphone", "StarMist", "MoonDew", "Hidapipe", "Trimate"
};
static const char *const SWAP_OUT[7] = {
    "Trimate", "Hidapipe", "Dimate", "Monomate", "Antidote", "Escapipe",
    "Telepipe"
};

static bool name_in(const char *name, const char *const *list, int n)
{
    for (int i = 0; i < n; i++) {
        if (streq(list[i], name)) {
            return true;
        }
    }
    return false;
}

/* A list of names as Python prints it: ['Rolf', 'Rudo']. */
static const char *names_repr(const char *const *names, int n, char *out,
                              size_t cap)
{
    size_t used = (size_t)snprintf(out, cap, "[");
    for (int i = 0; i < n && used < cap; i++) {
        used += (size_t)snprintf(out + used, cap - used, "%s'%s'",
                                 i ? ", " : "", names[i]);
    }
    if (used < cap) {
        snprintf(out + used, cap - used, "]");
    }
    return out;
}

static const char *party_repr(char (*names)[16], int n, char *out, size_t cap)
{
    const char *p[PL_ROSTER_MAX];
    for (int i = 0; i < n && i < PL_ROSTER_MAX; i++) {
        p[i] = names[i];
    }
    return names_repr(p, (n < PL_ROSTER_MAX) ? n : PL_ROSTER_MAX, out, cap);
}

/* keep_on_rolf: each of `items` carried by someone else into Rolf's
 * bag. */
static void keep_on_rolf(Session *s, const char *const *items, int n,
                         Report *log)
{
    for (int i = 0; i < n; i++) {
        if (carried(s, items[i]) == 0) {
            continue;
        }
        Bags b;
        bags(s, &b);
        bool on = false;
        for (int k = 0; b.n > 0 && k < b.m[0].n; k++) {
            on = on || streq(b.m[0].v[k].name, items[i]);
        }
        if (!on) {
            fetch_to(s, "Rolf", items[i], log);
        }
    }
}

bool stage_motavia(Session *s, Report *log)
{
    char pl[512], st[2048];
    /* From Zosa (story_dezolis_grind.json): the Prism and the Mogic Cap to
     * Rolf, home to Paseo by Skure's roof door, Shir into the party and
     * raised to 10 beside Paseo for the Central Tower's room. */
    pin_head_add("Rolf");
    if (find_item(s, "Prism", NULL, NULL)) {
        held_add("Prism");
    }
    keep_on_rolf(s, MOTAVIA_KEEP, 2, log);
    if (!enter_town(s, PASEO)) {
        say(log, "motavia: could not get home to Paseo: %s",
            state_repr(&s->state, st, sizeof st));
        return false;
    }
    say(log, "motavia: in Paseo -- %s", party_line(s, pl, sizeof pl));
    if (!shir_join(s, 3, log)) {
        return false;
    }
    int lv = 0;
    for (int m = 0; m < s->state.party_n; m++) {
        if (streq(s->state.party[m].name, "Shir")) {
            lv = s->state.party[m].level;
            break;
        }
    }
    if (lv < 10 && !raise_member(s, "Shir", 10, PASEO, log, 60)) {
        say(log, "motavia: Shir not raised to 10 -- %s",
            party_line(s, pl, sizeof pl));
        return false;
    }
    enter_town(s, PASEO);
    heal_here(s, PASEO);
    say(log, "motavia: %s", party_line(s, pl, sizeof pl));
    return true;
}

/* empty_shir: what Shir stole (STOLEN, not what she wears) out of her bag
 * to whoever has room, Rolf first -- when she leaves the party her bag
 * stays with her. The names in `first` go first, and one of them finds
 * room when there is none: a SWAP_OUT item from a full bag to hers. */
static void empty_shir(Session *s, Report *log, const char *const *first,
                       int nfirst)
{
    char t[1024];
    for (int round = 0; round < 16; round++) {
        const State st = s->state;
        int h = member_at(&st, "Shir");
        if (h < 0) {
            return;
        }
        Bags b;
        bags(s, &b);
        if (h >= b.n) {
            pl_error("IndexError: list index out of range (empty_shir)");
        }
        /* sorted by (not in first, slot): the first of `first`'s, else
         * the first */
        int slot = -1;
        bool slot_first = false;
        for (int i = 0; i < b.m[h].n; i++) {
            const BagItem *it = &b.m[h].v[i];
            if (it->worn || !name_in(it->name, STOLEN, 5)) {
                continue;
            }
            bool f = name_in(it->name, first, nfirst);
            if (slot < 0 || (f && !slot_first)) {
                slot = i;
                slot_first = f;
            }
        }
        if (slot < 0) {
            return;
        }
        const char *what = b.m[h].v[slot].name;
        int tgt = -1;
        for (int m = 0; m < st.party_n && tgt < 0; m++) {
            if (m == h) {
                continue;
            }
            if (m >= b.n) {
                pl_error("IndexError: list index out of range (empty_shir)");
            }
            if (b.m[m].n < 16) {
                tgt = m;
            }
        }
        if (tgt < 0 && name_in(what, first, nfirst)) {
            int sw = -1, si = -1;
            for (int m = 0; m < st.party_n && sw < 0; m++) {
                if (m == h) {
                    continue;
                }
                if (m >= b.n) {
                    pl_error("IndexError: list index out of range "
                             "(empty_shir)");
                }
                for (int i = b.m[m].n - 1; i >= 0 && sw < 0; i--) {
                    if (!b.m[m].v[i].worn
                        && name_in(b.m[m].v[i].name, SWAP_OUT, 7)) {
                        sw = m;
                        si = i;
                    }
                }
            }
            if (sw >= 0 && b.m[h].n < 16) {
                Ints shown = { 0 };
                give_item(s, sw, si, h, &shown);
                say(log, "%s from %s to Shir, for room: %s",
                    b.m[sw].v[si].name, st.party[sw].name,
                    scripts_text(&shown, t, sizeof t));
                ints_free(&shown);
                continue;
            }
            if (sw >= 0 && make_room(s, log, sw) != NULL) {
                continue;
            }
        }
        if (tgt < 0) {
            say(log, "shir: nobody has room for %s", what);
            return;
        }
        Ints shown = { 0 };
        give_item(s, h, slot, tgt, &shown);
        say(log, "%s from Shir to %s: %s", what, st.party[tgt].name,
            scripts_text(&shown, t, sizeof t));
        ints_free(&shown);
    }
}

static void empty_shir_all(Session *s, Report *log)
{
    empty_shir(s, log, NULL, 0);
}

/* The Prism's dungeons: their entrances on DezolisSkure open only to a
 * party holding the Prism. Floors in the order the chests are reached;
 * the Nei items they hold; a grind target for the leader first. */
typedef struct {
    const char *name;
    int         levels[16];
    int         nlevels;
    const char *wanted[2];
} PrismDungeon;

static const PrismDungeon PRISM_DUNGEONS[4] = {
    { "menobe", { 0x48, 0x49, 0x4A, 0x4B }, 4, { "NeiCrown", "Neimet" } },
    { "ikuto", { 0x52, 0x51, 0x50, 0x4F, 0x4E, 0x4D }, 6,
      { "NeiSlasher", "NeiShot" } },
    { "guaron", { 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B,
                  0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62 }, 16,
      { "NeiCape", "NeiArmor" } },
    { "naval", { 0x43, 0x44 }, 2, { "NeiShield", "NeiEmel" } },
};

bool stage_shir(Session *s, Report *log)
{
    char pl[512], hv[256], names[256], why[1024];
    /* From story_motavia.json: Shir in and out of Paseo's item store and
     * the Central Tower's room until the party carries SHIR_WANTS, up to
     * SHIR_EXITS exits. Then the stolen things to Rolf, the party chosen
     * for Menobe and the tower's roof to DezolisSkure 60,79. */
    pin_head_add("Rolf");
    if (find_item(s, "Prism", NULL, NULL)) {
        held_add("Prism");
    }
    shir_thefts(s, SHIR_WANTS, 3, log, SHIR_EXITS, empty_shir_all, NULL, 0);
    int have[3];
    bool short_of = false;
    size_t used = (size_t)snprintf(hv, sizeof hv, "{");
    for (int i = 0; i < 3; i++) {
        have[i] = carried(s, SHIR_WANTS[i].item);
        short_of = short_of || have[i] < SHIR_WANTS[i].count;
        used += (size_t)snprintf(hv + used, sizeof hv - used, "%s'%s': %d",
                                 i ? ", " : "", SHIR_WANTS[i].item, have[i]);
    }
    snprintf(hv + used, sizeof hv - used, "}");
    say(log, "shir: carried %s -- %s", hv, party_line(s, pl, sizeof pl));
    if (short_of) {
        if (!pl_lean) {
            say(log, "shir: not all stolen yet; rerun with --from shir "
                "--failed");
            return false;
        }
        /* (build 98) the thefts are a bonus, not the story */
        say(log, "lean: not all stolen, going on with %s", hv);
    }
    empty_shir(s, log, NULL, 0);
    keep_on_rolf(s, MOTAVIA_KEEP, 2, log);
    /* Nei is in the roster after Climatrol but never in the party again:
     * the core is Rolf alone. catch_up=False: the party that fights best
     * on the mix; catch_up's choice is logged beside it. */
    static const char *const CORE[1] = { "Rolf" };
    const PrismDungeon *menobe = &PRISM_DUNGEONS[0];
    char alt[PL_ROSTER_MAX][16];
    int na = choose_party_ahead(s, menobe->levels, menobe->nlevels, true,
                                CORE, 1, alt, why, sizeof why);
    say(log, "shir: catch_up would take %s (%s)",
        party_repr(alt, na, names, sizeof names), why);
    char got[PL_ROSTER_MAX][16];
    int ng = pick_party_x(s, log, NULL, 0, menobe->levels, menobe->nlevels,
                          false, CORE, 1, got);
    keep_on_rolf(s, MOTAVIA_KEEP, 2, log);
    say(log, "shir: party for Menobe %s -- %s",
        party_repr(got, ng, names, sizeof names),
        party_line(s, pl, sizeof pl));
    if (!stage_roof(s, log)) {
        return false;
    }
    return find_item(s, "Prism", NULL, NULL);
}

/* worn_out: (build 89) time to go back to town from a Prism dungeon --
 * anyone dead, the party under half its HP, or the members with TP
 * under a third of it between them. */
static bool worn_out(Session *s)
{
    const State *st = &s->state;
    if (st->party_n == 0 || party_dead(s)) {
        return st->party_n > 0;
    }
    long hp = 0, max_hp = 0, tp = 0, max_tp = 0;
    for (int i = 0; i < st->party_n; i++) {
        hp += st->party[i].hp;
        max_hp += st->party[i].max_hp;
        tp += st->party[i].tp;
        max_tp += st->party[i].max_tp;
    }
    double h = (double)hp / (double)((max_hp > 1) ? max_hp : 1);
    double t = max_tp ? (double)tp / (double)max_tp : 1.0;
    return h < 0.5 || t < 0.34;
}

/* prism_dungeon: one of the Prism's dungeons as the standing rules play
 * it -- the nearest town for gear, mates and a save; a grind by it from
 * open ground by its gate until the leader reaches the target; every
 * floor's chests (back to the town when worn out); the town again. */
static bool prism_dungeon(Session *s, int which, Report *log)
{
    char pl[512], nb[32], tb[32], floors[512], hv[128], wv[128];
    const PrismDungeon *p = &PRISM_DUNGEONS[which];
    int target = g_prism_target[which];
    pin_head_add("Rolf");
    if (find_item(s, "Prism", NULL, NULL)) {
        held_add("Prism");
    }
    int town = nearest_town(p->levels[0]);
    size_t used = 0;
    floors[0] = '\0';
    for (int i = 0; i < p->nlevels && used < sizeof floors; i++) {
        used += (size_t)snprintf(floors + used, sizeof floors - used, "%s%s",
                                 i ? " " : "",
                                 level_name(p->levels[i], nb, sizeof nb));
    }
    say(log, "%s: floors %s, town %s, grind to L%d", p->name, floors,
        level_name(town, tb, sizeof tb), target);
    s->careful = true;                  /* goto gives up hurt */
    if (!dezolis_town_visit(s, town, log, false)) {
        return false;
    }
    int nd;
    const Door *d = level_doors(DEZOLIS_SKURE, &nd);
    int gate = -1;
    for (int i = 0; i < nd && gate < 0; i++) {
        if (d[i].has_to_level && d[i].to_level == town) {
            gate = i;
        }
    }
    GrindOpts o = grind_defaults();
    o.fights_per_trip = 8;
    o.trips = 12;
    o.log = log;
    o.has_spot = gate >= 0
              && field_spot(DEZOLIS_SKURE, d[gate].x, d[gate].y, 3, &o.spot);
    grind_and_gear(s, town, target, &o);
    dezolis_town_visit(s, town, log, false);
    say(log, "%s after the grind: %s", p->name, party_line(s, pl, sizeof pl));
    LootOpts lo = loot_defaults();
    lo.town = town;
    lo.tired = worn_out;
    lo.wanted = p->wanted;
    lo.nwanted = 2;
    for (int i = 0; i < p->nlevels; i++) {
        loot(s, p->levels[i], log, &lo);
    }
    const char *have[2];
    int nh = 0;
    for (int i = 0; i < 2; i++) {
        if (carried(s, p->wanted[i]) > 0) {     /* worn counts */
            have[nh++] = p->wanted[i];
        }
    }
    say(log, "%s: Nei items %s of %s -- %s", p->name,
        names_repr(have, nh, hv, sizeof hv),
        names_repr(p->wanted, 2, wv, sizeof wv),
        party_line(s, pl, sizeof pl));
    s->careful = false;                 /* the walk home is the retreat */
    if (!dezolis_town_visit(s, town, log, false)) {
        return false;
    }
    return nh == 2;
}

bool stage_menobe(Session *s, Report *log)
{
    return prism_dungeon(s, 0, log);
}

bool stage_ikuto(Session *s, Report *log)
{
    return prism_dungeon(s, 1, log);
}

bool stage_guaron(Session *s, Report *log)
{
    return prism_dungeon(s, 2, log);
}

bool stage_naval(Session *s, Report *log)
{
    return prism_dungeon(s, 3, log);
}

/* ---- (build 177) the Nei Sword, Kain, and the grind on Noah ---- */

/* Building_EsperMansion (ps2.asm 18940), entered by A on F1's $33 (event
 * $34, loc_E0AA) once $C743 is 3:
 *   $C744 clear: $168F, or -- all eight of NeiEquipmentArray in the
 *     party's bags, worn or not -- $1690-$1694, $C744 = 1 and $C78F = 0,
 *     which puts the Nei Sword in the chest at 13,15.
 *   $C744 set, no Nei Sword carried: $1913 ("Remove the Nei-sword").
 *   $C744 set, the Nei Sword carried: $1911 and YES/NO (WinID_YesNo2).
 *     YES: $1695, then NoahGroundF, x $200, y $6B0.
 *     NO: $1912, then the hospital's HEAL (loc_B9D8): HP and TP full, no
 *     charge -- the mansion is a free inn from here on.
 * NeiSword_ItemSelected (14868): USE on level $63 or later takes the party
 * to EsperMansionF1 16,16. */
#define LUTZ_X  17              /* below $33 at 17,15 */
#define LUTZ_Y  16
#define NOAH_G  0x63

/* lutz: A on F1's $33 from 17,16 and the building played to the end: C
 * through the text, `answer` (0 YES, 1 NO) at $1911's YES/NO. The scripts
 * shown onto `shown`; false for the Python's None (nowhere to stand). YES
 * ends on NoahGroundF. */
static bool lutz(Session *s, int answer, Report *log, Ints *shown)
{
    char nb[32], xs[16], ys[16];
    if (s->state.level != ESPER_F1 || s->state.x != LUTZ_X
        || s->state.y != LUTZ_Y) {
        CSet stand = { 0 };
        cset_add(&stand, LUTZ_X, LUTZ_Y, 0);
        bool there = travel(s, ESPER_F1, &stand, log);
        cset_free(&stand);
        if (!there) {
            say(log, "lutz: could not stand below him: %s %s %s",
                level_name(s->state.level, nb, sizeof nb),
                num(s->state.x, xs, sizeof xs),
                num(s->state.y, ys, sizeof ys));
            return false;
        }
    }
    ready(s);
    int before = s->shown.n;
    static const char *const IN[4] = {
        "press up", "wait idle", "press a", "wait idle"
    };
    sendv(s, 4, IN);
    bool inside = false;
    for (int i = 0; i < 120; i++) {
        const State *st = &s->state;
        if (st_screen(st, "building")) {
            inside = true;
        }
        if (st_control(st, "level") && inside) {
            break;
        }
        if (st_control(st, "level") && !inside) {
            say(log, "lutz: A found nobody at %s %s",
                num(st->x, xs, sizeof xs), num(st->y, ys, sizeof ys));
            break;
        }
        if (!st_waiting(st)) {
            send1(s, "wait idle");
        } else if (st_wait(st, "menu") && st->menu == WIN_YESNO2) {
            menu_pick(s, answer);
            send1(s, "wait idle");
        } else {
            send2(s, "press c", "wait idle");
        }
    }
    for (int i = before; shown != NULL && i < s->shown.n; i++) {
        ints_add(shown, s->shown.v[i]);
    }
    return true;
}

/* KEEP_ON_ROLF: what story.py's make_room leaves in a bag (a spare of one
 * counts as spare). */
static const char *const KEEP_ON_ROLF[6] = {
    "Prism", "Visiphone", "MagicCap", "MogicCap", "NeiSword", "SmallKey"
};

/* make_room_keep: story.py's make_room (not play.py's toss) -- one free
 * slot in `who`'s bag: an unworn item not in KEEP_ON_ROLF given to whoever
 * carries least. The first run's Nei Sword stayed in Anna's bag: Rolf's
 * held sixteen, two of them LaserKnives Shir had stolen. */
static bool make_room_keep(Session *s, const char *who, Report *log)
{
    char t[1024];
    const State st = s->state;          /* the party before the bags */
    int m = member_at(&st, who);
    if (m < 0) {
        return false;
    }
    Bags b;
    bags(s, &b);
    if (m >= b.n) {
        pl_error("IndexError: list index out of range (make_room)");
    }
    if (b.m[m].n < 16) {
        return true;
    }
    const char *seen[BAG_MAX];
    int nseen = 0;
    int spare = -1;
    for (int i = 0; i < b.m[m].n && spare < 0; i++) {
        const char *n = b.m[m].v[i].name;
        if (b.m[m].v[i].worn) {
            continue;
        }
        if (name_in(n, KEEP_ON_ROLF, 6) && !name_in(n, seen, nseen)) {
            seen[nseen++] = n;
            continue;
        }
        spare = i;
    }
    int to = -1;
    for (int k = 0; k < st.party_n; k++) {
        if (k == m) {
            continue;
        }
        if (k >= b.n) {
            pl_error("IndexError: list index out of range (make_room)");
        }
        if (b.m[k].n < 16 && (to < 0 || b.m[k].n < b.m[to].n)) {
            to = k;
        }
    }
    if (spare < 0 || to < 0) {
        say(log, "make_room: nothing to move out of %s's bag", who);
        return false;
    }
    Ints shown = { 0 };
    give_item(s, m, spare, to, &shown);
    say(log, "%s from %s to %s: %s", b.m[m].v[spare].name, who,
        st.party[to].name, scripts_text(&shown, t, sizeof t));
    ints_free(&shown);
    Bags after;
    bags(s, &after);
    if (m >= after.n) {
        pl_error("IndexError: list index out of range (make_room)");
    }
    return after.m[m].n < 16;
}

/* wear_hand: `who` puts on a one-hand `item` from their bag into the
 * empty hand if one is empty (gear_up's rule), else over the first. */
static bool wear_hand(Session *s, const char *who, const char *item,
                      Report *log)
{
    char t[1024];
    int m = member_at(&s->state, who);
    if (m < 0) {
        return false;
    }
    Bags b;
    bags(s, &b);
    if (m >= b.n) {
        pl_error("IndexError: list index out of range (wear_hand)");
    }
    const Bag *bag = &b.m[m];
    int slot = -1;
    bool worn = false;
    int hands = 0;
    for (int i = 0; i < bag->n; i++) {
        bool is = streq(bag->v[i].name, item);
        if (is && !bag->v[i].worn && slot < 0) {
            slot = i;
        }
        worn = worn || (is && bag->v[i].worn);
        int k = kind_of(bag->v[i].name);
        if (bag->v[i].worn && (k == KIND_HAND || k == KIND_BOTH)) {
            hands++;
        }
    }
    if (slot < 0) {
        return worn;
    }
    int hand = (hands == 1 && kind_of(item) == KIND_HAND) ? 1 : 0;
    Ints shown = { 0 };
    equip(s, m, slot, hand, &shown);
    say(log, "%s puts on %s (hand %d): %s", who, item, hand,
        scripts_text(&shown, t, sizeof t));
    ints_free(&shown);
    Bags after;
    bags(s, &after);
    if (m >= after.n) {
        pl_error("IndexError: list index out of range (wear_hand)");
    }
    for (int i = 0; i < after.m[m].n; i++) {
        if (streq(after.m[m].v[i].name, item) && after.m[m].v[i].worn) {
            return true;
        }
    }
    return false;
}

/* lutz_heal: NO to Lutz with the Nei Sword carried -- $1912 and a free
 * HEAL. */
static bool lutz_heal(Session *s, Report *log)
{
    char t[1024], pl[512];
    Ints shown = { 0 };
    lutz(s, 1, log, &shown);
    say(log, "Lutz, NO: %s -- %s", scripts_text(&shown, t, sizeof t),
        party_line(s, pl, sizeof pl));
    bool ok = ints_has(&shown, 0x1912);
    ints_free(&shown);
    return ok;
}

bool stage_nei_sword(Session *s, Report *log)
{
    char nb[32], xs[16], ys[16], t[1024], b[4096];
    /* From Ryuon (story_naval.json), the eight Nei items in the bags: the
     * Esper mansion, Lutz ($1690-$1694 and $C744), F1's chest (the Nei
     * Sword, Rolf's only: equip mask 1), Rolf in it, Lutz again and NO
     * ($1912 and the HEAL), then the nearest town as the standing rules
     * have it and its Data Memory in the Magic Cap. */
    pin_head_add("Rolf");
    CSet arrive = { 0 };
    town_arrivals(ESPER_F1, &arrive);
    bool there = travel(s, ESPER_F1, &arrive, log);
    cset_free(&arrive);
    if (!there && s->state.level != ESPER_F1) {
        say(log, "nei_sword: could not reach the Esper mansion: %s %s %s",
            level_name(s->state.level, nb, sizeof nb),
            num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys));
        return false;
    }
    Ints shown = { 0 };
    lutz(s, 1, log, &shown);
    say(log, "Lutz: %s", scripts_text(&shown, t, sizeof t));
    bool in = ints_has(&shown, 0x1690) || ints_has(&shown, 0x1911)
           || ints_has(&shown, 0x1913);
    ints_free(&shown);
    if (!in) {
        say(log, "nei_sword: not all eight -- %s", bags_repr(s, b, sizeof b));
        return false;
    }
    if (carried(s, "NeiSword") == 0) {
        Ints got = { 0 };
        search(s, ESPER_F1, 13, 15, log, true, NULL, &got);
        say(log, "F1's chest: %s", scripts_text(&got, t, sizeof t));
        ints_free(&got);
    }
    if (carried(s, "NeiSword") == 0) {
        say(log, "nei_sword: no Nei Sword -- %s", bags_repr(s, b, sizeof b));
        return false;
    }
    make_room_keep(s, "Rolf", log);
    fetch_to(s, "Rolf", "NeiSword", log);
    if (!wear_hand(s, "Rolf", "NeiSword", log)) {
        Bags bb;
        bags(s, &bb);
        if (bb.n == 0) {
            pl_error("IndexError: list index out of range (nei_sword)");
        }
        say(log, "nei_sword: Rolf is not wearing it -- %s",
            bag_repr(&bb.m[0], b, sizeof b));
        return false;
    }
    Bags bb;
    bags(s, &bb);
    if (bb.n == 0) {
        pl_error("IndexError: list index out of range (nei_sword)");
    }
    const char *worn[BAG_MAX];
    int nw = 0;
    for (int i = 0; i < bb.m[0].n; i++) {
        if (bb.m[0].v[i].worn) {
            worn[nw++] = bb.m[0].v[i].name;
        }
    }
    say(log, "Rolf wears: %s", names_repr(worn, nw, b, sizeof b));
    lutz_heal(s, log);
    int town = nearest_town(ESPER_F1);
    if (!dezolis_town_visit(s, town, log, false)) {
        return false;
    }
    return carried(s, "NeiSword") > 0;
}

/* Kain, and the grind before Noah (build 91 of the Python): on Noah,
 * Rudo+Anna+Kain behind Rolf is behind Rudo+Amy+Anna up to about 80 grind
 * fights and ahead from about 160 (Kain L28), and Kain's parties manage
 * about 3.4 fights a trip at L10 -- so he is raised first somewhere
 * cheaper, then the party grinds on NoahGroundF from Lutz's YES: out by
 * the Nei Sword when worn out, NO at Lutz for the free HEAL (which leaves
 * the dead dead: those go to a clone lab). The party can only change at
 * Rolf's house in Paseo, so the trip home is also Shir's: a bounded round
 * of thefts for the Moon Dews and Star Mists Noah wants. */
static const Want NOAH_THEFTS[2] = { { "MoonDew", 2 }, { "StarMist", 4 } };
#define NOAH_THEFT_EXITS 300    /* store exits; 116-254 in build 98 */
static const char *const CONSUMABLES[10] = {
    "Monomate", "Dimate", "Trimate", "StarMist", "MoonDew", "Antidote",
    "Escapipe", "Telepipe", "Hidapipe", "Visiphone"
};

/* keep_from: before `leaving` stays at Rolf's house -- a bag stays with
 * its owner -- their unworn consumables, and every unworn piece no store
 * sells that one of `joining` can wear (Amy's Neimet is Kain's), go to
 * whoever stays, the emptiest bag first. What stayed behind (the unworn
 * consumables still in their bag) into `left`; how many. */
static int keep_from(Session *s, const char *leaving,
                     const char *const *joining, int njoining, Report *log,
                     char (*left)[24])
{
    char t[1024];
    int ids[4];
    for (int j = 0; j < njoining; j++) {
        ids[j] = char_id(joining[j]);
        if (ids[j] < 0) {
            pl_error("KeyError: '%s' (CHAR_ID)", joining[j]);
        }
    }
    struct {
        int  slot;
        char name[24];
    } tried[24];
    int ntried = 0;
    for (int round = 0; round < 24; round++) {
        const State st = s->state;
        int h = member_at(&st, leaving);
        if (h < 0) {
            return 0;
        }
        Bags b;
        bags(s, &b);
        if (h >= b.n) {
            pl_error("IndexError: list index out of range (keep_from)");
        }
        int slot = -1;
        for (int i = b.m[h].n - 1; i >= 0 && slot < 0; i--) {
            const char *n = b.m[h].v[i].name;
            bool was = false;
            for (int k = 0; k < ntried; k++) {
                was = was || (tried[k].slot == i && streq(tried[k].name, n));
            }
            if (b.m[h].v[i].worn || was) {
                continue;
            }
            bool unique = false;
            if (!sold_anywhere(n)) {
                for (int j = 0; j < njoining; j++) {
                    unique = unique || wearable(n, ids[j]);
                }
            }
            if (name_in(n, CONSUMABLES, 10) || unique) {
                slot = i;
            }
        }
        if (slot < 0) {
            break;
        }
        tried[ntried].slot = slot;
        snprintf(tried[ntried].name, sizeof tried[ntried].name, "%s",
                 b.m[h].v[slot].name);
        ntried++;
        int to = -1;
        for (int k = 0; k < st.party_n; k++) {
            if (k == h || streq(st.party[k].name, "Shir")) {
                continue;
            }
            if (k >= b.n) {
                pl_error("IndexError: list index out of range (keep_from)");
            }
            if (b.m[k].n < 16 && (to < 0 || b.m[k].n < b.m[to].n)) {
                to = k;
            }
        }
        if (to < 0) {
            say(log, "keep_from: no room for %s", b.m[h].v[slot].name);
            break;
        }
        Ints shown = { 0 };
        give_item(s, h, slot, to, &shown);
        say(log, "%s from %s to %s: %s", b.m[h].v[slot].name, leaving,
            st.party[to].name, scripts_text(&shown, t, sizeof t));
        ints_free(&shown);
    }
    Bags b;
    bags(s, &b);
    int h = member_at(&s->state, leaving);
    if (h < 0) {
        return 0;
    }
    if (h >= b.n) {
        pl_error("IndexError: list index out of range (keep_from)");
    }
    int n = 0;
    for (int i = 0; i < b.m[h].n; i++) {
        if (!b.m[h].v[i].worn && name_in(b.m[h].v[i].name, CONSUMABLES, 10)) {
            snprintf(left[n++], 24, "%s", b.m[h].v[i].name);
        }
    }
    return n;
}

/* A keep_from list as Python prints it. */
static const char *left_repr(char (*left)[24], int n, char *out, size_t cap)
{
    const char *p[BAG_MAX];
    for (int i = 0; i < n; i++) {
        p[i] = left[i];
    }
    return names_repr(p, n, out, cap);
}

bool stage_kain(Session *s, Report *log)
{
    char nb[32], pl[512], lr[1024], it[1024], names[256];
    /* From story_nei_sword.json (Aukba): home to Paseo by the roof door;
     * whoever KAIN_PARTY leaves out hands over the consumables and anything
     * unique Kain or Shir could wear (keep_from); Shir in for a bounded
     * round of thefts (NOAH_THEFTS, empty_shir after); then Rolf, Rudo,
     * Anna and Kain, Kain geared in Paseo, the tower's roof back to
     * DezolisSkure and Zosa's shelves in the Mogic Cap. */
    pin_head_add("Rolf");
    if (find_item(s, "Prism", NULL, NULL)) {
        held_add("Prism");
    }
    keep_on_rolf(s, MOTAVIA_KEEP, 2, log);
    /* (build 119) Under no_kain() the party is already KAIN_PARTY: home
     * all the same, for Shir's thefts. */
    if (pl_no_kain || !party_has(s, "Kain")) {
        if (!enter_town(s, PASEO)) {
            say(log, "kain: could not get home to Paseo: %s",
                level_name(s->state.level, nb, sizeof nb));
            return false;
        }
        gear_up(s, PASEO, GEAR_RESERVE, log);   /* sells what nobody wants */
        Roster r;
        roster(s, &r);
        const char *now[PL_ROSTER_MAX];
        int nnow = 0;
        for (int i = 0; i < r.n; i++) {
            if (r.v[i].in_party) {
                now[nnow++] = r.v[i].name;
            }
        }
        static const char *const KAIN_SHIR[2] = { "Kain", "Shir" };
        static const char *const SHIR[1] = { "Shir" };
        char left[BAG_MAX][24];
        for (int i = 0; i < nnow; i++) {
            if (!name_in(now[i], g_kain_party, 4) && !streq(now[i], "Shir")) {
                int nl = keep_from(s, now[i], KAIN_SHIR, 2, log, left);
                if (nl > 0) {
                    say(log, "kain: %s keeps %s", now[i],
                        left_repr(left, nl, lr, sizeof lr));
                }
            }
        }
        const char *stays[PL_ROSTER_MAX + 1];
        int nstays = 0;
        for (int i = 0; i < nnow; i++) {
            if (name_in(now[i], g_kain_party, 4)) {
                stays[nstays++] = now[i];
            }
        }
        bool shir_now = name_in("Shir", now, nnow);
        if (!shir_now && nstays >= 4) {
            /* (build 119) Nobody leaves for good (no_kain): the last but
             * Rolf out for Shir's round, back after it. */
            int nl = keep_from(s, stays[nstays - 1], SHIR, 1, log, left);
            if (nl > 0) {
                say(log, "kain: %s keeps %s", stays[nstays - 1],
                    left_repr(left, nl, lr, sizeof lr));
            }
            nstays--;
        }
        char got[PL_ROSTER_MAX][16];
        if (!shir_now) {
            stays[nstays] = "Shir";
            pick_party_want(s, log, stays, nstays + 1, got);
        }
        if (party_has(s, "Shir")) {
            shir_thefts(s, NOAH_THEFTS, 2, log, NOAH_THEFT_EXITS,
                        empty_shir_all, it, sizeof it);
            int dews = carried(s, "MoonDew");
            int mists = carried(s, "StarMist");
            say(log, "kain: thefts %s -- {'MoonDew': %d, 'StarMist': %d}", it,
                dews, mists);
            empty_shir(s, log, NULL, 0);
        }
        int ng = pick_party_want(s, log, g_kain_party, 4, got);
        bool all = true;
        for (int k = 0; k < 4; k++) {
            bool in = false;
            for (int i = 0; i < ng; i++) {
                in = in || streq(got[i], g_kain_party[k]);
            }
            all = all && in;
        }
        if (!all) {
            say(log, "kain: the party is %s",
                party_repr(got, ng, names, sizeof names));
            return false;
        }
        keep_on_rolf(s, MOTAVIA_KEEP, 2, log);
        gear_up(s, PASEO, GEAR_RESERVE, log);
        say(log, "kain: in Paseo -- %s", party_line(s, pl, sizeof pl));
    }
    if (!is_dezolis_level(s->state.level)) {
        if (s->state.level != PASEO && !enter_town(s, PASEO)) {
            return false;
        }
        if (!stage_roof(s, log)) {
            return false;
        }
    }
    if (!dezolis_town_visit(s, ZOSA, log, false)) {
        return false;
    }
    say(log, "kain: at Zosa -- %s", party_line(s, pl, sizeof pl));
    for (int k = 0; k < 4; k++) {
        if (!party_has(s, g_kain_party[k])) {
            return false;
        }
    }
    return true;
}

#define KAIN_TRIPS 40           /* trips, not seconds: 16-24 to L20 */

static int member_level(const Session *s, const char *name)
{
    int m = member_at(&s->state, name);
    return (m < 0) ? PL_NONE : s->state.party[m].level;
}

static bool kain_grind_body(Session *s, Report *log)
{
    char pl[512];
    /* Kain to KAIN_TARGET on the field outside Zosa (field_spot, as
     * dezolis_grind): grind_and_gear by the party's lowest level -- the
     * others carry him, EXP is split by the living (loc_F2A4) -- with the
     * best gear bought each time through town. */
    pin_head_add("Rolf");
    if (pl_no_kain) {
        say(log, "kain_grind: no Kain in the party (no_kain)");
        return true;
    }
    GrindOpts o = grind_defaults();
    o.has_spot = field_spot(DEZOLIS_SKURE, ZOSA_GATE_X, ZOSA_GATE_Y, 3,
                            &o.spot);
    int lv = member_level(s, "Kain");
    if (lv == PL_NONE) {
        say(log, "kain_grind: Kain is not in the party");
        return false;
    }
    o.fights_per_trip = 8;
    o.trips = 4;
    o.log = log;
    o.lowest = true;
    int trips = 0;
    while (lv < g_kain_target && trips < KAIN_TRIPS) {
        grind_and_gear(s, ZOSA, g_kain_target, &o);
        trips += 4;
        lv = member_level(s, "Kain");
        lv = (lv == PL_NONE) ? 0 : lv;
    }
    dezolis_town_visit(s, ZOSA, log, false);
    say(log, "kain_grind: Kain L%d after %d trips -- %s", lv, trips,
        party_line(s, pl, sizeof pl));
    if (lv < g_kain_target) {
        if (pl_lean) {
            say(log, "lean: Kain short of L%d, going on", g_kain_target);
            return true;
        }
        say(log, "kain_grind: out of trips; rerun with --from kain_grind "
            "--failed");
        return false;
    }
    return true;
}

bool stage_kain_grind(Session *s, Report *log)
{
    in_grind_begin(s);
    bool ok = kain_grind_body(s, log);
    in_grind_end(s);
    return ok;
}

#define NOAH_TRIP_FIGHTS 20
/* trips onto Noah, a wipe-out counted, not seconds (build 98's seeds:
 * 42-51 trips to NOAH_TARGET) */
#define NOAH_TRIPS 80
static const char *const NOAH_WALKS[4] = {
    "walk down 3", "walk left 3", "walk up 3", "walk right 3"
};

/* sword_holder: the party position whose bag holds the Nei Sword (worn or
 * not), or -1. */
static int sword_holder(Session *s)
{
    Bags b;
    bags(s, &b);
    for (int h = 0; h < b.n; h++) {
        for (int i = 0; i < b.m[h].n; i++) {
            if (streq(b.m[h].v[i].name, "NeiSword")) {
                return h;
            }
        }
    }
    return -1;
}

/* nei_sword_out: USE the Nei Sword on Noah (NeiSword_ItemSelected,
 * loc_A21E): Esper mansion F1 16,16. */
static bool nei_sword_out(Session *s, Report *log)
{
    char pl[512], t[1024], b[4096], nb[32], xs[16], ys[16];
    ready(s);
    /* (build 98) Its holder dead -- Rolf, who wears it -- the dead come
     * back first (Rever, a Moon Dew): the first Noah grind lost Rolf on a
     * trip, found no live holder and could not leave Noah. */
    int holder = sword_holder(s);
    if (holder >= 0 && holder < s->state.party_n
        && s->state.party[holder].dead) {
        int back = revive_x(s, true);   /* (build 99) the reserve */
        say(log, "nei_sword_out: the sword's holder was dead; %d revived -- "
            "%s", back, party_line(s, pl, sizeof pl));
    }
    /* Worn counts: Rolf wears it, and find_item looks only at what is not
     * worn. ITEM lists the whole bag, worn pieces marked, and USE on one
     * runs its action (NeiSword_ItemSelected does not ask). */
    const State st = s->state;          /* the party before the bags */
    Bags bb;
    bags(s, &bb);
    int fh = -1, fi = -1;
    for (int h = 0; h < bb.n && fh < 0; h++) {
        if (h >= st.party_n || st.party[h].dead) {
            continue;
        }
        for (int i = 0; i < bb.m[h].n && fh < 0; i++) {
            if (streq(bb.m[h].v[i].name, "NeiSword")) {
                fh = h;
                fi = i;
            }
        }
    }
    if (fh < 0) {
        say(log, "nei_sword_out: no Nei Sword -- %s",
            bags_repr(s, b, sizeof b));
        return false;
    }
    Ints shown = { 0 };
    use_item(s, fh, fi, PL_NONE, &shown);
    ready(s);
    bool ok = s->state.level == ESPER_F1;
    if (!ok) {
        say(log, "nei_sword_out: %s -> %s %s %s",
            scripts_text(&shown, t, sizeof t),
            level_name(s->state.level, nb, sizeof nb),
            num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys));
    }
    ints_free(&shown);
    return ok;
}

/* noah_trip: from the mansion, YES, fights on Noah until `fights` or
 * worn_out, the Nei Sword back. The fights; -1 for the Python's None
 * (wiped out). */
static int noah_trip(Session *s, Report *log, int fights)
{
    char t[1024], nb[32];
    Ints shown = { 0 };
    lutz(s, 0, log, &shown);
    if (s->state.level != NOAH_G) {
        say(log, "noah: not on Noah after %s %s",
            scripts_text(&shown, t, sizeof t),
            level_name(s->state.level, nb, sizeof nb));
        ints_free(&shown);
        return 0;
    }
    ints_free(&shown);
    int n = 0;
    while (n < fights && !worn_out(s)) {
        int got = wander(s, NOAH_WALKS, 4, 1, true);
        if (!st_screen(&s->state, "level") && !st_screen(&s->state, "battle")) {
            return -1;
        }
        if (got == 0) {
            break;
        }
        n += got;
    }
    if (s->state.level == NOAH_G || s->state.level == NOAH_G + 1) {
        nei_sword_out(s, log);
    }
    return n;
}

/* mansion_mend: after the Nei Sword, anyone dead to `town`'s clone lab
 * (loc_B9D8's HEAL skips the dead), everyone else to Lutz's NO. */
static bool mansion_mend(Session *s, int town, Report *log)
{
    if (party_dead(s)) {
        if (!enter_town(s, town)) {
            return false;
        }
        TownBuilding cl;
        if (town_building(town, BUILDING_CLONE_LAB, &cl)) {
            clone_lab(s, town, cl.y, cl.x);
        }
        heal_here(s, town);
        return !party_dead(s);
    }
    return lutz_heal(s, log);
}

/* (build 99) Moon Dews carried onto Noah and kept back from the walk's
 * revivals (revive, s.dew_reserve): the Nei Sword's holder -- Rolf, who
 * wears it -- dead with nobody able to cast Rever leaves the party no way
 * off Noah. No shop sells one; Shir steals them in Paseo. */
#define DEW_RESERVE 1

/* reserve_dews: Moon Dews the reserve can count on, those outside the Nei
 * Sword holder's bag: a dew in Rolf's own bag is no use to him dead
 * (find_item looks only at the living). */
static int reserve_dews(Session *s)
{
    int holder = sword_holder(s);
    Bags b;
    bags(s, &b);
    int n = 0;
    for (int h = 0; h < b.n; h++) {
        for (int i = 0; h != holder && i < b.m[h].n; i++) {
            n += streq(b.m[h].v[i].name, "MoonDew") ? 1 : 0;
        }
    }
    return n;
}

/* dews_off_holder: the holder's Moon Dews to a living member with room
 * (tossing something of theirs when every bag is full) until the reserve
 * is outside his bag. */
static void dews_off_holder(Session *s, Report *log)
{
    char t[1024];
    for (int k = 0; k < 4; k++) {
        if (reserve_dews(s) >= DEW_RESERVE) {
            return;
        }
        int holder = sword_holder(s);
        const State st = s->state;      /* the party before the bags */
        Bags b;
        bags(s, &b);
        if (holder < 0 || holder >= b.n) {
            return;
        }
        int slot = -1;
        for (int i = 0; i < b.m[holder].n && slot < 0; i++) {
            if (streq(b.m[holder].v[i].name, "MoonDew")
                && !b.m[holder].v[i].worn) {
                slot = i;
            }
        }
        int to = -1;
        int lim = (b.n < st.party_n) ? b.n : st.party_n;
        for (int h = 0; h < lim; h++) {
            if (h != holder && !st.party[h].dead
                && (to < 0 || b.m[h].n < b.m[to].n)) {
                to = h;
            }
        }
        if (slot < 0 || to < 0) {
            return;
        }
        if (b.m[to].n >= 16 && make_room(s, log, to) == NULL) {
            return;
        }
        Ints shown = { 0 };
        give_item(s, holder, slot, to, &shown);
        if (holder >= st.party_n) {
            pl_error("IndexError: list index out of range (dews_off_holder)");
        }
        say(log, "dews: a Moon Dew from %s to %s, off the sword's holder: %s",
            st.party[holder].name, st.party[to].name,
            scripts_text(&shown, t, sizeof t));
        ints_free(&shown);
    }
}

static const char *const DEWS_FIRST[1] = { "MoonDew" };

/* restock_dews' on_full: what she stole handed on, the dews first, and
 * one of hers tossed when her bag is still full. */
static void dews_room(Session *s, Report *log)
{
    empty_shir(s, log, DEWS_FIRST, 1);
    int h = member_at(&s->state, "Shir");
    if (h >= 0) {
        Bag bag;
        shir_bag(s, &bag);
        if (bag.n >= 16) {
            make_room(s, log, h);
        }
    }
}

/* restock_dews: before a trip onto Noah, at least DEW_RESERVE Moon Dews
 * carried, or home for them -- the tower's roof door to Paseo, the last
 * member but Rolf out for Shir (keep_from), her thefts for the dews
 * (empty_shir after), the party as it was, and the roof back to `town`.
 * False only when the way there or back fails; with no Shir to send it
 * goes on without, logged. */
static bool restock_dews(Session *s, int town, Report *log)
{
    char pl[512], nb[32], lr[1024], it[1024];
    s->dew_reserve = DEW_RESERVE;
    dews_off_holder(s, log);
    int have = reserve_dews(s);
    if (have >= DEW_RESERVE) {
        return true;
    }
    if (s->dews_gave_up) {
        return true;            /* a restock came back empty: once a try */
    }
    Roster r;
    roster(s, &r);
    bool shir = false;
    for (int i = 0; i < r.n; i++) {
        shir = shir || streq(r.v[i].name, "Shir");
    }
    if (!shir) {
        say(log, "dews: none in reserve and no Shir to steal one; going on");
        return true;
    }
    char names[PL_PARTY_MAX][16];
    const char *want[PL_PARTY_MAX + 1];
    int nn = 0;
    for (int m = 0; m < s->state.party_n && m < PL_PARTY_MAX; m++) {
        snprintf(names[nn], sizeof names[nn], "%s", s->state.party[m].name);
        want[nn] = names[nn];
        nn++;
    }
    say(log, "dews: %d of %d in reserve; home for Shir's thefts -- %s", have,
        DEW_RESERVE, party_line(s, pl, sizeof pl));
    keep_on_rolf(s, MOTAVIA_KEEP, 2, log);
    if (!enter_town(s, PASEO)) {
        say(log, "dews: could not get home to Paseo: %s",
            level_name(s->state.level, nb, sizeof nb));
        return false;
    }
    char got[PL_ROSTER_MAX][16];
    if (!name_in("Shir", want, nn)) {
        const char *leaving = NULL;
        for (int i = nn - 1; i >= 0 && leaving == NULL; i--) {
            if (!streq(want[i], "Rolf")) {
                leaving = want[i];
            }
        }
        if (leaving != NULL) {
            static const char *const SHIR[1] = { "Shir" };
            char left[BAG_MAX][24];
            int nl = keep_from(s, leaving, SHIR, 1, log, left);
            if (nl > 0) {
                say(log, "dews: %s keeps %s", leaving,
                    left_repr(left, nl, lr, sizeof lr));
            }
        }
        const char *with[PL_PARTY_MAX + 1];
        int nw = 0;
        for (int i = 0; i < nn; i++) {
            if (leaving == NULL || !streq(want[i], leaving)) {
                with[nw++] = want[i];
            }
        }
        with[nw++] = "Shir";
        pick_party_want(s, log, with, nw, got);
    }
    if (party_has(s, "Shir")) {
        empty_shir(s, log, DEWS_FIRST, 1);      /* any she already had */
        /* Room to steal into: seed 7's Shir came back from Rolf's house
         * with a full bag and every other bag full too, so no theft was
         * possible and none was tried. The commonest things go. */
        for (int k = 0; k < 4; k++) {
            int h = member_at(&s->state, "Shir");
            if (h < 0) {
                break;
            }
            Bag bag;
            shir_bag(s, &bag);
            if (bag.n < 14 || make_room(s, log, h) == NULL) {
                break;
            }
        }
        static const Want DEWS[1] = { { "MoonDew", DEW_RESERVE } };
        shir_thefts(s, DEWS, 1, log, NOAH_THEFT_EXITS, dews_room, it,
                    sizeof it);
        empty_shir(s, log, DEWS_FIRST, 1);
        say(log, "dews: thefts %s -- %d carried", it, carried(s, "MoonDew"));
    }
    pick_party_want(s, log, want, nn, got);
    keep_on_rolf(s, MOTAVIA_KEEP, 2, log);
    dews_off_holder(s, log);
    if (s->state.level != PASEO && !enter_town(s, PASEO)) {
        return false;
    }
    if (!stage_roof(s, log)) {
        return false;
    }
    if (!dezolis_town_visit(s, town, log, false)) {
        return false;
    }
    int now = reserve_dews(s);
    say(log, "dews: back at %s -- %d carried -- %s",
        level_name(town, nb, sizeof nb), now, party_line(s, pl, sizeof pl));
    if (now < DEW_RESERVE) {
        /* No spinning: one empty-handed restock is the last this session. */
        say(log, "dews: none stolen; the Noah trips go on without a reserve");
        s->dews_gave_up = true;
    }
    return true;
}

/* noah_grind: from Zosa with Kain raised, the grind on NoahGroundF until
 * `target` -- Lutz's YES, noah_trip, mansion_mend -- and every fifth trip
 * the nearest town for gear, mates and a save. top_up_noah passes a
 * target of its own (NOAH_TARGET while the Python's top-up runs). */
static bool noah_grind_body(Session *s, Report *log, const Target *target)
{
    char pl[512], tr[256];
    pin_head_add("Rolf");
    int town = nearest_town(ESPER_F1);
    int trips = 0, total = 0, went = 0;
    s->dews_gave_up = false;            /* (build 99) one restock a try */
    if (s->state.level != ESPER_F1 && s->state.level != NOAH_G) {
        if (!dezolis_town_visit(s, town, log, false)) {
            return false;
        }
    }
    while (!reached(s, target)) {
        if (went >= NOAH_TRIPS) {
            dezolis_town_visit(s, town, log, false);
            if (pl_lean) {
                /* (build 98) short of NOAH_TARGET: the bosses with these
                 * levels */
                say(log, "lean: noah_grind short of %s after %d fights, "
                    "going on -- %s", target_repr(target, tr, sizeof tr),
                    total, party_line(s, pl, sizeof pl));
                return true;
            }
            say(log, "noah_grind: out of trips; rerun with --from noah_grind "
                "--failed -- %s", party_line(s, pl, sizeof pl));
            return false;
        }
        if (!restock_dews(s, town, log)) {
            return false;
        }
        went++;
        int n = noah_trip(s, log, NOAH_TRIP_FIGHTS);
        if (n < 0) {
            if (!recover(s, log)) {
                return false;
            }
            continue;
        }
        total += n;
        trips++;
        say(log, "noah trip %d: %d fights -- %s", trips, n,
            party_line(s, pl, sizeof pl));
        if (!mansion_mend(s, town, log)) {
            say(log, "noah_grind: could not mend -- %s",
                party_line(s, pl, sizeof pl));
            return false;
        }
        if (trips % 5 == 0) {
            dezolis_town_visit(s, town, log, false);
        }
    }
    dezolis_town_visit(s, town, log, false);
    say(log, "noah_grind: %d fights in %d trips -- %s", total, trips,
        party_line(s, pl, sizeof pl));
    return true;
}

bool stage_noah_grind(Session *s, Report *log)
{
    in_grind_begin(s);
    bool ok = noah_grind_body(s, log, &g_noah_target);
    in_grind_end(s);
    return ok;
}

/* ---- (build 178) the bosses: Dark Force and Mother Brain ---- */

#define NOAH_BOX_X   33         /* object $39 at $210,$D8 (LoadSpritesInLevel) */
#define NOAH_BOX_Y   13
#define BOX_TRIES    4
#define BOX_HP       0.85
#define BOX_TP       0.6
#define BOSS_TRIES   4
/* The trials dealt eight Trimates a member; with none the same party won
 * 1 of 10 against Dark Force. Ryuon's is the Dezolis item store with them
 * (the mansion's town, Aukba, has none). */
#define BOSS_TRIMATES 32

/* The members with TP: "Rolf TP 40/52 Amy TP 71/135". */
static const char *tp_line(const Session *s, char *out, size_t cap)
{
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < s->state.party_n && used < cap; i++) {
        const Member *m = &s->state.party[i];
        if (m->max_tp) {
            used += (size_t)snprintf(out + used, cap - used, "%s%s TP %d/%d",
                                     used ? " " : "", m->name, m->tp,
                                     m->max_tp);
        }
    }
    return out;
}

/* box_ready: nobody down, HP at BOX_HP of the party's and TP at BOX_TP of
 * the TP users' between them. */
static bool box_ready(const Session *s)
{
    const State *st = &s->state;
    if (st->party_n == 0 || party_dead(s)) {
        return false;
    }
    long hp = 0, max_hp = 0, tp = 0, max_tp = 0;
    for (int i = 0; i < st->party_n; i++) {
        hp += st->party[i].hp;
        max_hp += st->party[i].max_hp;
        tp += st->party[i].tp;
        max_tp += st->party[i].max_tp;
    }
    double h = (double)hp / (double)((max_hp > 1) ? max_hp : 1);
    double t = max_tp ? (double)tp / (double)max_tp : 1.0;
    return h >= BOX_HP && t >= BOX_TP;
}

/* The scripts shown since `before`, the last `last` of them
 * (s.shown[before:][-last:]). */
static const char *shown_last(const Session *s, int before, int last,
                              char *out, size_t cap)
{
    Ints ids = { 0 };
    int from = before;
    if (s->shown.n - last > from) {
        from = s->shown.n - last;
    }
    for (int i = from; i < s->shown.n; i++) {
        ints_add(&ids, s->shown.v[i]);
    }
    scripts_text(&ids, out, cap);
    ints_free(&ids);
    return out;
}

/* s.state.get("screen") as Python prints it. */
static const char *screen_of(const Session *s)
{
    return s->state.valid ? s->state.screen : "None";
}

/* The state's enemies as Python prints the list: [(259, 3000, 3000)]. */
static const char *enemies_repr(const State *st, char *out, size_t cap)
{
    size_t used = (size_t)snprintf(out, cap, "[");
    for (int i = 0; i < st->enemy_n && used < cap; i++) {
        used += (size_t)snprintf(out + used, cap - used, "%s(%d, %d, %d)",
                                 i ? ", " : "", st->enemy[i].id,
                                 st->enemy[i].hp, st->enemy[i].max_hp);
    }
    if (used < cap) {
        snprintf(out + used, cap - used, "]");
    }
    return out;
}

/* boss_supplies: Ryuon's item store, Trimates to BOSS_TRIMATES across the
 * bags that have room, after the usual town visit. */
static bool boss_supplies(Session *s, Report *log)
{
    if (!dezolis_town_visit(s, RYUON, log, false)) {
        return false;
    }
    int got = stock_item(s, RYUON, "Trimate", BOSS_TRIMATES, log);
    Bags b;
    bags(s, &b);
    int have = 0;
    for (int h = 0; h < b.n; h++) {
        for (int i = 0; i < b.m[h].n; i++) {
            have += streq(b.m[h].v[i].name, "Trimate") ? 1 : 0;
        }
    }
    say(log, "boss supplies: %d Trimates bought, %d carried", got, have);
    return true;
}

bool stage_boss_party(Session *s, Report *log)
{
    char nb[32], pl[512], lr[1024], names[256];
    /* From story_noah_grind.json (the town nearest the mansion): home to
     * Paseo by the roof door, whoever BOSS_PARTY leaves out hands over
     * what Amy could use (keep_from), Amy in at Rolf's house, geared in
     * Paseo, and back to the mansion's town. */
    pin_head_add("Rolf");
    if (find_item(s, "Prism", NULL, NULL)) {
        held_add("Prism");
    }
    keep_on_rolf(s, MOTAVIA_KEEP, 2, log);
    int town = nearest_town(ESPER_F1);
    bool all = true;
    for (int k = 0; k < 4; k++) {
        all = all && party_has(s, g_boss_party[k]);
    }
    if (all) {
        return dezolis_town_visit(s, town, log, false);
    }
    if (!enter_town(s, PASEO)) {
        say(log, "boss_party: could not get home to Paseo: %s",
            level_name(s->state.level, nb, sizeof nb));
        return false;
    }
    gear_up(s, PASEO, GEAR_RESERVE, log);
    Roster r;
    roster(s, &r);
    const char *now[PL_ROSTER_MAX];
    int nnow = 0;
    for (int i = 0; i < r.n; i++) {
        if (r.v[i].in_party) {
            now[nnow++] = r.v[i].name;
        }
    }
    static const char *const AMY[1] = { "Amy" };
    char left[BAG_MAX][24];
    for (int i = 0; i < nnow; i++) {
        if (!name_in(now[i], g_boss_party, 4)) {
            int nl = keep_from(s, now[i], AMY, 1, log, left);
            if (nl > 0) {
                say(log, "boss_party: %s keeps %s", now[i],
                    left_repr(left, nl, lr, sizeof lr));
            }
        }
    }
    char got[PL_ROSTER_MAX][16];
    int ng = pick_party_want(s, log, g_boss_party, 4, got);
    for (int k = 0; k < 4; k++) {
        bool in = false;
        for (int i = 0; i < ng; i++) {
            in = in || streq(got[i], g_boss_party[k]);
        }
        if (!in) {
            say(log, "boss_party: the party is %s",
                party_repr(got, ng, names, sizeof names));
            return false;
        }
    }
    keep_on_rolf(s, MOTAVIA_KEEP, 2, log);
    gear_up(s, PASEO, GEAR_RESERVE, log);
    say(log, "boss_party: in Paseo -- %s", party_line(s, pl, sizeof pl));
    if (!stage_roof(s, log)) {
        return false;
    }
    if (!dezolis_town_visit(s, town, log, false)) {
        return false;
    }
    say(log, "boss_party: at the mansion's town -- %s",
        party_line(s, pl, sizeof pl));
    return true;
}

/* The walks to the bosses (build 97, tools/walk_trials.py): weapons and
 * mates only, no RUN -- TP kept for the boss -- and after each fight the
 * dead revived and everyone mended to half (WALK_FIGHT, WALK_MEND). */
#define WALK_MEND 0.5

static FightOpts walk_fight(void)
{
    FightOpts o = fight_defaults();
    o.use_techs = false;                /* type_techs (): none, and moot */
    o.run_below = 0.0;
    return o;
}

/* The boss fights: fight_smart(run_below=0.0, limit=4000,
 * stop_on_level=True, stop_on_gameover=True). */
static FightOpts boss_fight(void)
{
    FightOpts o = fight_defaults();
    o.run_below = 0.0;
    o.limit = 4000;
    o.stop_on_level = true;
    o.stop_on_gameover = true;
    return o;
}

/* boss_walk: the mansion mended, Lutz's YES and the walk on WALK_FIGHT to
 * `goal` on `level`, topped up below it, up to BOX_TRIES times until the
 * party is at box_ready there. 1: in shape below it; 0: never ("%s: never
 * at %s in shape" logged); -1: a way back that failed (the stage's False
 * at once). */
static int boss_walk(Session *s, int town, int level, int gx, int gy,
                     const char *who, const char *where, const char *below,
                     Report *log)
{
    char pl[512], tp[256], nb[32], xs[16], ys[16];
    for (int attempt = 0; attempt < BOX_TRIES; attempt++) {
        if (!restock_dews(s, town, log)) {                /* (build 99) */
            return -1;
        }
        if (!mansion_mend(s, town, log)) {
            return -1;
        }
        lutz(s, 0, log, NULL);
        if (s->state.level != NOAH_G) {
            return -1;
        }
        s->fight_opts = walk_fight();
        s->fight_opts_set = true;
        s->walk_mend = WALK_MEND;
        CSet goal = { 0 };
        cset_add(&goal, gx, gy, 0);
        bool there = travel(s, level, &goal, log);
        cset_free(&goal);
        s->fight_opts_set = false;
        s->walk_mend = -1.0;
        if (!there) {
            say(log, "%s: could not reach %s: %s %s %s", who, where,
                level_name(s->state.level, nb, sizeof nb),
                num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys));
            if (!st_screen(&s->state, "level")
                && !st_screen(&s->state, "battle")) {
                recover(s, log);
                return -1;
            }
            nei_sword_out(s, log);
            continue;
        }
        revive(s);
        mend(s, 0.9);
        party_line(s, pl, sizeof pl);
        tp_line(s, tp, sizeof tp);
        say(log, "%s, try %d -- %s -- %s -- %d mates", below, attempt + 1, pl,
            tp, count_mates(s));
        if (box_ready(s)) {
            return 1;
        }
        nei_sword_out(s, log);
    }
    say(log, "%s: never at %s in shape", who, where);
    return 0;
}

/* The last fight's record: (ticks, as Python prints rec.get("ticks")). */
static const char *last_fight_ticks(const Session *s, char *out, size_t cap)
{
    return (s->fights_n > 0) ? num(s->fights[s->fights_n - 1].ticks, out, cap)
                             : "None";
}

/* dark_force_once: Pandora's box and Dark Force (route noah_box) -- from
 * the town, mended, Lutz's YES, to 33,14 below the box, A ($171A), C
 * through the reload and $1910 into the fight ($102), fought with
 * fight_smart; then the Nei Sword out and the town. True when Dark Force
 * is beaten. */
static bool dark_force_once(Session *s, Report *log)
{
    char pl[512], t[2048], en[512], fs[16], tk[16], nb[32], xs[16], ys[16];
    s->dews_gave_up = false;            /* (build 99) */
    pin_head_add("Rolf");
    int town = nearest_town(ESPER_F1);
    if (!boss_supplies(s, log)) {
        return false;
    }
    if (!dezolis_town_visit(s, town, log, false)) {
        return false;
    }
    /* (build 91) The walk from Lutz's landing (32,107) to the box is
     * eleven or so fights; TP comes back only at Lutz's NO (or a town), so
     * the box is opened only with the party at box_ready -- else the Nei
     * Sword out, NO, YES and the walk again. */
    int w = boss_walk(s, town, NOAH_G, NOAH_BOX_X, NOAH_BOX_Y + 1,
                      "dark_force", "the box", "below the box", log);
    if (w <= 0) {
        return false;
    }
    send2(s, "press up", "wait idle");
    send1(s, "shot dark_force_01_box");
    int before = s->shown.n;
    send2(s, "press a", "wait idle");
    for (int i = 0; i < 12; i++) {
        if (st_screen(&s->state, "battle")) {
            break;
        }
        if (st_waiting(&s->state)) {
            send2(s, "press c", "wait idle");
        } else {
            send1(s, "wait idle");
        }
    }
    say(log, "the box: %s", shown_since(s, before, t, sizeof t));
    if (!st_screen(&s->state, "battle")) {
        say(log, "dark_force: no fight -- %s", screen_of(s));
        return false;
    }
    send2(s, "wait idle", "shot dark_force_02_fight");
    say(log, "fight: formation %s %s", num(s->state.formation, fs, sizeof fs),
        enemies_repr(&s->state, en, sizeof en));
    FightOpts fo = boss_fight();
    fight_smart(s, &fo, NULL);
    bool won = st_screen(&s->state, "level");
    Ints tail = { 0 };
    if (s->fights_n > 0) {
        const FightRec *rec = &s->fights[s->fights_n - 1];
        for (int i = 0; i < rec->tail_n; i++) {
            ints_add(&tail, rec->tail[i]);
        }
    }
    say(log, "Dark Force %s -- %s %s ticks %s scripts %s",
        won ? "beaten" : "not beaten", screen_of(s),
        party_line(s, pl, sizeof pl), last_fight_ticks(s, tk, sizeof tk),
        scripts_text(&tail, t, sizeof t));
    ints_free(&tail);
    if (!won) {
        recover(s, log);
        return false;
    }
    ready(s);
    send1(s, "shot dark_force_03_after");
    say(log, "after: %s %s %s %s", level_name(s->state.level, nb, sizeof nb),
        num(s->state.x, xs, sizeof xs), num(s->state.y, ys, sizeof ys),
        shown_last(s, before, 6, t, sizeof t));
    if (!nei_sword_out(s, log)) {
        /* (build 98) The sword's holder dead and nobody to revive him: no
         * way off Noah, and nowhere to save. A lost try. */
        say(log, "dark_force: beaten, but no way off Noah -- %s",
            party_line(s, pl, sizeof pl));
        return false;
    }
    mansion_mend(s, town, log);
    dezolis_town_visit(s, town, log, false);
    return true;
}

bool stage_dark_force(Session *s, Report *log)
{
    /* Pandora's box and Dark Force, up to BOSS_TRIES times: a loss goes
     * back to the session's last save (the town's) and tries again. */
    for (int attempt = 0; attempt < BOSS_TRIES; attempt++) {
        if (dark_force_once(s, log)) {
            return true;
        }
        say(log, "dark_force: try %d lost", attempt + 1);
        if (!st_screen(&s->state, "level") && !st_screen(&s->state, "building")
            && !recover(s, log)) {
            return false;
        }
    }
    return false;
}

#define NOAH_F1 0x64
/* The plain floor just south of NoahF1's corridor. The corridor itself is
 * block $33, every cell of it with the collision table's special bit,
 * which LevelMap keeps out of its floor: here it marks the approach's
 * trigger (loc_70D6: row 33 or north of it), so the stage walks the
 * corridor itself. */
#define MOTHER_BRAIN_FROM_X 33
#define MOTHER_BRAIN_FROM_Y 40
#define MOTHER_BRAIN_WALK   8
#define MOTHER_BRAIN        0x103
#define WIN_YES_NO          0x1F
#define ENDING_LIMIT        20000   /* the ending screen is ~17300 ticks */

/* answer: C through text; at each YES/NO the next of `answers` ("yes" or
 * "no") until the screen is `until`. False if the limit ran out first. */
static bool answer(Session *s, const char *const *answers, int nanswers,
                   const char *until, int limit)
{
    int next = 0;
    for (int i = 0; i < limit; i++) {
        const State *st = &s->state;
        if (st_screen(st, until)) {
            return true;
        }
        if (st_wait(st, "menu") && st->menu == WIN_YES_NO) {
            if (next < nanswers && streq(answers[next++], "no")) {
                send1(s, "press down");
            }
            send2(s, "press c", "wait idle");
            continue;
        }
        if (st_waiting(st)) {
            send2(s, "press c", "wait idle");
        } else {
            send1(s, "wait idle");
        }
    }
    return false;
}

/* mother_brain_once: from the town after Dark Force, mended, Lutz's YES,
 * to the foot of NoahF1's corridor on weapons alone (TP kept for her),
 * topped up and at box_ready, north up the corridor into the arrival, YES
 * to fighting, NO to returning ("Die!"), her battle ($103) with
 * fight_smart. True when she is beaten; the level is back with the
 * aftermath to come. */
static bool mother_brain_once(Session *s, Report *log)
{
    char pl[512], t[2048], fs[16], tk[16], walk[32];
    s->dews_gave_up = false;            /* (build 99) */
    pin_head_add("Rolf");
    int town = nearest_town(ESPER_F1);
    if (!boss_supplies(s, log)) {
        return false;
    }
    if (!dezolis_town_visit(s, town, log, false)) {
        return false;
    }
    int w = boss_walk(s, town, NOAH_F1, MOTHER_BRAIN_FROM_X,
                      MOTHER_BRAIN_FROM_Y, "mother_brain", "her corridor",
                      "below her", log);
    if (w <= 0) {
        return false;
    }
    int before = s->shown.n;
    snprintf(walk, sizeof walk, "walk up %d", MOTHER_BRAIN_WALK);
    const char *const IN[4] = { walk, "wait idle", "wait ticks 2",
                                "wait idle" };
    sendv(s, 4, IN);
    static const char *const YES_NO[2] = { "yes", "no" };
    if (!answer(s, YES_NO, 2, "battle", 200)) {
        say(log, "mother_brain: no fight -- %s", screen_of(s));
        return false;
    }
    say(log, "the confrontation: %s", shown_since(s, before, t, sizeof t));
    if (s->state.formation != MOTHER_BRAIN) {
        say(log, "mother_brain: a fight, but formation %s",
            num(s->state.formation, fs, sizeof fs));
        return false;
    }
    send2(s, "wait idle", "shot mother_brain_01_fight");
    FightOpts fo = boss_fight();
    fight_smart(s, &fo, NULL);
    bool won = st_screen(&s->state, "level");
    say(log, "Mother Brain %s -- %s %s ticks %s", won ? "beaten" : "not beaten",
        screen_of(s), party_line(s, pl, sizeof pl),
        last_fight_ticks(s, tk, sizeof tk));
    return won;
}

bool stage_mother_brain(Session *s, Report *log)
{
    char t[2048], lim[32], stopped[64];
    /* Mother Brain, up to BOSS_TRIES times (a loss back to the last save),
     * then everything after her: the aftermath, the walk, Lutz's warning,
     * the walk to the Earthmen, YES and YES to them, the six on the
     * bridge, and the ending screen played through to its last button and
     * the Sega screen. No checkpoint follows: the game is over. */
    bool beaten = false;
    for (int attempt = 0; attempt < BOSS_TRIES && !beaten; attempt++) {
        if (mother_brain_once(s, log)) {
            beaten = true;
            break;
        }
        say(log, "mother_brain: try %d lost", attempt + 1);
        if (!st_screen(&s->state, "level") && !st_screen(&s->state, "building")
            && !recover(s, log)) {
            return false;
        }
    }
    if (!beaten) {
        return false;
    }
    int before = s->shown.n;
    send1(s, "shot mother_brain_02_after");
    /* Each press is settled with `wait idle`, which on the ending screen
     * waits out the whole limit: at 600 ticks (the aftermath's walks and
     * texts take under 300) an overshoot onto the ending lands inside the
     * first page's text, so the shots below start from page 3. */
    send1(s, "limit 600");
    static const char *const YES3[3] = { "yes", "yes", "yes" };
    if (!answer(s, YES3, 3, "ending", 400)) {
        say(log, "mother_brain: no ending -- %s %s", screen_of(s),
            shown_last(s, before, 8, t, sizeof t));
        return false;
    }
    say(log, "after her: %s", shown_since(s, before, t, sizeof t));
    snprintf(lim, sizeof lim, "limit %d", ENDING_LIMIT);
    const char *const END[19] = {
        lim, "wait screen ending",
        "wait ending page3_text", "wait ticks 700", "shot ending_01_page3",
        "wait ending final_days_text", "wait ticks 600",
        "shot ending_02_final_days",
        "wait ending credits", "wait ticks 4500", "shot ending_03_credits",
        "wait ending ufo", "wait ticks 24", "shot ending_04_ufo",
        "wait ending button", "shot ending_05_the_end",
        "press c", "wait screen sega", "limit 3600"
    };
    sendv(s, 19, END);
    bool done = st_screen(&s->state, "sega");
    snprintf(stopped, sizeof stopped, "stopped on %s", screen_of(s));
    say(log, "the ending: %s -- %s",
        done ? "played to the Sega screen" : stopped,
        shown_last(s, before, 10, t, sizeof t));
    return done;
}

/* ---- squeak-by's top-up ---- */

static bool top_up_paseo(Session *s, Report *log, int trips)
{
    if (!enter_town(s, PASEO)) {
        return false;
    }
    RoamOpts o = roam_defaults();
    o.trips = trips;
    o.log = log;
    return roam_and_gear(s, PASEO, 99, mild_ground(), &o);
}

static bool top_up_arima(Session *s, Report *log, int trips)
{
    if (!enter_town(s, ARIMA)) {
        return false;
    }
    RoamOpts o = roam_defaults();
    o.trips = trips;
    o.log = log;
    return roam_and_gear(s, ARIMA, 99, mild_ground(), &o);
}

static bool top_up_oputa(Session *s, Report *log, int trips)
{
    if (!enter_town(s, OPUTA)) {
        return false;
    }
    GrindOpts o = grind_defaults();
    o.trips = trips;
    o.log = log;
    return grind_and_gear(s, OPUTA, 99, &o);
}

/* (build 168f) A Skure retry starts from story_roof.json, before Skure's
 * chests: no cap in the bags, and Zosa refuses a bare-headed Rolf
 * everything. B1's Magic Cap first, as the stage itself takes it, and
 * Rolf's head kept for it. */
static bool top_up_zosa(Session *s, Report *log, int trips)
{
    char t[2048];
    pin_head_add("Rolf");
    if (!caps_carried(s)) {
        Cells cells = { 0 };
        chests(SKURE_B1, &cells);
        int at = -1;
        for (int i = 0; i < cells.n && at < 0; i++) {
            const char *item = chest_item(SKURE_B1, cells.v[i].x,
                                          cells.v[i].y);
            if (item != NULL && streq(item, "MagicCap")) {
                at = i;
            }
        }
        Ints shown = { 0 };
        if (at >= 0) {
            search(s, SKURE_B1, cells.v[at].x, cells.v[at].y, log, true, NULL,
                   &shown);
        }
        say(log, "top-up: B1's Magic Cap first: %s",
            (shown.n > 0) ? scripts_text(&shown, t, sizeof t) : "unreached");
        ints_free(&shown);
        cells_free(&cells);
    }
    if (!dezolis_town_visit(s, ZOSA, log, false)) {
        return false;
    }
    GrindOpts o = grind_defaults();
    o.fights_per_trip = 8;
    o.trips = trips;
    o.log = log;
    o.has_spot = field_spot(DEZOLIS_SKURE, ZOSA_GATE_X, ZOSA_GATE_Y, 3,
                            &o.spot);
    bool ok = grind_and_gear(s, ZOSA, 99, &o);
    dezolis_town_visit(s, ZOSA, log, false);
    return ok;
}

/* (build 177) Noah's own grind, whoever NOAH_TARGET names (Rolf and Kain;
 * Rolf alone under no_kain) a level past where they stand for each
 * TOP_UP_TRIPS of `trips` -- a try already failed each: +1, then +2, +3.
 * In the party's order, as the Python's dict is made. */
static bool top_up_noah(Session *s, Report *log, int trips)
{
    static char names[4][16];
    Target t = { { NULL }, { 0 }, 0 };
    int up = (trips / 3 > 1) ? trips / 3 : 1;
    for (int m = 0; m < s->state.party_n && t.n < 4; m++) {
        const char *n = s->state.party[m].name;
        if (name_in(n, g_noah_target.name, g_noah_target.n)) {
            snprintf(names[t.n], sizeof names[t.n], "%s", n);
            t.name[t.n] = names[t.n];
            t.level[t.n] = s->state.party[m].level + up;
            t.n++;
        }
    }
    in_grind_begin(s);                  /* stage_noah_grind's @grinding */
    bool ok = noah_grind_body(s, log, &t);
    in_grind_end(s);
    return ok;
}

typedef bool (*TopUpFn)(Session *s, Report *log, int trips);

static const struct {
    const char *stage;
    TopUpFn     fn;
} TOP_UP[] = {
    { "arima", top_up_paseo }, { "gear", top_up_arima },
    { "oputa", top_up_oputa }, { "amy", top_up_oputa },
    { "lab_in", top_up_lab },
    { "zema", top_up_zema },
    { "climatrol", top_up_climatrol }, { "dam_grind", top_up_piata },
    { "skure", top_up_zosa },
    { "dezolis_grind", top_up_zosa }, { "kain_grind", top_up_zosa },
    { "noah_grind", top_up_noah }
};

static const char *const NO_TOP_UP[] = {
    "paseo", "motavia", "shir", "dam_party", "cards", "roof"
};

bool top_up(Session *s, const char *name, Report *log, int attempt)
{
    char pl[512];
    for (size_t i = 0; i < sizeof NO_TOP_UP / sizeof NO_TOP_UP[0]; i++) {
        if (streq(NO_TOP_UP[i], name)) {
            return false;
        }
    }
    in_grind_begin(s);
    int trips = 3 * ((attempt > 1) ? attempt : 1);      /* TOP_UP_TRIPS */
    for (int k = stage_index(name); k >= 0; k--) {
        const char *n = stage_name(k);
        for (size_t i = 0; i < sizeof TOP_UP / sizeof TOP_UP[0]; i++) {
            if (!streq(TOP_UP[i].stage, n)) {
                continue;
            }
            say(log, "%s: squeak-by's top-up before the retry -- %s's "
                "grind, %d trips -- %s", name, n, trips,
                party_line(s, pl, sizeof pl));
            if (TOP_UP[i].fn == NULL) {
                pl_error("NotPorted: top-up of %s", n);
            }
            TOP_UP[i].fn(s, log, trips);
            say(log, "%s: after the top-up -- %s", name,
                party_line(s, pl, sizeof pl));
            in_grind_end(s);
            return true;
        }
    }
    in_grind_end(s);
    return false;
}

/* revive_party: nobody left dead -- Rever or a Moon Dew on the spot, else
 * the town's clone lab. */
bool revive_party(Session *s, int town, Report *log)
{
    char pl[512], nb[32];
    if (!party_dead(s)) {
        return true;
    }
    int back = revive(s);
    if (party_dead(s) && enter_town(s, town)) {
        TownBuilding b;
        if (town_building(town, BUILDING_CLONE_LAB, &b)) {
            clone_lab(s, town, b.y, b.x);
        }
        heal_here(s, town);
    }
    say(log, "revive_party: %d on the spot, then %s -- %s", back,
        level_name(town, nb, sizeof nb), party_line(s, pl, sizeof pl));
    return !party_dead(s);
}
