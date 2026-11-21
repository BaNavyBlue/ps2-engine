#include "intro_select.h"

#include "lang.h"

#include <string.h>

/* event_routine values, 1-based as in the jump table at loc_D17A. */
enum {
    ST_BACKUP_CHECK = 1,   /* loc_D1C2 */
    ST_OPEN_MENU,          /* loc_D2B6 */
    ST_MENU_CHOICE,        /* loc_D2C2 */
    ST_NEW_ROOM_CHECK,     /* loc_D2F2 */
    ST_NEW_ASK_ANYWAY,     /* loc_D324 */
    ST_NEW_ANSWER,         /* loc_D330 */
    ST_NEW_OPEN_NAME,      /* loc_D362 */
    ST_NEW_NAME_DONE,      /* loc_D36E */
    ST_NEW_START,          /* loc_D384 */
    ST_CONT_PROMPT,        /* loc_D392 */
    ST_CONT_OPEN_SLOTS,    /* loc_D3AA */
    ST_CONT_SLOT_CHOSEN,   /* loc_D3B6 */
    ST_ERASE_PROMPT,       /* loc_D43A */
    ST_ERASE_OPEN_SLOTS,   /* loc_D44C */
    ST_ERASE_SLOT_CHOSEN,  /* loc_D458 */
    ST_ERASE_ASK,          /* loc_D4B8 */
    ST_ERASE_ANSWER,       /* loc_D4C4 */
    ST_ERASE_BACK          /* loc_D4EA */
};

static bool slot_in_use(int slot)
{
    /* The original tests the in-use word at $C69C in the slot. A file
     * being there is the same statement. */
    SlotSummary s;
    gamedata_slot_summary(slot, &s);
    return s.exists;
}

void intro_select_slot_info(void *ctx, int slot, WinSlotInfo *out)
{
    (void)ctx;
    SlotSummary s;
    gamedata_slot_summary(slot, &s);

    out->used  = s.exists;
    out->level = s.level;
    memcpy(out->name, s.name, sizeof out->name);
}

void intro_select_begin(IntroSelect *is, WinSys *ws, bool *backup_checked)
{
    memset(is, 0, sizeof *is);
    is->backup_checked = backup_checked;
    ws->event_routine  = ST_BACKUP_CHECK;
}

/* loc_D1C2. The cartridge compares each slot's checksum with its mirror
 * and repairs one from the other. A JSON file has no mirror, so the check
 * is whether it parses, and a file that does not is moved aside -- the
 * "can't fix it" branch, minus the overwrite. */
static void backup_check(IntroSelect *is, WinSys *ws)
{
    switch (is->sub_2 & 3) {
        case 0: {
            if (*is->backup_checked) {
                ws->event_routine++;
                return;
            }
            *is->backup_checked = true;
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            win_script(ws, 0x130B);     /* "Let me check the backup data." */
            is->check_slot = 0;
            is->sub_2++;
            break;
        }
        case 1: {
            SlotSummary s;
            gamedata_slot_summary(is->check_slot, &s);
            ws->meseta_value = (u32)is->check_slot + 1;

            if (s.exists && s.valid) {
                win_script(ws, 0x130C);         /* "...is okay." */
            } else if (s.exists) {
                win_script2(ws, 0x130D, 0x130F); /* problem; can't fix */
                gamedata_quarantine(is->check_slot);
            }

            is->check_slot++;
            if (is->check_slot == GD_SAVE_SLOTS) {
                is->sub_2++;
            }
            break;
        }
        case 2: {
            win_script(ws, 0x1310);     /* "Data check complete." */
            is->sub_2++;
            break;
        }
        default: {
            is->sub_2 = 0;
            ws->event_routine++;
            win_open(ws, WIN_CLOSE(1));
            break;
        }
    }
}

/* loc_B36C: the entry replaces ROLF only if something was typed. */
static void commit_name(const WinSys *ws, GameData *gd)
{
    bool typed = false;
    for (int i = 0; i < WIN_NAME_MAX; i++) {
        if (ws->name_entry[i] != 0 && ws->name_entry[i] != TXT_END) {
            typed = true;
        }
    }
    if (typed) {
        /* (build 163) The letters typed are the whole name: a six-letter
         * default's last letters go. (164) Six in a proportional
         * language. */
        gamedata_set_name(gd->chr[0].name, ws->name_entry,
                          lang_proportional() ? GD_NAME_MAX : GD_NAME_LEN);
    }
}

IntroSelectResult intro_select_tick(IntroSelect *is, WinSys *ws,
                                    GameData *gd)
{
    if (!win_idle(ws)) {
        return INTRO_SELECT_RUNNING;
    }
    if (ws->event_routine == 0) {
        return INTRO_SELECT_TO_TITLE;
    }

    switch (ws->event_routine) {
        case ST_BACKUP_CHECK: {
            backup_check(is, ws);
            break;
        }
        case ST_OPEN_MENU: {
            win_open(ws, WIN_GAME_SELECT);
            ws->event_routine++;
            break;
        }
        case ST_MENU_CHOICE: {
            if (ws->event_routine_sub != 0) {
                return INTRO_SELECT_TO_TITLE;
            }
            is->sub_2 = 0;
            if (ws->game_select == 0) {
                ws->event_routine = ST_NEW_ROOM_CHECK;
            } else if (ws->game_select == 1) {
                ws->event_routine = ST_CONT_PROMPT;
            } else {
                ws->event_routine = ST_ERASE_PROMPT;
            }
            break;
        }

        /* ---- NEW GAME ---- */
        case ST_NEW_ROOM_CHECK: {
            bool full = true;
            for (int s = 0; s < GD_SAVE_SLOTS; s++) {
                if (!slot_in_use(s)) {
                    full = false;
                }
            }
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            if (full) {
                win_script(ws, 0x1301);
                ws->event_routine = ST_NEW_ASK_ANYWAY;
            } else {
                win_script(ws, 0x1302);
                ws->event_routine = ST_NEW_OPEN_NAME;
            }
            break;
        }
        case ST_NEW_ASK_ANYWAY: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case ST_NEW_ANSWER: {
            if (is->sub_2 != 0) {
                is->sub_2 = 0;
                ws->event_routine -= 2;
                win_close_current(ws, WIN_CLOSE(1));
            } else if (ws->yes_no_input != 0) {
                win_script(ws, 0x1303);
                is->sub_2++;
            } else {
                win_script(ws, 0x1302);
                ws->event_routine++;
            }
            break;
        }
        case ST_NEW_OPEN_NAME: {
            win_open(ws, WIN_NAME_INPUT);
            ws->event_routine++;
            break;
        }
        case ST_NEW_NAME_DONE: {
            commit_name(ws, gd);
            memcpy(is->subst_name, gd->chr[0].name, GD_NAME_MAX);
            ws->char_name = is->subst_name;     /* character_index = 0 */
            win_script(ws, 0x1304);
            ws->event_routine++;
            break;
        }
        case ST_NEW_START: {
            return INTRO_SELECT_NEW_GAME;
        }

        /* ---- CONTINUE ---- */
        case ST_CONT_PROMPT: {
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            win_script(ws, 0x1305);
            is->sub_2 = 0;
            ws->event_routine++;
            break;
        }
        case ST_CONT_OPEN_SLOTS: {
            win_open(ws, WIN_SAVE_SLOTS);
            ws->event_routine++;
            break;
        }
        case ST_CONT_SLOT_CHOSEN: {
            if (ws->event_routine_sub != 0) {
                ws->event_routine -= 9;
                win_close_current(ws, WIN_CLOSE(2));
            } else if (is->sub_2 != 0) {
                is->sub_2 = 0;
                ws->window_index_saved = WIN_SAVE_SLOTS;
            } else if (slot_in_use(ws->save_slot)
                       && gamedata_load(gd, ws->save_slot)) {
                return INTRO_SELECT_CONTINUE;
            } else {
                win_script(ws, 0x130A);
                is->sub_2++;
            }
            break;
        }

        /* ---- ERASE GAME ---- */
        case ST_ERASE_PROMPT: {
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            win_script(ws, 0x1306);
            ws->event_routine++;
            break;
        }
        case ST_ERASE_OPEN_SLOTS: {
            win_open(ws, WIN_SAVE_SLOTS);
            ws->event_routine++;
            break;
        }
        case ST_ERASE_SLOT_CHOSEN: {
            if (ws->event_routine_sub != 0) {
                ws->event_routine -= 0xC;
                win_close_current(ws, WIN_CLOSE(2));
            } else if (is->sub_2 != 0) {
                is->sub_2 = 0;
                ws->window_index_saved = WIN_SAVE_SLOTS;
            } else {
                ws->meseta_value = (u32)ws->save_slot + 1;
                if (slot_in_use(ws->save_slot)) {
                    win_script(ws, 0x1307);
                    ws->event_routine++;
                } else {
                    win_script(ws, 0x130A);
                    is->sub_2++;
                }
            }
            break;
        }
        case ST_ERASE_ASK: {
            win_open(ws, WIN_YESNO3);
            ws->event_routine++;
            break;
        }
        case ST_ERASE_ANSWER: {
            if (ws->yes_no_input != 0) {
                win_script(ws, 0x1309);
            } else {
                win_script(ws, 0x1308);
                gamedata_erase(ws->save_slot);
            }
            ws->event_routine++;
            break;
        }
        case ST_ERASE_BACK: {
            ws->event_routine -= 0xE;
            win_close_current(ws, WIN_CLOSE(2));
            break;
        }
        default: {
            break;
        }
    }
    return INTRO_SELECT_RUNNING;
}
