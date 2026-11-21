/* (build 171) play.py's fights: fight_smart and what it orders through
 * the battle's menus (battle_order, battle_order_item and the paged
 * lists), the probe's `techs` line, battle.json's enemies and formations,
 * and the planners: plan_heal, plan_deban, plan_shift, plan_type_tech. */

#include "pl.h"

#include "../core/json.h"

#include <stdlib.h>

#define WIN_BATTLE_COMMANDS  0x63   /* ATTACK TECH ITEM DEFEND, across */
#define WIN_ENEMY_GROUPS     0x64
#define WIN_BATTLE_TECH_LIST 0x66
#define WIN_BATTLE_ITEM_LIST 0x67

enum { CMD_ATTACK = 0, CMD_TECH = 1, CMD_ITEM = 2, CMD_DEFEND = 3 };

#define SCRIPT_VICTORY 0x1211       /* "<leader> and party are victorious!" */
#define SHIFT_CASTS    2

#define TECH_MARGIN           1.25
#define TECH_GAIN_PER_TP      2.0
#define BOSS_TECH_MARGIN      1.0
#define BOSS_TECH_GAIN_PER_TP 0.5

FightOpts fight_defaults(void)
{
    FightOpts o;
    memset(&o, 0, sizeof o);
    o.run_below = 0.25;
    o.mate_below = 0.45;
    o.limit = 200;
    o.heal_below = 0.5;
    o.use_techs = true;
    o.boss_techs = true;
    o.deban = true;
    o.reset_techs = true;
    return o;
}

/* ---- battle.json ---- */

static JsonValue *g_battle;

const JsonValue *battle_data(void)
{
    if (g_battle == NULL) {
        char *text = pl_read_file("assets/data/battle.json", NULL);
        g_battle = (text != NULL) ? json_parse(text, NULL) : NULL;
        free(text);
        if (g_battle == NULL) {
            pl_error("cannot read assets/data/battle.json");
        }
    }
    return g_battle;
}

/* enemy(eid): battle.json's enemy of the id (the last of it), or NULL. */
const JsonValue *enemy_of(int eid)
{
    const JsonValue *all = json_get(battle_data(), "enemies");
    const JsonValue *hit = NULL;
    for (int i = 0; i < json_length(all); i++) {
        const JsonValue *e = json_at(all, i);
        if (json_int(e, "id", -1) == eid) {
            hit = e;
        }
    }
    return hit;
}

/* formation_of(f): entry 0 is formation 1; the bosses $100-$103 follow
 * the 255. */
const JsonValue *formation_of(int f)
{
    const JsonValue *forms = json_get(battle_data(), "formations");
    int i = (f < 0x100) ? f - 1 : 255 + (f - 0x100);
    return (i >= 0 && i < json_length(forms)) ? json_at(forms, i) : NULL;
}

typedef struct {
    int g, eid, type;
    int hp[PL_ENEMIES_MAX];
    int n;
} Group;

/* live_groups(st): (group, enemy id, type, the live ones' HP). */
static int live_groups(const State *st, Group *out)
{
    int f = (st->formation == PL_NONE) ? 0 : st->formation;
    const JsonValue *form = formation_of(f);
    if (form == NULL) {
        return 0;
    }
    int n = 0;
    for (int g = 0; g < 2; g++) {
        int eid = (int)json_int(form, g ? "enemy2" : "enemy1", 0);
        if (eid == 0) {
            continue;
        }
        Group *gr = &out[n];
        gr->n = 0;
        for (int i = 0; i < st->enemy_n; i++) {
            if (st->enemy[i].id == eid && st->enemy[i].hp > 0) {
                gr->hp[gr->n++] = st->enemy[i].hp;
            }
        }
        if (gr->n > 0) {
            const JsonValue *e = enemy_of(eid);
            gr->g = g;
            gr->eid = eid;
            gr->type = (int)json_int(e, "type", 0);
            n++;
        }
    }
    return n;
}

static int escape_of(int eid)
{
    const JsonValue *e = enemy_of(eid);
    return (int)json_int(e, "escape", 1);
}

static bool is_boss_fight(const State *st)
{
    Group gr[2];
    int n = live_groups(st, gr);
    for (int i = 0; i < n; i++) {
        if (escape_of(gr[i].eid) == 0) {
            return true;
        }
    }
    return false;
}

/* ---- the probe's techs line ---- */

typedef struct {
    char name[16];
    int  tp, flags;
} BTech;

typedef struct {
    char  name[16];
    char  cmd[12];
    BTech techs[24];
    int   n;
} Known;

typedef struct {
    Known m[PL_PARTY_MAX];
    int   n;
} KnownList;

const char *ok_word(const char *ln, const char *word)
{
    size_t k = strlen(word);
    for (const char *q = strstr(ln, "ok"); q != NULL; q = strstr(q + 1, "ok")) {
        const char *r = q + 2;
        if (*r != ' ' && *r != '\t') {
            continue;
        }
        while (*r == ' ' || *r == '\t') {
            r++;
        }
        if (strncmp(r, word, k) == 0 && r[k] == ' ') {
            return r + k + 1;
        }
    }
    return NULL;
}

/* techs(s): by party position, {name, cmd, techs: (NAME, tp, flags)}. */
static void techs(Session *s, KnownList *out)
{
    memset(out, 0, sizeof *out);
    const Lines *lines = send1(s, "techs");
    for (int i = 0; i < lines->n; i++) {
        const char *p = ok_word(lines->v[i], "techs");
        if (p == NULL) {
            continue;
        }
        while (out->n < PL_PARTY_MAX) {
            const char *end = strstr(p, "; ");
            size_t len = (end != NULL) ? (size_t)(end - p) : strlen(p);
            char part[512];
            if (len >= sizeof part) {
                len = sizeof part - 1;
            }
            memcpy(part, p, len);
            part[len] = '\0';
            Known *k = &out->m[out->n++];
            char *sep = strstr(part, ": ");
            char *rest = "";
            if (sep != NULL) {
                *sep = '\0';
                rest = sep + 2;
            }
            snprintf(k->name, sizeof k->name, "%.15s", part);
            int w = 0;
            for (char *tok = strtok(rest, " \t"); tok != NULL;
                 tok = strtok(NULL, " \t"), w++) {
                if (w == 0) {
                    snprintf(k->cmd, sizeof k->cmd, "%s", tok);
                    continue;
                }
                char *c1 = strchr(tok, ':');
                char *c2 = (c1 != NULL) ? strchr(c1 + 1, ':') : NULL;
                if (c2 == NULL || strchr(c2 + 1, ':') != NULL) {
                    pl_error("ValueError: techs entry %s", tok);
                }
                *c1 = '\0';
                *c2 = '\0';
                const char *fl = c2 + 1;
                while (*fl == '$') {
                    fl++;
                }
                char *e1, *e2;
                long tp = strtol(c1 + 1, &e1, 10);
                long flags = strtol(fl, &e2, 16);
                if (*e1 != '\0' || *e2 != '\0' || e1 == c1 + 1 || e2 == fl) {
                    pl_error("ValueError: techs entry %s", tok);
                }
                if (k->n < 24) {
                    snprintf(k->techs[k->n].name, 16, "%s", tok);
                    k->techs[k->n].tp = (int)tp;
                    k->techs[k->n].flags = (int)flags;
                    k->n++;
                }
            }
            if (end == NULL) {
                break;
            }
            p = end + 2;
        }
        return;
    }
}

/* known = techs(s) or known */
static void techs_or(Session *s, KnownList *known)
{
    KnownList fresh;
    techs(s, &fresh);
    if (fresh.n > 0) {
        *known = fresh;
    }
}

/* ---- the battle's menus ---- */

/* battle_stop: the cursor's stop for party position pos (4, 0, 2, 6:
 * Battle_CharacterIdIndex), the positions themselves under three. */
static int battle_stop(const Session *s, int pos)
{
    if (s->state.party_n <= 2) {
        return pos;
    }
    static const int STOP[4] = { 1, 2, 0, 3 };
    if (pos < 0 || pos > 3) {
        pl_error("KeyError: %d", pos);
    }
    return STOP[pos];
}

static bool menu_escape_battle(Session *s)
{
    for (int i = 0; i < 6; i++) {
        if (s->state.menu == WIN_BATTLE_OPTS
            || !st_screen(&s->state, "battle")) {
            return true;
        }
        send2(s, "press b", "wait idle");
    }
    return false;
}

/* battle_list_pick: four to a page; NEXT at stop 0 from five items. */
static bool battle_list_pick(Session *s, int slot)
{
    int stops = (s->state.menu_stops == PL_NONE) ? 0 : s->state.menu_stops;
    int window = s->state.menu;
    if (stops < 5 && slot < stops) {
        return menu_pick(s, slot);
    }
    if (stops != 5 && slot >= stops) {
        return false;
    }
    for (int i = 0; i < slot / 4; i++) {
        if (!menu_pick(s, 0)) {
            return false;
        }
        send1(s, "wait idle");
        if (s->state.menu != window) {
            return false;
        }
    }
    stops = (s->state.menu_stops == PL_NONE) ? 0 : s->state.menu_stops;
    if (slot % 4 + 1 >= stops) {
        return false;
    }
    return menu_pick(s, slot % 4 + 1);
}

static bool battle_order_item(Session *s, int holder, int slot, int target)
{
    if (s->state.menu != WIN_BATTLE_OPTS) {
        return false;
    }
    menu_pick(s, 1);                            /* STGY */
    send1(s, "wait idle");
    if (s->state.menu != WIN_BATTLE_OPTS2) {
        return false;
    }
    menu_pick(s, 0);                            /* ORDR */
    send1(s, "wait idle");
    if (!s->state.menu_party) {
        return false;
    }
    menu_pick(s, battle_stop(s, holder));
    send1(s, "wait idle");
    if (s->state.menu != WIN_BATTLE_COMMANDS) {
        return false;
    }
    menu_pick(s, CMD_ITEM);
    send1(s, "wait idle");
    if (s->state.menu != WIN_BATTLE_ITEM_LIST) {
        return false;
    }
    if (!battle_list_pick(s, slot)) {
        menu_escape_battle(s);
        return false;
    }
    send1(s, "wait idle");
    if (s->state.menu_party) {
        menu_pick(s, battle_stop(s, target));
        send1(s, "wait idle");
    }
    return s->state.menu == WIN_BATTLE_OPTS;
}

/* battle_order(s, holder, cmd, slot, target, group): slot < 0 and target
 * PL_NONE are the Python's None. */
static bool battle_order(Session *s, int holder, int cmd, int slot,
                         int target, int group)
{
    if (s->state.menu != WIN_BATTLE_OPTS) {
        return false;
    }
    menu_pick(s, 1);                            /* STGY */
    send1(s, "wait idle");
    if (s->state.menu != WIN_BATTLE_OPTS2) {
        menu_escape_battle(s);
        return false;
    }
    menu_pick(s, 0);                            /* ORDR */
    send1(s, "wait idle");
    if (!s->state.menu_party) {
        menu_escape_battle(s);
        return false;
    }
    menu_pick(s, battle_stop(s, holder));
    send1(s, "wait idle");
    if (s->state.menu != WIN_BATTLE_COMMANDS) {
        menu_escape_battle(s);
        return false;
    }
    menu_pick(s, cmd);
    send1(s, "wait idle");
    if (cmd == CMD_TECH || cmd == CMD_ITEM) {
        int want = (cmd == CMD_TECH) ? WIN_BATTLE_TECH_LIST
                                     : WIN_BATTLE_ITEM_LIST;
        if (s->state.menu != want || !battle_list_pick(s, slot)) {
            menu_escape_battle(s);
            return false;
        }
        send1(s, "wait idle");
    }
    if (s->state.menu_party && target != PL_NONE) {
        menu_pick(s, battle_stop(s, target));
        send1(s, "wait idle");
    }
    if (s->state.menu == WIN_ENEMY_GROUPS) {
        menu_pick(s, group);
        send1(s, "wait idle");
    }
    if (s->state.menu != WIN_BATTLE_OPTS) {
        menu_escape_battle(s);
        return false;
    }
    return true;
}

static bool is_mate(const char *name)
{
    return streq(name, "Monomate") || streq(name, "Dimate")
        || streq(name, "Trimate");
}

/* battle_find_mate: (holder, slot) of a mate a living member carries. */
static bool battle_find_mate(Session *s, int *holder, int *slot)
{
    const State st = s->state;
    Bags b;
    bags(s, &b);
    for (int h = 0; h < b.n; h++) {
        if (h < st.party_n && st.party[h].dead) {
            continue;
        }
        for (int i = 0; i < b.m[h].n; i++) {
            if (is_mate(b.m[h].v[i].name) && !b.m[h].v[i].worn) {
                *holder = h;
                *slot = i;
                return true;
            }
        }
    }
    return false;
}

/* ---- the planners ---- */

static int heal_worth(const char *name)
{
    if (streq(name, "NARES")) {
        return 120;
    }
    if (streq(name, "GIRES")) {
        return 60;
    }
    if (streq(name, "RES")) {
        return 20;
    }
    return 0;
}

typedef struct {
    int h, slot, target;
} Heal;

/* plan_heal: (healer, list slot, target) for the member furthest under
 * heal_below, the smallest heal that covers the deficit, else the
 * largest. */
static bool plan_heal(const State *st, const KnownList *known,
                      double heal_below, Heal *out)
{
    int target = -1;
    double frac = 0;
    for (int i = 0; i < st->party_n; i++) {
        const Member *m = &st->party[i];
        if (m->dead || m->max_hp == 0) {
            continue;
        }
        double f = (double)m->hp / m->max_hp;
        if (target < 0 || f < frac) {
            target = i;
            frac = f;
        }
    }
    if (target < 0 || frac >= heal_below) {
        return false;
    }
    int deficit = st->party[target].max_hp - st->party[target].hp;
    bool any = false;
    int bk0 = 0, bk1 = 0;
    for (int h = 0; h < st->party_n; h++) {
        const Member *m = &st->party[h];
        if (m->dead || h >= known->n) {
            continue;
        }
        for (int slot = 0; slot < known->m[h].n; slot++) {
            const BTech *t = &known->m[h].techs[slot];
            int worth = heal_worth(t->name);
            if (worth == 0 || m->tp < t->tp) {
                continue;
            }
            int k0 = (worth >= deficit) ? 0 : 1;
            int k1 = (worth >= deficit) ? t->tp : -worth;
            if (!any || k0 < bk0 || (k0 == bk0 && k1 < bk1)) {
                any = true;
                bk0 = k0;
                bk1 = k1;
                out->h = h;
                out->slot = slot;
                out->target = target;
            }
        }
    }
    return any;
}

/* plan_deban: (caster, list slot) while half or more of the living party
 * is without it. */
static bool plan_deban(const State *st, const KnownList *known, int *h_out,
                       int *slot_out)
{
    int live = 0, with = 0;
    for (int i = 0; i < st->party_n; i++) {
        if (!st->party[i].dead) {
            live++;
            with += st->party[i].deban;
        }
    }
    if (live == 0 || 2 * with > live) {
        return false;
    }
    for (int h = 0; h < st->party_n; h++) {
        const Member *m = &st->party[h];
        if (m->dead || m->sealed || h >= known->n) {
            continue;
        }
        for (int slot = 0; slot < known->m[h].n; slot++) {
            const BTech *t = &known->m[h].techs[slot];
            if (streq(t->name, "DEBAN") && m->tp >= t->tp) {
                *h_out = h;
                *slot_out = slot;
                return true;
            }
        }
    }
    return false;
}

/* ---- plan_type_tech ---- */

static JsonValue *g_chars, *g_techs;

static void chars_techs_load(void)
{
    if (g_chars == NULL) {
        char *a = pl_read_file("assets/data/characters.json", NULL);
        char *b = pl_read_file("assets/data/techniques.json", NULL);
        g_chars = (a != NULL) ? json_parse(a, NULL) : NULL;
        g_techs = (b != NULL) ? json_parse(b, NULL) : NULL;
        free(a);
        free(b);
        if (g_chars == NULL || g_techs == NULL) {
            pl_error("cannot read characters.json or techniques.json");
        }
    }
}

const JsonValue *techs_data(void)
{
    chars_techs_load();
    return g_techs;
}

/* expected_stats(name, level)[key]: the level table's rows summed. */
int expected_stat(const char *name, int level, const char *key)
{
    static const char *const NAMES[8] = {
        "Rolf", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"
    };
    int cid = -1;
    for (int i = 0; i < 8; i++) {
        if (streq(NAMES[i], name)) {
            cid = i;
        }
    }
    if (cid < 0) {
        pl_error("KeyError: '%s'", name);
    }
    chars_techs_load();
    const JsonValue *rows = json_get(json_at(g_chars, cid), "levels");
    int upto = (level > 1) ? level : 1;
    int sum = 0;
    for (int i = 0; i < upto && i < json_length(rows); i++) {
        sum += (int)json_int(json_at(rows, i), key, 0);
    }
    return sum;
}

typedef struct {
    int rate, hit, flags;
} TechRec;

/* tech_record(name): TechniqueData by the battle list's NAME or its key,
 * the hit rate from tech_props' first byte. */
static bool tech_record(const char *name, TechRec *out)
{
    chars_techs_load();
    const JsonValue *all = json_get(g_techs, "techniques");
    const JsonValue *props = json_get(battle_data(), "tech_props");
    bool found = false;
    for (int i = 0; i < json_length(all); i++) {
        const JsonValue *t = json_at(all, i);
        int id = (int)json_int(t, "id", 0);
        if (id == 0) {
            continue;
        }
        if (!streq(json_str(t, "name", ""), name)
            && !streq(json_str(t, "key", ""), name)) {
            continue;
        }
        found = true;
        out->rate = (int)json_int(t, "rate", 0);
        out->flags = (int)json_int(t, "flags", 0);
        out->hit = 0;
        if (id - 1 < json_length(props)) {
            const JsonValue *bytes = json_get(json_at(props, id - 1), "bytes");
            const JsonValue *b0 = json_at(bytes, 0);
            out->hit = (b0 != NULL) ? (int)b0->number : 0;
        }
    }
    return found;
}

static double tech_damage(const TechRec *rec)
{
    int rate = (rec->rate == 0xFF) ? 500 : rec->rate;
    return 99.5 * rate / 100;
}

/* ---- (build 176) the enemies ahead: dungeon_mix and tech_power ---- */

static const int MIX_TYPES[3] = { 0x20, 0x40, 0x60 };  /* BIO ROBOT BOTH */

/* group_mix(group): the share of the group's enemy HP by type, its
 * formations each a roll in eight. */
static void group_mix(int group, double out[3])
{
    double hp[3] = { 0.0, 0.0, 0.0 };
    const JsonValue *groups = json_get(battle_data(), "groups");
    if (group != 0 && group < json_length(groups)) {
        const JsonValue *g = json_at(groups, group);
        for (int i = 0; i < json_length(g); i++) {
            int f = (int)json_at(g, i)->number;
            const JsonValue *form = (f != 0) ? formation_of(f) : NULL;
            if (form == NULL) {
                continue;
            }
            for (int k = 1; k <= 2; k++) {
                char ek[8], ck[8];
                snprintf(ek, sizeof ek, "enemy%d", k);
                snprintf(ck, sizeof ck, "count%d", k);
                const JsonValue *e = enemy_of((int)json_int(form, ek, 0));
                int count = (int)json_int(form, ck, 0);
                if (e == NULL || count == 0) {
                    continue;
                }
                int t = (int)json_int(e, "type", 0) & 0x60;
                if (t == 0) {
                    /* dungeon_mix's acc has no key 0 */
                    pl_error("KeyError: 0 (group_mix)");
                }
                hp[t / 0x20 - 1] += (double)count * json_int(e, "hp", 0);
            }
        }
    }
    double total = hp[0] + hp[1] + hp[2];
    if (total == 0.0) {
        total = 1;
    }
    for (int k = 0; k < 3; k++) {
        out[k] = hp[k] / total;
    }
}

bool dungeon_mix(const int *levels, int n, Mix *out)
{
    double acc[3] = { 0.0, 0.0, 0.0 };
    int used = 0;
    for (int i = 0; i < n; i++) {
        int g = level_enemy_group(levels[i], true);
        if (g == 0) {
            continue;
        }
        double m[3];
        group_mix(g, m);
        for (int k = 0; k < 3; k++) {
            acc[k] += m[k];
        }
        used++;
    }
    out->set = used > 0;
    for (int k = 0; k < 3; k++) {
        out->v[k] = used ? acc[k] / used : 0.0;
    }
    return out->set;
}

const char *mix_repr(const Mix *mix, char *out, size_t cap)
{
    static const char *const NAMES[3] = { "living", "robot", "both" };
    size_t used = 0;
    out[0] = '\0';
    for (int k = 0; k < 3 && used < cap; k++) {
        if (mix->v[k] != 0.0) {
            used += (size_t)snprintf(out + used, cap - used, "%s%s:%.2f",
                                     used ? " " : "", NAMES[k], mix->v[k]);
        }
    }
    return out;
}

double tech_power(const char *name, int level, const Mix *mix)
{
    if (mix == NULL || !mix->set) {
        return 0.0;
    }
    int techs_a = expected_stat(name, level, "techs_a");
    const JsonValue *list = json_get(json_get(json_get(techs_data(),
                                                       "available"),
                                              name), "battle");
    if (list == NULL) {
        pl_error("KeyError: '%s'", name);
    }
    double best = 0.0;
    for (int i = 0; i < techs_a && i < json_length(list); i++) {
        const JsonValue *v = json_at(list, i);
        TechRec rec;
        if (v->type != JSON_STRING || !tech_record(v->string, &rec)
            || !(rec.flags & 0x80) || rec.rate == 0) {
            continue;
        }
        double cover = 0.0;
        for (int k = 0; k < 3; k++) {
            if (rec.flags & 0x60 & MIX_TYPES[k]) {
                cover += mix->v[k];
            }
        }
        int reach = (rec.flags & 4) ? 3 : ((rec.flags & 2) ? 2 : 1);
        double dmg = tech_damage(&rec);
        double p = ((dmg < 120.0) ? dmg : 120.0) * reach * cover;
        if (p > best) {
            best = p;
        }
    }
    return best;
}

/* weapon_blow: a FIGHT's mean on one of hps, for each weapon worn, at a
 * 90% hit, capped at the mean HP left. The bags are read once a fight
 * (s._worn). */
static double weapon_blow(Session *s, int member, const char *name, int level,
                          const int *hps, int nh, int defense)
{
    if (s->worn_bags == NULL || s->worn_at != s->fights_n) {
        if (s->worn_bags == NULL) {
            s->worn_bags = pl_alloc(sizeof *s->worn_bags);
        }
        s->worn_at = s->fights_n;
        bags(s, s->worn_bags);
    }
    const Bags *cache = s->worn_bags;
    int atk = expected_stat(name, level, "attack");
    int sum = 0;
    for (int i = 0; i < nh; i++) {
        sum += hps[i];
    }
    double cap = (double)sum / nh;
    double total = 0.0;
    if (member < cache->n) {
        const Bag *worn = &cache->m[member];
        for (int i = 0; i < worn->n; i++) {
            const Item *it = item_get(worn->v[i].name);
            int w = (it != NULL) ? it->attack : 0;
            if (worn->v[i].worn && w) {
                double blow = (99.5 + atk) * (w + 2) / (defense * 5 + 100);
                total += (cap < blow) ? cap : blow;
            }
        }
    }
    return 0.9 * total;
}

typedef struct {
    int  slot, group;
    char name[16];
} TypePick;

static bool plan_type_tech(Session *s, int member, const Known *m,
                           const State *st, bool boss, TypePick *out)
{
    if (member >= st->party_n) {
        pl_error("IndexError: list index out of range (plan_type_tech)");
    }
    const Member *me = &st->party[member];
    Group groups[2];
    int ng = live_groups(st, groups);
    if (ng == 0) {
        return false;
    }
    bool unrunnable = false;
    for (int i = 0; i < ng; i++) {
        unrunnable = unrunnable || escape_of(groups[i].eid) == 0;
    }
    int heal_min = -1;
    for (int i = 0; i < m->n; i++) {
        if (heal_worth(m->techs[i].name) != 0
            && (heal_min < 0 || m->techs[i].tp < heal_min)) {
            heal_min = m->techs[i].tp;
        }
    }
    int budget = me->tp - ((heal_min >= 0) ? 2 * heal_min : 0);
    bool any = false;
    double best = 0;
    for (int slot = 0; slot < m->n; slot++) {
        const BTech *t = &m->techs[slot];
        TechRec rec;
        if (!tech_record(t->name, &rec) || t->tp > budget
            || !(t->flags & 0x80) || !(t->flags & 7) || rec.rate == 0) {
            continue;
        }
        if (unrunnable && rec.rate == 0xFF) {
            continue;
        }
        double dmg = tech_damage(&rec);
        double hit = rec.hit / 256.0;
        for (int gi = 0; gi < ng; gi++) {
            const Group *g = &groups[gi];
            if (!(t->flags & 0x60 & g->type)) {
                continue;
            }
            double value = 0;
            if (t->flags & 4) {                 /* every live enemy */
                for (int k = 0; k < ng; k++) {
                    if (t->flags & 0x60 & groups[k].type) {
                        for (int j = 0; j < groups[k].n; j++) {
                            double h = groups[k].hp[j];
                            value += (dmg < h) ? dmg : h;
                        }
                    }
                }
            } else if (t->flags & 2) {          /* the group */
                for (int j = 0; j < g->n; j++) {
                    double h = g->hp[j];
                    value += (dmg < h) ? dmg : h;
                }
            } else {                            /* one of the group */
                int sum = 0;
                for (int j = 0; j < g->n; j++) {
                    sum += g->hp[j];
                }
                double h = (double)sum / g->n;
                value += (dmg < h) ? dmg : h;
            }
            value = hit * value;
            const JsonValue *e = enemy_of(g->eid);
            double blow = weapon_blow(s, member, me->name, me->level, g->hp,
                                      g->n, (int)json_int(e, "defense", 0));
            double gain = value - blow;
            double margin = boss ? BOSS_TECH_MARGIN : TECH_MARGIN;
            double per_tp = boss ? BOSS_TECH_GAIN_PER_TP : TECH_GAIN_PER_TP;
            if (value < margin * blow || gain < per_tp * t->tp) {
                continue;
            }
            if (!any || gain > best) {
                any = true;
                best = gain;
                out->slot = slot;
                out->group = (ng > 1) ? g->g : 0;
                snprintf(out->name, sizeof out->name, "%s", t->name);
            }
        }
    }
    return any;
}

static void tech_order(Session *s, const char *who, const char *tech,
                       int formation)
{
    if (s->tech_orders_n == s->tech_orders_cap) {
        s->tech_orders_cap = s->tech_orders_cap ? 2 * s->tech_orders_cap : 16;
        s->tech_orders = pl_grow(s->tech_orders, (size_t)s->tech_orders_cap
                                                 * sizeof *s->tech_orders);
    }
    TechOrder *t = &s->tech_orders[s->tech_orders_n++];
    snprintf(t->who, sizeof t->who, "%s", who);
    snprintf(t->tech, sizeof t->tech, "%s", tech);
    t->formation = formation;
}

static bool type_tech_member(const char *name)
{
    return streq(name, "Kain") || streq(name, "Hugh");      /* TYPE_TECHS */
}

/* ---- fight_smart ---- */

void fight_smart(Session *s, const FightOpts *o, Ints *shown)
{
    char wbuf[64];
    int before = s->shown.n;
    int orders_before = s->tech_orders_n;
    bool entry = false;
    u32 entry_tick = 0;
    int entry_formation = 0;
    bool ordered = false;
    static KnownList known;
    memset(&known, 0, sizeof known);
    if (o->use_techs) {
        techs(s, &known);
    }
    for (int round = 0; round < o->limit; round++) {
        const State st = s->state;
        if (st_screen(&st, "battle") && st.formation != PL_NONE
            && st.formation != 0 && !entry) {
            entry = true;
            entry_tick = st.tick;
            entry_formation = st.formation;
        }
        if (!st_screen(&st, "battle")) {
            if (st_control(&st, "level")) {
                break;
            }
            if (o->stop_on_level && entry && st_screen(&st, "level")) {
                break;
            }
            if (o->stop_on_gameover && entry && st_screen(&st, "gameover")) {
                break;
            }
            if (st_waiting(&st)) {
                send1(s, "press c");
            } else {
                send1(s, "wait idle");
            }
            continue;
        }
        if (!st_waiting(&st)) {
            ordered = false;
            send1(s, fight_wait(s, wbuf, sizeof wbuf));
            if (!st_waiting(&s->state) && st_screen(&s->state, "battle")
                && may_hurry(s)) {
                send1(s, "press c");            /* interrupt the repeat */
            }
            continue;
        }
        if (st_wait(&st, "menu") && st.menu == WIN_BATTLE_OPTS) {
            int alive = 0, worst_i = 0;
            double worst = 1;
            bool first = true;
            for (int i = 0; i < st.party_n; i++) {
                if (st.party[i].dead) {
                    continue;
                }
                alive++;
                double f = (double)st.party[i].hp / st.party[i].max_hp;
                if (first || f < worst) {
                    worst = f;
                    worst_i = i;
                    first = false;
                }
            }
            int left = 0;
            for (int i = 0; i < st.enemy_n; i++) {
                left += st.enemy[i].hp;
            }
            if (o->use_techs && !ordered) {
                ordered = true;
                techs_or(s, &known);
                for (int i = 0; i < known.n; i++) {
                    if (i < st.party_n && !st.party[i].dead
                        && streq(known.m[i].cmd, "defend")) {
                        battle_order(s, i, CMD_ATTACK, -1, PL_NONE, 0);
                    }
                }
                Heal heal = { 0, 0, 0 };
                bool has_heal = plan_heal(&st, &known, o->heal_below, &heal);
                bool vs_boss = is_boss_fight(&st);
                bool boss = o->boss_techs && vs_boss;
                int sh = -1, sh_slot = 0;
                if (vs_boss && o->deban && !plan_deban(&st, &known, &sh,
                                                       &sh_slot)) {
                    sh = -1;
                }
                if (sh >= 0 && has_heal && sh == heal.h) {
                    sh = -1;
                }
                if (sh >= 0 && battle_order(s, sh, CMD_TECH, sh_slot,
                                            PL_NONE, 0)) {
                    tech_order(s, known.m[sh].name, "DEBAN", st.formation);
                    techs_or(s, &known);
                }
                bool busy[PL_PARTY_MAX] = { false };
                if (has_heal) {
                    busy[heal.h] = true;
                }
                if (sh >= 0) {
                    busy[sh] = true;
                }
                if (vs_boss && o->shift) {
                    /* plan_shift: computed once, from known as it stands */
                    int picks[PL_PARTY_MAX * 24][2], np = 0;
                    for (int h = 0; h < st.party_n; h++) {
                        const Member *m = &st.party[h];
                        if (m->dead || m->sealed || busy[h] || h >= known.n) {
                            continue;
                        }
                        int done = 0;
                        for (int k = orders_before; k < s->tech_orders_n; k++) {
                            done += streq(s->tech_orders[k].tech, "SHIFT")
                                 && streq(s->tech_orders[k].who,
                                          known.m[h].name);
                        }
                        if (done >= SHIFT_CASTS) {
                            continue;
                        }
                        for (int slot = 0; slot < known.m[h].n; slot++) {
                            const BTech *t = &known.m[h].techs[slot];
                            if (streq(t->name, "SHIFT") && m->tp >= t->tp) {
                                picks[np][0] = h;
                                picks[np][1] = slot;
                                np++;
                            }
                        }
                    }
                    for (int k = 0; k < np; k++) {
                        int h = picks[k][0];
                        if (battle_order(s, h, CMD_TECH, picks[k][1], PL_NONE,
                                         0)) {
                            if (h >= known.n) {
                                pl_error("IndexError: known (shift)");
                            }
                            tech_order(s, known.m[h].name, "SHIFT",
                                       st.formation);
                            techs_or(s, &known);
                            busy[h] = true;
                        }
                    }
                }
                {
                    static KnownList it;
                    it = known;                 /* enumerate(known) */
                    for (int i = 0; i < it.n; i++) {
                        const Known *m = &it.m[i];
                        if (i >= st.party_n || st.party[i].dead
                            || st.party[i].sealed || busy[i]) {
                            continue;
                        }
                        if (!boss && !type_tech_member(m->name)) {
                            continue;
                        }
                        TypePick pick;
                        if (plan_type_tech(s, i, m, &st, boss, &pick)
                            && battle_order(s, i, CMD_TECH, pick.slot,
                                            PL_NONE, pick.group)) {
                            tech_order(s, m->name, pick.name, st.formation);
                            techs_or(s, &known);
                            busy[i] = true;
                        }
                    }
                }
                if (has_heal) {
                    busy[heal.h] = true;
                }
                {
                    static KnownList it;
                    it = known;
                    for (int i = 0; i < it.n; i++) {
                        if (o->reset_techs && i < st.party_n
                            && !st.party[i].dead
                            && streq(it.m[i].cmd, "tech") && !busy[i]) {
                            battle_order(s, i, CMD_ATTACK, -1, PL_NONE, 0);
                            techs_or(s, &known);
                        }
                    }
                }
                if (has_heal) {
                    battle_order(s, heal.h, CMD_TECH, heal.slot, heal.target,
                                 0);
                    techs_or(s, &known);
                    continue;
                }
                ordered = false;
            }
            if (worst < o->mate_below && !ordered) {
                int mh, ms;
                if (battle_find_mate(s, &mh, &ms)) {
                    ordered = true;
                    if (!battle_order_item(s, mh, ms, worst_i)) {
                        menu_escape_battle(s);
                    }
                    continue;
                }
            }
            if (worst < o->run_below
                && (o->run_below > 1.0
                    || left > 12 * ((alive > 1) ? alive : 1))) {
                menu_pick(s, 1);                /* STGY */
            } else {
                menu_pick(s, 0);                /* FIGHT */
            }
            continue;
        }
        if (st_wait(&st, "menu") && st.menu == WIN_BATTLE_OPTS2) {
            menu_pick(s, 1);                    /* RUN */
            continue;
        }
        answer_page(s);
    }
    int won = -1;
    for (int i = before; i < s->shown.n; i++) {
        if (s->shown.v[i] == SCRIPT_VICTORY) {
            won = i;
            break;
        }
    }
    if (entry) {
        if (s->fights_n == s->fights_cap) {
            s->fights_cap = s->fights_cap ? 2 * s->fights_cap : 64;
            s->fights = pl_grow(s->fights, (size_t)s->fights_cap
                                           * sizeof *s->fights);
        }
        FightRec *r = &s->fights[s->fights_n++];
        memset(r, 0, sizeof *r);
        r->tick = entry_tick;
        r->formation = entry_formation;
        r->ticks = (int)(s->state.tick - entry_tick);
        r->victory_ticks = (won >= 0)
                         ? (int)(s->state.tick - (u32)s->shown_ticks.v[won])
                         : PL_NONE;
        r->won = won >= 0;
        int dead = 0;
        for (int i = 0; i < s->state.party_n; i++) {
            dead += s->state.party[i].dead;
        }
        r->dead_after = dead;
        int from = (s->shown.n - 8 > before) ? s->shown.n - 8 : before;
        for (int i = from; i < s->shown.n; i++) {
            r->tail[r->tail_n++] = s->shown.v[i];
        }
    }
    if (shown != NULL) {
        for (int i = before; i < s->shown.n; i++) {
            ints_add(shown, s->shown.v[i]);
        }
    }
}
