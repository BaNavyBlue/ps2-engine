/* art_dump: the battle animations as they play, for the art dump.
 *
 * Built as one translation unit with src/game/game.c (included below) so
 * it can reach the live battle, VRAM and game data that game.c keeps
 * static; every other source file links as usual. Nothing under src/ is
 * changed and nothing here feeds back into the game.
 *
 * It reads a plan (tools/art_dump_plan.py writes it), one job a line:
 *     monster <enemy id> <formation> <slot> <name>
 *     weapon  <char id> <item id> <name>
 *     tech    <char id> <technique id> <name>
 * and for each job enters that battle the way `start battle F` does, runs
 * the game until the battle's own loop, then drives only the battle
 * objects (bobj_run), exactly as the fight does, rendering after every
 * tick only the objects the job is about. Frames that do not change are
 * merged, so each record is a frame and how many ticks it stays up.
 *
 * Output: <out>/<job kind>/<name>/<animation>.adm, one file an animation:
 *     "ADM1", u16 width, u16 height, 64 x RGB (the battle's palette, run
 *     through the port's DAC ladder), then records of
 *     u16 ticks, s16 x, s16 y, u16 w, u16 h, w*h indices (0 = empty).
 * tools/art_export.py (with tools/adm.py) turns those into PNG / APNG / TIFF. */

#include "../src/game/game.c"
#include "../src/game/battle_tables.h"

#include <errno.h>
#include <sys/stat.h>

/* (build 127) Windows's mkdir takes no mode (MinGW's direct.h). */
#ifdef _WIN32
#include <direct.h>
#define ad_mkdir(path) _mkdir(path)
#else
#define ad_mkdir(path) mkdir(path, 0755)
#endif

#define AD_MAX_TICKS 2400

static Game        ad_game;
static Video       ad_game_video;
static Audio       ad_audio;

static Video       ad_video;
static Framebuffer ad_fb;
static Battle      ad_copy;

typedef struct {
    FILE *fh;
    u8    prev[SCREEN_W * SCREEN_H];
    bool  have_prev;
    int   ticks;
    int   records;
} AdOut;

static void ad_mkdirs(const char *path)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            ad_mkdir(buf);
            *p = '/';
        }
    }
    ad_mkdir(buf);
}

static void ad_put16(FILE *fh, int v)
{
    fputc(v & 0xFF, fh);
    fputc((v >> 8) & 0xFF, fh);
}

static bool ad_open(AdOut *o, const char *dir, const char *anim,
                    const Palette *pal)
{
    char path[600];
    ad_mkdirs(dir);
    snprintf(path, sizeof path, "%s/%s.adm", dir, anim);
    memset(o, 0, sizeof *o);
    o->fh = fopen(path, "wb");
    if (o->fh == NULL) {
        fprintf(stderr, "art_dump: cannot write %s: %s\n", path,
                strerror(errno));
        return false;
    }
    fwrite("ADM1", 1, 4, o->fh);
    ad_put16(o->fh, SCREEN_W);
    ad_put16(o->fh, SCREEN_H);
    for (int i = 0; i < PAL_TOTAL; i++) {
        Rgb8 c = cram_to_rgb8(pal->entry[i]);
        fputc(c.r, o->fh);
        fputc(c.g, o->fh);
        fputc(c.b, o->fh);
    }
    return true;
}

static void ad_flush(AdOut *o)
{
    if (!o->have_prev || o->ticks == 0) {
        return;
    }
    int x0 = SCREEN_W, y0 = SCREEN_H, x1 = -1, y1 = -1;
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            if (o->prev[y * SCREEN_W + x] != 0) {
                if (x < x0) { x0 = x; }
                if (x > x1) { x1 = x; }
                if (y < y0) { y0 = y; }
                if (y > y1) { y1 = y; }
            }
        }
    }
    int w = (x1 >= x0) ? x1 - x0 + 1 : 0;
    int h = (y1 >= y0) ? y1 - y0 + 1 : 0;
    if (w == 0) {
        x0 = 0;
        y0 = 0;
    }
    ad_put16(o->fh, o->ticks);
    ad_put16(o->fh, x0);
    ad_put16(o->fh, y0);
    ad_put16(o->fh, w);
    ad_put16(o->fh, h);
    for (int y = 0; y < h; y++) {
        fwrite(&o->prev[(y0 + y) * SCREEN_W + x0], 1, (size_t)w, o->fh);
    }
    o->records++;
    o->ticks = 0;
}

static void ad_add(AdOut *o, const u8 *px)
{
    if (o->have_prev && memcmp(o->prev, px, sizeof o->prev) == 0) {
        o->ticks++;
        return;
    }
    ad_flush(o);
    memcpy(o->prev, px, sizeof o->prev);
    o->have_prev = true;
    o->ticks = 1;
}

static int ad_close(AdOut *o)
{
    ad_flush(o);
    if (o->fh != NULL) {
        fclose(o->fh);
    }
    o->fh = NULL;
    return o->records;
}

/* Only the objects in `keep`, through the port's own battle_render, onto
 * an empty plane with the backdrop at index 0. */
static void ad_render(const bool keep[BOBJ_COUNT], const Palette *pal)
{
    ad_copy = g_battle;
    for (int i = 0; i < BOBJ_COUNT; i++) {
        if (!keep[i]) {
            ad_copy.obj[i].id = 0;
        }
    }
    video_init(&ad_video, &g_vram);
    ad_video.palette  = *pal;
    ad_video.backdrop = 0;
    video_sprites_begin(&ad_video);
    battle_render(&ad_copy, &ad_video, &g_vram);
    video_compose(&ad_video, &ad_fb);
}

static void ad_game_tick(void)
{
    Input in;
    input_clear(&in);   /* (build 160) no key, no gamepad input */
    game_tick(&ad_game, &in);
    atomic_store(&ad_audio.tail, atomic_load(&ad_audio.head));
}

/* `start battle F` with the given party, then the game run until the
 * battle's loop is up and the windows are idle. */
static bool ad_enter(int formation, const int *members, int n)
{
    game_debug_reset(&ad_game);
    for (int i = 0; i < 4; i++) {
        ad_game_tick();
    }
    /* game_debug_battle enters the level first, which sets up a new
     * game's party; the party is set after it, before the battle screen
     * is entered on the next tick. */
    game_debug_battle(&ad_game, formation);
    game_debug_party(&ad_game, 4, 30);
    for (int i = 0; i < n; i++) {
        game_debug_member_level(&ad_game, members[i], 30);
    }
    game_debug_members(&ad_game, members, n);
    for (int t = 0; t < 3000; t++) {
        ad_game_tick();
        if (ad_game.screen == SCREEN_BATTLE && battle_in_loop(&g_battle)
            && t > 200) {
            game_debug_enemy_hp(&ad_game, 30000);
            return true;
        }
    }
    fprintf(stderr, "art_dump: formation %d never reached its loop\n",
            formation);
    return false;
}

/* (build 141) The objects' own palette writes -- the hit flash -- land on a
 * scratch Video, as they landed on a scratch live palette before, so the
 * captured colours stay the battle's. */
static Video ad_fx_video;

static void ad_objects_tick(Video *scratch)
{
    bobj_run(&g_battle, &g_data, &g_win, &ad_game.rng, &g_vram, scratch,
             NULL, NULL);
}

static bool ad_effects_alive(int child)
{
    if (child >= 0 && g_battle.obj[child].id != 0) {
        return true;
    }
    for (int i = BOBJ_EFFECTS; i < BOBJ_COUNT; i++) {
        if (g_battle.obj[i].id != 0) {
            return true;
        }
    }
    return false;
}

static int ad_script_steps(int table, int index)
{
    const AnimScript *s = anim_script(anim_table(table), index);
    if (s == NULL) {
        return 0;
    }
    int n = 0;
    while (n < s->len && s->byte[n] != 0xFF && s->byte[n] != 0xFE) {
        n++;
    }
    return n;
}

/* ---- monsters ---- */

static void ad_monster(const char *out, int id, int formation, int slot,
                       const char *name)
{
    int party[1] = { 0 };
    if (!ad_enter(formation, party, 1)) {
        return;
    }
    Palette pal  = ad_game.video->palette;
    video_init(&ad_fx_video, &g_vram);
    ad_fx_video.palette = pal;
    int e = BOBJ_ENEMIES + 2 * slot;
    if (g_battle.obj[e].id == 0) {
        fprintf(stderr, "art_dump: %s: slot %d empty in formation %d\n",
                name, slot, formation);
        return;
    }
    g_battle.fight_active = true;
    BObj saved[BOBJ_COUNT];
    memcpy(saved, g_battle.obj, sizeof saved);
    EnemyStats st = g_battle.stat[slot];

    bool keep[BOBJ_COUNT];
    memset(keep, 0, sizeof keep);
    keep[e] = keep[e + 1] = true;
    for (int i = BOBJ_EFFECTS; i < BOBJ_COUNT; i++) {
        keep[i] = true;
    }

    char dir[512];
    snprintf(dir, sizeof dir, "%s/monster/%s", out, name);
    AdOut o;

    /* The standby loop, once round: every step of the idle script. */
    {
        BObj *m = &g_battle.obj[e];
        m->pos = 0;
        m->timer = 0;
        m->state = 1;
        m->f = 0;
        bool idles = (st.word0 & 8) || (st.word0 & 7) == 0;
        int steps = idles ? ad_script_steps(ANIM_IDLE, m->anim) : 0;
        int ticks = (steps > 0) ? steps * (m->dur + 1) : 1;
        if (ad_open(&o, dir, "idle", &pal)) {
            for (int t = 0; t < ticks; t++) {
                ad_objects_tick(&ad_fx_video);
                ad_render(keep, &pal);
                ad_add(&o, ad_fb.px);
            }
            int n = ad_close(&o);
            printf("monster\t%d\t%s\tidle\t%d\t%d\n", id, name, n, ticks);
        }
    }

    /* The attack, then (if it has one) the technique: the enemy's own
     * script from the start, its turn flag up, aimed at Rolf -- the
     * state loc_2A58 leaves it in -- run until the script and every
     * object it launched are gone. */
    for (int k = 0; k < 2; k++) {
        if (k == 1 && st.technique == 0) {
            break;
        }
        memcpy(g_battle.obj, saved, sizeof saved);
        g_battle.stat[slot] = st;
        g_battle.cc06 = 1;
        BObj *m = &g_battle.obj[e];
        m->pos = 0;
        m->timer = 0;
        m->state = (k == 0) ? 2 : 3;
        m->f = 0x01 | 0x80;
        m->target = 0;
        g_battle.obj[BOBJ_CHARS].f |= 0x80;
        ad_fx_video.palette = pal;
        if (!ad_open(&o, dir, (k == 0) ? "attack" : "technique", &pal)) {
            continue;
        }
        int t = 0, after = 0;
        for (; t < AD_MAX_TICKS; t++) {
            ad_objects_tick(&ad_fx_video);
            ad_render(keep, &pal);
            ad_add(&o, ad_fb.px);
            bool done = !(m->f & 0x01) && !ad_effects_alive(e + 1);
            if (done && ++after >= 2) {
                break;
            }
        }
        int n = ad_close(&o);
        printf("monster\t%d\t%s\t%s\t%d\t%d%s\n", id, name,
               (k == 0) ? "attack" : "technique", n, t + 1,
               (t >= AD_MAX_TICKS) ? "\tCAPPED" : "");
    }
}

/* ---- the party: a weapon's attack or a technique ---- */

static void ad_party(const char *out, bool tech, int c, int what,
                     const char *name)
{
    int party[1] = { c };
    if (!ad_enter(1, party, 1)) {
        return;
    }
    Palette pal  = ad_game.video->palette;
    video_init(&ad_fx_video, &g_vram);
    ad_fx_video.palette = pal;
    CharData *ch = &g_data.chr[c & 7];
    if (!tech) {
        ch->equip[EQUIP_RIGHT] = (u8)what;
        ch->equip[EQUIP_LEFT]  = 0;
    }
    ch->tp     = 999;
    ch->status = 0;

    int a = BOBJ_CHARS + 2 * (c & 7);
    g_battle.fight_active = true;
    g_battle.cmd[c & 7][0] = tech ? 1 : 0;
    g_battle.cmd[c & 7][1] = tech ? (u16)what : 0;
    g_battle.cmd[c & 7][2] = 0;
    g_battle.obj[a].f |= 0x01;
    g_battle.cc06 = 1;

    bool with_char[BOBJ_COUNT], fx_only[BOBJ_COUNT];
    memset(with_char, 0, sizeof with_char);
    memset(fx_only, 0, sizeof fx_only);
    with_char[a] = with_char[a + 1] = true;
    fx_only[a + 1] = true;
    for (int i = BOBJ_EFFECTS; i < BOBJ_COUNT; i++) {
        with_char[i] = fx_only[i] = true;
    }

    char dir_fx[512], dir_char[512];
    snprintf(dir_fx, sizeof dir_fx, "%s/%s", out,
             tech ? "effect_technique" : "effect_weapon");
    snprintf(dir_char, sizeof dir_char, "%s/party/%d", out, c & 7);
    AdOut fx, pc;
    bool ok_fx = ad_open(&fx, dir_fx, name, &pal);
    bool ok_pc = ad_open(&pc, dir_char, name, &pal);
    int t = 0, after = 0;
    bool started = false;
    for (; t < AD_MAX_TICKS; t++) {
        ad_objects_tick(&ad_fx_video);
        if (ok_fx) {
            ad_render(fx_only, &pal);
            ad_add(&fx, ad_fb.px);
        }
        if (ok_pc) {
            ad_render(with_char, &pal);
            ad_add(&pc, ad_fb.px);
        }
        BObj *o = &g_battle.obj[a];
        if (o->state == 2) {
            started = true;
        }
        bool done = started && o->state != 2 && !ad_effects_alive(a + 1);
        if (!started && t > 60 && !(o->f & 0x01)) {
            done = true;        /* the action fell through: nothing to show */
        }
        if (done && ++after >= 2) {
            break;
        }
    }
    int n1 = ok_fx ? ad_close(&fx) : 0;
    int n2 = ok_pc ? ad_close(&pc) : 0;
    printf("%s\t%d\t%s\t%d\t%d\t%d%s%s\n", tech ? "tech" : "weapon", what,
           name, c & 7, n1, n2, started ? "" : "\tNOT_STARTED",
           (t >= AD_MAX_TICKS) ? "\tCAPPED" : "");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: art_dump PLAN OUTDIR (run from ps2/)\n");
        return 2;
    }
    FILE *plan = fopen(argv[1], "r");
    if (plan == NULL) {
        fprintf(stderr, "art_dump: cannot read %s\n", argv[1]);
        return 2;
    }
    assetfs_preload("assets/manifest.txt");
    video_init(&ad_game_video, NULL);
    if (!game_init(&ad_game, &ad_game_video)) {
        fprintf(stderr, "art_dump: game_init failed (run from ps2/)\n");
        return 2;
    }
    audio_init(&ad_audio, AUDIO_RATE);
    audio_load_music(&ad_audio, "assets");
    game_attach_audio(&ad_game, &ad_audio);

    char line[512];
    while (fgets(line, sizeof line, plan) != NULL) {
        char kind[16], name[256];
        int a = 0, b = 0, c = 0;
        if (sscanf(line, "monster %d %d %d %255s", &a, &b, &c, name) == 4) {
            ad_monster(argv[2], a, b, c, name);
        } else if (sscanf(line, "%15s %d %d %255s", kind, &a, &b, name) == 4
                   && (strcmp(kind, "weapon") == 0
                       || strcmp(kind, "tech") == 0)) {
            ad_party(argv[2], strcmp(kind, "tech") == 0, a, b, name);
        }
        fflush(stdout);
    }
    fclose(plan);
    return 0;
}
