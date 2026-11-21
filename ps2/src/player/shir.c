/* (build 176) play.py's Shir: into the party (shir_join), raised beside
 * Paseo (raise_member), and her thefts (shir_thefts) -- in and out of
 * Paseo's stores and the Central Tower's room, fetched back from Rolf's
 * house after each, until the party carries what is wanted.
 *
 * ProcessStealItem (ps2.asm:19093) runs as the party walks out of a store
 * or the room: with Shir in the party, alive, her bag not full -- and for
 * the room her level 10 or more -- one call in sixteen she takes an item
 * from the list (steal.json), joins it to her own bag and leaves the party
 * for Rolf's house ($0927, $0928). */

#include "pl.h"

#define WIN_TOWER_OPTIONS  0x35
#define WIN_TOWER_OPTIONS2 0x36

/* steal.json's lists, in its order: what each can take. */
static const struct {
    const char *list;
    const char *items[8];
    int         door_y, door_x;     /* PASEO_DOORS[STEAL_DOORS[list]] */
} STEAL[4] = {
    { "weapon", { "FireStaff", "Dagger", "LaserKnife", "Dagger",
                  "LaserKnife", "Dagger", "LaserShot", "Dagger" }, 5, 58 },
    { "armor", { "GrSleeves", "SilCrown", "JwlCrown", "SilCrown",
                 "JwlCrown", "SilCrown", "ShuneBoots", "SilCrown" }, 27, 58 },
    { "item", { "Trimate", "Hidapipe", "Trimate", "Hidapipe", "Trimate",
                "StarMist", "Trimate", "MoonDew" }, 17, 58 },
    { "room", { "Visiphone", "Visiphone", "Visiphone", "Visiphone",
                "Visiphone", "Visiphone", "Visiphone", "Visiphone" }, 15, 32 },
};

/* steal_source(item): the list (and so the Paseo door) it can be stolen
 * from, or -1. */
static int steal_source(const char *item)
{
    for (int l = 0; l < 4; l++) {
        for (int i = 0; i < 8; i++) {
            if (streq(STEAL[l].items[i], item)) {
                return l;
            }
        }
    }
    return -1;
}

/* The Central Tower: ROOM at the desk, then B out of whatever the keeper
 * asks -- the way out is the theft's. */
static int room_policy(Session *s, void *ctx)
{
    (void)ctx;
    const State *st = &s->state;
    if (st->menu == WIN_TOWER_OPTIONS || st->menu == WIN_TOWER_OPTIONS2) {
        return (st->building == 8) ? 0 : PICK_B;
    }
    if (st->menu != PL_NONE) {
        return PICK_B;
    }
    return PICK_C;
}

static bool shir_in_party(const Session *s)
{
    return party_has(s, "Shir");
}

/* shir_bag(s): her bag while she is in the party (else empty) into out. */
void shir_bag(Session *s, Bag *out)
{
    const State st = s->state;          /* the party before the bags */
    Bags b;
    bags(s, &b);
    out->n = 0;
    for (int m = 0; m < st.party_n; m++) {
        if (streq(st.party[m].name, "Shir")) {
            if (m < b.n) {
                *out = b.m[m];
            }
            return;
        }
    }
}

/* fetch_shir: Rolf's house after a theft, "Hey, Shir is coming back!" and
 * her return; true when she is back in the party. */
static bool fetch_shir(Session *s, Report *log)
{
    char t[512];
    Ints shown = { 0 }, back_lines = { 0 };
    rolf_house(s, NULL, 0, &shown);
    bool back = shir_in_party(s);
    for (int i = 0; i < shown.n; i++) {
        if (shown.v[i] >= 0x0927 && shown.v[i] <= 0x0928) {
            ints_add(&back_lines, shown.v[i]);
        }
    }
    say(log, "shir: fetched from Rolf's house (%s; %s)",
        back ? "back" : "NOT back", scripts_text(&back_lines, t, sizeof t));
    ints_free(&shown);
    ints_free(&back_lines);
    return back;
}

bool shir_join(Session *s, int keep, Report *log)
{
    Roster r;
    roster(s, &r);
    int at = -1;
    for (int i = 0; i < r.n; i++) {
        if (streq(r.v[i].name, "Shir")) {
            at = i;
        }
    }
    if (at < 0) {
        rolf_house(s, NULL, 0, NULL);
        roster(s, &r);
        for (int i = 0; i < r.n; i++) {
            if (streq(r.v[i].name, "Shir")) {
                at = i;
            }
        }
        if (at < 0) {
            say(log, "shir: not in the roster after Rolf's house");
            return false;
        }
    }
    if (r.v[at].in_party) {
        return true;
    }
    /* the party as it stands, less its weakest by member_value when there
     * are already four */
    const char *now[PL_ROSTER_MAX + 1];
    int lv[PL_ROSTER_MAX];
    int nn = 0;
    for (int i = 0; i < r.n; i++) {
        if (r.v[i].in_party) {
            lv[nn] = r.v[i].level;
            now[nn++] = r.v[i].name;
        }
    }
    while (nn > keep) {
        int drop = -1;
        double low = 0;
        for (int i = 0; i < nn; i++) {
            if (streq(now[i], "Rolf")) {
                continue;
            }
            double v = member_value(now[i], lv[i]);
            if (drop < 0 || v < low) {
                drop = i;
                low = v;
            }
        }
        if (drop < 0) {
            pl_error("ValueError: min() arg is an empty sequence");
        }
        for (int i = drop; i + 1 < nn; i++) {
            now[i] = now[i + 1];
            lv[i] = lv[i + 1];
        }
        nn--;
    }
    now[nn++] = "Shir";
    char got[PL_ROSTER_MAX][16];
    int ng = pick_party_want(s, log, now, nn, got);
    for (int i = 0; i < ng; i++) {
        if (streq(got[i], "Shir")) {
            return true;
        }
    }
    return false;
}

bool raise_member(Session *s, const char *name, int target, int town,
                  Report *log, int trips)
{
    static const char *const WALKS[4] = { "walk down 3", "walk up 3",
                                          "walk left 3", "walk right 3" };
    for (int trip = 0; trip < trips; trip++) {
        int lv = PL_NONE;
        for (int m = 0; m < s->state.party_n && lv == PL_NONE; m++) {
            if (streq(s->state.party[m].name, name)) {
                lv = s->state.party[m].level;
            }
        }
        if (lv == PL_NONE) {
            return false;
        }
        if (lv >= target) {
            return true;
        }
        if (s->state.level == town && !town_exit(s)) {
            return false;
        }
        wander(s, WALKS, 4, 6, false);
        if (!st_screen(&s->state, "level") && !st_screen(&s->state, "battle")) {
            say(log, "lost: %s", s->state.valid ? s->state.screen : "None");
            return false;
        }
        if (needs_healing(s, 0.5) || trip % 4 == 3) {
            if (!enter_town(s, town)) {
                return false;
            }
            heal_here(s, PASEO);            /* heal(s): Paseo's hospital */
        }
    }
    return false;
}

/* A tally's {name: count}, in the order first counted. */
typedef struct {
    char name[24][24];
    int  count[24];
    int  n;
} Tally;

static void tally_add(Tally *t, const char *name)
{
    for (int i = 0; i < t->n; i++) {
        if (streq(t->name[i], name)) {
            t->count[i]++;
            return;
        }
    }
    if (t->n < 24) {
        snprintf(t->name[t->n], 24, "%s", name);
        t->count[t->n++] = 1;
    }
}

/* (build 177) the {name: count} as Python prints the dict */
static void tally_repr(const Tally *t, char *out, size_t cap)
{
    size_t used = (size_t)snprintf(out, cap, "{");
    for (int i = 0; i < t->n && used < cap; i++) {
        used += (size_t)snprintf(out + used, cap - used, "%s'%s': %d",
                                 i ? ", " : "", t->name[i], t->count[i]);
    }
    if (used < cap) {
        snprintf(out + used, cap - used, "}");
    }
}

/* json.dumps of the {name: count} */
static size_t tally_json(const Tally *t, char *out, size_t cap)
{
    size_t used = (size_t)snprintf(out, cap, "{");
    for (int i = 0; i < t->n && used < cap; i++) {
        used += (size_t)snprintf(out + used, cap - used, "%s\"%s\": %d",
                                 i ? ", " : "", t->name[i], t->count[i]);
    }
    if (used < cap) {
        used += (size_t)snprintf(out + used, cap - used, "}");
    }
    return used;
}

void shir_thefts(Session *s, const Want *wants, int nwants, Report *log,
                 int max_exits, void (*on_full)(Session *s, Report *log),
                 char *items_out, size_t items_cap)
{
    char ex[512], th[512], it[1024];
    Tally exits = { 0 }, thefts = { 0 }, items = { 0 };
    if (items_out != NULL) {
        snprintf(items_out, items_cap, "{}");
    }
    if (!shir_join(s, 3, log)) {
        return;
    }
    bool room = false;
    for (int i = 0; i < nwants; i++) {
        room = room || steal_source(wants[i].item) == 3;
    }
    if (room) {
        int lv = PL_NONE;
        for (int m = 0; m < s->state.party_n && lv == PL_NONE; m++) {
            if (streq(s->state.party[m].name, "Shir")) {
                lv = s->state.party[m].level;
            }
        }
        if (lv == PL_NONE) {
            pl_error("StopIteration (shir_thefts)");
        }
        if (lv < 10) {
            say(log, "shir: L%d, the room wants 10", lv);
            if (!raise_member(s, "Shir", 10, PASEO, log, 60)) {
                say(log, "shir: could not raise her to 10");
                return;
            }
        }
    }
    int total = 0;
    while (total < max_exits) {
        int first = -1;
        for (int i = 0; i < nwants; i++) {
            if (carried(s, wants[i].item) < wants[i].count && first < 0) {
                first = i;
            }
        }
        if (first < 0) {
            break;
        }
        int lst = steal_source(wants[first].item);
        if (lst < 0) {
            pl_error("KeyError: None (STEAL_DOORS)");
        }
        Bag bag;
        shir_bag(s, &bag);
        bool full = bag.n >= 16;
        if (full && on_full != NULL) {
            on_full(s, log);
            shir_bag(s, &bag);
            full = bag.n >= 16;
            if (!full) {
                say(log, "shir: her bag was full; what she stole handed on");
            }
        }
        if (full) {
            say(log, "shir: her bag is full");
            break;
        }
        int before = s->shown.n;
        if (lst == 3) {
            visit(s, PASEO, STEAL[lst].door_y, STEAL[lst].door_x, room_policy,
                  NULL, 80, NULL);
        } else {
            buy(s, PASEO, STEAL[lst].door_y, STEAL[lst].door_x, NULL, 0);
        }
        total++;
        tally_add(&exits, STEAL[lst].list);
        ready(s);
        bool took_it = false;
        for (int i = before; i < s->shown.n; i++) {
            took_it = took_it || s->shown.v[i] == 0x1722;
        }
        if (took_it || !shir_in_party(s)) {
            tally_add(&thefts, STEAL[lst].list);
            if (!fetch_shir(s, log)) {
                break;
            }
            shir_bag(s, &bag);
            const char *took = (bag.n > 0) ? bag.v[bag.n - 1].name : "?";
            tally_add(&items, took);
            say(log, "shir: exit %d (%s): took %s", total, STEAL[lst].list,
                took);
        }
    }
    tally_json(&exits, ex, sizeof ex);
    tally_json(&thefts, th, sizeof th);
    tally_json(&items, it, sizeof it);
    if (items_out != NULL) {
        tally_repr(&items, items_out, items_cap);
    }
    say(log, "shir: {\"exits\": %s, \"thefts\": %s, \"items\": %s}", ex, th,
        it);
}
