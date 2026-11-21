/* Testing harness: the real game, headless, driven by a route script.
 *
 *     make route_probe
 *     ./route_probe routes/darum.route              # -> route_out/darum/
 *     ./route_probe -o /tmp/x -v routes/darum.route
 *     make routes                                   # every route in routes/
 *
 * CORE + GAME only: no platform, no pacing, no display. The tick counter
 * is the probe's own -- one per game_tick, from 0 -- and never has to
 * match real time. Nothing is drawn except where a script says `shot`,
 * and then exactly what main.c shows: game_render, video_compose,
 * game_overlay.
 *
 * A route is one command per line; `#` starts a comment. Numbers take
 * decimal, 0x hex or $hex.
 *
 *   start new                   power on at the SEGA screen, no saves
 *   start slot N [FILE]         CONTINUE from slot N: saves/slotN.json (or
 *                               FILE), copied into the route's own save
 *                               directory first, then Data Memory's
 *                               "We meet again" as loc_D3B6 does
 *   start level L CX CY         a new game on that 16px cell (PS2_LEVEL)
 *   start building B P F764 F766 [CX CY]            (PS2_BUILDING)
 *   start battle F              a new game in formation F (PS2_BATTLE)
 *
 *   press a|b|c|start|up|down|left|right|esc
 *                               one tick down, one tick up: a pad cannot
 *                               report a new press of the same button on
 *                               two fields running. `esc` (build 100) is
 *                               the Escape key, the port's own menu
 *   walk left|right|up|down N   hold the direction until N steps have
 *                               begun, then let go (the last step plays
 *                               out under the next command). Ends early,
 *                               without failing, when the game takes the
 *                               pad -- a door, a message, a battle -- and
 *                               fails when the leader stands blocked.
 *
 *   wait idle                   until the game waits on the pad and on
 *                               nothing else (GameWait in game.h)
 *   wait screen NAME            until NAME is up, loaded and faded in
 *   wait level L                until level L is up, loaded and faded in
 *   wait ending MOMENT          until the ending screen is at MOMENT:
 *                               setup fade_in pageN_draw|slide|text|
 *                               cleared (N 1-6) last_page_hold wipe
 *                               last_picture to_white white
 *                               fade_in_again final_days_slide|text|
 *                               cleared credits ufo button
 *   wait ticks N                N ticks, nothing pressed
 *   wait step                   until the leader is at rest on a cell
 *
 *   shot NAME                   NAME.png, the frame as main.c draws it
 *
 *   expect screen NAME          sega title intro level building battle
 *                               gameover
 *   expect level L [CX CY]      on level L (on that cell, if given)
 *   expect script $XXXX         shown since the last script this matched
 *                               (so a run of them checks their order)
 *   expect no-stderr TEXT       no stderr line so far contains TEXT
 *   expect text "TEXT"          (build 131a) the text window's last script
 *                               contains TEXT, as the script files write
 *                               it: characters, {XX} a code in hex
 *   expect pixel X Y $BGR       the frame `shot` would write has that
 *                               CRAM word at screen pixel X, Y
 *   expect object $XX STATE     a scene object by ObjID_ on this level is
 *                               visible, hidden or absent
 *   expect riding yes|no        the party is on the jet scooter, or not
 *   expect setting fullscreen on|off
 *                               (build 100) the Escape menu's FULLSCREEN;
 *                               (100b) speed normal|fast, its SPEED;
 *                               (100c) computer on|off, COMPUTER PLAYS's
 *                               lamp
 *   expect escape-menu open|closed
 *                               (build 100) the Escape menu is up, or not
 *   expect music $XX            the last music id to reach the sequencer
 *                               (every one is logged: "music $90")
 *   expect menu $XX [STOP]      the pad is held by window $XX's poll (the
 *                               `state` line's menu), its cursor on STOP
 *   expect menu none            (build 97d) no window is open or holds
 *                               the pad
 *   expect enemy-joined         an enemy has come into a fight since the
 *                               last match (a copy loc_2F6C made)
 *   expect command KIND         a character has begun an attack, tech,
 *                               item or defend command in a fight since
 *                               the last `expect command KIND` matched
 *   expect stat MEMBER STAT LO [HI]
 *                               (build 95a) party position MEMBER's
 *                               hp, tp, attack, defense or agility is
 *                               LO, or within LO..HI (`expect hp` is
 *                               the older form for HP)
 *   expect rumble KIND N        (build 160) the rumble (src/game/rumble.h)
 *                               felt KIND -- hurt, fell, lost, floor -- N
 *                               times since the last `expect rumble KIND`
 *   expect rumble going|still   (build 160) its motors now
 *   expect setting rumble LEVEL (build 160) the GAMEPAD page's RUMBLE:
 *                               off, low, medium, high
 *   rumble-log NAME LEVEL       (build 160) from now on NAME in the output
 *                               directory: "TICK 1 LOW HIGH" each tick the
 *                               motors at LEVEL (0-3) change, as the
 *                               headless game's PS2_RUMBLE log has them
 *   key NAME [TICKS]            (build 160) a key pressed as it is (keys.c's
 *                               names: Q, SPACE, ESCAPE, BACKSPC ...), held
 *                               TICKS ticks (1), then a tick of nothing --
 *                               what the platform makes of it through the
 *                               Escape menu's controls (its actions; Escape
 *                               the menu, Backspace back, the arrows and
 *                               Enter the menu's own), and Input.key for
 *                               the CONTROLS page
 *   pad NAME [TICKS]            (build 160) the same for a gamepad input by
 *                               its settings.json name (SOUTH, DUP, LSUP, L2,
 *                               JOY3 ...): Input.pad_input for the BUTTONS
 *                               page; Back the menu, the d-pad and left
 *                               stick the menu's own
 *   pad-style STYLE             (build 160) whose labels PAD 1 wears for the
 *                               BUTTONS page (none, ps3, xboxone, switch ...)
 *   expect keys|buttons ACTION SLOT1 / SLOT2
 *                               (build 160) what the CONTROLS page (keys) or
 *                               the BUTTONS page (buttons) shows in ACTION's
 *                               row (up ... start), each slot as drawn, "-"
 *                               for none: `expect buttons up DPAD UP / LS UP`
 *
 *   limit N                     ticks any later wait or walk may take
 *                               before it fails (default 3600)
 *
 *   fuzz TICKS [SEED]           play by itself for TICKS ticks: at every
 *                               wait, an input that wait can take (walks
 *                               and A on the map, C through text, cursor
 *                               moves then C or B in menus, Start on the
 *                               title and the pause). The battle's own
 *                               menus are played on purpose: FIGHT or
 *                               STGY, ORDR, a member, any of the four
 *                               commands, any list entry or group. Fails
 *                               only when the game stays busy past the
 *                               limit; stderr and any sanitizer report
 *                               land in the log.
 *
 *   party N LEVEL               right after a start: the first N members
 *                               at LEVEL (see game_debug_party)
 *   meseta N                    the party's money is N
 *   roster                      one log line: everyone who has joined,
 *                               in the party or at home, NAME:Llevel:
 *                               hp/max:tp/max, '*' for a party member
 *   objects                     one log line: every live object on the
 *                               level, slot:object:type/index:cell;
 *                               `expect person $XX present|absent`
 *                               looks for dialogue number $XX there
 *   flag $C7XX N                one byte of the $C700 block is N (read
 *                               wherever the game next looks: an
 *                               object's init at the next level load);
 *                               `expect flag $C7XX N` checks one
 *   warp LEVEL CX CY            through a door to that cell, the game
 *                               kept
 *   oneshot $XX                 (build 97d) the event word's high byte at
 *                               the next level load -- loc_7E02's case,
 *                               as `move.w #$XX00, (event_flags).w` just
 *                               before a reload leaves it
 *   give ITEM [MEMBER]          an ItemID_ name (Dynamite, SmallKey, ...)
 *                               into party position MEMBER's bag (the
 *                               leader's by default), as a chest would
 *                               hand it over; fails when the bag is full
 *                               or the name is unknown
 *   keep-equipment              the fuzz never takes EQP
 *   techs                       one log line: each member's stored battle
 *                               command and battle techniques,
 *                               NAME:TP:flags in list order
 *   state / bags                one log line of game state, or the bags
 *                               (see the README, "Playing it")
 *
 * Output, in the directory: the PNGs, log.txt (every command with the
 * tick it ran on, every script id, screen and level change, and all of
 * stderr, each stamped with its tick), summary.txt, and saves/ (the
 * route's own slots -- the player's saves/ is never touched). The
 * summary line also goes to stdout; the exit code is 0 for a pass, 1 for
 * a failure, 2 for a script or setup error.
 *
 * (build 171) `route_probe --play [ARGS]`: the C player (src/player/)
 * plays tools/playthrough.py's game with playthrough.py's ARGS, its lines
 * given to the interpreter as `-i` reads them from stdin, into
 * route_out/playthrough_s<N>[_KEY]/probe/, or route_out/NAME/probe/ with
 * --base NAME (docs/c_player.md).
 *
 * (build 179) `route_probe --computer [ARGS]`: the C player plays the
 * window's COMPUTER PLAYS (tools/computer_plays.py's game and its ARGS)
 * on a host that is the window's -- only a player's commands, `start new`
 * the console's reset, `start slot` a save copied in and the title's
 * CONTINUE by the pad -- its files under --out (saves/computer) as the
 * window's are, the probe's own in OUT/probe/. `--language KEY` among the
 * ARGS is the game's settings.json's, as the window's language is.
 *
 * (build 160) PS2_RUMBLE_CHECK=FILE: the rumble's watcher held to the
 * game every tick, the report in FILE (rumble_rules_tick, below);
 * tools/rumble_check.py runs it over every route and the fuzz.
 *
 * POSIX (dup2, pread, mkdir, opendir): stderr is captured by pointing
 * file descriptor 2 at a file in the output directory and reading it back
 * after every tick. */
#define _POSIX_C_SOURCE 200809L

#include "../src/core/assetfs.h"
#include "../src/core/audio.h"
#include "../src/core/pngwrite.h"
#include "../src/core/video.h"
#include "../src/game/game.h"
#include "../src/game/gamedata.h"
#include "../src/game/rom_ids.h"
#include "../src/game/building.h"
#include "../src/game/rumble.h"
#include "../src/game/walk.h"
#include "../src/game/window.h"
#include "../src/harness/probe.h"
#include "../src/player/player.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* (build 113) Windows (MSYS2 UCRT64): mkdir takes the path alone, there is
 * no pread, and a file opened without O_BINARY turns "\n" into "\r\n". */
#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#define CAP_OPEN_FLAGS (O_RDWR | O_CREAT | O_TRUNC | O_BINARY)
#else
#define make_dir(path) mkdir(path, 0755)
#define CAP_OPEN_FLAGS (O_RDWR | O_CREAT | O_TRUNC)
#endif

/* (build 100b) The interpreter is src/harness/probe.[ch], shared with the
 * game's COMPUTER PLAYS. What stays here is a test rig's: route files, the
 * output directory, the PNGs, stderr captured into the log, the summary. */
#define LINE_MAX_LEN   PROBE_LINE_MAX
#define PATH_LEN       PROBE_PATH_LEN
#define OUT_LEN        PROBE_OUT_LEN

typedef ProbeCmd   Cmd;
typedef ProbeRoute Route;

static Video       g_video;
static Game        g_game;
static Audio       g_audio;
/* (build 160) The rumble's watcher, after every tick, for `expect rumble`
 * and `rumble-log` (ProbeHost.rumble). It only reads the game. */
static Rumble      g_rumble;

static FILE       *g_log;
static FILE       *g_err;       /* the real stderr, for the probe's own */
static bool        g_verbose;

static int         g_cap_fd = -1;
#ifdef _WIN32
static int         g_cap_rd = -1;   /* its own offset: see capture_poll */
#endif
static off_t       g_cap_off;
static char        g_cap_partial[LINE_MAX_LEN];
static size_t      g_cap_partial_len;
static char        g_cap_path[PATH_LEN];

/* (build 140) The colour plane (video.h). PS2_DRAW=colour: the shots are
 * drawn from it, RGB PNGs, instead of from the index plane through the
 * palette. PS2_COLOUR_CHECK=DIR: every tick's frame is composed as main.c
 * composes it and its colour plane held to the palette path pixel for
 * pixel, each shot is written a second time from the colour plane into
 * DIR/<route>/, and DIR/<route>.txt says what was found -- nothing in the
 * route's own files changes. */
static FbDraw      g_draw;
static const char *g_colour_dir;
static char        g_colour_name[OUT_LEN];
static Framebuffer g_colour_fb;
static u32         g_colour_frames;
static u32         g_colour_bad;
static u32         g_colour_shots;
static u32         g_colour_shots_bad;
static char        g_colour_first[256];
/* (build 142) The pictures the frames drew from (video.h's `pic`): pixels,
 * frames, and the things by name, so a route's report shows its portraits
 * came from their pictures and not from their tiles. */
static u64         g_colour_picture_pixels;
static u32         g_colour_picture_frames;
/* (build 146) Every tick's colour plane, what the game shows, into one
 * FNV-1a hash: two runs of a route with the same hash showed the same
 * colours on every frame. The proof for a change meant to move no pixel
 * where the index plane or the files' form changes (the portraits drawn
 * without their tiles). */
static u64         g_colour_hash = 0xCBF29CE484222325ull;

static void colour_hash_frame(const Framebuffer *fb)
{
    const u8 *b = (const u8 *)fb->rgb;
    u64       h = g_colour_hash;
    for (size_t i = 0; i < sizeof fb->rgb; i++) {
        h = (h ^ b[i]) * 0x100000001B3ull;
    }
    g_colour_hash = h;
}
#define COLOUR_THINGS 16
static char        g_colour_things[COLOUR_THINGS][32];
static int         g_colour_thing_count;

static void colour_pictures_seen(const Framebuffer *fb)
{
    if (fb->picture_pixels == 0) {
        return;
    }
    g_colour_picture_pixels += fb->picture_pixels;
    g_colour_picture_frames++;
    bool used[VIDEO_PICTURES_MAX] = { false };
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        if (fb->pic[i] != 0) {
            used[(fb->pic[i] - 1) % VIDEO_PICTURES_MAX] = true;
        }
    }
    for (int b = 0; b < VIDEO_PICTURES_MAX; b++) {
        const Picture *p = fb->pictures[b];
        if (!used[b] || p == NULL) {
            continue;
        }
        bool known = false;
        for (int t = 0; t < g_colour_thing_count; t++) {
            if (strcmp(g_colour_things[t], p->thing) == 0) {
                known = true;
            }
        }
        if (!known && g_colour_thing_count < COLOUR_THINGS) {
            snprintf(g_colour_things[g_colour_thing_count++],
                     sizeof g_colour_things[0], "%s", p->thing);
        }
    }
}


/* ------------------------------------------------------------------ */
/* Files                                                              */
/* ------------------------------------------------------------------ */

static bool make_dirs(const char *path)
{
    char buf[PATH_LEN];
    snprintf(buf, sizeof buf, "%s", path);

    for (char *p = buf + 1; ; p++) {
        if (*p == '/' || *p == '\0') {
            char was = *p;
            *p = '\0';
            if (make_dir(buf) != 0 && errno != EEXIST) {
                return false;
            }
            *p = was;
            if (was == '\0') {
                break;
            }
        }
    }
    return true;
}

static bool ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s);
    size_t b = strlen(tail);
    return a >= b && strcmp(s + a - b, tail) == 0;
}

/* What an earlier run left: its shots, its log and summary, its slots.
 * Nothing else in the directory is touched. */
static void clear_previous(const char *out, const char *saves)
{
    char path[PATH_LEN + OUT_LEN];
    DIR *d = opendir(out);

    if (d != NULL) {
        for (struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
            if (ends_with(e->d_name, ".png")
                || strcmp(e->d_name, "log.txt") == 0
                || strcmp(e->d_name, "summary.txt") == 0) {
                /* (build 113) never remove a truncated path */
                if (snprintf(path, sizeof path, "%s/%s", out, e->d_name)
                    < (int)sizeof path) {
                    remove(path);
                }
            }
        }
        closedir(d);
    }
    d = opendir(saves);
    if (d != NULL) {
        for (struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
            if (strncmp(e->d_name, "slot", 4) == 0) {
                /* (build 113) never remove a truncated path */
                if (snprintf(path, sizeof path, "%s/%s", saves, e->d_name)
                    < (int)sizeof path) {
                    remove(path);
                }
            }
        }
        closedir(d);
    }
}

/* The framebuffer as it is: its indices, the game's 64 colours and the
 * Escape menu's 16 after them, through the DAC ladder as the platform
 * shows them. An indexed PNG, like every asset. (build 105) The writer
 * is src/core/pngwrite.c now, the same code. */
static bool write_png(const char *path, const Framebuffer *fb,
                      const Palette *pal)
{
    /* (build 146a) A frame with a picture in it has pixels no index
     * names (fb_palette_colour): RGB then, each pixel the palette path's
     * colour. */
    if (fb->picture_pixels > 0) {
        static Rgb8 rgb[SCREEN_W * SCREEN_H];
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
            rgb[i] = fb_palette_colour(fb, pal, i);
        }
        return png_write(path, SCREEN_W, SCREEN_H, 8, 2, (const u8 *)rgb,
                         SCREEN_W * 3, NULL, 0, NULL);
    }
    u8 plte[FB_INDICES * 3];
    for (int i = 0; i < FB_INDICES; i++) {
        Cram cram = (i < PAL_TOTAL) ? pal->entry[i]
                                    : fb->overlay[i - FB_OVERLAY_BASE];
        Rgb8 c = cram_to_rgb8(cram);
        plte[i * 3 + 0] = c.r;
        plte[i * 3 + 1] = c.g;
        plte[i * 3 + 2] = c.b;
    }
    return png_write(path, SCREEN_W, SCREEN_H, 8, 3, fb->px, SCREEN_W,
                     plte, FB_INDICES, NULL);
}

/* (build 140) The colour plane as it is: an RGB PNG, 8 bits a channel. */
_Static_assert(sizeof(Rgb8) == 3, "the colour plane is RGB triples");

static bool write_png_colour(const char *path, const Framebuffer *fb)
{
    return png_write(path, SCREEN_W, SCREEN_H, 8, 2, (const u8 *)fb->rgb,
                     SCREEN_W * 3, NULL, 0, NULL);
}

/* (build 140) One composed frame held to the palette path; the first
 * difference of the run is kept for the report. */
static void colour_check_frame(const Framebuffer *fb, const Palette *pal,
                               const char *what)
{
    int first = -1;
    int n = fb_colour_differs(fb, pal, &first);
    if (n == 0 || g_colour_first[0] != '\0') {
        return;
    }
    Rgb8 want = fb_palette_colour(fb, pal, first);
    Rgb8 got  = fb->rgb[first];
    snprintf(g_colour_first, sizeof g_colour_first,
             "first at tick %u (%s): %d pixels, the first (%d,%d) index %u "
             "palette #%02X%02X%02X colour plane #%02X%02X%02X",
             probe_ticks(), what, n, first % SCREEN_W, first / SCREEN_W,
             (unsigned)fb->px[first], want.r, want.g, want.b,
             got.r, got.g, got.b);
}

/* ---- stderr ---- */

static bool capture_begin(const char *out)
{
    snprintf(g_cap_path, sizeof g_cap_path, "%s/.stderr", out);
    int fd = open(g_cap_path, CAP_OPEN_FLAGS, 0644);
    if (fd < 0) {
        return false;
    }
#ifdef _WIN32
    g_cap_rd = open(g_cap_path, O_RDONLY | O_BINARY);
    if (g_cap_rd < 0) {
        close(fd);
        return false;
    }
#endif
    int real = dup(STDERR_FILENO);
    if (real < 0) {
        close(fd);
#ifdef _WIN32
        close(g_cap_rd);
        g_cap_rd = -1;
#endif
        return false;
    }
    g_err = fdopen(real, "w");
    setvbuf(g_err, NULL, _IONBF, 0);
    fflush(stderr);
    dup2(fd, STDERR_FILENO);
    setvbuf(stderr, NULL, _IONBF, 0);
    g_cap_fd  = fd;
    g_cap_off = 0;
    return true;
}

/* Whatever the game wrote to fd 2 since the last call, a line at a time,
 * stamped with the tick that wrote it. */
static void capture_poll(bool flush_partial)
{
    char buf[4096];

    if (g_cap_fd < 0) {
        return;
    }
    fflush(stderr);
    for (;;) {
#ifdef _WIN32
        /* fd 2 shares g_cap_fd's offset (dup2), so a seek there would move
         * where the game writes; g_cap_rd was opened apart and has its
         * own, always at g_cap_off. */
        ssize_t n = read(g_cap_rd, buf, sizeof buf);
#else
        ssize_t n = pread(g_cap_fd, buf, sizeof buf, g_cap_off);
#endif
        if (n <= 0) {
            break;
        }
        g_cap_off += n;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                g_cap_partial[g_cap_partial_len] = '\0';
                probe_stderr_line(g_cap_partial);
                g_cap_partial_len = 0;
            } else if (g_cap_partial_len + 1 < sizeof g_cap_partial) {
                g_cap_partial[g_cap_partial_len++] = buf[i];
            }
        }
    }
    if (flush_partial && g_cap_partial_len > 0) {
        g_cap_partial[g_cap_partial_len] = '\0';
        probe_stderr_line(g_cap_partial);
        g_cap_partial_len = 0;
    }
}

static void capture_end(void)
{
    if (g_cap_fd < 0) {
        return;
    }
    capture_poll(true);
    fflush(stderr);
    dup2(fileno(g_err), STDERR_FILENO);
    close(g_cap_fd);
    g_cap_fd = -1;
#ifdef _WIN32
    close(g_cap_rd);    /* Windows cannot remove an open file */
    g_cap_rd = -1;
#endif
    remove(g_cap_path);
}

/* ---- the host (src/harness/probe.h) ---- */

static void drain_audio(void)
{
    atomic_store(&g_audio.tail, atomic_load(&g_audio.head));
}

static void host_tick(void *ctx, const Input *in)
{
    (void)ctx;
    game_tick(&g_game, in);
}

static void rumble_rules_tick(void);

static void host_after_tick(void *ctx)
{
    (void)ctx;
    RumbleMotion mo;
    game_motion(&g_game, &mo);                  /* (build 161) */
    rumble_watch(&g_rumble, game_debug_data(&g_game), (int)g_game.screen,
                 g_game.audio, &mo);
    rumble_rules_tick();
    drain_audio();
    capture_poll(false);
    /* (build 140) PS2_COLOUR_CHECK: this tick's frame, as main.c draws it
     * after a tick, both ways. */
    if (g_colour_dir != NULL) {
        game_render(&g_game);
        video_compose(&g_video, &g_colour_fb);
        game_overlay(&g_game, &g_colour_fb);
        g_colour_frames++;
        colour_pictures_seen(&g_colour_fb);
        colour_hash_frame(&g_colour_fb);
        if (fb_colour_differs(&g_colour_fb, &g_video.palette, NULL) != 0) {
            g_colour_bad++;
            colour_check_frame(&g_colour_fb, &g_video.palette, "a tick");
        }
    }
}

/* (build 171) --play: the player's session, which reads the log back as
 * the Python reads `-i`'s stdout; the lines it gives, numbered as -i
 * numbers stdin's. */
static Session    *g_play;
static bool        g_computer;      /* (build 179) --computer */
static const char *g_computer_language;
static int         g_play_lineno;
static int         g_play_ran;
static const char *g_play_out;
static const char *g_play_saves;

static void play_run(void *ctx, const char *line)
{
    (void)ctx;
    char buf[LINE_MAX_LEN];
    snprintf(buf, sizeof buf, "%s\n", line);
    g_play_lineno++;
    if (probe_run_text(buf, g_play_lineno, g_play_out, g_play_saves) != 0) {
        g_play_ran++;
    }
}

static void host_log(void *ctx, u32 tick, const char *line)
{
    (void)ctx;
    if (g_play != NULL) {
        player_log_line(g_play, tick, line);
    }
    fprintf(g_log, "%7u  %s\n", tick, line);
    if (g_verbose) {
        printf("%7u  %s\n", tick, line);
        fflush(stdout);
    }
}

static bool host_write_png(void *ctx, const char *path, const Framebuffer *fb,
                           const Palette *pal)
{
    (void)ctx;
    bool ok = (g_draw == FB_DRAW_COLOUR) ? write_png_colour(path, fb)
                                         : write_png(path, fb, pal);
    /* (build 140) PS2_COLOUR_CHECK: the shot again, from the colour plane,
     * beside the route's own for tools/colour_check.py. */
    if (ok && g_colour_dir != NULL) {
        const char *base = strrchr(path, '/');
        base = (base != NULL) ? base + 1 : path;
        char dir[PATH_LEN + OUT_LEN];
        char again[PATH_LEN + OUT_LEN + LINE_MAX_LEN];
        snprintf(dir, sizeof dir, "%s/%s", g_colour_dir, g_colour_name);
        snprintf(again, sizeof again, "%s/%s", dir, base);
        if (!make_dirs(dir) || !write_png_colour(again, fb)) {
            fprintf(g_err, "route_probe: cannot write %s\n", again);
        }
        g_colour_shots++;
        if (fb_colour_differs(fb, pal, NULL) != 0) {
            g_colour_shots_bad++;
            colour_check_frame(fb, pal, base);
        }
    }
    return ok;
}

/* ---- (build 160) PS2_RUMBLE_CHECK: the rumble held to the game ----
 *
 * PS2_RUMBLE_CHECK=FILE: after every tick, what the watcher felt (g_rumble)
 * held against what the game itself did that tick -- the sound ids it
 * asked for (the watcher's copy of audio_take_asked's record) and each
 * party member's HP, read here apart from the watcher -- both ways:
 *
 *   in a fight   a member hurt only with SFX_SWORD (a blow landing) or a
 *                blow owed RUMBLE_PENDING_MAX ticks, and the member's HP
 *                below where the last blow felt left it; SFX_SWORD with a
 *                member's HP below that: the member hurt; fell only with
 *                the HP at 0 and hurt; an HP drop owed past
 *                RUMBLE_PENDING_MAX + 1 ticks: never felt
 *   on the map   floor exactly with SFX_DANGEROUS_FLOOR; a floor's point
 *                -- a point of HP within 12 ticks of it, off a member
 *                standing at its flash -- not felt again, and the floor's
 *                points all taken within 12 ticks; any other HP lost
 *                (Roron's cake) felt as hurt, and hurt felt only so; a
 *                member down to 0: fell
 *   elsewhere    nothing felt: HP lower there is a new game's or a load's
 *                (counted, and the first named in FILE)
 *   game over    lost exactly as the screen becomes the game over
 *   the motors   going on a tick that felt anything; going only within
 *                RUMBLE_LONGEST ticks of something felt
 *
 * (build 161) And, from the effect the sound driver started each tick
 * (Smps.sfx_started, sfx_starts) and the game's motion (game_motion),
 * read here apart from the watcher:
 *
 *   blast, hole  felt exactly on the ticks SFX_EXPLOSION, SFX_FELL_IN_HOLE
 *                start
 *   flight       felt exactly on the ticks a ship flies (the spaceship
 *                fading in or in SpaceShipLoop, Tyler's ship), the pause
 *                aside; shake exactly on those the camera shakes; the
 *                motors going on each; paused in a flight 40 ticks, still
 *   landed       felt exactly as SpaceShipLoop gives way to the level
 *   a blow       the small motor going as it is felt
 *   followed     from a followed effect's start (an explosion, the hole,
 *                the ship's departure and landing, a red flash's sound in
 *                a fight within RUMBLE_RED_SPAN ticks of a blow, the sword
 *                the tick after a blow), while the
 *                effect tracks it took (6-8, in use as it started) play:
 *                at every tick it is heard, the big motor at 40% of full
 *                by its loudness (its volume against where it began, 0.75
 *                dB a step, 30 dB to nothing) or more; where nothing but
 *                sounds was felt for RUMBLE_LONGEST ticks and nothing
 *                flies or shakes, the big motor no higher than its
 *                loudness, and once it is over (ended or cut) more than
 *                RUMBLE_TAIL ticks and a tick, both still
 *
 * and FILE says what was felt and what broke (the first 20), for
 * tools/rumble_check.py: "felt hurt N fell N lost N floor N late N; HP
 * drops elsewhere N (off the map and the fight); T ticks; B broken", with
 * a line before it for each rule broken and each HP drop elsewhere (the
 * first 20 of each).
 * Nothing in the route's own files changes. */
#define RULES_FLOOR_TICKS 12
#define RULES_SHOWN       20

static const char *g_rules_path;
static bool        g_rules_primed;
static int         g_rules_screen;
static u16         g_rules_hp[RUMBLE_CHARS];
static u16         g_rules_landed[RUMBLE_CHARS];   /* HP at the last hurt */
static int         g_rules_owed[RUMBLE_CHARS];
static bool        g_rules_floor_open;
static u32         g_rules_floor_tick;
static u16         g_rules_floor_hp[RUMBLE_CHARS];
static u8          g_rules_floor_left;   /* bit a member still owing */
static u32         g_rules_tick;
static u32         g_rules_last_felt;
static u32         g_rules_elsewhere;
static char        g_rules_elsewhere_first[RULES_SHOWN][96];
static u32         g_rules_broken;
static char        g_rules_shown[RULES_SHOWN][160];
/* (build 161) */
#define RULES_PAUSE_STILL 40
static u8          g_rules_starts;          /* Smps.sfx_starts, last seen */
static bool        g_rules_follow_due;
static u8          g_rules_follow_id;
static u8          g_rules_follow_tracks;   /* bit t - 6, in use at its start */
static u8          g_rules_follow_vol0[3];
static bool        g_rules_follow_was;
static u32         g_rules_since_follow;
static u32         g_rules_since_fixed;
static u32         g_rules_since_cut;
static int         g_rules_flight;
static u32         g_rules_paused_flying;
static bool        g_rules_hurt_last;       /* a blow felt last tick, in a fight */
static u32         g_rules_since_blow = 1000;  /* since a blow's kick began */
#define RULES_KICK 4                        /* a blow's kick, the small motor */
static u32         g_rules_followed;        /* ticks a followed sound was heard */

static void rules_broke(int who, const char *what)
{
    if (g_rules_broken < RULES_SHOWN) {
        snprintf(g_rules_shown[g_rules_broken], sizeof g_rules_shown[0],
                 "tick %u member %d screen %d: %s", g_rules_tick, who,
                 g_rules_screen, what);
    }
    g_rules_broken++;
}

/* (build 161) The followed effect's loudness now, 0-1000: the loudest of
 * its tracks in use and not at rest, by its volume against where it
 * began; -1 when none of them plays. */
static int rules_loudness(const Audio *au)
{
    int loud = -1;
    for (int i = 0; i < 3; i++) {
        if (!(g_rules_follow_tracks & (1u << i))) {
            continue;
        }
        const SmpsTrack *t = &au->smps.tracks[SMPS_MUSIC_TRACKS + i];
        if (!(t->f.flags & SMPS_F_IN_USE)) {
            continue;
        }
        loud = (loud < 0) ? 0 : loud;
        if (t->f.flags & SMPS_F_HOLD) {
            continue;
        }
        int l = 1000 - ((int)t->f.volume - (int)g_rules_follow_vol0[i]) * 25;
        l = (l < 0) ? 0 : (l > 1000) ? 1000 : l;
        loud = (l > loud) ? l : loud;
    }
    return loud;
}

/* (build 161) The new kinds and the followed sounds (the head above). */
static void rules_sounds(const Rumble *r, RumbleMotors m, int screen)
{
    RumbleMotion mo;
    game_motion(&g_game, &mo);
    const Audio *au = g_game.audio;
    bool ready = au != NULL && au->smps_ready;
    bool going = m.low != 0 || m.high != 0;
    u8 started = 0;
    if (ready) {
        if (au->smps.sfx_starts != g_rules_starts) {
            started = au->smps.sfx_started;
        }
        g_rules_starts = au->smps.sfx_starts;
    }
    bool battle = (screen == SCREEN_BATTLE);
    if (g_rules_since_blow < 100000) {
        g_rules_since_blow++;
    }
    bool red_ok = g_rules_since_blow <= RUMBLE_RED_SPAN;
    if (((r->felt & RUMBLE_FELT_BLAST) != 0) != (started == SFX_EXPLOSION)) {
        rules_broke(-1, "blast felt, or not, against the explosion starting");
    }
    if (((r->felt & RUMBLE_FELT_HOLE) != 0)
        != (started == SFX_FELL_IN_HOLE)) {
        rules_broke(-1, "hole felt, or not, against the fall's sound "
                        "starting");
    }
    bool ship = mo.flight == RUMBLE_FLIGHT_FADE_IN
             || mo.flight == RUMBLE_FLIGHT_LOOP
             || mo.building == BUILDING_TYLER_SHIP;
    bool flying = ship && !mo.paused;
    bool shaking = mo.shake != RUMBLE_SHAKE_NONE && !mo.paused;
    if (((r->felt & RUMBLE_FELT_FLIGHT) != 0) != flying) {
        rules_broke(-1, "flight felt, or not, against a ship flying");
    }
    if (((r->felt & RUMBLE_FELT_SHAKE) != 0) != shaking) {
        rules_broke(-1, "shake felt, or not, against the camera shaking");
    }
    bool landed = g_rules_flight == RUMBLE_FLIGHT_LOOP
               && mo.flight == RUMBLE_FLIGHT_DONE;
    if (((r->felt & RUMBLE_FELT_LANDED) != 0) != landed) {
        rules_broke(-1, "landed felt, or not, against the flight's end");
    }
    g_rules_flight = mo.flight;
    if ((flying || shaking) && !going) {
        rules_broke(-1, "a ship flying or the camera shaking, motors still");
    }
    if (ship && mo.paused) {
        if (++g_rules_paused_flying > RULES_PAUSE_STILL && going) {
            rules_broke(-1, "a flight paused 40 ticks, motors going");
        }
    } else {
        g_rules_paused_flying = 0;
    }
    if ((r->felt & RUMBLE_FELT_HURT) && m.high == 0) {
        rules_broke(-1, "a blow felt, the small motor still");
    }

    /* the followed effect: from its start, while its tracks play */
    bool sword = started == SFX_SWORD && g_rules_hurt_last;
    bool follows = started == SFX_EXPLOSION || started == SFX_FELL_IN_HOLE
                || started == SFX_SPACESHIP_DEPARTED
                || started == SFX_SPACESHIP_LANDED
                || (started == SFX_DAMAGE_RED_SCREEN && battle && red_ok)
                || sword;
    bool unheard = g_rules_hurt_last && started != SFX_SWORD;
    if (started != 0) {
        g_rules_follow_due = follows;
        g_rules_follow_id  = started;
        g_rules_follow_tracks = 0;
        for (int i = 0; i < 3 && ready; i++) {
            const SmpsTrack *t = &au->smps.tracks[SMPS_MUSIC_TRACKS + i];
            if (t->f.flags & SMPS_F_IN_USE) {
                g_rules_follow_tracks |= (u8)(1u << i);
            }
            g_rules_follow_vol0[i] = t->f.volume;
        }
    }
    int loud = (g_rules_follow_due && ready) ? rules_loudness(au) : -1;
    bool on_map_blow = (r->felt & RUMBLE_FELT_HURT) && !battle;
    if ((r->felt & (RUMBLE_FELT_FELL | RUMBLE_FELT_LOST | RUMBLE_FELT_FLOOR
                    | RUMBLE_FELT_SHAKE | RUMBLE_FELT_FLIGHT
                    | RUMBLE_FELT_LANDED)) || on_map_blow || unheard) {
        g_rules_since_fixed = 0;        /* a shake not a sound's */
    } else {
        g_rules_since_fixed++;
    }
    if (g_rules_follow_was && (loud < 0 || started != 0)) {
        g_rules_since_cut = 0;
    } else {
        g_rules_since_cut++;
    }
    g_rules_follow_was   = loud >= 0;
    g_rules_since_follow = (loud >= 0) ? 0 : g_rules_since_follow + 1;
    bool alone = g_rules_since_fixed > RUMBLE_LONGEST && !ship
              && mo.shake == RUMBLE_SHAKE_NONE;
    if (r->felt & RUMBLE_FELT_HURT) {
        g_rules_since_blow = 0;
    }
    char what[96];
    if (loud < 0 && g_rules_since_follow > RUMBLE_TAIL + 1 && alone
        && g_rules_since_blow >= RULES_KICK && going) {
        snprintf(what, sizeof what, "a followed sound ($%02X) over, nothing "
                 "else felt, motors going", g_rules_follow_id);
        rules_broke(-1, what);
    }
    if (loud >= 0 && alone && g_rules_since_cut > RUMBLE_TAIL + 1
        && m.low > (u32)0xFFFF * (u32)loud / 1000u + 1u) {
        snprintf(what, sizeof what, "the big motor louder than its sound "
                 "($%02X)", g_rules_follow_id);
        rules_broke(-1, what);
    }
    if (loud > 0 && !mo.paused) {
        g_rules_followed++;
        if (m.low < (u32)0xFFFF * 400u / 1000u * (u32)loud / 1000u) {
            snprintf(what, sizeof what, "a followed sound ($%02X) heard, the "
                     "big motor under 40%% of it", g_rules_follow_id);
            rules_broke(-1, what);
        }
    }
    if (loud < 0) {
        g_rules_follow_due = false;
    }
    g_rules_hurt_last = battle && (r->felt & RUMBLE_FELT_HURT) != 0;
}

static void rumble_rules_tick(void)
{
    if (g_rules_path == NULL) {
        return;
    }
    g_rules_tick++;
    const GameData *gd     = game_debug_data(&g_game);
    const Rumble   *r      = &g_rumble;
    int             screen = (int)g_game.screen;
    bool in_party[RUMBLE_CHARS];
    memset(in_party, 0, sizeof in_party);
    for (int i = 0; i <= gd->party_members_num && i < GD_PARTY_MAX; i++) {
        in_party[gd->party_member_id[i] & 7] = true;
    }
    if (!g_rules_primed) {
        for (int c = 0; c < RUMBLE_CHARS; c++) {
            g_rules_hp[c]     = gd->chr[c].hp;
            g_rules_landed[c] = gd->chr[c].hp;
            g_rules_owed[c]   = 0;
        }
        g_rules_screen    = screen;
        g_rules_primed    = true;
        g_rules_last_felt = g_rules_tick;
        /* (build 161) */
        RumbleMotion mo;
        game_motion(&g_game, &mo);
        g_rules_flight = mo.flight;
        if (g_game.audio != NULL && g_game.audio->smps_ready) {
            g_rules_starts = g_game.audio->smps.sfx_starts;
        }
        if (r->felt != 0) {
            rules_broke(-1, "felt as the watching began");
        }
        return;
    }
    if (r->felt != 0) {
        g_rules_last_felt = g_rules_tick;
    }
    bool sword  = rumble_asked(r, SFX_SWORD);
    bool battle = (screen == SCREEN_BATTLE);
    for (int c = 0; c < RUMBLE_CHARS; c++) {
        u16  hp    = gd->chr[c].hp;
        bool hurt  = (r->hurt_who & (1u << c)) != 0;
        bool fell  = (r->fell_who & (1u << c)) != 0;
        bool below = hp < g_rules_landed[c];
        if (!in_party[c]) {
            if (hurt || fell) {
                rules_broke(c, "felt, not in the party");
            }
            g_rules_landed[c] = hp;
            g_rules_owed[c]   = 0;
            g_rules_hp[c]     = hp;
            continue;
        }
        if (battle) {
            g_rules_owed[c] = below ? g_rules_owed[c] + 1 : 0;
            if (hurt && !(below && (sword
                                    || g_rules_owed[c] > RUMBLE_PENDING_MAX))) {
                rules_broke(c, "hurt felt: no blow landing, or no HP down");
            }
            if (!hurt && below && sword) {
                rules_broke(c, "SFX_SWORD with the HP down: not felt");
            }
            if (g_rules_owed[c] > RUMBLE_PENDING_MAX + 1) {
                rules_broke(c, "an HP drop never felt");
                g_rules_owed[c] = 0;
                g_rules_landed[c] = hp;
            }
            if (fell != (hurt && hp == 0)) {
                rules_broke(c, "fell felt, or not, against the HP at 0");
            }
            if (hurt || !below) {
                g_rules_landed[c] = hp;
                g_rules_owed[c]   = 0;
            }
        } else if (screen != SCREEN_LEVEL) {
            /* Off the fight and the map: no blow (a new game's HP, a
             * load's), nothing felt. */
            if (hurt || fell) {
                rules_broke(c, "felt off the fight and the map");
            }
            if (hp < g_rules_hp[c]) {
                if (g_rules_elsewhere < RULES_SHOWN) {
                    snprintf(g_rules_elsewhere_first[g_rules_elsewhere],
                             sizeof g_rules_elsewhere_first[0],
                             "tick %u member %d screen %d: %u to %u HP",
                             g_rules_tick, c, screen, g_rules_hp[c], hp);
                }
                g_rules_elsewhere++;
            }
            g_rules_landed[c] = hp;
            g_rules_owed[c]   = 0;
        } else {
            bool down        = hp < g_rules_hp[c];
            bool floor_point = down && screen == SCREEN_LEVEL
                            && g_rules_floor_open
                            && (g_rules_floor_left & (1u << c))
                            && hp + 1 == g_rules_floor_hp[c];
            if (floor_point) {
                g_rules_floor_left &= (u8)~(1u << c);
            }
            if (hurt != (down && !floor_point)) {
                rules_broke(c, "off the fight: hurt felt, or not, against "
                               "HP lost not a floor's point");
            }
            if (fell != (hp == 0 && g_rules_hp[c] != 0)) {
                rules_broke(c, "off the fight: fell felt, or not, against "
                               "the HP down to 0");
            }
            g_rules_landed[c] = hp;
            g_rules_owed[c]   = 0;
        }
        g_rules_hp[c] = hp;
    }

    bool floor_sound = screen == SCREEN_LEVEL
                    && rumble_asked(r, SFX_DANGEROUS_FLOOR);
    bool floor = (r->felt & RUMBLE_FELT_FLOOR) != 0;
    if (floor != floor_sound) {
        rules_broke(-1, "floor felt, or not, against SFX_DANGEROUS_FLOOR");
    }
    if (g_rules_floor_open && g_rules_floor_left == 0) {
        g_rules_floor_open = false;
    }
    if (g_rules_floor_open
        && g_rules_tick > g_rules_floor_tick + RULES_FLOOR_TICKS) {
        rules_broke(-1, "a floor's points not all taken");
        g_rules_floor_open = false;
    }
    if (floor) {
        g_rules_floor_open = true;
        g_rules_floor_tick = g_rules_tick;
        g_rules_floor_left = 0;
        for (int c = 0; c < RUMBLE_CHARS; c++) {
            g_rules_floor_hp[c] = gd->chr[c].hp;
            if (in_party[c] && gd->chr[c].hp != 0) {
                g_rules_floor_left |= (u8)(1u << c);
            }
        }
    }
    bool lost_screen = screen == SCREEN_GAMEOVER
                    && g_rules_screen != SCREEN_GAMEOVER;
    if (((r->felt & RUMBLE_FELT_LOST) != 0) != lost_screen) {
        rules_broke(-1, "lost felt, or not, against the game over");
    }
    RumbleMotors m = rumble_motors(r);
    bool going = m.low != 0 || m.high != 0;
    rules_sounds(r, m, screen);                 /* (build 161) */
    if (r->felt != 0 && !going) {
        rules_broke(-1, "felt, motors still");
    }
    if (going && g_rules_tick > g_rules_last_felt + RUMBLE_LONGEST) {
        rules_broke(-1, "motors going, nothing felt for the longest shake");
    }
    g_rules_screen = screen;
}

static void rumble_rules_report(void)
{
    FILE *fh = fopen(g_rules_path, "wb");
    if (fh == NULL) {
        fprintf(g_err, "route_probe: cannot write %s\n", g_rules_path);
        return;
    }
    for (u32 i = 0; i < g_rules_broken && i < RULES_SHOWN; i++) {
        fprintf(fh, "broke %s\n", g_rules_shown[i]);
    }
    for (u32 i = 0; i < g_rules_elsewhere && i < RULES_SHOWN; i++) {
        fprintf(fh, "elsewhere %s\n", g_rules_elsewhere_first[i]);
    }
    /* (build 161) */
    fprintf(fh, "followed %u ticks heard; blows with no sword %u\n",
            g_rules_followed, g_rumble.unfollowed);
    fprintf(fh, "felt");
    for (int k = 0; k < RUMBLE_FELT_KINDS; k++) {
        fprintf(fh, " %s %u", rumble_kind_name(k), g_rumble.felt_count[k]);
    }
    fprintf(fh, " late %d; HP drops elsewhere %u; %u ticks; %u broken\n",
            g_rumble.late, g_rules_elsewhere, g_rules_tick, g_rules_broken);
    fclose(fh);
}

/* (build 140) DIR/<route>.txt: the frames and shots held both ways. */
static void colour_report(void)
{
    char path[PATH_LEN + OUT_LEN + 8];
    snprintf(path, sizeof path, "%s/%s.txt", g_colour_dir, g_colour_name);
    FILE *fh = fopen(path, "wb");
    if (fh == NULL) {
        fprintf(g_err, "route_probe: cannot write %s\n", path);
        return;
    }
    /* (build 142) The pictures after the counts, so the first five words
     * stay where tools/colour_check.py reads them. */
    char things[COLOUR_THINGS * 34] = "";
    for (int t = 0; t < g_colour_thing_count; t++) {
        size_t at = strlen(things);
        snprintf(things + at, sizeof things - at, "%s%s",
                 (t == 0) ? " (" : ", ", g_colour_things[t]);
    }
    if (g_colour_thing_count > 0) {
        size_t at = strlen(things);
        snprintf(things + at, sizeof things - at, ")");
    }
    fprintf(fh, "%s %s: %u frames, %u differ; %u shots, %u differ; "
                "%llu pixels from pictures in %u frames%s; the frames' "
                "colours $%016llX\n",
            (g_colour_bad == 0 && g_colour_shots_bad == 0) ? "SAME" : "DIFF",
            g_colour_name, g_colour_frames, g_colour_bad, g_colour_shots,
            g_colour_shots_bad, (unsigned long long)g_colour_picture_pixels,
            g_colour_picture_frames, things,
            (unsigned long long)g_colour_hash);
    if (g_colour_first[0] != '\0') {
        fprintf(fh, "%s\n", g_colour_first);
    }
    fclose(fh);
}

static void host_flush_stderr(void *ctx)
{
    (void)ctx;
    capture_poll(true);
}

static void usage(FILE *fh)
{
    fprintf(fh, "usage: route_probe [-v] [-o DIR] ROUTE\n"
                "       route_probe -i [-o DIR]\n"
                "  -o DIR   output directory (default route_out/<route>,\n"
                "           or route_out/play for -i)\n"
                "  -v       echo the log to stdout as it runs\n"
                "  -i       play: commands from stdin, one a line, the\n"
                "           route language plus `state`; a failed command\n"
                "           reports and play goes on; EOF ends it\n"
                "       route_probe --play [ARGS]\n"
                "  --play   (build 171) the C player: tools/playthrough.py's\n"
                "           game and its ARGS (route_probe --play --help)\n"
                "       route_probe --computer [--language KEY] [ARGS]\n"
                "  --computer (build 179) the C player: the window's COMPUTER\n"
                "           PLAYS (tools/computer_plays.py's ARGS) on the\n"
                "           window's host, the probe's files in OUT/probe\n");
}

/* (build 179) --computer's arguments: --language KEY is the game's
 * settings.json's (the window's language is the player's setting), the
 * rest COMPUTER PLAYS's. The probe's directory is OUT/probe. */
static bool computer_args(int argc, char **argv, char *out, size_t n)
{
    static char *rest[64];
    int k = 0;
    for (int i = 0; i < argc && k < 64; i++) {
        if (strcmp(argv[i], "--language") == 0 && i + 1 < argc) {
            g_computer_language = argv[++i];
        } else {
            rest[k++] = argv[i];
        }
    }
    if (!player_computer_parse(k, rest)) {
        return false;
    }
    snprintf(out, n, "%s/probe", player_computer_out());
    return true;
}

/* (build 179) Every file in `dir`, not its folders: COMPUTER PLAYS's last
 * run, as the window clears it (autoplay.c, clear_dir). */
static void clear_files(const char *dir)
{
    char path[PATH_LEN + OUT_LEN];
    DIR *d = opendir(dir);
    if (d == NULL) {
        return;
    }
    for (struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
        struct stat st;
        if (e->d_name[0] == '.'
            || snprintf(path, sizeof path, "%s/%s", dir, e->d_name)
               >= (int)sizeof path) {
            continue;
        }
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            remove(path);
        }
    }
    closedir(d);
}

int main(int argc, char **argv)
{
    const char *route_path = NULL;
    const char *out_arg    = NULL;
    bool interactive       = false;
    bool play              = false;
    char play_out[OUT_LEN];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--play") == 0) {
            /* (build 171) the rest are the player's */
            if (!player_cli_parse(argc - i - 1, argv + i + 1, play_out,
                                  sizeof play_out)) {
                return 2;
            }
            play        = true;
            interactive = true;
            out_arg     = play_out;
            break;
        } else if (strcmp(argv[i], "--computer") == 0) {
            /* (build 179) the rest are COMPUTER PLAYS's */
            if (!computer_args(argc - i - 1, argv + i + 1, play_out,
                               sizeof play_out)) {
                return 2;
            }
            if (player_computer_classify()) {
                /* the Python's --classify: the stage, no game, no file
                 * touched */
                return player_computer_run(NULL);
            }
            g_computer  = true;
            play        = true;
            interactive = true;
            out_arg     = play_out;
            break;
        } else if (strcmp(argv[i], "-v") == 0) {
            g_verbose = true;
        } else if (strcmp(argv[i], "-i") == 0) {
            interactive = true;
            g_verbose   = true;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_arg = argv[++i];
        } else if (argv[i][0] == '-') {
            usage(stderr);
            return 2;
        } else if (route_path == NULL) {
            route_path = argv[i];
        } else {
            usage(stderr);
            return 2;
        }
    }
    if ((route_path == NULL) != interactive) {
        usage(stderr);
        return 2;
    }

    Route route;
    memset(&route, 0, sizeof route);
    if (!interactive && !probe_parse_route(&route, route_path, stderr)) {
        return 2;
    }

    /* route_out/<stem> unless told otherwise. */
    char out[OUT_LEN];
    if (out_arg != NULL) {
        snprintf(out, sizeof out, "%s", out_arg);
    } else if (interactive) {
        snprintf(out, sizeof out, "route_out/play");
    } else {
        const char *base = strrchr(route_path, '/');
        base = (base != NULL) ? base + 1 : route_path;
        char stem[128];
        snprintf(stem, sizeof stem, "%s", base);
        char *dot = strrchr(stem, '.');
        if (dot != NULL && dot != stem) {
            *dot = '\0';
        }
        snprintf(out, sizeof out, "route_out/%s", stem);
    }
    /* (build 140) The colour plane's switches, and the route's name for
     * the check's files: the output directory's last part. */
    {
        /* (build 141) The shots stay the palette path's indexed PNGs,
         * the routes' reference, unless PS2_DRAW=colour; the game
         * itself shows the colour plane. */
        const char *draw = getenv("PS2_DRAW");
        g_draw = (draw != NULL && strcmp(draw, "colour") == 0)
               ? FB_DRAW_COLOUR : FB_DRAW_PALETTE;
    }
    g_colour_dir = getenv("PS2_COLOUR_CHECK");
    if (g_colour_dir != NULL && g_colour_dir[0] == '\0') {
        g_colour_dir = NULL;
    }
    g_rules_path = getenv("PS2_RUMBLE_CHECK");          /* (build 160) */
    if (g_rules_path != NULL && g_rules_path[0] == '\0') {
        g_rules_path = NULL;
    }
    {
        const char *name = strrchr(out, '/');
        snprintf(g_colour_name, sizeof g_colour_name, "%s",
                 (name != NULL) ? name + 1 : out);
    }
    if (g_colour_dir != NULL && !make_dirs(g_colour_dir)) {
        fprintf(stderr, "route_probe: cannot make %s\n", g_colour_dir);
        return 2;
    }

    char saves[PATH_LEN];
    snprintf(saves, sizeof saves, "%s/saves", out);
    if (g_computer) {
        /* (build 179) the window's: the game's saves in OUT/slots */
        snprintf(saves, sizeof saves, "%s/slots", player_computer_out());
    }
    if (!make_dirs(out) || !make_dirs(saves)) {
        fprintf(stderr, "route_probe: cannot make %s\n", saves);
        return 2;
    }
    clear_previous(out, saves);
    if (g_computer && strcmp(player_computer_start(), "last") != 0) {
        /* (build 179) a new COMPUTER PLAYS clears the last run's files
         * first, as the window's does, unless it goes on from it */
        char cps[PATH_LEN + 16];
        snprintf(cps, sizeof cps, "%s/checkpoints", player_computer_out());
        clear_files(saves);
        clear_files(cps);
        clear_files(player_computer_out());
    }
    gamedata_set_save_dir(saves);
    /* (build 100) The route's own settings.json (none: the defaults), not
     * the player's: the Escape menu draws them, and a route must not
     * depend on how the player last left it. */
    char settings[PATH_LEN + 16];
    snprintf(settings, sizeof settings, "%s/settings.json", saves);
    if (g_computer) {
        /* (build 179) the probe's own folder, its language --language's */
        snprintf(settings, sizeof settings, "%s/settings.json", out);
        FILE *sf = fopen(settings, "wb");
        if (sf != NULL) {
            if (g_computer_language != NULL) {
                fprintf(sf, "{\"language\": \"%s\"}\n",
                        g_computer_language);
            } else {
                fprintf(sf, "{}\n");
            }
            fclose(sf);
        }
    }
    sysmenu_set_settings_path(settings);

    char log_path[PATH_LEN];
    snprintf(log_path, sizeof log_path, "%s/log.txt", out);
    /* (build 113) "wb", here and for summary.txt: Windows's text mode
     * writes "\r\n", and a route's files are the same bytes on every OS. */
    g_log = fopen(log_path, "wb");
    if (g_log == NULL || !capture_begin(out)) {
        fprintf(stderr, "route_probe: cannot write in %s\n", out);
        return 2;
    }
    fprintf(g_log, "route %s -> %s\n",
            interactive ? "(stdin)" : route_path, out);
    fprintf(g_log, "   tick\n");

    /* (build 157) The interpreter attached before game_init: the game's
     * stderr reaches the log through it (probe_stderr_line logs through
     * the host), and until 157 a failed load's message -- "png: cannot
     * read ..." -- went to a host not attached yet, a NULL call, and the
     * probe died before game_init's own message. */
    ProbeHost host;
    memset(&host, 0, sizeof host);
    host.game         = &g_game;
    host.video        = &g_video;
    host.tick         = host_tick;
    host.after_tick   = host_after_tick;
    host.log          = host_log;
    host.write_png    = host_write_png;
    host.flush_stderr = host_flush_stderr;
    host.rumble       = &g_rumble;          /* (build 160) */
    host.player_only  = g_computer;         /* (build 179) the window's */
    probe_attach(&host);

    /* From here on the game's stderr lands in the log. */
    assetfs_preload("assets/manifest.txt");
    video_init(&g_video, NULL);
    if (!game_init(&g_game, &g_video)) {
        capture_end();
        fprintf(g_log, "\ngame_init failed\nexit 2\n");
        fclose(g_log);
        fprintf(g_err, "route_probe: game_init failed (run from ps2/, with "
                       "assets/ in place); see %s\n", log_path);
        return 2;
    }
    audio_init(&g_audio, AUDIO_RATE);
    audio_load_music(&g_audio, "assets");
    game_attach_audio(&g_game, &g_audio);
    game_debug_on_script(&g_game, probe_on_script, NULL);
    audio_debug_on_sound(probe_on_sound, NULL);
    capture_poll(false);
    probe_begin();

    int  ran = 0;
    bool ok  = true;
    int  failed_line = 0;
    int  play_status = 0;
    if (interactive) {
        /* Play: one command a line from stdin, its log echoed to stdout
         * as it runs (-v is implied), so a driver can pipe a burst in and
         * read what happened. A FAIL reports and play goes on; the exit
         * is 0 either way. */
        char line[LINE_MAX_LEN];
        int  lineno = 0;
        if (play) {
            /* (build 171) the C player's lines in place of stdin's */
            g_verbose = false;
            PlayerIO io = { play_run, NULL, false };
            g_play_out   = out;
            g_play_saves = saves;
            if (g_computer) {
                g_play      = player_session_new("computer", &io);
                play_status = player_computer_run(g_play);
            } else {
                g_play      = player_session_new(out + strlen("route_out/"),
                                                 &io);
                play_status = player_cli_run(g_play);
            }
            player_session_free(g_play);
            g_play       = NULL;
            ran          = g_play_ran;
        }
        while (!play && fgets(line, sizeof line, stdin) != NULL) {
            lineno++;
            if (probe_run_text(line, lineno, out, saves) != 0) {
                ran++;
            }
        }
    } else {
        for (int i = 0; i < route.count; i++) {
            ran++;
            if (!probe_run(&route.cmd[i], out, saves)) {
                ok = false;
                failed_line = route.cmd[i].line;
                break;
            }
        }
    }
    capture_end();

    char summary[LINE_MAX_LEN + 256];
    if (interactive) {
        route_path = "(stdin)";
    }
    if (ok) {
        snprintf(summary, sizeof summary,
                 "PASS  %s: %d commands, %u ticks, %d shot%s -> %s",
                 route_path, ran, probe_ticks(), probe_shots(),
                 (probe_shots() == 1) ? "" : "s",
                 out);
    } else {
        snprintf(summary, sizeof summary, "FAIL  %s:%d at tick %u: %s",
                 route_path, failed_line, probe_ticks(),
                 probe_fail_text());
    }
    int code = ok ? 0 : 1;

    fprintf(g_log, "\n%s\nexit %d\n", summary, code);
    fclose(g_log);

    char sum_path[PATH_LEN];
    snprintf(sum_path, sizeof sum_path, "%s/summary.txt", out);
    FILE *sf = fopen(sum_path, "wb");
    if (sf != NULL) {
        fprintf(sf, "%s\nexit %d\n", summary, code);
        fclose(sf);
    }

    if (g_colour_dir != NULL) {
        colour_report();
    }
    if (g_rules_path != NULL) {
        rumble_rules_report();
    }

    printf("%s\n", summary);
    return play ? play_status : code;
}
