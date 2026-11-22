#include "core/assetfs.h"
#include "core/audio.h"
#include "core/tileset.h"
#include "core/video.h"
#include "game/game.h"
#include "game/gamedata.h"
#include "game/lang.h"
#include "game/rumble.h"
#include "game/ui.h"
#include "harness/autoplay.h"
#include "platform/home.h"
#include "platform/platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TICK_SECONDS (1.0 / TICK_HZ)

/* If the process stalls (debugger, window drag), catch up by at most this
 * much rather than spiralling. */
#define MAX_CATCHUP_SECONDS 0.25

/* Whether the display runs at the game's rate is judged from the last
 * PACE_WINDOW frame times with the PACE_TRIM longest and shortest thrown
 * away, so a level load, a dropped frame or a late timestamp cannot move
 * the estimate. The lock is taken within LOCK_ENTER of a tick and given up
 * only beyond LOCK_LEAVE, so it does not chatter at the edge. */
#define PACE_WINDOW 64
#define PACE_TRIM   8
#define LOCK_ENTER  0.04
#define LOCK_LEAVE  0.08

/* PS2_PACE=1 prints one summary line every this many frames. */
#define PACE_REPORT_FRAMES 300

static int compare_doubles(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

static double pace_estimate(const double *ring)
{
    double sorted[PACE_WINDOW];
    memcpy(sorted, ring, sizeof sorted);
    qsort(sorted, PACE_WINDOW, sizeof sorted[0], compare_doubles);

    double sum = 0.0;
    for (int i = PACE_TRIM; i < PACE_WINDOW - PACE_TRIM; i++) {
        sum += sorted[i];
    }
    return sum / (PACE_WINDOW - 2 * PACE_TRIM);
}

/* Wall clock for measuring the loop's own work. platform_now cannot be
 * used for it: the headless platform's clock is virtual. */
static double wall_seconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* (build 100b) COMPUTER PLAYS's fast speed: ticks back to back, a frame
 * drawn every FAST_TICKS of them (PS2_FAST_TICKS) or every FAST_FRAME
 * seconds, whichever comes first. docs/computer_plays.md. */
#define FAST_TICKS  4096
#define FAST_FRAME  0.015

/* (build 120) How long a frame waits for the driver's first tick at NORMAL
 * and 2X; 2X's second waits out what is left of it. */
#define TICK_WAIT   0.012

static Autoplay g_autoplay;

/* (build 160) The rumble (src/game/rumble.h): the watcher, after every tick
 * the player's game runs, and the Escape menu's test buzz, a voice a
 * gamepad. */
static Rumble      g_rumble;
static RumbleVoice g_rumble_test[PLATFORM_PADS];

/* (build 123) The folder every save goes under (home_saves): "saves" beside
 * the game, or the user's own data folder. */
static char g_saves[512] = "saves";

static void draw(Game *game, Video *video, Framebuffer *fb)
{
    game_render(game);
    video_compose(video, fb);
    game_overlay(game, fb);
}

/* The Escape menu's own sound while the computer has the game (the game
 * plays it itself otherwise). */
static void menu_sound(void *ctx, u8 id)
{
    audio_play_sfx((Audio *)ctx, id);
}

/* (build 100c) The frame while the Escape menu is up over the computer's
 * game: drawn once the command thread has let go of the game, with no
 * tick run. */
static void computer_draw(Game *game, Video *video, Framebuffer *fb)
{
    Input in;
    AutoplayHold h = autoplay_hold(&g_autoplay, &in, 0.005, false);
    if (h == AUTOPLAY_HOLD_TICK || h == AUTOPLAY_HOLD_IDLE) {
        draw(game, video, fb);
        autoplay_release(&g_autoplay, false);
    }
}

/* (build 110) The AUDIO page's settings to the mixer: the volumes at once,
 * and the synth when the one that should play is not the one playing -- the
 * audio thread hands it the register shadow between ticks. */
static void apply_audio(const SysMenu *m, Audio *audio)
{
    int playing = sysmenu_audio_synth_playing(m);

    audio_set_volume(audio, m->audio.master_db, m->audio.music_db,
                     m->audio.effects_db);
    audio_set_oversample(audio, m->audio.oversample);
    /* (build 112) Q's switches: VOICES, TAILS, PATCHES, (116) GRAIN */
    audio_set_synth_option(audio, SYNTH_OPT_VOICES, m->audio.voices);
    audio_set_synth_option(audio, SYNTH_OPT_TAILS, m->audio.tails);
    audio_set_synth_option(audio, SYNTH_OPT_PATCHES, m->audio.patches ? 1 : 0);
    audio_set_synth_option(audio, SYNTH_OPT_GRAIN, m->audio.grain ? 1 : 0);
    if (playing != audio->synth_want) {
        audio_set_synth(audio, playing);
        fprintf(stderr, "audio: synth %s\n",
                synth_backend((SynthId)playing)->name);
    }
}

/* The start-up line's volume: "-6 dB", or "off". */
static const char *db_words(int db, char *buf, size_t n)
{
    if (db <= SYSMENU_DB_OFF) {
        snprintf(buf, n, "off");
    } else {
        snprintf(buf, n, "%d dB", db);
    }
    return buf;
}

/* The menu's settings to the platform and settings.json, when changed.
 * (build 105) While the menu is up, what the display can do is asked
 * every frame, so the CRT page's values follow a resize or FULLSCREEN
 * at once. (build 110) And the AUDIO page's to the mixer. */
static void apply_settings(Game *game, Platform *plat, Audio *audio)
{
    SysMenu *m = &game->sysmenu;
    if (m->audio_changed) {
        m->audio_changed = false;
        apply_audio(m, audio);
        sysmenu_save_settings(m);
    }
    if (m->controls_changed || m->display_changed || m->fullscreen_changed
        || m->computer_speed_changed || m->crt_changed || m->rumble_changed) {
        m->controls_changed       = false;
        m->display_changed        = false;
        m->fullscreen_changed     = false;
        m->computer_speed_changed = false;
        m->crt_changed            = false;
        m->rumble_changed         = false;      /* (build 160) */
        platform_set_controls(plat, &m->controls);
        platform_set_aspect(plat, m->display);
        platform_set_fullscreen(plat, m->fullscreen);
        platform_set_crt(plat, &m->crt);
        sysmenu_save_settings(m);
    }
    /* (build 163) LANGUAGE: the game takes it up itself (game_tick);
     * here it is only written down. */
    if (m->language_changed) {
        m->language_changed = false;
        sysmenu_save_settings(m);
    }
    if (m->open) {
        platform_display_info(plat, &m->crt_display);
    }
}

/* (build 162) AUTO and AUTO 2X play FAST but for the story's beats
 * (game_story_beat): from the tick one begins until BEAT_HOLD ticks after
 * it ends, so a scene's last frame stands a second before the game races
 * on. g_beat_hold counts those ticks down. */
#define BEAT_HOLD 60
static int g_beat_hold;

/* (build 169a) AUTO's ticks a frame between the beats, every frame drawn
 * and the sound muted; AUTO 2X's twice it. Until 169a they ran FAST, a
 * frame drawn every 1,600 ticks or so: "I don't get to see a blur of grind
 * fighting" (The owner). */
#define AUTO_GRIND 32

/* (build 120) Ticks a frame for SPEED while the computer plays: 1 or 2, or
 * 0 for FAST. (build 162) AUTO and AUTO 2X are NORMAL and 2X in a story
 * beat, (build 169a) AUTO_GRIND and twice it outside one (until 169a FAST;
 * until 162 FAST only while the driver ground). */
static int computer_rate(int speed)
{
    switch (speed) {
        case COMPUTER_SPEED_AUTO: {
            return (g_beat_hold > 0) ? 1 : AUTO_GRIND;
        }
        case COMPUTER_SPEED_2X: {
            return 2;
        }
        case COMPUTER_SPEED_AUTO_2X: {
            return (g_beat_hold > 0) ? 2 : 2 * AUTO_GRIND;
        }
        case COMPUTER_SPEED_FAST: {
            return 0;
        }
        default: {
            return 1;
        }
    }
}

/* (build 169a) Whether the sound is muted at a rate: FAST and the AUTO
 * speeds' grind. It plays at one and two ticks a frame. */
static bool computer_muted(int rate)
{
    return rate <= 0 || rate > 2;
}

/* (build 100c) The computer takes the game: the lamp lit when the menu
 * closed. False (and the lamp out) when it cannot start. */
static bool computer_begin(Game *game, Video *video, Audio *audio)
{
    char start[600];
    if (game->sysmenu.computer_from == 0) {
        snprintf(start, sizeof start, "new");
    } else if (game->sysmenu.computer_from == 5) {
        snprintf(start, sizeof start, "last");
    } else {
        snprintf(start, sizeof start, "%s/slot%d.json", g_saves,
                 game->sysmenu.computer_from);
    }
    if (!autoplay_start(&g_autoplay, game, video, start,
                        sysmenu_speed_key(game->sysmenu.computer_speed),
                        getenv("PS2_COMPUTER_ARGS"))) {
        game->sysmenu.computer_on = false;
        return false;
    }
    g_beat_hold = 0;
    audio_set_muted(audio,
                    computer_muted(computer_rate(game->sysmenu.computer_speed)));
    return true;
}

/* (build 105) Whether a file can be read -- fopen, not POSIX's access(),
 * so main.c builds on Windows too. */
static bool readable(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}

/* What the Escape menu shows for COMPUTER PLAYS when it opens: whether it
 * can run, and which of the player's slots hold a save. */
static void computer_menu_refresh(SysMenu *m)
{
    char why[256];
    if (!autoplay_available(why, sizeof why, m->computer_note,
                            sizeof m->computer_note)) {
        fprintf(stderr, "computer plays: %s\n", why);
    }
    for (int i = 0; i < 4; i++) {
        char path[600];
        snprintf(path, sizeof path, "%s/slot%d.json", g_saves, i + 1);
        m->slot_exists[i] = readable(path);
    }
    /* (build 103) The computer's own last run: where it saved last. */
    char last[600];
    snprintf(last, sizeof last, "%s/last.json", autoplay_dir());
    m->last_run_exists = readable(last);
    if (m->computer_from == 5) {
        if (!m->last_run_exists) {
            m->computer_from = 0;
        }
    } else if (m->computer_from > 0
               && !m->slot_exists[m->computer_from - 1]) {
        m->computer_from = 0;
    }
}

/* (build 162) After each of the computer's ticks, at every SPEED: a story
 * beat holds g_beat_hold at BEAT_HOLD, and without one it counts down. A
 * line on stderr as each beat begins and as the hold runs out. True when a
 * beat has just begun. (build 162a) The driver's gear bought in a store
 * holds it too (autoplay_buying_gear, inside the store only: the walk to
 * its door stays FAST); the caller holds the game, as that asks. */
static bool beat_watch(const Game *game)
{
    const char *why = game_story_beat(game, NULL, 0);
    bool gear = why == NULL && game->screen == SCREEN_BUILDING
             && autoplay_buying_gear(&g_autoplay);
    if (gear) {
        why = "buying gear";
    }
    if (why != NULL) {
        bool began = g_beat_hold == 0;
        if (began) {
            char what[96];
            if (gear) {
                snprintf(what, sizeof what, "the driver's @gear on");
            } else {
                game_story_beat(game, what, sizeof what);
            }
            fprintf(stderr, "computer plays: story beat at tick %u: %s "
                            "(%s)\n", g_autoplay.ticks + 1, why, what);
        }
        g_beat_hold = BEAT_HOLD;
        return began;
    }
    if (g_beat_hold > 0) {
        g_beat_hold--;
        if (g_beat_hold == 0) {
            fprintf(stderr, "computer plays: fast again at tick %u\n",
                    g_autoplay.ticks + 1);
        }
    }
    return false;
}

/* One frame of COMPUTER PLAYS: the ticks the driver asks for -- `rate` of
 * them (one at normal speed, (build 120) two at 2X), or with rate 0 up to
 * fast_ticks at fast -- run here on the main thread while the command
 * thread waits, and the frame drawn while it still waits. When it is
 * waiting on the driver instead, the last frame stands. (build 162)
 * `beats`: AUTO's FAST ends the frame at a story beat's first tick, so
 * the beat plays from there at its own speed; (build 169a) its grind's
 * AUTO_GRIND ticks too. (build 169) Returns the ticks it ran, for
 * PS2_TRACE and PS2_PACE. */
static int computer_frame(Game *game, Video *video, Framebuffer *fb,
                          int rate, int fast_ticks, bool beats)
{
    bool   fast  = rate <= 0;
    int    ticks = 0;
    double t0    = wall_seconds();
    for (;;) {
        Input in;
        double left = (fast ? FAST_FRAME : TICK_WAIT)
                    - (wall_seconds() - t0);
        double wait = (!fast && ticks == 0) ? TICK_WAIT
                    : ((left > 0.0) ? left : 0.0);
        AutoplayHold h = autoplay_hold(&g_autoplay, &in, wait, true);
        if (h == AUTOPLAY_HOLD_TICK) {
            game_tick(game, &in);
            ticks++;
            bool began = beat_watch(game);
            bool last  = (fast ? (ticks >= fast_ticks
                                  || wall_seconds() - t0 > FAST_FRAME)
                               : ticks >= rate)
                      || (beats && began);
            if (last) {
                draw(game, video, fb);
            }
            autoplay_release(&g_autoplay, true);
            if (last) {
                return ticks;
            }
        } else if (h == AUTOPLAY_HOLD_IDLE) {
            if (ticks > 0) {
                draw(game, video, fb);
            }
            autoplay_release(&g_autoplay, false);
            return ticks;
        } else {
            return ticks;
        }
    }
}

/* (build 160) One tick of the player's game, and the rumble's look at it. */
static void play_tick(Game *game, const Input *in)
{
    game_tick(game, in);
    RumbleMotion mo;
    game_motion(game, &mo);                     /* (build 161) */
    rumble_watch(&g_rumble, game_debug_data(game), (int)game->screen,
                 game->audio, &mo);
}

/* (build 160) The gamepads' motors for this frame: the menu's names for the
 * GAMEPAD page (and PAD 1's labels for the BUTTONS page); with the Escape
 * menu up, only its test buzz (the game's voices were stopped as it
 * opened); else what the game made felt, every gamepad alike, at RUMBLE.
 * `still`: nothing (COMPUTER PLAYS has the game). */
static void rumble_frame(Game *game, Platform *plat, bool still)
{
    SysMenu *m = &game->sysmenu;
    /* (build 160) The BUTTONS page draws PAD 1's labels. */
    sysmenu_set_pad_style(m, platform_pad_style(plat, 0));
    for (int k = 0; k < PLATFORM_PADS; k++) {
        sysmenu_set_pad_name(m, k, platform_pad_name(plat, k));
        rumble_voices_age(&g_rumble_test[k], 1);
        if (m->rumble_test == k || m->rumble_test == SYSMENU_PADS) {
            rumble_test(&g_rumble_test[k], 1);
        }
    }
    m->rumble_test = -1;
    for (int k = 0; k < PLATFORM_PADS; k++) {
        RumbleMotors mo = { 0, 0 };
        if (still) {
            memset(&g_rumble_test[k], 0, sizeof g_rumble_test[k]);
        } else if (m->open) {
            mo = rumble_voices_motors(&g_rumble_test[k], 1);
        } else {
            mo = rumble_motors(&g_rumble);
        }
        mo = rumble_scale(mo, m->rumble);
        platform_rumble(plat, k, mo.low, mo.high);
    }
}

/* (build 186) A language asked for whose files are not made yet
 * (game.language_wanted): the platform's window offers to make them, and
 * when it has, they are preloaded, the languages' names read again and the
 * language chosen -- the game takes it up at its next tick. Left, the
 * language the game went on in is the one settings.json keeps. `files`:
 * the command line's, at the start. */
static void offer_language(Game *game, int nfiles, char **files)
{
    int i = game->language_wanted;
    game->language_wanted = -1;
    if (!platform_add_language(lang_key(i), lang_name(i), nfiles, files)
        || lang_missing(i)) {
        game->sysmenu.language_changed = true;
        return;
    }
    assetfs_preload("assets/manifest.txt");
    lang_rescan();
    game->sysmenu.language         = i;
    game->sysmenu.language_changed = true;
    fprintf(stderr, "lang: %s made; chosen\n", lang_name(i));
}

/* OR a frame's presses into the set waiting for the next tick. */
static void pending_press(Buttons *pending, const Buttons *presses)
{
    const bool *src = (const bool *)presses;
    bool       *dst = (bool *)pending;
    for (size_t i = 0; i < sizeof *pending / sizeof(bool); i++) {
        dst[i] = dst[i] || src[i];
    }
}

/* (build 160) The same for everything else a press is: the Escape menu's
 * own presses, Backspace, and the first key and gamepad input since the
 * last tick (for the CONTROLS and BUTTONS pages). */
static void pending_input(Input *pending, const Input *in)
{
    pending_press(&pending->pressed, &in->pressed);
    pending_press(&pending->nav_pressed, &in->nav_pressed);
    pending->back = pending->back || in->back;
    if (pending->key == KEY_NONE) {
        pending->key = in->key;
    }
    if (pending->pad_input == PAD_NONE) {
        pending->pad_input = in->pad_input;
    }
}

/* (build 160) A tick's input: what is held now, the presses waiting. */
static void take_pending(Input *input, Input *pending)
{
    input->pressed     = pending->pressed;
    input->nav_pressed = pending->nav_pressed;
    input->back        = pending->back;
    input->key         = pending->key;
    input->pad_input   = pending->pad_input;
    input_clear(pending);
}

int main(int argc, char **argv)
{
    /* The game owns the tileset: it mirrors VRAM tile space and each
     * screen places its art at the index that screen's load address
     * implies, exactly as the original does. */

    /* (build 123) The game's own folder, if it was started elsewhere, and
     * where its saves go: the slots, settings.json, COMPUTER PLAYS's. */
    home_enter();
    home_saves(g_saves, sizeof g_saves);
    char settings[600];
    snprintf(settings, sizeof settings, "%s/settings.json", g_saves);
    gamedata_set_save_home(g_saves);
    sysmenu_set_settings_path(settings);
    autoplay_set_home(g_saves);

    /* (build 138b) The mods folder (docs/mods.md): a file there is read
     * in place of the game's own. (build 138c) Every build's, the tree's
     * too; a packed game writes its own files out under it to copy from. */
    char mods[600];
    home_mods(mods, sizeof mods);
    assetfs_set_mods(mods);

    /* (build 186) The first start (docs/first_start.md): no game files
     * here and none packed in, so they are made now, from the player's own
     * cartridge -- an image named on the command line (dropped onto the
     * program), or one the platform's window asks for. */
    if (!readable("assets/manifest.txt") && assetfs_pack_count() <= 0) {
        if (!platform_make_assets(argc - 1, argv + 1)) {
            return 1;
        }
        argc = 1;               /* what was named is used */
    }

    /* Every asset file into RAM before anything asks for one. From here
     * on a screen change, a level change or a door decodes from memory. */
    assetfs_preload("assets/manifest.txt");

    home_write_originals(mods);
    int modded = assetfs_mods_count();
    fprintf(stderr, "mods: %s, %d in use\n", mods, modded);
    for (int i = 0; i < modded; i++) {
        const char *path = assetfs_mods_path(i);
        fprintf(stderr, "mods: %s/%s in place of the game's own\n", mods,
                (path != NULL) ? path + 7 : "?");
    }

    Video video;
    video_init(&video, NULL);

    Game game;
    if (!game_init(&game, &video)) {
        return 1;
    }
    /* (build 186) settings.json's language, its files not made yet. */
    if (game.language_wanted >= 0) {
        offer_language(&game, argc - 1, argv + 1);
    }

    int    asset_files = 0;
    size_t asset_bytes = 0;
    assetfs_stats(&asset_files, &asset_bytes, NULL);
    fprintf(stderr, "assets: %d files, %.1f MB held in RAM\n",
            asset_files, (double)asset_bytes / 1e6);

    Platform *plat = platform_create("Phantasy Star II");
    if (plat == NULL) {
        return 1;
    }

    Audio audio;
    audio_init(&audio, AUDIO_RATE);
    audio_load_music(&audio, "assets");
    /* PS2_NO_AUDIO=1 leaves the sound device closed, so no audio thread
     * runs at all. For telling whether uneven frames are the FM core
     * taking the CPU at the wrong moment. */
    if (getenv("PS2_NO_AUDIO") == NULL) {
        platform_attach_audio(plat, &audio);
    }
    game_attach_audio(&game, &audio);

    /* saves/settings.json: which keys are the pad and how wide a pixel
     * is drawn. Chosen in the Escape menu, applied here because only
     * main.c sees both sides. */
    platform_set_controls(plat, &game.sysmenu.controls);
    platform_set_aspect(plat, game.sysmenu.display);
    platform_set_fullscreen(plat, game.sysmenu.fullscreen);
    platform_set_crt(plat, &game.sysmenu.crt);
    platform_display_info(plat, &game.sysmenu.crt_display);

    /* (build 110) The AUDIO page's synth and volumes. PS2_SYNTH=chip|q
     * picks the synth for this run whatever settings.json says -- `make
     * audio_check` and `audio_check_q` name theirs -- and is what the page
     * shows; it is written to settings.json only if the page is changed. */
    const char *synth_env = getenv("PS2_SYNTH");
    if (synth_env != NULL) {
        int id = synth_from_name(synth_env);
        if (id < 0) {
            fprintf(stderr, "audio: PS2_SYNTH=%s is not chip or q\n",
                    synth_env);
        } else {
            game.sysmenu.audio.synth = id;
        }
    }
    /* (111a) PS2_Q_OVERSAMPLE=1|2|4 for one run, whatever the file says
     * (audio_check_q names 4). */
    const char *os_env = getenv("PS2_Q_OVERSAMPLE");
    if (os_env != NULL) {
        int os = atoi(os_env);
        game.sysmenu.audio.oversample = (os == 1 || os == 2) ? os : 4;
    }
    /* (build 112) The same for Q's switches, for one run: PS2_Q_EFFECTS=take
     * (VOICES LIMITED; 111b's name kept), PS2_Q_TAILS=fade|ring,
     * PS2_Q_PATCHES=0|1. */
    const char *voices_env = getenv("PS2_Q_EFFECTS");
    if (voices_env != NULL) {
        game.sysmenu.audio.voices = (strcmp(voices_env, "take") == 0) ? 1 : 0;
    }
    const char *tails_env = getenv("PS2_Q_TAILS");
    if (tails_env != NULL) {
        game.sysmenu.audio.tails = (strcmp(tails_env, "ring") == 0) ? 1 : 0;
    }
    const char *patches_env = getenv("PS2_Q_PATCHES");
    if (patches_env != NULL) {
        game.sysmenu.audio.patches = strcmp(patches_env, "0") != 0;
    }
    /* (build 116) PS2_Q_GRAIN=0|1, build 112's diagnostic, now GRAIN's */
    const char *grain_env = getenv("PS2_Q_GRAIN");
    if (grain_env != NULL) {
        game.sysmenu.audio.grain = strcmp(grain_env, "1") == 0;
    }
    apply_audio(&game.sysmenu, &audio);
    char master_words[16], music_words[16], effects_words[16];
    fprintf(stderr, "audio: %s%s, master %s, music %s, effects %s, "
                    "oversample %dx, voices %s, tails %s, patches %s "
                    "(%d in the file), grain %s\n",
            synth_backend((SynthId)audio.synth_want)->name,
            (sysmenu_audio_synth_playing(&game.sysmenu)
             != game.sysmenu.audio.synth) ? " (q asked for: not built)" : "",
            db_words(game.sysmenu.audio.master_db, master_words,
                     sizeof master_words),
            db_words(game.sysmenu.audio.music_db, music_words,
                     sizeof music_words),
            db_words(game.sysmenu.audio.effects_db, effects_words,
                     sizeof effects_words),
            game.sysmenu.audio.oversample,
            game.sysmenu.audio.voices ? "limited" : "extra",
            game.sysmenu.audio.tails ? "ring" : "fade",
            game.sysmenu.audio.patches ? "on" : "off", audio.patch_count,
            game.sysmenu.audio.grain ? "on" : "off");

    const char *jump = getenv("PS2_BUILDING");
    if (jump != NULL) {
        long v[6] = { 0, 0, 0, 0, -1, -1 };
        char *end = (char *)jump;
        for (int i = 0; i < 6 && *end != '\0'; i++) {
            v[i] = strtol(end, &end, 0);
            if (*end == ',') {
                end++;
            }
        }
        game_debug_building(&game, (int)v[0], (int)v[1], (int)v[2],
                            (int)v[3], (int)v[4], (int)v[5]);
    }

    const char *lvl = getenv("PS2_LEVEL");
    if (lvl != NULL) {
        long v[3] = { 0, 0, 0 };
        char *end = (char *)lvl;
        for (int i = 0; i < 3 && *end != '\0'; i++) {
            v[i] = strtol(end, &end, 0);
            if (*end == ',') {
                end++;
            }
        }
        game_debug_level(&game, (int)v[0], (int)v[1], (int)v[2]);
    }
    const char *bat = getenv("PS2_BATTLE");
    if (bat != NULL) {
        game_debug_battle(&game, (int)strtol(bat, NULL, 0));
    }

    /* (build 140) Static: with the colour plane the frame is 280 KB, a
     * quarter of the main thread's whole stack on Windows. */
    static Framebuffer fb;
    memset(&fb, 0, sizeof fb);
    bool        menu_was_open = false;
    const char *fast_env      = getenv("PS2_FAST_TICKS");
    const int   fast_ticks    = (fast_env != NULL && atoi(fast_env) > 0)
                              ? atoi(fast_env) : FAST_TICKS;
    Input       input;
    Input       pending;        /* (build 160) every press, not only the
                                   buttons */
    input_clear(&input);
    input_clear(&pending);
    double      prev  = platform_now(plat);
    double      accum = 0.0;
    double      pace_ring[PACE_WINDOW];
    int         pace_at = 0;
    bool        locked  = true;

    for (int i = 0; i < PACE_WINDOW; i++) {
        pace_ring[i] = TICK_SECONDS;
    }

    /* PS2_TRACE=1 prints one line per presented frame: the measured
     * delta, the rate estimate, the mode and how many ticks ran. A walk
     * stutters exactly when a frame shows 0 ticks or 2.
     *
     * PS2_PACE=1 prints one line every five seconds instead: how many
     * frames were locked, how many ran 0 or 2+ ticks, how many took long
     * enough to have missed a refresh, and the most time the loop spent
     * on its own work -- ticking, drawing, handing the frame over --
     * as opposed to waiting for the display.
     *
     * (build 169) While COMPUTER PLAYS has the game, PS2_TRACE's line is
     * the computer's frame -- its rate, the ticks it ran, the tick it
     * reached -- and PS2_PACE's counts the frames at NORMAL or 2X that ran
     * fewer ticks than their rate (docs/computer_plays.md, Speed). */
    const bool trace = getenv("PS2_TRACE") != NULL;
    const bool pace  = getenv("PS2_PACE") != NULL;
    long frame_no = 0;

    int    rep_locked = 0, rep_offbeat = 0, rep_long = 0;
    int    rep_paced = 0, rep_short = 0;        /* (build 169) */
    double rep_work = 0.0, rep_dt_min = 1.0, rep_dt_max = 0.0;

    while (platform_pump(plat, &input)) {
        double now = platform_now(plat);
        double dt  = now - prev;
        prev = now;

        /* How many ticks is this frame worth? Under vsync at the game's
         * own rate the answer is always one, and the clock must not be
         * asked: it reads 16.5 ms one frame and 16.9 the next, 21 then 12
         * when a timestamp is taken late, 33 when a refresh is missed.
         * Any accumulator turns those into a frame with two ticks and a
         * frame with none -- a 2px jump and a held frame in a
         * 1px-per-field walk. The Mega Drive ran one game loop per VBlank
         * and slowed down when it overran; locked, this does the same, so
         * a dropped frame costs one hold and no catch-up jump.
         *
         * Anything that is not the game's rate -- 120 or 144 Hz, vsync
         * off, a machine that cannot keep up -- runs free on the
         * accumulator, which keeps game time right at the cost of uneven
         * steps. */
        pace_ring[pace_at] = dt;
        pace_at = (pace_at + 1) % PACE_WINDOW;

        double estimate = pace_estimate(pace_ring);
        double error    = fabs(estimate - TICK_SECONDS) / TICK_SECONDS;
        if (locked && error > LOCK_LEAVE) {
            locked = false;
        } else if (!locked && error < LOCK_ENTER) {
            locked = true;
        }

        double work_start = wall_seconds();

        /* (build 100b-100c) COMPUTER PLAYS has the game. The player's keys
         * are the Escape menu's and nothing else's: it opens on Escape and
         * runs here, the game held where the computer's last tick left it;
         * closing it with COMPUTER PLAYS's lamp put out hands the pad back.
         * Nothing else the player presses reaches the game. */
        if (autoplay_running(&g_autoplay)) {
            bool menu = sysmenu_tick(&game.sysmenu, &input, menu_sound,
                                     &audio);
            bool closed = menu_was_open && !game.sysmenu.open;
            if (game.sysmenu.open && !menu_was_open) {
                computer_menu_refresh(&game.sysmenu);
            }
            menu_was_open = game.sysmenu.open;
            apply_settings(&game, plat, &audio);

            bool back  = game.sysmenu.quit
                      || (closed && !game.sysmenu.computer_on);
            bool ended = false;
            if (!back && !menu) {
                int  speed = game.sysmenu.computer_speed;
                int  rate  = computer_rate(speed);
                bool beats = speed == COMPUTER_SPEED_AUTO
                          || speed == COMPUTER_SPEED_AUTO_2X;
                audio_set_muted(&audio, computer_muted(rate));
                int ran = computer_frame(&game, &video, &fb, rate,
                                         fast_ticks, beats);
                ended = !g_autoplay.thread_live
                     || g_autoplay.state == AUTOPLAY_FINISHED;
                /* (build 169) At NORMAL and 2X a frame short of its
                 * ticks is one the music stood still in: the driver was
                 * still thinking at TICK_WAIT. (build 169a) Only the
                 * rates with the sound on: AUTO's grind is muted. */
                bool heard = !computer_muted(rate);
                if (trace) {
                    fprintf(stderr, "%6ld  computer rate %d  ticks %d  "
                                    "(tick %u)%s\n",
                            frame_no, rate, ran, g_autoplay.ticks,
                            (heard && ran < rate) ? "  <-- short" : "");
                }
                if (pace && heard) {
                    rep_paced++;
                    rep_short += (ran < rate) ? 1 : 0;
                }
            } else if (!back) {
                audio_set_muted(&audio, false);
                computer_draw(&game, &video, &fb);
            }
            /* (build 160) The computer's game shakes no one; the watcher
             * looks afresh when the pad comes back. */
            rumble_reset(&g_rumble);
            rumble_frame(&game, plat, true);
            if (back || ended) {
                autoplay_stop(&g_autoplay);
                game.sysmenu.computer_on = false;
                audio_set_muted(&audio, false);
                input_clear(&pending);
                draw(&game, &video, &fb);
                /* PS2_COMPUTER_QUIT=1: a test's run ends with the driver. */
                if ((ended && getenv("PS2_COMPUTER_QUIT") != NULL)
                    || game.sysmenu.quit) {
                    platform_present(plat, &fb, &video.palette);
                    break;
                }
            }
            platform_present(plat, &fb, &video.palette);
            frame_no++;
            if (pace && frame_no % PACE_REPORT_FRAMES == 0) {
                fprintf(stderr, "pace: computer plays, %d of %d frames at "
                                "its speed short of their ticks\n",
                        rep_short, rep_paced);
                rep_short = rep_paced = 0;
            }
            continue;
        }

        /* A press is delivered to exactly one tick. The pump reports
         * presses per frame; free-running, a frame can carry two ticks
         * (the same press would count twice -- a cursor stepping two
         * stops, a menu confirmed and the next one too) or none (the
         * press would be gone before any tick saw it). So presses are
         * accumulated here until a tick consumes them. */
        pending_input(&pending, &input);

        int ticks = 0;
        if (locked) {
            take_pending(&input, &pending);
            play_tick(&game, &input);
            ticks = 1;
            accum = 0.0;
        } else {
            accum += dt;
            if (accum > MAX_CATCHUP_SECONDS) {
                accum = MAX_CATCHUP_SECONDS;
            }
            while (accum >= TICK_SECONDS) {
                take_pending(&input, &pending);
                play_tick(&game, &input);
                accum -= TICK_SECONDS;
                ticks++;
            }
        }
        /* (build 186) LANGUAGE chose one whose files are not made yet. */
        if (game.language_wanted >= 0) {
            offer_language(&game, 0, NULL);
        }

        if (trace) {
            fprintf(stderr, "%6ld  dt %6.2f ms  est %6.2f  %s  ticks %d%s\n",
                    frame_no, dt * 1000.0, estimate * 1000.0,
                    locked ? "locked" : "free  ", ticks,
                    (ticks == 1) ? "" : "  <--");
        }
        frame_no++;

        /* (build 100b) What COMPUTER PLAYS shows is looked up as the menu
         * opens; (100c) closing it with the row's lamp lit hands the game
         * over. */
        bool closed = menu_was_open && !game.sysmenu.open;
        if (game.sysmenu.open && !menu_was_open) {
            computer_menu_refresh(&game.sysmenu);
            /* (build 160) The menu stills the game's shakes as it opens;
             * its own test buzz is all it shows. */
            rumble_quiet(&g_rumble);
            memset(g_rumble_test, 0, sizeof g_rumble_test);
        }
        menu_was_open = game.sysmenu.open;
        if (closed && game.sysmenu.computer_on) {
            if (computer_begin(&game, &video, &audio)) {
                input_clear(&pending);
            }
        }
        apply_settings(&game, plat, &audio);
        rumble_frame(&game, plat, false);

        draw(&game, &video, &fb);

        double work = wall_seconds() - work_start;

        platform_present(plat, &fb, &video.palette);

        if (pace) {
            rep_locked  += locked ? 1 : 0;
            rep_offbeat += (ticks != 1) ? 1 : 0;
            rep_long    += (dt > TICK_SECONDS * 1.5) ? 1 : 0;
            rep_work     = (work > rep_work) ? work : rep_work;
            rep_dt_min   = (dt < rep_dt_min) ? dt : rep_dt_min;
            rep_dt_max   = (dt > rep_dt_max) ? dt : rep_dt_max;

            if (frame_no % PACE_REPORT_FRAMES == 0) {
                fprintf(stderr, "pace: %d/%d locked  est %.2f ms  "
                                "dt %.1f..%.1f  off-beat %d  long %d  "
                                "work max %.2f ms\n",
                        rep_locked, PACE_REPORT_FRAMES, estimate * 1000.0,
                        rep_dt_min * 1000.0, rep_dt_max * 1000.0,
                        rep_offbeat, rep_long, rep_work * 1000.0);
                rep_locked = rep_offbeat = rep_long = 0;
                rep_work = 0.0;
                rep_dt_min = 1.0;
                rep_dt_max = 0.0;
            }
        }

        if (game.quit) {
            break;
        }
    }

    if (autoplay_running(&g_autoplay)) {
        autoplay_stop(&g_autoplay);
    }
    platform_destroy(plat);
    audio_shutdown(&audio);
    return 0;
}
