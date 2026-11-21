#include "intro_start.h"

#include <string.h>

#define MUSIC_DREAM 0x8F

/* event_routine values, 1-based as in the jump table at loc_D502. */
enum {
    ST_FIRST_TEXT = 1,  /* loc_D516 */
    ST_BATTLE,          /* loc_D534 */
    ST_BATTLE_FADES,    /* loc_D572 */
    ST_WAKING,          /* loc_D59C */
    ST_LEAVE            /* loc_D5EA */
};

void intro_start_begin(IntroStart *st, WinSys *ws)
{
    memset(st, 0, sizeof *st);
    ws->event_routine = ST_FIRST_TEXT;
}

DreamEffect intro_start_effect(const IntroStart *st)
{
    return st->effect;
}

IntroStartResult intro_start_tick(IntroStart *st, WinSys *ws)
{
    if (st->effect != DREAM_FX_NONE) {
        return INTRO_START_EFFECT;
    }
    if (!win_idle(ws)) {
        return INTRO_START_RUNNING;
    }

    switch (ws->event_routine) {
        case ST_FIRST_TEXT: {
            win_open(ws, WIN_SCRIPT_MESSAGE_BIG);
            win_script(ws, 0x1501);
            ws->event_routine++;
            if (ws->music != NULL) {
                ws->music(ws->ctx, MUSIC_DREAM);
            }
            break;
        }
        case ST_BATTLE: {
            st->picture = 1;
            st->effect  = DREAM_FX_DISSOLVE_IN;
            return INTRO_START_EFFECT;
        }
        case ST_BATTLE_FADES: {
            st->effect = DREAM_FX_DISSOLVE_OUT;
            return INTRO_START_EFFECT;
        }
        case ST_WAKING: {
            st->picture = 2;
            st->effect  = DREAM_FX_PALETTE_IN;
            return INTRO_START_EFFECT;
        }
        case ST_LEAVE: {
            return INTRO_START_TO_LEVEL;
        }
        default: {
            break;
        }
    }
    return INTRO_START_RUNNING;
}

/* Each effect state ends the same way: queue the next script, move on. */
void intro_start_effect_done(IntroStart *st, WinSys *ws)
{
    switch (st->effect) {
        case DREAM_FX_DISSOLVE_IN: {
            win_script(ws, 0x1502);
            break;
        }
        case DREAM_FX_DISSOLVE_OUT: {
            win_script(ws, 0x1503);
            break;
        }
        case DREAM_FX_PALETTE_IN: {
            win_script(ws, 0x1504);
            break;
        }
        default: {
            break;
        }
    }
    st->effect = DREAM_FX_NONE;
    ws->event_routine++;
}
