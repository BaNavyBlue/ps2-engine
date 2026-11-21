/* (build 171) play.py's grinds on a ground: encounter_group (battle.json's
 * regions), the Ground (a piece of one level of creature, its anchors and
 * what is kept off), roam and roam_and_gear, and the saves on the way
 * (trip_save) and lean_short at the end of the trips. */

#include "pl.h"

#include "../core/json.h"

#include <stdlib.h>

#define DEZOLIS_SKURE 0x03

/* encounter_group(level, cell, riding, late): the group the roll finds
 * where the party stands. */
int encounter_group(int level, int x, int y, bool riding, bool late)
{
    const JsonValue *regions = json_get(battle_data(), "regions");
    if (level == MOTAVIA) {
        char name[40];
        snprintf(name, sizeof name, "motavia%s%s", late ? "_late" : "",
                 riding ? "_scooter" : "");
        const JsonValue *r = json_get(regions, name);
        int i = (pydiv(y, 16) & 7) * 12 + (pydiv(x, 16) & 15);
        const JsonValue *v = json_at(r, i);
        if (v == NULL) {
            pl_error("KeyError: regions[%s][%d]", name, i);
        }
        return (int)v->number;
    }
    if (level == DEZOLIS_SKURE) {
        const JsonValue *r = json_get(regions, "skure");
        int i = (pydiv(y, 16) & 7) * 8 + (pydiv(x, 16) & 7);
        const JsonValue *v = json_at(r, i);
        if (v == NULL) {
            pl_error("KeyError: regions[skure][%d]", i);
        }
        return (int)v->number;
    }
    return level_enemy_group(level, late);
}

static bool in_groups(int g, const int *groups, int n)
{
    for (int i = 0; i < n; i++) {
        if (groups[i] == g) {
            return true;
        }
    }
    return false;
}

Ground *ground_new(int level, const int *groups, int ngroups, int sx, int sy,
                   const Cell *anchors, int nanchors)
{
    LevelMap *lm = level_map(level);
    Ground *g = pl_alloc(sizeof *g);
    g->level = level;
    /* the seed: the nearest open cell of the groups in the 7x7 round the
     * start, x outer, y inner, the first of the nearest */
    bool found = false;
    int bx = 0, by = 0, bd = 0;
    for (int x = sx - 3; x < sx + 4; x++) {
        for (int y = sy - 3; y < sy + 4; y++) {
            if (!lm_open(lm, pymod(x, lm->w), pymod(y, lm->h))
                || !in_groups(encounter_group(level, x, y, false, false),
                              groups, ngroups)) {
                continue;
            }
            int d = abs(x - sx) + abs(y - sy);
            if (!found || d < bd) {
                found = true;
                bx = x;
                by = y;
                bd = d;
            }
        }
    }
    if (!found) {
        pl_error("ValueError: min() arg is an empty sequence (Ground)");
    }
    bool bar_on = level != MOTAVIA;
    static const int STEP[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
    cset_add(&g->cells, bx, by, 0);
    for (int head = 0; head < g->cells.n; head++) {
        Cell c = cset_cell(&g->cells, head);
        for (int k = 0; k < 4; k++) {
            int nx = pymod(c.x + STEP[k][0], lm->w);
            int ny = pymod(c.y + STEP[k][1], lm->h);
            if (cset_has(&g->cells, nx, ny) || !lm_open(lm, nx, ny)) {
                continue;
            }
            if (bar_on && (lm_special_open(lm, nx, ny)
                           || lm_damage(lm, nx, ny))) {
                continue;
            }
            if (!in_groups(encounter_group(level, nx, ny, false, false),
                           groups, ngroups)) {
                continue;
            }
            cset_add(&g->cells, nx, ny, 0);
        }
    }
    /* anchors: the nearest cell to each, the lesser (x, y) of a tie */
    g->anchors = pl_alloc((size_t)(nanchors + 1) * sizeof *g->anchors);
    g->anchor_n = nanchors;
    for (int a = 0; a < nanchors; a++) {
        long best = -1;
        Cell bc = { 0, 0 };
        for (int i = 0; i < g->cells.n; i++) {
            Cell c = cset_cell(&g->cells, i);
            long dx = c.x - anchors[a].x, dy = c.y - anchors[a].y;
            long d = dx * dx + dy * dy;
            if (best < 0 || d < best
                || (d == best && (c.x < bc.x || (c.x == bc.x && c.y < bc.y)))) {
                best = d;
                bc = c;
            }
        }
        g->anchors[a] = bc;
    }
    for (int x = 0; x < lm->w; x++) {
        for (int y = 0; y < lm->h; y++) {
            if (lm_open(lm, x, y) && !cset_has(&g->cells, x, y)) {
                cset_add(&g->keep_off, x, y, 0);
            }
        }
    }
    return g;
}

RoamOpts roam_defaults(void)
{
    RoamOpts o;
    memset(&o, 0, sizeof o);
    o.fights_per_trip = 8;
    o.trips = 60;
    o.reserve = 120;
    return o;
}

static void res_after_fight(Session *s, void *ctx)
{
    (void)ctx;
    cast_res(s, 0.75);
}

/* roam: the ground's long legs, anchor to anchor from the one after the
 * nearest, until `fights` fights or hurt with nothing to mend with. */
static int roam(Session *s, Ground *g, int fights, IntsList *out)
{
    int legs = 0;
    int hx = s->state.x, hy = s->state.y;
    if (hx == PL_NONE || hy == PL_NONE) {
        pl_error("TypeError: roam off the map");
    }
    int i = 0, bd = -1;
    for (int k = 0; k < g->anchor_n; k++) {
        int d = abs(g->anchors[k].x - hx) + abs(g->anchors[k].y - hy);
        if (bd < 0 || d < bd) {
            bd = d;
            i = k;
        }
    }
    bool careful = s->careful;
    s->careful = true;
    while (out->n < fights && legs < 3 * g->anchor_n) {
        if (!ready(s) || s->state.level != g->level) {
            break;
        }
        cure_poison(s);
        if (needs_healing(s, 0.5)) {
            mend(s, 0.6);
            if (needs_healing(s, 0.5)) {
                break;
            }
        }
        i = (i + 1) % g->anchor_n;
        CSet goal = { 0 };
        cset_add(&goal, g->anchors[i].x, g->anchors[i].y, 0);
        GotoOpts o = goto_defaults();
        o.fights = out;
        o.keep_off = &g->keep_off;
        o.after_fight = res_after_fight;
        goto_opts(s, &goal, &o);
        cset_free(&goal);
        legs++;
        if (st_screen(&s->state, "gameover") || st_screen(&s->state, "sega")
            || st_screen(&s->state, "title") || s->hurt) {
            break;
        }
    }
    s->careful = careful;
    return legs;
}

/* ---- the saves on the way ---- */

static bool trip_save_stage(const char *stage)
{
    static const char *const STAGES[] = {
        "arima", "oputa", "amy", "lab_in", "zema", "climatrol",
        "dezolis_grind", "kain_grind"
    };
    for (size_t i = 0; i < sizeof STAGES / sizeof STAGES[0]; i++) {
        if (streq(STAGES[i], stage)) {
            return true;
        }
    }
    return false;
}

static int marks_find(const Marks *m, const char *name)
{
    for (int i = 0; i < m->n; i++) {
        if (streq(m->name[i], name)) {
            return i;
        }
    }
    return -1;
}

static int str_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* json.dumps of a string: the ASCII ones as they are, quotes and
 * backslashes escaped. */
static size_t json_string(char *out, size_t n, const char *s)
{
    size_t used = (size_t)snprintf(out, n, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p && used < n;
         p++) {
        if (*p == '"' || *p == '\\') {
            used += (size_t)snprintf(out + used, n - used, "\\%c", *p);
        } else if (*p < 0x20 || *p >= 0x7F) {
            used += (size_t)snprintf(out + used, n - used, "\\u%04x", *p);
        } else {
            used += (size_t)snprintf(out + used, n - used, "%c", *p);
        }
    }
    if (used < n) {
        used += (size_t)snprintf(out + used, n - used, "\"");
    }
    return used;
}

bool trip_save(Session *s, int town, Report *log, bool saved)
{
    char nb[32];
    if (pl_mid_saves[0] == '\0' || !trip_save_stage(s->stage)) {
        return false;
    }
    const State *st = &s->state;
    if (!st_screen(st, "level") || st->level != town) {
        return false;
    }
    TownBuilding b;
    if (!town_building(town, BUILDING_DATA_MEMORY, &b)) {
        return false;
    }
    if (is_dezolis_level(town)) {
        int rolf = -1;
        for (int i = 0; i < st->party_n; i++) {
            if (rolf < 0 && streq(st->party[i].name, "Rolf")) {
                rolf = i;
            }
        }
        if (rolf < 0 || st->party[rolf].dead) {
            return false;
        }
        const char *head = worn_head(s, "Rolf");
        if (head == NULL || !(streq(head, "MagicCap")
                              || streq(head, "MogicCap"))) {
            return false;
        }
    }
    if (party_dead(s)) {
        return false;
    }
    const Marks *last = &s->save_marks;
    Marks now;
    trip_marks(s, &now);
    char rose[256] = "", joined[256] = "";
    size_t nr = 0, nj = 0;
    for (int i = 0; i < now.n; i++) {
        int k = marks_find(last, now.name[i]);
        if (k >= 0 && now.level[i] > last->level[k]) {
            nr += (size_t)snprintf(rose + nr, sizeof rose - nr, "%s%s",
                                   nr ? ", " : "", now.name[i]);
        }
    }
    for (int i = 0; i < now.n; i++) {
        if (marks_find(last, now.name[i]) < 0) {
            nj += (size_t)snprintf(joined + nj, sizeof joined - nj, "%s%s",
                                   nj ? ", " : "", now.name[i]);
        }
    }
    bool story = s->story_point;
    if (!(nr || nj || story)) {
        return false;
    }
    double t0 = pl_now();
    if (!saved) {
        save_at(s, 1, town, b.y, b.x);
    }
    char src[300];
    snprintf(src, sizeof src, "%s/slot1.json", s->saves_dir);
    if (!pl_file_exists(src) || (!saved && pl_mtime(src) < t0)) {
        say(log, "trip save: none written in %d", town);
        return false;
    }
    pl_make_dirs(pl_mid_saves);
    char dst[400], note[400];
    snprintf(dst, sizeof dst, "%s/mid_%s.json", pl_mid_saves, s->stage);
    snprintf(note, sizeof note, "%s/mid_%s.note.json", pl_mid_saves,
             s->stage);
    pl_copy_file(src, dst);
    {
        int n = s->progress.n;
        char **v = pl_alloc((size_t)(n + 1) * sizeof *v);
        memcpy(v, s->progress.v, (size_t)n * sizeof *v);
        qsort(v, (size_t)n, sizeof *v, str_cmp);
        size_t cap = 64;
        for (int i = 0; i < n; i++) {
            cap += 8 + 6 * strlen(v[i]);
        }
        char *text = pl_alloc(cap);
        size_t used = (size_t)snprintf(text, cap, "{\"progress\": [");
        for (int i = 0; i < n; i++) {
            if (i) {
                used += (size_t)snprintf(text + used, cap - used, ", ");
            }
            used += json_string(text + used, cap - used, v[i]);
        }
        snprintf(text + used, cap - used, "]}\n");
        pl_write_text(note, text);
        free(text);
        free(v);
    }
    s->save_marks = now;
    s->story_point = false;
    char why[600] = "";
    size_t nw = 0;
    if (nr) {
        nw += (size_t)snprintf(why + nw, sizeof why - nw, "%s up", rose);
    }
    if (nj) {
        nw += (size_t)snprintf(why + nw, sizeof why - nw, "%s%s joined",
                               nw ? ", " : "", joined);
    }
    if (story) {
        snprintf(why + nw, sizeof why - nw, "%sa story point", nw ? ", " : "");
    }
    char levels[256] = "";
    size_t nl = 0;
    for (int i = 0; i < now.n; i++) {
        nl += (size_t)snprintf(levels + nl, sizeof levels - nl, "%s%s L%d",
                               i ? " " : "", now.name[i], now.level[i]);
    }
    say(log, "trip save: %s in %s (%s) -- %s", s->stage,
        level_name(town, nb, sizeof nb), why, levels);
    /* (build 179) ON_SAVE: COMPUTER PLAYS's last.json */
    if (pl_on_save != NULL) {
        pl_on_save("mid", s->stage, dst);
    }
    return true;
}

static const char *levels_line(const Session *s, char *out, size_t n)
{
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < s->state.party_n && used < n; i++) {
        used += (size_t)snprintf(out + used, n - used, "%s%s L%d",
                                 i ? " " : "", s->state.party[i].name,
                                 s->state.party[i].level);
    }
    return out;
}

/* lean_short: with LEAN, the party healed and geared in town and True. */
bool lean_short(Session *s, int town, int target_level, Report *log)
{
    char lv[256];
    if (!pl_lean) {
        return false;
    }
    if (!st_screen(&s->state, "level") && !st_screen(&s->state, "building")) {
        return false;
    }
    if (party_dead(s) && !retreat(s, town, log)) {
        return false;
    }
    if (s->state.level != town && !enter_town(s, town)) {
        return false;
    }
    heal_here(s, town);
    gear_up(s, town, GEAR_RESERVE, log);
    levels_line(s, lv, sizeof lv);
    if (target_level) {
        say(log, "lean: short of L%d, going on -- %s", target_level, lv);
    } else {
        say(log, "lean: out of trips with gear still to buy, going on -- %s",
            lv);
    }
    return true;
}

/* ---- roam_and_gear ---- */

/* A trip line's tail: each member's level and HP, then the meseta. */
void trip_party(const State *st, char *line, size_t used, size_t cap)
{
    for (int i = 0; i < st->party_n && used < cap; i++) {
        const Member *m = &st->party[i];
        used += (size_t)snprintf(line + used, cap - used, "%s%s L%d %d/%d%s",
                                 i ? " " : "", m->name, m->level, m->hp,
                                 m->max_hp, m->dead ? " dead" : "");
    }
    if (used < cap) {
        if (st->meseta == PL_NONE) {
            snprintf(line + used, cap - used, "; meseta None");
        } else {
            snprintf(line + used, cap - used, "; meseta %d", st->meseta);
        }
    }
}

static bool roam_and_gear_body(Session *s, int town, int target_level,
                               Ground *g, const RoamOpts *o)
{
    Report *log = o->log;
    for (int trip = 0; trip < o->trips; trip++) {
        if (s->state.level == town) {
            if (needs_healing(s, 0.99)) {
                heal_here(s, town);
            }
            gear_up(s, town, o->reserve, log);
            stock_mates(s, town, log);
            trip_save(s, town, log, false);
            if (!town_exit(s)) {
                say(log, "could not leave town");
                return false;
            }
        }
        if (!(s->state.level == g->level
              && s->state.x != PL_NONE && s->state.y != PL_NONE
              && cset_has(&g->cells, s->state.x, s->state.y))) {
            bool careful = s->careful;
            s->careful = true;
            travel(s, g->level, &g->cells, log);
            s->careful = careful;
        }
        IntsList fights = { 0 };
        int legs = 0;
        if (s->state.level == g->level && !needs_healing(s, 0.5)) {
            legs = roam(s, g, o->fights_per_trip, &fights);
        }
        int nf = fights.n;
        intslist_free(&fights);
        const State *st = &s->state;
        char line[1024];
        size_t used = (size_t)snprintf(line, sizeof line,
                                       "trip %d: %d fights, %d legs; ", trip,
                                       nf, legs);
        trip_party(st, line, used, sizeof line);
        say(log, "%s", line);
        if (!st_screen(st, "level") && !st_screen(st, "battle")) {
            say(log, "lost: %s", st->valid ? st->screen : "?");
            return false;
        }
        if (party_dead(s)) {
            if (!retreat(s, town, log)) {
                return false;
            }
            continue;
        }
        st = &s->state;
        int lvl = 0;
        if (o->lowest && st->party_n > 0) {
            lvl = st->party[0].level;
            for (int i = 1; i < st->party_n; i++) {
                if (st->party[i].level < lvl) {
                    lvl = st->party[i].level;
                }
            }
        } else if (st->party_n > 0) {
            lvl = st->party[0].level;
        }
        if (st->party_n > 0 && lvl >= target_level) {
            enter_town(s, town);
            heal_here(s, town);
            gear_up(s, town, o->reserve, log);
            if (!o->until_geared || geared(s, town)) {
                return true;
            }
            continue;
        }
        if (!enter_town(s, town)) {
            say(log, "could not get back to town");
            return false;
        }
    }
    return lean_short(s, town, target_level, log);
}

bool roam_and_gear(Session *s, int town, int target_level, Ground *g,
                   const RoamOpts *o)
{
    in_grind_begin(s);
    bool ok = roam_and_gear_body(s, town, target_level, g, o);
    in_grind_end(s);
    return ok;
}

/* ---- (build 172) grind_and_gear: the little square by the gate ---- */

static void step_of(const char *d, int *dx, int *dy)
{
    *dx = *dy = 0;
    if (streq(d, "up")) {
        *dy = -1;
    } else if (streq(d, "down")) {
        *dy = 1;
    } else if (streq(d, "left")) {
        *dx = -1;
    } else if (streq(d, "right")) {
        *dx = 1;
    }
}

/* open_walk: a pattern's walk cut short at the first solid cell ahead,
 * into out; false for None (the very next cell solid). A walk not of the
 * form, or the party off the map, as it is. */
static bool open_walk(Session *s, const char *walk, char *out, size_t cap)
{
    char d[8];
    int count = 0, end = 0;
    const State *st = &s->state;
    snprintf(out, cap, "%s", walk);
    if (sscanf(walk, "walk %7[a-z] %d%n", d, &count, &end) != 2
        || walk[end] != '\0'
        || !(streq(d, "up") || streq(d, "down") || streq(d, "left")
             || streq(d, "right"))
        || st->level == PL_NONE || st->x == PL_NONE) {
        return true;
    }
    const LevelMap *lm = level_map(st->level);
    int dx, dy;
    step_of(d, &dx, &dy);
    int x = st->x, y = st->y, n = 0;
    while (n < count) {
        x = pymod(x + dx, lm->w);
        y = pymod(y + dy, lm->h);
        if (!lm_open(lm, x, y)) {
            break;
        }
        n++;
    }
    if (n == 0) {
        return false;
    }
    snprintf(out, cap, "walk %s %d", d, n);
    return true;
}

int wander(Session *s, const char *const *walks, int nwalks, int fights,
           bool mates)
{
    int out = 0, i = 0, shut = 0;
    int level = s->state.level;
    while (out < fights) {
        if (!ready(s)) {
            break;
        }
        cure_poison(s);
        if (needs_healing(s, 0.5) || s->state.level != level) {
            break;
        }
        char walk[64];
        bool open = open_walk(s, walks[i % nwalks], walk, sizeof walk);
        i++;
        if (!open) {
            /* nothing open that way: the next walk, and none open in any
             * of them, the end of it */
            shut++;
            if (shut >= nwalks) {
                break;
            }
            continue;
        }
        shut = 0;
        send1(s, walk);
        if (!st_waiting(&s->state)) {
            send1(s, "wait idle");              /* the fade into a fight */
        }
        if (st_screen(&s->state, "battle")) {
            FightOpts fo = fight_defaults();
            Ints shown = { 0 };
            fight_smart(s, &fo, &shown);
            ints_free(&shown);
            out++;
            if (st_screen(&s->state, "gameover")
                || st_screen(&s->state, "sega")
                || st_screen(&s->state, "title")) {
                break;
            }
            cast_res(s, 0.75);
            if (mates && needs_healing(s, 0.5)) {
                mend(s, 0.6);
            }
        }
    }
    return out;
}

static const char *const GRIND_WALKS[4] = {
    "walk down 3", "walk left 3", "walk up 3", "walk right 3"
};

GrindOpts grind_defaults(void)
{
    GrindOpts o;
    memset(&o, 0, sizeof o);
    o.fights_per_trip = 5;
    o.trips = 60;
    o.walks = GRIND_WALKS;
    o.nwalks = 4;
    o.reserve = 120;
    return o;
}

static bool grind_and_gear_body(Session *s, int town, int target_level,
                                const GrindOpts *o)
{
    Report *log = o->log;
    for (int trip = 0; trip < o->trips; trip++) {
        if (s->state.level == town) {
            if (needs_healing(s, 0.99)) {
                heal_here(s, town);
            }
            gear_up(s, town, o->reserve, log);
            stock_mates(s, town, log);
            trip_save(s, town, log, false);
            if (!town_exit(s)) {
                say(log, "could not leave town");
                return false;
            }
            if (o->has_spot) {
                /* open ground to walk the pattern on */
                CSet spot = { 0 };
                cset_add(&spot, o->spot.x, o->spot.y, 0);
                goto_cells(s, &spot);
                cset_free(&spot);
            }
        }
        int nf = wander(s, o->walks, o->nwalks, o->fights_per_trip, true);
        const State *st = &s->state;
        char line[1024];
        size_t used = (size_t)snprintf(line, sizeof line,
                                       "trip %d: %d fights; ", trip, nf);
        trip_party(st, line, used, sizeof line);
        say(log, "%s", line);
        if (!st_screen(st, "level") && !st_screen(st, "battle")) {
            say(log, "lost: %s", st->valid ? st->screen : "?");
            return false;
        }
        if (party_dead(s)) {
            if (!retreat(s, town, log)) {
                return false;
            }
            continue;
        }
        st = &s->state;
        int lvl = 0;
        if (o->lowest && st->party_n > 0) {
            lvl = st->party[0].level;
            for (int i = 1; i < st->party_n; i++) {
                if (st->party[i].level < lvl) {
                    lvl = st->party[i].level;
                }
            }
        } else if (st->party_n > 0) {
            lvl = st->party[0].level;
        }
        if (st->party_n > 0 && lvl >= target_level) {
            enter_town(s, town);
            heal_here(s, town);
            gear_up(s, town, o->reserve, log);
            if (!o->until_geared || geared(s, town)) {
                return true;
            }
            if (!town_exit(s)) {
                say(log, "could not leave town");
                return false;
            }
            continue;
        }
        if (!enter_town(s, town)) {
            say(log, "could not get back to town");
            return false;
        }
    }
    return lean_short(s, town, target_level, log);
}

bool grind_and_gear(Session *s, int town, int target_level,
                    const GrindOpts *o)
{
    in_grind_begin(s);
    bool ok = grind_and_gear_body(s, town, target_level, o);
    in_grind_end(s);
    return ok;
}

/* ---- (build 173) dungeon_grind: a dungeon floor's fights ---- */

static const char *const DUNGEON_WALKS[2] = { "walk left 3", "walk right 3" };

DungeonOpts dungeon_defaults(void)
{
    DungeonOpts o;
    memset(&o, 0, sizeof o);
    o.lowest = true;
    o.trips = 60;
    o.fights_per_trip = 8;
    o.walks = DUNGEON_WALKS;
    o.nwalks = 2;
    return o;
}

/* min(levels, default=0) */
static int lowest_level(const State *st)
{
    int low = 0;
    for (int i = 0; i < st->party_n; i++) {
        if (i == 0 || st->party[i].level < low) {
            low = st->party[i].level;
        }
    }
    return low;
}

static bool dungeon_done(const Session *s, int target_level, bool lowest)
{
    const State *st = &s->state;
    if (st->party_n == 0) {
        return false;
    }
    int lv = lowest ? lowest_level(st) : st->party[0].level;
    return lv >= target_level;
}

static bool dungeon_grind_body(Session *s, int level, int sx, int sy,
                               int town, int target_level,
                               const DungeonOpts *o)
{
    char nb[32], c[32];
    Report *log = o->log;
    CSet spot = { 0 };
    const CSet *goals = &spot;
    if (o->ground != NULL) {
        goals = &o->ground->cells;
    } else {
        cset_add(&spot, sx, sy, 0);
    }
    int low_in_town = lowest_level(&s->state);
    bool ok = false;
    for (int trip = 0; trip < o->trips; trip++) {
        if (dungeon_done(s, target_level, o->lowest)) {
            cset_free(&spot);
            return true;
        }
        const State *st = &s->state;
        bool away = o->ground == NULL || st->level != level
                 || st->x == PL_NONE || st->y == PL_NONE
                 || !cset_has(goals, st->x, st->y);
        if (away && !travel(s, level, goals, log)) {
            say(log, "dungeon_grind: could not reach %s %s",
                level_name(level, nb, sizeof nb),
                (o->ground == NULL) ? xy_repr(sx, sy, c, sizeof c)
                                    : "ground");
            if (!st_screen(&s->state, "level")
                && !st_screen(&s->state, "battle")) {
                cset_free(&spot);
                return false;
            }
            retreat(s, town, log);
            continue;
        }
        int nf;
        char legs[32] = "";
        if (o->ground == NULL) {
            nf = wander(s, o->walks, o->nwalks, o->fights_per_trip, true);
        } else {
            IntsList fights = { 0 };
            int n = roam(s, o->ground, o->fights_per_trip, &fights);
            nf = fights.n;
            intslist_free(&fights);
            snprintf(legs, sizeof legs, ", %d legs", n);
        }
        st = &s->state;
        char line[1024];
        size_t used = (size_t)snprintf(line, sizeof line,
                                       "trip %d on %s: %d fights%s; ", trip,
                                       level_name(level, nb, sizeof nb), nf,
                                       legs);
        trip_party(st, line, used, sizeof line);
        say(log, "%s", line);
        if (!st_screen(st, "level") && !st_screen(st, "battle")) {
            say(log, "lost: %s", st->valid ? st->screen : "None");
            cset_free(&spot);
            return false;
        }
        bool dead = party_dead(s);
        int low = lowest_level(st);
        /* with `gear`, back to town too when the lowest member has risen
         * since the last time there: the new gear, and a save on the
         * way */
        bool back = o->gear && pl_mid_saves[0] != '\0' && low > low_in_town;
        if (dead || (needs_healing(s, 0.5) && count_mates(s) == 0) || back) {
            retreat(s, town, log);
            if (o->gear && s->state.level == town) {
                gear_up(s, town, GEAR_RESERVE, log);
                trip_save(s, town, log, false);
                low_in_town = lowest_level(&s->state);
            }
        }
    }
    cset_free(&spot);
    ok = dungeon_done(s, target_level, o->lowest);
    return ok || lean_short(s, town, target_level, log);
}

bool dungeon_grind(Session *s, int level, int spot_x, int spot_y, int town,
                   int target_level, const DungeonOpts *o)
{
    in_grind_begin(s);
    bool ok = dungeon_grind_body(s, level, spot_x, spot_y, town,
                                 target_level, o);
    in_grind_end(s);
    return ok;
}

/* (build 175) field_spot(level, near, reach): the open cell nearest `near`
 * with `reach` open cells each way across and down -- somewhere wander's
 * walks are not blocked -- that `near` can walk to; false for None. */
bool field_spot(int level, int nx, int ny, int reach, Cell *out)
{
    LevelMap *lm = level_map(level);
    bool found = false;
    long best = 0;
    CSet goal = { 0 };
    Cells way = { 0 };
    for (int cy = 0; cy < lm->h; cy++) {
        for (int cx = 0; cx < lm->w; cx++) {
            bool clear = true;
            for (int d = -reach; d <= reach && clear; d++) {
                clear = lm_open(lm, cx + d, cy) && lm_open(lm, cx, cy + d);
            }
            if (!clear) {
                continue;
            }
            long d2 = (long)(cx - nx) * (cx - nx) + (long)(cy - ny) * (cy - ny);
            if (!found || d2 < best) {
                cset_clear(&goal);
                cset_add(&goal, cx, cy, 0);
                if (lm_path(lm, nx, ny, &goal, NULL, NULL, &way)) {
                    found = true;
                    best = d2;
                    out->x = cx;
                    out->y = cy;
                }
            }
        }
    }
    cset_free(&goal);
    cells_free(&way);
    return found;
}
