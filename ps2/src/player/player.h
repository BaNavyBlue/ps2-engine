#ifndef PS2_PLAYER_H
#define PS2_PLAYER_H

/* (build 171) COMPUTER PLAYS's player in C (docs/c_player.md): what its
 * hosts see. pl.h is the player's own. */

#include "../core/ps2_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* How the player gives the probe a line: probe_run_text with the line's
 * number. The log's lines come back through player_log_line while it
 * runs, as route_probe -i's stdout carried them to the Python.
 * (build 179) `attached`: the game's COMPUTER PLAYS, which also hears the
 * host lines (`@grind on`, `@gear on` and their offs: play.Session's
 * attached). */
typedef struct {
    void (*run)(void *ctx, const char *line);
    void *ctx;
    bool  attached;
} PlayerIO;

typedef struct Session Session;

/* route_probe --play: the arguments (playthrough.py's) and the probe's
 * output directory they give; a session named for it (the directory less
 * route_out/); the run, its exit status playthrough.py's. */
bool     player_cli_parse(int argc, char **argv, char *out, size_t n);
Session *player_session_new(const char *name, const PlayerIO *io);
void     player_session_free(Session *s);
void     player_log_line(Session *s, u32 tick, const char *line);
int      player_cli_run(Session *s);

/* (build 179) COMPUTER PLAYS's own loop, tools/computer_plays.py's main:
 * its arguments (computer_plays.py's, less --attach and --home), the
 * output folder they name (--out, saves/computer by default), and the run
 * on a session named "computer", its exit status the Python's. The window
 * (autoplay.c) and `route_probe --computer` host it. */
bool        player_computer_parse(int argc, char **argv);
const char *player_computer_out(void);
const char *player_computer_start(void);   /* --start: new, last, SAVE */
bool        player_computer_classify(void); /* --classify: no game, and
                                               the run's s may be NULL */
int         player_computer_run(Session *s);

/* (build 179) The pad taken back, from inside the host's run or tick:
 * the run jumps to its end, writes its report ("handed back") and returns,
 * as the Python's EOFError did. Only on the run's own thread. */
_Noreturn void player_hand_back(void);

/* (build 179) Where the story's lines are printed as they come (the
 * Python's stdout): stdout when NULL. */
void player_set_echo(FILE *f);

#endif /* PS2_PLAYER_H */
