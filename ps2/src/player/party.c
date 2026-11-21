/* (build 173) play.py's who goes: the roster, member_value and
 * choose_party (the core, then the flexible places by value, or the
 * lowest to catch up), Nei's place until Climatrol, and pick_party at
 * Rolf's house. */

#include "pl.h"

#include "../core/json.h"

#include <math.h>
#include <stdlib.h>

static const char *const CHAR_NAMES[8] = {
    "Rolf", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"
};

/* Healing a member can cast outside a fight, weighted by how much it
 * mends. */
static const struct {
    const char *key;
    int         weight;
} HEAL_WEIGHT[8] = {
    { "Res", 10 }, { "Gires", 20 }, { "Nares", 30 }, { "Sar", 20 },
    { "Gisar", 30 }, { "Nasar", 40 }, { "Rever", 25 }, { "Anti", 5 }
};

/* roster(s): everyone who has joined, in join order ({} when no line). */
int roster(Session *s, Roster *out)
{
    out->n = 0;
    const Lines *lines = send1(s, "roster");
    for (int i = 0; i < lines->n; i++) {
        const char *p = ok_word(lines->v[i], "roster");
        if (p == NULL) {
            continue;
        }
        /* ROSTER_RE.findall: (\w+):L(\d+):(\d+)/(\d+):(\d+)/(\d+)(\*?) */
        char *copy = pl_strdup(p);
        for (char *tok = strtok(copy, " "); tok != NULL;
             tok = strtok(NULL, " ")) {
            RosterEntry e;
            memset(&e, 0, sizeof e);
            int used = 0;
            if (sscanf(tok, "%15[A-Za-z0-9_]:L%d:%d/%d:%d/%d%n", e.name,
                       &e.level, &e.hp, &e.max_hp, &e.tp, &e.max_tp,
                       &used) != 6 || e.level < 0 || e.hp < 0
                || e.max_hp < 0 || e.tp < 0 || e.max_tp < 0) {
                continue;
            }
            e.in_party = tok[used] == '*';
            /* a dict by name: the first one's place, the last value */
            int at = -1;
            for (int k = 0; k < out->n; k++) {
                if (streq(out->v[k].name, e.name)) {
                    at = k;
                }
            }
            if (at >= 0) {
                out->v[at] = e;
            } else if (out->n < PL_ROSTER_MAX) {
                out->v[out->n++] = e;
            }
        }
        free(copy);
        return out->n;
    }
    return 0;
}

/* member_value: HP, attack before weapons, defense, agility, the healing
 * the map techniques give, and (build 176) with a dungeon's mix the best
 * damage technique against it. */
static double member_value_mix(const char *name, int level, const Mix *mix)
{
    int hp = expected_stat(name, level, "hp");
    int attack = expected_stat(name, level, "attack");
    int defense = expected_stat(name, level, "defense");
    int agility = expected_stat(name, level, "agility");
    int techs_b = expected_stat(name, level, "techs_b");
    const JsonValue *avail = json_get(json_get(json_get(techs_data(),
                                                        "available"),
                                               name), "map");
    if (avail == NULL) {
        pl_error("KeyError: '%s'", name);
    }
    int count = json_length(avail);
    int upto = (techs_b < count) ? techs_b : count;
    int heals = 0;
    for (int k = 0; k < upto; k++) {
        const JsonValue *v = json_at(avail, k);
        const char *key = (v->type == JSON_STRING) ? v->string : "";
        for (int h = 0; h < 8; h++) {
            if (streq(HEAL_WEIGHT[h].key, key)) {
                heals += HEAL_WEIGHT[h].weight;
            }
        }
    }
    return hp + 1.5 * attack + defense + 0.5 * agility + heals
         + 0.5 * tech_power(name, level, mix);
}

double member_value(const char *name, int level)
{
    return member_value_mix(name, level, NULL);
}

/* format(v, ".0f"): round half to even, as Python prints it. */
static const char *fmt0(double v, char *buf, size_t n)
{
    snprintf(buf, n, "%.0f", nearbyint(v));
    return buf;
}

typedef struct {
    char name[16];
    int  level;
} Pick;

/* choose_party(members, places=4, core, catch_up): the party's names in
 * order into out, the reason into why. */
static int choose_party(const Pick *members, int nm, const char *const *core,
                        int ncore, bool catch_up, const Mix *mix,
                        char (*out)[16], char *why, size_t wn)
{
    const int places = 4;
    int np = 0;
    for (int c = 0; c < ncore && np < places; c++) {
        for (int i = 0; i < nm; i++) {
            if (streq(members[i].name, core[c])) {
                snprintf(out[np++], 16, "%s", core[c]);
                break;
            }
        }
    }
    Pick rest[PL_ROSTER_MAX];
    int nr = 0;
    for (int i = 0; i < nm; i++) {
        bool in = false;
        for (int k = 0; k < np; k++) {
            in = in || streq(out[k], members[i].name);
        }
        if (!in) {
            rest[nr++] = members[i];
        }
    }
    size_t used = (size_t)snprintf(why, wn, "core ");
    for (int k = 0; k < np && used < wn; k++) {
        used += (size_t)snprintf(why + used, wn - used, "%s%s",
                                 k ? ", " : "", out[k]);
    }
    while (np < places && nr > 0) {
        /* ranked: sorted by -member_value, stable */
        Pick ranked[PL_ROSTER_MAX];
        double val[PL_ROSTER_MAX];
        for (int i = 0; i < nr; i++) {
            double v = member_value_mix(rest[i].name, rest[i].level, mix);
            int j = i;
            for (; j > 0 && val[j - 1] < v; j--) {
                ranked[j] = ranked[j - 1];
                val[j] = val[j - 1];
            }
            ranked[j] = rest[i];
            val[j] = v;
        }
        int low = 0;                    /* min by level: the first */
        for (int i = 1; i < nr; i++) {
            if (rest[i].level < rest[low].level) {
                low = i;
            }
        }
        const Pick *best = &ranked[0];
        Pick pick;
        if (catch_up && !streq(rest[low].name, best->name)
            && best->level - rest[low].level >= CATCH_UP_GAP) {
            pick = rest[low];
            if (used < wn) {
                used += (size_t)snprintf(why + used, wn - used,
                                         "; %s L%d to catch up with %s L%d",
                                         rest[low].name, rest[low].level,
                                         best->name, best->level);
            }
        } else {
            pick = *best;
            if (used < wn) {
                used += (size_t)snprintf(why + used, wn - used,
                                         "; %s by value ", best->name);
            }
            for (int i = 0; i < nr && used < wn; i++) {
                char b[32];
                used += (size_t)snprintf(why + used, wn - used, "%s%s %s",
                                         i ? ", " : "", ranked[i].name,
                                         fmt0(val[i], b, sizeof b));
            }
        }
        snprintf(out[np++], 16, "%s", pick.name);
        /* rest.remove(pick): the first of that name */
        int at = 0;
        while (at < nr && !streq(rest[at].name, pick.name)) {
            at++;
        }
        for (int i = at; i + 1 < nr; i++) {
            rest[i] = rest[i + 1];
        }
        if (at < nr) {
            nr--;
        }
    }
    return np;
}

/* nei_available: only while $C737 is clear. */
static bool nei_available(Session *s)
{
    return !lines_any(send1(s, "expect flag $C737 0"), "FAIL");
}

/* pickable: {name: level} of everyone who can be taken -- the roster,
 * less Nei once $C737 is set. */
static int pickable(Session *s, const Roster *r, Pick *out)
{
    int n = 0;
    bool nei = false;
    for (int i = 0; i < r->n; i++) {
        snprintf(out[n].name, 16, "%s", r->v[i].name);
        out[n].level = r->v[i].level;
        nei = nei || streq(r->v[i].name, "Nei");
        n++;
    }
    if (nei && !nei_available(s)) {
        int k = 0;
        for (int i = 0; i < n; i++) {
            if (!streq(out[i].name, "Nei")) {
                out[k++] = out[i];
            }
        }
        n = k;
    }
    return n;
}

/* regroup_stops: the regroup list's stops for `want` beyond Rolf and
 * Nei, each pick leaving the list. */
static int regroup_stops(const Pick *members, int nm, char (*want)[16],
                         int nw, int *stops)
{
    char left[8][16];
    int nl = 0, ns = 0;
    for (int c = 0; c < 8; c++) {
        if (streq(CHAR_NAMES[c], "Rolf") || streq(CHAR_NAMES[c], "Nei")) {
            continue;
        }
        for (int i = 0; i < nm; i++) {
            if (streq(members[i].name, CHAR_NAMES[c])) {
                snprintf(left[nl++], 16, "%s", CHAR_NAMES[c]);
                break;
            }
        }
    }
    for (int w = 0; w < nw; w++) {
        if (streq(want[w], "Rolf") || streq(want[w], "Nei")) {
            continue;
        }
        int at = -1;
        for (int i = 0; i < nl && at < 0; i++) {
            if (streq(left[i], want[w])) {
                at = i;
            }
        }
        if (at < 0) {
            continue;
        }
        stops[ns++] = at;
        for (int i = at; i + 1 < nl; i++) {
            memcpy(left[i], left[i + 1], 16);
        }
        nl--;
    }
    return ns;
}

static int name_cmp16(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static bool same_names(char (*a)[16], int na, char (*b)[16], int nb)
{
    if (na != nb) {
        return false;
    }
    char x[PL_ROSTER_MAX][16], y[PL_ROSTER_MAX][16];
    memcpy(x, a, (size_t)na * 16);
    memcpy(y, b, (size_t)nb * 16);
    qsort(x, (size_t)na, 16, name_cmp16);
    qsort(y, (size_t)nb, 16, name_cmp16);
    /* By the names, not the 16 bytes: past a name's NUL is anything. */
    for (int i = 0; i < na; i++) {
        if (!streq(x[i], y[i])) {
            return false;
        }
    }
    return true;
}

static const char *const CORE[3] = { "Rolf", "Nei", "Rudo" };

int pick_party(Session *s, Report *log, char (*out)[16])
{
    return pick_party_x(s, log, NULL, 0, NULL, 0, true, CORE, 3, out);
}

int pick_party_want(Session *s, Report *log, const char *const *want,
                    int nwant, char (*out)[16])
{
    return pick_party_x(s, log, want, nwant, NULL, 0, true, CORE, 3, out);
}

int choose_party_ahead(Session *s, const int *ahead, int nahead,
                       bool catch_up, const char *const *core, int ncore,
                       char (*out)[16], char *why, size_t wn)
{
    Roster r;
    roster(s, &r);
    Pick members[PL_ROSTER_MAX];
    int nm = pickable(s, &r, members);
    Mix mix;
    dungeon_mix(ahead, nahead, &mix);
    return choose_party(members, nm, core, ncore, catch_up, &mix, out, why,
                        wn);
}

int pick_party_x(Session *s, Report *log, const char *const *want, int nwant,
                 const int *ahead, int nahead, bool catch_up,
                 const char *const *core, int ncore, char (*out)[16])
{
    Roster r;
    roster(s, &r);
    Pick members[PL_ROSTER_MAX];
    int nm = pickable(s, &r, members);
    char party[PL_ROSTER_MAX][16], why[1024];
    int np;
    if (want != NULL) {
        np = (nwant < PL_ROSTER_MAX) ? nwant : PL_ROSTER_MAX;
        for (int i = 0; i < np; i++) {
            snprintf(party[i], 16, "%s", want[i]);
        }
        snprintf(why, sizeof why, "chosen by the caller");
    } else {
        Mix mix = { { 0.0, 0.0, 0.0 }, false };
        if (nahead > 0) {
            dungeon_mix(ahead, nahead, &mix);
        }
        np = choose_party(members, nm, core, ncore, catch_up, &mix, party,
                          why, sizeof why);
        if (mix.set) {
            char m[128];
            size_t used = strlen(why);
            snprintf(why + used, sizeof why - used, " [mix %s]",
                     mix_repr(&mix, m, sizeof m));
        }
    }
    char now[PL_ROSTER_MAX][16];
    int nn = 0;
    for (int i = 0; i < r.n; i++) {
        if (r.v[i].in_party) {
            snprintf(now[nn++], 16, "%s", r.v[i].name);
        }
    }
    char names[128] = "";
    size_t used = 0;
    for (int i = 0; i < np && used < sizeof names; i++) {
        used += (size_t)snprintf(names + used, sizeof names - used, "%s%s",
                                 i ? " " : "", party[i]);
    }
    say(log, "party: %s (%s)", names, why);
    if (same_names(now, nn, party, np)) {
        memcpy(out, party, (size_t)np * 16);
        return np;
    }
    if (!enter_town(s, PASEO)) {
        say(log, "party: could not get to Paseo");
        memcpy(out, now, (size_t)nn * 16);
        return nn;
    }
    bool nei = false;
    for (int i = 0; i < np; i++) {
        nei = nei || streq(party[i], "Nei");
    }
    int from = nei ? 2 : 1;
    int stops[8];
    int ns = regroup_stops(members, nm, party + from,
                           (np > from) ? np - from : 0, stops);
    rolf_house_regroup(s, stops, ns, NULL);
    roster(s, &r);
    int n = 0;
    for (int i = 0; i < r.n; i++) {
        if (r.v[i].in_party) {
            snprintf(out[n++], 16, "%s", r.v[i].name);
        }
    }
    return n;
}
