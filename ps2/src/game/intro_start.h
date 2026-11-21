#ifndef PS2_INTRO_START_H
#define PS2_INTRO_START_H

#include "window.h"

/* IntroScr_EventStartGame (ps2.asm:19528): the dream. IntroScreen is
 * entered a second time for it, with building_index = 1 -- loc_D384 sets
 * that and screen_changed_flag once the name is chosen.
 *
 * Three of its five states stop the main loop for an effect: the ordered
 * dissolve in (loc_6BBE), the dissolve out (loc_6C54) and a palette fade
 * (loc_61F4). Those touch tile and palette memory, which this module does
 * not own, so a state that needs one returns the request and waits for
 * intro_start_effect_done(). */

typedef enum {
    DREAM_FX_NONE = 0,
    DREAM_FX_DISSOLVE_IN,   /* picture 1's map goes up, its art bleeds in */
    DREAM_FX_DISSOLVE_OUT,  /* picture 1's art is eaten away, line 3 cut  */
    DREAM_FX_PALETTE_IN     /* picture 2 goes up whole, line 3 fades in   */
} DreamEffect;

typedef enum {
    INTRO_START_RUNNING = 0,
    INTRO_START_EFFECT,     /* run intro_start_effect(), then _effect_done */
    INTRO_START_TO_LEVEL    /* loc_D5EA: the scripted walk to the tower   */
} IntroStartResult;

typedef struct {
    DreamEffect effect;
    int         picture;    /* 0 none, 1 the battle, 2 the waking */
} IntroStart;

/* Sets ws->event_routine to 1, as IntroScreen does on entry. */
void intro_start_begin(IntroStart *st, WinSys *ws);

IntroStartResult intro_start_tick(IntroStart *st, WinSys *ws);
DreamEffect      intro_start_effect(const IntroStart *st);
void             intro_start_effect_done(IntroStart *st, WinSys *ws);

#endif /* PS2_INTRO_START_H */
