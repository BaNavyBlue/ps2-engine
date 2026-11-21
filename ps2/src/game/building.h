#ifndef PS2_BUILDING_H
#define PS2_BUILDING_H

#include "gamedata.h"
#include "window.h"
#include "../core/rng.h"

/* Building_CheckRoutine (ps2.asm:16105) and the two buildings game start
 * passes through: Building_CentralTowerGovernor (18440) and
 * Building_RolfHouseStart (16150).
 *
 * Every building runs the same way. event_routine 1 opens the portrait
 * window the door named (WinID_PortraitStart + portrait_index); from 2 on
 * the building's own jump table takes over, one state each time the
 * windows fall idle; and event_routine 0 -- which CloseAllWindows leaves
 * behind -- sends the game back to the level screen. */

enum {
    BUILDING_ROLF_HOUSE_START = 0,
    BUILDING_ROLF_HOUSE       = 1,
    BUILDING_DATA_MEMORY      = 2,
    BUILDING_CLONE_LAB        = 3,
    BUILDING_HOSPITAL         = 4,
    BUILDING_WEAPON_STORE     = 5,
    BUILDING_ARMOR_STORE      = 6,
    BUILDING_ITEM_STORE       = 7,
    BUILDING_TOWER_OUTSIDE    = 8,
    BUILDING_TOWER_ROOM       = 9,
    BUILDING_LIBRARY          = 10,
    BUILDING_TOWER_ROOF       = 11,
    BUILDING_USTVESTIA        = 12,
    BUILDING_INVENTOR         = 13,
    BUILDING_GOVERNOR         = 14,
    BUILDING_TELEPORT         = 15,
    BUILDING_GAIRA_PANEL      = 16,
    BUILDING_TYLER_SHIP       = 17,
    BUILDING_ESPER_MANSION    = 18
};

/* True for the doors the port opens. */
bool building_ported(int building);

#define PORTRAIT_NEI            0x01
#define PORTRAIT_GOVERNOR       0x17
#define PORTRAIT_SPACESHIP      0x18    /* the roof; Skure's door too */
#define PORTRAIT_TYLER_SHIP     0x1C    /* loc_CC26; loc_8326 skips it */
#define PORTRAIT_ROLF_AT_HOME   0x37    /* window $72: Rolf, on the right */

typedef enum {
    BUILDING_RUNNING = 0,
    BUILDING_LOAD_PORTRAIT, /* LoadPortraits mid-scene: art for portrait  */
    BUILDING_TO_LEVEL,      /* event_routine reached 0                    */
    BUILDING_CHANGED,       /* screen_changed_flag: BuildingScreen again  */
    BUILDING_RESET,         /* jmp EntryPoint: the console restarts       */
    BUILDING_LESSON,        /* Ustvestia's loc_C7AC: fade out, SXFID_Musik,
                             * $78 frames, fade in -- the building paused */
    BUILDING_FADE_OUT,      /* Gaira's control panel, loc_CC10:
                             * PaletteFadeFrom, then $E0 queued */
    BUILDING_PICTURE,       /* Tyler's spaceship: loc_6BBE or loc_6C54,
                             * bs->fx, inside the building's call -- no
                             * windows, objects or palette cycle meanwhile */
    BUILDING_FLIGHT         /* Building_Roof's loc_C58A and SpaceShipLoop,
                             * inside the building's call: the flight to
                             * gd->level_index (after loc_C572's
                             * PaletteFadeFrom when bs->flight_fade), then
                             * ScreenID_Level */
} BuildingResult;

/* Tyler's spaceship's plane A (Building_TylerSpaceship, build 82). */
enum {
    TYLER_VIEW_NONE = 0,
    TYLER_VIEW_BATTLE,      /* loc_70A32 over loc_6D7BA: the dream's fight */
    TYLER_VIEW_HELP,        /* loc_70D42 over loc_6F040: "Help!!!"         */
    TYLER_VIEW_MONITOR      /* Tyler (TylerPortraitTileInd) and the monitor
                             * (TylerSpcshp_MonitorTileInd), TylerSpaceshipArt */
};

enum {
    BUILDING_FX_NONE = 0,
    BUILDING_FX_IN,         /* loc_6BBE: 64 steps, one every fourth frame */
    BUILDING_FX_OUT         /* loc_6C54: 32 steps, one a frame            */
};

/* What outlives a screen: the door walked through and the scripted walk
 * that follows. */
typedef struct {
    int  building;          /* building_index */
    int  portrait;          /* portrait_index */
    int  store_max;         /* $FFFFF764, from the door: stock size - 1 */
    int  store_index;       /* $FFFFF766, from the door: which stock    */
    bool demo_flag;
    int  demo_index;
    Rng *rng;               /* GenerateRandomNumber, for Shir           */
    s16  timer;             /* $FFFFDE5E: the hospital's two pauses     */

    /* ProcessStealItem's `move.w #$C00, (event_flags)`: the level
     * screen's one-shot event for Shir walking off, which game.c hands to
     * the next level load (loc_7E02 case $C, ObjType_Events $31). */
    u16  event_flags;

    /* REORGANIZE's bsr loc_F550 (ps2.asm:16334): every character's
     * battle command back to CharStartCommandsArray. The battle state
     * is the game's, so the building sets this and the building screen
     * does the reset and clears it. */
    bool reset_commands;

    /* Building_EsperMansion's `move.w #0, (event_flags).w` (loc_CF0A):
     * the level screen's live event_flags is the game's. */
    bool clear_event_flags;

    /* demo_timer ($FFFFF79x): the Gaira control panel's and Tyler's
     * spaceship's waits (loc_CC3E). */
    u16  demo_timer;

    /* Tyler's spaceship's pictures (build 82): what plane A shows, the
     * monitor's frame (loc_CDDE's d0), the dissolve the building asked
     * for with BUILDING_PICTURE and over how many tiles (d7 + 1), and the
     * two one-shot requests the building screen acts on after the call:
     * loc_CD50's PaletteLoad1/PaletteLoad2 $2A, and loc_CE20's
     * ObjID_MonitorExplosion into $E800. */
    int  picture;
    int  monitor_frame;
    int  fx;
    int  fx_tiles;
    int  fx_art;            /* the TYLER_VIEW_ whose art is dissolved in */
    bool palette_2a;
    bool graph_overlay;     /* (build 138d) loc_C4B2's PaletteLoad2 $32 */
    bool spawn_explosion;

    /* Building_Roof (build 84): true when the flight starts from the
     * Central Tower's roof (loc_C572, PaletteFadeFrom first), false from
     * Skure's (loc_C578, straight in). */
    bool flight_fade;
} BuildingState;

/* BuildingScreen's own part: event_routine = 1, and the door's store
 * bytes handed to the windows. */
void building_begin(WinSys *ws, const BuildingState *bs);

/* `in` is the pad for the one state that reads it directly: Rolf's
 * house waits on B or C itself while a profile is up. */
BuildingResult building_tick(BuildingState *bs, WinSys *ws, GameData *gd,
                             const Input *in);

/* CheckItemExistInventory (ps2.asm:19105): anyone in the party. */
bool building_party_has_item(const GameData *gd, int item_id);

#endif /* PS2_BUILDING_H */
