#ifndef PS2_HOME_H
#define PS2_HOME_H

#include <stddef.h>

/* (build 123) Where the game's own files are, and where its saves go, for
 * the game run as a program on its own (`make dist`) as well as from the
 * tree. main.c's; not the probe's, which is always run from the tree. */

/* The working folder to the program's own when the working folder has not
 * got the game's files (assets/manifest.txt): a shortcut or a double-click
 * that started it elsewhere. Nothing when it has them, or when the
 * program's folder cannot be found. */
void home_enter(void);

/* (build 193) A path named on the command line, as the folder the game
 * was started in reads it, into `out`: home_enter may have left that
 * folder for the game's own, so a relative path is joined to the one it
 * left. UTF-8 in and out (on Windows the folder is read by its wide
 * name). */
void home_given_path(const char *given, char *out, size_t n);

/* The folder every save goes under -- the player's slots, settings.json,
 * and COMPUTER PLAYS's own folder -- into `out`: PS2_SAVES when set; else
 * "saves" beside the game when that can be written; else the user's own
 * data folder (Windows: %LOCALAPPDATA%\PhantasyStarII\saves; elsewhere
 * $XDG_DATA_HOME, or ~/.local/share, then phantasy-star-ii/saves), made if
 * need be. Finding out leaves nothing behind: a file is made where the
 * saves would go (the folder itself if it is there, else the game's) and
 * removed. */
void home_saves(char *out, size_t n);

/* (build 138b) The mods folder (docs/mods.md) into `out`: PS2_MODS when
 * set; else "mods" beside the game when that can be written; else beside
 * the saves in the user's own data folder (Windows:
 * %LOCALAPPDATA%\PhantasyStarII\mods; elsewhere $XDG_DATA_HOME, or
 * ~/.local/share, then phantasy-star-ii/mods). Not made here. */
void home_mods(char *out, size_t n);

/* (build 138b) A packed game's own files under assets/, written out to
 * `mods`/originals/ for copying from -- the game never reads them there --
 * with `mods`/README.txt, unless originals/pack_id.txt says they are this
 * program's already. (build 138c) A tree build, whose own files are its
 * assets/: README.txt alone, when it is missing. Nothing else under `mods`
 * is touched. */
void home_write_originals(const char *mods);

#endif /* PS2_HOME_H */
