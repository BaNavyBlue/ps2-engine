/* (build 171) play.py's gear and the party's upkeep: items.json and the
 * stores, the bags and hands as the probe prints them, plan_gear and
 * gear_up (buy, put on, sell), the mates and the map techniques (Res,
 * Rever, Anti), revive, and the retreat to town. */

#include "pl.h"

#include "../core/json.h"

#include <stdlib.h>

/* ---- the tables ---- */

static Item *g_items;
static int   g_item_n;

static JsonValue *load_json(const char *path)
{
    char *text = pl_read_file(path, NULL);
    JsonValue *root = (text != NULL) ? json_parse(text, NULL) : NULL;
    free(text);
    if (root == NULL) {
        pl_error("cannot read %s", path);
    }
    return root;
}

static void items_load(void)
{
    if (g_items != NULL) {
        return;
    }
    JsonValue *root = load_json("assets/data/items.json");
    const JsonValue *arr = (root->type == JSON_OBJECT)
                         ? json_get(root, "items") : root;
    int n = json_length(arr);
    g_items = pl_alloc((size_t)(n + 1) * sizeof *g_items);
    for (int i = 0; i < n; i++) {
        const JsonValue *it = json_at(arr, i);
        Item v;
        memset(&v, 0, sizeof v);
        snprintf(v.key, sizeof v.key, "%s", json_str(it, "key", ""));
        v.price = (int)json_int(it, "price", 0);
        v.action_flags = (int)json_int(it, "action_flags", 0);
        v.equip_mask = (int)json_int(it, "equip_mask", 0);
        v.attack = (int)json_int(it, "attack", 0);
        v.defense = (int)json_int(it, "defense", 0);
        /* {key: item}: a later one of a key takes the earlier's place */
        int at = -1;
        for (int k = 0; k < g_item_n; k++) {
            if (streq(g_items[k].key, v.key)) {
                at = k;
            }
        }
        if (at >= 0) {
            g_items[at] = v;
        } else {
            g_items[g_item_n++] = v;
        }
    }
    json_free(root);
}

const Item *item_get(const char *key)
{
    items_load();
    for (int i = 0; i < g_item_n; i++) {
        if (streq(g_items[i].key, key)) {
            return &g_items[i];
        }
    }
    return NULL;
}

static const Item *item_must(const char *key)
{
    const Item *it = item_get(key);
    if (it == NULL) {
        pl_error("KeyError: '%s'", key);
    }
    return it;
}

typedef struct {
    int  index;
    char items[16][24];
    int  n;
} Store;

static Store *g_stores;
static int    g_store_n = -1;

static void stores_load(void)
{
    if (g_store_n >= 0) {
        return;
    }
    JsonValue *root = load_json("assets/data/stores.json");
    const JsonValue *arr = json_get(root, "stores");
    g_store_n = json_length(arr);
    g_stores = pl_alloc((size_t)(g_store_n + 1) * sizeof *g_stores);
    for (int i = 0; i < g_store_n; i++) {
        const JsonValue *st = json_at(arr, i);
        g_stores[i].index = (int)json_int(st, "index", -1);
        const JsonValue *names = json_get(st, "items");
        for (int k = 0; k < json_length(names) && k < 16; k++) {
            const JsonValue *v = json_at(names, k);
            snprintf(g_stores[i].items[k], 24, "%s",
                     (v->type == JSON_STRING) ? v->string : "");
            g_stores[i].n++;
        }
    }
    json_free(root);
}

/* store_stock(index)[:count]: the shelf, in cursor order. */
int store_stock(int index, int count, char (*out)[24])
{
    stores_load();
    const Store *hit = NULL;
    for (int i = 0; i < g_store_n; i++) {
        if (g_stores[i].index == index) {
            hit = &g_stores[i];
        }
    }
    if (hit == NULL) {
        return 0;
    }
    int n = (count < hit->n) ? count : hit->n;
    if (n < 0) {
        n = 0;
    }
    memcpy(out, hit->items, (size_t)n * 24);
    return n;
}

/* sold_anywhere: some store's shelf has it. */
bool sold_anywhere(const char *name)
{
    stores_load();
    for (int i = 0; i < g_store_n; i++) {
        /* {index: items}: a later store of an index hides the earlier */
        bool hidden = false;
        for (int k = i + 1; k < g_store_n; k++) {
            if (g_stores[k].index == g_stores[i].index) {
                hidden = true;
            }
        }
        if (hidden) {
            continue;
        }
        for (int k = 0; k < g_stores[i].n; k++) {
            if (streq(g_stores[i].items[k], name)) {
                return true;
            }
        }
    }
    return false;
}

int char_id(const char *name)
{
    static const char *const NAMES[8] = {
        "Rolf", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"
    };
    for (int i = 0; i < 8; i++) {
        if (streq(NAMES[i], name)) {
            return i;
        }
    }
    return -1;
}

int kind_of(const char *name)
{
    const Item *it = item_get(name);
    return (it != NULL) ? (it->action_flags & 7) : 0;
}

bool wearable(const char *name, int cid)
{
    const Item *it = item_get(name);
    return it != NULL && (it->equip_mask & (1 << cid)) != 0;
}

static int hands_score(char (*names)[24], int n)
{
    int a = 0, d = 0;
    for (int i = 0; i < n; i++) {
        a += item_must(names[i])->attack;
    }
    for (int i = 0; i < n; i++) {
        d += item_must(names[i])->defense;
    }
    return 2 * a + d;
}

/* hands_score of one name. (build 180b) Without a copy into a [24]: GCC
 * 16's -Wformat-truncation saw that copy cut a longer name. */
static int hands_score1(const char *name)
{
    const Item *it = item_must(name);
    return 2 * it->attack + it->defense;
}

static int footwear_agility(const char *name)
{
    static JsonValue *agility;
    if (agility == NULL) {
        agility = load_json("assets/data/agility_bonus.json");
    }
    return (int)json_int(agility, name, 0);
}

static double piece_score(const char *name)
{
    const Item *it = item_must(name);
    return (double)(2 * it->attack + it->defense)
         + 0.5 * footwear_agility(name);           /* AGILITY_WEIGHT */
}

/* ---- the bags and hands ---- */

/* bags(s): `bags`, each member's (name, worn) in bag order. */
void bags(Session *s, Bags *out)
{
    memset(out, 0, sizeof *out);
    const Lines *lines = send1(s, "bags");
    for (int i = 0; i < lines->n; i++) {
        const char *ln = lines->v[i];
        const char *p = NULL;
        for (const char *q = strstr(ln, "ok"); q != NULL;
             q = strstr(q + 1, "ok")) {
            const char *r = q + 2;
            if (*r != ' ' && *r != '\t') {
                continue;
            }
            while (*r == ' ' || *r == '\t') {
                r++;
            }
            if (starts(r, "bags ")) {
                p = r + 5;
                break;
            }
        }
        if (p == NULL) {
            continue;
        }
        /* split("; "), then each part's split(":", 1)[1].split() */
        while (out->n < PL_PARTY_MAX) {
            const char *end = strstr(p, "; ");
            size_t len = (end != NULL) ? (size_t)(end - p) : strlen(p);
            const char *colon = memchr(p, ':', len);
            if (colon == NULL) {
                pl_error("IndexError: list index out of range (bags)");
            }
            Bag *b = &out->m[out->n++];
            const char *w = colon + 1, *stop = p + len;
            while (w < stop) {
                while (w < stop && *w == ' ') {
                    w++;
                }
                const char *e = w;
                while (e < stop && *e != ' ') {
                    e++;
                }
                if (e > w && b->n < BAG_MAX) {
                    size_t k = (size_t)(e - w);
                    bool worn = w[k - 1] == '*';
                    size_t nk = k;
                    while (nk > 0 && w[nk - 1] == '*') {
                        nk--;
                    }
                    if (nk >= sizeof b->v[0].name) {
                        nk = sizeof b->v[0].name - 1;
                    }
                    memcpy(b->v[b->n].name, w, nk);
                    b->v[b->n].name[nk] = '\0';
                    b->v[b->n].worn = worn;
                    b->n++;
                }
                w = e;
            }
            if (end == NULL) {
                break;
            }
            p = end + 2;
        }
        return;
    }
}

/* hands(s): each member's [right, left], "" for an empty hand. */
static int hands(Session *s, char (*out)[2][24])
{
    const Lines *lines = send1(s, "hands");
    for (int i = 0; i < lines->n; i++) {
        const char *ln = lines->v[i];
        const char *p = NULL;
        for (const char *q = strstr(ln, "ok"); q != NULL;
             q = strstr(q + 1, "ok")) {
            const char *r = q + 2;
            if (*r != ' ' && *r != '\t') {
                continue;
            }
            while (*r == ' ' || *r == '\t') {
                r++;
            }
            if (starts(r, "hands ")) {
                p = r + 6;
                break;
            }
        }
        if (p == NULL) {
            continue;
        }
        int n = 0;
        while (n < PL_PARTY_MAX) {
            const char *end = strstr(p, "; ");
            size_t len = (end != NULL) ? (size_t)(end - p) : strlen(p);
            const char *colon = memchr(p, ':', len);
            if (colon == NULL) {
                pl_error("IndexError: list index out of range (hands)");
            }
            char part[256];
            size_t k = (size_t)(p + len - colon - 1);
            if (k >= sizeof part) {
                k = sizeof part - 1;
            }
            memcpy(part, colon + 1, k);
            part[k] = '\0';
            char a[24] = "", b[24] = "";
            int got = sscanf(part, "%23s %23s", a, b);
            out[n][0][0] = out[n][1][0] = '\0';
            if (got >= 1 && !streq(a, "-")) {
                snprintf(out[n][0], 24, "%s", a);
            }
            if (got >= 2 && !streq(b, "-")) {
                snprintf(out[n][1], 24, "%s", b);
            }
            n++;
            if (end == NULL) {
                break;
            }
            p = end + 2;
        }
        return n;
    }
    return 0;
}

/* (build 177) one bag as Python prints it: [('Knife', True), ...] */
const char *bag_repr(const Bag *bag, char *out, size_t n)
{
    size_t used = (size_t)snprintf(out, n, "[");
    for (int i = 0; i < bag->n && used < n; i++) {
        used += (size_t)snprintf(out + used, n - used, "%s('%s', %s)",
                                 i ? ", " : "", bag->v[i].name,
                                 bag->v[i].worn ? "True" : "False");
    }
    if (used < n) {
        snprintf(out + used, n - used, "]");
    }
    return out;
}

const char *bags_repr(Session *s, char *out, size_t n)
{
    Bags b;
    bags(s, &b);
    size_t used = (size_t)snprintf(out, n, "[");
    for (int m = 0; m < b.n && used < n; m++) {
        used += (size_t)snprintf(out + used, n - used, "%s", m ? ", " : "");
        if (used < n) {
            bag_repr(&b.m[m], out + used, n - used);
            used += strlen(out + used);
        }
    }
    if (used < n) {
        snprintf(out + used, n - used, "]");
    }
    return out;
}

static bool is_mate(const char *name)
{
    return streq(name, "Monomate") || streq(name, "Dimate")
        || streq(name, "Trimate");
}

static bool is_stowable(const char *name)
{
    return is_mate(name) || streq(name, "Antidote") || streq(name, "StarMist")
        || streq(name, "Telepipe") || streq(name, "Escapipe")
        || streq(name, "Hidapipe");
}

int count_mates(Session *s)
{
    Bags b;
    bags(s, &b);
    int n = 0;
    for (int m = 0; m < b.n; m++) {
        for (int i = 0; i < b.m[m].n; i++) {
            n += is_mate(b.m[m].v[i].name);
        }
    }
    return n;
}

int carried(Session *s, const char *item)
{
    Bags b;
    bags(s, &b);
    int n = 0;
    for (int m = 0; m < b.n; m++) {
        for (int i = 0; i < b.m[m].n; i++) {
            n += streq(b.m[m].v[i].name, item);
        }
    }
    return n;
}

/* find_item: (holder, slot) of the first `item` a living member carries
 * unworn (either may be NULL). */
bool find_item(Session *s, const char *item, int *holder, int *slot)
{
    const State st = s->state;          /* the party before the bags */
    Bags b;
    bags(s, &b);
    for (int m = 0; m < b.n; m++) {
        if (m < st.party_n && st.party[m].dead) {
            continue;
        }
        for (int i = 0; i < b.m[m].n; i++) {
            if (streq(b.m[m].v[i].name, item) && !b.m[m].v[i].worn) {
                if (holder != NULL) {
                    *holder = m;
                }
                if (slot != NULL) {
                    *slot = i;
                }
                return true;
            }
        }
    }
    return false;
}

/* ---- map techniques ---- */

static JsonValue *g_chars, *g_techs;

/* map_techs(name, level): (stop, key, tp) the member knows, in list
 * order. */
static int map_techs(const char *name, int level, Tech *out, int cap)
{
    if (g_chars == NULL) {
        g_chars = load_json("assets/data/characters.json");
        g_techs = load_json("assets/data/techniques.json");
    }
    for (int i = 0; i < json_length(g_chars); i++) {
        const JsonValue *c = json_at(g_chars, i);
        const char *cn = json_str(c, "name", "");
        if (!same_upper(cn, name)) {
            continue;
        }
        int known = 0;
        const JsonValue *levels = json_get(c, "levels");
        for (int k = 0; k < json_length(levels); k++) {
            const JsonValue *r = json_at(levels, k);
            if (json_int(r, "level", 0) <= level) {
                known += (int)json_int(r, "techs_b", 0);
            }
        }
        char cap_name[24];
        snprintf(cap_name, sizeof cap_name, "%s", cn);
        for (char *p = cap_name; *p; p++) {
            if (p == cap_name) {
                if (*p >= 'a' && *p <= 'z') {
                    *p = (char)(*p - 'a' + 'A');
                }
            } else if (*p >= 'A' && *p <= 'Z') {
                *p = (char)(*p - 'A' + 'a');
            }
        }
        const JsonValue *avail = json_get(json_get(json_get(g_techs,
                                                            "available"),
                                                   cap_name), "map");
        if (avail == NULL) {
            pl_error("KeyError: '%s'", cap_name);
        }
        const JsonValue *all = json_get(g_techs, "techniques");
        int n = 0;
        int count = json_length(avail);
        /* [:known] with a negative known counts from the end */
        int upto = (known >= 0) ? ((known < count) ? known : count)
                                : ((count + known > 0) ? count + known : 0);
        for (int k = 0; k < upto && n < cap; k++) {
            const JsonValue *v = json_at(avail, k);
            const char *key = (v->type == JSON_STRING) ? v->string : "";
            int tp = 0;
            /* {key: tp}: the last of a key */
            for (int t = 0; t < json_length(all); t++) {
                const JsonValue *tt = json_at(all, t);
                if (streq(json_str(tt, "key", ""), key)) {
                    tp = (int)json_int(tt, "tp", 0);
                }
            }
            out[n].stop = k;
            snprintf(out[n].key, sizeof out[n].key, "%s", key);
            out[n].tp = tp;
            n++;
        }
        return n;
    }
    return 0;
}

/* find_tech: (party position, stop, tp) of the first living member who
 * knows `key` and has the TP. */
bool find_tech(Session *s, const char *key, int *who, int *stop)
{
    for (int i = 0; i < s->state.party_n; i++) {
        const Member *m = &s->state.party[i];
        if (m->dead) {
            continue;
        }
        Tech t[32];
        int n = map_techs(m->name, m->level, t, 32);
        for (int k = 0; k < n; k++) {
            if (streq(t[k].key, key) && m->tp >= t[k].tp) {
                *who = i;
                *stop = t[k].stop;
                return true;
            }
        }
    }
    return false;
}

/* ---- mending ---- */

bool needs_healing(Session *s, double frac)
{
    for (int i = 0; i < s->state.party_n; i++) {
        const Member *m = &s->state.party[i];
        bool skip = false;
        for (int k = 0; k < s->expendable_n; k++) {
            if (streq(s->expendable[k], m->name)) {
                skip = true;
            }
        }
        if (skip) {
            continue;
        }
        if (m->dead || m->poison || m->hp < m->max_hp * frac) {
            return true;
        }
    }
    return false;
}

int cast_res(Session *s, double below)
{
    int used = 0;
    for (int k = 0; k < 20; k++) {
        if (!ready(s)) {
            break;
        }
        const State *st = &s->state;
        int hurt[PL_PARTY_MAX], n = 0;
        for (int i = 0; i < st->party_n; i++) {
            if (!st->party[i].dead
                && st->party[i].hp < st->party[i].max_hp * below) {
                hurt[n++] = i;
            }
        }
        if (n == 0) {
            break;
        }
        /* sorted(key=hp / max_hp): stable */
        for (int i = 1; i < n; i++) {
            int t = hurt[i], j = i - 1;
            double kt = (double)st->party[t].hp / st->party[t].max_hp;
            while (j >= 0 && (double)st->party[hurt[j]].hp
                                 / st->party[hurt[j]].max_hp > kt) {
                hurt[j + 1] = hurt[j];
                j--;
            }
            hurt[j + 1] = t;
        }
        int who, stop;
        bool have = false;
        if (st->party[hurt[0]].max_hp - st->party[hurt[0]].hp >= 60) {
            have = find_tech(s, "Gires", &who, &stop);
        }
        if (!have) {
            have = find_tech(s, "Res", &who, &stop);
        }
        if (!have) {
            break;
        }
        use_tech(s, who, stop, hurt[0], NULL);
        used++;
    }
    return used;
}

void mend(Session *s, double below)
{
    cast_res(s, below);
    for (int k = 0; k < 12; k++) {
        if (!ready(s)) {
            break;
        }
        const State *st = &s->state;
        int hurt = -1;
        for (int i = 0; i < st->party_n; i++) {
            if (!st->party[i].dead && st->party[i].hp < st->party[i].max_hp
                                                        * below) {
                hurt = i;
                break;
            }
        }
        if (hurt < 0) {
            break;
        }
        const State pre = s->state;
        Bags b;
        bags(s, &b);
        int fh = -1, fs = -1;
        for (int m = 0; m < b.n && fh < 0; m++) {
            if (m < pre.party_n && pre.party[m].dead) {
                continue;
            }
            for (int i = 0; i < b.m[m].n; i++) {
                if (is_mate(b.m[m].v[i].name) && !b.m[m].v[i].worn) {
                    fh = m;
                    fs = i;
                    break;
                }
            }
        }
        if (fh < 0) {
            break;
        }
        use_item(s, fh, fs, hurt, NULL);
    }
}

/* revive: Rever, else a Moon Dew, on each dead member in turn; how many
 * came back. The last s.dew_reserve Moon Dews are left alone unless
 * `spend_reserve` (build 177: the Nei Sword's holder on Noah). */
int revive(Session *s)
{
    return revive_x(s, false);
}

int revive_x(Session *s, bool spend_reserve)
{
    int back = 0;
    bool tech_failed = false;
    for (int k = 0; k < 4; k++) {
        if (!ready(s)) {
            break;
        }
        int dead = -1;
        for (int i = 0; i < s->state.party_n; i++) {
            if (s->state.party[i].dead) {
                dead = i;
                break;
            }
        }
        if (dead < 0) {
            break;
        }
        int who, stop;
        bool tech = !tech_failed && find_tech(s, "Rever", &who, &stop);
        if (tech) {
            use_tech(s, who, stop, dead, NULL);
        } else {
            int h, sl;
            bool dew = find_item(s, "MoonDew", &h, &sl);
            int keep = spend_reserve ? 0 : s->dew_reserve;
            if (!dew || carried(s, "MoonDew") <= keep) {
                break;
            }
            use_item(s, h, sl, dead, NULL);
        }
        send1(s, "wait idle");
        ready(s);
        if (dead >= s->state.party_n || s->state.party[dead].dead) {
            if (tech && !tech_failed) {
                tech_failed = true;
                continue;
            }
            break;
        }
        back++;
    }
    return back;
}

void cure_poison(Session *s)
{
    for (int k = 0; k < 4; k++) {
        int sick = -1;
        for (int i = 0; i < s->state.party_n; i++) {
            if (s->state.party[i].poison && !s->state.party[i].dead) {
                sick = i;
                break;
            }
        }
        if (sick < 0 || !ready(s)) {
            break;
        }
        int who, stop;
        if (find_tech(s, "Anti", &who, &stop)) {
            use_tech(s, who, stop, sick, NULL);
        } else {
            int h, sl;
            if (!find_item(s, "Antidote", &h, &sl)) {
                break;
            }
            use_item(s, h, sl, sick, NULL);
        }
        send1(s, "wait idle");
        ready(s);
        bool still;
        if (s->state.party_n == 0) {
            still = false;
        } else if (sick >= s->state.party_n) {
            pl_error("IndexError: list index out of range (cure_poison)");
        } else {
            still = s->state.party[sick].poison;
        }
        if (still) {
            break;
        }
    }
}

/* ---- the item store's mates ---- */

void stock_mates(Session *s, int level, Report *log)
{
    stock_mates_x(s, level, 4, false, log);
}

/* (build 174) With `trimates`, Trimates where the store has them (Kueri,
 * Piata, Ryuon) and the money is there. */
int stock_mates_x(Session *s, int level, int want, bool trimates,
                  Report *log)
{
    (void)log;
    rolf_alive_for_shops(s, level);
    TownBuilding b;
    if (!town_building(level, BUILDING_ITEM, &b)) {
        return 0;
    }
    char shelf[16][24];
    int ns = store_stock(b.f766, b.f764, shelf);
    int party = s->state.party_n;
    int have = count_mates(s);
    Bags bg;
    bags(s, &bg);
    int room[PL_PARTY_MAX], nr = bg.n;
    for (int i = 0; i < nr; i++) {
        room[i] = (8 - bg.m[i].n > 0) ? 8 - bg.m[i].n : 0;
    }
    int money = (s->state.meseta == PL_NONE) ? 0 : s->state.meseta;
    Purchase todo[64];
    int n = 0, m = 0;
    for (;;) {
        bool any = false;
        for (int i = 0; i < nr; i++) {
            any = any || room[i] != 0;
        }
        if (!(have + n < want && any && money >= 40)) {
            break;
        }
        while (room[m % party] == 0) {
            m++;
        }
        room[m % party]--;
        int ti = -1, di = -1, mi = -1;
        for (int i = 0; i < ns; i++) {
            if (ti < 0 && streq(shelf[i], "Trimate")) {
                ti = i;
            }
            if (di < 0 && streq(shelf[i], "Dimate")) {
                di = i;
            }
            if (mi < 0 && streq(shelf[i], "Monomate")) {
                mi = i;
            }
        }
        const Item *tri = item_get("Trimate");
        if (trimates && ti >= 0 && money >= 10 * tri->price) {
            todo[n].stock = ti;
            todo[n].who = m % party;
            n++;
            money -= tri->price;
        } else if (di >= 0 && money >= 400) {
            todo[n].stock = di;
            todo[n].who = m % party;
            n++;
            money -= 60;
        } else if (mi >= 0) {
            todo[n].stock = mi;
            todo[n].who = m % party;
            n++;
            money -= 20;
        } else {
            break;
        }
        m++;
    }
    if (n > 0) {
        buy(s, level, b.y, b.x, todo, n);
    }
    return n;
}

int stock_item(Session *s, int level, const char *item, int want,
               Report *log)
{
    rolf_alive_for_shops(s, level);
    TownBuilding b;
    if (!town_building(level, BUILDING_ITEM, &b)) {
        return 0;
    }
    char shelf[16][24];
    int ns = store_stock(b.f766, b.f764, shelf);
    int at = -1;
    for (int i = 0; i < ns && at < 0; i++) {
        if (streq(shelf[i], item)) {
            at = i;
        }
    }
    if (at < 0) {
        return 0;
    }
    int party = s->state.party_n;
    int have = carried(s, item);
    Bags bg;
    bags(s, &bg);
    int room[PL_PARTY_MAX], nr = bg.n;
    for (int i = 0; i < nr; i++) {
        room[i] = (16 - bg.m[i].n > 0) ? 16 - bg.m[i].n : 0;
    }
    int money = (s->state.meseta == PL_NONE) ? 0 : s->state.meseta;
    int price = item_get(item)->price;
    Purchase todo[64];
    int n = 0, m = 0;
    for (;;) {
        bool any = false;
        for (int i = 0; i < nr; i++) {
            any = any || room[i] != 0;
        }
        if (!(have + n < want && any && money >= price) || n >= 64) {
            break;
        }
        while (room[m % party] == 0) {
            m++;
        }
        room[m % party]--;
        todo[n].stock = at;
        todo[n].who = m % party;
        n++;
        money -= price;
        m++;
    }
    if (n > 0) {
        buy(s, level, b.y, b.x, todo, n);
        say(log, "stock: %d %s", n, item);
    }
    return n;
}

/* resupply: an Escapipe and a Telepipe, then mates to `want`. */
void resupply(Session *s, int level, int dy, int dx)
{
    const int want = 8;
    int party = s->state.party_n;
    int have = count_mates(s);
    Bags bg;
    bags(s, &bg);
    int room[PL_PARTY_MAX], nr = bg.n;
    for (int i = 0; i < nr; i++) {
        room[i] = (8 - bg.m[i].n > 0) ? 8 - bg.m[i].n : 0;
    }
    int money = (s->state.meseta == PL_NONE) ? 0 : s->state.meseta;
    Purchase todo[64];
    int n = 0, m = 0;
    static const struct { const char *name; int stock, price; } PIPES[2] = {
        { "Escapipe", 4, 70 }, { "Telepipe", 3, 130 }
    };
    for (int p = 0; p < 2; p++) {
        int h, sl;
        if (!find_item(s, PIPES[p].name, &h, &sl)
            && money >= PIPES[p].price + 100) {
            for (int who = 0; who < party; who++) {
                if (room[who] > 0) {
                    room[who]--;
                    todo[n].stock = PIPES[p].stock;
                    todo[n].who = who;
                    n++;
                    money -= PIPES[p].price;
                    break;
                }
            }
        }
    }
    for (;;) {
        bool any = false;
        for (int i = 0; i < nr; i++) {
            any = any || room[i] != 0;
        }
        if (!(have + n < want && any && money >= 20)) {
            break;
        }
        while (room[m % party] == 0) {
            m++;
        }
        room[m % party]--;
        if (money >= 60 + 60 * 2) {
            todo[n].stock = 1;                  /* Dimate */
            todo[n].who = m % party;
            n++;
            money -= 60;
        } else {
            todo[n].stock = 0;                  /* Monomate */
            todo[n].who = m % party;
            n++;
            money -= 20;
        }
        m++;
    }
    if (n > 0) {
        buy(s, level, dy, dx, todo, n);
    }
}

/* ---- the retreat ---- */

static bool is_dezolis(int level)
{
    return level == 0x01 || level == 0x02 || level == 0x03 || level == 0x0A
        || level == 0x0B || level == 0x0C;
}

static bool walk_out_level(int level)
{
    return level == 0x13 || (level >= 0x36 && level < 0x3E);
}

/* pipe_town: where a Telepipe or Ryuka lands from here. */
static int pipe_town(const Session *s)
{
    int level = s->state.level;
    bool here_motavia = level != PL_NONE && level < 0x10 && !is_dezolis(level);
    if (here_motavia && is_dezolis(s->save_town)) {
        return PASEO;
    }
    return s->save_town;
}

static bool retreat_body(Session *s, int explicit_town, int *town,
                         Report *log)
{
    char nb[32];
    if (!ready(s)) {
        return false;
    }
    int who, stop, h, sl;
    if (s->state.level >= 0x10) {
        bool tech = find_tech(s, "Hinas", &who, &stop);
        bool pipe = find_item(s, "Escapipe", &h, &sl);
        if (tech) {
            use_tech(s, who, stop, PL_NONE, NULL);
            ready(s);
        } else if (pipe) {
            use_item(s, h, sl, PL_NONE, NULL);
            ready(s);
        }
    }
    if (s->state.level != PL_NONE && walk_out_level(s->state.level)) {
        CSet goal = { 0 };
        cset_add(&goal, 159, 15, SIDE_NONE);
        if (!travel(s, MOTAVIA, &goal, log)) {
            say(log, "retreat: could not walk out of %s",
                level_name(s->state.level, nb, sizeof nb));
        }
        cset_free(&goal);
    }
    if (explicit_town == PL_NONE) {
        *town = pipe_town(s);
    }
    if (s->state.riding && s->state.level == MOTAVIA) {
        /* (build 173) Out of a dungeon onto the water: ashore first, on
         * the ground the town's exit lands on -- not within three cells
         * of the landing, where the scooter's footprint would stand. */
        CSet region = { 0 };
        int tx, ty;
        if (town_exit_to(*town, &tx, &ty)) {
            CSet landing = { 0 }, less = { 0 };
            cset_add(&landing, tx, ty, 0);
            walk_region(MOTAVIA, &landing, &region);
            for (int i = 0; i < region.n; i++) {
                Cell c = cset_cell(&region, i);
                if (!(abs(c.x - tx) <= 3 && abs(c.y - ty) <= 3)) {
                    cset_add(&less, c.x, c.y, 0);
                }
            }
            if (less.n > 0) {
                cset_free(&region);
                region = less;
            } else {
                cset_free(&less);
            }
            cset_free(&landing);
        }
        if (region.n == 0) {
            CSet none = { 0 };
            landing_region(*town, &none, &region);
        }
        say(log, "retreat: ashore toward %d -- region %d cells", *town,
            region.n);
        if (region.n > 0 && !ride_ashore(s, NULL, &region, log)) {
            say(log, "retreat: could not get ashore");
        }
        cset_free(&region);
    }
    if (!st_screen(&s->state, "level") && !st_screen(&s->state, "battle")) {
        say(log, "retreat: off the map -- %s", s->state.screen);
        return false;
    }
    int lv = (s->state.level == PL_NONE) ? 0 : s->state.level;
    if (lv <= 3 && s->state.level != *town) {
        bool tech = find_tech(s, "Ryuka", &who, &stop);
        bool pipe = find_item(s, "Telepipe", &h, &sl);
        if (tech) {
            use_tech(s, who, stop, PL_NONE, NULL);
            ready(s);
        } else if (pipe) {
            use_item(s, h, sl, PL_NONE, NULL);
            ready(s);
        }
    }
    if (!enter_town(s, *town)) {
        say(log, "retreat: could not reach town");
        return false;
    }
    TownBuilding b;
    if (party_dead(s) && town_building(*town, BUILDING_CLONE_LAB, &b)) {
        clone_lab(s, *town, b.y, b.x);
    }
    heal_here(s, *town);
    if (town_building(*town, BUILDING_ITEM, &b)) {
        resupply(s, *town, b.y, b.x);
    }
    return true;
}

bool retreat(Session *s, int town, Report *log)
{
    int explicit_town = town;
    if (town == PL_NONE) {
        town = s->save_town;
    }
    bool careful = s->careful;
    s->careful = false;
    s->hurt = false;
    bool ok = retreat_body(s, explicit_town, &town, log);
    s->careful = careful;
    if (!ok) {
        return false;
    }
    return !party_dead(s) && s->state.level == town;
}

/* ---- gear ---- */

typedef struct {
    char name[KIND_SLOTS][4][24];
    int  n[KIND_SLOTS];
} Loadout;

/* loadout(bag): what is worn, by kind. */
static void loadout(const Bag *bag, Loadout *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < bag->n; i++) {
        if (!bag->v[i].worn) {
            continue;
        }
        int k = kind_of(bag->v[i].name);
        if (out->n[k] < 4) {
            snprintf(out->name[k][out->n[k]++], 24, "%s", bag->v[i].name);
        }
    }
}

const char *worn_head(Session *s, const char *name)
{
    static char head[24];
    const State st = s->state;
    Bags b;
    bags(s, &b);
    for (int m = 0; m < b.n; m++) {
        if (m < st.party_n && streq(st.party[m].name, name)) {
            for (int i = 0; i < b.m[m].n; i++) {
                if (b.m[m].v[i].worn && kind_of(b.m[m].v[i].name) == KIND_HEAD) {
                    snprintf(head, sizeof head, "%s", b.m[m].v[i].name);
                    return head;
                }
            }
        }
    }
    return NULL;
}

static int price_factor(Session *s, int level)
{
    if (is_dezolis(level)) {
        const char *h = worn_head(s, "Rolf");
        if (h != NULL && streq(h, "MagicCap")) {
            return 2;
        }
    }
    return 1;
}

/* PIN_HEAD: members whose head plan_gear leaves alone. (build 175) A
 * stage sets it, pin_head_add("Rolf"), and it stays for the process. */
static char g_pin_head[PL_PARTY_MAX][16];
static int  g_pin_head_n;

void pin_head_add(const char *name)
{
    for (int i = 0; i < g_pin_head_n; i++) {
        if (streq(g_pin_head[i], name)) {
            return;
        }
    }
    if (g_pin_head_n < PL_PARTY_MAX) {
        snprintf(g_pin_head[g_pin_head_n++], 16, "%s", name);
    }
}

/* (build 179) PIN_HEAD as a new process has it: empty (story_reset). */
void pin_head_clear(void)
{
    g_pin_head_n = 0;
}

static bool pin_head(const char *name)
{
    for (int i = 0; i < g_pin_head_n; i++) {
        if (streq(g_pin_head[i], name)) {
            return true;
        }
    }
    return false;
}

typedef struct {
    double gain;
    int    cost;
    Src    src;
    int    m;
    char   name[24];
    char   old[4][24];
    int    old_n;
} Option;

/* sources(pred) for member m: unworn in the bag, then in the others'
 * (the bag not full), then the shelves. */
typedef struct {
    Src  src;
    char name[24];
    int  cost;
} Source;

typedef struct {
    const Bags *all;
    const State *st;
    int  m, cid, factor;
    int  nshelf;
    int  shelf_building[2];
    char shelf[2][16][24];
    int  shelf_n[2];
} SourceCtx;

static int sources(const SourceCtx *c, bool (*pred)(const char *, int),
                   int arg, Source *out, int cap)
{
    int n = 0;
    const Bag *bag = &c->all->m[c->m];
    for (int i = 0; i < bag->n && n < cap; i++) {
        const char *name = bag->v[i].name;
        if (!bag->v[i].worn && item_get(name) != NULL
            && wearable(name, c->cid) && pred(name, arg)) {
            out[n].src.kind = SRC_BAG;
            out[n].src.a = i;
            out[n].src.b = 0;
            snprintf(out[n].name, 24, "%s", name);
            out[n].cost = 0;
            n++;
        }
    }
    if (bag->n < 16) {
        for (int h = 0; h < c->all->n; h++) {
            if (h == c->m || h >= c->st->party_n || c->st->party[h].dead) {
                continue;
            }
            const Bag *o = &c->all->m[h];
            for (int i = 0; i < o->n && n < cap; i++) {
                const char *name = o->v[i].name;
                if (!o->v[i].worn && item_get(name) != NULL
                    && wearable(name, c->cid) && pred(name, arg)) {
                    out[n].src.kind = SRC_GIVE;
                    out[n].src.a = h;
                    out[n].src.b = i;
                    snprintf(out[n].name, 24, "%s", name);
                    out[n].cost = 0;
                    n++;
                }
            }
        }
    }
    for (int k = 0; k < c->nshelf; k++) {
        for (int i = 0; i < c->shelf_n[k] && n < cap; i++) {
            const char *name = c->shelf[k][i];
            if (item_get(name) != NULL && wearable(name, c->cid)
                && pred(name, arg)) {
                out[n].src.kind = SRC_SHOP;
                out[n].src.a = c->shelf_building[k];
                out[n].src.b = i;
                snprintf(out[n].name, 24, "%s", name);
                out[n].cost = item_must(name)->price * c->factor;
                n++;
            }
        }
    }
    return n;
}

static bool pred_kind(const char *name, int kind)
{
    return kind_of(name) == kind;
}

static bool pred_hands(const char *name, int unused)
{
    (void)unused;
    int k = kind_of(name);
    return k == KIND_HAND || k == KIND_BOTH;
}

static bool src_free(const Src *s)
{
    return s->kind == SRC_BAG || s->kind == SRC_GIVE;
}

typedef struct {
    char   name[24];
    Source v[48];
    int    n;
} Pool;

static int name_cmp24(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/* plan_gear: what to put on in this town, best gain first. (build 172)
 * With `unlimited` (geared), every upgrade the shelves hold, whatever the
 * money or the room, and no log line. */
static int plan_gear_x(Session *s, int level, int reserve, Report *log,
                       GearTodo *todo, int cap, bool unlimited)
{
    static State snap;
    snap = s->state;                    /* party: before the bags */
    const State *st = &snap;
    int party = st->party_n;
    int money = ((st->meseta == PL_NONE) ? 0 : st->meseta) - reserve;
    if (unlimited) {
        money = 1 << 30;
    }
    static SourceCtx c;
    memset(&c, 0, sizeof c);
    static const int SHOPS[2] = { BUILDING_WEAPON, BUILDING_ARMOR };
    for (int k = 0; k < 2; k++) {
        TownBuilding b;
        if (town_building(level, SHOPS[k], &b)) {
            c.shelf_building[c.nshelf] = SHOPS[k];
            c.shelf_n[c.nshelf] = store_stock(b.f766, b.f764,
                                              c.shelf[c.nshelf]);
            c.nshelf++;
        }
    }
    static Bags all;
    bags(s, &all);
    c.all = &all;
    c.st = st;
    c.factor = price_factor(s, level);

    static Option options[256];
    int no = 0;
    static Source src[256];
    for (int m = 0; m < party; m++) {
        const Member *member = &st->party[m];
        int cid = char_id(member->name);
        if (cid < 0 || member->dead || m >= all.n) {
            continue;
        }
        c.m = m;
        c.cid = cid;
        Loadout have;
        loadout(&all.m[m], &have);

        static const int KINDS[3] = { KIND_HEAD, KIND_BODY, KIND_LEGS };
        for (int ki = 0; ki < 3; ki++) {
            int kind = KINDS[ki];
            if (kind == KIND_HEAD && pin_head(member->name)) {
                continue;
            }
            double cur = 0;
            for (int i = 0; i < have.n[kind]; i++) {
                double v = piece_score(have.name[kind][i]);
                if (i == 0 || v > cur) {
                    cur = v;
                }
            }
            int ns = sources(&c, pred_kind, kind, src, 256);
            int best = -1;
            double bgain = 0;
            for (int i = 0; i < ns; i++) {
                double gain = piece_score(src[i].name) - cur;
                if (gain > 0 && (best < 0 || gain > bgain
                                 || (gain == bgain
                                     && src[i].cost < src[best].cost))) {
                    best = i;
                    bgain = gain;
                }
            }
            if (best >= 0 && no < 256) {
                Option *o = &options[no++];
                o->gain = bgain;
                o->cost = src[best].cost;
                o->src = src[best].src;
                o->m = m;
                snprintf(o->name, 24, "%s", src[best].name);
                o->old_n = have.n[kind];
                memcpy(o->old, have.name[kind], sizeof o->old);
            }
        }

        /* hands */
        char worn[8][24];
        int nw = 0;
        for (int i = 0; i < have.n[KIND_HAND] && nw < 8; i++) {
            memcpy(worn[nw++], have.name[KIND_HAND][i], 24);
        }
        for (int i = 0; i < have.n[KIND_BOTH] && nw < 8; i++) {
            memcpy(worn[nw++], have.name[KIND_BOTH][i], 24);
        }
        int cur = hands_score(worn, nw);
        static Pool pool[64];
        int np = 0;
        int ns = sources(&c, pred_hands, 0, src, 256);
        for (int i = 0; i < ns; i++) {
            int p = -1;
            for (int k = 0; k < np; k++) {
                if (streq(pool[k].name, src[i].name)) {
                    p = k;
                }
            }
            if (p < 0 && np < 64) {
                p = np++;
                snprintf(pool[p].name, 24, "%s", src[i].name);
                pool[p].n = 0;
            }
            if (p >= 0 && pool[p].n < 47) {
                pool[p].v[pool[p].n++] = src[i];
            }
        }
        for (int p = 0; p < np; p++) {
            /* sort by cost, stable */
            for (int i = 1; i < pool[p].n; i++) {
                Source t = pool[p].v[i];
                int j = i - 1;
                while (j >= 0 && pool[p].v[j].cost > t.cost) {
                    pool[p].v[j + 1] = pool[p].v[j];
                    j--;
                }
                pool[p].v[j + 1] = t;
            }
            for (int i = 0; i < pool[p].n; i++) {
                if (!src_free(&pool[p].v[i].src)) {
                    pool[p].v[pool[p].n] = pool[p].v[i];
                    pool[p].n++;                /* a shop sells two */
                    break;
                }
            }
        }
        /* ones: one-handers in the pool or worn, by (-score, name), six */
        char ones[72][24];
        int n1 = 0;
        for (int p = 0; p < np; p++) {
            if (kind_of(pool[p].name) == KIND_HAND) {
                memcpy(ones[n1++], pool[p].name, 24);
            }
        }
        for (int i = 0; i < nw; i++) {
            if (kind_of(worn[i]) != KIND_HAND) {
                continue;
            }
            bool dup = false;
            for (int k = 0; k < n1; k++) {
                dup = dup || streq(ones[k], worn[i]);
            }
            if (!dup && n1 < 72) {
                memcpy(ones[n1++], worn[i], 24);
            }
        }
        qsort(ones, (size_t)n1, 24, name_cmp24);
        for (int i = 1; i < n1; i++) {          /* stable: by score */
            char t[24];
            memcpy(t, ones[i], 24);
            int kt = hands_score1(t), j = i - 1;
            while (j >= 0 && hands_score1(ones[j]) < kt) {
                memcpy(ones[j + 1], ones[j], 24);
                j--;
            }
            memcpy(ones[j + 1], t, 24);
        }
        if (n1 > 6) {
            n1 = 6;
        }
        /* pairs: each two-hander, each one-hander, each pair of ones */
        char pairs[128][2][24];
        int pn[128], npairs = 0;
        for (int p = 0; p < np && npairs < 128; p++) {
            if (kind_of(pool[p].name) == KIND_BOTH) {
                memcpy(pairs[npairs][0], pool[p].name, 24);
                pn[npairs++] = 1;
            }
        }
        for (int i = 0; i < n1 && npairs < 128; i++) {
            memcpy(pairs[npairs][0], ones[i], 24);
            pn[npairs++] = 1;
        }
        for (int i = 0; i < n1; i++) {
            for (int k = i; k < n1 && npairs < 128; k++) {
                memcpy(pairs[npairs][0], ones[i], 24);
                memcpy(pairs[npairs][1], ones[k], 24);
                pn[npairs++] = 2;
            }
        }
        int best = -1, bcost = 0;
        double bgain = 0;
        Source bpicks[2];
        char bpn[2][24];
        int bnp = 0;
        for (int q = 0; q < npairs; q++) {
            double gain = hands_score(pairs[q], pn[q]) - cur;
            char need[2][24];
            int nn = pn[q];
            memcpy(need, pairs[q], sizeof need);
            for (int i = 0; i < nw; i++) {
                for (int k = 0; k < nn; k++) {
                    if (streq(need[k], worn[i])) {
                        for (int j = k; j + 1 < nn; j++) {
                            memcpy(need[j], need[j + 1], 24);
                        }
                        nn--;
                        break;
                    }
                }
            }
            if (gain <= 0 || nn == 0) {
                continue;
            }
            Source picks[2];
            char pnames[2][24];
            int cost = 0, taken_n[2] = { 0, 0 };
            bool ok = true;
            for (int k = 0; k < nn; k++) {
                int p = -1;
                for (int j = 0; j < np; j++) {
                    if (streq(pool[j].name, need[k])) {
                        p = j;
                    }
                }
                int already = 0;
                for (int j = 0; j < k; j++) {
                    already += streq(need[j], need[k]);
                }
                (void)taken_n;
                if (p < 0 || already >= pool[p].n) {
                    ok = false;
                    break;
                }
                picks[k] = pool[p].v[already];
                memcpy(pnames[k], need[k], 24);
                cost += pool[p].v[already].cost;
            }
            if (!ok) {
                continue;
            }
            if (best < 0 || gain / (cost > 1 ? cost : 1)
                                > bgain / (bcost > 1 ? bcost : 1)) {
                best = q;
                bgain = gain;
                bcost = cost;
                bnp = nn;
                memcpy(bpicks, picks, sizeof bpicks);
                memcpy(bpn, pnames, sizeof bpn);
            }
        }
        if (best >= 0) {
            char old[8][24];
            int no_old = 0;
            for (int i = 0; i < nw; i++) {
                bool in_pair = false;
                for (int k = 0; k < pn[best]; k++) {
                    in_pair = in_pair || streq(pairs[best][k], worn[i]);
                }
                if (!in_pair && no_old < 4) {
                    memcpy(old[no_old++], worn[i], 24);
                }
            }
            for (int k = 0; k < bnp && no < 256; k++) {
                Option *o = &options[no++];
                o->gain = bgain;
                o->cost = src_free(&bpicks[k].src)
                        ? 0 : item_must(bpn[k])->price * c.factor;
                o->src = bpicks[k].src;
                o->m = m;
                snprintf(o->name, 24, "%.23s", bpn[k]);
                o->old_n = no_old;
                memcpy(o->old, old, sizeof o->old);
                no_old = 0;                     /* sold once */
            }
        }
    }

    /* options.sort(key=(-1 if free else 0, -gain / max(1, cost))) */
    for (int i = 1; i < no; i++) {
        Option t = options[i];
        int j = i - 1;
        for (; j >= 0; j--) {
            const Option *a = &options[j];
            int ka = (a->cost == 0) ? -1 : 0, kt = (t.cost == 0) ? -1 : 0;
            double ga = -a->gain / (a->cost > 1 ? a->cost : 1);
            double gt = -t.gain / (t.cost > 1 ? t.cost : 1);
            if (ka < kt || (ka == kt && ga <= gt)) {
                break;
            }
            options[j + 1] = options[j];
        }
        options[j + 1] = t;
    }

    int n = 0;
    CSet claimed = { 0 };
    int free_[PL_PARTY_MAX], taken_in[PL_PARTY_MAX], given_out[PL_PARTY_MAX];
    for (int i = 0; i < all.n; i++) {
        free_[i] = 16 - all.m[i].n;
        taken_in[i] = given_out[i] = 0;
    }
    for (int oi = 0; oi < no; oi++) {
        const Option *o = &options[oi];
        int kx = -1, ky = -1;
        if (o->src.kind == SRC_GIVE) {
            kx = o->src.a;
            ky = o->src.b;
        } else if (o->src.kind == SRC_BAG) {
            kx = o->m;
            ky = o->src.a;
        }
        if (kx >= 0 && cset_has(&claimed, kx, ky)) {
            continue;
        }
        if (o->cost > money) {
            continue;
        }
        if (!unlimited && o->src.kind == SRC_GIVE
            && !(free_[o->m] - taken_in[o->m] > 0)) {
            continue;
        }
        if (!unlimited && !src_free(&o->src)
            && free_[o->m] - taken_in[o->m] + given_out[o->m] <= 0) {
            /* stow_from(m) */
            int b = o->m, r = -1, k = -1;
            int lim = (party < all.n) ? party : all.n;
            for (int q = 0; q < lim; q++) {
                if (q != b && !st->party[q].dead
                    && free_[q] - taken_in[q] > 0) {
                    /* max by (room, -r): the most room, the lowest r */
                    if (r < 0 || free_[q] - taken_in[q]
                                     > free_[r] - taken_in[r]) {
                        r = q;
                    }
                }
            }
            for (int q = 0; q < all.m[b].n; q++) {
                const BagItem *it = &all.m[b].v[q];
                if (it->worn || !is_stowable(it->name)
                    || cset_has(&claimed, b, q)) {
                    continue;
                }
                /* max by (not a mate, k): the last non-mate, else the
                 * last mate */
                bool nm = !is_mate(it->name);
                if (k < 0 || nm || is_mate(all.m[b].v[k].name)) {
                    k = q;
                }
            }
            if (r < 0 || k < 0) {
                continue;
            }
            cset_add(&claimed, b, k, 0);
            taken_in[r]++;
            given_out[b]++;
            if (n < cap) {
                GearTodo *t = &todo[n++];
                memset(t, 0, sizeof *t);
                t->src.kind = SRC_STOW;
                t->src.a = b;
                t->src.b = k;
                t->m = r;
                snprintf(t->name, 24, "%s", all.m[b].v[k].name);
            }
        }
        money -= o->cost;
        if (n < cap) {
            GearTodo *t = &todo[n++];
            memset(t, 0, sizeof *t);
            t->src = o->src;
            t->m = o->m;
            snprintf(t->name, 24, "%s", o->name);
            t->old_n = o->old_n;
            memcpy(t->old, o->old, sizeof t->old);
        }
        if (o->src.kind == SRC_GIVE) {
            taken_in[o->m]++;
            given_out[o->src.a]++;
        } else if (o->src.kind != SRC_BAG) {
            taken_in[o->m]++;
        }
        if (kx >= 0) {
            cset_add(&claimed, kx, ky, 0);
        }
    }
    cset_free(&claimed);
    if (n > 0 && !unlimited) {
        char line[2048];
        size_t used = (size_t)snprintf(line, sizeof line, "gear: ");
        for (int i = 0; i < n && used < sizeof line; i++) {
            const GearTodo *t = &todo[i];
            char from[64] = "";
            if (t->src.kind == SRC_BAG) {
                snprintf(from, sizeof from, " (carried)");
            } else if (t->src.kind == SRC_GIVE) {
                snprintf(from, sizeof from, " (from %s)",
                         st->party[t->src.a].name);
            } else if (t->src.kind == SRC_STOW) {
                snprintf(from, sizeof from, " (from %s, for room)",
                         st->party[t->src.a].name);
            }
            used += (size_t)snprintf(line + used, sizeof line - used,
                                     "%s%s %s%s", i ? ", " : "",
                                     st->party[t->m].name, t->name, from);
        }
        say(log, "%s", line);
    }
    return n;
}

int plan_gear(Session *s, int level, int reserve, Report *log,
              GearTodo *todo, int cap)
{
    return plan_gear_x(s, level, reserve, log, todo, cap, false);
}

/* geared: nothing in the town's shelves (or the bags) still worth
 * putting on, whatever it costs. */
bool geared(Session *s, int town)
{
    static GearTodo todo[64];
    return plan_gear_x(s, town, GEAR_RESERVE, NULL, todo, 64, true) == 0;
}

/* hand_for: the hand a one-hander goes into. */
static int hand_for(Session *s, int m, const GearTodo *todo, int n,
                    const Bag *bag)
{
    char held[PL_PARTY_MAX][2][24];
    int nh = hands(s, held);
    if (m >= nh) {
        int worn = 0;
        for (int i = 0; i < bag->n; i++) {
            int k = kind_of(bag->v[i].name);
            worn += bag->v[i].worn && (k == KIND_HAND || k == KIND_BOTH);
        }
        return (worn == 1) ? 1 : 0;
    }
    if (held[m][0][0] == '\0') {
        return 0;
    }
    if (held[m][1][0] == '\0') {
        return 1;
    }
    for (int h = 0; h < 2; h++) {
        for (int i = 0; i < n; i++) {
            int k = kind_of(todo[i].name);
            if (todo[i].m != m || !(k == KIND_HAND || k == KIND_BOTH)) {
                continue;
            }
            for (int j = 0; j < todo[i].old_n; j++) {
                if (streq(todo[i].old[j], held[m][h])) {
                    return h;
                }
            }
        }
    }
    return 0;
}

/* surplus: unworn gear nobody has a use for, sold anywhere. */
static int surplus(Session *s, Sale *out, int cap)
{
    static State snap;
    snap = s->state;
    const State *st = &snap;
    Bags all;
    bags(s, &all);
    Loadout worn[PL_PARTY_MAX];
    int nworn = 0;
    for (int m = 0; m < st->party_n; m++) {
        if (m < all.n) {
            loadout(&all.m[m], &worn[nworn++]);
        } else {
            memset(&worn[nworn++], 0, sizeof worn[0]);
        }
    }
    int n = 0;
    for (int m = 0; m < all.n; m++) {
        const Bag *bag = &all.m[m];
        for (int slot = bag->n - 1; slot >= 0; slot--) {
            const char *name = bag->v[slot].name;
            int kind = kind_of(name);
            if (bag->v[slot].worn || !sold_anywhere(name)
                || !(kind == KIND_HEAD || kind == KIND_HAND
                     || kind == KIND_BOTH || kind == KIND_BODY
                     || kind == KIND_LEGS)) {
                continue;
            }
            bool useful = false;
            for (int mm = 0; mm < st->party_n; mm++) {
                int cid = char_id(st->party[mm].name);
                if (cid < 0 || !wearable(name, cid) || mm >= nworn) {
                    continue;
                }
                const Loadout *have = &worn[mm];
                if (kind == KIND_HAND || kind == KIND_BOTH) {
                    int nh = have->n[KIND_HAND] + have->n[KIND_BOTH];
                    int weakest = 0;
                    bool any = false;
                    for (int k = 0; k < 2; k++) {
                        int kk = k ? KIND_BOTH : KIND_HAND;
                        for (int i = 0; i < have->n[kk]; i++) {
                            int v = hands_score1(have->name[kk][i]);
                            if (!any || v < weakest) {
                                weakest = v;
                            }
                            any = true;
                        }
                    }
                    if (nh < 2 || hands_score1(name) > weakest) {
                        useful = true;
                    }
                } else {
                    double cur = 0;
                    for (int i = 0; i < have->n[kind]; i++) {
                        double v = piece_score(have->name[kind][i]);
                        if (i == 0 || v > cur) {
                            cur = v;
                        }
                    }
                    if (piece_score(name) > cur) {
                        useful = true;
                    }
                }
            }
            if (!useful && n < cap) {
                out[n].who = m;
                out[n].slot = slot;
                n++;
            }
        }
    }
    return n;
}

static void buying_gear_begin(Session *s)
{
    host_line(s, "@gear on");
    s->buying_gear = true;
}

static void buying_gear_end(Session *s)
{
    s->buying_gear = false;
    host_line(s, "@gear off");
}

void gear_up(Session *s, int level, int reserve, Report *log)
{
    gear_up_got(s, level, reserve, log, NULL);
}

int gear_up_got(Session *s, int level, int reserve, Report *log,
                const GearTodo **got)
{
    char text[512];
    static GearTodo todo[64];
    if (got != NULL) {
        *got = todo;
    }
    if (!ready(s)) {
        return 0;
    }
    rolf_alive_for_shops(s, level);
    int n = plan_gear(s, level, reserve, log, todo, 64);
    /* GIVEs first, by holder, the highest slot first */
    int order[64], ng = 0;
    for (int i = 0; i < n; i++) {
        if (todo[i].src.kind == SRC_GIVE || todo[i].src.kind == SRC_STOW) {
            order[ng++] = i;
        }
    }
    for (int i = 1; i < ng; i++) {
        int t = order[i], j = i - 1;
        while (j >= 0 && (todo[order[j]].src.a > todo[t].src.a
                          || (todo[order[j]].src.a == todo[t].src.a
                              && -todo[order[j]].src.b > -todo[t].src.b))) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = t;
    }
    for (int i = 0; i < ng; i++) {
        const GearTodo *g = &todo[order[i]];
        Ints shown = { 0 };
        give_item(s, g->src.a, g->src.b, g->m, &shown);
        if (!ints_has(&shown, 0x24) && !ints_has(&shown, 0x08)) {
            say(log, "gear: could not give %s to %s: %s", g->name,
                s->state.party[g->m].name,
                scripts_text(&shown, text, sizeof text));
        }
        ints_free(&shown);
    }
    static const int SHOPS[2] = { BUILDING_WEAPON, BUILDING_ARMOR };
    for (int k = 0; k < 2; k++) {
        Purchase p[64];
        int np = 0;
        for (int i = 0; i < n; i++) {
            if (todo[i].src.kind == SRC_SHOP && todo[i].src.a == SHOPS[k]) {
                p[np].stock = todo[i].src.b;
                p[np].who = todo[i].m;
                np++;
            }
        }
        if (np > 0) {
            TownBuilding b;
            if (!town_building(level, SHOPS[k], &b)) {
                pl_error("KeyError: %d", SHOPS[k]);
            }
            buying_gear_begin(s);
            buy(s, level, b.y, b.x, p, np);
            buying_gear_end(s);
        }
    }
    for (int i = 0; i < n; i++) {
        const GearTodo *g = &todo[i];
        if (g->src.kind == SRC_STOW) {
            continue;
        }
        static Bags bg;
        bags(s, &bg);
        int m = g->m;
        if (m >= bg.n) {
            continue;
        }
        int last = -1;
        for (int k = 0; k < bg.m[m].n; k++) {
            if (streq(bg.m[m].v[k].name, g->name) && !bg.m[m].v[k].worn) {
                last = k;
            }
        }
        if (last < 0) {
            int planned = 0, worn_n = 0;
            for (int k = 0; k < n; k++) {
                planned += todo[k].m == m && streq(todo[k].name, g->name)
                        && todo[k].src.kind != SRC_STOW;
            }
            for (int k = 0; k < bg.m[m].n; k++) {
                worn_n += bg.m[m].v[k].worn
                       && streq(bg.m[m].v[k].name, g->name);
            }
            if (worn_n < planned) {
                say(log, "gear: %s is not in %s's bag -- not bought?",
                    g->name, s->state.party[m].name);
            }
            continue;
        }
        int hand = 0;
        if (kind_of(g->name) == KIND_HAND) {
            hand = hand_for(s, m, todo, n, &bg.m[m]);
        }
        Ints shown = { 0 };
        equip(s, m, last, hand, &shown);
        if (!ints_has(&shown, 0x202)) {
            say(log, "gear: %s could not put on %s: %s",
                s->state.party[m].name, g->name,
                scripts_text(&shown, text, sizeof text));
            ints_free(&shown);
            continue;
        }
        ints_free(&shown);
        if (kind_of(g->name) == KIND_HAND) {
            int worn_now = 0;
            for (int k = 0; k < bg.m[m].n; k++) {
                worn_now += bg.m[m].v[k].worn
                         && streq(bg.m[m].v[k].name, g->name);
            }
            bags(s, &bg);
            bool still_old = false;
            int again = -1, now = 0;
            for (int k = 0; k < bg.m[m].n; k++) {
                const BagItem *it = &bg.m[m].v[k];
                for (int j = 0; j < g->old_n; j++) {
                    still_old = still_old
                             || (it->worn && streq(it->name, g->old[j]));
                }
                if (streq(it->name, g->name) && !it->worn) {
                    again = k;
                }
                now += it->worn && streq(it->name, g->name);
            }
            bool gained = now > worn_now;
            if (again >= 0 && (still_old || !gained)) {
                equip(s, m, again, 1 - hand, NULL);
            }
        }
    }
    Sale sales[64];
    int nsale = surplus(s, sales, 64);
    if (nsale > 0) {
        TownBuilding b;
        if (town_building(level, BUILDING_ITEM, &b)) {
            sell(s, level, b.y, b.x, sales, nsale);
        }
    }
    return n;
}

/* A todo list as Python prints it: [(('bag', 3), 0, 'Ceramic', []), ...];
 * a shop's source is (building, stock). */
const char *gear_todo_repr(const GearTodo *todo, int n, char *out, size_t cap)
{
    size_t used = (size_t)snprintf(out, cap, "[");
    for (int i = 0; i < n && used < cap; i++) {
        const GearTodo *t = &todo[i];
        char src[48];
        if (t->src.kind == SRC_BAG) {
            snprintf(src, sizeof src, "('bag', %d)", t->src.a);
        } else if (t->src.kind == SRC_GIVE) {
            snprintf(src, sizeof src, "('give', %d, %d)", t->src.a, t->src.b);
        } else if (t->src.kind == SRC_STOW) {
            snprintf(src, sizeof src, "('stow', %d, %d)", t->src.a, t->src.b);
        } else {
            snprintf(src, sizeof src, "(%d, %d)", t->src.a, t->src.b);
        }
        used += (size_t)snprintf(out + used, cap - used, "%s(%s, %d, '%s', [",
                                 i ? ", " : "", src, t->m, t->name);
        for (int k = 0; k < t->old_n && used < cap; k++) {
            used += (size_t)snprintf(out + used, cap - used, "%s'%s'",
                                     k ? ", " : "", t->old[k]);
        }
        if (used < cap) {
            used += (size_t)snprintf(out + used, cap - used, "])");
        }
    }
    if (used < cap) {
        snprintf(out + used, cap - used, "]");
    }
    return out;
}

/* trip_marks: each member's level by name. */
void trip_marks(const Session *s, Marks *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < s->state.party_n && i < PL_PARTY_MAX; i++) {
        /* a dict: a later member of a name takes the earlier's level */
        int at = -1;
        for (int k = 0; k < out->n; k++) {
            if (streq(out->name[k], s->state.party[i].name)) {
                at = k;
            }
        }
        if (at < 0) {
            at = out->n++;
            snprintf(out->name[at], 16, "%s", s->state.party[i].name);
        }
        out->level[at] = s->state.party[i].level;
    }
}
