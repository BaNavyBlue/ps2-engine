/* (build 187) The extractors in C, by the Makefile's EXTRACTORS names, in
 * its order. A name not here is still the Python's alone. */

#include "extract.h"

void ex_step_palettes(Extract *x, const ExArgs *a);
void ex_step_paletteptrs(Extract *x, const ExArgs *a);
void ex_step_levels(Extract *x, const ExArgs *a);
void ex_step_collision(Extract *x, const ExArgs *a);
void ex_step_interactions(Extract *x, const ExArgs *a);
void ex_step_script(Extract *x, const ExArgs *a);
void ex_step_buildings(Extract *x, const ExArgs *a);
void ex_step_stores(Extract *x, const ExArgs *a);
void ex_step_techs(Extract *x, const ExArgs *a);
void ex_step_npcs(Extract *x, const ExArgs *a);
void ex_step_gamedata(Extract *x, const ExArgs *a);
void ex_step_battle(Extract *x, const ExArgs *a);
void ex_step_inline_art(Extract *x, const ExArgs *a);
void ex_step_tables(Extract *x, const ExArgs *a);
void ex_step_music(Extract *x, const ExArgs *a);
void ex_step_drums(Extract *x, const ExArgs *a);
void ex_step_art(Extract *x, const ExArgs *a);
void ex_step_nametables(Extract *x, const ExArgs *a);
void ex_step_winart(Extract *x, const ExArgs *a);
void ex_step_flag_names(Extract *x, const ExArgs *a);
void ex_step_pictures(Extract *x, const ExArgs *a);

const ExStep EX_STEPS[] = {
    { "palettes",     ex_step_palettes,     "assets/palettes" },
    { "paletteptrs",  ex_step_paletteptrs,  "assets/palettes" },
    { "levels",       ex_step_levels,       "assets" },
    { "collision",    ex_step_collision,    "assets/levels" },
    { "interactions", ex_step_interactions, "assets/levels" },
    { "script",       ex_step_script,       "assets/script" },
    { "buildings",    ex_step_buildings,    "assets/data" },
    { "stores",       ex_step_stores,       "assets/data" },
    { "techs",        ex_step_techs,        "assets/data" },
    { "npcs",         ex_step_npcs,         "assets" },
    { "gamedata",     ex_step_gamedata,     "assets/data" },
    { "battle",       ex_step_battle,       "assets/data" },
    { "inline_art",   ex_step_inline_art,   "assets/tiles" },
    { "tables",       ex_step_tables,       "assets/tables" },
    { "music",        ex_step_music,        "assets/music" },
    { "drums",        ex_step_drums,        "assets/music" },
    { "art",          ex_step_art,          "assets" },
    { "nametables",   ex_step_nametables,   "assets/nametables" },
    { "winart",       ex_step_winart,       "assets/winart.json" },
    { "flag_names",   ex_step_flag_names,   "assets/data" },
    { "pictures",     ex_step_pictures,     "assets/art" },
};

const int EX_STEP_COUNT = (int)(sizeof EX_STEPS / sizeof EX_STEPS[0]);
