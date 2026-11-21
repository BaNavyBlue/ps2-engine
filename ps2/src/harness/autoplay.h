#ifndef PS2_HARNESS_AUTOPLAY_H
#define PS2_HARNESS_AUTOPLAY_H

/* (build 100b) COMPUTER PLAYS: the harness's driver (tools/computer_plays.py)
 * as a child process, speaking the route language over two pipes, run by
 * the probe's own interpreter (probe.h) on a thread of its own. Each tick a
 * command wants is handed to the frame loop, which runs game_tick and may
 * draw while the command thread waits; the two never touch the game at
 * once. docs/computer_plays.md has the design.
 *
 * (build 179) The player is the C player (src/player/), on that thread
 * itself; PS2_COMPUTER_DRIVER=python the Python driver, as before.
 *
 * POSIX: posix_spawn, pipe, pthreads. (build 115) Windows: CreateProcess,
 * the pipes handed over by handle, the thread by winpthreads, the lock and
 * its wake-ups Windows's own. */

#include "../core/video.h"
#include "../game/game.h"
#include "../platform/platform.h"

#include <pthread.h>
#include <stdbool.h>
#include <sys/types.h>

/* Where COMPUTER PLAYS keeps everything: never the player's own slots.
 * (build 123) The saves folder's computer/ and computer/slots, the saves
 * folder as autoplay_set_home was given it ("saves" until then). */
void        autoplay_set_home(const char *saves);
const char *autoplay_dir(void);     /* <saves>/computer */

typedef enum {
    AUTOPLAY_OFF = 0,
    AUTOPLAY_PIPE,          /* the command thread is reading the driver  */
    AUTOPLAY_RUNNING,       /* ... interpreting a command: it has the game */
    AUTOPLAY_REQUEST,       /* ... waiting for the tick it asked for      */
    AUTOPLAY_DONE,          /* the tick ran; the thread has not woken yet */
    AUTOPLAY_ABORT,         /* handed back while a tick was asked for     */
    AUTOPLAY_FINISHED       /* the thread is gone or going                */
} AutoplayState;

typedef enum {
    AUTOPLAY_HOLD_TICK = 0, /* a tick is asked for: *in filled; the caller
                               has the game until autoplay_release       */
    AUTOPLAY_HOLD_IDLE,     /* the thread waits on the driver: the caller
                               has the game (to draw) until release      */
    AUTOPLAY_HOLD_BUSY,     /* the thread kept the game past the wait    */
    AUTOPLAY_HOLD_ENDED     /* the driver has finished: autoplay_stop    */
} AutoplayHold;

typedef struct {
#ifdef _WIN32
    void           *lock;       /* (build 115) an SRWLOCK (autoplay.c) */
    void           *cond;       /* ... and a CONDITION_VARIABLE */
    long            wakes;      /* ... and its wake-ups, counted */
#else
    pthread_mutex_t lock;
    pthread_cond_t  cond;
#endif
    pthread_t       thread;
    bool            thread_live;
    AutoplayState   state;
    bool            abort;
    Input           want;       /* the tick's input, while REQUEST */

    pid_t           child;      /* the driver's process id, -1 for none */
#ifdef _WIN32
    void           *process;    /* (build 115) ... and its process HANDLE */
#endif
    int             cmd_fd;     /* the driver's commands, read here */
    int             log_fd;     /* the log lines, written here      */

    Game           *game;
    Video          *video;
    u32             ticks;      /* ticks run for the driver */
    bool            gear;       /* (build 162a) the driver's `@gear on`,
                                 * till `@gear off`: buying gear */
    bool            python;     /* (build 179) the Python driver, not C */
    struct Session *session;    /* ... the C player's, NULL for none */
    int             lineno;     /* ... its lines, numbered as -i's */
} Autoplay;

/* Whether COMPUTER PLAYS can run: (build 179) the C player's files, the
 * tree's or the pack's; for the Python driver python3 on the PATH and the
 * harness's files. On false, `why` names what is missing and `short_why`
 * is the menu's value (NO TOOLS; NO PYTHON3 for the Python). */
bool autoplay_available(char *why, size_t n, char *short_why, size_t sn);

/* Starts the driver: `start` is "new", (build 103) "last" -- the
 * computer's own last run, from the save autoplay_dir()/last.json names --
 * or the path of a save to CONTINUE (the player's, read only). Clears
 * autoplay_dir()'s last run first unless `start` is "last"; points
 * the game's saves at its slots/. `extra` (may be NULL) is appended to
 * the driver's arguments -- PS2_COMPUTER_ARGS, for tests. `speed` names
 * SPEED for the start's line on stderr (build 120; the frame loop does the
 * rest). The caller must own the game (not inside a tick). */
bool autoplay_start(Autoplay *a, Game *g, Video *v, const char *start,
                    const char *speed, const char *extra);

bool autoplay_running(const Autoplay *a);

/* (build 162a) Whether the driver is buying gear: its last host line of
 * the two was `@gear on` (docs/computer_plays.md, Speed). Only for the
 * caller that holds the game -- between autoplay_hold's TICK and
 * autoplay_release -- which holds the lock the command thread sets it
 * under. */
bool autoplay_buying_gear(const Autoplay *a);

/* Frame loop: waits up to `wait_s` seconds for the command thread to ask
 * for a tick (with want_tick false, only for it to let go of the game: the
 * Escape menu is up and wants to draw). TICK: the caller has the game -- runs game_tick with *in,
 * may draw -- until autoplay_release(true). IDLE: the wait ran out with the
 * thread waiting on the driver; the caller has the game to draw until
 * autoplay_release(false). */
AutoplayHold autoplay_hold(Autoplay *a, Input *in, double wait_s,
                           bool want_tick);
void         autoplay_release(Autoplay *a, bool ticked);

/* Hands the pad back: the driver told (its log pipe closed), given a
 * second to write its report, then killed; the command thread released
 * without another tick and joined; the game's saves back to saves/. */
void autoplay_stop(Autoplay *a);

#endif /* PS2_HARNESS_AUTOPLAY_H */
