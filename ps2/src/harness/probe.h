#ifndef PS2_HARNESS_PROBE_H
#define PS2_HARNESS_PROBE_H

/* (build 100b) The route language's interpreter, shared by its two hosts:
 * tools/route_probe.c (routes, `route_probe -i`) and the game's COMPUTER
 * PLAYS (src/harness/autoplay.c). Moved out of route_probe.c unchanged;
 * docs/computer_plays.md has the design, docs/harness.md the language.
 *
 * The interpreter advances the game only through the host's tick and
 * reports only through its log. A command that waits -- `wait idle`, a
 * walk -- is a loop of host ticks: the probe runs game_tick in place, the
 * window hands each tick to its frame loop. */

#include "../core/video.h"
#include "../game/game.h"
#include "../platform/platform.h"

#include <stdbool.h>
#include <stdio.h>

#define PROBE_LINE_MAX  512
#define PROBE_ARGS_MAX  8
#define PROBE_PATH_LEN  512
#define PROBE_OUT_LEN   256     /* the output directory, leaving room */

typedef enum {
    C_START = 0,
    C_PRESS,
    C_WALK,
    C_WAIT_IDLE,
    C_WAIT_SCREEN,
    C_WAIT_TICKS,
    C_WAIT_SCRIPT,
    C_WAIT_STEP,
    C_SHOT,
    C_EXPECT_SCREEN,
    C_EXPECT_LEVEL,
    C_EXPECT_SCRIPT,
    C_EXPECT_NO_STDERR,
    C_EXPECT_TEXT,
    C_EXPECT_COMMAND,
    C_EXPECT_JOINED,
    C_EXPECT_ITEM,
    C_EXPECT_JOIN_NEXT,
    C_EXPECT_FLAG,
    C_EXPECT_PERSON,
    C_FLAG,
    C_EXPECT_HP,
    C_EXPECT_STAT,
    C_EXPECT_PARTY,
    C_MEMBERS,
    C_MEMBER,
    C_OUTFIT,
    C_WARP,
    C_ONESHOT,
    C_SAVE_HERE,
    C_EXPECT_HIDAPIPE,
    C_EXPECT_MENU,
    C_EXPECT_MUSIC,
    C_EXPECT_SONG,          /* (build 139f) */
    C_WAIT_LEVEL,
    C_WAIT_ENDING,
    C_EXPECT_OBJECT,
    C_EXPECT_RIDING,
    C_EXPECT_SETTING,
    C_EXPECT_ESCAPE_MENU,
    C_EXPECT_PIXEL,
    C_WAIT_PAD,         /* (build 101) wait pad N */
    C_LIMIT,
    C_FUZZ,
    C_PARTY,
    C_ENEMY_HP,
    C_KEEP_EQUIPMENT,
    C_STATE,
    C_BAGS,
    C_HANDS,
    C_TECHS,
    C_OBJECTS,
    C_ROSTER,
    C_EXP,
    C_GIVE,
    C_MESETA,
    C_DEFENSE,
    C_DISPLAY,          /* (build 105) display WORDS: the CRT page's screen */
    C_EXPECT_CRT,       /* (build 105) expect crt ROW VALUE [/ WHY] */
    C_EXPECT_AUDIO,     /* (build 110) expect audio ROW VALUE [/ WHY] */
    C_SYNTH,            /* (build 110) synth chip|q */
    C_SOUND_RENDER,     /* (build 110) sound render on|off */
    C_SOUND_PLAY,       /* (build 111) sound play ID */
    C_EXPECT_SOUND,     /* (build 110) expect sound-writes N $HASH,
                           expect sound playing|silent, (124a)
                           expect sound-records, (183)
                           expect sound-register PART $REG $VALUE */
    C_EXPECT_PICTURE,   /* (build 142) expect picture X Y THING|none */
    C_JOIN_NEXT,        /* (build 145) join-next N */
    C_EXPECT_RUMBLE,    /* (build 160) expect rumble KIND N | going|still */
    C_RUMBLE_LOG,       /* (build 160) rumble-log NAME LEVEL */
    C_KEY,              /* (build 160) key NAME */
    C_PAD,              /* (build 160) pad NAME */
    C_PAD_STYLE,        /* (build 160) pad-style STYLE */
    C_EXPECT_CONTROLS,  /* (build 160) expect keys|buttons ACTION S1 / S2 */
    C_LANGUAGE          /* (build 163) language KEY */
} CmdKind;

typedef enum {
    START_NEW = 0,
    START_SLOT,
    START_LEVEL,
    START_BUILDING,
    START_BATTLE
} StartKind;

typedef struct {
    CmdKind kind;
    int     line;
    char    text[PROBE_LINE_MAX];     /* as written, for the log */
    int     sub;                    /* StartKind, button, direction, screen */
    long    arg[PROBE_ARGS_MAX];
    int     nargs;
    char    str[PROBE_LINE_MAX];      /* a shot name, stderr text, a file */
} ProbeCmd;

typedef struct {
    ProbeCmd *cmd;
    int  count;
    int  cap;
} ProbeRoute;

typedef struct {
    Game  *game;
    Video *video;
    /* One game tick with this input. */
    void (*tick)(void *ctx, const Input *in);
    /* After the tick count has moved, before the tick is observed: the
     * probe drains the audio queue and reads the captured stderr. May be
     * NULL. */
    void (*after_tick)(void *ctx);
    /* One finished log line, without its newline, stamped with the tick. */
    void (*log)(void *ctx, u32 tick, const char *line);
    /* A shot's PNG. NULL: `shot` is logged and nothing is drawn. */
    bool (*write_png)(void *ctx, const char *path, const Framebuffer *fb,
                      const Palette *pal);
    /* Everything the game wrote to stderr so far, to probe_stderr_line.
     * May be NULL (no stderr is captured). */
    void (*flush_stderr)(void *ctx);
    /* COMPUTER PLAYS: only a player's commands (the pad, the waits, the
     * queries); `start new` and `start slot` press the console's reset
     * instead of loading anything, and every other command is refused. */
    bool  player_only;
    /* (build 160) The host's rumble watcher (src/game/rumble.h), which it
     * runs in after_tick, for `expect rumble` and `rumble-log`; NULL where
     * there is none (COMPUTER PLAYS: those commands fail). */
    Rumble *rumble;
    void *ctx;
} ProbeHost;

/* Takes the host and starts a run afresh: tick 0, no scripts seen, the
 * default limit. Then probe_begin once the game is ready to observe. */
void probe_attach(const ProbeHost *host);
void probe_begin(void);

bool probe_parse_line(ProbeCmd *c, char *body, char *err, size_t errsz);
bool probe_parse_route(ProbeRoute *r, const char *path, FILE *err);
bool probe_run(const ProbeCmd *c, const char *out, const char *saves);

/* One line of `route_probe -i`'s input: comment stripped, parsed, run. A
 * parse error is logged ("?") and nothing runs. 0 when nothing ran, 1 when
 * a command ran and passed, -1 when it ran and failed. */
int probe_run_text(char *line, int lineno, const char *out,
                   const char *saves);

void probe_log(const char *fmt, ...);
void probe_stderr_line(const char *text);
void probe_on_script(void *ctx, u16 id);
void probe_on_sound(void *ctx, u8 id);

u32         probe_ticks(void);
int         probe_shots(void);
const char *probe_fail_text(void);

#endif /* PS2_HARNESS_PROBE_H */
