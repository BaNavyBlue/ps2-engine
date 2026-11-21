/* (build 171) tools/playthrough.py's loop in C: the story's stages in
 * order, each tried up to its tries from the last checkpoint, a
 * checkpoint after each, and the report. Its arguments are
 * playthrough.py's; its probe's log, route_out/<base>/probe/log.txt, is
 * the Python's own line for line (docs/c_player.md). And story.py's
 * Report, party_line, checkpoint, recover. */
/* (build 180b) clock_gettime and struct stat's st_mtim are POSIX's, which
 * -std=c11 hides on glibc; macOS shows its st_mtimespec only with
 * _DARWIN_C_SOURCE beside _POSIX_C_SOURCE. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include "pl.h"

#include "../core/json.h"

#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

bool pl_lean;
bool pl_squeak;
char pl_saves[256];             /* story.SAVES */
char pl_mid_saves[256];         /* play.MID_SAVES; "" for None */
FILE *pl_echo;                  /* (build 179) NULL: stdout */
void (*pl_on_save)(const char *kind, const char *stage, const char *path);

/* ---- time, for the checkpoints' "written since" ---- */

double pl_now(void)
{
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    u64 t = ((u64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (double)(t - 116444736000000000ull) / 1e7;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

/* The file's st_mtime as Python reads it, or -1 when there is none. */
double pl_mtime(const char *path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) {
        return -1.0;
    }
    u64 t = ((u64)a.ftLastWriteTime.dwHighDateTime << 32)
          | a.ftLastWriteTime.dwLowDateTime;
    return (double)(t - 116444736000000000ull) / 1e7;
#else
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return -1.0;
    }
#ifdef __APPLE__
    return (double)sb.st_mtimespec.tv_sec + sb.st_mtimespec.tv_nsec / 1e9;
#else
    return (double)sb.st_mtim.tv_sec + sb.st_mtim.tv_nsec / 1e9;
#endif
#endif
}

/* ---- the Report ---- */

void say(Report *log, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (log != NULL) {
        lines_add(&log->lines, buf);
    }
    FILE *f = (pl_echo != NULL) ? pl_echo : stdout;
    fprintf(f, "%s\n", buf);
    fflush(f);
}

const char *party_line(const Session *s, char *out, size_t n)
{
    const State *st = &s->state;
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < st->party_n && used < n; i++) {
        const Member *m = &st->party[i];
        used += (size_t)snprintf(out + used, n - used, "%s%s L%d %d/%d%s",
                                 i ? " " : "", m->name, m->level, m->hp,
                                 m->max_hp, m->dead ? " dead" : "");
    }
    if (used < n) {
        char me[16];
        snprintf(me, sizeof me, "%d", st->meseta);
        snprintf(out + used, n - used, "; meseta %s",
                 (st->meseta == PL_NONE) ? "None" : me);
    }
    return out;
}

bool party_dead(const Session *s)
{
    for (int i = 0; i < s->state.party_n; i++) {
        if (s->state.party[i].dead) {
            return true;
        }
    }
    return false;
}

bool party_has(const Session *s, const char *name)
{
    for (int i = 0; i < s->state.party_n; i++) {
        if (streq(s->state.party[i].name, name)) {
            return true;
        }
    }
    return false;
}

/* story.checkpoint: save in slot 1 at the nearest Data Memory, then a
 * copy as SAVES/story_<name>.json. */
void checkpoint(Session *s, const char *name, Report *log)
{
    char pl[512], src[300], dst[300];
    if (st_screen(&s->state, "building")) {
        leave_building(s);
    }
    if (!st_screen(&s->state, "level") && !st_screen(&s->state, "building")) {
        say(log, "checkpoint: none for %s -- the screen is %s", name,
            s->state.screen);
        return;
    }
    /* (build 176) stale by the slot's own mtime before the save, not by
     * the wall clock (a coarse file clock against a precise one) */
    snprintf(src, sizeof src, "%s/slot1.json", s->saves_dir);
    double before = pl_mtime(src);
    int town = s->state.level;
    TownBuilding dm;
    bool has_dm = town != PL_NONE && town < 0x10
               && town_building(town, BUILDING_DATA_MEMORY, &dm);
    if (town == 3) {
        send1(s, "save-here 1");
        s->looted.n = 0;
    } else if (has_dm) {
        save_at(s, 1, town, dm.y, dm.x);
    } else {
        if (town != PASEO) {
            enter_town(s, PASEO);
        }
        save_paseo(s, 1);
    }
    double mt = pl_mtime(src);
    if (mt >= 0 && before >= 0 && mt <= before) {
        say(log, "checkpoint: no save written for %s (slot 1 is stale)",
            name);
        return;
    }
    if (mt >= 0) {
        pl_make_dirs(pl_saves);
        snprintf(dst, sizeof dst, "%s/story_%s.json", pl_saves, name);
        pl_copy_file(src, dst);
        say(log, "checkpoint: %s -- %s", dst, party_line(s, pl, sizeof pl));
    } else {
        say(log, "checkpoint: no save written for %s", name);
    }
}

/* story.recover: wiped out, CONTINUE the session's last save. */
bool recover(Session *s, Report *log)
{
    char slot[300], pl[512];
    snprintf(slot, sizeof slot, "%s/slot1.json", s->saves_dir);
    s->careful = s->hurt = false;
    if (!pl_file_exists(slot) || !continue_game(s, slot)) {
        say(log, "wiped out, and no save to continue");
        return false;
    }
    s->continued = true;
    say(log, "wiped out; continued the last save -- %s",
        party_line(s, pl, sizeof pl));
    return true;
}

/* ---- play.continue_game ---- */

#define WIN_GAME_SELECT 0x37

bool continue_game(Session *s, const char *save_file)
{
    const int slot = 1;
    if (save_file == NULL) {
        send2(s, "start new", "wait idle");
    } else {
        char cmd[400];
        snprintf(cmd, sizeof cmd, "start slot %d %s", slot, save_file);
        send2(s, cmd, "wait idle");
        char *text = pl_read_file(save_file, NULL);
        JsonValue *root = (text != NULL) ? json_parse(text, NULL) : NULL;
        free(text);
        const JsonValue *map = json_get(json_get(root, "location"), "map");
        int town = PL_NONE;
        if (map != NULL && map->type == JSON_STRING) {
            town = map_index_name(map->string);
        } else if (map != NULL && map->type == JSON_NUMBER) {
            town = (int)map->number;
        }
        if (town != PL_NONE) {
            s->save_town = town;
        }
        json_free(root);
    }
    for (int i = 0; i < 40; i++) {
        const State *st = &s->state;
        if (st_control(st, "level")) {
            return true;
        }
        if (!st_waiting(st)) {
            send1(s, "wait idle");
        } else if (st_wait(st, "menu") && st->menu == WIN_GAME_SELECT) {
            menu_pick(s, 1);
        } else if (st_wait(st, "menu") && st->menu == WIN_SAVE_SLOTS) {
            menu_pick(s, slot - 1);
        } else if (st_wait(st, "title")) {
            send1(s, "press start");
        } else {
            send1(s, "press c");
        }
    }
    return false;
}

/* ---- playthrough.py ---- */

typedef struct {
    const char *name;
    StageFn     fn;
} Stage;

/* story.STAGES: the ones ported so far, then the rest by name. */
static const Stage STAGES[] = {
    { "paseo", stage_paseo }, { "arima", stage_arima },
    { "gear", stage_gear }, { "shure", stage_shure }, { "nido", stage_nido },
    { "darum", stage_darum }, { "oputa", stage_oputa }, { "amy", stage_amy },
    { "lab_in", stage_lab_in }, { "lab", stage_lab },
    { "lab_b1", stage_lab_b1 }, { "hugh", stage_hugh },
    { "key_tube", stage_key_tube }, { "zema", stage_zema },
    { "roron", stage_roron }, { "uzo", stage_uzo }, { "gum", stage_gum },
    { "climatrol", stage_climatrol }, { "dam_party", stage_dam_party },
    { "cards", stage_cards }, { "dam_grind", stage_dam_grind },
    { "dams", stage_dams }, { "roof", stage_roof },
    { "skure", stage_skure }, { "dezolis_towns", stage_dezolis_towns },
    { "esper", stage_esper }, { "dezolis_grind", stage_dezolis_grind },
    { "motavia", stage_motavia }, { "shir", stage_shir },
    { "menobe", stage_menobe }, { "ikuto", stage_ikuto },
    { "guaron", stage_guaron }, { "naval", stage_naval },
    { "nei_sword", stage_nei_sword }, { "kain", stage_kain },
    { "kain_grind", stage_kain_grind }, { "noah_grind", stage_noah_grind },
    { "boss_party", stage_boss_party }, { "dark_force", stage_dark_force },
    { "mother_brain", stage_mother_brain }
};

#define STAGE_COUNT ((int)(sizeof STAGES / sizeof STAGES[0]))

int stage_index(const char *name)
{
    for (int i = 0; i < STAGE_COUNT; i++) {
        if (streq(STAGES[i].name, name)) {
            return i;
        }
    }
    return -1;
}

const char *stage_name(int i)
{
    return STAGES[i].name;
}

int stage_count(void)
{
    return STAGE_COUNT;
}

StageFn stage_fn(int i)
{
    return STAGES[i].fn;
}

static struct {
    int         seed;
    const char *to;
    int         tries;
    bool        no_lean, no_squeak, no_kain;
    const char *pace;
    const char *fail_once;
    const char *language;
    char        base[160];
} g_args;

bool raise_dead(Session *s, Report *log)
{
    char pl[512];
    if (!party_dead(s)) {
        return true;
    }
    int level = s->state.level;
    TownBuilding b;
    if (st_screen(&s->state, "level") && level != PL_NONE && level < 0x10
        && town_building(level, BUILDING_CLONE_LAB, &b)) {
        return revive_party(s, level, log);
    }
    revive(s);
    if (party_dead(s)) {
        retreat(s, PL_NONE, log);
    }
    bool ok = !party_dead(s);
    say(log, "raise_dead: %s -- %s", ok ? "all alive" : "still dead",
        party_line(s, pl, sizeof pl));
    return ok;
}

/* resume_point: mid_<name>.json when this try's stage made one, else the
 * checkpoint before it (NULL for none). *mid: it was the mid save. */
const char *resume_point(const char *name, const char *cp, char *buf,
                         size_t n, bool *mid)
{
    snprintf(buf, n, "%s/mid_%s.json", pl_saves, name);
    *mid = pl_file_exists(buf);
    return *mid ? buf : cp;
}

void stage_begins(Session *s, const char *name, int attempt,
                  const char *resumed)
{
    char p[300];
    if (attempt == 0 && resumed == NULL) {
        snprintf(p, sizeof p, "%s/mid_%s.json", pl_saves, name);
        remove(p);
        snprintf(p, sizeof p, "%s/mid_%s.note.json", pl_saves, name);
        remove(p);
    }
    snprintf(s->stage, sizeof s->stage, "%s", name);
    trip_marks(s, &s->save_marks);
    s->looted.n = 0;
    s->continued = false;
    s->story_point = false;
    s->progress.n = 0;
    if (resumed != NULL) {
        snprintf(p, sizeof p, "%.*s.note.json",
                 (int)(strlen(resumed) - strlen(".json")), resumed);
        char *text = pl_read_file(p, NULL);
        JsonValue *root = (text != NULL) ? json_parse(text, NULL) : NULL;
        free(text);
        const JsonValue *pr = json_get(root, "progress");
        for (int i = 0; i < json_length(pr); i++) {
            const JsonValue *v = json_at(pr, i);
            if (v->type == JSON_STRING) {
                lines_add(&s->progress, v->string);
            }
        }
        json_free(root);
    }
}

void party_levels(const Session *s, char *out, size_t n)
{
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < s->state.party_n && used < n; i++) {
        used += (size_t)snprintf(out + used, n - used, "%s%s L%d",
                                 i ? " " : "", s->state.party[i].name,
                                 s->state.party[i].level);
    }
}

/* story.revival_how: where a member came back -- the clone lab, another
 * building, in a fight or on the map after the last two scripts. */
static void revival_how(const DownRec *r, char *out, size_t n)
{
    Ints tail = { 0 };
    for (int i = (r->script_n > 2) ? r->script_n - 2 : 0; i < r->script_n;
         i++) {
        ints_add(&tail, r->scripts[i]);
    }
    char t[256];
    scripts_text(&tail, t, sizeof t);
    ints_free(&tail);
    if (streq(r->screen, "building")) {
        if (r->building == 3) {
            snprintf(out, n, "clone lab");
        } else if (r->building == PL_NONE) {
            snprintf(out, n, "building None");
        } else {
            snprintf(out, n, "building %d", r->building);
        }
    } else if (streq(r->screen, "battle")) {
        snprintf(out, n, "in a fight after %s", t);
    } else {
        snprintf(out, n, "on the map after %s", t);
    }
}

/* story.revival_tally, by count, most first (a stable sort of the
 * dict's order). */
static void revival_tally(const Session *s, Lines *rep)
{
    Lines how = { 0 };
    Ints count = { 0 };
    for (int i = 0; i < s->revivals_n; i++) {
        char k[320];
        revival_how(&s->revivals[i], k, sizeof k);
        int at = -1;
        for (int j = 0; j < how.n; j++) {
            if (streq(how.v[j], k)) {
                at = j;
            }
        }
        if (at < 0) {
            lines_add(&how, k);
            ints_add(&count, 0);
            at = how.n - 1;
        }
        count.v[at]++;
    }
    int *order = pl_alloc((size_t)(how.n + 1) * sizeof *order);
    for (int i = 0; i < how.n; i++) {
        int t = i, j = i - 1;
        while (j >= 0 && count.v[order[j]] < count.v[t]) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = t;
    }
    for (int i = 0; i < how.n; i++) {
        lines_addf(rep, "   %d x %s", count.v[order[i]], how.v[order[i]]);
    }
    free(order);
    lines_free(&how);
    ints_free(&count);
}

void tick_totals(const Session *s, char *out, size_t n)
{
    long fight = 0, vic = 0;
    int nv = 0;
    for (int i = 0; i < s->fights_n; i++) {
        fight += s->fights[i].ticks;
        if (s->fights[i].victory_ticks != PL_NONE) {
            vic += s->fights[i].victory_ticks;
            nv++;
        }
    }
    size_t used = (size_t)snprintf(out, n, "ticks %ld; in fights %ld",
                                   s->ticks, fight);
    if (s->fights_n && used < n) {
        used += (size_t)snprintf(out + used, n - used, " (%.0f a fight)",
                                 nearbyint((double)fight / s->fights_n));
    }
    if (used < n) {
        used += (size_t)snprintf(out + used, n - used, "; victory pages %ld",
                                 vic);
    }
    if (nv && used < n) {
        used += (size_t)snprintf(out + used, n - used, " (%.0f a win)",
                                 nearbyint((double)vic / nv));
    }
    if (used < n) {
        snprintf(out + used, n - used,
                 "; paused %ld (%ld in fights); pace %s", s->jitter_ticks,
                 s->jitter_fight_ticks, s->pace);
    }
}

static void usage(void)
{
    fprintf(stderr,
            "usage: route_probe --play [--seed N] [--to STAGE] [--tries N]\n"
            "                   [--no-lean] [--squeak | --no-squeak]\n"
            "                   [--no-kain] [--pace probe|watch]\n"
            "                   [--fail-once STAGE] [--language KEY]\n"
            "                   [--base NAME]\n"
            "  tools/playthrough.py's game in C: route_out/<base>/probe/\n"
            "  is the probe's own output, <base> playthrough_s<N>[_KEY]\n");
}

bool player_cli_parse(int argc, char **argv, char *out, size_t n)
{
    g_args.seed = 1;
    g_args.to = "mother_brain";
    g_args.tries = -1;
    g_args.pace = "probe";
    g_args.base[0] = '\0';
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (streq(a, "--seed") && more) {
            g_args.seed = atoi(argv[++i]);
        } else if (streq(a, "--to") && more) {
            g_args.to = argv[++i];
        } else if (streq(a, "--tries") && more) {
            g_args.tries = atoi(argv[++i]);
        } else if (streq(a, "--no-lean")) {
            g_args.no_lean = true;
        } else if (streq(a, "--squeak")) {
            g_args.no_squeak = false;
        } else if (streq(a, "--no-squeak")) {
            g_args.no_squeak = true;
        } else if (streq(a, "--no-kain")) {
            g_args.no_kain = true;
        } else if (streq(a, "--pace") && more) {
            g_args.pace = argv[++i];
        } else if (streq(a, "--fail-once") && more) {
            g_args.fail_once = argv[++i];
        } else if (streq(a, "--language") && more) {
            g_args.language = argv[++i];
        } else if (streq(a, "--base") && more) {
            snprintf(g_args.base, sizeof g_args.base, "%s", argv[++i]);
        } else {
            usage();
            return false;
        }
    }
    if (stage_index(g_args.to) < 0) {
        fprintf(stderr, "route_probe --play: no stage %s\n", g_args.to);
        return false;
    }
    if (!streq(g_args.pace, "probe") && !streq(g_args.pace, "watch")) {
        usage();
        return false;
    }
    if (g_args.base[0] == '\0') {
        bool other = g_args.language != NULL
                  && !streq(g_args.language, "us");
        snprintf(g_args.base, sizeof g_args.base, "playthrough_s%d%s%s",
                 g_args.seed, other ? "_" : "", other ? g_args.language : "");
    }
    snprintf(out, n, "route_out/%s/probe", g_args.base);
    return true;
}

/* A try's own state put back after a harness error jumped out of it: the
 * Python's `finally` blocks on the way out. (build 179) COMPUTER PLAYS's
 * loop too. */
void stage_unwind(Session *s)
{
    s->route_depth = 0;
    if (s->grind_depth > 0) {
        s->grind_depth = 1;
        in_grind_end(s);
    }
    if (s->buying_gear) {
        s->buying_gear = false;
        host_line(s, "@gear off");
    }
    /* (build 173) the stages' `finally: s.careful = False`, and the
     * travels' restore of what it was before them (False at a stage's
     * top) */
    s->careful = false;
    /* (build 174) escape_gaira's `finally: s.fight_opts = None` */
    s->fight_opts_set = false;
    /* (build 178) the boss walks' `finally: s.walk_mend = None` */
    s->walk_mend = -1.0;
}

int player_cli_run(Session *s)
{
    char pl[512], buf[512], cpbuf[300], rpbuf[300], ln[600];
    Report log;
    memset(&log, 0, sizeof log);
    log.t0 = pl_now();
    if (!g_args.no_squeak) {
        squeak();
    }
    if (g_args.no_kain) {
        no_kain();
    }
    int tries = (g_args.tries >= 0) ? g_args.tries : (pl_squeak ? 6 : 4);
    snprintf(buf, sizeof buf, "route_out/%s", g_args.base);
    pl_make_dirs(buf);
    snprintf(pl_saves, sizeof pl_saves, "route_out/%s/checkpoints",
             g_args.base);
    pl_lean = !g_args.no_lean;
    snprintf(pl_mid_saves, sizeof pl_mid_saves, "%s", pl_saves);

    set_pace(s, g_args.pace);
    if (g_args.language != NULL) {
        sendf(s, "language %s", g_args.language);
    }
    int first = 0;
    const char *volatile cp = NULL;
    volatile int last = stage_index(g_args.to);
    static Lines rows, failed, errors;
    volatile int status = 0;
    static jmp_buf trap;
    static double try_t0, try_secs;     /* t0 and secs, kept over a jump */
    for (volatile int si = first; si <= last; si++) {
        const char *name = STAGES[si].name;
        volatile bool done = false;
        const char *volatile rp = NULL;
        volatile bool rp_mid = false;
        for (volatile int attempt = 0; attempt < tries; attempt++) {
            snprintf(buf, sizeof buf, "%d/%s/%d", g_args.seed, name, attempt);
            jitter_init(&s->jitter, buf);
            bool ok = false;
            pl_trap = &trap;
            if (setjmp(trap) != 0) {
                stage_unwind(s);
                say(&log, "%s: harness error %s", name, pl_error_text);
                lines_addf(&errors, "%s try %d: %s", name, attempt + 1,
                           pl_error_text);
                ok = false;
                try_secs = pl_now() - try_t0;
                goto tried;
            }
            if (attempt > 0) {
                bool mid = false;
                rp = resume_point(name, cp, rpbuf, sizeof rpbuf, &mid);
                rp_mid = mid;
                if (rp == NULL) {
                    /* paseo starts a new game */
                } else if (!continue_game(s, rp)) {
                    say(&log, "%s: could not continue %s", name, rp);
                    break;
                } else if (rp_mid) {
                    say(&log, "%s: resumed from its last save on the way, "
                        "%s -- %s", name, rp, party_line(s, pl, sizeof pl));
                }
                s->careful = s->hurt = false;
                raise_dead(s, &log);
                if (pl_squeak && top_up(s, name, &log, attempt)) {
                    if (wiped(&s->state) && rp != NULL) {
                        continue_game(s, rp);
                    }
                    raise_dead(s, &log);
                }
            }
            stage_begins(s, name, attempt,
                         (attempt > 0 && rp_mid) ? rp : NULL);
            say(&log, "--- %s (try %d) ---", name, attempt + 1);
            try_t0 = pl_now();
            int f0 = s->fights_n;
            long k0 = s->ticks, j0 = s->jitter_ticks;
            if (STAGES[si].fn == NULL) {
                pl_error("NotPorted: stage %s", name);
            }
            ok = STAGES[si].fn(s, &log);
            if (ok && attempt == 0 && g_args.fail_once != NULL
                && streq(name, g_args.fail_once)) {
                say(&log, "%s: --fail-once: this try counted as not "
                    "finished", name);
                ok = false;
            }
            try_secs = pl_now() - try_t0;
            if (ok && si != STAGE_COUNT - 1) {
                raise_dead(s, &log);
                char want[300];
                snprintf(want, sizeof want, "%s/story_%s.json", pl_saves,
                         name);
                checkpoint(s, name, &log);
                if (party_dead(s)) {
                    say(&log, "%s: the walk to the save left the dead", name);
                    if (raise_dead(s, &log)) {
                        checkpoint(s, name, &log);
                    }
                }
                double mt = pl_mtime(want);
                if (mt >= 0 && mt >= try_t0) {
                    snprintf(cpbuf, sizeof cpbuf, "%s", want);
                    cp = cpbuf;
                } else {
                    say(&log, "%s: finished, but no checkpoint written", name);
                    ok = false;
                }
            }
            if (ok) {
                snprintf(buf, sizeof buf, "%s/mid_%s.json", pl_saves, name);
                remove(buf);
                snprintf(buf, sizeof buf, "%s/mid_%s.note.json", pl_saves,
                         name);
                remove(buf);
                party_levels(s, pl, sizeof pl);
                lines_addf(&rows, "| %s | %d | %.1f | %d | %ld | %ld | %s |",
                           name, attempt + 1, try_secs,
                           s->fights_n - f0, s->ticks - k0,
                           s->jitter_ticks - j0, pl);
                done = true;
                pl_trap = NULL;
                break;
            }
        tried:
            pl_trap = NULL;
            sendf(s, "shot fail_%s_%d", name, attempt + 1);
            lines_addf(&failed, "%s try %d (%.1f s): %s -- %s", name,
                       attempt + 1, try_secs, s->state.screen,
                       party_line(s, pl, sizeof pl));
            say(&log, "%s: try %d did not finish -- %s %s", name, attempt + 1,
                s->state.screen, party_line(s, pl, sizeof pl));
        }
        pl_trap = NULL;
        if (!done) {
            say(&log, "stage %s did not finish in %d tries", name, tries);
            status = 1;
            break;
        }
    }

    /* the report */
    Lines rep = { 0 };
    snprintf(ln, sizeof ln, "playthrough seed %d from a new game, lean %s%s%s%s%s"
             ", %.0f s", g_args.seed, pl_lean ? "on" : "off",
             pl_squeak ? ", squeak-by" : "", pl_no_kain ? ", no Kain" : "",
             g_args.language ? ", language " : "",
             g_args.language ? g_args.language : "", pl_now() - log.t0);
    lines_add(&rep, ln);
    lines_add(&rep, "");
    lines_add(&rep, "| stage | try | s | fights | ticks | paused ticks "
                    "| party after |");
    lines_add(&rep, "|---|---|---|---|---|---|---|");
    lines_extend(&rep, &rows);
    lines_add(&rep, "");
    lines_addf(&rep, "fights %d; paused ticks %ld; members down %d, brought "
               "back %d", s->fights_n, s->jitter_ticks, s->downs_n,
               s->revivals_n);
    tick_totals(s, buf, sizeof buf);
    lines_add(&rep, buf);
    revival_tally(s, &rep);
    lines_addf(&rep, "tries that did not finish: %d", failed.n);
    for (int i = 0; i < failed.n; i++) {
        lines_addf(&rep, "   %s", failed.v[i]);
    }
    lines_addf(&rep, "harness errors: %d", errors.n);
    for (int i = 0; i < errors.n; i++) {
        lines_addf(&rep, "   %s", errors.v[i]);
    }
    lines_add(&rep, "probe stalls or deaths: 0");
    int lean = 0;
    for (int i = 0; i < log.lines.n; i++) {
        lean += starts(log.lines.v[i], "lean:");
    }
    lines_addf(&rep, "lean go-ons: %d", lean);
    for (int i = 0; i < log.lines.n; i++) {
        if (starts(log.lines.v[i], "lean:")) {
            lines_addf(&rep, "   %s", log.lines.v[i]);
        }
    }
    lines_addf(&rep, "port stderr (distinct): %d", s->stderr_lines.n);
    lines_addf(&rep, "finished: %s", (status == 0 && last == STAGE_COUNT - 1)
                                     ? "the ending"
                                     : (status == 0) ? "through" : "no");
    if (status == 0 && last != STAGE_COUNT - 1) {
        char *f = rep.v[rep.n - 1];
        snprintf(buf, sizeof buf, "%s %s", f, g_args.to);
        free(f);
        rep.v[rep.n - 1] = pl_strdup(buf);
    }
    size_t total = 1;
    for (int i = 0; i < rep.n; i++) {
        total += strlen(rep.v[i]) + 1;
    }
    char *text = pl_alloc(total);
    for (int i = 0; i < rep.n; i++) {
        strcat(text, rep.v[i]);
        strcat(text, "\n");
    }
    snprintf(buf, sizeof buf, "route_out/%s/report.txt", g_args.base);
    pl_write_text(buf, text);
    free(text);
    total = 1;
    for (int i = 0; i < log.lines.n; i++) {
        total += strlen(log.lines.v[i]) + 1;
    }
    text = pl_alloc(total);
    for (int i = 0; i < log.lines.n; i++) {
        strcat(text, log.lines.v[i]);
        strcat(text, "\n");
    }
    snprintf(buf, sizeof buf, "route_out/%s/story.txt", g_args.base);
    pl_write_text(buf, text);
    free(text);
    printf("%s\n%s\n", rep.v[0], rep.v[1]);
    lines_free(&rep);
    lines_free(&rows);
    lines_free(&failed);
    lines_free(&errors);
    lines_free(&log.lines);
    return status;
}
