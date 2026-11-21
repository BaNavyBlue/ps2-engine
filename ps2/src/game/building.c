#include "building.h"
#include "lang.h"
#include "rom_ids.h"
#include "script_ids.h"

#include "../core/json.h"

#include <stdio.h>
#include <string.h>

/* Items, levels, sounds, demo walks and the $C700 flag bytes by name are
 * in rom_ids.h. FLAG_GOVERNOR_ERRAND_STEP ($C736) is how far the
 * Governor's errand has got: 0 before the Recorder is handed in, 1 after
 * the first debrief. */

void building_begin(WinSys *ws, const BuildingState *bs)
{
    ws->event_routine       = 1;
    ws->event_routine_sub   = 0;
    ws->event_routine_sub_2 = 0;
    ws->store_max   = (u16)bs->store_max;
    ws->store_index = (u16)bs->store_index;
    /* BuildingScreen's Tyler case (loc_8326): addq.w #1, (event_routine)
     * -- no portrait window; the building's own table starts at once. */
    if (bs->building == BUILDING_TYLER_SHIP) {
        ws->event_routine = 2;
    }
    /* loc_8364, the Esper mansion: Lutz drawn in plane A and
     * `addq.w #1, (event_routine).w` -- no portrait window either. */
    if (bs->building == BUILDING_ESPER_MANSION) {
        ws->event_routine = 2;
    }
}

bool building_ported(int building)
{
    if (building == BUILDING_GAIRA_PANEL || building == BUILDING_TYLER_SHIP
        || building == BUILDING_ESPER_MANSION) {
        return true;
    }
    switch (building) {
        case BUILDING_ROLF_HOUSE_START: {
            return true;
        }
        case BUILDING_ROLF_HOUSE: {
            return true;
        }
        case BUILDING_DATA_MEMORY: {
            return true;
        }
        case BUILDING_CLONE_LAB: {
            return true;
        }
        case BUILDING_HOSPITAL: {
            return true;
        }
        case BUILDING_WEAPON_STORE: {
            return true;
        }
        case BUILDING_ARMOR_STORE: {
            return true;
        }
        case BUILDING_ITEM_STORE: {
            return true;
        }
        case BUILDING_TOWER_OUTSIDE: {
            return true;
        }
        case BUILDING_TOWER_ROOM: {
            return true;
        }
        case BUILDING_LIBRARY: {
            return true;
        }
        case BUILDING_TOWER_ROOF: {
            return true;
        }
        case BUILDING_GOVERNOR: {
            return true;
        }
        case BUILDING_TELEPORT: {
            return true;
        }
        case BUILDING_USTVESTIA: {
            return true;
        }
        case BUILDING_INVENTOR: {
            return true;
        }
        default: {
            return false;
        }
    }
}

bool building_party_has_item(const GameData *gd, int item_id)
{
    for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX; m++) {
        const CharData *c = &gd->chr[gd->party_member_id[m] % GD_CHARS];

        /* All sixteen slots, whatever the count says, as the original. */
        for (int i = 0; i < GD_ITEM_SLOTS; i++) {
            if ((c->items[i] & 0x7F) == item_id) {
                return true;
            }
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Building_CentralTowerGovernor                                      */
/* ------------------------------------------------------------------ */

enum {
    GOV_GREETING = 2,       /* loc_C98E */
    GOV_SEND_HOME,          /* loc_CA00 */
    GOV_TO_LIBRARY,         /* loc_CA22 */
    GOV_ROLF_PORTRAIT,      /* loc_CA36 */
    GOV_DEBRIEF,            /* loc_CA42 */
    GOV_DEBRIEF_DONE,       /* loc_CA58 */
    GOV_LEAVE               /* loc_CA64 */
};

static BuildingResult governor(BuildingState *bs, WinSys *ws, GameData *gd)
{
    switch (ws->event_routine) {
        case GOV_GREETING: {
            win_open(ws, WIN_SCRIPT_MESSAGE_BIG);

            if (gd->c700[FLAG_GOVERNOR_ERRAND_STEP] != 0) {
                /* Already debriefed: $1111, and out onto the street. */
                win_script(ws, SCRIPT_GOVERNOR_WELCOME_BACK_WORRIED_SICK);
                ws->event_routine += 6;
                gd->level_index = LEVEL_PASEO;
                gd->level_y     = 0x100;
                gd->level_x     = 0x1F0;
                gd->jet_scooter = 0;
            } else if (building_party_has_item(gd, ITEM_RECORDER)) {
                win_script(ws, SCRIPT_GOVERNOR_SPLENDID_WORK_WILL_IMMEDIATELY);
                ws->event_routine += 2;
            } else if (gd->c700[FLAG_NEI_GONE] == 1) {
                /* cmpi.w #1, $C736 with the byte at $C736 known to be
                 * zero: it is the byte after it, $C737 (Nei gone), that
                 * is being tested. */
                win_script(ws, SCRIPT_GOVERNOR_ONCE_AGAIN_THERES_SERIOUS);
                ws->event_routine += 3;
            } else {
                /* The start of the game: the briefing. */
                win_script2(ws, SCRIPT_GOVERNOR_GOOD_MORNING_HOW_ALMOST, SCRIPT_GOVERNOR_MISSION_GO_BIOSYSTEMS_LAB);
                ws->event_routine += 1;
            }
            break;
        }
        case GOV_SEND_HOME: {
            bs->building   = BUILDING_ROLF_HOUSE_START;
            bs->portrait   = PORTRAIT_ROLF_AT_HOME;
            bs->demo_flag  = true;
            bs->demo_index = DEMO_INDEX_TOWER_TO_HOUSE;
            win_close_all(ws);
            break;
        }
        case GOV_TO_LIBRARY: {
            bs->building = BUILDING_LIBRARY;
            bs->portrait = 8;
            return BUILDING_CHANGED;
        }
        case GOV_ROLF_PORTRAIT: {
            win_open(ws, WIN_ROLF_PORTRAIT2);
            ws->event_routine++;
            break;
        }
        case GOV_DEBRIEF: {
            win_script2(ws, SCRIPT_GOVERNOR_PLEASE_LET_ME_DO, SCRIPT_GOVERNOR_WELL_BUT_IM_SORRY);
            win_script2(ws, SCRIPT_GOVERNOR_BUT_EVEN_IF_STAY, SCRIPT_GOVERNOR_SEE_WONT_SAY_ANY);
            ws->event_routine++;
            break;
        }
        case GOV_DEBRIEF_DONE: {
            gd->c700[FLAG_GOVERNOR_ERRAND_STEP] = 1;
            gd->c700[FLAG_PIATA_TOWER_UNLOCKED] = 1;
            bs->demo_flag = false;
            win_close_all(ws);
            break;
        }
        default: {
            bs->demo_flag = false;
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_RolfHouseStart                                            */
/* ------------------------------------------------------------------ */

enum {
    HOME_NARRATION = 2,     /* loc_AF8E */
    HOME_NEI_APPEARS,       /* loc_AFA4 */
    HOME_CONVERSATION,      /* loc_AFC2 */
    HOME_NEI_JOINS          /* loc_AFE6 */
};

static BuildingResult rolf_house_start(BuildingState *bs, WinSys *ws,
                                       GameData *gd)
{
    switch (ws->event_routine) {
        case HOME_NARRATION: {
            /* loc_AF8E's bsr loc_F550 is done earlier, by the new game in
             * game.c's enter_intro: nothing between the two changes a
             * command or what anyone holds. */
            win_open(ws, WIN_SCRIPT_MESSAGE_BIG);
            win_script(ws, SCRIPT_HOME_AFTER_GOING_HOME_PREPARING);
            ws->event_routine++;
            break;
        }
        case HOME_NEI_APPEARS: {
            win_open(ws, WIN_NEI_PORTRAIT);
            bs->portrait = PORTRAIT_NEI;
            ws->event_routine++;
            return BUILDING_LOAD_PORTRAIT;
        }
        case HOME_CONVERSATION: {
            win_script2(ws, SCRIPT_HOME_NEI_LEAVE_AWHILE, SCRIPT_HOME_NEI_STARED_ME_MOMENT);
            win_script2(ws, SCRIPT_HOME_WERE_STILL_SMALL_WHEN, SCRIPT_HOME_STILL_NEI_STOOD_DOORWAY);
            win_script2(ws, SCRIPT_HOME_PLEASE_TAKE_ME_ILL, SCRIPT_HOME_TOOK_SACK_STARTED_GO);
            ws->event_routine++;
            break;
        }
        default: {
            /* loc_AFE6: the scripted part of the game is over. */
            bs->demo_flag  = false;
            bs->demo_index = 0;
            gd->party_members_num    = 1;
            gd->party_members_joined = 1;
            gd->party_member_id[0]   = 0;   /* Rolf */
            gd->party_member_id[1]   = 1;   /* Nei  */
            gd->party_member_id[2]   = 0;
            gd->party_member_id[3]   = 0;
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}


/* ------------------------------------------------------------------ */
/* Money and inventory (ps2.asm:19046-19105, 16063-16100)             */
/* ------------------------------------------------------------------ */

#define MONEY_CAP   0x5F5E0FF           /* 99,999,999 */
#define CHAR_SHIR   7

static int item_id(const char *key)
{
    return gamedata_item_id(key);
}

/* AddToMoneyOwned. The cap test is on the amount added, not the total,
 * as written. */
static void add_money(GameData *gd, u32 amount)
{
    gd->money += amount;
    if (amount >= MONEY_CAP) {
        gd->money = MONEY_CAP;
    }
}

/* CheckSubtractMoney: false and nothing taken when there is not enough. */
static bool subtract_money(GameData *gd, u32 amount)
{
    if (gd->money < amount) {
        return false;
    }
    gd->money -= amount;
    return true;
}

/* AddItemToInventory2: after the last one; the count is trusted. c may be
 * GD_KEEPER, the tower room's bag. */
static void add_item(GameData *gd, int c, u8 item)
{
    u8 *count;
    u8 *items = gamedata_bag(gd, c, &count);
    (*count)++;
    items[(*count - 1) % GD_ITEM_SLOTS] = item;
}

/* RemoveItemFromInventory: the ones after it move down; the last slot
 * is cleared. */
static void remove_item(GameData *gd, int c, int slot)
{
    u8 *count;
    u8 *items = gamedata_bag(gd, c, &count);
    (*count)--;
    slot %= GD_ITEM_SLOTS;
    if (slot < GD_ITEM_SLOTS - 1) {
        memmove(&items[slot], &items[slot + 1],
                (size_t)(GD_ITEM_SLOTS - 1 - slot));
    }
    items[GD_ITEM_SLOTS - 1] = 0;
}

/* loc_AE2E: selling something that is worn takes it off first. The
 * item's body-part nibble names the slot; a one-handed weapon (2) is
 * whichever hand holds it, a two-handed one (3) is both. */
static void unequip_sold(GameData *gd, int c, int slot)
{
    if (c >= GD_CHARS) {
        return;                     /* the keeper wears nothing */
    }
    CharData *d = &gd->chr[c % GD_CHARS];
    u8 *cell = &d->items[slot % GD_ITEM_SLOTS];

    if (!(*cell & 0x80)) {
        return;
    }
    *cell &= 0x7F;
    gamedata_equip_bonuses_remove(gd, c);

    int part = gamedata_item_action_flags(*cell) & 7;
    if (part == 2) {
        int hand = (d->equip[EQUIP_RIGHT] == *cell) ? EQUIP_RIGHT
                                                    : EQUIP_LEFT;
        d->equip[hand] = 0;
    } else if (part == 3) {
        d->equip[EQUIP_RIGHT] = 0;
        d->equip[EQUIP_LEFT]  = 0;
    } else if (part >= 1 && part <= 5) {
        d->equip[part - 1] = 0;
    }
    gamedata_equip_bonuses_add(gd, c);
}

/* CheckPlanetAndCaps (19188): on Dezolis the shopkeeper only makes sense
 * to a live Rolf in a Magic Cap or a Mogic Cap. True means "no sale". */
static bool language_barrier(const GameData *gd)
{
    if (gd->planet == 0) {
        return false;
    }
    if (gd->chr[0].hp == 0) {
        return true;
    }
    int head = gd->chr[0].equip[EQUIP_HEAD] & 0x7F;
    return head != item_id("MagicCap") && head != item_id("MogicCap");
}

/* ProcessStealItem (19093). Shir, in the party, alive, level 10 or
 * more for the Central Tower room, with room in her bag, one time in
 * sixteen pockets something from the list and leaves. */
static void steal_item(BuildingState *bs, WinSys *ws, GameData *gd,
                       const char *list)
{
    int slot = -1;
    for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX; m++) {
        if (gd->party_member_id[m] == CHAR_SHIR) {
            slot = m;
        }
    }
    if (slot < 0 || bs->rng == NULL) {
        return;
    }
    CharData *shir = &gd->chr[CHAR_SHIR];
    if (strcmp(list, "room") == 0 && shir->level < 10) {
        return;
    }
    if (shir->hp == 0 || shir->item_count == 0x10) {
        return;
    }
    if ((rng_next(bs->rng) & 0xF) != 0) {
        return;
    }
    int pick = rng_next(bs->rng) & 7;

    JsonValue *root = json_parse_file("assets/data/steal.json");
    const JsonValue *it = json_at(json_get(json_get(root, "lists"), list),
                                  pick);
    u8 item = (u8)item_id((it != NULL && it->type == JSON_STRING)
                          ? it->string : "None");
    json_free(root);

    ws->item_index = item;
    add_item(gd, CHAR_SHIR, item);
    gd->party_member_join_next++;
    memset(&gd->c600_raw[GD_ORDER_NEW], 0, 4);  /* $FFFFC610 */

    for (int m = slot; m < GD_PARTY_MAX - 1; m++) {
        gd->party_member_id[m] = gd->party_member_id[m + 1];
    }
    gd->party_member_id[GD_PARTY_MAX - 1] = 0;
    gd->party_members_num--;
    bs->event_flags = 0x0C00;
}

/* ------------------------------------------------------------------ */
/* Building_WeaponStore (17136) and Building_ArmorStore (17279)       */
/* ------------------------------------------------------------------ */

/* The two are one routine with a different script bank: $6 and $7.
 * event_routine_sub is the way out -- 1 says goodbye, anything else
 * closes the shop -- and any B press on a list puts it there. */
enum {
    EQ_GREET = 2,       /* loc_BAA8 */
    EQ_STOCK,           /* loc_BACE */
    EQ_ASK_WHO,         /* loc_BADA */
    EQ_WHO,             /* loc_BAE6 */
    EQ_CHOSEN,          /* loc_BAF2 */
    EQ_JOKING,          /* loc_BB74 */
    EQ_JOKING_ANSWER,   /* loc_BB80 */
    EQ_BACK_TO_WHO,     /* loc_BBA8 */
    EQ_ANYTHING_ELSE,   /* loc_BBB4 */
    EQ_ELSE_ANSWER      /* loc_BBC6 */
};

/* loc_BB36: the price is known, take the money and hand it over. */
static void equipment_buy(WinSys *ws, GameData *gd, u16 bank)
{
    u32 price = win_price(ws, ws->item_index);
    ws->meseta_value = price;

    if (!subtract_money(gd, price)) {
        win_script(ws, (u16)(bank | 0x05));
        ws->event_routine_sub++;
        return;
    }
    win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW | WIN_STORE_MESETA));
    add_item(gd, ws->character_index, ws->item_index);
    if (ws->sound != NULL) {
        ws->sound(ws->ctx, SFX_ITEM_RECEIVED);
    }
    win_script(ws, (u16)(bank | 0x07));
    ws->event_routine += 4;
}

static BuildingResult equipment_store(BuildingState *bs, WinSys *ws,
                                      GameData *gd, u16 bank,
                                      const char *steal_list)
{
    if (ws->event_routine_sub != 0) {
        if (ws->event_routine_sub != 1) {
            win_close_all(ws);
            return BUILDING_RUNNING;
        }
        win_script(ws, (u16)(bank | 0x08));         /* "Later, ace!" */
        ws->event_routine_sub++;
        steal_item(bs, ws, gd, steal_list);
        return BUILDING_RUNNING;
    }

    switch (ws->event_routine) {
        case EQ_GREET: {
            /* One move.l: the high word, StoreMeseta, is queued first,
             * so the message window opens last and its phase 1 -- which
             * points the text at it -- is the one that runs. */
            win_open(ws, WIN_STORE_MESETA);
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            if (language_barrier(gd)) {
                win_script(ws, SCRIPT_ITEMS_DANYARAHA_BEBEKUCHA);         /* "Danyaraha? bebekucha?" */
                ws->event_routine_sub += 2;
            } else {
                win_script(ws, (u16)(bank | 0x01));
                ws->event_routine++;
            }
            break;
        }
        case EQ_STOCK: {
            win_open(ws, WIN_STORE_INVENTORY);
            ws->event_routine++;
            break;
        }
        case EQ_ASK_WHO: {
            win_script(ws, (u16)(bank | 0x02));
            ws->event_routine++;
            break;
        }
        case EQ_WHO: {
            win_open(ws, WIN_STORE_CHAR_LIST);
            ws->event_routine++;
            break;
        }
        case EQ_CHOSEN: {
            const CharData *c = &gd->chr[ws->character_index % GD_CHARS];
            if (c->item_count == 0x10) {
                win_script(ws, (u16)(bank | 0x09));
                ws->event_routine_sub++;
                break;
            }
            if (!(gamedata_item_equip_mask(ws->item_index)
                  & (1 << (ws->character_index % 8)))) {
                win_script(ws, (u16)(bank | 0x03));
                ws->event_routine++;
                break;
            }
            equipment_buy(ws, gd, bank);
            break;
        }
        case EQ_JOKING: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case EQ_JOKING_ANSWER: {
            if (ws->yes_no_input != 0) {
                win_script(ws, (u16)(bank | 0x04));
                ws->event_routine++;
                break;
            }
            ws->event_routine -= 2;
            equipment_buy(ws, gd, bank);
            break;
        }
        case EQ_BACK_TO_WHO: {
            ws->window_index_saved = WIN_STORE_CHAR_LIST;
            ws->event_routine -= 3;
            break;
        }
        case EQ_ANYTHING_ELSE: {
            if (ws->script_id[0] == 0) {
                win_open(ws, WIN_YESNO2);
                ws->event_routine++;
            }
            break;
        }
        case EQ_ELSE_ANSWER: {
            if (ws->yes_no_input != 0) {
                win_script(ws, (u16)(bank | 0x06));
                ws->event_routine_sub++;
                break;
            }
            win_open(ws, WIN_CLOSE(1));
            ws->event_routine -= 7;
            ws->window_index_saved = WIN_STORE_INVENTORY;
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_ItemStore (17422)                                         */
/* ------------------------------------------------------------------ */

enum {
    IS_GREET = 2,       /* loc_BDEC */
    IS_BUY_OR_SELL,     /* loc_BE12 */
    IS_WHICH_WAY,       /* loc_BE1E */
    IS_SELL_WHOSE,      /* loc_BE3C */
    IS_SELL_WHO,        /* loc_BE4E */
    IS_SELL_WHAT,       /* loc_BE8A */
    IS_SELL_YESNO,      /* loc_BEF6 */
    IS_SELL_ANSWER,     /* loc_BF02 */
    IS_SELL_ELSE,       /* loc_BF44 */
    IS_SELL_ELSE_ANSWER,/* loc_BF56 */
    IS_BUY_STOCK,       /* loc_BF6E */
    IS_BUY_ASK_WHO,     /* loc_BF7A */
    IS_BUY_WHO,         /* loc_BF86 */
    IS_BUY_CHOSEN,      /* loc_BF92 */
    IS_BUY_ELSE,        /* loc_C014 */
    IS_BUY_ELSE_ANSWER  /* loc_C026 */
};

static BuildingResult item_store(BuildingState *bs, WinSys *ws,
                                 GameData *gd)
{
    if (ws->event_routine_sub != 0) {
        if (ws->event_routine_sub != 1) {
            win_close_all(ws);
            return BUILDING_RUNNING;
        }
        win_script(ws, SCRIPT_ITEMS_GOOD_BYE_COME_AGAIN);                     /* "Good-bye" */
        ws->event_routine_sub++;
        steal_item(bs, ws, gd, "item");
        return BUILDING_RUNNING;
    }

    switch (ws->event_routine) {
        case IS_GREET: {
            /* One move.l: the high word, StoreMeseta, is queued first,
             * so the message window opens last and its phase 1 -- which
             * points the text at it -- is the one that runs. */
            win_open(ws, WIN_STORE_MESETA);
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            if (language_barrier(gd)) {
                win_script(ws, SCRIPT_ITEMS_DANYARAHA_BEBEKUCHA);
                ws->event_routine_sub += 2;
            } else {
                win_script(ws, SCRIPT_ITEMS_WELCOME_HOW_MAY_SERVICE);
                ws->event_routine++;
            }
            break;
        }
        case IS_BUY_OR_SELL: {
            win_open(ws, WIN_BUYSELL);
            ws->event_routine++;
            break;
        }
        case IS_WHICH_WAY: {
            if (ws->buy_sell != 0) {
                win_script(ws, SCRIPT_ITEMS_WHOSE_BELONGINGS_WOULD_LIKE);
                ws->event_routine++;
            } else {
                win_script(ws, SCRIPT_ITEMS_WHICH_WOULD_LIKE);
                ws->event_routine += 8;
            }
            break;
        }
        case IS_SELL_WHOSE: {
            win_open(ws, WIN_STORE_CHAR_LIST);
            win_script(ws, SCRIPT_ITEMS_WHICH_DO_WANT_SELL);
            ws->event_routine++;
            break;
        }
        case IS_SELL_WHO: {
            if (ws->event_routine_sub_2 != 0) {
                ws->event_routine_sub_2 = 0;
                ws->window_index_saved  = WIN_STORE_CHAR_LIST;
                break;
            }
            if (gd->chr[ws->character_index % GD_CHARS].item_count == 0) {
                win_script(ws, SCRIPT_ITEMS_ISNT_CARRYING_ANYTHING_THERE);
                ws->event_routine_sub_2++;
                break;
            }
            win_open(ws, WIN_MENU_ITEM_LIST);
            ws->event_routine++;
            break;
        }
        case IS_SELL_WHAT: {
            if (ws->event_routine_sub_2 != 0) {
                /* loc_BEE6: back to the list, same state. */
                ws->event_routine_sub_2 = 0;
                ws->event_routine++;
                win_close_current(ws, 0);
                break;
            }
            if (!(gamedata_item_action_flags(ws->item_index) & 0x80)) {
                win_script(ws, (ws->item_index == item_id("Teim"))
                               ? SCRIPT_ITEMS_WHAT_MUST_JOKING : SCRIPT_ITEMS_CANT_GIVE_PRICE_SOMETHING);
                ws->event_routine_sub_2++;
                break;
            }
            win_script(ws, SCRIPT_ITEMS_LETS_SAY_MESETA_HOW);
            u32 offer = gamedata_item_price(ws->item_index) >> 1;
            if (gd->planet != 0
                && (gd->chr[0].equip[EQUIP_HEAD] & 0x7F)
                   == item_id("MagicCap")) {
                offer = 10;
            }
            ws->meseta_value = offer;
            ws->event_routine++;
            break;
        }
        case IS_SELL_YESNO: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case IS_SELL_ANSWER: {
            /* The list under the prompt is one page or two. */
            win_open(ws, WIN_CLOSE(2 + ((ws->item_slot >> 3) & 1)));
            ws->event_routine++;
            if (ws->yes_no_input != 0) {
                win_script(ws, SCRIPT_ITEMS_SEE_THATS_TOO_BAD);
                break;
            }
            unequip_sold(gd, ws->character_index, ws->item_slot);
            remove_item(gd, ws->character_index, ws->item_slot);
            add_money(gd, ws->meseta_value);
            win_script(ws, SCRIPT_ITEMS_THANK_THERE_ANYTHING_ELSE);
            win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW
                               | WIN_STORE_MESETA));
            break;
        }
        case IS_SELL_ELSE: {
            if (ws->script_id[0] == 0) {
                win_open(ws, WIN_YESNO2);
                ws->event_routine++;
            }
            break;
        }
        case IS_SELL_ELSE_ANSWER: {
            if (ws->yes_no_input == 0) {
                ws->window_index_saved = WIN_BUYSELL;
                ws->event_routine -= 7;
            } else {
                ws->event_routine_sub++;
            }
            break;
        }
        case IS_BUY_STOCK: {
            win_open(ws, WIN_STORE_INVENTORY);
            ws->event_routine++;
            break;
        }
        case IS_BUY_ASK_WHO: {
            win_script(ws, SCRIPT_ITEMS_WHO_GOING_CARRY_ITEM);
            ws->event_routine++;
            break;
        }
        case IS_BUY_WHO: {
            win_open(ws, WIN_STORE_CHAR_LIST);
            ws->event_routine++;
            break;
        }
        case IS_BUY_CHOSEN: {
            if (ws->event_routine_sub_2 != 0) {
                /* loc_C004: back to the character list, same state. */
                ws->event_routine_sub_2 = 0;
                ws->event_routine++;
                win_close_current(ws, 0);
                break;
            }
            if (gd->chr[ws->character_index % GD_CHARS].item_count == 0x10) {
                win_script(ws, SCRIPT_ITEMS_TOO_MUCH_ALREADY_CAN);
                ws->event_routine_sub_2++;
                break;
            }
            u32 price = win_price(ws, ws->item_index);
            ws->meseta_value = price;
            if (!subtract_money(gd, price)) {
                win_script(ws, SCRIPT_ITEMS_DONT_ENOUGH_MONEY);
                ws->event_routine_sub++;
                break;
            }
            win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW
                               | WIN_STORE_MESETA));
            add_item(gd, ws->character_index, ws->item_index);
            if (ws->sound != NULL) {
                ws->sound(ws->ctx, SFX_ITEM_RECEIVED);
            }
            win_script(ws, SCRIPT_ITEMS_THANK_WILL_THERE_ANYTHING);
            ws->event_routine++;
            break;
        }
        case IS_BUY_ELSE: {
            if (ws->script_id[0] == 0) {
                win_open(ws, WIN_YESNO2);
                ws->event_routine++;
            }
            break;
        }
        case IS_BUY_ELSE_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            win_open(ws, WIN_CLOSE(1));
            ws->event_routine -= 4;
            ws->window_index_saved = WIN_STORE_INVENTORY;
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}


/* ------------------------------------------------------------------ */
/* Helpers the other buildings share                                  */
/* ------------------------------------------------------------------ */

static void sfx(WinSys *ws, u8 id)
{
    if (ws->sound != NULL) {
        ws->sound(ws->ctx, id);
    }
}

/* CheckIfDoublePrice on an amount rather than an item. */
static u32 double_price(const GameData *gd, u32 amount)
{
    if (gd->planet != 0
        && (gd->chr[0].equip[EQUIP_HEAD] & 0x7F) == item_id("MagicCap")) {
        amount *= 2;
    }
    return amount;
}

/* $FFFFC602 (GD_STEP): a party index the Data Memory report and the
 * clone lab's scripted revival walk through. It is in the saved block. */
static u16 c602(const GameData *gd)
{
    return gamedata_word(gd, GD_STEP);
}

static void set_c602(GameData *gd, u16 v)
{
    gamedata_set_word(gd, GD_STEP, v);
}

static CharData *party_char(GameData *gd, int m)
{
    return &gd->chr[gd->party_member_id[m % GD_PARTY_MAX] % GD_CHARS];
}

/* ------------------------------------------------------------------ */
/* Building_Hospital (ps2.asm:16928)                                  */
/* ------------------------------------------------------------------ */

enum {
    HO_GREET = 2,       /* loc_B87C */
    HO_MENU,            /* loc_B8A2 */
    HO_CHOSEN,          /* loc_B8AE */
    HO_WHO,             /* loc_B90E */
    HO_WHO_CHOSEN,      /* loc_B924 */
    HO_YESNO,           /* loc_B95E */
    HO_ANSWER,          /* loc_B96A */
    HO_TREAT,           /* loc_B9A0 */
    HO_SHOW,            /* loc_BA00 */
    HO_ELSE,            /* loc_BA18 */
    HO_ELSE_ANSWER      /* loc_BA2A */
};

static BuildingResult hospital(BuildingState *bs, WinSys *ws, GameData *gd)
{
    if (ws->event_routine_sub != 0) {
        if (ws->event_routine_sub != 1) {
            win_close_all(ws);
            return BUILDING_RUNNING;
        }
        win_script(ws, SCRIPT_HOSPITAL_SEE_WELL_THEN_TAKE);                 /* "take care and good day" */
        ws->event_routine_sub++;
        return BUILDING_RUNNING;
    }

    switch (ws->event_routine) {
        case HO_GREET: {
            /* move.l #(WinID_StoreMeseta<<16 | WinID_ScriptMessage2):
             * big-endian, so the meseta box is first and the message
             * window last, and the message window's phase 1 is what
             * aims the text at it. */
            win_open(ws, WIN_STORE_MESETA);
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            if (language_barrier(gd)) {
                win_script(ws, SCRIPT_ITEMS_DANYARAHA_BEBEKUCHA);
                ws->event_routine_sub += 2;
            } else {
                win_script(ws, SCRIPT_HOSPITAL_MAY_HELP);
                ws->event_routine++;
            }
            break;
        }
        case HO_MENU: {
            win_open(ws, WIN_HEAL_CURE);
            ws->event_routine++;
            break;
        }
        case HO_CHOSEN: {
            if (ws->heal_cure != 0) {
                win_script(ws, SCRIPT_HOSPITAL_WHO_DO_WANT_US);         /* "Who do you want us to cure?" */
                ws->event_routine++;
                break;
            }
            /* HEAL: everyone alive short of full HP or full TP counts. */
            u32 needy = 0;
            for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX;
                 m++) {
                const CharData *c = party_char(gd, m);
                if (c->hp != 0 && (c->hp != c->max_hp || c->tp != c->max_tp)) {
                    needy++;
                }
            }
            if (needy == 0) {
                win_script2(ws, SCRIPT_HOSPITAL_NOBODY_SEEMS_INJURED_BELIEVE, SCRIPT_HOSPITAL_DOES_ANYONE_ELSE_NEED);
                ws->event_routine += 7;
                break;
            }
            /* The door's $FFFFF764 is the town's price per head. */
            ws->meseta_value = double_price(gd, needy * ws->store_max);
            ws->event_routine += 2;
            break;
        }
        case HO_WHO: {
            win_open(ws, WIN_STORE_CHAR_LIST);
            ws->meseta_value = double_price(gd, 10);
            ws->event_routine++;
            break;
        }
        case HO_WHO_CHOSEN: {
            if (ws->heal_cure != 0) {
                const CharData *c = &gd->chr[ws->character_index % GD_CHARS];
                if (!(c->status & 0x8000)) {
                    /* Not poisoned; loc_B924 swaps the first script for
                     * $508 when the character is dead. */
                    win_script2(ws, (c->hp == 0) ? SCRIPT_HOSPITAL_DEAD_IM_AFRAID : SCRIPT_HOSPITAL_DONT_SEEM_POISONED, SCRIPT_HOSPITAL_DOES_ANYONE_ELSE_NEED);
                    ws->event_routine += 5;
                    break;
                }
            }
            win_script(ws, SCRIPT_HOSPITAL_WILL_COST_MESETA_CARE);             /* "It will cost {meseta}" */
            ws->event_routine++;
            break;
        }
        case HO_YESNO: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case HO_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            if (!subtract_money(gd, ws->meseta_value)) {
                win_script(ws, SCRIPT_HOSPITAL_IF_DONT_ENOUGH_MONEY);
                ws->event_routine_sub += 2;
                break;
            }
            /* (WinID_MenuCharStats<<16 | $0623): the panel first. */
            win_open(ws, WIN_MENU_CHAR_STATS);
            win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW
                               | WIN_STORE_MESETA));
            bs->timer = 0x1E;
            ws->event_routine++;
            break;
        }
        case HO_TREAT: {
            bs->timer--;
            if (bs->timer >= 0) {
                break;
            }
            if (ws->heal_cure == 0) {
                /* loc_B9D8: HP and TP to their maximums, the dead left. */
                for (int m = 0;
                     m <= gd->party_members_num && m < GD_PARTY_MAX; m++) {
                    CharData *c = party_char(gd, m);
                    if (c->hp != 0) {
                        c->hp = c->max_hp;
                        c->tp = c->max_tp;
                    }
                }
                sfx(ws, SFX_HEALED);
            } else {
                gd->chr[ws->character_index % GD_CHARS].status &= 0x7FFF;
                sfx(ws, SFX_POISON_CURED);
            }
            win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW
                               | WIN_MENU_CHAR_STATS));
            bs->timer = 0x1E;
            ws->event_routine++;
            break;
        }
        case HO_SHOW: {
            bs->timer--;
            if (bs->timer >= 0) {
                break;
            }
            win_open(ws, WIN_CLOSE(1));
            win_script(ws, SCRIPT_HOSPITAL_DOES_ANYONE_ELSE_NEED);             /* "Does anyone else ..." */
            ws->event_routine++;
            break;
        }
        case HO_ELSE: {
            if (ws->script_id[0] == 0) {
                win_open(ws, WIN_YESNO2);
                ws->event_routine++;
            }
            break;
        }
        case HO_ELSE_ANSWER: {
            if (ws->yes_no_input != 0) {
                win_script(ws, SCRIPT_HOSPITAL_TAKE_CARE_GOOD_BYE);
                ws->event_routine_sub += 2;
                break;
            }
            if (ws->heal_cure != 0) {
                win_open(ws, WIN_CLOSE(1));     /* the party list */
            }
            ws->event_routine -= 8;
            ws->window_index_saved = WIN_HEAL_CURE;
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_CloneLab (ps2.asm:16773)                                  */
/* ------------------------------------------------------------------ */

enum {
    CL_GREET = 2,       /* loc_B6AE */
    CL_WHO,             /* loc_B6FA */
    CL_CHOSEN,          /* loc_B750 */
    CL_YESNO,           /* loc_B7A6 */
    CL_ANSWER,          /* loc_B7B2 */
    CL_REVIVED,         /* loc_B7EE */
    CL_AGAIN,           /* loc_B802 */
    CL_AGAIN_ANSWER     /* loc_B810 */
};

#define PORTRAIT_TOWER_OUTSIDE  0x15

static BuildingResult clone_lab(BuildingState *bs, WinSys *ws, GameData *gd)
{
    if (ws->event_routine_sub != 0) {
        if (ws->event_routine_sub != 1) {
            win_close_all(ws);
            return BUILDING_RUNNING;
        }
        win_script(ws, SCRIPT_CLONE_ALL_RIGHT_HERE_GO);                 /* "All right, here you go." */
        ws->event_routine_sub++;
        return BUILDING_RUNNING;
    }

    switch (ws->event_routine) {
        case CL_GREET: {
            /* move.l #(WinID_StoreMeseta<<16 | WinID_ScriptMessage2):
             * big-endian, so the meseta box is first and the message
             * window last, and the message window's phase 1 is what
             * aims the text at it. */
            win_open(ws, WIN_STORE_MESETA);
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            if (!bs->demo_flag) {
                win_script(ws, SCRIPT_CLONE_WELCOME_CLONE_LABS_WHO);
                ws->event_routine++;
                break;
            }
            /* After Neifirst: Nei cannot be brought back. $408 replaces
             * the greeting; with anyone dead, $40B follows it and the
             * revival walk below starts from the first member. */
            win_script(ws, SCRIPT_CLONE_WELCOME_CLONE_LABS_WHO_2);
            bool any_dead = false;
            for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX;
                 m++) {
                if (party_char(gd, m)->hp == 0) {
                    any_dead = true;
                    break;
                }
            }
            if (any_dead) {
                win_script(ws, SCRIPT_CLONE_WHAT_ABOUT_TIRED_YOUNG);
                set_c602(gd, 0);
                ws->event_routine++;
            } else {
                ws->event_routine += 2;
            }
            break;
        }
        case CL_WHO: {
            if (!bs->demo_flag) {
                win_open(ws, WIN_STORE_CHAR_LIST);
                ws->event_routine++;
                break;
            }
            u16 m = c602(gd);
            if (m > gd->party_members_num) {
                win_script(ws, SCRIPT_CLONE_ALL_RIGHT_HERE_GO);
                ws->event_routine++;
                break;
            }
            ws->character_index = gd->party_member_id[m % GD_PARTY_MAX];
            CharData *c = &gd->chr[ws->character_index % GD_CHARS];
            if (c->hp == 0) {
                c->hp = c->max_hp;
                sfx(ws, SFX_REVIVED);
                win_script(ws, SCRIPT_CLONE_RETURNED_LIFE);
            }
            set_c602(gd, (u16)(m + 1));
            break;
        }
        case CL_CHOSEN: {
            if (bs->demo_flag) {
                /* loc_B78A: on to the Central Tower on foot. */
                bs->building   = BUILDING_TOWER_OUTSIDE;
                bs->portrait   = PORTRAIT_TOWER_OUTSIDE;
                bs->demo_index = DEMO_INDEX_CLONE_LAB_TO_TOWER;
                win_close_all(ws);
                break;
            }
            const CharData *c = &gd->chr[ws->character_index % GD_CHARS];
            if (c->hp != 0) {
                win_script(ws, SCRIPT_CLONE_NO_PLACE_STUPID_JOKES);         /* "no place for stupid jokes" */
                ws->event_routine_sub++;
                break;
            }
            ws->meseta_value = (u32)c->level * 20;
            win_script(ws, SCRIPT_CLONE_WILL_COST_MESETA_WILL);
            ws->event_routine++;
            break;
        }
        case CL_YESNO: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case CL_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            if (!subtract_money(gd, ws->meseta_value)) {
                win_script(ws, SCRIPT_CLONE_NEVER_HEARD_MONEY_MAKES);
                ws->event_routine_sub++;
                break;
            }
            CharData *c = &gd->chr[ws->character_index % GD_CHARS];
            c->hp = c->max_hp;
            win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW
                               | WIN_STORE_MESETA));
            ws->event_routine++;
            break;
        }
        case CL_REVIVED: {
            sfx(ws, SFX_REVIVED);
            win_script2(ws, SCRIPT_CLONE_RETURNED_LIFE, SCRIPT_CLONE_THERE_ANYONE_ELSE_WOULD);
            ws->event_routine++;
            break;
        }
        case CL_AGAIN: {
            win_open(ws, WIN_CLOSE(1));
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case CL_AGAIN_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            win_script(ws, SCRIPT_CLONE_WHO_WOULD_LIKE_RETURNED);
            ws->event_routine -= 6;
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_DataMemory (ps2.asm:16552)                                */
/* ------------------------------------------------------------------ */

enum {
    DM_GREET = 2,       /* loc_B44A */
    DM_REPORT,          /* loc_B48A */
    DM_ASK,             /* loc_B4EE */
    DM_YESNO,           /* loc_B50C */
    DM_ANSWER,          /* loc_B518 */
    DM_SLOTS,           /* loc_B530 */
    DM_SLOT_CHOSEN,     /* loc_B53C */
    DM_OVERWRITE,       /* loc_B568 */
    DM_OVERWRITE_ANSWER,/* loc_B574 */
    DM_NAME,            /* loc_B598 */
    DM_SAVE,            /* loc_B5A4 */
    DM_SAVED,           /* loc_B628 */
    DM_MORE_ANSWER,     /* loc_B63C */
    DM_RESET            /* loc_B654 */
};

#define MAX_LEVEL 50

/* loc_B5A4. The name typed, or the slot's old name if nothing was. */
static void data_memory_save(WinSys *ws, GameData *gd)
{
    gamedata_set_word(gd, GD_LAST_SAVE_VISIPHONE, ws->store_index);       /* $C65C: the door's F766 */
    gamedata_set_word(gd, GD_LAST_SAVE_PLANET, gd->planet);            /* $C65A */
    gamedata_set_word(gd, GD_LAST_SAVE_MAP, gd->level_index);       /* $C646-$C64A */
    gamedata_set_word(gd, GD_LAST_SAVE_Y, gd->level_y);
    gamedata_set_word(gd, GD_LAST_SAVE_X, gd->level_x);
    gd->save_level = gd->chr[0].level;          /* $C684 = Rolf's level */

    /* (build 164) Six letters in a proportional language. */
    int n = lang_proportional() ? GD_NAME_MAX : GD_NAME_LEN;
    u8 name[GD_NAME_MAX];
    gamedata_set_name(name, ws->name_entry, n);
    bool blank = true;
    for (int i = 0; i < n; i++) {
        if (name[i] != 0 && name[i] != TXT_END) {
            blank = false;
        }
    }
    if (blank) {
        SlotSummary old;
        gamedata_slot_summary(ws->save_slot, &old);
        memcpy(name, old.name, GD_NAME_MAX);
    }
    memcpy(gd->save_name, name, GD_NAME_MAX);
    gamedata_set_word(gd, GD_SLOT_IN_USE, 0xFFFF);                /* $C69C */

    if (!gamedata_save(gd, ws->save_slot)) {
        fprintf(stderr, "data memory: saving slot %u failed\n",
                ws->save_slot + 1u);
    }
}

static BuildingResult data_memory(WinSys *ws, GameData *gd)
{
    if (ws->event_routine_sub != 0) {
        if (ws->event_routine_sub != 1) {
            win_close_all(ws);
            return BUILDING_RUNNING;
        }
        win_script(ws, SCRIPT_MEMORY_SEE_WELL_WAY_THEN);                 /* "be on your way then" */
        ws->event_routine_sub++;
        return BUILDING_RUNNING;
    }

    switch (ws->event_routine) {
        case DM_GREET: {
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            /* $F766 is set only at the Dezolis stations, which need no
             * cap to be understood. */
            if (ws->store_index == 0 && language_barrier(gd)) {
                win_script(ws, SCRIPT_ITEMS_DANYARAHA_BEBEKUCHA);
                ws->event_routine_sub += 2;
                break;
            }
            /* $F764 is set when CONTINUE lands here: "We meet again".
             * The loc_F550 that goes with it is game.c's, on the way in
             * from the select screen. */
            win_script(ws, (ws->store_max != 0) ? SCRIPT_MEMORY_MEET_AGAIN : SCRIPT_MEMORY_WELCOME_DATA_MEMORY);
            set_c602(gd, 0);
            ws->event_routine++;
            break;
        }
        case DM_REPORT: {
            /* One member per pass: how far each is from the next level. */
            u16 m = c602(gd);
            int who = gd->party_member_id[m % GD_PARTY_MAX] % GD_CHARS;
            const CharData *c = &gd->chr[who];
            ws->character_index = (u16)who;

            if (c->level == MAX_LEVEL) {
                win_script(ws, SCRIPT_MEMORY_NOT_POSSIBLE_GAIN_ANY);
            } else {
                ws->meseta_value = gamedata_level_exp(who, c->level)
                                   - c->experience;
                win_script(ws, SCRIPT_MEMORY_NEEDS_POINTS_GO_UP);
            }
            set_c602(gd, (u16)(m + 1));
            if (m == gd->party_members_num) {
                ws->event_routine++;
            }
            break;
        }
        case DM_ASK: {
            if (ws->store_max != 0) {
                win_script(ws, SCRIPT_MEMORY_CAREFUL_OUT_THERE);         /* "Be careful out there!" */
                ws->event_routine_sub += 2;
                break;
            }
            win_script(ws, SCRIPT_MEMORY_DO_WANT_PUT_MEMORIES);
            ws->event_routine++;
            break;
        }
        case DM_YESNO: {
            win_open(ws, WIN_YESNO3);
            ws->event_routine++;
            break;
        }
        case DM_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            win_script(ws, SCRIPT_MEMORY_WHAT_NUMBER_SHOULD_SAVE);             /* "What number ..." */
            ws->event_routine++;
            break;
        }
        case DM_SLOTS: {
            win_open(ws, WIN_SAVE_SLOTS);
            ws->event_routine++;
            break;
        }
        case DM_SLOT_CHOSEN: {
            SlotSummary slot;
            gamedata_slot_summary(ws->save_slot, &slot);
            if (slot.exists) {
                win_script(ws, SCRIPT_MEMORY_ALREADY_DATA_SAVED_UNDER);         /* "Is it OK to erase it?" */
                ws->event_routine++;
                break;
            }
            ws->yes_no_input = 0;
            ws->event_routine += 2;
            break;
        }
        case DM_OVERWRITE: {
            win_open(ws, WIN_YESNO3);
            ws->event_routine++;
            break;
        }
        case DM_OVERWRITE_ANSWER: {
            if (ws->yes_no_input != 0) {
                win_open(ws, WIN_CLOSE(1));
                ws->yes_no_input = 0;
                ws->event_routine -= 4;
                break;
            }
            win_script(ws, SCRIPT_MEMORY_ENTER_NAME_FILE);             /* "Enter a name ..." */
            ws->event_routine++;
            break;
        }
        case DM_NAME: {
            win_open(ws, WIN_NAME_INPUT);
            ws->event_routine++;
            break;
        }
        case DM_SAVE: {
            data_memory_save(ws, gd);
            win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW
                               | WIN_SAVE_SLOTS));
            win_script2(ws, SCRIPT_MEMORY_ALL_MEMORIES_EXPERIENCES_SAVED, SCRIPT_MEMORY_GOING_ADVENTURE_SOME_MORE);
            ws->event_routine++;
            break;
        }
        case DM_SAVED: {
            if (ws->script_id[0] == 0) {
                win_open(ws, WIN_CLOSE(1));
                win_open(ws, WIN_YESNO3);
                ws->event_routine++;
            }
            break;
        }
        case DM_MORE_ANSWER: {
            if (ws->yes_no_input != 0) {
                win_script(ws, SCRIPT_MEMORY_OK_GOOD_BYE_THEN);         /* "Ok, good-bye, then." */
                ws->event_routine++;
                break;
            }
            ws->event_routine_sub++;
            break;
        }
        case DM_RESET: {
            return BUILDING_RESET;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_TeleportStation (ps2.asm:18522)                           */
/* ------------------------------------------------------------------ */

enum {
    TP_GREET = 2,       /* loc_CABE */
    TP_PLACES,          /* loc_CB16 */
    TP_PRICE,           /* loc_CB22 */
    TP_YESNO,           /* loc_CB2E */
    TP_ANSWER,          /* loc_CB3A */
    TP_GO               /* loc_CBB0 */
};

/* TeleportLocCoord (ps2.asm:18650): the station's doorstep in each town
 * from Paseo, y then x, in 16px cells. */
static const u8 TELEPORT_ARRIVAL[9][2] = {
    { 0x12, 0x07 }, { 0x1C, 0x13 }, { 0x14, 0x39 }, { 0x08, 0x29 },
    { 0x16, 0x05 }, { 0x1A, 0x27 }, { 0x1A, 0x19 }, { 0x14, 0x13 },
    { 0x06, 0x0F }
};

static BuildingResult teleport_station(WinSys *ws, GameData *gd)
{
    if (ws->event_routine_sub != 0) {
        if (ws->event_routine_sub == 1) {
            win_script(ws, SCRIPT_TELEPORT_SORRY_COULDNT_HELP);             /* "Sorry we couldn't help" */
            ws->event_routine_sub++;
        } else if (ws->event_routine_sub == 2) {
            win_script(ws, SCRIPT_TELEPORT_WELL_THEN_COME_AGAIN);             /* "come again later!" */
            ws->event_routine_sub++;
        } else {
            win_close_all(ws);
        }
        return BUILDING_RUNNING;
    }

    switch (ws->event_routine) {
        case TP_GREET: {
            /* move.l #(WinID_StoreMeseta<<16 | WinID_ScriptMessage2):
             * big-endian, so the meseta box is first and the message
             * window last, and the message window's phase 1 is what
             * aims the text at it. */
            win_open(ws, WIN_STORE_MESETA);
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            if (language_barrier(gd)) {
                win_script(ws, SCRIPT_ITEMS_DANYARAHA_BEBEKUCHA);
                ws->event_routine_sub += 3;
                break;
            }
            ws->meseta_value = double_price(gd, 60);
            win_script(ws, SCRIPT_TELEPORT_WELCOME_TELEPORT_STATION);
            bool elsewhere = false;
            for (int i = 0; i < 12; i++) {
                u8 level = gd->c700[i];
                if (level != 0 && level != gd->level_index) {
                    elsewhere = true;
                    break;
                }
            }
            if (!elsewhere) {
                win_script(ws, SCRIPT_TELEPORT_JUST_MESETA_CAN_TELEPORT);         /* "any town you know of" */
                ws->event_routine_sub += 2;
                break;
            }
            win_script(ws, SCRIPT_TELEPORT_WHERE_WOULD_LIKE_TELEPORT);             /* "Where would you like" */
            ws->event_routine++;
            break;
        }
        case TP_PLACES: {
            win_open(ws, WIN_TELEPORT_PLACES);
            ws->event_routine++;
            break;
        }
        case TP_PRICE: {
            win_script(ws, SCRIPT_TELEPORT_WILL_COST_MESETA_WILL);
            ws->event_routine++;
            break;
        }
        case TP_YESNO: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case TP_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            if (!subtract_money(gd, ws->meseta_value)) {
                win_script(ws, SCRIPT_TELEPORT_DONT_SEEM_ENOUGH_MONEY);
                ws->event_routine_sub += 2;
                break;
            }
            win_script(ws, SCRIPT_TELEPORT_TELEPORT_BYE_BYE);             /* "Teleport on! Bye-bye!" */
            ws->event_routine++;

            int level = ws->teleport_level[ws->teleport_choice % 6];
            gd->level_index = (u16)level;
            if (level < 4) {
                gd->level_index = LEVEL_DEZOLIS_SKURE;
                gd->level_y     = 0x4F0;
                gd->level_x     = 0x3C0;
            } else if (level - 4 < 9) {
                gd->level_y = (u16)(TELEPORT_ARRIVAL[level - 4][0] << 4);
                gd->level_x = (u16)(TELEPORT_ARRIVAL[level - 4][1] << 4);
            }
            break;
        }
        case TP_GO: {
            sfx(ws, SFX_TELEPORT);
            ws->event_routine_sub += 3;
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_CentralTowerOutside (ps2.asm:17657)                       */
/* ------------------------------------------------------------------ */

enum {
    TO_GREET = 2,       /* loc_C066 */
    TO_OPTIONS,         /* loc_C0AC */
    TO_CHOSEN,          /* loc_C0C4 */
    TO_PORTRAIT,        /* loc_C10A */
    TO_ALARM,           /* loc_C116 */
    TO_GOVERNOR         /* loc_C122 */
};

static BuildingResult tower_outside(BuildingState *bs, WinSys *ws,
                                    GameData *gd)
{
    switch (ws->event_routine) {
        case TO_GREET: {
            win_open(ws, WIN_SCRIPT_MESSAGE_BIG);
            if (gd->chr[0].hp != 0
                && building_party_has_item(gd, ITEM_RECORDER)) {
                win_script(ws, SCRIPT_TOWER_MUST_GET_GOVERNOR);         /* "This must get to the governor!" */
                ws->event_routine += 5;
                break;
            }
            /* The word at $C736: the errand's step, then Nei gone. */
            u16 errand = (u16)(gd->c700[FLAG_GOVERNOR_ERRAND_STEP] << 8
                               | gd->c700[FLAG_NEI_GONE]);
            if (errand == 1) {
                /* $CD22 = $B4 here is overwritten by the message window's
                 * own phase 1 a frame later, in the ROM as here. */
                ws->end_timer = 0xB4;
                win_script(ws, SCRIPT_TOWER_SOMETHINGS_WRONG);         /* "Something's wrong..." */
                ws->event_routine += 3;
                break;
            }
            win_script(ws, SCRIPT_TOWER_CENTRAL_TOWER_PASEO_CAPITAL);             /* "... Where to?" */
            ws->event_routine++;
            break;
        }
        case TO_OPTIONS: {
            win_open(ws, (gd->c700[FLAG_CENTRAL_TOWER_ROOF_OPEN] != 0) ? WIN_TOWER_OPTIONS2
                                                        : WIN_TOWER_OPTIONS);
            ws->event_routine++;
            break;
        }
        case TO_CHOSEN: {
            if (ws->event_routine_sub != 0) {
                win_close_all(ws);
                break;
            }
            int opt = ws->tower_option;
            int portrait;
            if (opt == 0) {
                portrait = 0x14;                /* ROOM */
            } else if (opt == 1) {
                portrait = 0x08;                /* LIBRARY */
            } else if (opt == 2 && gd->c700[FLAG_CENTRAL_TOWER_ROOF_OPEN] != 0) {
                portrait = 0x18;                /* ROOF */
            } else {
                ws->event_routine_sub++;        /* OUTSIDE */
                break;
            }
            bs->building = opt + BUILDING_TOWER_ROOM;
            bs->portrait = portrait;
            return BUILDING_CHANGED;
        }
        case TO_PORTRAIT: {
            win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW
                               | WIN_TOWER_PORTRAIT_COPY));
            ws->event_routine++;
            break;
        }
        case TO_ALARM: {
            win_script(ws, SCRIPT_TOWER_OMIGOSH_LAKE_OVERFLOWING_THERE);             /* the lake is overflowing */
            ws->event_routine++;
            break;
        }
        default: {
            bs->building = BUILDING_GOVERNOR;
            bs->portrait = PORTRAIT_GOVERNOR;
            return BUILDING_CHANGED;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_CentralTowerRoom (ps2.asm:17741): the item keeper         */
/* ------------------------------------------------------------------ */

enum {
    RM_GREET = 2,       /* loc_C1A2 */
    RM_MENU,            /* loc_C1B4 */
    RM_CHOSEN,          /* loc_C1C0 */
    RM_KEEP_WHO,        /* loc_C1D8 */
    RM_KEEP_WHOSE,      /* loc_C1EA */
    RM_KEEP_ITEM,       /* loc_C224 */
    RM_KEEP_YESNO,      /* loc_C26E */
    RM_KEEP_ANSWER,     /* loc_C27A */
    RM_TAKE,            /* loc_C2A4 */
    RM_TAKE_LIST,       /* loc_C2C8 */
    RM_TAKE_ASK,        /* loc_C2DA */
    RM_TAKE_WHO,        /* loc_C2E6 */
    RM_TAKE_CHOSEN,     /* loc_C2F8 */
    RM_TAKE_ELSE,       /* loc_C34A */
    RM_TAKE_YESNO,      /* loc_C356 */
    RM_TAKE_ANSWER      /* loc_C362 */
};

/* Item byte 13, bit 5: the keeper will hold it. */
#define ITEM_STORABLE 0x20

/* $8002 + bit 3 of the slot: the party list and one page of the item
 * list, or both pages. */
static u16 close_item_lists(const WinSys *ws)
{
    return (u16)(0x8002 + ((ws->item_slot >> 3) & 1));
}

static BuildingResult tower_room(BuildingState *bs, WinSys *ws, GameData *gd)
{
    if (ws->event_routine_sub != 0) {
        if (ws->event_routine_sub != 1) {
            win_close_all(ws);
            return BUILDING_RUNNING;
        }
        win_script(ws, SCRIPT_ROOM_WELL_THEN_TAKE_CARE);
        ws->event_routine_sub++;
        steal_item(bs, ws, gd, "room");
        return BUILDING_RUNNING;
    }

    u8 *keeper_count;
    gamedata_bag(gd, GD_KEEPER, &keeper_count);

    switch (ws->event_routine) {
        case RM_GREET: {
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            win_script(ws, SCRIPT_ROOM_HOWRE_DOING_THERE_SOMETHING);
            ws->event_routine++;
            break;
        }
        case RM_MENU: {
            win_open(ws, WIN_ROOM_OPTIONS);
            ws->event_routine++;
            break;
        }
        case RM_CHOSEN: {
            if (ws->room_option != 0) {
                ws->event_routine += 6;         /* BAGGAGE PLEASE */
                break;
            }
            win_script(ws, SCRIPT_ROOM_WHOSE_BELONGINGS_DO_WANT);
            ws->event_routine++;
            break;
        }
        case RM_KEEP_WHO: {
            win_open(ws, WIN_STORE_CHAR_LIST);
            ws->event_routine_sub_2 = 0;
            ws->event_routine++;
            break;
        }
        case RM_KEEP_WHOSE: {
            if (ws->event_routine_sub_2 != 0) {
                win_open(ws, WIN_MENU_ITEM_LIST);
                ws->event_routine++;
                break;
            }
            if (gd->chr[ws->character_index % GD_CHARS].item_count != 0) {
                win_script(ws, SCRIPT_ROOM_WHICH_ITEMS);
                ws->event_routine_sub_2++;
                break;
            }
            win_script(ws, SCRIPT_ROOM_HUH_WHERE_CANT_SEE);             /* carrying nothing */
            ws->event_routine_sub++;
            break;
        }
        case RM_KEEP_ITEM: {
            u16 id = SCRIPT_ROOM_SORRY_ALL_LOCKERS_FILLED;                    /* the keeper is full */
            if (*keeper_count != 0x10) {
                id = SCRIPT_ROOM_DONT_THINK_HAD_BETTER;                    /* not something kept */
                if (gamedata_item_action_flags(ws->item_index)
                    & ITEM_STORABLE) {
                    id = SCRIPT_ROOM_OK_ILL_KEEP_THEM;
                    unequip_sold(gd, ws->character_index, ws->item_slot);
                    remove_item(gd, ws->character_index, ws->item_slot);
                    add_item(gd, GD_KEEPER, ws->item_index);
                }
            }
            win_script2(ws, id, SCRIPT_ROOM_THERE_ANYTHING_ELSE);
            ws->event_routine++;
            break;
        }
        case RM_KEEP_YESNO: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case RM_KEEP_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            win_open(ws, close_item_lists(ws));
            ws->window_index_saved = WIN_ROOM_OPTIONS;
            ws->event_routine -= 5;
            break;
        }
        case RM_TAKE: {
            if (*keeper_count == 0) {
                win_script(ws, SCRIPT_ROOM_THATS_FUNNY_IM_PRETTY);         /* nothing kept */
                ws->item_slot = 0xFFFF;
                ws->event_routine += 5;
                break;
            }
            win_script(ws, SCRIPT_ROOM_THESE_THINGS_HERE_WHICH);
            ws->event_routine++;
            break;
        }
        case RM_TAKE_LIST: {
            ws->character_index = GD_KEEPER;
            win_open(ws, WIN_MENU_ITEM_LIST);
            ws->event_routine++;
            break;
        }
        case RM_TAKE_ASK: {
            win_script(ws, SCRIPT_ROOM_WHOS_GOING_TAKE);
            ws->event_routine++;
            break;
        }
        case RM_TAKE_WHO: {
            win_open(ws, WIN_STORE_CHAR_LIST);
            ws->event_routine_sub_2 = 0;
            ws->event_routine++;
            break;
        }
        case RM_TAKE_CHOSEN: {
            if (ws->event_routine_sub_2 != 0) {
                ws->event_routine_sub_2 = 0;
                ws->window_index_saved  = WIN_STORE_CHAR_LIST;
                break;
            }
            int who = ws->character_index % GD_CHARS;
            if (gd->chr[who].item_count == 0x10) {
                win_script(ws, SCRIPT_ROOM_TOO_MUCH_ALREADY_CAN);         /* has too much */
                ws->event_routine_sub_2++;
                break;
            }
            win_script(ws, SCRIPT_ROOM_OK_YOURE_GOING_TAKE);
            remove_item(gd, GD_KEEPER, ws->item_slot);
            add_item(gd, who, ws->item_index);
            ws->event_routine++;
            break;
        }
        case RM_TAKE_ELSE: {
            win_script(ws, SCRIPT_ROOM_THERE_ANYTHING_ELSE);
            ws->event_routine++;
            break;
        }
        case RM_TAKE_YESNO: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case RM_TAKE_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            if (!(ws->item_slot & 0x8000)) {
                win_open(ws, close_item_lists(ws));
            }
            ws->item_slot = 0;
            ws->window_index_saved = WIN_ROOM_OPTIONS;
            ws->event_routine -= 0x0D;
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_Library (ps2.asm:17932)                                   */
/* ------------------------------------------------------------------ */

enum {
    LB_GREET = 2,       /* loc_C3E6 */
    LB_MENU,            /* loc_C448 */
    LB_TOPIC,           /* loc_C454 */
    LB_YESNO,           /* loc_C476 */
    LB_ANSWER,          /* loc_C482 */
    LB_GRAPH,           /* loc_C49A */
    LB_GRAPH_TEXT,      /* loc_C4A6 */
    LB_GRAPH_PALETTE,   /* loc_C4B2 */
    LB_GRAPH_TEXT2,     /* loc_C4BE */
    LB_KEY_TUBE         /* loc_C4CA */
};

/* LibraryTextIndArray: the five topics' scripts. */
static const u8 LIBRARY_TOPIC[5] = { 0x04, 0x05, 0x08, 0x0A, 0x0D };

/* CheckItemExistInventory: who in the party carries it and where; also
 * where RemoveItemFromInventory will take it from. */
static bool find_party_item(const GameData *gd, int item, u16 *who,
                            u16 *slot)
{
    for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX; m++) {
        int c = gd->party_member_id[m] % GD_CHARS;
        for (int i = 0; i < GD_ITEM_SLOTS; i++) {
            if ((gd->chr[c].items[i] & 0x7F) == item) {
                *who  = (u16)c;
                *slot = (u16)i;
                return true;
            }
        }
    }
    return false;
}

static BuildingResult library(BuildingState *bs, WinSys *ws, GameData *gd)
{
    if (ws->event_routine_sub != 0) {
        if (ws->event_routine_sub != 1) {
            win_close_all(ws);
            return BUILDING_RUNNING;
        }
        win_script(ws, SCRIPT_LIBRARY_SEE_WELL_SEE_LATER);
        ws->event_routine_sub++;
        return BUILDING_RUNNING;
    }

    switch (ws->event_routine) {
        case LB_GREET: {
            win_open(ws, WIN_SCRIPT_MESSAGE_BIG);
            if (gd->chr[0].hp != 0
                && find_party_item(gd, ITEM_RECORDER, &ws->character_index,
                                   &ws->item_slot)) {
                win_script(ws, SCRIPT_LIBRARY_ANALYSIS_RECORDER_BEEN_COMPLETED);         /* the Recorder */
                ws->event_routine += 5;
                break;
            }
            /* What the librarian says next follows the story: $C722,
             * then Nei gone ($C737), then the roof open ($C73F). */
            ws->character_index = 0;
            u16 news = SCRIPT_LIBRARY_UNDERSTAND_OTHERS_GOING_BIOSYSTEMS;
            if (gd->c700[FLAG_RECORDER_FOUND] != 0) {
                news = SCRIPT_LIBRARY_FIGURED_OUT_CAUSE_ACCIDENT;
                if (gd->c700[FLAG_NEI_GONE] != 0) {
                    news = SCRIPT_LIBRARY_TRUE_MOTA_GOING_FLOODED;
                    if (gd->c700[FLAG_CENTRAL_TOWER_ROOF_OPEN] != 0) {
                        news = SCRIPT_LIBRARY_THANKS_ALL_MOTA_SAVED;
                    }
                }
            }
            win_script(ws, SCRIPT_LIBRARY_WELCOME_LIBRARY);
            win_script(ws, news);
            win_script(ws, SCRIPT_LIBRARY_IF_WANT_FIND_OUT);
            ws->event_routine++;
            break;
        }
        case LB_MENU: {
            win_open(ws, WIN_LIBRARY_OPTIONS);
            ws->event_routine++;
            break;
        }
        case LB_TOPIC: {
            win_script(ws, (u16)(0x1000 | LIBRARY_TOPIC[ws->library_topic % 5]));
            win_script(ws, SCRIPT_LIBRARY_THERE_ANYTHING_ELSE_WOULD);             /* anything else? */
            ws->event_routine++;
            break;
        }
        case LB_YESNO: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case LB_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            ws->window_index_saved = WIN_LIBRARY_OPTIONS;
            ws->event_routine -= 2;
            break;
        }
        case LB_GRAPH: {
            win_open(ws, WIN_LIBRARY_GRAPH);
            ws->event_routine++;
            break;
        }
        case LB_GRAPH_TEXT: {
            win_script(ws, SCRIPT_LIBRARY_PLEASE_LOOK_GRAPH_SHOWS);
            ws->event_routine++;
            break;
        }
        case LB_GRAPH_PALETTE: {
            /* (build 138d) PaletteLoad2 $32 (loc_C4B2): straight into the
             * live palette's line 1 -- PaletteLoad1 is the one that adds
             * $80 for the target -- so the graph's hidden lines appear
             * (game.c applies it). Before 138d taken for a target-only
             * load and left out. */
            bs->graph_overlay = true;
            ws->event_routine++;
            break;
        }
        case LB_GRAPH_TEXT2: {
            win_script(ws, SCRIPT_LIBRARY_SEE_ENERGY_WHICH_USED);
            ws->event_routine++;
            break;
        }
        default: {
            /* The Recorder for the Key Tube, in the same hands. */
            remove_item(gd, ws->character_index, ws->item_slot);
            add_item(gd, ws->character_index, (u8)item_id("KeyTube"));
            sfx(ws, SFX_ITEM_RECEIVED);
            ws->event_routine_sub += 2;
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_RolfHouse (ps2.asm:16198)                                 */
/* ------------------------------------------------------------------ */

enum {
    RH_GREET = 2,       /* loc_B04E */
    RH_LEAVE_YESNO,     /* loc_B0A6 */
    RH_LEAVE_ANSWER,    /* loc_B0B2 */
    RH_MENU,            /* loc_B0DA */
    RH_OPTION,          /* loc_B0EC */
    RH_ARRIVAL,         /* loc_B282 */
    RH_RENAME_YESNO,    /* loc_B2EC */
    RH_RENAME_ANSWER,   /* loc_B306 */
    RH_NAME,            /* loc_B350 */
    RH_NAMED,           /* loc_B35C */
    RH_DONE             /* loc_B398 */
};

#define CHAR_NEI         1

/* RolfHouse_CharIntroTextPtrs and _CharNameChangeTextPtrs: by the
 * joiner's character index, from Rudo (2). */
static const u8 HOUSE_INTRO[6]  = { 0x1C, 0x1E, 0x1F, 0x21, 0x23, 0x25 };
static const u8 HOUSE_RENAME[6] = { 0x16, 0x17, 0x17, 0x17, 0x18, 0x19 };

/* loc_B0CE: "There doesn't seem to be ...", then out via the sub path. */
static void house_goodbye(WinSys *ws)
{
    win_script(ws, SCRIPT_HOME_THERE_DOESNT_SEEM_ANY);
    ws->event_routine_sub++;
}

/* RolfHouseOption_SeeStrength: the list, the character's portrait, then
 * level/experience, profile and stats together until B or C. */
static BuildingResult house_strength(BuildingState *bs, WinSys *ws,
                                     const Input *in)
{
    switch (ws->event_routine_sub) {
        case 0: {
            win_open(ws, WIN_PROFILE_CHAR_LIST);
            ws->event_routine_sub++;
            return BUILDING_RUNNING;
        }
        case 1: {
            win_open(ws, WIN_CLOSE(1));
            bs->portrait        = ws->profile_choice;
            ws->character_index = ws->profile_choice;
            win_open(ws, (u16)(WIN_PORTRAIT_START + ws->profile_choice));
            ws->event_routine_sub++;
            return BUILDING_LOAD_PORTRAIT;
        }
        case 2: {
            win_open(ws, WIN_HOUSE_LVEXP);
            win_open(ws, (u16)(WIN_PROFILE_START + ws->profile_choice));
            win_open(ws, WIN_STRNG_STATS);
            ws->event_routine_sub++;
            return BUILDING_RUNNING;
        }
        default: {
            /* WaitJoypad_B_C_Pressed, then four windows closed. */
            if (in != NULL && (in->pressed.b || in->pressed.c)) {
                ws->event_routine_sub -= 2;
                win_close_current(ws, WIN_CLOSE(4));
            }
            return BUILDING_RUNNING;
        }
    }
}

/* RolfHouseOption_Reorganize: pick up to three to go with Rolf. */
static void house_regroup(BuildingState *bs, WinSys *ws, GameData *gd)
{
    switch (ws->event_routine_sub) {
        case 0: {
            if (gd->party_members_joined == 1) {
                win_script(ws, SCRIPT_HOME_GUESS_THERE_ARENT_ENOUGH);     /* nobody else to take */
            } else {
                win_script2(ws, SCRIPT_HOME_LETS_SEE_HOW_MANY, SCRIPT_HOME_WHO_SHOULD_TAKE_INTO);
            }
            ws->event_routine_sub++;
            break;
        }
        case 1: {
            if (gd->party_members_joined == 1) {
                win_close_current(ws, 0);
                break;
            }
            /* loc_B1A0's bsr loc_F550: the battle commands back to their
             * defaults, done by the building screen (game.c). */
            bs->reset_commands = true;
            memset(gd->party_member_id, 0, sizeof gd->party_member_id);
            gd->party_members_num = 0;
            if (gd->c700[FLAG_NEI_GONE] == 0) {
                gd->party_members_num  = 1;
                gd->party_member_id[1] = CHAR_NEI;
            }
            /* (SelectedChar<<16 | CharList): the party box first. */
            win_open(ws, WIN_REGROUP_SELECTED);
            win_open(ws, WIN_REGROUP_CHAR_LIST);
            ws->event_routine_sub++;
            break;
        }
        case 2: {
            gd->party_members_num++;
            gd->party_member_id[gd->party_members_num % GD_PARTY_MAX] =
                ws->character_index;
            win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW
                               | WIN_REGROUP_SELECTED));
            u16 id = (gd->party_members_num == 3) ? SCRIPT_HOME_MEMBER_MAKES_FOUR_LETS : SCRIPT_HOME_MEMBER_DO_WANT_ANYONE;
            if (gd->party_members_joined == 2) {
                id = SCRIPT_HOME_WEVE_ADDED_MUCH_MORE;
            }
            win_script(ws, id);
            ws->event_routine_sub++;
            break;
        }
        case 3: {
            if (ws->script_id[0] != 0) {
                break;
            }
            if (gd->party_members_joined == 2
                || gd->party_members_num == 3) {
                win_close_all(ws);
                break;
            }
            win_open(ws, WIN_YESNO3);
            ws->event_routine_sub++;
            break;
        }
        default: {
            if (ws->yes_no_input != 0) {
                ws->event_routine     -= 2;
                ws->event_routine_sub -= 4;
                house_goodbye(ws);
                break;
            }
            win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW
                               | WIN_REGROUP_CHAR_LIST));
            ws->event_routine_sub -= 2;
            break;
        }
    }
}

static BuildingResult rolf_house(BuildingState *bs, WinSys *ws, GameData *gd,
                                 const Input *in)
{
    switch (ws->event_routine) {
        case RH_GREET: {
            win_open(ws, WIN_SCRIPT_MESSAGE_BIG);
            if (gd->party_members_joined == gd->party_member_join_next) {
                /* Nobody new: "home at last", who is with you, and the
                 * question of heading out again. character_index 8 names
                 * the first party member who is not Rolf. */
                ws->character_index = 8;
                win_script(ws, SCRIPT_HOME_AHH_HOME_LAST_BUT);
                win_script(ws, (u16)(SCRIPT_HOME_AM_GOING_JOURNEY_OWN + gd->party_members_num));
                win_script(ws, SCRIPT_HOME_SHALL_CONTINUE_OUR_QUEST);
                ws->event_routine++;
                break;
            }
            if (gd->party_member_join_next == 8) {
                win_script(ws, SCRIPT_HOME_HEY_SHIR_COMING_BACK);     /* "Hey, Shir is coming back!" */
            } else {
                gd->party_members_joined++;
                win_script(ws, SCRIPT_HOME_THERES_KNOCK_DOOR);     /* "There's a knock ..." */
            }
            ws->event_routine += 5;
            break;
        }
        case RH_LEAVE_YESNO: {
            win_open(ws, WIN_YESNO3);
            ws->event_routine++;
            break;
        }
        case RH_LEAVE_ANSWER: {
            if (ws->event_routine_sub != 0) {
                win_close_all(ws);
                break;
            }
            if (ws->yes_no_input == 0) {
                house_goodbye(ws);
                break;
            }
            win_script2(ws, SCRIPT_HOME_WILL_DIFFICULT_JOURNEY_PICK, SCRIPT_HOME_LETS_CHECK_CURRENT_ROSTER);
            ws->event_routine++;
            break;
        }
        case RH_MENU: {
            if (ws->script_id[0] == 0) {
                win_open(ws, WIN_ROLF_HOUSE_OPTIONS);
                ws->event_routine++;
            }
            break;
        }
        case RH_OPTION: {
            switch (ws->house_option & 3) {
                case 0: {
                    return house_strength(bs, ws, in);
                }
                case 1: {
                    house_regroup(bs, ws, gd);
                    break;
                }
                default: {
                    /* RolfHouseOption_Outside */
                    ws->event_routine -= 2;
                    house_goodbye(ws);
                    break;
                }
            }
            break;
        }
        case RH_ARRIVAL: {
            int who = gd->party_members_joined;
            ws->character_index = 0;
            bs->portrait = who;
            win_open(ws, (u16)(WIN_PORTRAIT_START + who));
            ws->event_routine++;
            if (gd->party_member_join_next == 8) {
                /* Shir back in the party, in the last place taken. */
                gd->party_member_join_next--;
                gd->party_members_num++;
                gd->party_member_id[gd->party_members_num % GD_PARTY_MAX] =
                    CHAR_SHIR;
                win_script(ws, SCRIPT_HOME_SORRY_MAKE_WORRY_BUT);
                ws->event_routine_sub++;
            } else if (who >= 2 && who <= 7) {
                win_script(ws, (u16)(0x0900 | HOUSE_INTRO[who - 2]));
            }
            return BUILDING_LOAD_PORTRAIT;
        }
        case RH_RENAME_YESNO: {
            if (ws->script_id[0] != 0) {
                break;
            }
            if (ws->event_routine_sub != 0) {
                win_close_all(ws);
                break;
            }
            win_open(ws, WIN_YESNO3);
            ws->event_routine++;
            break;
        }
        case RH_RENAME_ANSWER: {
            int who = gd->party_members_joined;
            ws->character_index = (u16)who;
            if (ws->yes_no_input != 0) {
                win_script2(ws, SCRIPT_HOME_NAME_FITS_WELL_IM, SCRIPT_HOME_LETS_CHECK_ROSTER_POINT);
                ws->event_routine += 3;
                break;
            }
            ws->event_routine++;
            if (who >= 2 && who <= 7) {
                win_script(ws, (u16)(0x0900 | HOUSE_RENAME[who - 2]));
            }
            if (who == 7) {
                /* Shir keeps her name: no name entry. */
                win_script2(ws, SCRIPT_HOME_EXCUSE_ME, SCRIPT_HOME_LETS_CHECK_ROSTER_POINT);
                ws->event_routine += 2;
            }
            break;
        }
        case RH_NAME: {
            win_open(ws, WIN_NAME_INPUT);
            ws->event_routine++;
            break;
        }
        case RH_NAMED: {
            /* loc_B36C: a name typed replaces the old one; nothing typed
             * keeps it. */
            bool blank = true;
            for (int i = 0; i < WIN_NAME_MAX; i++) {
                if (ws->name_entry[i] != 0 && ws->name_entry[i] != TXT_END) {
                    blank = false;
                }
            }
            if (!blank) {
                /* (build 163) The letters typed are the whole name; (164)
                 * six in a proportional language. */
                gamedata_set_name(gd->chr[ws->character_index % GD_CHARS].name,
                                  ws->name_entry, lang_proportional()
                                                  ? GD_NAME_MAX : GD_NAME_LEN);
            }
            win_script2(ws, SCRIPT_HOME_WILL_CALL, SCRIPT_HOME_LETS_CHECK_ROSTER_POINT);
            ws->event_routine++;
            break;
        }
        default: {
            win_open(ws, WIN_CLOSE(1));         /* the newcomer's portrait */
            ws->event_routine -= 7;
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Building_UstvestiaHouse (ps2.asm:18151)                            */
/* ------------------------------------------------------------------ */

/* UstvesHouseEventIndex, by event_routine (the dispatcher's d1 is
 * event_routine - 1, and the table is entered at -4). */
enum {
    UV_GREET = 2,       /* loc_C6A4 */
    UV_ASK_PLAY,        /* loc_C6B6 */
    UV_PLAY_ANSWER,     /* loc_C6C2 */
    UV_ASK_LEARN,       /* loc_C6E2 */
    UV_LEARN_ANSWER,    /* loc_C6EE */
    UV_ASK_SURE,        /* loc_C706 */
    UV_SURE_ANSWER,     /* loc_C712 */
    UV_WHO,             /* loc_C730 */
    UV_PRICE,           /* loc_C73C */
    UV_ASK_PAY,         /* loc_C76A */
    UV_PAY_ANSWER,      /* loc_C778 */
    UV_LESSON,          /* loc_C7AC */
    UV_LEARNED,         /* loc_C7D6 */
    UV_SONGS,           /* loc_C7FC */
    UV_SONG_PICK        /* loc_C80E */
};

/* Ustvestia_MusicPointers: the sound id and the event flag that must be
 * set for the song to be offered (0: always). event_flags is $C710, so
 * flag n is c700[$10 + n]. */
static const u8 USTVESTIA_SONG[22][2] = {
    { 0x81, 0x00 }, { 0x82, 0x00 }, { 0x84, 0x00 }, { 0x87, 0x00 },
    { 0x8B, 0x00 }, { 0x8C, 0x00 }, { 0x8D, 0x00 }, { 0x8E, 0x00 },
    { 0x8F, 0x00 }, { 0x92, 0x00 }, { 0x94, 0x00 }, { 0x95, 0x00 },
    { 0x86, 0x06 }, { 0x88, 0x06 }, { 0x93, 0x2F }, { 0x83, 0x2F },
    { 0x85, 0x2F }, { 0x89, 0x2F }, { 0x90, 0x2F }, { 0x8A, 0x34 },
    { 0x91, 0x34 }, { 0x97, 0x34 }
};

static BuildingResult ustvestia(WinSys *ws, GameData *gd, const Input *in)
{
    /* loc_C652: sub 1 is "Well, actually, I'm kind of busy right now,
     * I've got to go!" ($A04); anything past it closes everything. */
    if (ws->event_routine_sub != 0) {
        if (ws->event_routine_sub != 1) {
            win_close_all(ws);
            return BUILDING_RUNNING;
        }
        win_script_set(ws, SCRIPT_USTVESTIA_WELL_ACTUALLY_IM_KIND);
        ws->event_routine_sub++;
        return BUILDING_RUNNING;
    }

    switch (ws->event_routine) {
        case UV_GREET: {
            /* "I am Ustvestia, a musician. Do you want to hear me
             * play?" */
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            win_script_set(ws, SCRIPT_USTVESTIA_AM_USTVESTIA_MUSICIAN_DO);
            ws->event_routine++;
            break;
        }
        case UV_ASK_PLAY: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case UV_PLAY_ANSWER: {
            if (ws->yes_no_input != 0) {
                /* "Oh, I get it, you want to learn to play the piano!" */
                win_script_set(ws, SCRIPT_USTVESTIA_OH_GET_WANT_LEARN);
                ws->event_routine++;
                break;
            }
            /* "Great! Pick a song." -- on to the soundtracks. */
            win_script_set(ws, SCRIPT_USTVESTIA_GREAT_PICK_SONG);
            ws->event_routine += 0x0B;
            break;
        }
        case UV_ASK_LEARN: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case UV_LEARN_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            win_script_set(ws, SCRIPT_USTVESTIA_AM_GREAT_MUSICIAN_OR);
            ws->event_routine++;
            break;
        }
        case UV_ASK_SURE: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case UV_SURE_ANSWER: {
            if (ws->yes_no_input != 0) {
                win_script_set(ws, SCRIPT_USTVESTIA_WHY_GO_GET_OUT);
                ws->event_routine_sub += 2;
                break;
            }
            win_script_set(ws, SCRIPT_USTVESTIA_WHO_GOING_LEARN);
            ws->event_routine++;
            break;
        }
        case UV_WHO: {
            win_open(ws, WIN_STORE_CHAR_LIST);
            ws->event_routine++;
            break;
        }
        case UV_PRICE: {
            /* Bit 0 of the character: the women (Nei, Amy, Anna, Shir)
             * pay 5000, the men 2000. */
            ws->event_routine++;
            if (ws->character_index & 1) {
                win_script_set(ws, SCRIPT_USTVESTIA_ALL_RIGHT_ILL_GIVE);
                ws->meseta_value = 5000;
                break;
            }
            win_script_set(ws, SCRIPT_USTVESTIA_HEY_LOOKS_SMART_ILL);
            ws->meseta_value = 2000;
            break;
        }
        case UV_ASK_PAY: {
            /* move.l #(WinID_StoreMeseta<<16 | WinID_YesNo2): both. */
            win_open(ws, WIN_STORE_MESETA);
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case UV_PAY_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            if (!subtract_money(gd, ws->meseta_value)) {
                /* "You don't have enough money. I'm not cheap." */
                win_script_set(ws, SCRIPT_USTVESTIA_DONT_ENOUGH_MONEY_IM);
                ws->event_routine_sub += 2;
                break;
            }
            win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW
                               | WIN_STORE_MESETA));
            /* "Ok, I'll start the lessons." */
            win_script_set(ws, SCRIPT_USTVESTIA_OK_ILL_START_LESSONS);
            ws->event_routine++;
            break;
        }
        case UV_LESSON: {
            /* Once the line is out: PaletteFadeFrom, SXFID_Musik, $78
             * frames of vblank_routine $14, PaletteFadeTo. The screen
             * does that part. */
            if (ws->script_id[0] != 0) {
                break;
            }
            ws->event_routine++;
            return BUILDING_LESSON;
        }
        case UV_LEARNED: {
            /* "Now you are also a musical artist!" Bit 7 of the
             * character's map technique count ($C026 + $40 * n) is
             * Musik; the first time, the count goes up by one and
             * "{char} learned Musik" follows ($A0D in $CD02). */
            win_script_set(ws, SCRIPT_USTVESTIA_NOW_ALSO_MUSICAL_ARTIST);
            CharData *d = &gd->chr[ws->character_index % GD_CHARS];
            if (!(d->techs_b & 0x80)) {
                d->techs_b = (u8)((d->techs_b | 0x80) + 1);
                ws->script_id[1] = SCRIPT_USTVESTIA_LEARNED_MUSIK_TECHNIQUE;
            }
            ws->event_routine_sub += 2;
            break;
        }
        case UV_SONGS: {
            ws->ustvestia_song = 0;
            win_open(ws, WIN_USTVESTIA_SONGS);
            ws->event_routine++;
            break;
        }
        case UV_SONG_PICK: {
            /* Up and down step two bytes through the pointers, wrapping
             * ($2A at the top, 0 past $2C), and keep stepping the same
             * way past a song whose flag is not yet set. B leaves (sub
             * 1: $A04); C plays it. Either redraws the window. */
            bool up   = in != NULL && in->pressed.up;
            bool down = in != NULL && in->pressed.down;
            bool b    = in != NULL && in->pressed.b;
            bool c    = in != NULL && in->pressed.c;
            if (!up && !down && !b && !c) {
                break;
            }
            int d1 = ws->ustvestia_song;
            for (int guard = 0; guard < 24; guard++) {
                if (up) {
                    d1 -= 2;
                    if (d1 < 0) {
                        d1 = 0x2A;
                    }
                }
                if (down) {
                    d1 += 2;
                    if (d1 >= 0x2C) {
                        d1 = 0;
                    }
                }
                u8 flag = USTVESTIA_SONG[(d1 >> 1) % 22][1];
                if (flag == 0 || gd->c700[(FLAG_EVENT_SLOTS + flag) & 0xFF] != 0) {
                    break;
                }
            }
            ws->ustvestia_song = (u16)d1;
            if (b) {
                ws->event_routine_sub++;
                break;
            }
            if (c && ws->sound != NULL) {
                ws->sound(ws->ctx, USTVESTIA_SONG[(d1 >> 1) % 22][0]);
            }
            win_open(ws, (u16)(WIN_F_INSTANT | WIN_F_REDRAW
                               | WIN_USTVESTIA_SONGS));
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_InventorHouse (ps2.asm:18378): Kueri's gum inventor         */
/* ------------------------------------------------------------------ */

/* GumInvHouseEventIndex, entered at -4 with d1 = event_routine - 1, as
 * every building's table is. There is no flag: the leaf is asked for
 * every visit, and one turned in again (a second leaf) makes more gum. */
enum {
    INV_GREET = 2,      /* loc_C8CA */
    INV_LEAF,           /* loc_C8E2 */
    INV_ASK_KNOW,       /* loc_C902 */
    INV_KNOW_ANSWER,    /* loc_C90E */
    INV_ASK_GIVE,       /* loc_C92A */
    INV_GIVE_ANSWER     /* loc_C936 */
};

/* CheckItemExistInventory (ps2.asm:19067) as it leaves the RAM:
 * character_index is each member in turn as it is searched and $DE84
 * counts the slots, so a find leaves the holder and the slot, and a miss
 * the last member and 16. */
static bool check_item_exist(WinSys *ws, const GameData *gd, int item)
{
    for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX; m++) {
        int c = gd->party_member_id[m] % GD_CHARS;
        ws->character_index = (u16)c;
        ws->item_slot = 0;
        for (int i = 0; i < GD_ITEM_SLOTS; i++) {
            if ((gd->chr[c].items[i] & 0x7F) == item) {
                return true;
            }
            ws->item_slot++;
        }
    }
    return false;
}

static BuildingResult inventor_house(WinSys *ws, GameData *gd)
{
    if (ws->event_routine_sub != 0) {
        win_close_all(ws);
        return BUILDING_RUNNING;
    }

    switch (ws->event_routine) {
        case INV_GREET: {
            /* $B08 is written first and overwritten at once: "Now, let me
             * see, what can I work on?" is never shown. */
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            win_script_set(ws, SCRIPT_INVENTOR_NOW_LET_ME_SEE);
            win_script_set(ws, SCRIPT_INVENTOR_HI_IM_WORKING_INVENTING);     /* "... a new kind of gum!" */
            ws->event_routine++;
            break;
        }
        case INV_LEAF: {
            if (check_item_exist(ws, gd, ITEM_MRURA_LEAF)) {
                win_script_set(ws, SCRIPT_INVENTOR_THATS_MARUERA_LEAF_PLEASE); /* "That's it; a MARUERA LEAF!" */
                ws->event_routine += 3;
                break;
            }
            win_script_set(ws, SCRIPT_INVENTOR_DO_KNOW_WHAT_MARUERA);     /* "Do you know what MARUERA
                                             * LEAVES are?" */
            ws->event_routine++;
            break;
        }
        case INV_ASK_KNOW: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case INV_KNOW_ANSWER: {
            ws->event_routine_sub++;
            if (ws->yes_no_input != 0) {
                /* NO: the tree on an island in the sea, then the ask. */
                win_script_set2(ws, SCRIPT_INVENTOR_THERE_MARUERA_TREE_ISLAND, SCRIPT_INVENTOR_NEED_SOME_THOSE_LEAVES);
                break;
            }
            win_script_set(ws, SCRIPT_INVENTOR_NEED_SOME_THOSE_LEAVES);     /* "I need some of those
                                             * leaves." */
            break;
        }
        case INV_ASK_GIVE: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case INV_GIVE_ANSWER: {
            ws->event_routine_sub++;
            if (ws->yes_no_input != 0) {
                win_script_set(ws, SCRIPT_INVENTOR_THATS_TOO_BAD_WELL); /* "That's too bad." */
                break;
            }
            /* loc_C948: the leaf out of the bag INV_LEAF found it in, the
             * gum into the same bag, after the last item. */
            remove_item(gd, ws->character_index, ws->item_slot);
            ws->item_index = ITEM_MRURA_GUM;
            add_item(gd, ws->character_index, ITEM_MRURA_GUM);
            if (ws->sound != NULL) {
                ws->sound(ws->ctx, SFX_ITEM_RECEIVED);
            }
            win_script_set(ws, SCRIPT_INVENTOR_THANKS_IF_YOULL_WAIT);     /* "... Done! There's plenty,
                                             * so take some!" */
            break;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_Roof (ps2.asm:18055): the spaceship                       */
/* ------------------------------------------------------------------ */

enum {
    ROOF_ASK = 2,           /* loc_C52A */
    ROOF_YESNO,             /* loc_C53C */
    ROOF_ANSWER,            /* loc_C548 */
    ROOF_TO_SKURE,          /* loc_C572 */
    ROOF_TO_PASEO           /* loc_C578 */
};

/* Building_Roof. The desk's ROOF ($C73F) and Skure's door (79,59) both
 * name building 11 with portrait $18; BuildingScreen's loc_8318 adds 5 to
 * event_routine on DezolisSkure, so Skure's side starts at 6 -- no
 * portrait, no question, the flight to Paseo on the first idle pass
 * (game.c enter_building). Building_CheckRoutine dispatches on
 * event_routine - 1, so 2-6 are RoofEventIndex's five entries.
 *   2  WinID_ScriptMessage2, $F01
 *   3  WinID_YesNo2
 *   4  NO: event_routine_sub + 1. YES: $F02, DezolisSkure $4F0,$3C0
 *   5  loc_C572: PaletteFadeFrom, then loc_C58A -- the flight
 *   6  loc_C578: Paseo $100,$1F0, then loc_C58A
 * loc_C500, with event_routine_sub set: 1 is $F03 and + 1, anything else
 * CloseAllWindows (event_routine 0, the street). No flag is written,
 * jet_scooter_flag is not touched, and event_routine stays where it is:
 * SpaceShipLoop sets ScreenID_Level itself. */
static BuildingResult roof(BuildingState *bs, WinSys *ws, GameData *gd)
{
    if (ws->event_routine_sub != 0) {
        if (ws->event_routine_sub == 1) {
            win_script_set(ws, SCRIPT_ROOF_PARTY_LEAVES_CENTRAL_TOWER);
            ws->event_routine_sub++;
            return BUILDING_RUNNING;
        }
        win_close_all(ws);
        return BUILDING_RUNNING;
    }

    switch (ws->event_routine) {
        case ROOF_ASK: {
            win_open(ws, WIN_SCRIPT_MESSAGE2);
            win_script_set(ws, SCRIPT_ROOF_MOTAS_ONLY_REMAINING_SPACESHIP);
            ws->event_routine++;
            break;
        }
        case ROOF_YESNO: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case ROOF_ANSWER: {
            if (ws->yes_no_input != 0) {
                ws->event_routine_sub++;
                break;
            }
            win_script_set(ws, SCRIPT_ROOF_NOW_LEAVING_DEZO);
            gd->level_index = LEVEL_DEZOLIS_SKURE;
            gd->level_y     = 0x4F0;
            gd->level_x     = 0x3C0;
            ws->event_routine++;
            break;
        }
        case ROOF_TO_SKURE: {
            bs->flight_fade = true;
            return BUILDING_FLIGHT;
        }
        case ROOF_TO_PASEO: {
            gd->level_index = LEVEL_PASEO;
            gd->level_y     = 0x100;
            gd->level_x     = 0x1F0;
            bs->flight_fade = false;
            return BUILDING_FLIGHT;
        }
        default: {
            win_close_all(ws);
            break;
        }
    }
    return BUILDING_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Building_GairaControlPanel and Building_TylerSpaceship             */
/* ------------------------------------------------------------------ */

/* loc_CC3E: demo_timer down if it is not 0 already. Its caller branches
 * on the Z flag the subq leaves, so the call that takes it to 0 is the
 * one that goes on -- $12C is 300 calls. */
static bool demo_timer_running(BuildingState *bs)
{
    if (bs->demo_timer == 0) {
        return false;
    }
    bs->demo_timer--;
    return bs->demo_timer != 0;
}

enum {
    PANEL_ALARM = 2,        /* loc_CBE8 */
    PANEL_WAIT,             /* loc_CC00 */
    PANEL_FADE,             /* loc_CC10 */
    PANEL_FALL              /* loc_CC26 */
};

/* Building_GairaControlPanel (ps2.asm:18668), from ObjType_Events $2F with
 * the Radar portrait ($1B). The alarm palette is game.c's
 * building_palette_cycle (loc_66F6).
 *   2  the big box, $1901; demo_timer = $12C
 *   3  300 frames, then SXFID_Explosion
 *   4  PaletteFadeFrom, $E0 (the music fades); demo_timer = $12C
 *   5  300 frames, then Tyler's spaceship, portrait $1C, and
 *      screen_changed_flag = -1: BuildingScreen again. */
static BuildingResult gaira_panel(BuildingState *bs, WinSys *ws)
{
    switch (ws->event_routine) {
        case PANEL_ALARM: {
            win_open(ws, WIN_SCRIPT_MESSAGE_BIG);
            win_script(ws, SCRIPT_MISC_OH_NO_SATELLITE_HEADED);
            ws->event_routine++;
            bs->demo_timer = 0x12C;
            break;
        }
        case PANEL_WAIT: {
            if (demo_timer_running(bs)) {
                break;
            }
            ws->event_routine++;
            sfx(ws, SFX_EXPLOSION);
            break;
        }
        case PANEL_FADE: {
            ws->event_routine++;
            bs->demo_timer = 0x12C;
            return BUILDING_FADE_OUT;
        }
        default: {
            if (demo_timer_running(bs)) {
                break;
            }
            bs->building = BUILDING_TYLER_SHIP;
            bs->portrait = PORTRAIT_TYLER_SHIP;
            return BUILDING_CHANGED;
        }
    }
    return BUILDING_RUNNING;
}

enum {
    TYLER_HELLO = 2,        /* loc_CC7C */
    TYLER_PICTURE_1,        /* loc_CC8E */
    TYLER_PICTURE_1_OFF,    /* loc_CCCC */
    TYLER_PICTURE_2,        /* loc_CCE6 */
    TYLER_PICTURE_2_OFF,    /* loc_CD2A */
    TYLER_MONITOR,          /* loc_CD50 */
    TYLER_PALMA,            /* loc_CDF8 */
    TYLER_EXPLOSION,        /* loc_CE20 */
    TYLER_AFTER,            /* loc_CE52 */
    TYLER_HOME              /* loc_CE5E */
};

/* TylerSpaceShip_EventFrames: a monitor frame (loc_CDDE) a step, 0 for
 * none, $FE for ObjID_MonitorExplosion into $E800, $FF the end. */
static const u8 TYLER_FRAMES[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x03, 0x02, 0x03, 0x02, 0x03, 0x02, 0x04, 0x02,
    0x04, 0xFF
};

/* loc_6BBE / loc_6C54 on d7 + 1 tiles at VRAM $0020, asked of the
 * building screen: the dissolve runs inside the building's call in the
 * ROM, so nothing else on the screen moves until it is done. */
static BuildingResult picture_fx(BuildingState *bs, int fx, int tiles,
                                 int art)
{
    bs->fx       = fx;
    bs->fx_tiles = tiles;
    bs->fx_art   = art;
    return BUILDING_PICTURE;
}

/* Building_TylerSpaceship (ps2.asm:18715). BuildingScreen's loc_8326
 * starts it at event_routine 2 with VRAM $0000-$3FFF cleared, palette
 * pal_id_tit_scr and no portrait. Every picture goes to plane A in the
 * dream's place ($410C0003: row 2, col 6) with its art at tile 1.
 *   2   the big box, $1902
 *   3   loc_70A32 (the dream's fight, 28x14) over loc_6D7BA, loc_6BBE
 *       on $FA tiles (256 frames); demo_timer = $12C
 *   4   300 frames, loc_6C54 on $FA tiles (32 frames)
 *   5   loc_70D42 (28x14) over loc_6F040, loc_6BBE on $96 tiles, then
 *       $1903; demo_timer = $12C
 *   6   300 frames, loc_6C54 on $96 tiles, $1904; demo_timer = $B4
 *   7   180 frames; palette $2A (lines 2-3) live and target, rows 2 and
 *       15 blanked, TylerPortraitTileInd 13x12 at row 3 col 6, monitor
 *       frame 0 (loc_CDDE, 15x12 at row 3 col 19), TylerSpaceshipArt by
 *       loc_6BBE on $B4 tiles, $1905; $F764 = 0, $F766 = 3
 *   8   $1907, monitor frame 1
 *   9   every fourth call the next TylerSpaceShip_EventFrames byte: a
 *       monitor frame, 0 nothing, $FE ObjID_MonitorExplosion, $FF on
 *   10  $1908
 *   11  loc_CE5E (gamedata_prison_release): every character's HP and TP
 *       full and the equipment and bag from $DA00 back; Paseo at x $3D0,
 *       y $170, on foot; the Governor (portrait $17) at the end of demo
 *       walk 8, game_screen = ScreenID_Level. */
static BuildingResult tyler_ship(BuildingState *bs, WinSys *ws, GameData *gd)
{
    switch (ws->event_routine) {
        case TYLER_HELLO: {
            win_open(ws, WIN_SCRIPT_MESSAGE_BIG);
            win_script(ws, SCRIPT_MISC_SAME_DREAM_AGAIN);
            ws->event_routine++;
            break;
        }
        case TYLER_PICTURE_1: {
            bs->picture = TYLER_VIEW_BATTLE;
            ws->event_routine++;
            bs->demo_timer = 0x12C;
            return picture_fx(bs, BUILDING_FX_IN, 0xF9 + 1, TYLER_VIEW_BATTLE);
        }
        case TYLER_PICTURE_1_OFF: {
            if (demo_timer_running(bs)) {
                break;
            }
            ws->event_routine++;
            return picture_fx(bs, BUILDING_FX_OUT, 0xF9 + 1, TYLER_VIEW_BATTLE);
        }
        case TYLER_PICTURE_2: {
            bs->picture = TYLER_VIEW_HELP;
            win_script(ws, SCRIPT_MISC_HELP);         /* after the dissolve, as there */
            ws->event_routine++;
            bs->demo_timer = 0x12C;
            return picture_fx(bs, BUILDING_FX_IN, 0x95 + 1, TYLER_VIEW_HELP);
        }
        case TYLER_PICTURE_2_OFF: {
            if (demo_timer_running(bs)) {
                break;
            }
            win_script(ws, SCRIPT_MISC_04);
            ws->event_routine++;
            bs->demo_timer = 0xB4;
            return picture_fx(bs, BUILDING_FX_OUT, 0x95 + 1, TYLER_VIEW_HELP);
        }
        case TYLER_MONITOR: {
            if (demo_timer_running(bs)) {
                break;
            }
            bs->palette_2a    = true;
            bs->picture       = TYLER_VIEW_MONITOR;
            bs->monitor_frame = 0;
            win_script(ws, SCRIPT_MISC_DID_DISTURB_IM_TYLER);
            ws->event_routine++;
            bs->store_max   = 0;            /* $F764: the frame step */
            bs->store_index = 3;            /* $F766: its wait */
            return picture_fx(bs, BUILDING_FX_IN, 0xB3 + 1,
                              TYLER_VIEW_MONITOR);
        }
        case TYLER_PALMA: {
            win_script(ws, SCRIPT_MISC_TERRIBLE_ALL_LEFT_PALM);
            ws->event_routine++;
            bs->monitor_frame = 1;
            break;
        }
        case TYLER_EXPLOSION: {
            if (--bs->store_index >= 0) {
                break;
            }
            bs->store_index = 3;
            int step = bs->store_max++;
            u8 frame = (step < (int)sizeof TYLER_FRAMES) ? TYLER_FRAMES[step]
                                                         : 0xFF;
            if (frame == 0) {
                break;
            }
            if (frame < 0x80) {
                bs->monitor_frame = frame;          /* bpl.s loc_CDDE */
            } else if (frame == 0xFF) {
                ws->event_routine++;
            } else {
                bs->spawn_explosion = true;         /* $FE */
            }
            break;
        }
        case TYLER_AFTER: {
            win_script(ws, SCRIPT_MISC_ONE_PLANET_DESTROYED_WHO);
            ws->event_routine++;
            break;
        }
        default: {
            gamedata_prison_release(gd);
            gd->level_index = LEVEL_PASEO;
            gd->level_y     = 0x170;
            gd->level_x     = 0x3D0;
            gd->jet_scooter = 0;
            bs->building    = BUILDING_GOVERNOR;
            bs->portrait    = PORTRAIT_GOVERNOR;
            bs->demo_flag   = true;
            bs->demo_index  = DEMO_INDEX_TYLER_SPC_TO_TOWER;
            bs->picture     = TYLER_VIEW_NONE;
            return BUILDING_TO_LEVEL;
        }
    }
    return BUILDING_RUNNING;
}


/* ------------------------------------------------------------------ */
/* Building_EsperMansion (ps2.asm:18940): Lutz                        */
/* ------------------------------------------------------------------ */

enum {
    ESP_WAKE = 2,       /* loc_CF0A */
    ESP_ASK,            /* loc_CF96 */
    ESP_ANSWER,         /* loc_CFA2 */
    ESP_TO_NOAH,        /* loc_CFC0 */
    ESP_TO_F1,          /* loc_CFD8 */
    ESP_HEAL            /* loc_CFF6 */
};

/* NeiEquipmentArray (19033): the eight pieces Lutz asks for. */
static const char *const NEI_EQUIPMENT[8] = {
    "Neimet", "NeiCrown", "NeiArmor", "NeiCape", "NeiShield", "NeiEmel",
    "NeiSlasher", "NeiShot"
};

/* Every state first tests event_routine_sub: set, CloseAllWindows. The
 * states, by event_routine:
 *   2  character_index = 0 and the big box. With $C743 = 2 (B1's scene
 *      just ended): $190B then $190E, event_flags = 0, $C78E = 0 (F1's
 *      chest slot becomes chest $0E, the Prism), and state 6. Otherwise,
 *      with $C744 clear: $168F, and if every piece of NeiEquipmentArray
 *      is in someone's bag, $C744 = 1, $1690 in its place and $C78F = 0
 *      (the slot's chest becomes $0F, the Nei Sword); done. With $C744
 *      set: $1913 and done -- or, holding the Nei Sword, $1911, state 3
 *      and loc_DDA2's character_index.
 *   3  WinID_YesNo2.   4  YES: $1695, state 5. NO: $1912, state 7.
 *   5  NoahGroundF, $200,$6B0; done.
 *   6  $C743 = 3, EsperMansionF1 at $100,$100; done.
 *   7  loc_B9D8, HP and TP full for the living, SXFID_Healed; done.
 * The disassembly notes that $1690 overruns sound RAM on the cartridge
 * (the music freeze); the port's scripts are not in that RAM. */
static BuildingResult esper_mansion(BuildingState *bs, WinSys *ws,
                                    GameData *gd)
{
    if (ws->event_routine_sub != 0) {
        win_close_all(ws);
        return BUILDING_RUNNING;
    }
    switch (ws->event_routine) {
        case ESP_WAKE: {
            win_open(ws, WIN_SCRIPT_MESSAGE_BIG);
            ws->character_index = 0;
            if (gd->c700[FLAG_ESPER_B1_TALKS] == 2) {
                win_script_set2(ws, SCRIPT_MISC_FINALLY_MADE_IM_LUTZ, SCRIPT_MISC_BUT_DOESNT_MEAN_THERE);
                ws->event_routine += 4;
                bs->clear_event_flags = true;
                gd->c700[FLAG_CHEST_PRISM] = 0;
                break;
            }
            if (gd->c700[FLAG_LUTZ_SAW_NEI_ITEMS] == 0) {
                win_script_set(ws, SCRIPT_PEOPLE_BEFORE_DARK_POWER_REIGNS);
                int have = 0;
                for (int i = 0; i < 8; i++) {
                    if (building_party_has_item(gd,
                            gamedata_item_id(NEI_EQUIPMENT[i]))) {
                        have++;
                    }
                }
                if (have == 8) {
                    gd->c700[FLAG_LUTZ_SAW_NEI_ITEMS] = 1;
                    win_script_set(ws, SCRIPT_PEOPLE_GOOD_JOB_TRULY_DESCENDANT);
                    gd->c700[FLAG_CHEST_NEI_SWORD] = 0;
                }
                ws->event_routine_sub++;
                break;
            }
            win_script_set(ws, SCRIPT_MISC_REMOVE_NEI_SWORD_FROM);
            if (!building_party_has_item(gd, gamedata_item_id("NeiSword"))) {
                ws->event_routine_sub++;
                break;
            }
            win_script_set(ws, SCRIPT_MISC_BRAVE_ENOUGH_DO_BATTLE);
            ws->event_routine++;
            /* loc_DDA2: Rolf while he stands, else the leader. */
            ws->character_index = 0;
            if (gd->chr[0].hp == 0) {
                for (int m = 0;
                     m <= gd->party_members_num && m < GD_PARTY_MAX; m++) {
                    ws->character_index = gd->party_member_id[m];
                    if (gd->chr[gd->party_member_id[m] & 7].hp != 0) {
                        break;
                    }
                }
            }
            break;
        }
        case ESP_ASK: {
            win_open(ws, WIN_YESNO2);
            ws->event_routine++;
            break;
        }
        case ESP_ANSWER: {
            if (ws->yes_no_input == 0) {
                win_script_set(ws, SCRIPT_PEOPLE_WILL_LEAVE_FUTURE_ALGO);
                ws->event_routine++;
                break;
            }
            win_script_set(ws, SCRIPT_MISC_CASE_ILL_SEND_WAY);
            ws->event_routine += 3;
            break;
        }
        case ESP_TO_NOAH: {
            gd->level_index = LEVEL_NOAH_GROUND_F;
            gd->level_y     = 0x6B0;
            gd->level_x     = 0x200;
            ws->event_routine_sub++;
            break;
        }
        case ESP_TO_F1: {
            gd->c700[FLAG_ESPER_B1_TALKS]  = 3;
            gd->level_index = LEVEL_ESPER_MANSION_F1;
            gd->level_y     = 0x100;
            gd->level_x     = 0x100;
            ws->event_routine_sub++;
            break;
        }
        default: {
            ws->event_routine_sub++;
            for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX;
                 m++) {
                CharData *c = party_char(gd, m);
                if (c->hp != 0) {
                    c->hp = c->max_hp;
                    c->tp = c->max_tp;
                }
            }
            if (ws->sound != NULL) {
                ws->sound(ws->ctx, SFX_HEALED);
            }
            break;
        }
    }
    return BUILDING_RUNNING;
}

BuildingResult building_tick(BuildingState *bs, WinSys *ws, GameData *gd,
                             const Input *in)
{
    if (!win_idle(ws)) {
        return BUILDING_RUNNING;
    }
    if (ws->event_routine == 0) {
        return BUILDING_TO_LEVEL;
    }
    if (ws->event_routine == 1) {
        win_open(ws, (u16)(WIN_PORTRAIT_START + bs->portrait));
        ws->event_routine++;
        return BUILDING_RUNNING;
    }

    switch (bs->building) {
        case BUILDING_ROLF_HOUSE_START: {
            return rolf_house_start(bs, ws, gd);
        }
        case BUILDING_GOVERNOR: {
            return governor(bs, ws, gd);
        }
        case BUILDING_ROLF_HOUSE: {
            return rolf_house(bs, ws, gd, in);
        }
        case BUILDING_WEAPON_STORE: {
            return equipment_store(bs, ws, gd, 0x0600, "weapon");
        }
        case BUILDING_ARMOR_STORE: {
            return equipment_store(bs, ws, gd, 0x0700, "armor");
        }
        case BUILDING_ITEM_STORE: {
            return item_store(bs, ws, gd);
        }
        case BUILDING_HOSPITAL: {
            return hospital(bs, ws, gd);
        }
        case BUILDING_CLONE_LAB: {
            return clone_lab(bs, ws, gd);
        }
        case BUILDING_DATA_MEMORY: {
            return data_memory(ws, gd);
        }
        case BUILDING_TELEPORT: {
            return teleport_station(ws, gd);
        }
        case BUILDING_TOWER_OUTSIDE: {
            return tower_outside(bs, ws, gd);
        }
        case BUILDING_TOWER_ROOM: {
            return tower_room(bs, ws, gd);
        }
        case BUILDING_LIBRARY: {
            return library(bs, ws, gd);
        }
        case BUILDING_TOWER_ROOF: {
            return roof(bs, ws, gd);
        }
        case BUILDING_USTVESTIA: {
            return ustvestia(ws, gd, in);
        }
        case BUILDING_INVENTOR: {
            return inventor_house(ws, gd);
        }
        case BUILDING_GAIRA_PANEL: {
            return gaira_panel(bs, ws);
        }
        case BUILDING_TYLER_SHIP: {
            return tyler_ship(bs, ws, gd);
        }
        case BUILDING_ESPER_MANSION: {
            return esper_mansion(bs, ws, gd);
        }
        default: {
            fprintf(stderr, "building: %d is not ported\n", bs->building);
            win_close_all(ws);
            return BUILDING_RUNNING;
        }
    }
}
