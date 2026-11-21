#ifndef PS2_INTRO_SELECT_H
#define PS2_INTRO_SELECT_H

#include "gamedata.h"
#include "window.h"

/* IntroScr_EventSelectGame (ps2.asm:19235): the backup check, the
 * NEW GAME / CONTINUE / ERASE GAME menu and everything under it. */

typedef enum {
    INTRO_SELECT_RUNNING = 0,
    INTRO_SELECT_TO_TITLE,      /* B at the menu                         */
    INTRO_SELECT_NEW_GAME,      /* loc_D384: name chosen, run the dream  */
    INTRO_SELECT_CONTINUE       /* loc_D3B6: slot loaded, go to the save
                                   building with $F764 = 1               */
} IntroSelectResult;

typedef struct {
    u16  sub_2;                 /* event_routine_sub_2 */
    u16  check_slot;            /* $FFFFC602 during the backup check */
    bool *backup_checked;       /* $FFFFFF00: survives leaving the screen */
    u8   subst_name[GD_NAME_MAX];   /* (build 163) six: lang.h */
} IntroSelect;

/* Sets ws->event_routine to 1, as IntroScreen does on entry. `gd` must
 * already hold gamedata_new_game() state. */
void intro_select_begin(IntroSelect *is, WinSys *ws, bool *backup_checked);

/* IntroScr_CheckRoutine. Call between win_run_cursors and win_prepare. */
IntroSelectResult intro_select_tick(IntroSelect *is, WinSys *ws,
                                    GameData *gd);

/* The WinSys slot_info hook, backed by the save files. */
void intro_select_slot_info(void *ctx, int slot, WinSlotInfo *out);

#endif /* PS2_INTRO_SELECT_H */
