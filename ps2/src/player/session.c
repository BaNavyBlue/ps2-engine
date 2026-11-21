/* (build 171) play.py's Session: the commands down, the log back, the
 * state line read from it; the pauses (Jitter), the fight's pace, and
 * the walks through screens every policy builds on (press_through,
 * ready, new_game). Line for line the Python's, in its order. */

#include "pl.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>

/* ---- Jitter ---- */

void jitter_init(Jitter *j, const char *name)
{
    u64 h = 0xCBF29CE484222325ull;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        h = (h ^ *p) * 0x100000001B3ull;
    }
    j->state = h;
    j->on = true;
}

u64 jitter_next64(Jitter *j)
{
    j->state += 0x9E3779B97F4A7C15ull;
    u64 z = j->state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double jitter_random(Jitter *j)
{
    return (double)(jitter_next64(j) >> 11) / 9007199254740992.0;
}

/* a + x % n for the first x under the largest multiple of n that 2^64
 * holds: all of them when n divides 2^64. */
int jitter_randint(Jitter *j, int a, int b)
{
    u64 n = (u64)((long long)b - a + 1);
    u64 q = UINT64_MAX / n, r = UINT64_MAX % n;
    bool all = (r == n - 1);
    u64 limit = q * n;
    for (;;) {
        u64 x = jitter_next64(j);
        if (all || x < limit) {
            return (int)((long long)a + (long long)(x % n));
        }
    }
}

/* ---- the state line ---- */

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'
        || c == '\v';
}

/* Python's \w, near enough: ASCII letters, digits, _ and anything past
 * ASCII (the state line's words are ASCII). */
static bool is_word(char c)
{
    return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool lit(const char **p, const char *text)
{
    size_t n = strlen(text);
    if (strncmp(*p, text, n) != 0) {
        return false;
    }
    *p += n;
    return true;
}

static bool digits(const char **p, long *v)
{
    if (!is_digit(**p)) {
        return false;
    }
    long n = 0;
    while (is_digit(**p)) {
        n = n * 10 + (**p - '0');
        (*p)++;
    }
    *v = n;
    return true;
}

static bool word(const char **p, char *out, size_t cap)
{
    const char *s = *p;
    while (is_word(**p)) {
        (*p)++;
    }
    size_t n = (size_t)(*p - s);
    if (n == 0) {
        return false;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return true;
}

int map_index_name(const char *name);       /* map.c */

/* MEMBER_RE over the party's text, finditer's way. */
static void parse_members(const char *t, size_t len, State *st)
{
    const char *end = t + len;
    const char *p = t;
    while (p < end && st->party_n < PL_PARTY_MAX) {
        const char *q = p;
        Member m;
        memset(&m, 0, sizeof m);
        long v[5];
        char nm[16];
        const char *ws = q;
        while (q < end && is_word(*q)) {
            q++;
        }
        bool ok = q > ws;
        if (ok) {
            size_t n = (size_t)(q - ws);
            if (n >= sizeof nm) {
                n = sizeof nm - 1;
            }
            memcpy(nm, ws, n);
            nm[n] = '\0';
        }
        ok = ok && lit(&q, " L") && digits(&q, &v[0]) && lit(&q, " ")
          && digits(&q, &v[1]) && lit(&q, "/") && digits(&q, &v[2])
          && lit(&q, " ") && digits(&q, &v[3]) && lit(&q, "/");
        long tpmax = 0;
        ok = ok && digits(&q, &tpmax);
        if (!ok || q > end) {
            p++;
            continue;
        }
        snprintf(m.name, sizeof m.name, "%s", nm);
        m.level = (int)v[0];
        m.hp = (int)v[1];
        m.max_hp = (int)v[2];
        m.tp = (int)v[3];
        m.max_tp = (int)tpmax;
        m.curse = PL_NONE;
        if (q + 5 <= end && strncmp(q, " dead", 5) == 0) {
            m.dead = true;
            q += 5;
        }
        if (q + 7 <= end && strncmp(q, " poison", 7) == 0) {
            m.poison = true;
            q += 7;
        }
        bool cursed = false, marked = false;
        int kind = 0;
        if (q + 9 <= end && strncmp(q, " cursed ", 8) == 0
            && is_digit(q[8])) {
            cursed = true;
            kind = q[8] - '0';
            q += 9;
        }
        if (q + 7 <= end && strncmp(q, " marked", 7) == 0) {
            marked = true;
            q += 7;
        }
        m.curse = cursed ? kind : marked ? 0 : PL_NONE;
        if (q + 7 <= end && strncmp(q, " sealed", 7) == 0) {
            m.sealed = true;
            q += 7;
        }
        if (q + 6 <= end && strncmp(q, " deban", 6) == 0) {
            m.deban = true;
            q += 6;
        }
        st->party[st->party_n++] = m;
        p = q;
    }
}

/* The optional segments after the wait, each in its order, to the end. */
static bool parse_rest(const char *p, State *st)
{
    long v, w;
    if (lit(&p, ", level ")) {
        char lv[48];
        bool dollar = (*p == '$');
        if (dollar) {
            p++;
        }
        if (!word(&p, lv, sizeof lv)) {
            return false;
        }
        st->level = dollar ? (int)strtol(lv, NULL, 16) : map_index_name(lv);
        if (!lit(&p, " cell ") || !digits(&p, &v) || !lit(&p, ",")
            || !digits(&p, &w) || !lit(&p, " facing ")
            || !word(&p, st->facing, sizeof st->facing)) {
            return false;
        }
        st->x = (int)v;
        st->y = (int)w;
        if (lit(&p, " mid-step")) {
            st->mid_step = true;
        }
        if (lit(&p, " riding")) {
            st->riding = true;
        }
    }
    if (lit(&p, ", building ")) {
        if (!digits(&p, &v)) {
            return false;
        }
        st->building = (int)v;
    }
    if (lit(&p, ", menu $")) {
        char hex[16];
        if (!word(&p, hex, sizeof hex)) {
            return false;
        }
        st->menu = (int)strtol(hex, NULL, 16);
        const char *save = p;
        if (lit(&p, " stop ") && digits(&p, &v) && lit(&p, "/")
            && digits(&p, &w)) {
            st->menu_stop = (int)v;
            st->menu_stops = (int)w;
            if (lit(&p, " across")) {
                st->menu_across = true;
            }
            if (lit(&p, " party")) {
                st->menu_party = true;
            }
        } else {
            p = save;
        }
    }
    if (lit(&p, ", party ")) {
        const char *m = strstr(p, ", meseta ");
        if (m == NULL) {
            return false;
        }
        parse_members(p, (size_t)(m - p), st);
        p = m + strlen(", meseta ");
        if (!digits(&p, &v)) {
            return false;
        }
        st->meseta = (int)v;
    }
    if (lit(&p, ", formation ")) {
        if (!digits(&p, &v) || !lit(&p, " enemies")) {
            return false;
        }
        st->formation = (int)v;
        for (;;) {
            const char *save = p;
            long a, b, c;
            if (lit(&p, " ") && digits(&p, &a) && lit(&p, ":")
                && digits(&p, &b) && lit(&p, "/") && digits(&p, &c)) {
                if (st->enemy_n < PL_ENEMIES_MAX) {
                    st->enemy[st->enemy_n].id = (int)a;
                    st->enemy[st->enemy_n].hp = (int)b;
                    st->enemy[st->enemy_n].max_hp = (int)c;
                    st->enemy_n++;
                }
            } else {
                p = save;
                break;
            }
        }
    }
    return *p == '\0';
}

static bool parse_at(const char *p, State *st)
{
    long v;
    memset(st, 0, sizeof *st);
    st->level = st->x = st->y = PL_NONE;
    st->building = st->menu = st->menu_stop = st->menu_stops = PL_NONE;
    st->meseta = st->formation = PL_NONE;
    if (!lit(&p, "ok") || !is_space(*p)) {
        return false;
    }
    while (is_space(*p)) {
        p++;
    }
    if (!lit(&p, "tick ") || !digits(&p, &v) || !lit(&p, ": screen ")) {
        return false;
    }
    st->tick = (u32)v;
    if (!word(&p, st->screen, sizeof st->screen)) {
        return false;
    }
    p = strchr(p, ',');
    if (p == NULL) {
        return false;
    }
    if (lit(&p, ", waiting: ")) {
        snprintf(st->kind, sizeof st->kind, "waiting");
    } else if (lit(&p, ", busy: ")) {
        snprintf(st->kind, sizeof st->kind, "busy");
    } else {
        return false;
    }
    const char *e = strchr(p, ',');
    if (e == NULL) {
        e = p + strlen(p);
    }
    const char *w = e;
    while (w > p && is_space(w[-1])) {
        w--;
    }
    if (e == p) {
        return false;               /* [^,]+? wants one */
    }
    size_t n = (size_t)(w - p);
    if (n >= sizeof st->wait) {
        n = sizeof st->wait - 1;
    }
    memcpy(st->wait, p, n);
    st->wait[n] = '\0';
    /* g[4].strip(): the front too */
    char *f = st->wait;
    while (is_space(*f)) {
        f++;
    }
    memmove(st->wait, f, strlen(f) + 1);
    if (!parse_rest(e, st)) {
        return false;
    }
    st->valid = true;
    return true;
}

bool parse_state_line(const char *line, State *st)
{
    for (const char *p = strstr(line, "ok"); p != NULL;
         p = strstr(p + 1, "ok")) {
        State t;
        if (parse_at(p, &t)) {
            *st = t;
            return true;
        }
    }
    return false;
}

/* `* script $XXXX` (\*\s+script \$(\w+)), the first in the line. */
static bool script_of(const char *line, int *id)
{
    for (const char *p = strchr(line, '*'); p != NULL;
         p = strchr(p + 1, '*')) {
        const char *q = p + 1;
        if (!is_space(*q)) {
            continue;
        }
        while (is_space(*q)) {
            q++;
        }
        if (!lit(&q, "script $")) {
            continue;
        }
        char hex[16];
        if (!word(&q, hex, sizeof hex)) {
            continue;
        }
        char *bad = NULL;
        long v = strtol(hex, &bad, 16);
        if (bad == NULL || *bad != '\0') {
            pl_error("ValueError: invalid literal for int() with base 16: "
                     "'%s'", hex);
        }
        *id = (int)v;
        return true;
    }
    return false;
}

void scripts_of(const Lines *lines, Ints *out)
{
    for (int i = 0; i < lines->n; i++) {
        int id;
        if (script_of(lines->v[i], &id)) {
            ints_add(out, id);
        }
    }
}

bool wiped(const State *st)
{
    return st_screen(st, "gameover") || st_screen(st, "sega")
        || st_screen(st, "title");
}

/* ---- the session ---- */

void session_init(Session *s, const char *name, const PlayerIO *io)
{
    memset(s, 0, sizeof *s);
    s->io = *io;
    s->attached = io->attached;
    snprintf(s->name, sizeof s->name, "%s", name);
    snprintf(s->saves_dir, sizeof s->saves_dir, "route_out/%s/saves", name);
    s->save_town = 4;               /* PASEO */
    s->walk_mend = -1.0;
    s->last_tick = -1;
    set_pace(s, "probe");
}

void session_free(Session *s)
{
    lines_free(&s->pending);
    lines_free(&s->out);
    lines_free(&s->stderr_lines);
    ints_free(&s->shown);
    ints_free(&s->shown_ticks);
    ints_free(&s->looted);
    free(s->fights);
    free(s->tech_orders);
    free(s->downs);
    free(s->revivals);
    free(s->worn_bags);
    lines_free(&s->progress);
}

void set_pace(Session *s, const char *name)
{
    if (streq(name, "watch")) {
        s->fight_poll = 30;
        s->pad_waits = true;
        s->fight_jitter_max = JITTER_MAX / 2;
        s->victory_hold = 15;
        s->victory_slack = 5;
    } else {
        s->fight_poll = 240;
        s->pad_waits = false;
        s->fight_jitter_max = JITTER_MAX;
        s->victory_hold = 0;
        s->victory_slack = 0;
    }
    snprintf(s->pace, sizeof s->pace, "%s", name);
}

Session *player_session_new(const char *name, const PlayerIO *io)
{
    Session *s = pl_alloc(sizeof *s);
    session_init(s, name, io);
    return s;
}

void player_session_free(Session *s)
{
    if (s != NULL) {
        session_free(s);
        free(s);
    }
}

void player_log_line(Session *s, u32 tick, const char *line)
{
    session_log_line(s, tick, line);
}

void session_log_line(Session *s, u32 tick, const char *line)
{
    char buf[4096];
    snprintf(buf, sizeof buf, "%7u  %s", tick, line);
    lines_add(&s->pending, buf);
}

static void remember(Session *s, const char *line)
{
    if (s->last_n == 3) {
        memmove(s->last[0], s->last[1], sizeof s->last[0] * 2);
        s->last_n = 2;
    }
    snprintf(s->last[s->last_n++], sizeof s->last[0], "%s", line);
    if (strstr(line, "    |     ") != NULL) {
        lines_add(&s->stderr_lines, line);
    }
}

static void add_down(DownRec **v, int *n, int *cap, const DownRec *d)
{
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *v = pl_grow(*v, (size_t)*cap * sizeof **v);
    }
    (*v)[(*n)++] = *d;
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static bool name_in(char (*names)[16], int n, const char *name)
{
    for (int i = 0; i < n; i++) {
        if (streq(names[i], name)) {
            return true;
        }
    }
    return false;
}

static bool live_screen(const char *screen)
{
    return streq(screen, "level") || streq(screen, "building")
        || streq(screen, "battle");
}

/* _track_downs: downs and revivals from one state line to the next. */
static void track_downs(Session *s)
{
    const State *st = &s->state;
    char names[PL_PARTY_MAX][16], dead[PL_PARTY_MAX][16];
    int nn = 0, nd = 0;
    for (int i = 0; i < st->party_n; i++) {
        if (!name_in(names, nn, st->party[i].name)) {
            snprintf(names[nn++], 16, "%s", st->party[i].name);
        }
        if (st->party[i].dead && !name_in(dead, nd, st->party[i].name)) {
            snprintf(dead[nd++], 16, "%s", st->party[i].name);
        }
    }
    if (live_screen(st->screen) && s->prev_screen_set
        && live_screen(s->prev_screen)) {
        char list[PL_PARTY_MAX][16];
        int k = 0;
        for (int i = 0; i < nd; i++) {
            if (!name_in(s->dead, s->dead_n, dead[i])) {
                memcpy(list[k++], dead[i], 16);
            }
        }
        qsort(list, (size_t)k, 16, name_cmp);
        for (int i = 0; i < k; i++) {
            DownRec d;
            memset(&d, 0, sizeof d);
            memcpy(d.name, list[i], 16);
            snprintf(d.screen, sizeof d.screen, "%s", st->screen);
            d.level = st->level;
            d.building = PL_NONE;
            d.tick = st->tick;
            add_down(&s->downs, &s->downs_n, &s->downs_cap, &d);
        }
        k = 0;
        for (int i = 0; i < s->dead_n; i++) {
            if (!name_in(dead, nd, s->dead[i])
                && name_in(names, nn, s->dead[i])) {
                memcpy(list[k++], s->dead[i], 16);
            }
        }
        qsort(list, (size_t)k, 16, name_cmp);
        for (int i = 0; i < k; i++) {
            DownRec d;
            memset(&d, 0, sizeof d);
            memcpy(d.name, list[i], 16);
            snprintf(d.screen, sizeof d.screen, "%s", st->screen);
            d.building = st->building;
            d.level = st->level;
            d.tick = st->tick;
            int from = s->shown.n - 4;
            if (from < 0) {
                from = 0;
            }
            for (int j = from; j < s->shown.n; j++) {
                d.scripts[d.script_n++] = s->shown.v[j];
            }
            add_down(&s->revivals, &s->revivals_n, &s->revivals_cap, &d);
        }
    }
    if (live_screen(st->screen)) {
        memcpy(s->dead, dead, sizeof dead);
        s->dead_n = nd;
    } else {
        s->dead_n = 0;
    }
    snprintf(s->prev_screen, sizeof s->prev_screen, "%s", st->screen);
    s->prev_screen_set = true;
}

/* _pause: the idle ticks a player takes before `first`, or NULL. */
static const char *pause_for(Session *s, const char *first, char *buf,
                             size_t n)
{
    if (!s->jitter.on || s->route_depth || first == NULL) {
        return NULL;
    }
    const State *st = &s->state;
    if (!st_waiting(st)
        || !(st_screen(st, "level") || st_screen(st, "battle"))
        || !(starts(first, "walk ") || starts(first, "press "))) {
        return NULL;
    }
    if (jitter_random(&s->jitter) >= JITTER_P) {
        return NULL;
    }
    int most = st_screen(st, "battle") ? s->fight_jitter_max : JITTER_MAX;
    int k = jitter_randint(&s->jitter, 1, most);
    s->jitter_ticks += k;
    if (st_screen(st, "battle")) {
        s->jitter_fight_ticks += k;
    }
    snprintf(buf, n, "wait ticks %d", k);
    return buf;
}

const Lines *sendv(Session *s, int n, const char *const *cmds)
{
    char buf[64];
    const char *pause = pause_for(s, (n > 0) ? cmds[0] : NULL, buf,
                                  sizeof buf);
    if (pause != NULL) {
        s->io.run(s->io.ctx, pause);
    }
    for (int i = 0; i < n; i++) {
        s->io.run(s->io.ctx, cmds[i]);
    }
    s->io.run(s->io.ctx, "state");
    lines_clear(&s->out);
    while (s->pending_head < s->pending.n) {
        const char *line = s->pending.v[s->pending_head++];
        lines_add(&s->out, line);
        remember(s, line);
        int id;
        if (script_of(line, &id)) {
            s->last_script = id;
            ints_add(&s->shown, id);
            ints_add(&s->shown_ticks, (int)strtol(line, NULL, 10));
        }
        State st;
        if (parse_state_line(line, &st)) {
            s->state = st;
            long t = (long)st.tick;
            if (s->last_tick >= 0) {
                s->ticks += (t >= s->last_tick) ? t - s->last_tick : t;
            }
            s->last_tick = t;
            track_downs(s);
            if (s->pending_head == s->pending.n) {
                lines_clear(&s->pending);
                s->pending_head = 0;
            }
            return &s->out;
        }
    }
    lines_clear(&s->pending);
    s->pending_head = 0;
    pl_error("Stall: no state line; last: %s; %s; %s",
             s->last_n > 0 ? s->last[0] : "", s->last_n > 1 ? s->last[1] : "",
             s->last_n > 2 ? s->last[2] : "");
}

const Lines *send1(Session *s, const char *cmd)
{
    return sendv(s, 1, &cmd);
}

const Lines *sendf(Session *s, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    const char *c = buf;
    return sendv(s, 1, &c);
}

const Lines *send2(Session *s, const char *a, const char *b)
{
    const char *c[2] = { a, b };
    return sendv(s, 2, c);
}

const Lines *send3(Session *s, const char *a, const char *b, const char *c)
{
    const char *v[3] = { a, b, c };
    return sendv(s, 3, v);
}

/* _host_line: only the game's COMPUTER PLAYS hears them. */
void host_line(Session *s, const char *line)
{
    if (!s->attached) {
        return;
    }
    s->io.run(s->io.ctx, line);
}

void in_grind_begin(Session *s)
{
    s->grind_depth++;
    if (s->grind_depth == 1) {
        host_line(s, "@grind on");
    }
}

void in_grind_end(Session *s)
{
    s->grind_depth--;
    if (s->grind_depth == 0) {
        host_line(s, "@grind off");
    }
}

const char *fight_wait(Session *s, char *buf, size_t n)
{
    if (s->pad_waits) {
        snprintf(buf, n, "wait pad %d", s->fight_poll);
    } else {
        snprintf(buf, n, "wait ticks %d", s->fight_poll);
    }
    return buf;
}

#define SCRIPT_VICTORY_FIRST 0x1211
#define SCRIPT_VICTORY_LAST  0x1217

const Lines *answer_page(Session *s)
{
    if (s->victory_hold && st_wait(&s->state, "text")
        && s->last_script >= SCRIPT_VICTORY_FIRST
        && s->last_script <= SCRIPT_VICTORY_LAST) {
        int hold = s->victory_hold;
        if (s->victory_slack && s->jitter.on) {
            hold += jitter_randint(&s->jitter, -s->victory_slack,
                                   s->victory_slack);
        }
        sendf(s, "wait ticks %d", hold);
    }
    return send1(s, "press c");
}

bool may_hurry(const Session *s)
{
    return !(s->pad_waits && st_wait(&s->state, "text printing"));
}

/* ---- screens ---- */

/* language_is: LANGUAGE is `key`, asked with `expect setting language`. */
bool language_is(Session *s, const char *key)
{
    const Lines *out = sendf(s, "expect setting language %s", key);
    return !lines_any(out, "FAIL");
}

bool japanese_board(Session *s)
{
    return language_is(s, "ja");
}

void type_end(Session *s)
{
    if (japanese_board(s)) {
        send3(s, "press up", "press left", "press c");
    } else {
        const char *c[4] = { "press up", "press right", "press right",
                             "press c" };
        sendv(s, 4, c);
    }
}

/* menu_pick: the live cursor to `stop` a press at a time, then C. */
bool menu_pick(Session *s, int stop)
{
    for (int i = 0; i < 24; i++) {
        const State *st = &s->state;
        if (!st_wait(st, "menu") || st->menu_stops == PL_NONE
            || st->menu_stops == 0) {
            return false;
        }
        int cur = st->menu_stop;
        if (cur == stop) {
            send1(s, "press c");
            return true;
        }
        const char *d;
        if (st->menu_across) {
            d = (cur < stop) ? "right" : "left";
        } else {
            d = (cur < stop) ? "down" : "up";
        }
        sendf(s, "press %s", d);
    }
    return false;
}

bool leave_building(Session *s)
{
    for (int i = 0; i < 40; i++) {
        const State *st = &s->state;
        if (st_control(st, "level")) {
            return true;
        }
        if (!st_screen(st, "building") && !st_screen(st, "level")) {
            return false;
        }
        if (st_waiting(st)) {
            send2(s, st_wait(st, "menu") ? "press b" : "press c",
                  "wait idle");
        } else {
            send1(s, "wait idle");
        }
    }
    return false;
}

/* routes/new_game.route's way over the stock board from A: ROLF, END. */
static void name_rolf(Session *s)
{
    send1(s, "press down");
    for (int i = 0; i < 8; i++) {
        send1(s, "press right");
    }
    send1(s, "press c");
    for (int i = 0; i < 3; i++) {
        send1(s, "press left");
    }
    send1(s, "press c");
    for (int i = 0; i < 3; i++) {
        send1(s, "press left");
    }
    send1(s, "press c");
    send1(s, "press up");
    for (int i = 0; i < 3; i++) {
        send1(s, "press right");
    }
    send1(s, "press c");
    send1(s, "press up");
    send1(s, "press right");
    send1(s, "wait idle");
    send1(s, "press c");
}

void new_game(Session *s)
{
    static const char *const BOOT[] = {
        "start new", "wait screen title", "wait idle", "press start",
        "wait screen intro", "wait idle", "press c", "wait idle"
    };
    s->route_depth++;
    for (size_t i = 0; i < sizeof BOOT / sizeof BOOT[0]; i++) {
        send1(s, BOOT[i]);
    }
    if (language_is(s, "us")) {
        name_rolf(s);
    } else {
        type_end(s);
    }
    for (int i = 0; i < 200; i++) {
        send1(s, "wait idle");
        const State *st = &s->state;
        if (!st_waiting(st)) {
            continue;
        }
        if (st_control(st, "level")) {
            break;
        }
        send1(s, "press c");
    }
    send1(s, "walk down 2");
    send1(s, "wait step");
    s->route_depth--;
}

void press_through(Session *s, const char *screen, Ints *shown)
{
    char buf[48];
    for (int i = 0; i < 200; i++) {
        const State *st = &s->state;
        if (wiped(st)) {
            return;
        }
        if (st_control(st, screen)) {
            return;
        }
        const Lines *out;
        if (st_waiting(st)) {
            if (st_wait(st, "menu") && st_screen(st, "level")) {
                out = send1(s, "press b");
            } else {
                out = answer_page(s);
            }
        } else if (st_screen(st, "battle")) {
            out = send1(s, fight_wait(s, buf, sizeof buf));
            if (!st_waiting(&s->state) && may_hurry(s)) {
                if (shown != NULL) {
                    scripts_of(out, shown);
                }
                out = send1(s, "press c");
            }
        } else {
            out = send1(s, "wait ticks 60");
        }
        if (shown != NULL) {
            scripts_of(out, shown);
        }
    }
}

bool ready(Session *s)
{
    return ready_n(s, 40);
}

bool ready_n(Session *s, int limit)
{
    for (int i = 0; i < limit; i++) {
        const State *st = &s->state;
        if (wiped(st)) {
            return false;
        }
        if (st_control(st, "level")) {
            return true;
        }
        if (st_screen(st, "building")) {
            leave_building(s);
        } else if (st_screen(st, "battle") || st_waiting(st)) {
            press_through(s, "level", NULL);
        } else {
            send1(s, "wait idle");
        }
    }
    return false;
}

void session_fight(Session *s, Ints *shown)
{
    press_through(s, "level", shown);
}

/* A str as Python's repr prints it: single quotes, double quotes when it
 * holds a single quote and no double one. */
static size_t py_repr_str(char *out, size_t n, const char *s)
{
    char q = (strchr(s, '\'') != NULL && strchr(s, '"') == NULL) ? '"' : '\'';
    size_t used = (size_t)snprintf(out, n, "%c", q);
    for (const char *p = s; *p != '\0' && used < n; p++) {
        if (*p == '\\' || *p == q) {
            used += (size_t)snprintf(out + used, n - used, "\\%c", *p);
        } else {
            used += (size_t)snprintf(out + used, n - used, "%c", *p);
        }
    }
    if (used < n) {
        used += (size_t)snprintf(out + used, n - used, "%c", q);
    }
    return used;
}

static size_t py_int_or_none(char *out, size_t n, int v)
{
    return (v == PL_NONE) ? (size_t)snprintf(out, n, "None")
                          : (size_t)snprintf(out, n, "%d", v);
}

static const char *py_bool(bool b)
{
    return b ? "True" : "False";
}

/* str(s.state): parse_state's dict as Python prints it. */
const char *state_repr(const State *st, char *out, size_t n)
{
    if (!st->valid) {
        snprintf(out, n, "{}");
        return out;
    }
    size_t u = 0;
#define ADD(...) do { if (u < n) { u += (size_t)snprintf(out + u, n - u, __VA_ARGS__); } } while (0)
#define STR(v) do { if (u < n) { u += py_repr_str(out + u, n - u, (v)); } } while (0)
#define NUM(v) do { if (u < n) { u += py_int_or_none(out + u, n - u, (v)); } } while (0)
    ADD("{'tick': %u, 'screen': ", st->tick);
    STR(st->screen);
    ADD(", 'kind': ");
    STR(st->kind);
    ADD(", 'wait': ");
    STR(st->wait);
    ADD(", 'level': ");
    NUM(st->level);
    ADD(", 'x': ");
    NUM(st->x);
    ADD(", 'y': ");
    NUM(st->y);
    ADD(", 'facing': ");
    if (st->facing[0] == '\0') {
        ADD("None");
    } else {
        STR(st->facing);
    }
    ADD(", 'mid_step': %s, 'riding': %s, 'building': ", py_bool(st->mid_step),
        py_bool(st->riding));
    NUM(st->building);
    ADD(", 'menu': ");
    NUM(st->menu);
    ADD(", 'menu_stop': ");
    NUM(st->menu_stop);
    ADD(", 'menu_stops': ");
    NUM(st->menu_stops);
    ADD(", 'menu_across': %s, 'menu_party': %s, 'party': [",
        py_bool(st->menu_across), py_bool(st->menu_party));
    for (int i = 0; i < st->party_n; i++) {
        const Member *m = &st->party[i];
        ADD("%s{'name': ", i ? ", " : "");
        STR(m->name);
        ADD(", 'level': %d, 'hp': %d, 'max_hp': %d, 'tp': %d, 'max_tp': %d, "
            "'dead': %s, 'poison': %s, 'curse': ", m->level, m->hp, m->max_hp,
            m->tp, m->max_tp, py_bool(m->dead), py_bool(m->poison));
        NUM(m->curse);
        ADD(", 'sealed': %s, 'deban': %s}", py_bool(m->sealed),
            py_bool(m->deban));
    }
    ADD("], 'meseta': ");
    NUM(st->meseta);
    ADD(", 'formation': ");
    NUM(st->formation);
    ADD(", 'enemies': [");
    for (int i = 0; i < st->enemy_n; i++) {
        ADD("%s(%d, %d, %d)", i ? ", " : "", st->enemy[i].id, st->enemy[i].hp,
            st->enemy[i].max_hp);
    }
    ADD("]}");
#undef ADD
#undef STR
#undef NUM
    return out;
}
