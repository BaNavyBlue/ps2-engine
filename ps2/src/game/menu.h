#ifndef PS2_MENU_H
#define PS2_MENU_H

#include "gamedata.h"
#include "window.h"

/* ProcessPlayerMenu (ps2.asm:14099): C on the map opens ITEM / STATE /
 * TECH / STRNG / EQP. Like the buildings, it runs on the window
 * system's queues and only when the windows are idle; event_routine 0
 * is the menu shut, and the entry picked ($FFFFDE80) owns the states
 * from 1 up.
 *
 * All five entries are ported: Menu_ItemEntry (14143), Menu_StateEntry
 * (15044), Menu_TechEntry (15152), Menu_StrngEntry (15639) and
 * Menu_EqpEntry (15669), with the item effects (ItemAction_*) and the
 * field techniques (TechEffect_*). What an item does when it is pointed
 * at a container or a dungeon fixture (keys, dynamite, the key tube,
 * the cards, Musik) goes through Level_ChkTargetInteract, which game.c
 * answers from the level's objects (WinSys.facing_object). The Small
 * Key, Dynamite and the Key Tube find their targets; the cards and
 * Musik still take the "nothing there" path and show the item's
 * description, as the original does away from the object. */

typedef struct {
    bool demo_flag;         /* the scripted walks cannot open it        */
    bool event_word_high;   /* (build 97d) the event word's high byte is
                             * not 0 -- a one-shot case in hand or the
                             * Esper B1 hold: ProcessPlayerMenu's
                             * `tst.b (event_flags).w` keeps it shut */

    /* $FFFFE80A / $FFFFE80E: object slot 0's x and y, set by the level
     * screen each frame. On Motavia with $C716 set that is the jet
     * scooter; ItemAction_MruraGum's loc_9CD6 records it as the way out
     * of the Underwater Passage. */
    s16  slot0_x, slot0_y;

    s16  timer;             /* $FFFFDE5E: the 30 frames before a heal   */
    u16  give_result;       /* $FFFFDE8C: 0 give / drop, 1 refused      */

    /* What the level screen has to do once the menu is done with the
     * frame: screen_changed_flag = -1 with gd->level_* already set, or
     * the Visiphone's jump into Data Memory. */
    bool level_change;
    bool to_data_memory;

    /* demo_flag = 1, demo_index = this, demo_input_index = 0: the walk
     * an item used on a door starts (Dynamite 4, the Key Tube 5). -1
     * when nothing was started. The level screen picks it up and clears
     * it; the walk itself waits for the menu to close, as ObjCL_Main
     * only reads the demo with no window up. */
    int  start_demo;

    /* `move.w #N, (event_flags).w` from an item (a card at its dam's
     * panel: 8, loc_809C's case 8). -1 when nothing was written; the
     * level screen picks it up and clears it. */
    int  event_flags;

    /* The routine waited for its sound to end before returning
     * (TechEffect_Musik): the level screen holds everything but the
     * sequencer until the track ends. Cleared by the level screen. */
    bool sound_wait;

    /* ItemAction_Hidapipe's `move.w #$FFFF, $FFFFCB0E.w`, after its
     * sound: the level screen sets the encounter state's flag. */
    bool hidapipe;
} MenuState;

void menu_init(MenuState *ms);

/* Once a frame, after the party has moved and before the A button is
 * looked at. */
void menu_tick(MenuState *ms, WinSys *ws, GameData *gd, const Input *in);

#endif /* PS2_MENU_H */
