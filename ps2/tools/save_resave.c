/* (build 138e) The port's own reading of a save file, for tools/to_srm.py's
 * round trips: what gamedata_load makes of a slot file, written back by
 * gamedata_save. The Python converter mirrors gamedata_load; this is the C
 * it is checked against, so the check does not rest on the mirror alone.
 *
 *     ./save_resave IN_DIR OUT_DIR      IN_DIR/slot1.json loaded, then
 *                                       written as OUT_DIR/slot1.json
 *     ./save_resave --new OUT_DIR       a new game (gamedata_new_game,
 *                                       loc_89EE) written as OUT_DIR/slot1.json
 *
 * Run from the tree's top: the tables are read from assets/data and
 * assets/levels, as the game reads them. Exit status 0 on success, 1 when
 * a table, the file or the write fails. */

#include "../src/game/gamedata.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    GameData gd;

    if (argc != 3) {
        fprintf(stderr, "usage: save_resave IN_DIR OUT_DIR\n"
                        "       save_resave --new OUT_DIR\n");
        return 1;
    }
    if (!gamedata_load_tables()) {
        fprintf(stderr, "save_resave: the tables under assets/ did not load "
                        "(run from the tree's top)\n");
        return 1;
    }

    if (strcmp(argv[1], "--new") == 0) {
        gamedata_new_game(&gd);
    } else {
        gamedata_set_save_dir(argv[1]);
        if (!gamedata_load(&gd, 0)) {
            fprintf(stderr, "save_resave: %s/slot1.json did not load\n",
                    argv[1]);
            return 1;
        }
    }

    gamedata_set_save_dir(argv[2]);
    if (!gamedata_save(&gd, 0)) {
        return 1;
    }
    return 0;
}
