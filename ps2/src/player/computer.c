/* (build 179) tools/computer_plays.py's main in C: COMPUTER PLAYS's own
 * loop, the one the window plays (docs/computer_plays.md, docs/
 * c_player.md "Step 9"). It is playthrough's loop (run.c) less what a
 * player could not do: no `save-here` (a stage that ends in the Skure
 * field leaves no checkpoint, and a retry of the next plays it again
 * first), a retry's CONTINUE the game's own (the host's `start slot`), and
 * it starts from a new game, a player's save (its stage told from the
 * story's marks in it) or the computer's last run (last.json). The report
 * and the story's log go to --out, the game's saves to --out/slots. */

#include "pl.h"

#include "../core/json.h"

#include <stdlib.h>
#include <time.h>

/* DezolisSkure: the field the roof's spaceport opens on. It has no Data
 * Memory, and a player saves there only with the Visiphone. */
#define SKURE_FIELD 3
#define REFS        "routes/saves"
#define SQUEAK_TRIES 6

/* NO_PICKUP: stages no player's save can start, each begun exactly where
 * the stage before it ended, in the Skure field: played on from that one. */
static const char *const NO_PICKUP[][2] = {
    { "skure", "roof" }, { "menobe", "shir" }
};

/* STORY_ITEMS, things the story hands over once and the party keeps, as
 * the Python spells them. (build 180) The helmet is items.json's
 * "Neimet": spelt "NeiMet" until 179 (the Python's), it marked no save. */
static const char *const STORY_ITEMS[] = {
    "Prism", "NeiCrown", "Neimet", "NeiArmor", "NeiCape", "NeiShield",
    "NeiEmel", "NeiSlasher", "NeiShot", "NeiSword"
};

/* The recruits in their order of joining (gamedata.c, write_recruit). */
static const char *const RECRUITS[] = {
    "none", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"
};

#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

static struct {
    const char *start;
    char        out[224];       /* room for /checkpoints in pl_saves */
    int         seed;
    bool        seed_set;
    const char *first;          /* NULL: None */
    const char *to;
    int         tries;          /* -1: None */
    bool        no_squeak, no_swap, classify;
    const char *pace;
    const char *fail_once;
} g_c;

void player_set_echo(FILE *f)
{
    pl_echo = f;
}

static FILE *echo(void)
{
    return (pl_echo != NULL) ? pl_echo : stdout;
}

/* ---- the arguments ---- */

static void usage(void)
{
    fprintf(echo(),
            "usage: COMPUTER PLAYS [--start new|last|SAVE] [--out DIR]\n"
            "                      [--seed N] [--first STAGE] [--to STAGE]\n"
            "                      [--tries N] [--squeak | --no-squeak]\n"
            "                      [--no-swap] [--pace probe|watch]\n"
            "                      [--fail-once STAGE] [--classify]\n"
            "  tools/computer_plays.py's game in C (less --attach and "
            "--home)\n");
}

bool player_computer_parse(int argc, char **argv)
{
    memset(&g_c, 0, sizeof g_c);
    g_c.start = "new";
    snprintf(g_c.out, sizeof g_c.out, "saves/computer");
    g_c.to = stage_name(stage_count() - 1);
    g_c.tries = -1;
    g_c.pace = "watch";
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (streq(a, "--start") && more) {
            g_c.start = argv[++i];
        } else if (streq(a, "--out") && more) {
            snprintf(g_c.out, sizeof g_c.out, "%s", argv[++i]);
        } else if (streq(a, "--seed") && more) {
            g_c.seed = atoi(argv[++i]);
            g_c.seed_set = true;
        } else if (streq(a, "--first") && more) {
            g_c.first = argv[++i];
        } else if (streq(a, "--to") && more) {
            g_c.to = argv[++i];
        } else if (streq(a, "--tries") && more) {
            g_c.tries = atoi(argv[++i]);
        } else if (streq(a, "--squeak")) {
            g_c.no_squeak = false;
        } else if (streq(a, "--no-squeak")) {
            g_c.no_squeak = true;
        } else if (streq(a, "--no-swap")) {
            g_c.no_swap = true;
        } else if (streq(a, "--pace") && more) {
            g_c.pace = argv[++i];
        } else if (streq(a, "--fail-once") && more) {
            g_c.fail_once = argv[++i];
        } else if (streq(a, "--classify")) {
            g_c.classify = true;
        } else {
            usage();
            return false;
        }
    }
    if (g_c.first != NULL && stage_index(g_c.first) < 0) {
        fprintf(echo(), "computer plays: no stage %s\n", g_c.first);
        return false;
    }
    if (stage_index(g_c.to) < 0) {
        fprintf(echo(), "computer plays: no stage %s\n", g_c.to);
        return false;
    }
    if (!streq(g_c.pace, "probe") && !streq(g_c.pace, "watch")) {
        usage();
        return false;
    }
    return true;
}

const char *player_computer_out(void)
{
    return g_c.out;
}

const char *player_computer_start(void)
{
    return g_c.start;
}

bool player_computer_classify(void)
{
    return g_c.classify;
}

/* ---- a save's stage: stage_of_save ---- */

/* A set of marks, as story_flags' Python set: a flag byte's address as
 * "#<n>" (never one of the other marks), "recruit N", "item NAME". */
static bool set_has(const Lines *set, const char *m)
{
    for (int i = 0; i < set->n; i++) {
        if (streq(set->v[i], m)) {
            return true;
        }
    }
    return false;
}

static void set_add(Lines *set, const char *m)
{
    if (!set_has(set, m)) {
        lines_add(set, m);
    }
}

static bool subset(const Lines *a, const Lines *b)
{
    for (int i = 0; i < a->n; i++) {
        if (!set_has(b, a->v[i])) {
            return false;
        }
    }
    return true;
}

/* Python's truth of a JSON value. */
static bool truthy(const JsonValue *v)
{
    if (v == NULL) {
        return false;
    }
    switch (v->type) {
        case JSON_BOOL: {
            return v->boolean;
        }
        case JSON_NUMBER: {
            return v->number != 0.0;
        }
        case JSON_STRING: {
            return v->string[0] != '\0';
        }
        case JSON_ARRAY: {
            return v->child != NULL;
        }
        case JSON_OBJECT: {
            return v->child != NULL;
        }
        default: {
            return false;
        }
    }
}

/* recruits_joined: the save's last_recruit_joined by name (build 106), or
 * the count older saves kept as recruits_joined. */
static int recruits_joined(const JsonValue *save)
{
    const JsonValue *party = json_get(save, "party");
    if (!truthy(party) || party->type != JSON_OBJECT) {
        return 0;
    }
    const JsonValue *v = json_get(party, "last_recruit_joined");
    if (v == NULL) {
        v = json_get(party, "recruits_joined");
    }
    if (!truthy(v)) {
        return 0;
    }
    if (v->type == JSON_STRING) {
        for (int i = 0; i < COUNT(RECRUITS); i++) {
            if (streq(RECRUITS[i], v->string)) {
                return i;
            }
        }
        return 0;
    }
    if (v->type == JSON_NUMBER) {
        return (int)v->number;
    }
    return v->type == JSON_BOOL && v->boolean;
}

/* `"NAME` in json.dumps(...)'s text: a string (a key or a value) that
 * begins with NAME, or holds a quote and NAME after it (dumped as \"). */
static bool text_names(const char *text, const char *name)
{
    if (starts(text, name)) {
        return true;
    }
    for (const char *q = strchr(text, '"'); q != NULL; q = strchr(q + 1, '"')) {
        if (starts(q + 1, name)) {
            return true;
        }
    }
    return false;
}

static bool dump_names(const JsonValue *v, const char *name)
{
    for (const JsonValue *c = v->child; c != NULL; c = c->next) {
        if (c->key != NULL && text_names(c->key, name)) {
            return true;
        }
        if (c->type == JSON_STRING && text_names(c->string, name)) {
            return true;
        }
        if (dump_names(c, name)) {
            return true;
        }
    }
    return false;
}

/* story_flags: what a save has played, as marks every game of the story
 * shares and none loses -- the story's flag bytes set ($C700-$C77F), how
 * many have joined, the story's kept items. Both save formats: an object
 * names each byte (the key's c7XX), an array is rows of hex. */
static void story_flags(const char *path, Lines *out)
{
    static const struct {
        const char *key;
        int         base;
    } BLOCKS[] = { { "flags_c700", 0xC700 }, { "event_flags", 0xC710 } };

    char *text = pl_read_file(path, NULL);
    if (text == NULL) {
        pl_error("FileNotFoundError: %s", path);
    }
    JsonValue *save = json_parse(text, NULL);
    free(text);
    if (save == NULL) {
        pl_error("JSONDecodeError: %s", path);
    }
    char m[64];
    for (int b = 0; b < COUNT(BLOCKS); b++) {
        const JsonValue *block = json_get(save, BLOCKS[b].key);
        if (block != NULL && block->type == JSON_OBJECT) {
            for (const JsonValue *e = block->child; e != NULL; e = e->next) {
                if (truthy(e) && e->key != NULL && strlen(e->key) >= 4) {
                    char hex[4] = { e->key[1], e->key[2], e->key[3], '\0' };
                    snprintf(m, sizeof m, "#%ld",
                             strtol(hex, NULL, 16) | 0xC000);
                    set_add(out, m);
                }
            }
        } else if (block != NULL && block->type == JSON_ARRAY) {
            int at = 0;
            int hi = -1;
            for (const JsonValue *row = block->child; row != NULL;
                 row = row->next) {
                if (row->type != JSON_STRING) {
                    continue;
                }
                for (const char *p = row->string; *p != '\0'; p++) {
                    int d = (*p >= '0' && *p <= '9') ? *p - '0'
                          : (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10
                          : (*p >= 'A' && *p <= 'F') ? *p - 'A' + 10 : -1;
                    if (d < 0) {
                        continue;
                    }
                    if (hi < 0) {
                        hi = d;
                        continue;
                    }
                    if (hi * 16 + d != 0) {
                        snprintf(m, sizeof m, "#%d", BLOCKS[b].base + at);
                        set_add(out, m);
                    }
                    at++;
                    hi = -1;
                }
            }
        }
    }
    int joined = recruits_joined(save);
    for (int i = 0; i < joined; i++) {
        snprintf(m, sizeof m, "recruit %d", i + 1);
        set_add(out, m);
    }
    const JsonValue *chars = json_get(save, "characters");
    if (truthy(chars)) {
        for (int i = 0; i < COUNT(STORY_ITEMS); i++) {
            bool own = chars->type == JSON_STRING
                     ? text_names(chars->string, STORY_ITEMS[i])
                     : dump_names(chars, STORY_ITEMS[i]);
            if (own) {
                snprintf(m, sizeof m, "item %s", STORY_ITEMS[i]);
                set_add(out, m);
            }
        }
    }
    json_free(save);
}

/* stage_of_save: the stage to play first (NULL: none, and *why says so),
 * and the earlier stages whose own marks the save lacks. Each stage's own
 * marks are what its checkpoint in the story's own chain (routes/saves/
 * story_<stage>.json) has and the one before lacks; the save has played
 * every stage up to the last whose own marks it all has, so a save at the
 * end of a stage that marks nothing plays that stage again. */
static const char *stage_of_save(const char *path, char *why, size_t wn,
                                 Lines *unmet)
{
    Lines have = { 0 };
    story_flags(path, &have);
    int n = stage_count();
    Lines *sig = pl_alloc((size_t)n * sizeof *sig);
    bool *none = pl_alloc((size_t)n * sizeof *none);
    Lines prev = { 0 };
    for (int i = 0; i < n; i++) {
        char ref[300];
        snprintf(ref, sizeof ref, REFS "/story_%s.json", stage_name(i));
        char *probe = pl_read_file(ref, NULL);
        if (probe == NULL) {
            none[i] = true;
            continue;
        }
        free(probe);
        Lines cur = { 0 };
        story_flags(ref, &cur);
        for (int k = 0; k < cur.n; k++) {
            if (!set_has(&prev, cur.v[k])) {
                lines_add(&sig[i], cur.v[k]);
            }
        }
        lines_free(&prev);
        prev = cur;
    }
    lines_free(&prev);
    int last = -1;
    for (int i = 0; i < n; i++) {
        if (!none[i] && sig[i].n > 0 && subset(&sig[i], &have)) {
            last = i;
        }
    }
    for (int i = 0; i <= last; i++) {
        if (!none[i] && sig[i].n > 0 && !subset(&sig[i], &have)) {
            lines_add(unmet, stage_name(i));
        }
    }
    for (int i = 0; i < n; i++) {
        lines_free(&sig[i]);
    }
    free(sig);
    free(none);
    lines_free(&have);
    if (last + 1 >= n) {
        snprintf(why, wn, "the story is played out in this save");
        return NULL;
    }
    const char *pick = stage_name(last + 1);
    for (int i = 0; i < COUNT(NO_PICKUP); i++) {
        if (streq(pick, NO_PICKUP[i][0])) {
            pick = NO_PICKUP[i][1];
        }
    }
    return pick;
}

static void join(const Lines *l, const char *sep, char *out, size_t n)
{
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < l->n && used < n; i++) {
        used += (size_t)snprintf(out + used, n - used, "%s%s", i ? sep : "",
                                 l->v[i]);
    }
}

/* ---- the run ---- */

static Report g_log;
static Lines  g_rows, g_failed, g_errors, g_pending;
static char   g_status[600];
static char   g_start_line[1200];
static int    g_seed;
static int    g_code;
static char   g_cp[400];            /* the checkpoint: "" for None */
static jmp_buf *g_back;             /* player_hand_back's */

_Noreturn void player_hand_back(void)
{
    if (g_back == NULL) {
        fprintf(stderr, "computer plays: handed back with no run\n");
        abort();
    }
    longjmp(*g_back, 1);
}

/* A JSON string as json.dumps writes it (ensure_ascii): \uXXXX past
 * ASCII, from the UTF-8. */
static size_t dump_string(char *out, size_t n, const char *s)
{
    size_t used = (size_t)snprintf(out, n, "\"");
    const unsigned char *p = (const unsigned char *)s;
    while (*p != '\0' && used < n) {
        unsigned c = *p++;
        int more = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
        if (c >= 0x80) {
            c &= (more == 3) ? 0x07 : (more == 2) ? 0x0F : 0x1F;
            for (int k = 0; k < more && (*p & 0xC0) == 0x80; k++) {
                c = (c << 6) | (*p++ & 0x3F);
            }
        }
        if (c == '"' || c == '\\') {
            used += (size_t)snprintf(out + used, n - used, "\\%c", (int)c);
        } else if (c == '\n') {
            used += (size_t)snprintf(out + used, n - used, "\\n");
        } else if (c == '\r') {
            used += (size_t)snprintf(out + used, n - used, "\\r");
        } else if (c == '\t') {
            used += (size_t)snprintf(out + used, n - used, "\\t");
        } else if (c == '\b') {
            used += (size_t)snprintf(out + used, n - used, "\\b");
        } else if (c == '\f') {
            used += (size_t)snprintf(out + used, n - used, "\\f");
        } else if (c < 0x20 || (c >= 0x80 && c < 0x10000)) {
            used += (size_t)snprintf(out + used, n - used, "\\u%04x", c);
        } else if (c >= 0x10000) {
            c -= 0x10000;
            used += (size_t)snprintf(out + used, n - used, "\\u%04x\\u%04x",
                                     0xD800 + (c >> 10), 0xDC00 + (c & 0x3FF));
        } else {
            used += (size_t)snprintf(out + used, n - used, "%c", (int)c);
        }
    }
    if (used < n) {
        used += (size_t)snprintf(out + used, n - used, "\"");
    }
    return used;
}

/* note_last: (build 103) where a later COMPUTER PLAYS can go on from,
 * OUT/last.json; play.ON_SAVE for the saves on the way. */
static void note_last(const char *kind, const char *stage, const char *path)
{
    char text[1600], a[300], b[100], c[900];
    dump_string(a, sizeof a, stage);
    dump_string(b, sizeof b, kind);
    dump_string(c, sizeof c, path);
    snprintf(text, sizeof text,
             "{\"stage\": %s, \"kind\": %s, \"save\": %s, \"seed\": %d}\n",
             a, b, c, g_seed);
    char p[300];
    snprintf(p, sizeof p, "%s/last.json", g_c.out);
    pl_write_text(p, text);
}

/* CONTINUE the checkpoint (or, --no-swap, the last own save). */
static bool cont(Session *s, const char *cp)
{
    s->careful = s->hurt = false;
    return continue_game(s, g_c.no_swap ? NULL : cp);
}

/* The stages played since the checkpoint that left none, again. */
static bool replay(Session *s, const char *name, int attempt, const char *tag)
{
    char key[300];
    for (int p = 0; p < g_pending.n; p++) {
        const char *pn = g_pending.v[p];
        say(&g_log, "--- %s again, before %s (no checkpoint between) ---", pn,
            name);
        snprintf(key, sizeof key, "%d/%s/%d/%s%s", g_seed, name, attempt, pn,
                 tag);
        jitter_init(&s->jitter, key);
        if (!stage_fn(stage_index(pn))(s, &g_log)) {
            return false;
        }
    }
    return true;
}

static const char *base_name(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            b = p + 1;
        }
    }
    return b;
}

static void write_lines(const char *path, const Lines *l)
{
    size_t total = 1;
    for (int i = 0; i < l->n; i++) {
        total += strlen(l->v[i]) + 1;
    }
    char *text = pl_alloc(total);
    size_t used = 0;
    for (int i = 0; i < l->n; i++) {
        size_t k = strlen(l->v[i]);
        memcpy(text + used, l->v[i], k);
        used += k;
        text[used++] = '\n';
    }
    if (l->n == 0) {
        text[used++] = '\n';
    }
    text[used] = '\0';
    pl_write_text(path, text);
    free(text);
}

/* The report and the story's log, the Python's `finally`. */
static void report(Session *s)
{
    char buf[1600];
    Lines rep = { 0 };
    lines_addf(&rep, "%s, lean on%s, %.0f s", g_start_line,
               pl_squeak ? ", squeak-by" : "", pl_now() - g_log.t0);
    lines_add(&rep, "");
    lines_add(&rep, "| stage | try | s | fights | ticks | party after |");
    lines_add(&rep, "|---|---|---|---|---|---|");
    lines_extend(&rep, &g_rows);
    lines_add(&rep, "");
    lines_addf(&rep, "fights %d; paused ticks %ld", s->fights_n,
               s->jitter_ticks);
    tick_totals(s, buf, sizeof buf);
    lines_add(&rep, buf);
    lines_addf(&rep, "tries that did not finish: %d", g_failed.n);
    for (int i = 0; i < g_failed.n; i++) {
        lines_addf(&rep, "   %s", g_failed.v[i]);
    }
    lines_addf(&rep, "harness errors: %d", g_errors.n);
    for (int i = 0; i < g_errors.n; i++) {
        lines_addf(&rep, "   %s", g_errors.v[i]);
    }
    lines_addf(&rep, "finished: %s", g_status);
    snprintf(buf, sizeof buf, "%s/report.txt", g_c.out);
    write_lines(buf, &rep);
    snprintf(buf, sizeof buf, "%s/story.txt", g_c.out);
    write_lines(buf, &g_log.lines);
    fprintf(echo(), "%s\n%s\n", rep.v[0], rep.v[rep.n - 1]);
    fflush(echo());
    lines_free(&rep);
}

static int classify(void)
{
    static jmp_buf trap;
    char why[200], u[1200];
    Lines unmet = { 0 };
    pl_trap = &trap;
    if (setjmp(trap) != 0) {
        pl_trap = NULL;
        fprintf(echo(), "computer plays: %s\n", pl_error_text);
        lines_free(&unmet);
        return 1;
    }
    const char *pick = stage_of_save(g_c.start, why, sizeof why, &unmet);
    pl_trap = NULL;
    join(&unmet, ", ", u, sizeof u);
    if (pick != NULL) {
        fprintf(echo(), "%s (earlier stages unmet: %s)\n", pick,
                unmet.n ? u : "none");
    } else {
        fprintf(echo(), "none: %s (earlier stages unmet: %s)\n", why,
                unmet.n ? u : "none");
    }
    lines_free(&unmet);
    return 0;
}

int player_computer_run(Session *s)
{
    static jmp_buf back, outer, stage_trap;
    static double try_t0;
    static char rp[400], resumed[400], resumed_first[400];
    static bool rp_set, rp_mid;
    static JsonValue *last_run;
    char pl[512], buf[1200], want[400];

    if (g_c.classify) {
        return classify();
    }
    story_reset();
    if (!g_c.no_squeak) {
        squeak();
    }
    const int tries = (g_c.tries >= 0) ? g_c.tries
                                       : (pl_squeak ? SQUEAK_TRIES : 4);
    snprintf(pl_saves, sizeof pl_saves, "%s/checkpoints", g_c.out);
    pl_make_dirs(pl_saves);
    pl_lean = true;
    snprintf(pl_mid_saves, sizeof pl_mid_saves, "%s", pl_saves);
    last_run = NULL;
    if (streq(g_c.start, "last")) {
        snprintf(buf, sizeof buf, "%s/last.json", g_c.out);
        char *text = pl_read_file(buf, NULL);
        last_run = (text != NULL) ? json_parse(text, NULL) : NULL;
        free(text);
        if (last_run == NULL) {
            fprintf(echo(), "computer plays: no last run to go on from\n");
            return 2;
        }
    }
    g_seed = g_c.seed_set ? g_c.seed
           : (last_run != NULL) ? (int)json_int(last_run, "seed", 0)
           : (int)(time(NULL) % 100000);
    pl_on_save = note_last;

    memset(&g_log, 0, sizeof g_log);
    g_log.t0 = pl_now();
    set_pace(s, g_c.pace);
    snprintf(s->saves_dir, sizeof s->saves_dir, "%s/slots", g_c.out);
    lines_clear(&g_rows);
    lines_clear(&g_failed);
    lines_clear(&g_errors);
    lines_clear(&g_pending);
    snprintf(g_status, sizeof g_status, "no");
    snprintf(g_start_line, sizeof g_start_line, "computer plays seed %d from %s",
             g_seed, g_c.start);
    g_cp[0] = '\0';
    resumed_first[0] = '\0';
    g_code = 0;

    g_back = &back;
    if (setjmp(back) != 0) {
        /* The pad was taken back: the Python's EOFError. */
        snprintf(g_status, sizeof g_status, "handed back");
        g_code = 0;
        goto done;
    }
    pl_trap = &outer;
    if (setjmp(outer) != 0) {
        /* An error outside a stage's own try: the Python's traceback,
         * after its `finally` wrote the report. */
        fprintf(echo(), "computer plays: %s\n", pl_error_text);
        g_code = 1;
        goto done;
    }

    volatile int first = 0;
    if (last_run != NULL) {
        const char *stage = json_str(last_run, "stage", NULL);
        if (stage == NULL || stage_index(stage) < 0) {
            say(&g_log, "last run: stage %s is not one of the story's",
                (stage != NULL) ? stage : "None");
            snprintf(g_status, sizeof g_status,
                     "refused: the last run's stage is unknown");
            g_code = 2;
            goto done;
        }
        const char *save = json_str(last_run, "save", NULL);
        const char *kind = json_str(last_run, "kind", NULL);
        if (save == NULL || kind == NULL) {
            pl_error("KeyError: '%s'", (save == NULL) ? "save" : "kind");
        }
        snprintf(g_cp, sizeof g_cp, "%s/start.json", pl_saves);
        if (!pl_copy_file(save, g_cp)) {
            pl_error("FileNotFoundError: %s", save);
        }
        size_t k = strlen(save);
        snprintf(buf, sizeof buf, "%.*s.note.json",
                 (int)((k > 5) ? k - 5 : 0), save);
        if (streq(kind, "mid") && pl_file_exists(buf)) {
            snprintf(want, sizeof want, "%s/start.note.json", pl_saves);
            pl_copy_file(buf, want);
        }
        first = stage_index(stage) + (streq(kind, "end") ? 1 : 0);
        if (streq(kind, "mid")) {
            snprintf(resumed_first, sizeof resumed_first, "%s", g_cp);
        }
        if (first >= stage_count()) {
            snprintf(g_status, sizeof g_status,
                     "refused: the last run played the story out");
            g_code = 2;
            goto done;
        }
        size_t used = strlen(g_start_line);
        snprintf(g_start_line + used, sizeof g_start_line - used,
                 ", the last run's %s save of %s, at stage %s", kind, stage,
                 stage_name(first));
        if (!cont(s, g_cp)) {
            say(&g_log, "could not continue %s", save);
            snprintf(g_status, sizeof g_status,
                     "no: the save would not continue");
            g_code = 2;
            goto done;
        }
        say(&g_log, "continued the last run (%s of %s) at %s: %s", kind,
            stage, stage_name(first), party_line(s, pl, sizeof pl));
    } else if (!streq(g_c.start, "new")) {
        char why[200], u[1200];
        Lines unmet = { 0 };
        const char *pick = stage_of_save(g_c.start, why, sizeof why, &unmet);
        if (g_c.first != NULL) {
            pick = g_c.first;
        }
        if (pick == NULL) {
            say(&g_log, "%s: %s", g_c.start, why);
            snprintf(g_status, sizeof g_status, "refused: %s", why);
            lines_free(&unmet);
            g_code = 2;
            goto done;
        }
        if (unmet.n > 0) {
            join(&unmet, ", ", u, sizeof u);
            say(&g_log, "%s: played past %s's stages with these stages' "
                "flags not all set: %s", g_c.start, pick, u);
        }
        lines_free(&unmet);
        snprintf(g_cp, sizeof g_cp, "%s/start.json", pl_saves);
        if (!pl_copy_file(g_c.start, g_cp)) {
            pl_error("FileNotFoundError: %s", g_c.start);
        }
        first = stage_index(pick);
        size_t used = strlen(g_start_line);
        snprintf(g_start_line + used, sizeof g_start_line - used,
                 ", at stage %s", pick);
        if (!cont(s, g_cp)) {
            say(&g_log, "could not continue %s", g_c.start);
            snprintf(g_status, sizeof g_status,
                     "no: the save would not continue");
            g_code = 2;
            goto done;
        }
        say(&g_log, "continued %s at %s: %s", g_c.start, pick,
            party_line(s, pl, sizeof pl));
    }

    volatile int last = stage_index(g_c.to);
    for (volatile int si = first; si <= last; si++) {
        const char *name = stage_name(si);
        volatile bool done_stage = false;
        rp_set = false;
        rp_mid = false;
        for (volatile int attempt = 0; attempt < tries; attempt++) {
            snprintf(buf, sizeof buf, "%d/%s/%d", g_seed, name, attempt);
            jitter_init(&s->jitter, buf);
            volatile bool ok = true;
            if (attempt > 0) {
                char mid_buf[400];
                bool mid = false;
                const char *r = resume_point(name, g_cp[0] ? g_cp : NULL,
                                             mid_buf, sizeof mid_buf, &mid);
                rp_set = r != NULL;
                rp_mid = mid;
                snprintf(rp, sizeof rp, "%s", rp_set ? r : "");
                if (rp_set && !cont(s, rp)) {
                    say(&g_log, "%s: could not continue %s", name, rp);
                    break;
                }
                if (rp_mid) {
                    say(&g_log, "%s: resumed from its last save on the way, "
                        "%s -- %s", name, rp, party_line(s, pl, sizeof pl));
                    lines_clear(&g_pending);    /* the save is past them */
                }
                raise_dead(s, &g_log);
                /* (build 180) The lost stages first, then the top-up:
                 * skure's (menobe's) is Zosa's grind on Dezolis, and the
                 * checkpoint CONTINUEd is dams' (motavia's) on Motavia. Run
                 * before the flight, it reached nothing, and every try
                 * played the first again (seeds 7, 10 and 11, build 179).
                 * Now each starts where playthrough's does. */
                ok = replay(s, name, attempt, "");
                if (g_pending.n > 0) {
                    snprintf(buf, sizeof buf, "%d/%s/%d", g_seed, name,
                             attempt);
                    jitter_init(&s->jitter, buf);
                }
                if (ok && pl_squeak && top_up(s, name, &g_log, attempt)) {
                    /* (build 119) As playthrough's: a top-up wiped out goes
                     * back to the save; the dead raised. */
                    if (wiped(&s->state) && rp_set) {
                        cont(s, rp);
                        if (g_pending.n > 0) {
                            raise_dead(s, &g_log);
                            ok = replay(s, name, attempt, "/again");
                        }
                    }
                    raise_dead(s, &g_log);
                }
                snprintf(buf, sizeof buf, "%d/%s/%d", g_seed, name, attempt);
                jitter_init(&s->jitter, buf);
            }
            if (attempt > 0) {
                snprintf(resumed, sizeof resumed, "%s", rp_mid ? rp : "");
            } else {
                snprintf(resumed, sizeof resumed, "%s", resumed_first);
                resumed_first[0] = '\0';
            }
            stage_begins(s, name, attempt, resumed[0] ? resumed : NULL);
            say(&g_log, "--- %s (try %d) ---", name, attempt + 1);
            try_t0 = pl_now();
            volatile int f0 = s->fights_n;
            volatile long k0 = s->ticks;
            pl_trap = &stage_trap;
            if (setjmp(stage_trap) != 0) {
                pl_trap = &outer;
                stage_unwind(s);
                say(&g_log, "%s: harness error %s", name, pl_error_text);
                lines_addf(&g_errors, "%s try %d: %s", name, attempt + 1,
                           pl_error_text);
                ok = false;
            } else {
                ok = ok && stage_fn(si)(s, &g_log);
                if (ok && attempt == 0 && g_c.fail_once != NULL
                    && streq(name, g_c.fail_once)) {
                    say(&g_log, "%s: --fail-once: this try counted as not "
                        "finished", name);
                    ok = false;
                }
            }
            pl_trap = &outer;
            if (ok && si != stage_count() - 1) {
                raise_dead(s, &g_log);
                if (st_screen(&s->state, "level")
                    && s->state.level == SKURE_FIELD) {
                    say(&g_log, "%s: played; no player's save in the Skure "
                        "field, so no checkpoint -- a retry of the next "
                        "stage plays this one again", name);
                    lines_add(&g_pending, name);
                } else {
                    snprintf(want, sizeof want, "%s/story_%s.json", pl_saves,
                             name);
                    checkpoint(s, name, &g_log);
                    double mt = pl_mtime(want);
                    if (mt >= 0 && mt >= try_t0) {
                        snprintf(g_cp, sizeof g_cp, "%s", want);
                        lines_clear(&g_pending);
                        note_last("end", name, want);
                    } else {
                        say(&g_log, "%s: finished, but no checkpoint "
                            "written", name);
                        ok = false;
                    }
                }
            }
            if (ok) {
                snprintf(buf, sizeof buf, "%s/mid_%s.json", pl_saves, name);
                remove(buf);
                snprintf(buf, sizeof buf, "%s/mid_%s.note.json", pl_saves,
                         name);
                remove(buf);
                party_levels(s, pl, sizeof pl);
                lines_addf(&g_rows, "| %s | %d | %.1f | %d | %ld | %s |",
                           name, attempt + 1, pl_now() - try_t0,
                           s->fights_n - f0, s->ticks - k0, pl);
                done_stage = true;
                break;
            }
            lines_addf(&g_failed, "%s try %d: %s -- %s", name, attempt + 1,
                       s->state.screen[0] ? s->state.screen : "None",
                       party_line(s, pl, sizeof pl));
            say(&g_log, "%s: try %d did not finish", name, attempt + 1);
        }
        if (!done_stage) {
            snprintf(g_status, sizeof g_status,
                     "no: %s did not finish in %d tries", name, tries);
            /* (build 123) the owner: out of tries, the pad goes back in
             * the game, never wherever the last try left it: the last
             * save CONTINUEd first -- the stage's save on the way, else
             * the checkpoint before it. */
            char back_buf[400];
            bool mid = false;
            const char *bk = resume_point(name, g_cp[0] ? g_cp : NULL,
                                          back_buf, sizeof back_buf, &mid);
            if (bk != NULL) {
                snprintf(rp, sizeof rp, "%s", bk);
                if (cont(s, rp)) {
                    say(&g_log, "%s: out of tries; continued %s for the pad "
                        "-- %s", name, rp, party_line(s, pl, sizeof pl));
                    size_t used = strlen(g_status);
                    snprintf(g_status + used, sizeof g_status - used,
                             "; the pad back at %s", base_name(rp));
                }
            }
            g_code = 1;
            goto done;
        }
    }
    if (last == stage_count() - 1) {
        snprintf(g_status, sizeof g_status, "the ending");
    } else {
        snprintf(g_status, sizeof g_status, "through %s", g_c.to);
    }
    g_code = 0;

done:
    g_back = NULL;
    pl_trap = NULL;
    pl_on_save = NULL;
    report(s);
    json_free(last_run);
    last_run = NULL;
    return g_code;
}
