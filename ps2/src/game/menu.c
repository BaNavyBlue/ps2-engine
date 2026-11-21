#include "menu.h"
#include "rom_ids.h"
#include "script_ids.h"

#include <stdio.h>
#include <string.h>


enum { MENU_ITEM, MENU_STATE, MENU_TECH, MENU_STRNG, MENU_EQP };
enum { ACTION_USE, ACTION_GIVE, ACTION_TOSS };

#define CHAR_NEI 1

/* The kind byte of InventoryData+$C: bits 0-2 say where it is worn. */
#define KIND_HEAD  1
#define KIND_RIGHT 2
#define KIND_LEFT  3
#define KIND_BODY  4
#define KIND_LEGS  5

#define STATUS_POISON 0x8000
#define HEAL_WAIT     0x1E

void menu_init(MenuState *ms)
{
    memset(ms, 0, sizeof *ms);
    ms->start_demo = -1;
    ms->event_flags = -1;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static CharData *chr_of(GameData *gd, int index)
{
    return &gd->chr[index % GD_CHARS];
}

static void sound(WinSys *ws, u8 id)
{
    if (ws->sound != NULL) {
        ws->sound(ws->ctx, id);
    }
}

/* `move.w #id, (window_index).w` on top of a word already queued this
 * frame replaces it: the ROM writes the same location twice. */
static void win_replace(WinSys *ws, u16 word)
{
    ws->window_index[0] = word;
}

static void close_current(WinSys *ws)
{
    win_close_current(ws, WIN_CLOSE(1));
}

static void redraw_char_stats(WinSys *ws)
{
    win_open(ws, WIN_F_INSTANT | WIN_F_REDRAW | WIN_MENU_CHAR_STATS);
}

static bool pressed_bc(const Input *in)
{
    return in->pressed.b || in->pressed.c;
}

/* RemoveItemFromInventory (ps2.asm:16063): character_index's bag loses
 * the entry at item_slot; everything after it moves down one and the
 * last slot is cleared. */
static void remove_item(WinSys *ws, GameData *gd)
{
    u8 *count;
    u8 *items = gamedata_bag(gd, ws->character_index, &count);
    int slot  = ws->item_slot % GD_ITEM_SLOTS;

    (*count)--;
    if (slot < GD_ITEM_SLOTS - 1) {
        memmove(items + slot, items + slot + 1,
                (size_t)(GD_ITEM_SLOTS - 1 - slot));
    }
    items[GD_ITEM_SLOTS - 1] = 0;
}

/* AddItemToInventory (16086): character_index_2's bag grows by one. */
static void add_item(WinSys *ws, GameData *gd)
{
    u8 *count;
    u8 *items = gamedata_bag(gd, ws->character_index_2, &count);

    (*count)++;
    items[(*count - 1) % GD_ITEM_SLOTS] = ws->item_index;
}

/* loc_AC2E: clear one equipment slot, named by its kind byte. */
static void clear_slot(CharData *c, int kind)
{
    if (kind >= 1 && kind <= EQUIP_COUNT) {
        c->equip[kind - 1] = 0;
    }
}

/* loc_AE2E: before an item is given or dropped, take it off if it is
 * worn -- bonuses off, the slot it sat in cleared, bonuses back on. */
static void unequip_before_losing(WinSys *ws, GameData *gd)
{
    u8 *count;
    u8 *items = gamedata_bag(gd, ws->character_index, &count);
    u8 *entry = &items[ws->item_slot % GD_ITEM_SLOTS];

    if (!(*entry & 0x80)) {
        return;
    }
    *entry &= 0x7F;

    CharData *c = chr_of(gd, ws->character_index);
    gamedata_equip_bonuses_remove(gd, ws->character_index);

    int kind = gamedata_item_action_flags(ws->item_index) & 7;
    if (kind == KIND_RIGHT) {
        /* loc_AE84: whichever hand holds it. */
        if ((c->equip[EQUIP_RIGHT] & 0x7F) == ws->item_index) {
            clear_slot(c, KIND_RIGHT);
        } else {
            clear_slot(c, KIND_LEFT);
        }
    } else if (kind == KIND_LEFT) {
        clear_slot(c, KIND_RIGHT);
        clear_slot(c, KIND_LEFT);
    } else {
        clear_slot(c, kind);
    }
    gamedata_equip_bonuses_add(gd, ws->character_index);
}

/* Level_TechEffect_SubtractTP (15618). */
static void subtract_tp(WinSys *ws, GameData *gd)
{
    CharData *c = chr_of(gd, ws->character_index);
    c->tp = (u16)(c->tp - gamedata_tech_tp(ws->technique_index));
}

/* loc_9BFE: the $2E word of whatever the party faces, or -1. */
static int facing_word(const WinSys *ws)
{
    return (ws->facing_object != NULL) ? ws->facing_object(ws->ctx) : -1;
}

/* loc_9C12: the index byte of an event object (type 0) in the cell the
 * party faces, which is what Dynamite, the Key Tube, the cards and
 * Mruragum look for; 0 for anything else, or nothing. The ROM returns
 * a1 = event_flags + that byte alongside it. */
static int facing_event(const WinSys *ws)
{
    int word = facing_word(ws);
    return (word >= 0 && (word >> 8) == (EVENT_KIND_EVENTS >> 8))
        ? (word & 0xFF) : 0;
}

/* Mruragum's target is the same test for the water around the
 * Underwater Passage, event $F (loc_9CBE). */
static bool facing_object(const WinSys *ws)
{
    return facing_event(ws) == EVENT_WATER_COLOUR;
}

/* The "'Character' uses 'Item'" message was opened at sub 0; a pipe or
 * the sword replaces it with the level jump. */
static void request_level(MenuState *ms, GameData *gd, u16 level, u16 y,
                          u16 x)
{
    gd->level_index = level;
    gd->level_y     = y;
    gd->level_x     = x;
    ms->level_change = true;
}

/* ------------------------------------------------------------------ */
/* ITEM: the USE effects, ItemAction_* (14204-14939)                   */
/* ------------------------------------------------------------------ */

/* ItemAction_NoAction. */
static void use_no_action(WinSys *ws, int d1)
{
    if (d1 == 0) {
        win_script_set(ws, SCRIPT_ITEM_USES);
    } else if (d1 == 1) {
        win_script_set(ws, SCRIPT_ITEM_NOTHING_HAPPENS);
        ws->event_routine_sub++;
    } else {
        win_close_all(ws);
    }
}

/* One description at d1 0 and the menu shut on the next button. */
static void use_describe(WinSys *ws, int d1, u16 script)
{
    if (d1 != 0) {
        win_close_all(ws);
        return;
    }
    win_script_set(ws, script);
}

/* ItemAction_SmallKey (14285): facing a chest (type 2) whose flag has
 * bit 7, clear it -- "'Character' uses 'Item'", then "The container
 * opens." Anything else, or a chest already unlocked, gets the
 * description. */
static void use_small_key(WinSys *ws, GameData *gd, int d1)
{
    if (d1 == 0) {
        int word = facing_word(ws);
        if (word >= 0 && (word >> 8) == (EVENT_KIND_CHESTS >> 8)) {
            u8 *flag = &gd->c700[(FLAG_CHESTS + (word & 0xFF)) & 0xFF];
            if (*flag & 0x80) {
                *flag &= 0x7F;
                win_script_set(ws, SCRIPT_ITEM_USES);
                return;
            }
        }
        win_script_set(ws, SCRIPT_ITEM_KEY_SMALL_CONTAINER);
        ws->event_routine_sub++;
        return;
    }
    if (d1 != 1) {
        win_close_all(ws);
        return;
    }
    win_script_set(ws, SCRIPT_ITEM_CONTAINER_OPENS);
    ws->event_routine_sub++;
}

/* loc_9C4C and loc_9C90, shared by Dynamite and the Key Tube: bit 0 of
 * event_flags[index] set -- already set, and the item is only looked at
 * -- then `script`, demo `demo` queued, and RemoveItemFromInventory. */
static bool use_on_door(MenuState *ms, WinSys *ws, GameData *gd, int index,
                        u16 script, int demo)
{
    u8 *flag = &gd->c700[(FLAG_EVENT_SLOTS + index) & 0xFF];
    if (*flag & 1) {
        return false;
    }
    *flag |= 1;
    win_script_set(ws, script);
    ms->start_demo = demo;
    remove_item(ws, gd);
    return true;
}

/* ItemAction_Dynamite (14336): facing a dynamite door -- an event
 * object with index $18, $19 or $1A (Nido, and the Biosystems lab's
 * ground floor and F2) -- whose flag is clear: "'Character' uses
 * 'Item'...." and demo 4, the party backing away before the blast.
 * Anything else: "'Character' takes out 'Item' and puts it back."
 * ($14). The next button closes the menu either way. */
static void use_dynamite(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    if (d1 != 0) {
        win_close_all(ws);
        return;
    }
    int index = facing_event(ws);
    if (index >= EVENT_DYNAMITE_DOOR_1 && index <= EVENT_DYNAMITE_DOOR_3
        && use_on_door(ms, ws, gd, index, SCRIPT_ITEM_USES, DEMO_INDEX_DYNAMITE_USED)) {
        return;
    }
    win_script_set(ws, SCRIPT_ITEM_TAKES_OUT_BUT_THEN);
}

/* ItemAction_KeyTube (14360): the same on the locked door with index $1B
 * -- "'Character' puts 'Item' inside." ($18) and demo 5; otherwise "It
 * is a metal pole, 20cm long, with markings." ($16). */
static void use_key_tube(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    if (d1 != 0) {
        win_close_all(ws);
        return;
    }
    if (facing_event(ws) == EVENT_STRONG_SHUTTER
        && use_on_door(ms, ws, gd, EVENT_STRONG_SHUTTER, SCRIPT_ITEM_PUTS_INSIDE,
                       DEMO_INDEX_KEY_USED)) {
        return;
    }
    win_script_set(ws, SCRIPT_ITEM_METAL_POLE_20CM_LONG);
}

static void use_mrura_gum(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    if (d1 == 0) {
        if (facing_object(ws)) {
            win_script_set(ws, SCRIPT_ITEM_PARTY_EATS_EACH_BITE);
            return;
        }
        win_script_set(ws, SCRIPT_ITEM_OCEAN_BOTTOM_WOULD_GOOD);
        ws->event_routine_sub++;
        return;
    }
    if (d1 != 1) {
        win_close_all(ws);
        return;
    }
    /* loc_9CD6: into the Underwater Passage, remembering the way out --
     * $C64C Motavia, $C64E / $C650 object slot 0's y and x ($FFFFE80E,
     * $FFFFE80A) & $FFF0: the scooter, where the gum was eaten. A pipe or
     * Hinas from the passage or Climatrol comes back to it there. */
    gamedata_set_word(gd, GD_ENTRANCE_MAP, LEVEL_MOTAVIA_OUTSIDE);
    gamedata_set_word(gd, GD_ENTRANCE_Y, (u16)(ms->slot0_y & 0xFFF0));
    gamedata_set_word(gd, GD_ENTRANCE_X, (u16)(ms->slot0_x & 0xFFF0));
    request_level(ms, gd, LEVEL_UNDERWATER_PASSAGE, 0x170, 0x700);
    ws->event_routine_sub++;
}

/* ItemAction_GreenCard .. ItemAction_RedCard (ps2.asm:14406-14446):
 * `first` is the card's own door event (EVENT_DAM_LOCK_1, _3, _5, _7:
 * $1C, $1E, $20, $22; its byte FLAG_<colour>_DAM_DOOR) and the
 * description its script. At d1 0 the description is queued, then
 * loc_9D3E looks at what is faced. The card's door, its flag clear, is
 * "'Character' puts 'Item' inside." ($18) and demo 5, the door swinging
 * open; either way event_routine_sub + 1, so the next button (d1 2)
 * closes the menu. The next index up, `first` + 1, is the dam's control
 * panel (loc_9D70): `bset #0` on its byte of event_flags
 * (FLAG_<colour>_DAM_PANEL: $C72D, $C72F, $C731, $C733) -- already set, it is the description path again --
 * then `move.w #8, (event_flags).w`, $18, and RemoveItemFromInventory:
 * the card is used up, with no sub + 1, so the next visit (d1 1) is "The
 * dam locks open, and rushing water is heard." ($1B) with
 * SXFID_DamOpened. The dam's own map does not change; Motavia's does,
 * at its next load once Climatrol is destroyed (loc_9258 with $C735,
 * loc_9302: the panel's table fills the empty canals with water --
 * route motavia_dam_patches). event_flags' low byte 8 is
 * loc_809C's case 8, CAMERA_DAMS_CHECK: the level screen's
 * (MenuState.event_flags). */
static void use_card(MenuState *ms, WinSys *ws, GameData *gd, int d1,
                     int first, u16 script)
{
    if (d1 == 0) {
        win_script_set(ws, script);
        int index = facing_event(ws);
        if (index == first) {
            u8 *flag = &gd->c700[(FLAG_EVENT_SLOTS + index) & 0xFF];
            if (!(*flag & 1)) {
                *flag |= 1;
                win_script_set(ws, SCRIPT_ITEM_PUTS_INSIDE);
                ms->start_demo = DEMO_INDEX_KEY_USED;
            }
        } else if (index == first + 1) {
            u8 *flag = &gd->c700[(FLAG_EVENT_SLOTS + index) & 0xFF];
            if (!(*flag & 1)) {
                *flag |= 1;
                ms->event_flags = CAMERA_DAMS_CHECK;
                win_script_set(ws, SCRIPT_ITEM_PUTS_INSIDE);
                remove_item(ws, gd);
                return;
            }
        }
        ws->event_routine_sub++;
        return;
    }
    /* loc_9D92 */
    win_script_set(ws, SCRIPT_NONE);
    if (d1 != 1) {
        win_close_all(ws);
        return;
    }
    win_script_set(ws, SCRIPT_ITEM_DAM_LOCKS_OPEN_RUSHING);
    sound(ws, SFX_DAM_OPENED);
    ws->event_routine_sub++;
}

/* Shared by Telepipe and Ryuka: back to the last town. */
static void use_telepipe(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    if (d1 == 0) {
        win_script_set(ws, SCRIPT_ITEM_BODY_FEELS_VERY_LIGHT);
        if (gd->level_index > LEVEL_DEZOLIS_SKURE) {
            win_script_set(ws, SCRIPT_ITEM_CANT_USED_HERE);
            ws->event_routine_sub++;
        }
        return;
    }
    if (d1 != 1) {
        win_close_all(ws);
        return;
    }
    /* loc_9E18 */
    u16 level, y, x;
    if (gd->planet == 0) {
        level = LEVEL_PASEO;
        y     = 0x100;
        x     = 0x1F0;
        if (gamedata_word(gd, GD_LAST_SAVE_PLANET) == 0) {
            level = LEVEL_PASEO;
            y     = 0x80;
            x     = 0x110;
            if (gamedata_word(gd, GD_LAST_SAVE_VISIPHONE) == 0) {
                level = gamedata_word(gd, GD_LAST_SAVE_MAP);
                y     = gamedata_word(gd, GD_LAST_SAVE_Y);
                x     = gamedata_word(gd, GD_LAST_SAVE_X);
            }
        }
    } else {
        level = LEVEL_DEZOLIS_SKURE;
        y     = 0x4F0;
        x     = 0x3C0;
        if (gamedata_word(gd, GD_LAST_SAVE_PLANET) != 0) {
            level = LEVEL_AUKBA;
            y     = 0x60;
            x     = 0x70;
            if (gamedata_word(gd, GD_LAST_SAVE_VISIPHONE) == 0) {
                level = gamedata_word(gd, GD_LAST_SAVE_MAP);
                y     = gamedata_word(gd, GD_LAST_SAVE_Y);
                x     = gamedata_word(gd, GD_LAST_SAVE_X);
            }
        }
    }
    /* loc_9E98 */
    gd->jet_scooter = 0;
    request_level(ms, gd, level, y, x);
    ws->event_routine_sub++;
    sound(ws, SFX_TELEPORT);
    if (ws->menu_choice != MENU_ITEM) {
        subtract_tp(ws, gd);
    } else {
        remove_item(ws, gd);
    }
}

/* Shared by Escapipe and Hinas: out of the dungeon. */
static void use_escapipe(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    if (d1 == 0) {
        win_script_set(ws, SCRIPT_ITEM_BODY_FEELS_VERY_LIGHT);
        if (gd->level_index < LEVEL_ESPER_MANSION_B1) {
            win_script_set(ws, SCRIPT_ITEM_CANT_USED_HERE);
            ws->event_routine_sub++;
        }
        return;
    }
    if (d1 != 1) {
        win_close_all(ws);
        return;
    }
    request_level(ms, gd, gamedata_word(gd, GD_ENTRANCE_MAP), gamedata_word(gd, GD_ENTRANCE_Y),
                  gamedata_word(gd, GD_ENTRANCE_X));
    ws->event_routine_sub++;
    sound(ws, SFX_TELEPORT);
    if (ws->menu_choice != MENU_ITEM) {
        subtract_tp(ws, gd);
    } else {
        remove_item(ws, gd);
    }
}

/* ItemAction_Hidapipe (14573): only on Motavia or Dezolis outside
 * ($13, else $27 and the next button closes). d1 1: SXFID_Hidapipe,
 * waited out in place, then $FFFFCB0E = $FFFF -- ProcessRandomBattle's
 * counter then gains 2 a step fewer on the planets' surface, halving
 * the encounters on foot -- and the item goes. loc_6F1C clears it again
 * on the next way into a dungeon. */
static void use_hidapipe(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    if (d1 == 0) {
        win_script_set(ws, SCRIPT_ITEM_TONES_COVER_SOUND_STEPS);
        if (gd->level_index != LEVEL_MOTAVIA_OUTSIDE
            && gd->level_index != LEVEL_DEZOLIS_SKURE) {
            win_script_set(ws, SCRIPT_ITEM_CANT_USED_HERE);
            ws->event_routine_sub++;
        }
        return;
    }
    if (d1 != 1) {
        win_close_all(ws);
        return;
    }
    sound(ws, SFX_HIDAPIPE);
    ms->sound_wait = true;
    ms->hidapipe   = true;
    ws->event_routine_sub++;
    remove_item(ws, gd);
}

/* ItemAction_Monomate: the three mates. */
static void use_mate(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    CharData *target = chr_of(gd, ws->character_index_2);

    switch (d1) {
        case 0: {
            win_replace(ws, WIN_CHOSEN_ITEM_CHAR);
            break;
        }
        case 1: {
            win_open(ws, WIN_SCRIPT_MESSAGE);
            win_script_set(ws, SCRIPT_ITEM_USES);
            if (target->hp == 0) {
                win_script_set(ws, SCRIPT_TECH_IS_DEAD);
                ws->event_routine_sub--;
            }
            ws->event_routine_sub += 2;
            break;
        }
        case 2: {
            close_current(ws);
            break;
        }
        case 3: {
            win_script_set(ws, SCRIPT_ITEM_WOUNDS_HEALED);
            if (target->status & STATUS_POISON) {
                win_script_set(ws, SCRIPT_TECH_BUT_POISON_STILL_REMAINS);
                ws->event_routine_sub += 3;
            }
            ws->event_routine_sub++;
            remove_item(ws, gd);
            break;
        }
        case 4: {
            win_open(ws, WIN_MENU_CHAR_STATS);
            ms->timer = HEAL_WAIT;
            ws->event_routine_sub++;
            break;
        }
        case 5: {
            ms->timer--;
            if (ms->timer >= 0) {
                break;
            }
            u16 hp = target->hp;
            if (ws->item_index == ITEM_MONOMATE) {
                hp = (u16)(hp + 0x14);
            } else if (ws->item_index == ITEM_DIMATE) {
                hp = (u16)(hp + 0x3C);
            } else {
                hp = target->max_hp;
            }
            if (hp >= target->max_hp) {
                hp = target->max_hp;
            }
            target->hp = hp;
            redraw_char_stats(ws);
            ws->event_routine_sub++;
            sound(ws, SFX_HEALED);
            break;
        }
        case 6: {
            if (pressed_bc(ws->last_input)) {
                win_close_all(ws);
            }
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
}

static void use_antidote(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    CharData *target = chr_of(gd, ws->character_index_2);

    switch (d1) {
        case 0: {
            win_replace(ws, WIN_CHOSEN_ITEM_CHAR);
            break;
        }
        case 1: {
            win_open(ws, WIN_SCRIPT_MESSAGE);
            win_script_set(ws, SCRIPT_ITEM_USES);
            if (target->hp == 0) {
                win_script_set(ws, SCRIPT_TECH_IS_DEAD);
                ws->event_routine_sub--;
            }
            ws->event_routine_sub += 2;
            break;
        }
        case 2: {
            close_current(ws);
            break;
        }
        case 3: {
            win_script_set(ws, SCRIPT_ITEM_CURED_POISON);
            if (!(target->status & STATUS_POISON)) {
                win_script_set(ws, SCRIPT_ITEM_NOTHING_HAPPENS);
                ws->event_routine_sub += 3;
            }
            ws->event_routine_sub++;
            remove_item(ws, gd);
            break;
        }
        case 4: {
            win_open(ws, WIN_MENU_CHAR_STATS);
            ms->timer = HEAL_WAIT;
            ws->event_routine_sub++;
            break;
        }
        case 5: {
            ms->timer--;
            if (ms->timer >= 0) {
                break;
            }
            target->status = 0;
            redraw_char_stats(ws);
            ws->event_routine_sub++;
            sound(ws, SFX_POISON_CURED);
            break;
        }
        case 6: {
            if (pressed_bc(ws->last_input)) {
                win_close_all(ws);
            }
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
}

/* Everyone standing and unpoisoned back to full HP. */
static void heal_party_full(GameData *gd)
{
    for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX; m++) {
        CharData *c = chr_of(gd, gd->party_member_id[m]);
        if ((c->status & STATUS_POISON) || c->hp == 0) {
            continue;
        }
        c->hp = c->max_hp;
    }
}

static void use_star_mist(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    switch (d1) {
        case 0: {
            win_script_set2(ws, SCRIPT_ITEM_USES, SCRIPT_ITEM_STRONG_SCENT_FILLS_AREA);
            break;
        }
        case 1: {
            remove_item(ws, gd);
            win_open(ws, WIN_MENU_CHAR_STATS);
            ms->timer = HEAL_WAIT;
            ws->event_routine_sub++;
            break;
        }
        case 2: {
            ms->timer--;
            if (ms->timer >= 0) {
                break;
            }
            heal_party_full(gd);
            redraw_char_stats(ws);
            ws->event_routine_sub++;
            sound(ws, SFX_HEALED);
            break;
        }
        default: {
            if (pressed_bc(ws->last_input)) {
                win_close_all(ws);
            }
            break;
        }
    }
}

static void use_moon_dew(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    CharData *target = chr_of(gd, ws->character_index_2);

    switch (d1) {
        case 0: {
            win_replace(ws, WIN_CHOSEN_ITEM_CHAR);
            break;
        }
        case 1: {
            win_open(ws, WIN_SCRIPT_MESSAGE);
            win_script_set2(ws, SCRIPT_ITEM_USES, SCRIPT_ITEM_STRONG_SCENT_FILLS_AREA);
            ws->event_routine_sub++;
            break;
        }
        case 2: {
            remove_item(ws, gd);
            win_script_set(ws, (target->hp == 0) ? SCRIPT_ITEM_ALIVE_AGAIN : SCRIPT_ITEM_WOUNDS_HEALED);
            ws->event_routine_sub++;
            break;
        }
        case 3: {
            win_open(ws, WIN_MENU_CHAR_STATS);
            ms->timer = HEAL_WAIT;
            ws->event_routine_sub++;
            break;
        }
        case 4: {
            ms->timer--;
            if (ms->timer >= 0) {
                break;
            }
            redraw_char_stats(ws);
            ws->event_routine_sub++;
            sound(ws, SFX_REVIVED);
            target->hp = target->max_hp;
            break;
        }
        default: {
            if (pressed_bc(ws->last_input)) {
                win_close_all(ws);
            }
            break;
        }
    }
}

static void use_nei_sword(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    if (d1 == 0) {
        win_script_set(ws, SCRIPT_ITEM_BODY_FEELS_VERY_LIGHT);
        if (gd->level_index < LEVEL_NOAH_GROUND_F) {
            win_script_set(ws, SCRIPT_ITEM_CANT_USED_HERE);
            ws->event_routine_sub++;
        }
        return;
    }
    if (d1 != 1) {
        win_close_all(ws);
        return;
    }
    request_level(ms, gd, LEVEL_ESPER_MANSION_F1, 0x100, 0x100);
    ws->event_routine_sub++;
    sound(ws, SFX_TELEPORT);
}

static void use_visiphone(MenuState *ms, WinSys *ws, int d1)
{
    if (d1 == 0) {
        win_script_set(ws, SCRIPT_ITEM_MAKING_CONTACT_DATA_MEMORY);
        return;
    }
    /* Into Data Memory as a building, with the map position kept. */
    ms->to_data_memory = true;
}

/* ItemAction_UseEntry (14204). */
static void item_use(MenuState *ms, WinSys *ws, GameData *gd)
{
    int d1 = ws->event_routine_sub;

    if (d1 == 0) {
        win_open(ws, WIN_SCRIPT_MESSAGE);
        ws->event_routine_sub++;
    }

    CharData *user = chr_of(gd, ws->character_index);
    if (user->hp == 0) {
        if (ws->event_routine_sub == 1) {
            win_script_set(ws, SCRIPT_ITEM_IS_DEAD);
            ws->event_routine_sub++;
            return;
        }
        ws->event_routine_sub--;
        close_current(ws);
        return;
    }

    int item = ws->item_index;
    if (!(gamedata_item_action_flags(item) & 0x08)) {
        use_no_action(ws, d1);
        return;
    }

    switch (item) {
        case ITEM_NEI_SWORD: {
            use_nei_sword(ms, ws, gd, d1);
            break;
        }
        case ITEM_TEIM: {
            use_describe(ws, d1, SCRIPT_ITEM_HOW_CAN_GET_FATHER);
            break;
        }
        case ITEM_VISIPHONE: {
            use_visiphone(ms, ws, d1);
            break;
        }
        case ITEM_SMALL_KEY: {
            use_small_key(ws, gd, d1);
            break;
        }
        case ITEM_DYNAMITE: {
            use_dynamite(ms, ws, gd, d1);
            break;
        }
        case ITEM_KEY_TUBE: {
            use_key_tube(ms, ws, gd, d1);
            break;
        }
        case ITEM_MRURA_GUM: {
            use_mrura_gum(ms, ws, gd, d1);
            break;
        }
        case ITEM_GREEN_CARD: {
            use_card(ms, ws, gd, d1, EVENT_DAM_LOCK_1, SCRIPT_ITEM_ITS_CARD_WHICH_SHINES);
            break;
        }
        case ITEM_BLUE_CARD: {
            use_card(ms, ws, gd, d1, EVENT_DAM_LOCK_3, SCRIPT_ITEM_ITS_CARD_WHICH_BLUISH);
            break;
        }
        case ITEM_YELLOW_CARD: {
            use_card(ms, ws, gd, d1, EVENT_DAM_LOCK_5, SCRIPT_ITEM_ITS_YELLOW_CARD_LIKE);
            break;
        }
        case ITEM_RED_CARD: {
            use_card(ms, ws, gd, d1, EVENT_DAM_LOCK_7, SCRIPT_ITEM_ITS_CARD_WHICH_RED);
            break;
        }
        case ITEM_LETTER: {
            use_describe(ws, d1, SCRIPT_ITEM_NOTE_SAID_DARUM_DAUGHTER);
            break;
        }
        case ITEM_RECORDER: {
            if (d1 != 0) {
                win_close_all(ws);
            } else {
                win_script_set2(ws, SCRIPT_ITEM_DEVICE_RECORDS_ALL_EVENTS, SCRIPT_ITEM_IF_CAN_DELIVERED_PASEO);
            }
            break;
        }
        case ITEM_MRURA_LEAF: {
            use_describe(ws, d1, SCRIPT_ITEM_LEAF_FROM_MARUERA_TREE);
            break;
        }
        case ITEM_PLSM_RING: {
            use_describe(ws, d1, SCRIPT_ITEM_CANT_LET_GO_ITEM);
            break;
        }
        case ITEM_PRISM: {
            use_describe(ws, d1, SCRIPT_ITEM_PRISM_SHINES_STRANGE_BRILLIANCE);
            break;
        }
        case ITEM_TELEPIPE: {
            use_telepipe(ms, ws, gd, d1);
            break;
        }
        case ITEM_ESCAPIPE: {
            use_escapipe(ms, ws, gd, d1);
            break;
        }
        case ITEM_HIDAPIPE: {
            use_hidapipe(ms, ws, gd, d1);
            break;
        }
        case ITEM_MONOMATE: {
            use_mate(ms, ws, gd, d1);
            break;
        }
        case ITEM_DIMATE: {
            use_mate(ms, ws, gd, d1);
            break;
        }
        case ITEM_TRIMATE: {
            use_mate(ms, ws, gd, d1);
            break;
        }
        case ITEM_ANTIDOTE: {
            use_antidote(ms, ws, gd, d1);
            break;
        }
        case ITEM_STAR_MIST: {
            use_star_mist(ms, ws, gd, d1);
            break;
        }
        case ITEM_MOON_DEW: {
            use_moon_dew(ms, ws, gd, d1);
            break;
        }
        default: {
            use_no_action(ws, d1);
            break;
        }
    }
}

/* ItemAction_GiveEntry (14926). */
static void item_give(MenuState *ms, WinSys *ws, GameData *gd)
{
    int d1 = ws->event_routine_sub;

    if (d1 == 0) {
        win_open(ws, WIN_CHOSEN_ITEM_CHAR);
        ws->event_routine_sub++;
        return;
    }
    if (d1 == 1) {
        CharData *target = chr_of(gd, ws->character_index_2);

        if (target->hp == 0) {
            win_script_set(ws, SCRIPT_TECH_IS_DEAD);
            ms->give_result = 1;
        } else {
            int full = (target->item_count >> 4) & 1;
            ms->give_result = (u16)full;
            win_script_set(ws, SCRIPT_ITEM_CANT_CARRY_ANY_MORE);
            if (ws->item_index == ITEM_PLSM_RING) {
                win_script_set(ws, SCRIPT_ITEM_CANT_LET_GO_ITEM);
                full = 1;
                ms->give_result = 1;
            }
            if (full == 0) {
                win_script_set(ws, SCRIPT_ITEM_TAKES_OUT_PUTS_BACK);
                if (ws->character_index != ws->character_index_2) {
                    win_script_set(ws, SCRIPT_ITEM_GIVES_TO);
                    if (chr_of(gd, ws->character_index)->hp == 0) {
                        win_script_set(ws, SCRIPT_ITEM_TAKES_FROM_SACK);
                    }
                }
            }
        }
        ws->event_routine_sub++;
        win_open(ws, WIN_SCRIPT_MESSAGE);
        return;
    }
    /* loc_A342 */
    if (ms->give_result == 0) {
        unequip_before_losing(ws, gd);
        remove_item(ws, gd);
        add_item(ws, gd);
        win_close_all(ws);
    } else {
        close_current(ws);
    }
}

/* ItemAction_TossEntry (14985). */
static void item_toss(MenuState *ms, WinSys *ws, GameData *gd)
{
    if (ws->event_routine_sub == 0) {
        if (chr_of(gd, ws->character_index)->hp == 0) {
            ms->give_result = 0;
            win_script_set(ws, SCRIPT_ITEM_IS_DEAD);
        } else {
            int droppable = (gamedata_item_action_flags(ws->item_index)
                             >> 7) & 1;
            ms->give_result = (u16)droppable;
            win_script_set(ws, droppable ? SCRIPT_ITEM_DO_REALLY_WANT_DROP : SCRIPT_ITEM_ITS_NOT_GOOD_IDEA);
        }
        ws->event_routine_sub++;
        win_open(ws, WIN_SCRIPT_MESSAGE);
        ws->event_routine_sub_2 = 0;
        return;
    }

    if (ms->give_result == 0) {
        close_current(ws);
        return;
    }

    switch (ws->event_routine_sub_2) {
        case 0: {
            win_open(ws, WIN_YESNO);
            ws->event_routine_sub_2++;
            break;
        }
        case 1: {
            win_script_set(ws, (ws->yes_no_input == 0) ? SCRIPT_ITEM_DROPS : SCRIPT_ITEM_CHANGES_HIS_MIND_KEEPS);
            ws->event_routine_sub_2++;
            break;
        }
        default: {
            if (ws->yes_no_input != 0) {
                win_close_current(ws, WIN_CLOSE(2));
                break;
            }
            unequip_before_losing(ws, gd);
            remove_item(ws, gd);
            win_close_all(ws);
            break;
        }
    }
}

/* Menu_ItemEntry (14143). */
static void item_entry(MenuState *ms, WinSys *ws, GameData *gd)
{
    switch (ws->event_routine) {
        case 1: {
            win_open(ws, WIN_MENU_ITEM_CHAR);
            ws->event_routine++;
            break;
        }
        case 2: {
            if (ws->event_routine_sub != 0) {
                close_current(ws);
                break;
            }
            if (chr_of(gd, ws->character_index)->item_count == 0) {
                win_script_set(ws, SCRIPT_ITEM_ISNT_CARRYING_ANYTHING);
                win_open(ws, WIN_SCRIPT_MESSAGE);
                ws->event_routine_sub++;
                break;
            }
            win_open(ws, WIN_MENU_ITEM_LIST);
            ws->event_routine++;
            break;
        }
        case 3: {
            win_open(ws, WIN_ITEM_ACTION);
            ws->event_routine++;
            break;
        }
        default: {
            switch (ws->item_action % 3) {
                case ACTION_USE: {
                    item_use(ms, ws, gd);
                    break;
                }
                case ACTION_GIVE: {
                    item_give(ms, ws, gd);
                    break;
                }
                default: {
                    item_toss(ms, ws, gd);
                    break;
                }
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* STATE: Menu_StateEntry (15044)                                      */
/* ------------------------------------------------------------------ */

/* loc_A4E4: the pick counts through the members not yet placed. */
static int order_pick(WinSys *ws, int pick)
{
    for (int i = 0; i < GD_PARTY_MAX; i++) {
        if (ws->char_list_id[i] & 0x8000) {
            continue;
        }
        if (pick == 0) {
            return i;
        }
        pick--;
    }
    return GD_PARTY_MAX - 1;
}

static void state_entry(WinSys *ws, GameData *gd)
{
    if (ws->event_routine == 1) {
        win_open(ws, WIN_STATE_ORDER);
        ws->event_routine++;
        return;
    }

    if (ws->state_choice == 0) {
        /* loc_A462: STATE -- a box a member, then the money. */
        if (ws->event_routine_sub != 0) {
            win_close_all(ws);
            return;
        }
        for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX;
             m++) {
            win_open(ws, (u16)(WIN_CHAR_STATS_1 + m));
        }
        win_open(ws, WIN_MENU_MESETA);
        ws->event_routine_sub++;
        return;
    }

    /* loc_A48A: ORDER */
    switch (ws->event_routine_sub) {
        case 0: {
            if (gd->party_members_num == 0) {
                win_open(ws, WIN_SCRIPT_MESSAGE);
                win_script_set(ws, SCRIPT_HOME_HEY_SHIR_COMING_BACK);
                ws->event_routine_sub += 2;
                break;
            }
            ws->order_count = 0xFFFF;
            memset(ws->order_dest, 0, sizeof ws->order_dest);
            for (int i = 0; i < GD_PARTY_MAX; i++) {
                ws->char_list_id[i] = gd->party_member_id[i];
            }
            win_open(ws, WIN_CHAR_ORDER_DEST);
            win_open(ws, WIN_CHAR_LIST2);
            ws->event_routine_sub++;
            break;
        }
        case 1: {
            int i = order_pick(ws, ws->char_list_cursor);
            u16 id = ws->char_list_id[i];

            ws->char_list_id[i] |= 0x8000;
            ws->order_count++;
            ws->order_dest[ws->order_count % GD_PARTY_MAX] = id;

            if (ws->order_count != gd->party_members_num - 1) {
                /* The long's high word goes first: the destination is
                 * redrawn, then the list, whose cursor then waits. */
                win_open(ws, WIN_F_INSTANT | WIN_F_REDRAW
                             | WIN_CHAR_ORDER_DEST);
                win_open(ws, WIN_F_INSTANT | WIN_F_REDRAW | WIN_CHAR_LIST2);
                break;
            }
            /* loc_A51C: the last one places itself. */
            i  = order_pick(ws, 0);
            id = ws->char_list_id[i];
            ws->char_list_id[i] |= 0x8000;
            ws->order_dest[gd->party_members_num % GD_PARTY_MAX] = id;
            ws->order_count++;
            for (int k = 0; k < GD_PARTY_MAX; k++) {
                gd->party_member_id[k] = ws->order_dest[k];
            }
            win_open(ws, WIN_F_INSTANT | WIN_F_REDRAW | WIN_CHAR_ORDER_DEST);
            win_open(ws, WIN_CLOSE(1));
            ws->event_routine_sub++;
            break;
        }
        default: {
            if (pressed_bc(ws->last_input)) {
                win_close_all(ws);
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* TECH: Menu_TechEntry (15152) and TechEffect_* (15266-15613)         */
/* ------------------------------------------------------------------ */

/* The 30-frame wait, the heal itself, and the button that closes the
 * menu, as loc_A6BE / loc_A6D4 / loc_A73E lay them out. `heal` runs
 * once when the wait is over. */
static void tech_res(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    CharData *target = chr_of(gd, ws->character_index_2);

    switch (d1) {
        case 0: {
            break;
        }
        case 1: {
            win_open(ws, WIN_SCRIPT_MESSAGE);
            win_script_set(ws, SCRIPT_TECH_TOUCHES_WOUNDS_WOUNDS_HEALED);
            if (target->hp == 0) {
                win_script_set(ws, SCRIPT_TECH_IS_DEAD);
                ws->event_routine_sub++;
                break;
            }
            if (target->status & STATUS_POISON) {
                win_script_set(ws, SCRIPT_TECH_TOUCHES_WOUNDS);
                ws->event_routine_sub += 3;
            }
            ws->event_routine_sub += 2;
            subtract_tp(ws, gd);
            break;
        }
        case 2: {
            close_current(ws);
            break;
        }
        case 3: {
            win_open(ws, WIN_CLOSE(1));
            ms->timer = HEAL_WAIT;
            ws->event_routine_sub++;
            break;
        }
        case 4: {
            ms->timer--;
            if (ms->timer >= 0) {
                break;
            }
            u16 hp = target->hp;
            if (ws->technique_index == TECH_REVER) {
                hp = (u16)(hp + 0x1E);
            } else if (ws->technique_index == TECH_RES) {
                hp = (u16)(hp + 0x14);
            } else if (ws->technique_index == TECH_GIRES) {
                hp = (u16)(hp + 0x3C);
            } else {
                hp = target->max_hp;
            }
            if (hp >= target->max_hp) {
                hp = target->max_hp;
            }
            target->hp = hp;
            redraw_char_stats(ws);
            ws->event_routine_sub++;
            sound(ws, SFX_REVIVED);
            if (ws->technique_index != TECH_REVER) {
                sound(ws, SFX_HEALED);
            }
            break;
        }
        case 5: {
            if (pressed_bc(ws->last_input)) {
                win_close_all(ws);
            }
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
}

static void tech_sar(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    switch (d1) {
        case 0: {
            subtract_tp(ws, gd);
            win_replace(ws, WIN_SCRIPT_MESSAGE);
            win_script_set(ws, SCRIPT_TECH_HEALS_EVERYBODYS_WOUNDS);
            break;
        }
        case 1: {
            win_open(ws, WIN_CLOSE(1));
            ms->timer = HEAL_WAIT;
            ws->event_routine_sub++;
            break;
        }
        case 2: {
            ms->timer--;
            if (ms->timer >= 0) {
                break;
            }
            for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX;
                 m++) {
                CharData *c = chr_of(gd, gd->party_member_id[m]);
                if ((c->status & STATUS_POISON) || c->hp == 0) {
                    continue;
                }
                u16 hp = c->hp;
                if (ws->technique_index == TECH_SAR) {
                    hp = (u16)(hp + 0x14);
                } else if (ws->technique_index == TECH_GISAR) {
                    hp = (u16)(hp + 0x3C);
                } else {
                    hp = c->max_hp;
                }
                if (hp >= c->max_hp) {
                    hp = c->max_hp;
                }
                c->hp = hp;
            }
            redraw_char_stats(ws);
            ws->event_routine_sub++;
            /* The ROM queues no sound for the Sar techniques. */
            break;
        }
        default: {
            if (pressed_bc(ws->last_input)) {
                win_close_all(ws);
            }
            break;
        }
    }
}

static void tech_sak(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    CharData *target = chr_of(gd, ws->character_index_2);

    switch (d1) {
        case 0: {
            break;
        }
        case 1: {
            win_open(ws, WIN_SCRIPT_MESSAGE);
            win_script_set(ws, SCRIPT_TECH_IS_DEAD);
            if (target->hp != 0) {
                win_script_set(ws, SCRIPT_TECH_EMBRACES_GIVES_PART_HER);
                ws->event_routine_sub++;
            }
            ws->event_routine_sub++;
            break;
        }
        case 2: {
            close_current(ws);
            break;
        }
        case 3: {
            win_open(ws, WIN_CLOSE(1));
            ms->timer = HEAL_WAIT;
            ws->event_routine_sub++;
            subtract_tp(ws, gd);
            break;
        }
        case 4: {
            ms->timer--;
            if (ms->timer >= 0) {
                break;
            }
            chr_of(gd, ws->character_index)->hp = 0;
            if (!(target->status & STATUS_POISON) && target->hp != 0) {
                target->hp = target->max_hp;
            }
            redraw_char_stats(ws);
            ws->event_routine_sub++;
            break;
        }
        case 5: {
            if (pressed_bc(ws->last_input)) {
                win_close_all(ws);
            }
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
}

static void tech_nasak(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    switch (d1) {
        case 0: {
            subtract_tp(ws, gd);
            win_replace(ws, WIN_SCRIPT_MESSAGE);
            win_script_set(ws, SCRIPT_TECH_GIVES_UP_PART_HER);
            break;
        }
        case 1: {
            win_open(ws, WIN_CLOSE(1));
            ms->timer = HEAL_WAIT;
            ws->event_routine_sub++;
            break;
        }
        case 2: {
            ms->timer--;
            if (ms->timer >= 0) {
                break;
            }
            chr_of(gd, ws->character_index)->hp = 0;
            heal_party_full(gd);
            redraw_char_stats(ws);
            ws->event_routine_sub++;
            break;
        }
        default: {
            if (pressed_bc(ws->last_input)) {
                win_close_all(ws);
            }
            break;
        }
    }
}

static void tech_anti(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    CharData *target = chr_of(gd, ws->character_index_2);

    switch (d1) {
        case 0: {
            break;
        }
        case 1: {
            win_open(ws, WIN_SCRIPT_MESSAGE);
            win_script_set(ws, SCRIPT_TECH_TOUCHES_CURED_POISON);
            if (target->hp == 0) {
                win_script_set(ws, SCRIPT_TECH_IS_DEAD);
                ws->event_routine_sub++;
                break;
            }
            if (!(target->status & STATUS_POISON)) {
                win_script_set2(ws, SCRIPT_TECH_USES_SUCCESSFULLY, SCRIPT_ITEM_NOTHING_HAPPENS);
                ws->event_routine_sub += 3;
            }
            ws->event_routine_sub += 2;
            subtract_tp(ws, gd);
            break;
        }
        case 2: {
            close_current(ws);
            break;
        }
        case 3: {
            win_open(ws, WIN_CLOSE(1));
            ms->timer = HEAL_WAIT;
            ws->event_routine_sub++;
            break;
        }
        case 4: {
            ms->timer--;
            if (ms->timer >= 0) {
                break;
            }
            target->status &= (u16)~STATUS_POISON;
            redraw_char_stats(ws);
            ws->event_routine_sub++;
            break;
        }
        case 5: {
            if (pressed_bc(ws->last_input)) {
                win_close_all(ws);
            }
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
}

static void tech_rever(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    if (d1 == 0) {
        return;
    }
    if (d1 != 1) {
        /* loc_A6BE on: the same close, wait, heal and button as Res,
         * one step earlier in Rever's count -- TechEffect_Rever subtracts
         * once before joining Res's code and TechEffect_Res twice -- so
         * Rever's step n is Res's step n + 1. (Up to build 96b this
         * passed d1 as it was: Rever's step 2 became Res's close for a
         * dead target, and nobody came back.) */
        tech_res(ms, ws, gd, d1 + 1);
        return;
    }
    subtract_tp(ws, gd);
    CharData *target = chr_of(gd, ws->character_index_2);
    win_open(ws, WIN_SCRIPT_MESSAGE);
    win_script_set(ws, (target->hp == 0) ? SCRIPT_TECH_TOUCHES_CHEEK_COMES_BACK : SCRIPT_TECH_TOUCHES_WOUNDS_WOUNDS_HEALED);
    ws->event_routine_sub++;
}

/* TechEffect_Musik (15597). The TP first, then the faced object: the
 * Control Tower's keyboard (an event block with index 7) and nothing
 * else answers. There, $109 alone, $C734 (event_flags $24, Control
 * Tower F1's shutter) = 1, SXFID_Musik and the ROM waits in place for
 * the track to end; then bset #0 on the keyboard's own flag ($C717),
 * and the first time demo 5 -- Demo_KeyUsed, whose $FD slides slot 0's
 * shutter open. Anything else: $109 then $004. The next button closes
 * the windows either way. */
static void tech_musik(MenuState *ms, WinSys *ws, GameData *gd, int d1)
{
    if (d1 != 0) {
        win_close_all(ws);
        return;
    }
    subtract_tp(ws, gd);
    win_replace(ws, WIN_SCRIPT_MESSAGE);
    if (facing_event(ws) != 0x07) {
        win_script_set2(ws, SCRIPT_TECH_USES_SUCCESSFULLY, SCRIPT_ITEM_NOTHING_HAPPENS);     /* loc_AA3E */
        return;
    }
    win_script_set(ws, SCRIPT_TECH_USES_SUCCESSFULLY);
    gd->c700[0x10 + 0x24] = 1;
    sound(ws, SFX_MUSIK);
    ms->sound_wait = true;
    u8 *flag = &gd->c700[0x10 + 0x07];
    bool was = (*flag & 1) != 0;
    *flag |= 1;
    if (!was) {
        ms->start_demo = 5;
    }
}

static void tech_entry(MenuState *ms, WinSys *ws, GameData *gd)
{
    switch (ws->event_routine) {
        case 1: {
            win_open(ws, WIN_MENU_CHAR_STATS);
            win_open(ws, WIN_MENU_ITEM_CHAR);
            ws->event_routine++;
            break;
        }
        case 2: {
            if (ws->event_routine_sub != 0) {
                close_current(ws);
                break;
            }
            CharData *c = chr_of(gd, ws->character_index);
            if (c->hp == 0) {
                win_script_set(ws, SCRIPT_ITEM_IS_DEAD);
                win_open(ws, WIN_SCRIPT_MESSAGE);
                ws->event_routine_sub++;
                break;
            }
            if (c->techs_b == 0) {
                win_script_set(ws, SCRIPT_TECH_CANT_USE_TECHNIQUES);
                win_open(ws, WIN_SCRIPT_MESSAGE);
                ws->event_routine_sub++;
                break;
            }
            win_open(ws, WIN_LEVEL_TECH_LIST);
            ws->event_routine++;
            break;
        }
        default: {
            int d1 = ws->event_routine_sub;

            if (d1 == 0) {
                /* loc_A5D4 queues the "ON?" list before dispatching;
                 * the effects that need no target write the message
                 * over it (win_replace). */
                win_open(ws, WIN_CHOSEN_ITEM_CHAR);
                ws->event_routine_sub++;
                CharData *c = chr_of(gd, ws->character_index);
                if (c->tp < gamedata_tech_tp(ws->technique_index)) {
                    ws->technique_index = 0;
                }
            }

            int tech = ws->technique_index;
            if (tech < TECH_RES) {
                /* loc_A65C: not enough TP. */
                if (d1 != 0) {
                    win_close_all(ws);
                } else {
                    win_replace(ws, WIN_SCRIPT_MESSAGE);
                    win_script_set(ws, SCRIPT_TECH_NOT_ENOUGH_TP);
                }
                break;
            }
            switch (tech) {
                case TECH_RES: {
                    tech_res(ms, ws, gd, d1);
                    break;
                }
                case TECH_GIRES: {
                    tech_res(ms, ws, gd, d1);
                    break;
                }
                case TECH_NARES: {
                    tech_res(ms, ws, gd, d1);
                    break;
                }
                case TECH_SAR: {
                    tech_sar(ms, ws, gd, d1);
                    break;
                }
                case TECH_GISAR: {
                    tech_sar(ms, ws, gd, d1);
                    break;
                }
                case TECH_NASAR: {
                    tech_sar(ms, ws, gd, d1);
                    break;
                }
                case TECH_SAK: {
                    tech_sak(ms, ws, gd, d1);
                    break;
                }
                case TECH_NASAK: {
                    tech_nasak(ms, ws, gd, d1);
                    break;
                }
                case TECH_ANTI: {
                    tech_anti(ms, ws, gd, d1);
                    break;
                }
                case TECH_REVER: {
                    tech_rever(ms, ws, gd, d1);
                    break;
                }
                case TECH_RYUKA: {
                    if (d1 == 0) {
                        win_replace(ws, WIN_SCRIPT_MESSAGE);
                    }
                    use_telepipe(ms, ws, gd, d1);
                    break;
                }
                case TECH_HINAS: {
                    if (d1 == 0) {
                        win_replace(ws, WIN_SCRIPT_MESSAGE);
                    }
                    use_escapipe(ms, ws, gd, d1);
                    break;
                }
                case TECH_MUSIK: {
                    tech_musik(ms, ws, gd, d1);
                    break;
                }
                default: {
                    win_close_all(ws);
                    break;
                }
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* STRNG: Menu_StrngEntry (15639)                                      */
/* ------------------------------------------------------------------ */

static void strng_entry(WinSys *ws)
{
    switch (ws->event_routine) {
        case 1: {
            /* loc_AA88 */
            win_open(ws, WIN_STRNG_CHAR_LIST);
            ws->event_routine++;
            break;
        }
        case 2: {
            /* loc_AA94: two move.l pairs, high word first -- level and
             * experience, HP/TP, the stats, and equipment last, which is
             * the one that waits. */
            win_open(ws, WIN_STRNG_LVEXP);
            win_open(ws, WIN_STRNG_HPTP);
            win_open(ws, WIN_STRNG_STATS);
            win_open(ws, WIN_STRNG_EQUIP);
            ws->event_routine++;
            break;
        }
        case 3: {
            /* loc_AAAA: (FullTechList<<16 | FullTechList2) -- map
             * techniques, then battle ones, which is the list that
             * waits. */
            win_open(ws, WIN_FULL_TECH_LIST);
            win_open(ws, WIN_FULL_TECH_LIST2);
            ws->event_routine++;
            break;
        }
        default: {
            /* loc_AAB8 */
            win_close_all(ws);
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* EQP: Menu_EqpEntry (15669)                                          */
/* ------------------------------------------------------------------ */

/* The end of loc_AC82: a mark taken off an item on the first page is
 * blanked in that page's own art. When page 2 closes, page 1 is shown
 * again as it was drawn, not rebuilt from the bag, so without this its
 * E stays. The ROM reaches the page through the art pointer of a fixed
 * window-stack entry ($FFFFDF62); the port finds it by id. Row pair
 * `slot`, lower row, interior column 0 -- where fill_item_list puts the
 * mark. */
static void blank_first_page_mark(WinSys *ws, int slot)
{
    for (int d = 0; d < ws->depth && d < WIN_STACK; d++) {
        if ((ws->stack[d].id & 0xFF) == WIN_ITEM_LIST2) {
            ws->stack[d].cell[2 + 2 * slot][0] = 0x26;     /* TILE_FILL */
            return;
        }
    }
}

/* loc_AC82: put item_index into the slot named by its kind. Whatever
 * was there comes off first -- both hands if it was two-handed -- and
 * loses its mark in the bag. */
static void equip_into(WinSys *ws, GameData *gd, int kind)
{
    CharData *c = chr_of(gd, ws->character_index);
    if (kind < 1 || kind > EQUIP_COUNT) {
        return;
    }
    u8 old = c->equip[kind - 1];

    if ((gamedata_item_action_flags(old & 0x7F) & 7) == KIND_LEFT) {
        c->equip[EQUIP_RIGHT] = 0;
        c->equip[EQUIP_LEFT]  = 0;
    }
    c->equip[kind - 1] = ws->item_index;

    if ((old & 0x7F) != 0) {
        for (int i = 0; i < GD_ITEM_SLOTS; i++) {
            if (c->items[i] == (u8)(old | 0x80)) {
                c->items[i] &= 0x7F;
                if (i < 8) {
                    blank_first_page_mark(ws, i);
                }
                break;
            }
        }
    }
}

static void eqp_entry(WinSys *ws, GameData *gd)
{
    CharData *c = chr_of(gd, ws->character_index);

    switch (ws->event_routine) {
        case 1: {
            win_open(ws, WIN_STRNG_CHAR_LIST);
            ws->event_routine++;
            break;
        }
        case 2: {
            /* loc_AAE6 */
            if (ws->event_routine_sub != 0) {
                close_current(ws);
                break;
            }
            if (c->hp == 0) {
                win_script_set(ws, SCRIPT_ITEM_IS_DEAD);
                win_open(ws, WIN_SCRIPT_MESSAGE);
                ws->event_routine_sub++;
                break;
            }
            if (c->item_count == 0) {
                win_script_set(ws, SCRIPT_ITEM_ISNT_CARRYING_ANYTHING);
                win_open(ws, WIN_SCRIPT_MESSAGE);
                ws->event_routine_sub++;
                break;
            }
            /* $FFFFDE74/78/7C: agility, attack, defense as (target,
             * shown), both the current value. */
            ws->equip_target[0] = c->agility;
            ws->equip_target[1] = c->attack;
            ws->equip_target[2] = c->defense;
            for (int i = 0; i < 3; i++) {
                ws->equip_shown[i] = ws->equip_target[i];
            }
            win_open(ws, WIN_STRNG_LVEXP);
            win_open(ws, WIN_EQP_EQUIP_LIST);
            win_open(ws, WIN_EQUIP_STATS);
            win_open(ws, WIN_ITEM_LIST2);
            ws->event_routine++;
            break;
        }
        case 3: {
            /* loc_AB6A */
            if (ws->event_routine_sub != 0) {
                close_current(ws);
                break;
            }
            u8 mask = gamedata_item_equip_mask(ws->item_index);
            if (!(mask & (1 << (ws->character_index % 8)))) {
                win_script_set(ws, SCRIPT_EQUIP_CANT_TAKEN);
                if (mask != 0) {
                    win_script_set(ws, SCRIPT_EQUIP_CANT_TAKE);
                    if (ws->character_index == CHAR_NEI) {
                        win_script_set(ws, SCRIPT_EQUIP_DOESNT_WANT_TAKE);
                    }
                }
                win_open(ws, WIN_SCRIPT_MESSAGE);
                ws->event_routine_sub++;
                break;
            }
            /* loc_ABB2 */
            u8 *entry = &c->items[ws->item_slot % GD_ITEM_SLOTS];
            int kind  = gamedata_item_action_flags(ws->item_index) & 7;
            ws->event_routine++;
            if (!(*entry & 0x80)) {
                if (kind == KIND_RIGHT) {
                    win_open(ws, WIN_RIGHT_LEFT);
                    break;
                }
                ws->equip_hand = (u16)((kind == KIND_LEFT) ? 6 : kind);
                break;
            }
            /* Worn: take it off. */
            gamedata_equip_bonuses_remove(gd, ws->character_index);
            *entry &= 0x7F;
            if (kind == KIND_RIGHT) {
                if ((c->equip[EQUIP_RIGHT] & 0x7F) == *entry) {
                    clear_slot(c, KIND_RIGHT);
                } else {
                    clear_slot(c, KIND_LEFT);
                }
            } else if (kind == KIND_LEFT) {
                clear_slot(c, KIND_RIGHT);
                clear_slot(c, KIND_LEFT);
            } else {
                clear_slot(c, kind);
            }
            win_script_set(ws, SCRIPT_EQUIP_REMOVES);
            win_open(ws, WIN_SCRIPT_MESSAGE);
            ws->event_routine++;
            break;
        }
        case 4: {
            /* loc_AC42: put it on. */
            gamedata_equip_bonuses_remove(gd, ws->character_index);
            c->items[ws->item_slot % GD_ITEM_SLOTS] |= 0x80;
            win_script_set(ws, SCRIPT_EQUIP_TAKES);
            if (ws->equip_hand == 6) {
                equip_into(ws, gd, KIND_RIGHT);
                equip_into(ws, gd, KIND_LEFT);
            } else {
                equip_into(ws, gd, (int)ws->equip_hand);
            }
            win_open(ws, WIN_SCRIPT_MESSAGE);
            ws->event_routine++;
            break;
        }
        default: {
            /* loc_AD00: bonuses back on, the new values as the targets
             * the stats box counts toward, the message closed, the
             * equipment and item pages redrawn, back to picking. */
            gamedata_equip_bonuses_add(gd, ws->character_index);
            ws->equip_target[0] = c->agility;
            ws->equip_target[1] = c->attack;
            ws->equip_target[2] = c->defense;
            close_current(ws);
            win_open(ws, WIN_F_INSTANT | WIN_F_REDRAW | WIN_EQP_EQUIP_LIST);
            win_open(ws, WIN_F_INSTANT | WIN_F_REDRAW
                         | ((ws->item_slot < 8) ? WIN_ITEM_LIST2
                                                : WIN_ITEM_LIST3));
            ws->event_routine--;
            break;
        }
    }
}

/* ------------------------------------------------------------------ */

void menu_tick(MenuState *ms, WinSys *ws, GameData *gd, const Input *in)
{
    ws->last_input = in;

    if (ws->event_step != 0 || !win_idle(ws)) {
        return;
    }

    if (ws->event_routine == 0) {
        /* tst.b (event_flags): the high byte -- a one-shot case in hand
         * or a hold keeps the menu shut. The port keeps that byte in
         * game.c (the next load's one-shot case and the hold), not in
         * the save block's copy at $C710, which nothing ported writes:
         * reading that one (to build 97c) let C open the menu during
         * the Esper B1 hold and the Motavia views after Climatrol. */
        if (ms->demo_flag || ms->event_word_high) {
            return;
        }
        if (in->pressed.c) {
            sound(ws, SFX_SELECTION);
            win_open(ws, WIN_PLAYER_MENU);
            ws->event_routine     = 1;
            ws->event_routine_sub = 0;
        }
        return;
    }

    int entry = ws->menu_choice % 5;
    switch (entry) {
        case MENU_ITEM: {
            item_entry(ms, ws, gd);
            break;
        }
        case MENU_STATE: {
            state_entry(ws, gd);
            break;
        }
        case MENU_TECH: {
            tech_entry(ms, ws, gd);
            break;
        }
        case MENU_STRNG: {
            strng_entry(ws);
            break;
        }
        default: {
            eqp_entry(ws, gd);
            break;
        }
    }
}
