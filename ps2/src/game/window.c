#include "window.h"

#include "gamedata.h"
#include "lang.h"
#include "../core/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SFX_SELECTION 0xA0

#define TILE_FILL   0x26
#define TILE_TL     0xB8
#define TILE_TR     0xBA
#define TILE_LEFT   0xBB
#define TILE_RIGHT  0xBC
#define TILE_BL     0xBD
#define TILE_BR     0xBF

#define TILE_EQUIPPED    0xA3   /* loc_F948: the mark on a worn item   */
#define TILE_BORDER_TOP  0xB9

#define TILE_LAMP        0xB6   /* Obj_RedRectangleCursor: $85B6, 2x1 */
#define TILE_TRIANGLE    0x22   /* Map_Cursor_Triangle: $8522, 2x2    */
#define TILE_NAME_BLINK  0x26   /* Obj_InputWindowBlinkCursor: $8526  */
#define TILE_NAME_DEST   0x5F   /* Obj_NameDestinationTile: $855F     */

#define SPRITE_BIAS 128         /* VDP sprite space to screen */

/* ------------------------------------------------------------------ */
/* WindowArtLayoutPtrs (ps2.asm:28800), the entries ported so far.    */
/* A position word is a nametable byte offset: $80 per row, 2 per     */
/* column, with the VRAM-write bit $4000 on top.                      */
/* ------------------------------------------------------------------ */

typedef struct {
    int         id;
    u16         pos;
    const char *art;
    u8          w, h;      /* interior + 1, rows - 1 */
    WinMap      map;
    bool        loaded;
} WinDesc;

static WinDesc g_static_desc[] = {
    { WIN_PLAYER_MENU,        0x4082, "WinArt_PlayerMenu",       0x08, 0x0A,
      { 0 }, false },
    { WIN_MENU_ITEM_CHAR,     0x4488, "WinArt_CharList",         0x07, 0x0A,
      { 0 }, false },
    { WIN_MENU_ITEM_LIST,     0x409A, "WinArt_MenuItemList",     0x0E, 0x11,
      { 0 }, false },
    { WIN_ITEM_ACTION,        0x40BC, "WinArt_ItemAction",       0x06, 0x06,
      { 0 }, false },
    { WIN_CHOSEN_ITEM_CHAR,   0x44BE, "WinArt_CharList",         0x07, 0x0A,
      { 0 }, false },
    { WIN_STATE_ORDER,        0x4604, "WinArt_StateOrder",       0x08, 0x04,
      { 0 }, false },
    { WIN_CHAR_STATS_1,       0x471E, "WinArt_IndividualCharStats", 0x0B, 0x05,
      { 0 }, false },
    { WIN_CHAR_STATS_1 + 1,   0x4736, "WinArt_IndividualCharStats", 0x0B, 0x05,
      { 0 }, false },
    { WIN_CHAR_STATS_1 + 2,   0x4A1E, "WinArt_IndividualCharStats", 0x0B, 0x05,
      { 0 }, false },
    { WIN_CHAR_STATS_1 + 3,   0x4A36, "WinArt_IndividualCharStats", 0x0B, 0x05,
      { 0 }, false },
    { WIN_MENU_MESETA,        0x4904, "WinArt_Meseta",           0x0C, 0x02,
      { 0 }, false },
    { WIN_CHAR_ORDER_DEST,    0x4220, "WinArt_CharOrderDestination", 0x07, 0x09,
      { 0 }, false },
    { WIN_CHAR_LIST2,         0x43B8, "WinArt_CharList2",        0x07, 0x09,
      { 0 }, false },
    { WIN_LEVEL_TECH_LIST,    0x4098, "WinArt_LevelTechList",    0x08, 0x11,
      { 0 }, false },
    { WIN_LEVEL_TECH_LIST2,   0x411A, "WinArt_LevelTechList",    0x08, 0x11,
      { 0 }, false },
    { WIN_EQUIP_STATS,        0x49AE, "WinArt_EquipStats",       0x0C, 0x07,
      { 0 }, false },
    { WIN_EQP_EQUIP_LIST,     0x478C, "WinArt_StrngEquip",       0x10, 0x0B,
      { 0 }, false },
    { WIN_ITEM_LIST2,         0x40AE, "WinArt_MenuItemList",     0x0E, 0x11,
      { 0 }, false },
    { WIN_ITEM_LIST3,         0x4130, "WinArt_MenuItemList",     0x0E, 0x11,
      { 0 }, false },
    { WIN_RIGHT_LEFT,         0x449C, "WinArt_RightLeft",        0x07, 0x05,
      { 0 }, false },
    { WIN_MENU_ITEM_LIST2,    0x411C, "WinArt_MenuItemList",     0x0E, 0x11,
      { 0 }, false },
    { WIN_MENU_CHAR_STATS,    0x4A0C, "WinArt_MenuCharStats",    0x1C, 0x06,
      { 0 }, false },
    { WIN_SCRIPT_MESSAGE,     0x4A8E, "WinArt_ScriptMessage",    0x19, 0x05,
      { 0 }, false },
    { WIN_STRNG_HPTP,         0x40B6, "WinArt_StrngHPTP",        0x0B, 0x04,
      { 0 }, false },
    { WIN_STRNG_STATS,        0x4634, "WinArt_StrngStats",       0x0C, 0x0E,
      { 0 }, false },
    { WIN_STRNG_EQUIP,        0x478C, "WinArt_StrngEquip",       0x10, 0x0B,
      { 0 }, false },
    { WIN_STRNG_LVEXP,        0x4096, "WinArt_StrngLVEXP",       0x0B, 0x07,
      { 0 }, false },
    { WIN_STRNG_CHAR_LIST,    0x4586, "WinArt_CharList2",        0x07, 0x09,
      { 0 }, false },
    { WIN_YESNO,              0x45BC, "WinArt_YesNo",            0x06, 0x04,
      { 0 }, false },
    { WIN_SCRIPT_MESSAGE_BIG, 0x488E, "WinArt_ScriptMessageBig", 0x19, 0x09,
      { 0 }, false },
    { WIN_FULL_TECH_LIST,     0x4418, "WinArt_FullTechList",     0x0C, 0x11,
      { 0 }, false },
    { WIN_FULL_TECH_LIST2,    0x4434, "WinArt_FullTechList",     0x0C, 0x11,
      { 0 }, false },
    { WIN_YESNO2,             0x493E, "WinArt_YesNo",            0x06, 0x04,
      { 0 }, false },
    { WIN_YESNO3,             0x4620, "WinArt_YesNo",            0x06, 0x04,
      { 0 }, false },
    { WIN_SCRIPT_MESSAGE2,    0x4A04, "WinArt_ScriptMessage",    0x19, 0x05,
      { 0 }, false },
    { WIN_BUYSELL,            0x4AB8, "WinArt_BuySell",          0x07, 0x04,
      { 0 }, false },
    { WIN_STORE_MESETA,       0x41B4, "WinArt_Meseta",           0x0C, 0x02,
      { 0 }, false },
    { WIN_LIBRARY_OPTIONS,    0x41AE, "WinArt_LibraryOptions",   0x0D, 0x0B,
      { 0 }, false },
    { WIN_HEAL_CURE,          0x429A, "WinArt_HealCure",         0x0A, 0x05,
      { 0 }, false },
    { WIN_STORE_INVENTORY,    0x431A, "WinArt_StoreInventory",   0x13, 0x0D,
      { 0 }, false },
    { WIN_ROLF_HOUSE_OPTIONS, 0x4118, "WinArt_RolfHouseOptions", 0x0E, 0x06,
      { 0 }, false },
    { WIN_PROFILE_CHAR_LIST,  0x4292, "WinArt_ProfileCharList",  0x07, 0x11,
      { 0 }, false },
    { WIN_PROFILE_START + 0,  0x4784, "WinArt_RolfProfile",      0x17, 0x0B,
      { 0 }, false },
    { WIN_PROFILE_START + 1,  0x4784, "WinArt_NeiProfile",       0x17, 0x0B,
      { 0 }, false },
    { WIN_PROFILE_START + 2,  0x4784, "WinArt_RudoProfile",      0x17, 0x0B,
      { 0 }, false },
    { WIN_PROFILE_START + 3,  0x4784, "WinArt_AmyProfile",       0x17, 0x0B,
      { 0 }, false },
    { WIN_PROFILE_START + 4,  0x4784, "WinArt_HughProfile",      0x17, 0x0B,
      { 0 }, false },
    { WIN_PROFILE_START + 5,  0x4784, "WinArt_AnnaProfile",      0x17, 0x0B,
      { 0 }, false },
    { WIN_PROFILE_START + 6,  0x4784, "WinArt_KainProfile",      0x17, 0x0B,
      { 0 }, false },
    { WIN_PROFILE_START + 7,  0x4784, "WinArt_ShirProfile",      0x17, 0x0B,
      { 0 }, false },
    { WIN_REGROUP_CHAR_LIST,  0x4102, "WinArt_RegroupCharList",  0x07, 0x0D,
      { 0 }, false },
    { WIN_REGROUP_SELECTED,   0x4316, "WinArt_RegroupSelectedChar", 0x05, 0x09,
      { 0 }, false },
    { WIN_ROOM_OPTIONS,       0x4782, "WinArt_RoomOptions",      0x11, 0x05,
      { 0 }, false },
    { WIN_TOWER_OPTIONS,      0x4332, "WinArt_CentTowerOptions", 0x0B, 0x07,
      { 0 }, false },
    { WIN_TOWER_OPTIONS2,     0x4332, "WinArt_CentTowerOptions2", 0x0B, 0x09,
      { 0 }, false },
    { WIN_STORE_CHAR_LIST,    0x43B8, "WinArt_CharList2",        0x07, 0x09,
      { 0 }, false },
    { WIN_TELEPORT_PLACES,    0x43A4, "WinArt_TeleportPlaceNames", 0x08, 0x0B,
      { 0 }, false },
    { WIN_USTVESTIA_SONGS,    0x451A, "WinArt_UstvestiaSoundtracks", 0x0D, 0x03,
      { 0 }, false },
    { WIN_HOUSE_LVEXP,        0x4236, "WinArt_StrngLVEXP",       0x0B, 0x07,
      { 0 }, false },
    { WIN_NAME_INPUT,         0x409C, "WinArt_NameInput",        0x14, 0x10,
      { 0 }, false },
    { WIN_SAVE_SLOTS,         0x48B8, "WinArt_SaveSlots",        0x0A, 0x09,
      { 0 }, false },
    { WIN_GAME_SELECT,        0x418E, "WinArt_GameSelect",       0x12, 0x07,
      { 0 }, false },
    { WIN_ROLF_PORTRAIT2,     0x4236, "WinArt_RolfPortrait",     0x0B, 0x0B,
      { 0 }, false },
    /* The battle screen (ps2.asm:29280). The enemy boxes' $6xxx words
     * have bit 13 set: $2000 past plane A is plane B, where they go so
     * the enemy tiles rebuilt into plane A every frame cannot clear
     * them. */
    { WIN_BATTLE_CHAR_STATS_1,     0x4A92, "WinArt_BattleCharStats", 0x07, 0x05,
      { 0 }, false },
    { WIN_BATTLE_CHAR_STATS_1 + 1, 0x4AAE, "WinArt_BattleCharStats", 0x07, 0x05,
      { 0 }, false },
    { WIN_BATTLE_CHAR_STATS_1 + 2, 0x4A82, "WinArt_BattleCharStats", 0x07, 0x05,
      { 0 }, false },
    { WIN_BATTLE_CHAR_STATS_1 + 3, 0x4ABE, "WinArt_BattleCharStats", 0x07, 0x05,
      { 0 }, false },
    { WIN_BATTLE_OPTIONS,     0x4AA2, "WinArt_BattleOptions",    0x05, 0x05,
      { 0 }, false },
    { WIN_BATTLE_OPTIONS2,    0x4AA2, "WinArt_BattleOptions2",   0x05, 0x05,
      { 0 }, false },
    { WIN_BATTLE_CHAR_NAME,   0x48A2, "WinArt_BattleCharName",   0x05, 0x03,
      { 0 }, false },
    { WIN_BATTLE_COMMANDS,    0x4808, "WinArt_BattleCommands",   0x0C, 0x04,
      { 0 }, false },
    { WIN_BATTLE_MESSAGE,     0x4892, "WinArt_BattleMessage",    0x15, 0x03,
      { 0 }, false },
    { WIN_ENEMY_GROUPS,       0x47AE, "WinArt_EnemyGroups",      0x0D, 0x05,
      { 0 }, false },
    { WIN_BATTLE_TECH_LIST,   0x4790, "WinArt_BattleTechList",   0x08, 0x09,
      { 0 }, false },
    { WIN_BATTLE_ITEM_LIST,   0x4784, "WinArt_BattleItemList",   0x0E, 0x09,
      { 0 }, false },
    { WIN_BATTLE_ITEM_USED,   0x489C, "WinArt_BattleItemUsed",   0x0B, 0x03,
      { 0 }, false },
    { WIN_BATTLE_TECH_USED,   0x48A2, "WinArt_BattleTechUsed",   0x06, 0x03,
      { 0 }, false },
    { WIN_ENEMY_NAME_1,       0x6082, "WinArt_EnemyNames",       0x0B, 0x03,
      { 0 }, false },
    { WIN_ENEMY_NAME_2,       0x60AA, "WinArt_EnemyNames",       0x0B, 0x03,
      { 0 }, false },
    { WIN_ENEMY_INFO_1,       0x609A, "WinArt_EnemyInfo",        0x05, 0x03,
      { 0 }, false },
    { WIN_ENEMY_INFO_2,       0x60C2, "WinArt_EnemyInfo",        0x05, 0x03,
      { 0 }, false },
    { WIN_BATTLE_EMPTY_1,     0x4A92, "WinArt_BattleEmptySpots", 0x07, 0x05,
      { 0 }, false },
    { WIN_BATTLE_EMPTY_1 + 1, 0x4AAE, "WinArt_BattleEmptySpots", 0x07, 0x05,
      { 0 }, false },
    { WIN_BATTLE_EMPTY_1 + 2, 0x4A82, "WinArt_BattleEmptySpots", 0x07, 0x05,
      { 0 }, false },
    { WIN_BATTLE_EMPTY_1 + 3, 0x4ABE, "WinArt_BattleEmptySpots", 0x07, 0x05,
      { 0 }, false },
};

#define STATIC_DESC_COUNT \
    ((int)(sizeof g_static_desc / sizeof g_static_desc[0]))
#define DESC_MAX (STATIC_DESC_COUNT + WIN_PORTRAIT_COUNT)

/* The static entries, then one per portrait from
 * assets/data/portrait_windows.json (WinID_PortraitStart + n). */
static WinDesc g_desc[DESC_MAX];
static char    g_portrait_art[WIN_PORTRAIT_COUNT][40];
static int     g_desc_count;
#define DESC_COUNT g_desc_count

/* StoreEquipItemArray (ps2.asm:26269) from assets/data/stores.json: six
 * ids a store, indexed by the door's $FFFFF766 byte. */
#define STORE_MAX   32
#define STORE_ITEMS 6
static u8  g_store[STORE_MAX][STORE_ITEMS];
static int g_store_count;

static void load_stores(void)
{
    JsonValue *root = json_parse_file("assets/data/stores.json");
    const JsonValue *list = json_get(root, "stores");

    g_store_count = 0;
    for (const JsonValue *e = (list != NULL) ? list->child : NULL;
         e != NULL && g_store_count < STORE_MAX; e = e->next) {
        const JsonValue *items = json_get(e, "items");
        for (int i = 0; i < STORE_ITEMS; i++) {
            const JsonValue *it = json_at(items, i);
            g_store[g_store_count][i] =
                (u8)gamedata_item_id((it != NULL && it->type == JSON_STRING)
                                     ? it->string : "None");
        }
        g_store_count++;
    }
    json_free(root);
}

static u8 store_item(const WinSys *ws, int slot)
{
    if (ws->store_index >= g_store_count || slot < 0 || slot >= STORE_ITEMS) {
        return 0;
    }
    return g_store[ws->store_index][slot];
}

/* CheckIfDoublePrice (ps2.asm:19203): on Dezolis with Rolf in a Magic
 * Cap everything costs twice as much. */
u32 win_price(const WinSys *ws, int item)
{
    u32 price = gamedata_item_price(item);
    if (ws->gd != NULL && ws->gd->planet != 0
        && (ws->gd->chr[0].equip[EQUIP_HEAD] & 0x7F)
           == gamedata_item_id("MagicCap")) {
        price *= 2;
    }
    return price;
}

static WinDesc *find_desc(int id)
{
    for (int i = 0; i < DESC_COUNT; i++) {
        if (g_desc[i].id == id) {
            return &g_desc[i];
        }
    }
    return NULL;
}

static void gather_descriptors(void)
{
    g_desc_count = 0;
    for (int i = 0; i < STATIC_DESC_COUNT; i++) {
        g_desc[g_desc_count++] = g_static_desc[i];
    }

    JsonValue *list = json_parse_file("assets/data/portrait_windows.json");
    for (const JsonValue *e = (list != NULL) ? list->child : NULL;
         e != NULL && g_desc_count < DESC_MAX; e = e->next) {
        long n = json_int(e, "portrait", -1);
        if (n < 0 || n >= WIN_PORTRAIT_COUNT) {
            continue;
        }
        snprintf(g_portrait_art[n], sizeof g_portrait_art[n], "%s",
                 json_str(e, "art", ""));

        WinDesc *d = &g_desc[g_desc_count++];
        memset(d, 0, sizeof *d);
        d->id  = WIN_PORTRAIT_START + (int)n;
        d->pos = (u16)json_int(e, "pos", 0);
        d->art = g_portrait_art[n];
        d->w   = (u8)json_int(e, "w", 0);
        d->h   = (u8)json_int(e, "h", 0);
    }
    json_free(list);
}

bool win_load_assets(void)
{
    bool ok = true;

    gather_descriptors();
    load_stores();

    for (int i = 0; i < DESC_COUNT; i++) {
        char path[128];
        snprintf(path, sizeof path, "assets/winart/%s.tmx", g_desc[i].art);
        g_desc[i].loaded = winmap_load(&g_desc[i].map, path);

        if (g_desc[i].loaded
            && (g_desc[i].map.width != g_desc[i].w - 1
                || g_desc[i].map.height != g_desc[i].h + 1)) {
            /* Portraits $1C and $1D name a 6x6 battle map with an 11x11
             * descriptor in the ROM itself: BuildingScreen's special
             * cases draw those scenes another way. Not an asset error. */
            if (i < STATIC_DESC_COUNT) {
                fprintf(stderr, "window: %s is %dx%d, descriptor wants "
                                "%dx%d\n", g_desc[i].art,
                        g_desc[i].map.width, g_desc[i].map.height,
                        g_desc[i].w - 1, g_desc[i].h + 1);
            }
            g_desc[i].loaded = false;
        }
        if (i < STATIC_DESC_COUNT) {
            ok = ok && g_desc[i].loaded;
        }
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* Character maps                                                     */
/* ------------------------------------------------------------------ */

/* VDPCharacterMaps (ps2.asm:27449). Every entry's upper tile is $26 in
 * this ROM; the table has the pair so the lower one carries the glyph. */
void win_glyph(u8 code, u8 *upper, u8 *lower)
{
    *upper = TILE_FILL;

    if (code >= 1 && code <= 10) {
        *lower = (u8)(0x96 + code);          /* '0'..'9' -> $97..$A0 */
    } else if (code >= 11 && code <= 0x48) {
        *lower = (u8)(code + 28);            /* 'A' -> $27 ... -> $64 */
    } else if (code == 0x77) {
        *lower = 0x80;                       /* ':' */
    } else {
        *lower = TILE_FILL;
    }
}

/* The script charset (ps2.asm:25907). */
u8 win_charset(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (u8)(1 + (ch - '0'));
    }
    if (ch >= 'A' && ch <= 'Z') {
        return (u8)(11 + (ch - 'A'));
    }
    if (ch >= 'a' && ch <= 'z') {
        return (u8)(37 + (ch - 'a'));
    }

    switch (ch) {
        case ',': {
            return 0x3F;
        }
        case '.': {
            return 0x40;
        }
        case ';': {
            return 0x41;
        }
        case '"': {
            return 0x42;
        }
        case '?': {
            return 0x43;
        }
        case '!': {
            return 0x44;
        }
        case '\'': {
            return 0x45;
        }
        case '-': {
            return 0x46;
        }
        case ':': {
            return 0x77;
        }
        default: {
            return 0;
        }
    }
}

/* InputCharacterMap (ps2.asm:25922): 4 rows of 17. */
#define NAME_COLS  17
#define NAME_CELLS (NAME_COLS * 4)

/* (build 165) A language's board (names.json "board", the Japanese
 * cartridge's): its rows of cells as the art has them, 19 a row, rows 16
 * px apart from the art's row 5 -- the window the same, every cell a stop
 * but the blanks, the last row's last three words advance, back and end.
 * The cartridge's own board is the stock one, g_name_map's first
 * NAME_CELLS. */
#define BOARD_COLS  19
#define BOARD_ROWS  6
#define BOARD_CELLS (BOARD_COLS * BOARD_ROWS)
static bool g_board;

static u8 g_name_map[BOARD_CELLS];

static void build_name_map(void)
{
    static const char *const ROWS[3] = {
        "A B C D E F G H I",
        "J K L M N O P Q R",
        "S T U V W X Y Z  ",
    };

    memset(g_name_map, 0, sizeof g_name_map);
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < NAME_COLS; c++) {
            g_name_map[r * NAME_COLS + c] = win_charset(ROWS[r][c]);
        }
    }
    g_name_map[3 * NAME_COLS + 0]  = NAME_ADV;
    g_name_map[3 * NAME_COLS + 6]  = NAME_RUB;
    g_name_map[3 * NAME_COLS + 12] = TXT_END;
    g_board = false;
}

/* (build 165) The language's board into g_name_map, when it has one: each
 * cell its letter's code, a blank 0, and the last row's last three words
 * one stop each at their first cell -- NAME_ADV, NAME_RUB, TXT_END. */
static void build_board(void)
{
    if (lang_list_name("board", 0) == NULL) {
        build_name_map();
        return;
    }
    memset(g_name_map, 0, sizeof g_name_map);
    for (int r = 0; r < BOARD_ROWS; r++) {
        const char *p = lang_list_name("board", r);
        for (int c = 0; p != NULL && *p != '\0' && c < BOARD_COLS; c++) {
            u32 cp = lang_next_code_point(&p);
            g_name_map[r * BOARD_COLS + c] = (cp == ' ') ? 0
                                                         : lang_code_of(cp);
        }
    }
    /* The words: the groups of the last row, the last three. */
    int starts[BOARD_COLS];
    int n = 0;
    u8 *last = &g_name_map[(BOARD_ROWS - 1) * BOARD_COLS];
    for (int c = 0; c < BOARD_COLS; c++) {
        if (last[c] != 0 && (c == 0 || last[c - 1] == 0)) {
            starts[n++] = c;
        }
    }
    static const u8 WORD[3] = { NAME_ADV, NAME_RUB, TXT_END };
    for (int w = 0; w < 3 && n >= 3; w++) {
        int c = starts[n - 3 + w];
        last[c] = WORD[w];
        for (c++; c < BOARD_COLS && last[c] != 0; c++) {
            last[c] = 0;
        }
    }
    g_board = true;
}

/* ------------------------------------------------------------------ */
/* Queues                                                             */
/* ------------------------------------------------------------------ */

static void check_load_script(WinSys *ws);

void win_init(WinSys *ws)
{
    memset(ws, 0, sizeof *ws);
    ws->tile_base     = WIN_BASE_FONT;
    ws->end_timer     = 0x3C;
    ws->newline_delay = 3;
    build_name_map();
}

static void queue_push(u16 *q, u16 word)
{
    for (int i = 0; i < WIN_QUEUE; i++) {
        if (q[i] == 0) {
            q[i] = word;
            return;
        }
    }
}

static void queue_shift(u16 *q)
{
    memmove(&q[0], &q[1], WIN_QUEUE * sizeof q[0]);
    q[WIN_QUEUE] = 0;
}

void win_open(WinSys *ws, u16 word)
{
    queue_push(ws->window_index, word);
}

void win_script(WinSys *ws, u16 id)
{
    queue_push(ws->script_id, id);
}

void win_script2(WinSys *ws, u16 first, u16 second)
{
    win_script(ws, first);
    win_script(ws, second);
}

void win_script_set(WinSys *ws, u16 id)
{
    ws->script_id[0] = id;
}

void win_script_set2(WinSys *ws, u16 first, u16 second)
{
    ws->script_id[0] = first;
    ws->script_id[1] = second;
}

bool win_idle(const WinSys *ws)
{
    return ws->window_index[0] == 0
        && ws->window_index_saved == 0
        && ws->window_active_flag == 0;
}

static void play(WinSys *ws, u8 id)
{
    if (ws->sound != NULL) {
        ws->sound(ws->ctx, id);
    }
}

/* CloseCurrentWindow (ps2.asm:25430). The window that becomes current is
 * the one `word & 7` entries below the top. */
void win_close_current(WinSys *ws, u16 word)
{
    ws->window_index[0] = word;

    int below = ws->depth - 1 - (word & 7);
    ws->window_index_saved = (below >= 0) ? (u16)ws->stack[below].id : 0;

    if (ws->event_routine_sub != 0) {
        ws->event_routine_sub--;
    } else {
        ws->event_routine--;
    }
}

void win_clear_painted(WinSys *ws)
{
    ws->painted_count = 0;
}

/* The window the handler is running for: the top of the stack when it
 * is that window, otherwise the painted copy of it. */
static WinEntry *current_entry(WinSys *ws)
{
    int id = ws->window_index_saved & 0xFF;
    if (ws->depth > 0 && ws->stack[ws->depth - 1].id == id) {
        return &ws->stack[ws->depth - 1];
    }
    for (int i = 0; i < ws->painted_count; i++) {
        if (ws->painted[i].id == id) {
            return &ws->painted[i];
        }
    }
    return (ws->depth > 0) ? &ws->stack[ws->depth - 1] : NULL;
}

/* loc_94B0 with bit 2 of the word set: the window just drawn stays
 * where it is on the plane and the count is not bumped. The port keeps
 * it as a painted entry, one per id. */
static void paint_scratch(WinSys *ws)
{
    WinEntry *src = &ws->stack[ws->depth];

    /* The plane is one surface: whatever was drawn last is what shows.
     * So a repainted id goes to the END of the list, not back into its
     * old slot -- FIGHT/STGY reopened by B on ORDR/RUN has to cover
     * ORDR/RUN, which was painted after it. */
    for (int i = 0; i < ws->painted_count; i++) {
        if (ws->painted[i].id == src->id) {
            for (int k = i; k + 1 < ws->painted_count; k++) {
                ws->painted[k] = ws->painted[k + 1];
            }
            ws->painted_count--;
            break;
        }
    }
    if (ws->painted_count < WIN_PAINTED) {
        ws->painted[ws->painted_count] = *src;
        /* (build 142) Drawn now, over the windows open beneath it: the
         * ROM's loc_978C writes it into the nametable over theirs (the
         * Central Tower's copy portrait over the outside one, loc_C10A),
         * and a window opened after it draws over it in turn. */
        ws->painted[ws->painted_count].over = ws->depth;
        ws->painted_count++;
    }
}

/* (build 142) The stack has lost windows: a painted entry drawn over them
 * now sits on what is left, so one opened from here on draws over it.
 * (The ROM's close also puts back, inside the closed window's frame, what
 * was there before it opened -- which a painted entry drawn over it then
 * loses there. Not kept: none of the routes paints over a window that
 * then closes beneath it where the two meet -- the battle's boxes over its
 * command windows sit apart.) */
static void painted_settle(WinSys *ws)
{
    for (int i = 0; i < ws->painted_count; i++) {
        if (ws->painted[i].over > ws->depth) {
            ws->painted[i].over = ws->depth;
        }
    }
}

static void triangle_place(WinSys *ws, WinTriangle *t);

void win_spawn_triangle(WinSys *ws, TriKind kind, int slot, int max, int x,
                        int y)
{
    WinTriangle *t = NULL;
    for (int i = 0; i < 2; i++) {
        if (ws->tri[i].kind != TRI_NONE && ws->tri[i].slot == slot) {
            t = &ws->tri[i];        /* the slot is taken over */
        }
    }
    for (int i = 0; t == NULL && i < 2; i++) {
        if (ws->tri[i].kind == TRI_NONE) {
            t = &ws->tri[i];
        }
    }
    if (t == NULL) {
        t = &ws->tri[0];
    }
    memset(t, 0, sizeof *t);
    t->kind   = kind;
    t->slot   = slot;
    t->index  = 0;
    t->max    = (u8)max;
    t->base_x = (s16)x;
    t->x      = (s16)x;
    t->y      = (s16)y;
    t->blink  = 0;
    t->repeat = 0x1F;

    /* ObjTCCSel_Init (bsr loc_18E4) and ObjTCCS_Init place the cursor
     * before its first frame, so it never shows at the raw x. */
    triangle_place(ws, t);
}

void win_kill_triangle(WinSys *ws, int slot)
{
    for (int i = 0; i < 2; i++) {
        if (ws->tri[i].kind != TRI_NONE && ws->tri[i].slot == slot) {
            ws->tri[i].kind = TRI_NONE;
        }
    }
}

void win_plain_text(WinSys *ws, u16 script, int col, int row, int max_line)
{
    if (ws->depth >= WIN_STACK) {
        return;
    }
    WinEntry *e = &ws->stack[ws->depth];
    memset(e, 0, sizeof *e);
    e->id        = 0xFF;
    e->col       = col - 1;         /* interior column 0 is frame column 1 */
    e->row       = row - 1;
    e->w         = WIN_TEXT_COLS + 1;
    /* The text's lines are a row apart, the last one's lower half on row
     * max_line + 2; the ending's pages ask for twelve lines, which the
     * old max_line * 2 + 4 made 28 rows of a WIN_MAX_ROWS array. */
    e->rows      = max_line * 2 + 4;
    if (e->rows > WIN_MAX_ROWS) {
        e->rows = WIN_MAX_ROWS;
    }
    e->base      = WIN_BASE_FONT;
    e->layer     = LAYER_A;
    e->frameless = true;
    ws->depth++;
    ws->text_target   = ws->depth - 1;
    ws->text_col      = 0;
    ws->text_line     = 0;
    ws->text_max_line = max_line;
    ws->newline_delay = 3;
    ws->end_timer     = 0x3C;
    win_script_set(ws, script);
    check_load_script(ws);
}

void win_drop_plain_text(WinSys *ws)
{
    while (ws->depth > 0 && ws->stack[ws->depth - 1].frameless) {
        ws->depth--;
    }
    painted_settle(ws);
    if (ws->text_target >= ws->depth) {
        ws->text_target = -1;
    }
}

void win_close_all(WinSys *ws)
{
    ws->window_index_saved = 0;
    ws->event_routine       = 0;
    ws->event_routine_sub   = 0;
    ws->event_routine_sub_2 = 0;
    ws->event_step = 0;
    ws->event_done = 0;
    ws->window_index[0] = WIN_CLOSE(ws->depth);
}

/* ------------------------------------------------------------------ */
/* LoadScript                                                         */
/* ------------------------------------------------------------------ */

static void text_put(WinSys *ws, int *n, u8 byte)
{
    if (*n < WIN_TEXT_BUF - 1) {
        ws->text[(*n)++] = byte;
        return;
    }
    static bool told;
    if (!told) {
        told = true;
        fprintf(stderr, "window: script text over %d bytes, cut short\n",
                WIN_TEXT_BUF - 1);
    }
}

/* Script_ProcessCharNames: up to WIN_NAME_LEN codes, stopping at $C4;
 * (build 163) a proportional language's six (GD_NAME_MAX), its names
 * being up to six letters, as his long_names. */
static void text_put_name(WinSys *ws, int *n, const u8 *name)
{
    if (name == NULL) {
        return;
    }
    int max = lang_layout("long_names") ? GD_NAME_MAX : WIN_NAME_LEN;
    for (int i = 0; i < max && name[i] != TXT_END; i++) {
        text_put(ws, n, name[i]);
    }
}

/* (build 163) A language's name, whole, in its own letters (lang_codes):
 * his names are measured in pixels, not cells. */
static void text_put_lang(WinSys *ws, int *n, const char *text)
{
    u8  codes[WIN_RUN_LEN];
    int k = lang_codes(text, codes, WIN_RUN_LEN);
    for (int i = 0; i < k; i++) {
        text_put(ws, n, codes[i]);
    }
}

/* LoadScript's $BB: character_index names the character, unless it is 8
 * or more, when the first nonzero party_member_id does. */
static const u8 *char_name_for(const WinSys *ws)
{
    const GameData *gd = ws->gd;
    if (gd == NULL) {
        return ws->char_name;
    }
    int c = ws->character_index;
    if (c >= GD_CHARS) {
        c = 0;
        for (int i = 0; i < GD_PARTY_MAX; i++) {
            if (gd->party_member_id[i] != 0) {
                c = gd->party_member_id[i] % GD_CHARS;
                break;
            }
        }
    }
    return gd->chr[c].name;
}

/* Script_ProcessItemEnemyNames: ten codes, stopping at $C4 -- which is
 * where the name's ASCII ends. */
/* {char2}: character_index_2's name, the way {char} takes
 * character_index's. */
static const u8 *char2_name_for(const WinSys *ws)
{
    if (ws->gd == NULL) {
        return ws->char2_name;
    }
    return ws->gd->chr[ws->character_index_2 % GD_CHARS].name;
}

/* Script_ProcessItemEnemyNames for $BD: EnemyNames[enemy_index], ten
 * codes stopping at $C4. */
static void text_put_enemy(WinSys *ws, int *n)
{
    const char *text = (ws->enemy_text != NULL)
                     ? ws->enemy_text(ws->ctx, ws->enemy_index) : NULL;
    if (text != NULL) {
        text_put_lang(ws, n, text);
        return;
    }
    const u8 *name = (ws->enemy_name_lookup != NULL)
                   ? ws->enemy_name_lookup(ws->ctx, ws->enemy_index) : NULL;
    for (int i = 0; name != NULL && i < 10 && name[i] != TXT_END; i++) {
        text_put(ws, n, name[i]);
    }
}

/* Script_ProcessTechNames for $BE: five codes of technique_index's name. */
static void text_put_tech(WinSys *ws, int *n)
{
    const char *text = gamedata_tech_text(ws->technique_index & 0x3F);
    if (text != NULL) {
        text_put_lang(ws, n, text);
        return;
    }
    const char *name = gamedata_tech_name(ws->technique_index & 0x3F);
    for (int i = 0; i < 5 && name[i] != '\0'; i++) {
        text_put(ws, n, win_charset(name[i]));
    }
}

static void text_put_item(WinSys *ws, int *n)
{
    const char *text = gamedata_item_text(ws->item_index);
    if (text != NULL) {
        text_put_lang(ws, n, text);
        return;
    }
    const char *name = gamedata_item_name(ws->item_index);
    for (int i = 0; i < 10 && name[i] != '\0'; i++) {
        text_put(ws, n, win_charset(name[i]));
    }
}

/* LoadDigitInRAMLoop: six places, leading zeros dropped rather than
 * padded, the units digit always written. */
static void text_put_meseta(WinSys *ws, int *n, u32 value)
{
    static const u32 PLACE[6] = { 100000, 10000, 1000, 100, 10, 1 };
    bool started = false;

    for (int i = 0; i < 6; i++) {
        int digit = 0;
        while (value >= PLACE[i]) {
            value -= PLACE[i];
            digit++;
        }
        if (i == 5 || digit != 0) {
            started = true;
        }
        if (started) {
            text_put(ws, n, (u8)(digit + 1));
        }
    }
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

/* The extracted text back to the byte stream LoadScript walks. Returns
 * the token's code, or -1, and advances *pp past the closing brace. */
static int read_token(const char **pp)
{
    static const struct {
        const char *name;
        int         code;
    } TOKENS[] = {
        { "char",   TXT_CHAR },   { "char2", TXT_CHAR2 },
        { "enemy",  0xBD },       { "tech",  0xBE },
        { "item",   TXT_ITEM },   { "meseta", TXT_MESETA },
        { "cont",   TXT_CONT },   { "pause", TXT_PAUSE },
    };

    const char *p   = *pp + 1;
    const char *end = strchr(p, '}');
    if (end == NULL) {
        return -1;
    }

    size_t len  = (size_t)(end - p);
    int    code = -1;

    if (len == 6 && strncmp(p, "end:", 4) == 0) {
        code = hex_digit(p[4]) * 16 + hex_digit(p[5]);
    } else if (len == 2 && hex_digit(p[0]) >= 0 && hex_digit(p[1]) >= 0) {
        code = hex_digit(p[0]) * 16 + hex_digit(p[1]);
    } else {
        for (size_t i = 0; i < sizeof TOKENS / sizeof TOKENS[0]; i++) {
            if (strlen(TOKENS[i].name) == len
                && strncmp(p, TOKENS[i].name, len) == 0) {
                code = TOKENS[i].code;
            }
        }
    }

    *pp = end + 1;
    return code;
}

static bool next_is_cont(const char *p)
{
    while (*p == '\n') {
        p++;
    }
    return strncmp(p, "{cont}", 6) == 0;
}

/* ------------------------------------------------------------------ */
/* (build 168f) The text check                                        */
/* ------------------------------------------------------------------ */

/* With PS2_TEXT_CHECK set, what a language's text gets wrong on the
 * screen is said on stderr, each once ("text check: ..."): a line of a
 * message, a name or a label wider than the room it is drawn in (a
 * proportional run is cut at the window's edge; the stock text, a code a
 * cell, is not checked), and a message the game asks for that is empty in
 * the language. The playthroughs in each language read them
 * (tools/playthrough.py --language). Nothing the game does changes. */
static int g_text_check = -1;

static bool text_check(void)
{
    if (g_text_check < 0) {
        g_text_check = (getenv("PS2_TEXT_CHECK") != NULL) ? 1 : 0;
    }
    return g_text_check != 0;
}

/* True the first time `key` is seen. */
static bool text_check_new(u32 key)
{
    static u32 seen[1024];
    static int count;
    for (int i = 0; i < count; i++) {
        if (seen[i] == key) {
            return false;
        }
    }
    if (count < (int)(sizeof seen / sizeof seen[0])) {
        seen[count++] = key;
    }
    return true;
}

/* A key for codes drawn at a place: FNV-1a over both. */
static u32 text_check_key(u32 place, const u8 *codes, int n)
{
    u32 h = 2166136261u ^ place;
    for (int i = 0; i < n; i++) {
        h = (h ^ codes[i]) * 16777619u;
    }
    return h;
}

static const char *text_check_language(void)
{
    const char *key = lang_key(lang_current());
    return (key != NULL) ? key : "?";
}

/* The text window's script, for the run check's line. */
static u16 g_text_check_script;

/* A glyph with ink, in either of its halves. */
static bool text_check_inked(u8 code)
{
    const Tile *g[2] = { lang_glyph(code), lang_glyph_upper(code) };
    for (int i = 0; i < 2; i++) {
        for (int p = 0; g[i] != NULL && p < TILE_W * TILE_H; p++) {
            if (g[i]->px[p] != 0) {
                return true;
            }
        }
    }
    return false;
}

/* A run wider than the cells draw_runs gives it -- its own, cut at the
 * window's right edge: lang.c's draw_glyphs drops the first glyph whose
 * ink would cross the end and every glyph after it. Said when what is
 * dropped has ink (a space past the end is not), once for its place and
 * the glyphs up to the first dropped, so a line still being typed is said
 * once. */
static void text_check_run(const WinEntry *e, const WinRun *r)
{
    if (r->len == 0 || r->row >= e->rows) {
        return;
    }
    int cells = r->cells;
    if (r->col + cells > e->w - 1) {
        cells = e->w - 1 - r->col;
    }
    int room = cells * TILE_W;
    int x = 0;
    int k = 0;
    while (k < r->len && x + lang_advance(r->code[k]) - 1 <= room) {
        x += lang_advance(r->code[k]);
        k++;
    }
    bool inked = false;
    for (int i = k; i < r->len && !inked; i++) {
        inked = text_check_inked(r->code[i]);
    }
    if (!inked) {
        return;
    }
    u32 place = ((u32)e->id << 16) | ((u32)r->row << 8) | r->col;
    if (!text_check_new(text_check_key(place, r->code, k + 1))) {
        return;
    }
    fprintf(stderr, "text check: %s, window $%02X row %d col %d: glyph %d "
                    "of %d would end at %d px, the room is %d (the text's "
                    "script $%04X)\n", text_check_language(), e->id, r->row,
            r->col, k + 1, r->len, x + lang_advance(r->code[k]) - 1, room,
            g_text_check_script);
}

/* Every run of every window shown and painted, once a tick (win_process):
 * the probe draws only for a shot, so the check cannot wait for a draw. */
static void text_check_windows(const WinSys *ws)
{
    if (!lang_proportional()) {
        return;
    }
    g_text_check_script = ws->text_script;
    int shown = ws->depth + (ws->anim_drawn ? 1 : 0);
    for (int i = 0; i < shown && i < WIN_STACK; i++) {
        for (int j = 0; j < ws->stack[i].runs; j++) {
            text_check_run(&ws->stack[i], &ws->stack[i].run[j]);
        }
    }
    for (int i = 0; i < ws->painted_count; i++) {
        for (int j = 0; j < ws->painted[i].runs; j++) {
            text_check_run(&ws->painted[i], &ws->painted[i].run[j]);
        }
    }
}

/* A message asked for whose first entry is empty in the language: it
 * shows the entries after it, or nothing. */
static void text_check_empty(const WinSys *ws, u16 id)
{
    const char *text = (ws->script_text != NULL)
                     ? ws->script_text(ws->ctx, id) : NULL;
    if (text != NULL && text[0] == '\0'
        && text_check_new(0x10000u | id)) {
        fprintf(stderr, "text check: %s, script $%04X asked for is empty\n",
                text_check_language(), id);
    }
}

/* LoadScript's copy of script `id` into the text buffer, run-ons and all,
 * through `look`; `start` raises window_active_flag at its end, as the
 * ROM's does. (build 166) Split from load_script for the language's
 * change, which copies the script on the window again in the new
 * language (win_language_changed): through script_text, which logs
 * nothing, and with the flag left as the event code knows it. */
static void expand_script(WinSys *ws, u16 id,
                          const char *(*look)(void *ctx, u16 id), bool start)
{
    int n = 0;
    ws->text_pos = 0;

    const char *src = (look != NULL) ? look(ws->ctx, id) : NULL;
    if (src == NULL) {
        return;
    }

    /* A script that opens anywhere but the home position starts on a new
     * line, unless it clears the window itself. */
    if (strncmp(src, "{cont}", 6) != 0
        && (ws->text_col != 0 || ws->text_line != 0)) {
        text_put(ws, &n, TXT_NEWLINE);
        text_put(ws, &n, TXT_NEWLINE);
    }

    for (;;) {
        const char *p = src;

        while (*p != '\0') {
            if (*p == '\n') {
                /* At the end of an entry exactly one newline is the
                 * file's own -- the blank line before the next [n] -- and
                 * any others are real $C1s. It matters when a script has
                 * no terminator and runs on into the next one: Governor
                 * $1102 ends "...Mother Brain." and a line break. */
                const char *q = p;
                while (*q == '\n') {
                    q++;
                }
                if (*q == '\0') {
                    for (const char *r = p + 1; r < q; r++) {
                        text_put(ws, &n, TXT_NEWLINE);
                        text_put(ws, &n, TXT_NEWLINE);
                    }
                    break;
                }
                text_put(ws, &n, TXT_NEWLINE);
                text_put(ws, &n, TXT_NEWLINE);
                p++;
                continue;
            }

            if (*p != '{') {
                if (lang_charset_full()) {
                    /* (build 165) Every character the language's own code
                     * (the Japanese cartridge's), the file being UTF-8. */
                    text_put(ws, &n, lang_code_of(lang_next_code_point(&p)));
                    continue;
                }
                text_put(ws, &n, win_charset(*p));
                p++;
                continue;
            }

            int code = read_token(&p);
            if (code < 0) {
                continue;
            }

            if (code == TXT_CHAR) {
                text_put_name(ws, &n, char_name_for(ws));
            } else if (code == TXT_CHAR2) {
                text_put_name(ws, &n, char2_name_for(ws));
            } else if (code == TXT_ITEM) {
                text_put_item(ws, &n);
            } else if (code == 0xBD) {
                text_put_enemy(ws, &n);
            } else if (code == 0xBE) {
                text_put_tech(ws, &n);
            } else if (code == TXT_MESETA) {
                text_put_meseta(ws, &n, ws->meseta_value);
            } else if (code == TXT_PAUSE) {
                text_put(ws, &n, TXT_PAUSE);
                if (!next_is_cont(p)) {
                    text_put(ws, &n, TXT_NEWLINE);
                    text_put(ws, &n, TXT_NEWLINE);
                }
            } else if (code >= TXT_END) {
                text_put(ws, &n, (u8)code);
                ws->text[n] = 0;
                if (start) {
                    ws->window_active_flag = 1;
                }
                return;
            } else if (code < TXT_CHAR || code == TXT_CONT) {
                text_put(ws, &n, (u8)code);
            }
        }

        /* No terminator: the ROM's scripts are contiguous and LoadScript
         * simply keeps copying into the next one. */
        id++;
        src = look(ws->ctx, id);
        if (src == NULL) {
            text_put(ws, &n, TXT_END);
            if (start) {
                ws->window_active_flag = 1;
            }
            return;
        }
    }
}

static void load_script(WinSys *ws, u16 id)
{
    queue_shift(ws->script_id);
    ws->text_script = id;               /* (build 166) */
    if (text_check()) {
        text_check_empty(ws, id);       /* (build 168f) */
    }
    expand_script(ws, id, ws->script_lookup, true);
}

static void check_load_script(WinSys *ws)
{
    if (ws->window_active_flag == 0 && ws->script_id[0] != 0) {
        load_script(ws, ws->script_id[0]);
    }
}

/* ------------------------------------------------------------------ */
/* RunScript -- the VBlank half of text                               */
/* ------------------------------------------------------------------ */

static WinEntry *text_entry(WinSys *ws)
{
    if (ws->text_target < 0 || ws->text_target >= WIN_STACK) {
        return NULL;
    }
    return &ws->stack[ws->text_target];
}

/* (build 163) The run on interior row `row` at column `col`, made when
 * there is none (and room for one): a proportional language's line. */
static WinRun *run_at(WinEntry *e, int row, int col, int cells)
{
    for (int i = 0; i < e->runs; i++) {
        if (e->run[i].row == row && e->run[i].col == col) {
            return &e->run[i];
        }
    }
    if (e->runs >= WIN_RUNS) {
        return NULL;
    }
    WinRun *r = &e->run[e->runs++];
    memset(r, 0, sizeof *r);
    r->row   = (u8)row;
    r->col   = (u8)col;
    r->cells = (u8)cells;
    return r;
}

/* (build 163) The runs on interior row `row` gone with its cells. */
static void drop_runs(WinEntry *e, int row)
{
    int k = 0;
    for (int i = 0; i < e->runs; i++) {
        if (e->run[i].row != row) {
            e->run[k++] = e->run[i];
        }
    }
    e->runs = k;
}

/* (build 164) `codes` as the run at (row, col), `cells` wide, in place of
 * whatever run was there, and those cells of the row blank (the paper):
 * a name or a label in a proportional language. None drops it. */
static void put_run_codes(WinEntry *e, int row, int col, int cells,
                          const u8 *codes, int n)
{
    if (row < 0 || row >= WIN_MAX_ROWS || col < 0 || cells <= 0) {
        return;
    }
    if (col + cells > WIN_MAX_COLS) {
        cells = WIN_MAX_COLS - col;
    }
    memset(&e->cell[row][col], TILE_FILL, (size_t)cells);
    for (int i = 0; i < e->runs; i++) {
        if (e->run[i].row == row && e->run[i].col == col) {
            e->run[i] = e->run[--e->runs];
            break;
        }
    }
    if (n <= 0) {
        return;
    }
    WinRun *r = run_at(e, row, col, cells);
    if (r != NULL) {
        r->len = (u8)((n < WIN_RUN_LEN) ? n : WIN_RUN_LEN);
        memcpy(r->code, codes, r->len);
    }
}

/* (build 164) The same from a language's text (lang_codes). */
static void put_run(WinEntry *e, int row, int col, int cells,
                    const char *text)
{
    u8  codes[WIN_RUN_LEN];
    int n = (text != NULL) ? lang_codes(text, codes, WIN_RUN_LEN) : 0;
    put_run_codes(e, row, col, cells, codes, n);
}

/* (build 164) A name or label the stock way writes as glyph halves --
 * upper on `row`, lower on row + 1 -- as a run on the lower row, the
 * upper row's cells blank. */
static void put_run2(WinEntry *e, int row, int col, int cells,
                     const char *text)
{
    if (row >= 0 && row < WIN_MAX_ROWS && col >= 0
        && col + cells <= WIN_MAX_COLS) {
        memset(&e->cell[row][col], TILE_FILL, (size_t)cells);
    }
    put_run(e, row + 1, col, cells, text);
}

/* (build 164) A party name (codes, up to `max`, $C4 ending a shorter one)
 * in a proportional language: a run on the lower row over the stock four
 * cells -- his names are 32 px at most, as his long_names plate. */
static void put_name_run(WinEntry *e, int row, int col, const u8 *name,
                         int max)
{
    u8  codes[GD_NAME_MAX];
    int n = 0;
    while (n < max && n < GD_NAME_MAX && name[n] != TXT_END) {
        codes[n] = name[n];
        n++;
    }
    memset(&e->cell[row][col], TILE_FILL, WIN_NAME_LEN);
    put_run_codes(e, row + 1, col, WIN_NAME_LEN, codes, n);
}

static void blank_text_row(WinEntry *e, int row, int cols)
{
    if (row >= 0 && row < WIN_MAX_ROWS) {
        memset(e->cell[row], TILE_FILL, (size_t)cols);
        drop_runs(e, row);
    }
}

/* loc_96AE */
static void text_clear(WinSys *ws)
{
    WinEntry *e = text_entry(ws);
    int cols = (ws->text_max_line == 0 && ws->battle_box_lines == 0)
             ? 20 : WIN_TEXT_COLS;

    if (e != NULL) {
        for (int r = 0; r < ws->text_max_line + 2; r++) {
            blank_text_row(e, 1 + r, cols);
        }
    }
    ws->text_line = 0;
}

/* loc_9666: everything moves up one tile row, the last row is blanked. */
static void text_scroll(WinSys *ws)
{
    if (ws->text_max_line == 0) {
        text_clear(ws);
        return;
    }

    WinEntry *e = text_entry(ws);
    if (e == NULL) {
        return;
    }
    for (int r = 0; r <= ws->text_max_line; r++) {
        memcpy(e->cell[1 + r], e->cell[2 + r], WIN_TEXT_COLS);
    }
    /* (build 163) A line's run moves with its cells; row 1's is
     * written over. */
    drop_runs(e, 1);
    for (int i = 0; i < e->runs; i++) {
        if (e->run[i].row >= 2 && e->run[i].row <= ws->text_max_line + 2) {
            e->run[i].row--;
        }
    }
    blank_text_row(e, 1 + ws->text_max_line + 1, WIN_TEXT_COLS);
}

static void run_script_step(WinSys *ws)
{
    u8 code = ws->text[ws->text_pos];

    if (code == TXT_NEWLINE) {
        /* A line break costs four frames; the byte is not consumed until
         * the counter runs out. */
        ws->newline_delay--;
        if (ws->newline_delay >= 0) {
            return;
        }
        ws->newline_delay = 3;

        if (ws->text_line == ws->text_max_line) {
            text_scroll(ws);
        } else {
            ws->text_line++;
        }
        ws->text_col = 0;
        ws->text_pos++;
        return;
    }

    if (code == TXT_CONT) {
        text_clear(ws);
        ws->text_col = 0;
        ws->text_pos++;
        return;
    }

    if (code >= TXT_PAUSE) {
        ws->fast = false;
        return;
    }

    WinEntry *e = text_entry(ws);
    if (e != NULL && lang_proportional()) {
        /* (build 163) A proportional language types the line's run a
         * glyph a frame, as the stock text types a cell a frame; the
         * upper (dakuten) row stays paper, as in his engine. */
        if (2 + ws->text_line < WIN_MAX_ROWS) {
            int cols = (ws->text_max_line == 0 && ws->battle_box_lines == 0)
                     ? 20 : WIN_TEXT_COLS;
            WinRun *r = run_at(e, 2 + ws->text_line, 0, cols);
            if (r != NULL && r->len < WIN_RUN_LEN) {
                r->code[r->len++] = code;
            }
        }
    } else if (e != NULL && ws->text_col < WIN_MAX_COLS
               && 2 + ws->text_line < WIN_MAX_ROWS) {
        u8 upper, lower;
        win_glyph(code, &upper, &lower);
        e->cell[1 + ws->text_line][ws->text_col] = upper;
        e->cell[2 + ws->text_line][ws->text_col] = lower;
    }
    ws->text_col++;
    ws->text_pos++;
}

/* CheckRunScript: one character per field, or the rest of the page at
 * once after a button. */
static void check_run_script(WinSys *ws)
{
    if (ws->window_active_flag == 0) {
        return;
    }
    do {
        run_script_step(ws);
    } while (ws->fast);
}

/* ------------------------------------------------------------------ */
/* ProcessWindows                                                     */
/* ------------------------------------------------------------------ */

static void fill_save_slots(WinSys *ws, WinEntry *e)
{
    for (int slot = 0; slot < 4; slot++) {
        WinSlotInfo info;
        memset(&info, 0, sizeof info);
        if (ws->slot_info != NULL) {
            ws->slot_info(ws->ctx, slot, &info);
        }

        /* Win_SaveSlots: name tiles from map offset $C -- row 1, column
         * 3 -- upper row then lower row, the level after the lower. */
        int top = 1 + slot * 2;
        if (lang_proportional()) {
            put_name_run(e, top, 3, info.name, WIN_NAME_MAX);
        }
        for (int i = 0; i < WIN_NAME_LEN && !lang_proportional(); i++) {
            u8 upper, lower;
            win_glyph(info.name[i] == TXT_END ? 0 : info.name[i],
                      &upper, &lower);
            e->cell[top][3 + i]     = upper;
            e->cell[top + 1][3 + i] = lower;
        }

        /* loc_11364: two digits, a blank for a leading zero. */
        int tens = (info.level / 10) % 10;
        int ones = info.level % 10;
        e->cell[top + 1][3 + WIN_NAME_LEN]     =
            (tens != 0) ? (u8)(0x97 + tens) : TILE_FILL;
        e->cell[top + 1][3 + WIN_NAME_LEN + 1] = (u8)(0x97 + ones);
    }
}

/* StartDecimalConversion2: `digits` places right-aligned at (col, row),
 * leading zeros as fill, the units digit always drawn. */
static void put_number(WinEntry *e, int row, int col, u32 value, int digits)
{
    static const u32 PLACE[8] = { 10000000, 1000000, 100000, 10000, 1000,
                                  100, 10, 1 };
    bool started = false;

    for (int i = 8 - digits; i < 8; i++) {
        int d = 0;
        while (value >= PLACE[i]) {
            value -= PLACE[i];
            d++;
        }
        if (i == 7 || d != 0) {
            started = true;
        }
        e->cell[row][col++] = started ? (u8)(0x97 + d) : TILE_FILL;
    }
}

/* (build 164) A label of the tile charset's two halves ("    Who?": four
 * upper, four lower; "  LV" two and two) as the text drawn: its lower half
 * when the upper is blank, else the whole (his "To whom?" is one run).
 * `stock` when the language has none. */
static const char *lower_half_of(const char *text, const char *stock)
{
    if (text == NULL) {
        return stock;
    }
    size_t half = strlen(text) / 2;
    for (size_t i = 0; i < half; i++) {
        if (text[i] != ' ') {
            return text;
        }
    }
    return text + half;
}

/* (build 164) The cells a technique's name has in a proportional language:
 * his wide_techs' six (48 px; Saschnella and Nasaresta are past the stock
 * five), the technique windows a cell wider (widen_for_language); (165)
 * the stock five without that layout (the Japanese cartridge's). */
#define TECH_CELLS (lang_layout("wide_techs") ? 6 : 5)

/* (build 164) Technique `id`'s name in a proportional language: its own,
 * or the stock one in its face. */
static const char *tech_text(int id)
{
    const char *text = gamedata_tech_text(id);
    return (text != NULL) ? text : gamedata_tech_name(id);
}

/* loc_F95A: an item's ten name codes as one row of glyph halves. */
static void put_item_name(WinEntry *e, int row, int col, int item,
                          bool lower_half)
{
    const char *name = gamedata_item_name(item);
    size_t      len  = strlen(name);

    /* (build 164) A proportional language's name whole, a run on the
     * lower row (his long_item_names: 80 px, the ten cells). */
    if (lang_proportional()) {
        if (!lower_half) {
            memset(&e->cell[row][col], TILE_FILL, 10);
            return;
        }
        const char *text = gamedata_item_text(item);
        put_run(e, row, col, 10, (text != NULL) ? text : name);
        return;
    }

    for (int i = 0; i < 10; i++) {
        u8 upper, lower;
        win_glyph(((size_t)i < len) ? win_charset(name[i]) : 0,
                  &upper, &lower);
        e->cell[row][col + i] = lower_half ? lower : upper;
    }
}

/* Win_StoreMeseta: eight digits after "MST", map offset $E. */
static void fill_store_meseta(WinSys *ws, WinEntry *e)
{
    put_number(e, 1, 3, (ws->gd != NULL) ? ws->gd->money : 0, 8);
}

/* loc_105C6: six items from the stock, name on two rows and the price
 * (doubled for a Magic Cap on Dezolis) on the lower one. */
static void fill_store_inventory(WinSys *ws, WinEntry *e)
{
    for (int i = 0; i < STORE_ITEMS; i++) {
        int item = store_item(ws, i);
        put_item_name(e, 1 + 2 * i, 2, item, false);
        put_item_name(e, 2 + 2 * i, 2, item, true);
        put_number(e, 2 + 2 * i, 12, win_price(ws, item), 6);
    }
}

static void put_char_name(WinEntry *e, int row, int col, const u8 *name);

/* loc_FE10: the party's names, and $FFFFDE9A counts them. */
static void fill_store_char_list(WinSys *ws, WinEntry *e)
{
    const GameData *gd = ws->gd;
    int row = 1;

    ws->char_list_cursor = 0xFFFF;
    if (gd == NULL) {
        return;
    }
    for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX; m++) {
        int id = gd->party_member_id[m];
        if (id & 0x8000) {
            continue;
        }
        const u8 *name = gd->chr[id % GD_CHARS].name;
        if (lang_proportional()) {
            put_name_run(e, row, 2, name, GD_NAME_MAX);
        }
        for (int i = 0; i < WIN_NAME_LEN && !lang_proportional(); i++) {
            u8 upper, lower;
            win_glyph(name[i] == TXT_END ? 0 : name[i], &upper, &lower);
            e->cell[row][2 + i]     = upper;
            e->cell[row + 1][2 + i] = lower;
        }
        row += 2;
        ws->char_list_cursor++;
    }
}

/* loc_11432: a status label -- dead, poisoned or plain HP/TP -- as two
 * tiles over two tiles. The plain one is "HP" over "TP" in the window
 * charset. */
static const u8 STATUS_DEAD[4]   = { 0x70, 0x71, 0x74, 0x75 };
static const u8 STATUS_POISON[4] = { 0x6E, 0x6F, 0x72, 0x73 };
static const u8 STATUS_FINE[4]   = { 0x2E, 0x36, 0x3A, 0x36 };

/* loc_FE10 again, but from a list the caller hands over: the ORDER
 * list reads $FFFFC618, where a picked member has bit 15 set and is
 * skipped. $FFFFDE9A counts the lines drawn, less one. */
static void fill_char_list_ids(WinSys *ws, WinEntry *e, const u16 *ids,
                               int n)
{
    const GameData *gd = ws->gd;
    int row = 1;

    ws->char_list_cursor = 0xFFFF;
    if (gd == NULL) {
        return;
    }
    for (int m = 0; m < n && m < GD_PARTY_MAX; m++) {
        if (ids[m] & 0x8000) {
            continue;
        }
        put_char_name(e, row, 2, gd->chr[ids[m] % GD_CHARS].name);
        row += 2;
        ws->char_list_cursor++;
    }
}

/* loc_F83A: Win_MenuItemChar and Win_ChosenItemChar. The party's names
 * from row 1, and a four-letter label -- "WHO?" or "ON? " -- written
 * over the bottom border at $37 + 6, the way NEXT sits in a top one.
 * The four bytes before it are blanks into row 9. */
static void fill_menu_char_list(WinSys *ws, WinEntry *e, const char *label)
{
    const GameData *gd = ws->gd;

    if (gd == NULL) {
        return;
    }
    for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX; m++) {
        put_char_name(e, 1 + 2 * m, 2,
                      gd->chr[gd->party_member_id[m] % GD_CHARS].name);
    }
    if (lang_proportional()) {
        /* (build 164) His prompts: "    Who?" (four blank upper halves,
         * then the letters) and "To whom?", which his engine draws as one
         * run on the bottom row, not split four and four. Five cells: the
         * row's last but the border's corner. */
        const char *text = lang_list_name("prompts",
                                          (label[0] == 'W') ? 0 : 2);
        memset(&e->cell[9][1], TILE_FILL, 4);
        put_run(e, 10, 1, 5, lower_half_of(text, label));
        return;
    }
    for (int i = 0; i < 4; i++) {
        u8 upper, lower;
        win_glyph(win_charset(label[i]), &upper, &lower);
        e->cell[9][1 + i]  = TILE_FILL;
        e->cell[10][1 + i] = (label[i] == ' ') ? TILE_FILL : lower;
    }
}

/* Win_FirstCharStats..Win_FourthCharStats (loc_FD04): HP and TP with
 * their maximums on rows 1 and 3, the status label over the art's "LV"
 * in rows 4-5, the level in row 5 from column 2, and the name to its
 * right on rows 4-5. */
static void fill_individual_stats(WinSys *ws, WinEntry *e, int position)
{
    const GameData *gd = ws->gd;

    ws->window_index_saved = 0;
    if (gd == NULL || position > gd->party_members_num) {
        return;
    }
    const CharData *c = &gd->chr[gd->party_member_id[position] % GD_CHARS];

    put_number(e, 1, 3, c->hp, 3);
    put_number(e, 1, 7, c->max_hp, 3);
    put_number(e, 3, 3, c->tp, 3);
    put_number(e, 3, 7, c->max_tp, 3);

    /* loc_1143E: dead, poisoned, or "  " over "LV". */
    if (c->hp == 0) {
        e->cell[4][0] = STATUS_DEAD[0];
        e->cell[4][1] = STATUS_DEAD[1];
        e->cell[5][0] = STATUS_DEAD[2];
        e->cell[5][1] = STATUS_DEAD[3];
    } else if (c->status & 0x8000) {
        e->cell[4][0] = STATUS_POISON[0];
        e->cell[4][1] = STATUS_POISON[1];
        e->cell[5][0] = STATUS_POISON[2];
        e->cell[5][1] = STATUS_POISON[3];
    } else if (lang_proportional()) {
        /* (build 164) His "  LV": two blank upper halves, then LV. */
        e->cell[4][0] = TILE_FILL;
        e->cell[4][1] = TILE_FILL;
        put_run(e, 5, 0, 2, lower_half_of(lang_list_name("prompts", 4),
                                          "LV"));
    } else {
        u8 upper, lower;
        e->cell[4][0] = TILE_FILL;
        e->cell[4][1] = TILE_FILL;
        win_glyph(win_charset('L'), &upper, &lower);
        e->cell[5][0] = lower;
        win_glyph(win_charset('V'), &upper, &lower);
        e->cell[5][1] = lower;
    }
    put_number(e, 5, 2, c->level, 2);
    put_char_name(e, 4, 5, c->name);
}

/* Win_CharOrderDestination (loc_FD98): the members placed so far, from
 * $FFFFC610, one name per two rows from row 1. */
static void fill_order_dest(WinSys *ws, WinEntry *e)
{
    const GameData *gd = ws->gd;

    ws->window_index_saved = 0;
    if (gd == NULL || (ws->order_count & 0x8000)) {
        return;
    }
    for (int m = 0; m <= ws->order_count && m < GD_PARTY_MAX; m++) {
        put_char_name(e, 1 + 2 * m, 2,
                      gd->chr[ws->order_dest[m] % GD_CHARS].name);
    }
}

/* Win_LevelTechList / Win_LevelTechList2 (loc_FEF2): eight of the
 * character's map techniques from `first`, five glyphs from column 2 on
 * every other row. The first page's header is plain border unless the
 * character knows nine or more, when it carries NEXT; the second page's
 * always does. */
static void fill_level_tech_list(WinSys *ws, WinEntry *e, int first,
                                 bool next)
{
    if (!next) {
        memset(e->cell[0], TILE_BORDER_TOP, 6);
    }
    if (ws->gd == NULL) {
        return;
    }
    u8 known[32];
    gamedata_known_techs(ws->gd, ws->character_index, known);

    for (int i = 0; i < 8; i++) {
        if (lang_proportional()) {
            put_run2(e, 1 + 2 * i, 2, TECH_CELLS,
                     tech_text(known[(first + i) & 0x1F]));
            continue;
        }
        const char *name = gamedata_tech_name(known[(first + i) & 0x1F]);
        size_t len = strlen(name);
        for (int k = 0; k < 5; k++) {
            u8 upper, lower;
            win_glyph(((size_t)k < len) ? win_charset(name[k]) : 0,
                      &upper, &lower);
            e->cell[1 + 2 * i][2 + k] = upper;
            e->cell[2 + 2 * i][2 + k] = lower;
        }
    }
}

/* loc_10110: agility, then attack and defense, three digits each at
 * column 8 of rows 2, 4 and 6. */
static void fill_equip_stats(WinSys *ws, WinEntry *e)
{
    ws->window_index_saved = 0;
    if (ws->gd == NULL) {
        return;
    }
    const CharData *c = &ws->gd->chr[ws->character_index % GD_CHARS];
    put_number(e, 2, 8, c->agility, 3);
    put_number(e, 4, 8, c->attack, 3);
    put_number(e, 6, 8, c->defense, 3);
}

/* loc_F922: eight of a character's items from `first`, the equipped
 * mark in column 0 of the lower row, and the header row either plain or
 * carrying NEXT. */
static void fill_item_list(WinSys *ws, WinEntry *e, int first, bool next)
{
    if (!next) {
        memset(e->cell[0], TILE_BORDER_TOP, 8);
    }
    if (ws->gd == NULL) {
        return;
    }
    u8 *count;
    const u8 *items = gamedata_bag((GameData *)ws->gd, ws->character_index,
                                   &count);

    for (int i = 0; i < 8; i++) {
        u8 item = items[(first + i) % GD_ITEM_SLOTS];
        e->cell[1 + 2 * i][0] = TILE_FILL;
        e->cell[2 + 2 * i][0] = (item & 0x80) ? TILE_EQUIPPED : TILE_FILL;
        put_item_name(e, 1 + 2 * i, 3, item & 0x7F, false);
        put_item_name(e, 2 + 2 * i, 3, item & 0x7F, true);
    }
}

/* loc_11432: a status label -- dead, poisoned or plain HP/TP -- as two
 * tiles over two tiles. The plain one is "HP" over "TP" in the window
 * charset. */

/* loc_1135A / StartDecimalConversion: three places, leading blanks. */
static void put_small_number(WinEntry *e, int row, int col, u16 value)
{
    put_number(e, row, col, value, 3);
}

/* Win_MenuCharStats (ps2.asm:23186): seven columns a member -- name on
 * rows 1-2 from column 1, the status label at column 0 of rows 4-5,
 * HP and TP in the three columns from 3. */
static void fill_char_stats(WinSys *ws, WinEntry *e)
{
    const GameData *gd = ws->gd;
    if (gd == NULL) {
        return;
    }
    for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX; m++) {
        const CharData *c = &gd->chr[gd->party_member_id[m] % GD_CHARS];
        int col = 7 * m;

        if (lang_proportional()) {
            put_name_run(e, 1, col + 1, c->name, GD_NAME_MAX);
        }
        for (int i = 0; i < WIN_NAME_LEN && !lang_proportional(); i++) {
            u8 upper, lower;
            win_glyph(c->name[i] == TXT_END ? 0 : c->name[i],
                      &upper, &lower);
            e->cell[1][col + 1 + i] = upper;
            e->cell[2][col + 1 + i] = lower;
        }

        const u8 *label = (c->hp == 0) ? STATUS_DEAD
                        : (c->status & 0x8000) ? STATUS_POISON
                        : STATUS_FINE;
        e->cell[4][col]     = label[0];
        e->cell[4][col + 1] = label[1];
        e->cell[5][col]     = label[2];
        e->cell[5][col + 1] = label[3];

        put_small_number(e, 4, col + 3, c->hp);
        put_small_number(e, 5, col + 3, c->tp);
    }
}

/* TeleportPlaceNamesArray (ps2.asm:25345), from level 3, five codes a
 * name. */
static const char *const TOWN_NAME[10] = {
    "SKURE", "PASEO", "ARIMA", "OPUTA", "ZEMA ", "KUERI", "PIATA",
    "AUKBA", "ZOSA ", "RYUON"
};

static void put_town(WinEntry *e, int slot, int level)
{
    const char *name = (level >= 3 && level <= 12) ? TOWN_NAME[level - 3]
                                                   : "     ";
    if (lang_proportional()) {
        const char *text = (level >= 3 && level <= 12)
                         ? lang_list_name("towns", level - 3) : "";
        put_run2(e, 1 + 2 * slot, 2, 5, (text != NULL) ? text : name);
        return;
    }
    for (int i = 0; i < 5; i++) {
        u8 upper, lower;
        win_glyph(win_charset(name[i]), &upper, &lower);
        e->cell[1 + 2 * slot][2 + i] = upper;
        e->cell[2 + 2 * slot][2 + i] = lower;
    }
}

/* SoundtrackCharArray (ps2.asm:32154): twelve characters each, in
 * Ustvestia_MusicPointers' order. */
static const char *const SONG_NAME[22] = {
    "Phantasy",    "Restration",  "Pleasure",    "Advanced",
    "Step Up",     "Bracky News", "My Home",     "Pressure",
    "a Prologue",  "Rise or Fall", "Movement",   "Over",
    "Secret Ways", "Mystery",     "Death Place", "Silent Zone",
    "Excite Town", "Violation",   "Power",       "Under",
    "Exclaim",     "Never Dream"
};

/* Win_UstvestiaSoundtracks (ps2.asm:25364): the name of song $CB12 / 2,
 * upper halves into row 1 and lower halves into row 2 of the art
 * (loc_10EA0 twice, d3 0 then 1), twelve characters from column 0. */
static void fill_ustvestia(WinSys *ws, WinEntry *e)
{
    const char *name = SONG_NAME[(ws->ustvestia_song >> 1) % 22];
    int len = (int)strlen(name);
    if (lang_proportional()) {
        const char *text = lang_list_name("songs",
                                          (ws->ustvestia_song >> 1) % 22);
        put_run2(e, 1, 0, 12, (text != NULL) ? text : name);
        return;
    }
    for (int i = 0; i < 12; i++) {
        u8 upper, lower;
        win_glyph(win_charset((i < len) ? name[i] : ' '), &upper, &lower);
        e->cell[1][i] = upper;
        e->cell[2][i] = lower;
    }
}

/* loc_10F2A: the towns known, from $C704, skipping the one you are in.
 * With $FFFFF764 set (the Dezolis stations) the list is Skure then
 * $C70A on. $FFFFDED6 counts the lines, less one. */
static void fill_teleport(WinSys *ws, WinEntry *e)
{
    const GameData *gd = ws->gd;
    int n = 0;

    ws->teleport_choice = 0xFFFF;
    memset(ws->teleport_level, 0, sizeof ws->teleport_level);
    if (gd == NULL) {
        return;
    }

    int from  = 0x04;
    int tries = 6;
    if (ws->store_max != 0) {
        ws->teleport_level[n] = 3;
        put_town(e, n++, 3);
        ws->teleport_choice++;
        from  = 0x0A;
        tries = 3;
    }
    for (int i = 0; i < tries && n < 5; i++) {
        int level = gd->c700[from + i];
        if (level == 0 || level == gd->level_index) {
            continue;
        }
        ws->teleport_level[n] = (u8)level;
        put_town(e, n++, level);
        ws->teleport_choice++;
    }
}

/* A character's name as the $C038 tile bytes lay it out: upper halves at
 * (row, col), lower halves one row down. */
static void put_char_name(WinEntry *e, int row, int col, const u8 *name)
{
    if (lang_proportional()) {
        put_name_run(e, row, col, name, GD_NAME_MAX);
        return;
    }
    for (int i = 0; i < WIN_NAME_LEN; i++) {
        u8 upper, lower;
        win_glyph(name[i] == TXT_END ? 0 : name[i], &upper, &lower);
        e->cell[row][col + i]     = upper;
        e->cell[row + 1][col + i] = lower;
    }
}

static void blank_cells(WinEntry *e, int row, int col, int n)
{
    memset(&e->cell[row][col], TILE_FILL, (size_t)n);
}

/* loc_10684: everyone who has joined, in character order, then blanks to
 * eight lines. */
static void fill_profile_list(WinSys *ws, WinEntry *e)
{
    const GameData *gd = ws->gd;
    int joined = (gd != NULL) ? gd->party_members_joined : 0;

    for (int c = 0; c < 8; c++) {
        if (gd != NULL && c <= joined) {
            put_char_name(e, 1 + 2 * c, 2, gd->chr[c].name);
        } else {
            blank_cells(e, 1 + 2 * c, 2, WIN_NAME_LEN);
            blank_cells(e, 2 + 2 * c, 2, WIN_NAME_LEN);
        }
    }
}

/* loc_10728: whoever has joined from Rudo on and is not already picked.
 * $FFFFC602 is the line count less one. */
static void fill_regroup_list(WinSys *ws, WinEntry *e)
{
    const GameData *gd = ws->gd;
    int line = 0;

    ws->regroup_count = 0xFFFF;
    if (gd == NULL) {
        return;
    }
    for (int c = 2; c <= gd->party_members_joined && c < GD_CHARS; c++) {
        bool picked = false;
        for (int m = 0; m < 3; m++) {
            if (gd->party_member_id[m] == c) {
                picked = true;
            }
        }
        if (picked) {
            continue;
        }
        put_char_name(e, 1 + 2 * line, 2, gd->chr[c].name);
        line++;
        ws->regroup_count++;
    }
}

/* Win_RegroupSelectedChar: the party so far, four tiles a row. */
static void fill_regroup_selected(WinSys *ws, WinEntry *e)
{
    const GameData *gd = ws->gd;
    if (gd == NULL || (gd->party_members_num & 0x8000)) {
        return;
    }
    for (int m = 0; m <= gd->party_members_num && m < GD_PARTY_MAX; m++) {
        const CharData *c = &gd->chr[gd->party_member_id[m] % GD_CHARS];
        put_char_name(e, 1 + 2 * m, 0, c->name);
    }
}

/* loc_10000: strength, mental, agility, luck, dexterity, then attack and
 * defense -- $C010 on, skipping the base attack at $C01A. */
static void fill_strng_stats(WinSys *ws, WinEntry *e)
{
    if (ws->gd == NULL) {
        return;
    }
    const CharData *c = &ws->gd->chr[ws->character_index % GD_CHARS];
    const u16 v[7] = { c->strength, c->mental, c->agility, c->luck,
                       c->dexterity, c->attack, c->defense };
    for (int i = 0; i < 7; i++) {
        put_number(e, 1 + 2 * i, 8, v[i], 3);
    }
}

/* loc_11456: each character's job, eight tiles in the window charset. */
static const char *const JOB[8] = {
    " AGENT  ", " NO JOB ", " HUNTER ", " DOCTOR ",
    "BIOLGST ", "GUARDIAN", "WRECKER ", " THIEF  "
};

/* Win_StrngLVEXP: name, level, job and experience. */
static void fill_lvexp(WinSys *ws, WinEntry *e)
{
    if (ws->gd == NULL) {
        return;
    }
    int who = ws->character_index % GD_CHARS;
    const CharData *c = &ws->gd->chr[who];

    put_char_name(e, 1, 6, c->name);
    put_number(e, 2, 2, c->level, 2);
    if (lang_proportional()) {
        const char *text = lang_list_name("jobs", who);
        put_run(e, 4, 1, 8, (text != NULL) ? text : JOB[who]);
    }
    for (int i = 0; i < 8 && !lang_proportional(); i++) {
        u8 upper, lower;
        win_glyph(win_charset(JOB[who][i]), &upper, &lower);
        e->cell[4][1 + i] = (JOB[who][i] == ' ') ? TILE_FILL : lower;
    }
    put_number(e, 6, 3, c->experience, 7);
}

/* loc_FD50: HP and its maximum on row 1, TP and its maximum on row 3,
 * each three digits either side of the art's "/". */
static void fill_strng_hptp(WinSys *ws, WinEntry *e)
{
    if (ws->gd == NULL) {
        return;
    }
    const CharData *c = &ws->gd->chr[ws->character_index % GD_CHARS];
    put_number(e, 1, 3, c->hp, 3);
    put_number(e, 1, 7, c->max_hp, 3);
    put_number(e, 3, 3, c->tp, 3);
    put_number(e, 3, 7, c->max_tp, 3);
}

/* loc_10076: the five equipment slots, head to feet, names from
 * column 5 of every other row. */
static void fill_strng_equip(WinSys *ws, WinEntry *e)
{
    if (ws->gd == NULL) {
        return;
    }
    const CharData *c = &ws->gd->chr[ws->character_index % GD_CHARS];
    for (int i = 0; i < EQUIP_COUNT; i++) {
        put_item_name(e, 1 + 2 * i, 5, c->equip[i] & 0x7F, false);
        put_item_name(e, 2 + 2 * i, 5, c->equip[i] & 0x7F, true);
    }
}

/* Win_FullTechList (ps2.asm:24019): sixteen of the character's 32 in two
 * columns of eight, at columns 0 and 6. The first window takes the map
 * half, the second the battle half. */
/* Win_BattleFirstCharStats..Fourth (ps2.asm:24678), loc_10946. The
 * two-tile label pair for HP and TP rows comes from loc_114EA by the
 * character's state: dead, then status bit 14, then bit 4 clear with
 * a nonzero low byte, then bit 15 (poison), else "HP"/"TP". Then three
 * digits of HP and TP, the command's 2x2 icon (loc_114DA) and the name
 * as two rows of glyph halves. The bytes are tile indices: this table
 * sits under the tile-index charset ('A' -> 39). */
static void fill_battle_char_stats(WinSys *ws, WinEntry *e, int position)
{
    static const u8 LABEL[5][4] = {
        { 0x70, 0x71, 0x74, 0x75 },     /* dead                          */
        { 0x6A, 0x6B, 0x3A, 0x36 },     /* status bit 14, "TP"           */
        { 0x6C, 0x6D, 0x3A, 0x36 },     /* low byte set, bit 4 clear     */
        { 0x6E, 0x6F, 0x72, 0x73 },     /* poison                        */
        { 0x2E, 0x36, 0x3A, 0x36 },     /* "HP" "TP"                     */
    };
    static const u8 ICON[4][4] = {      /* loc_114DA: ATTK TECH ITEM DEFN */
        { 0x12, 0x13, 0x14, 0x15 },
        { 0x16, 0x17, 0x18, 0x19 },
        { 0x1A, 0x1B, 0x1C, 0x1D },
        { 0x1E, 0x1F, 0x20, 0x21 },
    };

    if (ws->gd == NULL || position > ws->gd->party_members_num) {
        return;
    }
    int member = ws->gd->party_member_id[position] & 7;
    const CharData *c = &ws->gd->chr[member];

    int k;
    if (c->hp == 0) {
        k = 0;
    } else if (c->status & 0x4000) {
        k = 1;
    } else if (!(c->status & 0x10) && (c->status & 0xFF) != 0) {
        k = 2;
    } else if (c->status & 0x8000) {
        k = 3;
    } else {
        k = 4;
    }
    e->cell[1][0] = LABEL[k][0];
    e->cell[1][1] = LABEL[k][1];
    e->cell[2][0] = LABEL[k][2];
    e->cell[2][1] = LABEL[k][3];
    put_number(e, 1, 3, c->hp, 3);
    put_number(e, 2, 3, c->tp, 3);

    int cmd = (ws->battle_cmd != NULL) ? (ws->battle_cmd[member][0] & 3) : 0;
    e->cell[3][0] = ICON[cmd][0];
    e->cell[3][1] = ICON[cmd][1];
    e->cell[4][0] = ICON[cmd][2];
    e->cell[4][1] = ICON[cmd][3];
    put_char_name(e, 3, 2, c->name);
}

/* Win_FirstEnemyName / Win_SecondEnemyName (25186): ten CHARSET codes
 * through VDPCharacterMaps, uppers on row 1 and lowers on row 2, drawn
 * with base $500 -- no priority. */
/* (build 164) Group g's name in a proportional language: the language's,
 * else the stock codes; a run on the lower row, `cells` wide. */
static void put_enemy_run(WinSys *ws, WinEntry *e, int row, int col, int g,
                          int cells)
{
    const u8   *name = ws->enemy_name[g];
    const char *text = (name != NULL && ws->enemy_text != NULL)
                     ? ws->enemy_text(ws->ctx, ws->enemy_group_id[g]) : NULL;
    memset(&e->cell[row][col], TILE_FILL, (size_t)cells);
    if (text != NULL) {
        put_run(e, row + 1, col, cells, text);
        return;
    }
    u8  codes[10];
    int n = 0;
    while (name != NULL && n < 10 && name[n] != TXT_END) {
        codes[n] = name[n];
        n++;
    }
    put_run_codes(e, row + 1, col, cells, codes, n);
}

static void fill_enemy_name(WinSys *ws, WinEntry *e, int group)
{
    if (lang_proportional()) {
        put_enemy_run(ws, e, 1, 0, group, 10);
        return;
    }
    const u8 *name = ws->enemy_name[group];
    for (int i = 0; i < 10; i++) {
        u8 upper, lower;
        win_glyph((name != NULL) ? name[i] : 0, &upper, &lower);
        e->cell[1][i] = upper;
        e->cell[2][i] = lower;
    }
}

/* Win_FirstEnemyInfo / Win_SecondEnemyInfo (25224): row 1 is blank or
 * "Heal" (bit 14 of $FFFFCB20), row 2 blank, "miss" (negative) or the
 * damage as digits (loc_11350). */
static void fill_enemy_info(WinSys *ws, WinEntry *e, int group)
{
    static const u8 HEAL[4] = { 0x2E, 0x45, 0x41, 0x4C };
    static const u8 MISS[4] = { 0x4D, 0x49, 0x53, 0x53 };
    u16 v = (ws->enemy_info != NULL) ? ws->enemy_info[group] : 0;

    memset(e->cell[1], TILE_FILL, 4);
    memset(e->cell[2], TILE_FILL, 4);
    /* (build 164) His labels (names.json "labels": "    Heal", "    Miss",
     * the four blanks the stock entry's upper halves). */
    bool prop = lang_proportional();
    put_run(e, 1, 0, 4, NULL);
    put_run(e, 2, 0, 4, NULL);
    if (!(v & 0x8000) && (v & 0x4000)) {
        if (prop) {
            put_run(e, 1, 0, 4, lower_half_of(lang_list_name("labels", 1),
                                              "Heal"));
        } else {
            memcpy(e->cell[1], HEAL, 4);
        }
        v &= 0xBFFF;
    }
    if (v & 0x8000) {
        if (prop) {
            put_run(e, 2, 0, 4, lower_half_of(lang_list_name("labels", 2),
                                              "Miss"));
        } else {
            memcpy(e->cell[2], MISS, 4);
        }
    } else if (v != 0) {
        put_number(e, 2, 0, v, 4);
    }
}

/* Win_BattleCharName (24829): the name from CharData+$38, the eight
 * glyph halves. */
static void fill_battle_char_name(WinSys *ws, WinEntry *e)
{
    if (ws->gd == NULL) {
        return;
    }
    put_char_name(e, 1, 0, ws->gd->chr[ws->character_index % GD_CHARS].name);
}

/* Win_EnemyGroups' draw (loc_10B2A): both groups' names from map offset
 * $E -- row 1, column 2 -- upper then lower, the second group two rows
 * down. */
static void fill_enemy_groups(WinSys *ws, WinEntry *e)
{
    for (int g = 0; g < 2 && lang_proportional(); g++) {
        put_enemy_run(ws, e, 1 + 2 * g, 2, g, 10);
    }
    for (int g = 0; g < 2 && !lang_proportional(); g++) {
        const u8 *name = ws->enemy_name[g];
        for (int i = 0; i < 10; i++) {
            u8 upper, lower;
            u8 code = (name != NULL) ? name[i] : 0;
            win_glyph((code == TXT_END) ? 0 : code, &upper, &lower);
            e->cell[1 + 2 * g][2 + i] = upper;
            e->cell[2 + 2 * g][2 + i] = lower;
        }
    }
}

/* Win_BattleTechList's draw (loc_10CBE): the top border plain when the
 * character knows fewer than five battle techniques, else the NEXT box
 * (loc_1144A); four names from $C810 + 32c + page, five glyphs each
 * on the column after the lamp. */
static void fill_battle_tech_list(WinSys *ws, WinEntry *e)
{
    if (ws->gd == NULL) {
        return;
    }
    const CharData *c = &ws->gd->chr[ws->character_index % GD_CHARS];
    if ((c->techs_a & 0x1F) < 5) {
        memset(e->cell[0], TILE_BORDER_TOP, 6);
    }
    u8 known[32];
    gamedata_known_techs(ws->gd, ws->character_index, known);
    for (int i = 0; i < 4; i++) {
        int idx = (ws->tech_page + i) & 0x1F;
        const char *name = gamedata_tech_name(known[16 + (idx & 0xF)]
                                              & 0x3F);
        if (idx >= 16) {
            name = "";
        }
        if (lang_proportional()) {
            put_run2(e, 1 + 2 * i, 2, TECH_CELLS,
                     (idx >= 16) ? ""
                     : tech_text(known[16 + (idx & 0xF)] & 0x3F));
            continue;
        }
        size_t len = strlen(name);
        for (int k = 0; k < 5; k++) {
            u8 upper, lower;
            win_glyph(((size_t)k < len) ? win_charset(name[k]) : 0,
                      &upper, &lower);
            e->cell[1 + 2 * i][2 + k] = upper;
            e->cell[2 + 2 * i][2 + k] = lower;
        }
    }
}

/* Win_BattleItemList's draw (loc_10DF0): the first eight bytes of the
 * top border plain (loc_1141A) or with NEXT, then four items from the
 * bag at the page: the worn mark at column 0 of the lower row, the name
 * from column 3. */
static void fill_battle_item_list(WinSys *ws, WinEntry *e)
{
    if (ws->gd == NULL) {
        return;
    }
    u8 *count;
    const u8 *items = gamedata_bag((GameData *)ws->gd, ws->character_index,
                                   &count);
    if (*count < 5) {
        memset(e->cell[0], TILE_BORDER_TOP, 8);
    }
    for (int i = 0; i < 4; i++) {
        u8 item = items[(ws->item_page + i) % GD_ITEM_SLOTS];
        e->cell[1 + 2 * i][0] = TILE_FILL;
        e->cell[2 + 2 * i][0] = (item & 0x80) ? TILE_EQUIPPED : TILE_FILL;
        put_item_name(e, 1 + 2 * i, 3, item & 0x7F, false);
        put_item_name(e, 2 + 2 * i, 3, item & 0x7F, true);
    }
}

/* Win_BattleItemUsed (25159), Win_BattleTechUsed (25170): the name on
 * rows 1-2 from column 0. */
static void fill_battle_item_used(WinSys *ws, WinEntry *e)
{
    put_item_name(e, 1, 0, ws->item_index & 0x7F, false);
    put_item_name(e, 2, 0, ws->item_index & 0x7F, true);
}

static void fill_battle_tech_used(WinSys *ws, WinEntry *e)
{
    if (lang_proportional()) {
        put_run2(e, 1, 0, TECH_CELLS, tech_text(ws->technique_index & 0x3F));
        return;
    }
    const char *name = gamedata_tech_name(ws->technique_index & 0x3F);
    size_t len = strlen(name);
    for (int k = 0; k < 5; k++) {
        u8 upper, lower;
        win_glyph(((size_t)k < len) ? win_charset(name[k]) : 0,
                  &upper, &lower);
        e->cell[1][k] = upper;
        e->cell[2][k] = lower;
    }
}

static void fill_tech_list(WinSys *ws, WinEntry *e, int half)
{
    if (ws->gd == NULL) {
        return;
    }
    u8 known[32];
    gamedata_known_techs(ws->gd, ws->character_index, known);

    for (int i = 0; i < 16; i++) {
        const char *name = gamedata_tech_name(known[half * 16 + i]);
        size_t len = strlen(name);
        int row = 1 + 2 * (i % 8);
        int col = (i < 8) ? 0 : 6;
        if (lang_proportional()) {
            put_run2(e, row, (i < 8) ? 0 : TECH_CELLS + 1, TECH_CELLS,
                     tech_text(known[half * 16 + i]));
            continue;
        }
        for (int k = 0; k < 5; k++) {
            u8 upper, lower;
            win_glyph(((size_t)k < len) ? win_charset(name[k]) : 0,
                      &upper, &lower);
            e->cell[row][col + k]     = upper;
            e->cell[row + 1][col + k] = lower;
        }
    }
}

/* (build 164) The windows a proportional language draws wider, his layout
 * for his longer words: the technique lists and the casting plate a cell
 * (wide_techs), STRNG's two lists two and moved left (his $4410 and $4430:
 * columns 8 and 24), the main menu a cell ("Techniques" is 46 px). The new
 * columns are the map's last: fill inside, its border on the border rows.
 * (165) As the language's lang.json "layout" asks: "wide_techs" and
 * "wide_menu", the retranslation's. */
static void widen_for_language(WinEntry *e, int id)
{
    int k   = 0;
    int col = e->col;
    if (!lang_layout("wide_techs") && id != WIN_PLAYER_MENU) {
        return;
    }
    switch (id) {
        case WIN_PLAYER_MENU: {
            k = lang_layout("wide_menu") ? 1 : 0;
            break;
        }
        case WIN_LEVEL_TECH_LIST: {
            k = 1;
            break;
        }
        case WIN_LEVEL_TECH_LIST2: {
            k = 1;
            break;
        }
        case WIN_BATTLE_TECH_LIST: {
            k = 1;
            break;
        }
        case WIN_BATTLE_TECH_USED: {
            k = 1;
            break;
        }
        case WIN_FULL_TECH_LIST: {
            k   = 2;
            col = 8;
            break;
        }
        case WIN_FULL_TECH_LIST2: {
            k   = 2;
            col = 24;
            break;
        }
        default: {
            break;
        }
    }
    int map_w = e->w - 1;
    if (k == 0 || map_w <= 0 || map_w + k > WIN_MAX_COLS) {
        return;
    }
    for (int r = 0; r < e->rows; r++) {
        bool border = (r == 0 || r == e->rows - 1);
        for (int c = map_w; c < map_w + k; c++) {
            e->cell[r][c] = border ? e->cell[r][map_w - 1] : TILE_FILL;
        }
    }
    e->w  += k;
    e->col = col;
}

/* (build 164) A window map's letter tiles (the window charset: A-Z $27,
 * a-z $41, the punctuation $5B-$62), which a label is made of. */
static bool label_tile(u8 t)
{
    return t >= 0x27 && t <= 0x62;
}

/* (build 164) The name board's last row in a proportional language: ADV,
 * RUB and END as his words (FWD, DEL, END), each at its stock word's
 * place, the letters' grid above left as it is -- its cursor stops on
 * the cells. */
static void name_board_words(WinEntry *e, const char *art, int row)
{
    const char *text = lang_window_label(art, 3);
    if (text == NULL) {
        return;
    }
    int cols = e->w - 1;
    int col  = 0;
    for (;;) {
        while (*text == ' ') {
            text++;
        }
        while (col < cols && !label_tile(e->cell[row][col])) {
            col++;
        }
        if (*text == '\0' || col >= cols) {
            return;
        }
        int start = col;
        while (col < cols && label_tile(e->cell[row][col])) {
            col++;
        }
        char word[16];
        int  n = 0;
        while (*text != '\0' && *text != ' ' && n + 1 < (int)sizeof word) {
            word[n++] = *text++;
        }
        word[n] = '\0';
        put_run(e, row, start, col - start, word);
    }
}

/* (build 164) His labels for a window map (names.json "windows", his
 * vwf_windows): each row of the map with letters is a label, in order,
 * and his row of the same place takes its letters' cells, drawn from the
 * first letter's cell over the cells up to the next thing the row holds
 * (a number's placeholder, the border) -- numbers stay the stock digits,
 * where the game writes them. A window whose rows and his do not agree
 * keeps the stock labels. The profiles take his five lines on rows 2-10
 * (the stock Rolf has four); the name board only its last row. The battle
 * party boxes' HP/TP are written over by their status, so stay. */
static void lang_labels(WinEntry *e, const char *art)
{
    if (art == NULL || strcmp(art, "WinArt_BattleCharStats") == 0) {
        return;
    }
    int cols = e->w - 1;
    size_t alen = strlen(art);
    if (alen > 7 && strcmp(art + alen - 7, "Profile") == 0) {
        for (int r = 1; r < e->rows - 1; r++) {
            for (int c = 0; c < cols; c++) {
                if (label_tile(e->cell[r][c])) {
                    e->cell[r][c] = TILE_FILL;
                }
            }
        }
        for (int i = 0; 2 + 2 * i < e->rows - 1; i++) {
            put_run(e, 2 + 2 * i, 0, cols, lang_window_label(art, i));
        }
        return;
    }
    if (strcmp(art, "WinArt_NameInput") == 0) {
        if (lang_list_name("board", 0) == NULL) {
            name_board_words(e, art, e->rows - 3);
            return;
        }
        /* (build 165) A language's board: its rows from the art's row 5,
         * two apart, each a run a cell a letter -- the stock letters
         * gone. */
        for (int r = 1; r < e->rows - 1; r++) {
            for (int c = 0; c < cols; c++) {
                if (label_tile(e->cell[r][c])) {
                    e->cell[r][c] = TILE_FILL;
                }
            }
        }
        for (int k = 0; k < BOARD_ROWS && 5 + 2 * k < e->rows - 1; k++) {
            put_run(e, 5 + 2 * k, 0, cols, lang_list_name("board", k));
        }
        return;
    }

    int rows[WIN_MAX_ROWS];
    int n = 0;
    for (int r = 1; r < e->rows - 1; r++) {
        for (int c = 0; c < cols; c++) {
            if (label_tile(e->cell[r][c])) {
                rows[n++] = r;
                break;
            }
        }
    }
    if (n == 0 || lang_window_label(art, n - 1) == NULL
        || lang_window_label(art, n) != NULL) {
        if (n > 0 && lang_window_label(art, 0) != NULL) {
            fprintf(stderr, "window: %s has %d labels; the language's do "
                            "not match, the stock ones stay\n", art, n);
        }
        return;
    }
    for (int i = 0; i < n; i++) {
        int r = rows[i];
        int first = 0;
        while (!label_tile(e->cell[r][first])) {
            first++;
        }
        int last = cols - 1;
        while (!label_tile(e->cell[r][last])) {
            last--;
        }
        int end = last + 1;
        while (end < cols && e->cell[r][end] == TILE_FILL) {
            end++;
        }
        memset(&e->cell[r][first], TILE_FILL, (size_t)(last + 1 - first));
        put_run(e, r, first, end - first, lang_window_label(art, i));
    }
}

/* (build 163) His battle_box (ext/battlebox.asm), which his battle text
 * is written for: in a proportional language the battle's message box is
 * the dialogue window's width, 24 cells at columns 7-32 (the stock box is
 * 20 at 9-30), and two lines -- the script window's art two rows higher
 * -- when a script queued as it opens breaks its line, one otherwise. A
 * {C6} message stays up a second a line (text_poll). */
#define BATTLE_BOX_COL  7
#define BATTLE_BOX_ROW  17

static bool script_breaks(const char *src)
{
    if (src == NULL) {
        return false;
    }
    /* The entry's last newline is the file's own (load_script). */
    size_t len = strlen(src);
    while (len > 0 && src[len - 1] == '\n') {
        len--;
    }
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '\n' || strncmp(src + i, "{pause}", 7) == 0) {
            return true;
        }
    }
    return false;
}

/* (build 166) True while win_language_changed rebuilds what is shown: the
 * battle box then also asks the script it is showing (its text_script),
 * which has left the queue. */
static bool g_rebuilding;

static void battle_box(WinSys *ws, WinEntry *e)
{
    bool two = g_rebuilding && ws->script_text != NULL && ws->text_script != 0
               && script_breaks(ws->script_text(ws->ctx, ws->text_script));
    for (int i = 0; i < WIN_QUEUE && ws->script_id[i] != 0; i++) {
        if (ws->script_text != NULL
            && script_breaks(ws->script_text(ws->ctx, ws->script_id[i]))) {
            two = true;
        }
    }
    const WinDesc *art = find_desc(WIN_SCRIPT_MESSAGE);
    if (art == NULL || !art->loaded) {
        return;
    }
    ws->battle_box_lines = two ? 2 : 1;
    e->col  = BATTLE_BOX_COL;
    e->row  = BATTLE_BOX_ROW - (two ? 2 : 0);
    e->w    = art->w;
    e->rows = two ? art->h + 1 : 4;
    const WinMap *m = &art->map;
    for (int r = 0; r < e->rows; r++) {
        /* One line: the art's top, two text rows, its bottom. */
        int from = (r == e->rows - 1) ? m->height - 1 : r;
        memcpy(e->cell[r], &m->cell[from * m->width], (size_t)m->width);
    }
}

static bool build_entry(WinSys *ws, WinEntry *e, int id)
{
    WinDesc *d = find_desc(id);
    if (d == NULL || !d->loaded) {
        fprintf(stderr, "window: id $%02X not ported\n", id);
        return false;
    }

    if (d->map.width > WIN_MAX_COLS || d->map.height > WIN_MAX_ROWS) {
        fprintf(stderr, "window: $%02X is %dx%d, the cell grid holds %dx%d\n",
                id, d->map.width, d->map.height, WIN_MAX_COLS, WIN_MAX_ROWS);
        return false;
    }

    memset(e, 0, sizeof *e);
    e->id    = id;
    e->row   = (d->pos & 0x0F80) >> 7;
    e->col   = (d->pos & 0x007F) >> 1;
    e->w     = d->w;
    e->rows  = d->h + 1;
    e->base  = ws->tile_base;
    e->layer = (d->pos & 0x2000) ? LAYER_B : LAYER_A;

    for (int r = 0; r < e->rows; r++) {
        memcpy(e->cell[r], &d->map.cell[r * d->map.width],
               (size_t)d->map.width);
    }
    if (lang_proportional()) {
        widen_for_language(e, id);  /* (build 164) */
        lang_labels(e, d->art);
    }
    if (id == WIN_BATTLE_MESSAGE) {
        ws->battle_box_lines = 0;
        if (lang_layout("battle_box")) {     /* (build 165) his alone */
            battle_box(ws, e);
        }
    }

    switch (id) {
        case WIN_BATTLE_CHAR_STATS_1: {
            fill_battle_char_stats(ws, e, 0);
            break;
        }
        case WIN_BATTLE_CHAR_STATS_1 + 1: {
            fill_battle_char_stats(ws, e, 1);
            break;
        }
        case WIN_BATTLE_CHAR_STATS_1 + 2: {
            fill_battle_char_stats(ws, e, 2);
            break;
        }
        case WIN_BATTLE_CHAR_STATS_1 + 3: {
            fill_battle_char_stats(ws, e, 3);
            break;
        }
        case WIN_ENEMY_NAME_1: {
            fill_enemy_name(ws, e, 0);
            break;
        }
        case WIN_ENEMY_NAME_2: {
            fill_enemy_name(ws, e, 1);
            break;
        }
        case WIN_ENEMY_INFO_1: {
            fill_enemy_info(ws, e, 0);
            break;
        }
        case WIN_ENEMY_INFO_2: {
            fill_enemy_info(ws, e, 1);
            break;
        }
        case WIN_BATTLE_CHAR_NAME: {
            fill_battle_char_name(ws, e);
            break;
        }
        case WIN_ENEMY_GROUPS: {
            fill_enemy_groups(ws, e);
            break;
        }
        case WIN_BATTLE_TECH_LIST: {
            fill_battle_tech_list(ws, e);
            break;
        }
        case WIN_BATTLE_ITEM_LIST: {
            fill_battle_item_list(ws, e);
            break;
        }
        case WIN_BATTLE_ITEM_USED: {
            fill_battle_item_used(ws, e);
            break;
        }
        case WIN_BATTLE_TECH_USED: {
            fill_battle_tech_used(ws, e);
            break;
        }
        case WIN_SAVE_SLOTS: {
            fill_save_slots(ws, e);
            break;
        }
        case WIN_STORE_MESETA: {
            fill_store_meseta(ws, e);
            break;
        }
        case WIN_STORE_INVENTORY: {
            fill_store_inventory(ws, e);
            break;
        }
        case WIN_STORE_CHAR_LIST: {
            fill_store_char_list(ws, e);
            break;
        }
        case WIN_MENU_ITEM_LIST: {
            u8 *count = NULL;
            if (ws->gd != NULL) {
                gamedata_bag((GameData *)ws->gd, ws->character_index,
                             &count);
            }
            bool next = (count != NULL) && *count >= 9;
            fill_item_list(ws, e, 0, next);
            break;
        }
        case WIN_MENU_ITEM_LIST2: {
            fill_item_list(ws, e, 8, true);
            break;
        }
        case WIN_ITEM_LIST2: {
            u8 *count = NULL;
            if (ws->gd != NULL) {
                gamedata_bag((GameData *)ws->gd, ws->character_index,
                             &count);
            }
            bool next = (count != NULL) && *count >= 9;
            fill_item_list(ws, e, 0, next);
            break;
        }
        case WIN_ITEM_LIST3: {
            fill_item_list(ws, e, 8, true);
            break;
        }
        case WIN_MENU_ITEM_CHAR: {
            fill_menu_char_list(ws, e, "WHO?");
            break;
        }
        case WIN_CHOSEN_ITEM_CHAR: {
            fill_menu_char_list(ws, e, "ON? ");
            break;
        }
        case WIN_CHAR_STATS_1: {
            fill_individual_stats(ws, e, 0);
            break;
        }
        case WIN_CHAR_STATS_1 + 1: {
            fill_individual_stats(ws, e, 1);
            break;
        }
        case WIN_CHAR_STATS_1 + 2: {
            fill_individual_stats(ws, e, 2);
            break;
        }
        case WIN_CHAR_STATS_1 + 3: {
            fill_individual_stats(ws, e, 3);
            break;
        }
        case WIN_MENU_MESETA: {
            fill_store_meseta(ws, e);
            break;
        }
        case WIN_CHAR_ORDER_DEST: {
            fill_order_dest(ws, e);
            break;
        }
        case WIN_CHAR_LIST2: {
            fill_char_list_ids(ws, e, ws->char_list_id,
                               (ws->gd != NULL)
                                   ? ws->gd->party_members_num + 1 : 0);
            break;
        }
        case WIN_LEVEL_TECH_LIST: {
            bool next = (ws->gd != NULL)
                && (ws->gd->chr[ws->character_index % GD_CHARS].techs_b
                    & 0x1F) >= 9;
            fill_level_tech_list(ws, e, 0, next);
            break;
        }
        case WIN_LEVEL_TECH_LIST2: {
            fill_level_tech_list(ws, e, 8, true);
            break;
        }
        case WIN_EQUIP_STATS: {
            fill_equip_stats(ws, e);
            break;
        }
        case WIN_EQP_EQUIP_LIST: {
            fill_strng_equip(ws, e);
            break;
        }
        case WIN_MENU_CHAR_STATS: {
            fill_char_stats(ws, e);
            break;
        }
        case WIN_TELEPORT_PLACES: {
            fill_teleport(ws, e);
            break;
        }
        case WIN_USTVESTIA_SONGS: {
            fill_ustvestia(ws, e);
            break;
        }
        case WIN_PROFILE_CHAR_LIST: {
            fill_profile_list(ws, e);
            break;
        }
        case WIN_REGROUP_CHAR_LIST: {
            fill_regroup_list(ws, e);
            break;
        }
        case WIN_REGROUP_SELECTED: {
            fill_regroup_selected(ws, e);
            break;
        }
        case WIN_STRNG_STATS: {
            fill_strng_stats(ws, e);
            break;
        }
        case WIN_HOUSE_LVEXP: {
            fill_lvexp(ws, e);
            break;
        }
        case WIN_STRNG_LVEXP: {
            fill_lvexp(ws, e);
            break;
        }
        case WIN_STRNG_HPTP: {
            fill_strng_hptp(ws, e);
            break;
        }
        case WIN_STRNG_EQUIP: {
            fill_strng_equip(ws, e);
            break;
        }
        case WIN_FULL_TECH_LIST: {
            fill_tech_list(ws, e, 0);
            break;
        }
        case WIN_FULL_TECH_LIST2: {
            fill_tech_list(ws, e, 1);
            break;
        }
        case WIN_STRNG_CHAR_LIST: {
            fill_store_char_list(ws, e);
            break;
        }
        default: {
            break;
        }
    }
    return true;
}

static void open_frame(WinSys *ws)
{
    WinEntry *e = &ws->stack[ws->depth];
    u16 word    = ws->window_index[0];

    /* loc_978C: the frame's left edge sits half the unrevealed width in,
     * contents stay where they will end up, and `reveal` counts the
     * right border, so reveal - 1 interior cells show. With reveal 0 only
     * the left border is drawn. */
    ws->clip_lo    = ((e->w - ws->reveal) & ~1) / 2;
    ws->clip_hi    = ws->clip_lo + ws->reveal;
    ws->clip_right = (ws->reveal != 0);
    ws->anim_drawn = true;

    ws->reveal += 2;
    ws->steps--;
    if (ws->steps != 0) {
        return;
    }

    ws->anim_drawn = false;
    if (word & WIN_F_REDRAW) {
        paint_scratch(ws);
    } else {
        ws->depth = (ws->depth + 1) & 0xF;
    }
    queue_shift(ws->window_index);
}

static void close_frame(WinSys *ws)
{
    WinEntry *e = &ws->stack[ws->depth];

    if (ws->steps == 1) {
        /* loc_9872's last pass restores what is left. */
        ws->anim_drawn = false;
    } else {
        /* loc_98C2: one column in from each side per frame. */
        int k = ws->reveal / 2;
        ws->clip_lo    = k + 1;
        ws->clip_hi    = e->w - k - 1;
        ws->clip_right = true;
        ws->anim_drawn = true;
    }

    ws->reveal += 2;
    ws->steps--;
    if (ws->steps != 0) {
        return;
    }

    ws->closing = false;

    u16 word = ws->window_index[0];
    u8  left = (u8)((word & 0xFF) - 1);
    if (left == 0) {
        queue_shift(ws->window_index);
    } else {
        ws->window_index[0] = (u16)((word & 0xFF00) | left);
    }
}

/* loc_96EC: with $FFFFDEA8 set this frame (an item page or the
 * Right/Left window being polled) each shown stat moves one toward its
 * target and the three numbers are rewritten into the EquipStats box,
 * at column 8 of rows 2, 4 and 6. The flag is cleared here, so the
 * count runs only while a page keeps setting it. */
static void equip_stats_step(WinSys *ws)
{
    if (!ws->item_page_up) {
        return;
    }
    ws->item_page_up = false;

    WinEntry *e = NULL;
    for (int i = ws->depth - 1; i >= 0; i--) {
        if (ws->stack[i].id == WIN_EQUIP_STATS) {
            e = &ws->stack[i];
            break;
        }
    }
    for (int i = 0; i < 3; i++) {
        if (ws->equip_shown[i] < ws->equip_target[i]) {
            ws->equip_shown[i]++;
        } else if (ws->equip_shown[i] > ws->equip_target[i]) {
            ws->equip_shown[i]--;
        }
        if (e != NULL) {
            put_number(e, 2 + 2 * i, 8, ws->equip_shown[i], 3);
        }
    }
}

void win_process(WinSys *ws)
{
    if (text_check()) {
        text_check_windows(ws);         /* (build 168f) */
    }
    equip_stats_step(ws);

    if (ws->steps != 0) {
        if (ws->closing) {
            close_frame(ws);
        } else {
            open_frame(ws);
        }
        return;
    }

    u16 word = ws->window_index[0];

    if (word == 0) {
        check_run_script(ws);
        return;
    }

    if (word & 0x8000) {
        /* loc_94EE. The first frame only sets the close up. */
        if (ws->depth == 0) {
            queue_shift(ws->window_index);
            return;
        }
        ws->depth = (ws->depth - 1) & 0xF;
        painted_settle(ws);
        /* (build 168d) The text window closing takes its script with it.
         * text_target stays on its place in the stack, as the ROM's does,
         * and the window opened there next (the select screen's menu,
         * after the backup check's messages) is no text window: a change
         * of language typed the old message into it again. */
        if (ws->text_target >= ws->depth) {
            ws->text_script = 0;
        }

        WinEntry *e = &ws->stack[ws->depth];
        ws->steps   = e->w / 2 + 1;
        ws->reveal  = 0;
        if (word & WIN_F_INSTANT) {
            ws->reveal = e->w;
            ws->steps  = 1;
        }
        ws->closing    = true;
        ws->anim_drawn = true;
        ws->clip_lo    = 0;
        ws->clip_hi    = e->w;
        ws->clip_right = true;

        if (!(word & WIN_F_REDRAW)) {
            e->cursor.kind = CURSOR_NONE;
        }
        return;
    }

    /* ProcessWindows (ps2.asm:13502): before a new window is set up, the
     * main camera's x and y are each tested against the 16px grid
     * (`andi.w #$F`) and either off it is loc_9478, a bare rts -- the
     * word waits and is tried again next frame. A window asked for while
     * the screen scrolls under a step opens as the step ends. */
    if (ws->camera_off_grid) {
        return;
    }

    int id = word & 0xFF;

    if (word & WIN_F_REDRAW) {
        /* ProcessWindows' first pass over a word only sets the entry up
         * (for an instant window, `btst #1` then rts, ps2.asm:13570) and
         * the drawing comes a VBlank later -- so CheckPrepareWindows
         * always sees the word, and runs its handler from phase 0,
         * before it is drawn and shifted. Here the drawing is one pass,
         * so a word the prepare pass has not marked yet waits for it.
         * That is only ever a word a window's own handler wrote this
         * frame (Win_BattleTechList's and Win_BattleItemList's NEXT,
         * $0600 | id): shifted at once, window_index_saved stayed 0,
         * nothing polled the list and the command went on without it. */
        if (!(word & WIN_F_PROCESSED)) {
            return;
        }
        /* Drawn over the copy already open; nothing is pushed. A window
         * not on the stack is drawn below as usual and then kept as a
         * painted entry instead of being pushed (paint_scratch). */
        bool found = false;
        for (int i = ws->depth - 1; i >= 0; i--) {
            if (ws->stack[i].id == id) {
                WinCursor keep = ws->stack[i].cursor;
                build_entry(ws, &ws->stack[i], id);
                ws->stack[i].cursor = keep;
                found = true;
                break;
            }
        }
        if (found) {
            queue_shift(ws->window_index);
            return;
        }
    }

    WinEntry *e = &ws->stack[ws->depth];
    if (!build_entry(ws, e, id)) {
        queue_shift(ws->window_index);
        return;
    }

    ws->closing = false;
    ws->steps   = e->w / 2 + 1;
    ws->reveal  = e->w & 1;

    if (word & WIN_F_INSTANT) {
        /* Instant windows set up this field and draw on the next. */
        ws->reveal = e->w;
        ws->steps  = 1;
        return;
    }
    open_frame(ws);
}

/* ------------------------------------------------------------------ */
/* Window handlers -- the main-loop half                              */
/* ------------------------------------------------------------------ */

static bool pressed_bc(const Input *in)
{
    return in->pressed.b || in->pressed.c;
}

static bool pressed_abc(const Input *in)
{
    return in->pressed.a || in->pressed.b || in->pressed.c;
}

/* LoadCursorInWindows, then ObjRRC_Init: the word handed over is the
 * start index in its high byte and the last index in its low byte, and
 * the lamp opens at base_y + 16 * index. */
static void load_cursor_at(WinSys *ws, int index, int max, int x, int y)
{
    WinEntry *e = current_entry(ws);
    if (e == NULL) {
        return;
    }
    if (e >= ws->painted && e < ws->painted + WIN_PAINTED) {
        /* The battle windows spawn their cursor at object slot
         * current_active_objects_num, which is 0 while nothing is
         * pushed, so a new one replaces the old. */
        for (int i = 0; i < ws->painted_count; i++) {
            ws->painted[i].cursor.kind = CURSOR_NONE;
        }
    }
    WinCursor *c = &e->cursor;
    memset(c, 0, sizeof *c);
    c->kind   = CURSOR_RECT;
    c->index  = (u8)index;
    c->max    = (u8)max;
    c->x      = (s16)x;
    c->y      = (s16)(y + 16 * index);
    c->base_y = (s16)y;
    ws->cursor_index = (u16)index;
}

static void load_cursor(WinSys *ws, int max, int x, int y)
{
    load_cursor_at(ws, 0, max, x, y);
}

static u8 top_cursor_max(const WinSys *ws)
{
    return (ws->depth > 0) ? ws->stack[ws->depth - 1].cursor.max : 0;
}

/* loc_11064: C stores the cursor index and lets go of the window; B
 * closes `close` windows and steps the event routine back. Returns
 * which was pressed. */
typedef enum { POLL_NONE, POLL_C, POLL_B } PollKey;

static PollKey list_poll(WinSys *ws, const Input *in, u16 *result,
                         u16 close)
{
    if (in->pressed.c) {
        play(ws, SFX_SELECTION);
        ws->window_index_saved = 0;
        *result = ws->cursor_index;
        return POLL_C;
    }
    if (in->pressed.b) {
        play(ws, SFX_SELECTION);
        win_close_current(ws, close);
        return POLL_B;
    }
    return POLL_NONE;
}

/* loc_11028: a menu's poll. */
static void menu_poll(WinSys *ws, const Input *in, u16 *result, u16 word)
{
    if (!pressed_bc(in)) {
        return;
    }
    ws->window_index[0] = word;
    play(ws, SFX_SELECTION);
    ws->window_index_saved = 0;
    *result = ws->cursor_index;

    if (in->pressed.b) {
        if (ws->event_step != 0) {
            ws->event_done++;
        } else {
            ws->event_routine_sub++;
        }
    }
}

/* loc_FB56: what a button or the timer does at each stop. */
static void text_poll(WinSys *ws, const Input *in)
{
    u8 code = ws->text[ws->text_pos];

    if (code == TXT_PAUSE) {
        if (pressed_abc(in)) {
            play(ws, SFX_SELECTION);
            ws->text_pos++;
        }
    } else if (code == TXT_END) {
        if (pressed_abc(in)) {
            play(ws, SFX_SELECTION);
            ws->window_active_flag = 0;
        }
    } else if (code == TXT_END_NOW) {
        ws->window_active_flag = 0;
    } else if (code == TXT_END_T_C) {
        ws->end_timer--;
        if (ws->end_timer < 0 || pressed_abc(in)) {
            play(ws, SFX_SELECTION);
            ws->end_timer = 0x3C;
            ws->window_active_flag = 0;
            win_open(ws, WIN_CLOSE(1));
        }
    } else if (code == TXT_END_T) {
        ws->end_timer--;
        if (ws->end_timer < 0) {
            ws->end_timer = 0x3C;
            ws->window_active_flag = 0;
        }
    } else if (pressed_abc(in) && !ws->controls_locked) {
        ws->fast = true;
    }
}

static void script_window(WinSys *ws, int phase, int max_line)
{
    if (phase == 1) {
        ws->text_target   = ws->depth - 1;
        ws->text_col      = 0;
        ws->text_line     = 0;
        check_load_script(ws);
        ws->text_max_line = max_line;
        ws->newline_delay = 3;
        ws->end_timer     = 0x3C;
    }
    if (phase >= 1) {
        ws->window_index_saved = 0;
    }
}

/* Win_RolfPortrait / Win_Portraits (ps2.asm:24656): phase 0 points
 * $FFFFF72C at the portrait's art for the draw that follows; every later
 * phase puts the font back and lets go of the window. */
static void portrait_window(WinSys *ws, int phase, u16 base)
{
    if (phase == 0) {
        ws->tile_base = base;
        return;
    }
    ws->tile_base          = WIN_BASE_FONT;
    ws->window_index_saved = 0;
}

static void name_input(WinSys *ws, int phase, const Input *in)
{
    if (phase == 0) {
        return;
    }

    if (phase == 1) {
        if (ws->depth == 0) {
            return;
        }
        WinCursor *c = &ws->stack[ws->depth - 1].cursor;
        memset(c, 0, sizeof *c);
        build_board();                  /* (build 165) */
        c->kind       = CURSOR_NAME;
        c->x          = g_board ? 0xF8 : 0x100;
        c->y          = 0xB0;
        c->blink      = 7;
        c->dest_blink = 7;
        c->frame      = 1;
        ws->cursor_index = 0;
        ws->letter_pos   = 0;
        memset(ws->name_entry, 0, sizeof ws->name_entry);
        return;
    }

    if (!pressed_bc(in)) {
        return;
    }
    play(ws, SFX_SELECTION);

    u8 code = g_name_map[ws->cursor_index % (g_board ? BOARD_CELLS
                                                     : NAME_CELLS)];
    bool advance_only = false;

    if (in->pressed.b) {
        code = 0;                         /* B always rubs out */
    } else if (code == NAME_ADV) {
        advance_only = true;
    } else if (code == NAME_RUB) {
        code = 0;
    }

    /* (build 164) Six letters in a proportional language, his long_names;
     * the cartridge's four otherwise. */
    int max = lang_layout("long_names") ? WIN_NAME_MAX : WIN_NAME_LEN;
    if (!advance_only && code == TXT_END) {
        /* Terminate after the last letter, unless the name is full. */
        if (ws->name_entry[max - 1] == 0) {
            int i = max - 1;
            while (i >= 0 && ws->name_entry[i] == 0) {
                i--;
            }
            ws->name_entry[i + 1] = TXT_END;
        }
        ws->window_index_saved = 0;
        ws->window_index[0]    = WIN_CLOSE(1);
        return;
    }

    if (!advance_only) {
        ws->name_entry[ws->letter_pos] = code;

        /* The original pokes the plane at $412C + 2 * position: row 2,
         * column 22, which is interior column 7 of this window. */
        WinEntry *e = &ws->stack[ws->depth - 1];
        if (lang_charset_full()) {
            /* (build 165) A language with its own letters (the Japanese
             * cartridge's): the name so far a run in its face, a cell a
             * letter. */
            int n = 0;
            while (n < max && ws->name_entry[n] != TXT_END) {
                n++;
            }
            put_run_codes(e, 2, 7, max, ws->name_entry, n);
        } else {
            u8 upper, lower;
            win_glyph(code, &upper, &lower);
            e->cell[1][7 + ws->letter_pos] = upper;
            e->cell[2][7 + ws->letter_pos] = lower;
        }
    }

    if (advance_only || code != 0) {
        if (ws->letter_pos < max - 1) {
            ws->letter_pos++;
        }
    } else if (ws->letter_pos > 0) {
        ws->letter_pos--;
    }
}

/* Win_MenuItemList (ps2.asm:22987). Up to eight items; with nine or
 * more the header reads NEXT, the cursor has nine stops and starts on
 * the first item, and stop 0 opens the second page. */
static void item_list(WinSys *ws, int phase, const Input *in)
{
    if (phase == 0) {
        /* Win_StoreCharList's phase 0 takes the party ids; this one
         * only draws (build_entry). */
        return;
    }
    u8 *bag_count = NULL;
    const u8 *items = (ws->gd != NULL)
        ? gamedata_bag((GameData *)ws->gd, ws->character_index, &bag_count)
        : NULL;
    int count = (bag_count != NULL) ? *bag_count : 0;

    if (phase == 1) {
        if (count - 1 < 8) {
            load_cursor(ws, count - 1, 0xF8, 0x98);
        } else {
            load_cursor_at(ws, 1, 8, 0xF8, 0x88);
        }
        return;
    }

    if (list_poll(ws, in, &ws->item_slot, WIN_CLOSE(1)) != POLL_C) {
        return;
    }
    if (top_cursor_max(ws) == 8) {
        if (ws->item_slot == 0) {
            ws->window_index[0] = WIN_MENU_ITEM_LIST2;
            return;
        }
        ws->item_slot--;
    }
    if (items != NULL) {
        ws->item_index = (u8)(items[ws->item_slot % GD_ITEM_SLOTS] & 0x7F);
    }
}

/* Win_MenuItemList2 (23095): items 8 and up. Stop 0 is NEXT again and
 * goes back a page; B closes both pages. */
static void item_list2(WinSys *ws, int phase, const Input *in)
{
    if (phase == 0) {
        return;
    }
    u8 *bag_count = NULL;
    const u8 *items = (ws->gd != NULL)
        ? gamedata_bag((GameData *)ws->gd, ws->character_index, &bag_count)
        : NULL;
    int count = (bag_count != NULL) ? *bag_count : 0;

    if (phase == 1) {
        load_cursor_at(ws, 1, (count - 8) & 0xFF, 0x100, 0x90);
        return;
    }

    if (list_poll(ws, in, &ws->item_slot, WIN_CLOSE(2)) != POLL_C) {
        return;
    }
    if (ws->item_slot == 0) {
        ws->window_index[0]    = WIN_CLOSE(1);
        ws->window_index_saved = (ws->depth >= 2)
            ? (u16)ws->stack[ws->depth - 2].id : 0;
        return;
    }
    ws->item_slot += 7;
    if (items != NULL) {
        ws->item_index = (u8)(items[ws->item_slot % GD_ITEM_SLOTS] & 0x7F);
    }
}

/* Win_ItemList2 / Win_ItemList3 (ps2.asm:23866, 23932): EQP's two
 * pages of the character's bag. The same shape as the ITEM pages, but
 * further right, and B closes the whole EQP set: four windows from the
 * first page, five from the second. */
static void eqp_item_list(WinSys *ws, int phase, const Input *in, int page)
{
    if (phase == 0) {
        return;
    }
    ws->item_page_up = true;
    u8 *bag_count = NULL;
    const u8 *items = (ws->gd != NULL)
        ? gamedata_bag((GameData *)ws->gd, ws->character_index, &bag_count)
        : NULL;
    int count = (bag_count != NULL) ? *bag_count : 0;

    if (phase == 1) {
        if (page == 0) {
            if (count - 1 < 8) {
                load_cursor(ws, count - 1, 0x148, 0x98);
            } else {
                load_cursor_at(ws, 1, 8, 0x148, 0x88);
            }
        } else {
            load_cursor_at(ws, 1, (count - 8) & 0xFF, 0x150, 0x90);
        }
        return;
    }

    PollKey k = list_poll(ws, in, &ws->item_slot,
                          WIN_CLOSE(page == 0 ? 4 : 5));
    if (k == POLL_B) {
        ws->item_page_up = false;
    }
    if (k != POLL_C) {
        return;
    }
    if (page == 0) {
        if (top_cursor_max(ws) == 8) {
            if (ws->item_slot == 0) {
                ws->window_index[0] = WIN_ITEM_LIST3;
                return;
            }
            ws->item_slot--;
        }
    } else {
        if (ws->item_slot == 0) {
            ws->window_index[0]    = WIN_CLOSE(1);
            ws->window_index_saved = (ws->depth >= 2)
                ? (u16)ws->stack[ws->depth - 2].id : 0;
            return;
        }
        ws->item_slot += 7;
    }
    if (items != NULL) {
        ws->item_index = (u8)(items[ws->item_slot % GD_ITEM_SLOTS] & 0x7F);
    }
}

/* Win_LevelTechList / Win_LevelTechList2 (ps2.asm:23580, 23678): the
 * field technique pages. The count is the low five bits of +$26; nine
 * or more puts NEXT at stop 0 of the first page, and stop 0 of the
 * second goes back. The pick lands in technique_index from the $C800
 * list. */
static void level_tech_list(WinSys *ws, int phase, const Input *in,
                            int page)
{
    if (phase == 0) {
        return;
    }
    int count = (ws->gd != NULL)
        ? (ws->gd->chr[ws->character_index % GD_CHARS].techs_b & 0x1F) : 0;
    u8 known[32];

    if (ws->gd != NULL) {
        gamedata_known_techs(ws->gd, ws->character_index, known);
    } else {
        memset(known, 0, sizeof known);
    }

    if (phase == 1) {
        if (page == 0) {
            if (count - 1 < 8) {
                load_cursor(ws, count - 1, 0xE8, 0x98);
            } else {
                load_cursor_at(ws, 1, 8, 0xE8, 0x88);
            }
        } else {
            load_cursor_at(ws, 1, (count - 8) & 0xFF, 0xF0, 0x90);
        }
        return;
    }

    PollKey k = list_poll(ws, in, &ws->tech_cursor,
                          WIN_CLOSE(page == 0 ? 1 : 2));
    if (k != POLL_C) {
        return;
    }
    if (page == 0) {
        if (top_cursor_max(ws) == 8) {
            if (ws->tech_cursor == 0) {
                ws->window_index[0] = WIN_LEVEL_TECH_LIST2;
                return;
            }
            ws->tech_cursor--;
        }
    } else {
        if (ws->tech_cursor == 0) {
            ws->window_index[0]    = WIN_CLOSE(1);
            ws->window_index_saved = (ws->depth >= 2)
                ? (u16)ws->stack[ws->depth - 2].id : 0;
            return;
        }
        ws->tech_cursor += 7;
    }
    ws->technique_index = (u8)(known[ws->tech_cursor & 0x1F] & 0x3F);
}

/* Win_BattleTechList (24937) and Win_BattleItemList (25049): four to a
 * page. Fewer than five entries put the lamp at $108 over count - 1
 * stops; five or more put NEXT in the top border, the lamp at $F8 from
 * stop 1 over the page's entries (four, or what is left). C on NEXT
 * redraws the next page, wrapping; C on an entry stores it. B closes
 * the list and hands the pad back to the commands window. */
static void battle_list_cursor(WinSys *ws, int count, int page, int x)
{
    int d0 = count - 1;
    int y  = 0x108;
    int start = 0;
    if (d0 >= 4) {
        y = 0xF8;
        d0 -= page;
        start = 1;
        if (d0 >= 4) {
            d0 = 4;
        } else {
            d0 += 1;
        }
    }
    load_cursor_at(ws, start, d0, x, y);
}

static void battle_tech_list_handler(WinSys *ws, int phase, const Input *in)
{
    if (ws->gd == NULL) {
        return;
    }
    const CharData *c = &ws->gd->chr[ws->character_index % GD_CHARS];
    int count = c->techs_a & 0x1F;

    if (phase == 1) {
        battle_list_cursor(ws, count, ws->tech_page, 0xC8);
        return;
    }
    if (phase != 2 || !pressed_bc(in)) {
        return;
    }
    play(ws, SFX_SELECTION);
    ws->window_index_saved = 0;
    int d1 = ws->cursor_index;
    ws->tech_cursor = (u16)d1;
    if (in->pressed.b) {
        ws->window_index[0]     = 0x8001;
        ws->window_index_saved  = WIN_BATTLE_COMMANDS;
        ws->event_routine_sub_2 = 0;
        return;
    }
    if (ws->tech_page != 0 || top_cursor_max(ws) == 4) {
        if (d1 == 0) {
            ws->window_index[0] = (u16)(0x0600 | WIN_BATTLE_TECH_LIST);
            ws->tech_page = (u16)(ws->tech_page + 4);
            if (count <= ws->tech_page) {
                ws->tech_page = 0;
            }
            return;
        }
        ws->tech_cursor--;
    }
    u8 known[32];
    gamedata_known_techs(ws->gd, ws->character_index, known);
    int idx = 16 + ((ws->tech_cursor + ws->tech_page) & 0xF);
    ws->technique_index = (u8)(known[idx] & 0x3F);
}

static void battle_item_list_handler(WinSys *ws, int phase, const Input *in)
{
    if (ws->gd == NULL) {
        return;
    }
    u8 *count;
    const u8 *items = gamedata_bag((GameData *)ws->gd, ws->character_index,
                                   &count);

    if (phase == 1) {
        battle_list_cursor(ws, *count, ws->item_page, 0xA0);
        return;
    }
    if (phase != 2 || !pressed_bc(in)) {
        return;
    }
    play(ws, SFX_SELECTION);
    ws->window_index_saved = 0;
    int d1 = ws->cursor_index;
    ws->item_slot = (u16)d1;
    if (in->pressed.b) {
        ws->window_index[0]     = 0x8001;
        ws->window_index_saved  = WIN_BATTLE_COMMANDS;
        ws->event_routine_sub_2 = 0;
        return;
    }
    if (ws->item_page != 0 || top_cursor_max(ws) == 4) {
        if (d1 == 0) {
            ws->window_index[0] = (u16)(0x0600 | WIN_BATTLE_ITEM_LIST);
            ws->item_page = (u16)(ws->item_page + 4);
            if ((*count & 0x1F) <= ws->item_page) {
                ws->item_page = 0;
            }
            return;
        }
        ws->item_slot--;
    }
    int idx = (ws->item_slot + ws->item_page) % GD_ITEM_SLOTS;
    ws->item_index = (u8)(items[idx] & 0x7F);
}

/* A battle stats slot, 0-3 (WinID_BattleFirstCharStats + slot).
 * loc_10946: a slot past the party becomes the empty-spot art before the
 * draw. */
static void battle_char_stats_handler(WinSys *ws, int phase, int slot)
{
    ws->window_index_saved = 0;
    if (phase == 0 && ws->gd != NULL && slot > ws->gd->party_members_num) {
        ws->window_index[0] = (u16)(ws->window_index[0]
            + (WIN_BATTLE_EMPTY_1 - WIN_BATTLE_CHAR_STATS_1));
    }
}

/* An enemy name or info box: drawn with $500 -- line 0, no priority --
 * then the font base back and the window let go. */
static void enemy_box_handler(WinSys *ws, int phase)
{
    if (phase == 0) {
        ws->tile_base = 0x0500;
    } else {
        ws->tile_base          = WIN_BASE_FONT;
        ws->window_index_saved = 0;
    }
}

static void run_handler(WinSys *ws, int id, int phase, const Input *in)
{
    switch (id) {
        case 0: {
            /* Win_Null */
            if (ws->window_active_flag != 0) {
                text_poll(ws, in);
            }
            check_load_script(ws);
            break;
        }
        case WIN_SCRIPT_MESSAGE: {
            script_window(ws, phase, 2);
            break;
        }
        case WIN_SCRIPT_MESSAGE2: {
            script_window(ws, phase, 2);
            break;
        }
        case WIN_SCRIPT_MESSAGE_BIG: {
            script_window(ws, phase, 6);
            break;
        }
        case WIN_BATTLE_MESSAGE: {
            /* Win_BattleMessage (24904): one text line, home one cell
             * in and one down, $3C on the end timer. (build 163) His
             * battle box's two lines when it opened with two. */
            if (phase == 0) {
                ws->tile_base = WIN_BASE_FONT;
            }
            script_window(ws, phase, (ws->battle_box_lines == 2) ? 2 : 0);
            if (phase == 1 && ws->battle_box_lines == 2) {
                ws->end_timer = 2 * 0x3C;
            }
            break;
        }
        case WIN_BATTLE_CHAR_STATS_1: {
            battle_char_stats_handler(ws, phase, 0);
            break;
        }
        case WIN_BATTLE_CHAR_STATS_1 + 1: {
            battle_char_stats_handler(ws, phase, 1);
            break;
        }
        case WIN_BATTLE_CHAR_STATS_1 + 2: {
            battle_char_stats_handler(ws, phase, 2);
            break;
        }
        case WIN_BATTLE_CHAR_STATS_1 + 3: {
            battle_char_stats_handler(ws, phase, 3);
            break;
        }
        case WIN_BATTLE_EMPTY_1: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_BATTLE_EMPTY_1 + 1: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_BATTLE_EMPTY_1 + 2: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_BATTLE_EMPTY_1 + 3: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_BATTLE_OPTIONS: {
            /* Win_BattleOptions (24759): the battle cursor at $118/$130,
             * two stops; C stores the stop in $FFFFDEDA. B does nothing. */
            if (phase == 1) {
                load_cursor(ws, 1, 0x118, 0x130);
            } else if (phase == 2 && in->pressed.c) {
                play(ws, SFX_SELECTION);
                ws->window_index_saved = 0;
                ws->battle_option      = ws->cursor_index;
            }
            break;
        }
        case WIN_BATTLE_OPTIONS2: {
            /* Win_BattleOptions2 (24791): B or C stores the stop in
             * $FFFFDEDC; B also steps the event routine back two, to
             * the state that reopens FIGHT / STGY. */
            if (phase == 1) {
                load_cursor(ws, 1, 0x118, 0x130);
            } else if (phase == 2 && pressed_bc(in)) {
                play(ws, SFX_SELECTION);
                ws->window_index_saved = 0;
                ws->battle_option2     = ws->cursor_index;
                if (in->pressed.b) {
                    ws->event_routine = (u16)(ws->event_routine - 2);
                }
            }
            break;
        }
        case WIN_BATTLE_CHAR_NAME: {
            /* Drawn and let go at once. */
            ws->window_index_saved = 0;
            break;
        }
        case WIN_BATTLE_ITEM_USED: {
            /* Drawn and let go at once. */
            ws->window_index_saved = 0;
            break;
        }
        case WIN_BATTLE_TECH_USED: {
            /* Drawn and let go at once. */
            ws->window_index_saved = 0;
            break;
        }
        case WIN_BATTLE_COMMANDS: {
            /* Win_BattleCommands (24841): the command triangle at $A8 +
             * $18 a stop, four stops, spawned at slot count - 1; C
             * stores the stop in $FFFFDEE2, B closes it and the name box
             * and steps the event routine back two. */
            if (phase == 1) {
                win_spawn_triangle(ws, TRI_COMMAND, ws->depth - 1, 3,
                                   0xA8, 0x118);
            } else if (phase == 2 && pressed_bc(in)) {
                play(ws, SFX_SELECTION);
                ws->window_index_saved = 0;
                ws->battle_cmd_pick    = ws->cursor_index;
                if (in->pressed.b) {
                    ws->window_index[0] = 0x8402;
                    ws->event_routine   = (u16)(ws->event_routine - 2);
                }
            }
            break;
        }
        case WIN_ENEMY_GROUPS: {
            /* Win_EnemyGroups (24865): the lamp at $140, $108 over two
             * stops; C or B store the stop in $FFFFDEE4 and set
             * character_index_2 to the second group's start for stop 1;
             * B also closes three windows (four from a technique) and
             * steps the event routine back two. */
            if (phase == 1) {
                load_cursor(ws, 1, 0x140, 0x108);
            } else if (phase == 2 && pressed_bc(in)) {
                play(ws, SFX_SELECTION);
                ws->window_index_saved = 0;
                ws->enemy_group_pick   = ws->cursor_index;
                if (in->pressed.b) {
                    ws->window_index[0] = (ws->battle_cmd_pick != 0)
                                        ? 0x8004 : 0x8003;
                    ws->event_routine   = (u16)(ws->event_routine - 2);
                }
                ws->character_index_2 = (u16)((ws->cursor_index != 0)
                                              ? ws->group2_start : 0);
            }
            break;
        }
        case WIN_BATTLE_TECH_LIST: {
            battle_tech_list_handler(ws, phase, in);
            break;
        }
        case WIN_BATTLE_ITEM_LIST: {
            battle_item_list_handler(ws, phase, in);
            break;
        }
        case WIN_ENEMY_NAME_1: {
            enemy_box_handler(ws, phase);
            break;
        }
        case WIN_ENEMY_NAME_2: {
            enemy_box_handler(ws, phase);
            break;
        }
        case WIN_ENEMY_INFO_1: {
            enemy_box_handler(ws, phase);
            break;
        }
        case WIN_ENEMY_INFO_2: {
            enemy_box_handler(ws, phase);
            break;
        }
        case WIN_GAME_SELECT: {
            if (phase == 1) {
                load_cursor(ws, 2, 0xC0, 0xA8);
            } else if (phase == 2) {
                menu_poll(ws, in, &ws->game_select, 0);
            }
            break;
        }
        case WIN_SAVE_SLOTS: {
            if (phase == 0) {
                if (ws->window_index[0] & WIN_F_INSTANT) {
                    ws->window_index_saved = 0;
                }
            } else if (phase == 1) {
                load_cursor(ws, 3, 0x170, 0x118);
            } else {
                menu_poll(ws, in, &ws->save_slot, 0);
            }
            break;
        }
        case WIN_YESNO: {
            /* Win_YesNo (ps2.asm:23287): loc_11064 with $8002, so B
             * closes it and the question under it. */
            if (phase == 1) {
                load_cursor(ws, 1, 0x178, 0xE0);
            } else if (phase == 2) {
                list_poll(ws, in, &ws->yes_no_input, WIN_CLOSE(2));
            }
            break;
        }
        case WIN_YESNO2: {
            if (phase == 1) {
                load_cursor(ws, 1, 0x180, 0x118);
            } else if (phase == 2 && pressed_bc(in)) {
                ws->window_index[0] = WIN_CLOSE(1);
                play(ws, SFX_SELECTION);
                ws->window_index_saved = 0;
                ws->yes_no_input = in->pressed.b ? 1 : ws->cursor_index;
            }
            break;
        }
        case WIN_YESNO3: {
            if (phase == 1) {
                load_cursor(ws, 1, 0x108, 0xE8);
            } else if (phase == 2 && pressed_bc(in)) {
                play(ws, SFX_SELECTION);
                ws->window_index_saved = 0;
                ws->window_index[0]    = WIN_CLOSE(1);
                ws->yes_no_input = in->pressed.b ? 1 : ws->cursor_index;
            }
            break;
        }
        case WIN_NAME_INPUT: {
            name_input(ws, phase, in);
            break;
        }
        case WIN_STORE_MESETA: {
            /* Win_StoreMeseta: the digits are drawn in build_entry; the
             * window itself never waits. */
            ws->window_index_saved = 0;
            break;
        }
        case WIN_USTVESTIA_SONGS: {
            /* Win_UstvestiaSoundtracks: drawn, never waits -- the
             * building reads the pad itself (loc_C80E). */
            ws->window_index_saved = 0;
            break;
        }
        case WIN_BUYSELL: {
            if (phase == 1) {
                load_cursor(ws, 1, 0x168, 0x130);
            } else if (phase == 2) {
                menu_poll(ws, in, &ws->buy_sell, 0);
            }
            break;
        }
        case WIN_STORE_INVENTORY: {
            if (phase == 1) {
                load_cursor(ws, ws->store_max, 0xF0, 0xC0);
            } else if (phase == 2) {
                menu_poll(ws, in, &ws->store_cursor, 0);
                ws->item_index =
                    (u8)(store_item(ws, ws->store_cursor) & 0x7F);
            }
            break;
        }
        case WIN_STORE_CHAR_LIST: {
            if (phase == 0) {
                /* $FFFFC618: the party as it was when the list opened. */
                for (int i = 0; i < 4; i++) {
                    ws->char_list_id[i] = (ws->gd != NULL)
                        ? ws->gd->party_member_id[i] : 0;
                }
            } else if (phase == 1) {
                load_cursor(ws, ws->char_list_cursor, 0x168, 0xC8);
            } else if (phase == 2) {
                menu_poll(ws, in, &ws->char_list_cursor, 0);
                ws->character_index =
                    ws->char_list_id[ws->char_list_cursor % 4];
            }
            break;
        }
        case WIN_MENU_ITEM_LIST: {
            item_list(ws, phase, in);
            break;
        }
        case WIN_LIBRARY_OPTIONS: {
            if (phase == 1) {
                load_cursor(ws, 4, 0x140, 0xA8);
            } else if (phase == 2) {
                menu_poll(ws, in, &ws->library_topic, 0);
            }
            break;
        }
        case WIN_ROOM_OPTIONS: {
            /* Win_RoomOptions puts event_routine back to 4 every frame it
             * waits: the room returns here from either errand. */
            if (phase == 1) {
                load_cursor(ws, 1, 0x90, 0x108);
            } else if (phase == 2) {
                ws->event_routine = 4;
                menu_poll(ws, in, &ws->room_option, 0);
            }
            break;
        }
        case WIN_PLAYER_MENU: {
            /* Win_PlayerMenu: five entries; B closes it and steps the
             * event routine back to 0, which is the menu gone. */
            if (phase == 1) {
                load_cursor(ws, 4, 0x90, 0x90);
            } else if (phase == 2) {
                list_poll(ws, in, &ws->menu_choice, WIN_CLOSE(1));
            }
            break;
        }
        case WIN_STRNG_CHAR_LIST: {
            if (phase == 0) {
                for (int i = 0; i < 4; i++) {
                    ws->char_list_id[i] = (ws->gd != NULL)
                        ? ws->gd->party_member_id[i] : 0;
                }
            } else if (phase == 1) {
                load_cursor(ws, (ws->gd != NULL)
                                ? ws->gd->party_members_num : 0,
                            0xA0, 0xE8);
            } else if (phase == 2) {
                u16 pick = 0;
                PollKey k = list_poll(ws, in, &pick, WIN_CLOSE(1));
                if (k != POLL_NONE) {
                    /* After B loc_1089A reads entry 0: d1 comes back 0. */
                    ws->character_index = ws->char_list_id[pick % 4];
                }
            }
            break;
        }
        case WIN_STRNG_HPTP: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_STRNG_LVEXP: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_FULL_TECH_LIST: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_FULL_TECH_LIST2: {
            /* loc_11064 with $8002: C lets go, B closes both lists and
             * steps back to the stats. */
            if (phase >= 1) {
                list_poll(ws, in, &ws->tech_list2, WIN_CLOSE(2));
            }
            break;
        }
        case WIN_STRNG_EQUIP: {
            /* Win_StrngEquip, loc_11064 with $8004: C lets go, B closes
             * all four and steps back to the party list. */
            if (phase >= 1) {
                list_poll(ws, in, &ws->strng_equip, WIN_CLOSE(4));
            }
            break;
        }
        case WIN_ROLF_HOUSE_OPTIONS: {
            /* Win_RolfHouseOptions: B leaves (option 2) and closes the
             * menu; C picks. */
            if (phase == 1) {
                load_cursor(ws, 2, 0xE8, 0x98);
            } else if (phase == 2 && pressed_bc(in)) {
                play(ws, SFX_SELECTION);
                ws->window_index_saved = 0;
                if (in->pressed.b) {
                    ws->house_option    = 2;
                    ws->window_index[0] = WIN_CLOSE(1);
                } else {
                    ws->house_option = ws->cursor_index;
                }
            }
            break;
        }
        case WIN_PROFILE_CHAR_LIST: {
            if (phase == 1) {
                load_cursor(ws, (ws->gd != NULL)
                                ? ws->gd->party_members_joined : 0,
                            0xD0, 0xB8);
            } else if (phase == 2) {
                /* loc_10676 stores d1 to character_index whatever
                 * happened: the index on C, 0 after B, and the $8001
                 * loc_11060 loaded when nothing was pressed. */
                PollKey k = list_poll(ws, in, &ws->profile_choice,
                                      WIN_CLOSE(1));
                ws->character_index = (k == POLL_C) ? ws->profile_choice
                                    : (k == POLL_B) ? 0 : 0x8001;
            }
            break;
        }
        case WIN_REGROUP_CHAR_LIST: {
            if (phase == 1) {
                load_cursor(ws, ws->regroup_count, 0x90, 0xA0);
            } else if (phase == 2) {
                PollKey k = list_poll(ws, in, &ws->regroup_choice,
                                      WIN_CLOSE(2));
                if (k == POLL_B) {
                    ws->event_routine_sub--;
                } else if (k == POLL_C && ws->gd != NULL) {
                    /* loc_106FE: the chosen line's character, counting
                     * past Nei and anyone in the first three places. */
                    const GameData *gd = ws->gd;
                    int left = ws->regroup_choice;
                    int c    = -1;
                    for (;;) {
                        c++;
                        if (c == 1 || c == gd->party_member_id[0]
                            || c == gd->party_member_id[1]
                            || c == gd->party_member_id[2]) {
                            continue;
                        }
                        if (left-- == 0) {
                            break;
                        }
                    }
                    ws->character_index = (u16)c;
                }
            }
            break;
        }
        case WIN_REGROUP_SELECTED: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_STRNG_STATS: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_HOUSE_LVEXP: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_MENU_CHAR_STATS: {
            /* Draws (build_entry) and never waits. */
            ws->window_index_saved = 0;
            break;
        }
        case WIN_HEAL_CURE: {
            if (phase == 1) {
                load_cursor(ws, 1, 0xF0, 0xB8);
            } else if (phase == 2) {
                menu_poll(ws, in, &ws->heal_cure, 0);
            }
            break;
        }
        case WIN_TOWER_OPTIONS: {
            if (phase == 1) {
                load_cursor(ws, 2, 0x150, 0xC0);
            } else if (phase == 2) {
                menu_poll(ws, in, &ws->tower_option, 0);
            }
            break;
        }
        case WIN_TOWER_OPTIONS2: {
            if (phase == 1) {
                load_cursor(ws, 3, 0x150, 0xC0);
            } else if (phase == 2) {
                menu_poll(ws, in, &ws->tower_option, 0);
            }
            break;
        }
        case WIN_TELEPORT_PLACES: {
            if (phase == 1) {
                load_cursor(ws, ws->teleport_choice, 0x118, 0xC8);
            } else if (phase == 2) {
                menu_poll(ws, in, &ws->teleport_choice, 0);
            }
            break;
        }
        case WIN_MENU_ITEM_LIST2: {
            item_list2(ws, phase, in);
            break;
        }
        case WIN_MENU_ITEM_CHAR: {
            /* Win_MenuItemChar: C names the character; B closes it, or
             * both it and the stats box when TECH opened them. */
            if (phase == 1) {
                load_cursor(ws, (ws->gd != NULL)
                                ? ws->gd->party_members_num : 0,
                            0xA8, 0xD8);
            } else if (phase == 2) {
                u16 pick = 0;
                PollKey k = list_poll(ws, in, &pick,
                                      WIN_CLOSE(ws->menu_choice == 2
                                                    ? 2 : 1));
                if (k != POLL_NONE) {
                    ws->item_char_line = pick;
                    if (ws->gd != NULL) {
                        ws->character_index =
                            ws->gd->party_member_id[pick % GD_PARTY_MAX];
                    }
                }
            }
            break;
        }
        case WIN_CHOSEN_ITEM_CHAR: {
            if (phase == 1) {
                load_cursor(ws, (ws->gd != NULL)
                                ? ws->gd->party_members_num : 0,
                            0x180, 0xD8);
            } else if (phase == 2) {
                u16 pick = 0;
                PollKey k = list_poll(ws, in, &pick, WIN_CLOSE(1));
                if (k != POLL_NONE) {
                    ws->chosen_char_line = pick;
                    if (ws->gd != NULL) {
                        ws->character_index_2 =
                            ws->gd->party_member_id[pick % GD_PARTY_MAX];
                    }
                }
            }
            break;
        }
        case WIN_ITEM_ACTION: {
            if (phase == 1) {
                load_cursor(ws, 2, 0x178, 0x90);
            } else if (phase == 2) {
                list_poll(ws, in, &ws->item_action, WIN_CLOSE(1));
            }
            break;
        }
        case WIN_STATE_ORDER: {
            if (phase == 1) {
                load_cursor(ws, 1, 0x98, 0xE8);
            } else if (phase == 2) {
                list_poll(ws, in, &ws->state_choice, WIN_CLOSE(1));
            }
            break;
        }
        case WIN_CHAR_STATS_1: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_CHAR_STATS_1 + 1: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_CHAR_STATS_1 + 2: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_CHAR_STATS_1 + 3: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_MENU_MESETA: {
            /* Win_MenuMeseta: B closes it and every member's box. */
            if (phase >= 1) {
                list_poll(ws, in, &ws->meseta_dismiss,
                          WIN_CLOSE(2 + ((ws->gd != NULL)
                                             ? ws->gd->party_members_num
                                             : 0)));
            }
            break;
        }
        case WIN_CHAR_ORDER_DEST: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_CHAR_LIST2: {
            /* Win_OrderCharList: B closes it and the destination. */
            if (phase == 1) {
                load_cursor(ws, ws->char_list_cursor, 0x168, 0xC8);
            } else if (phase == 2) {
                list_poll(ws, in, &ws->char_list_cursor, WIN_CLOSE(2));
            }
            break;
        }
        case WIN_LEVEL_TECH_LIST: {
            level_tech_list(ws, phase, in, 0);
            break;
        }
        case WIN_LEVEL_TECH_LIST2: {
            level_tech_list(ws, phase, in, 1);
            break;
        }
        case WIN_EQUIP_STATS: {
            ws->window_index_saved = 0;
            break;
        }
        case WIN_EQP_EQUIP_LIST: {
            /* Win_EqpEquipList: C lets go; B closes one and, on top of
             * CloseCurrentWindow's step back, bumps event_routine_sub. */
            if (phase >= 1) {
                if (list_poll(ws, in, &ws->strng_equip, WIN_CLOSE(1))
                    == POLL_B) {
                    ws->event_routine_sub++;
                }
            }
            break;
        }
        case WIN_ITEM_LIST2: {
            eqp_item_list(ws, phase, in, 0);
            break;
        }
        case WIN_ITEM_LIST3: {
            eqp_item_list(ws, phase, in, 1);
            break;
        }
        case WIN_RIGHT_LEFT: {
            /* Win_RightLeft: C closes it and names the hand, 2 or 3. */
            if (phase >= 1) {
                ws->item_page_up = true;
            }
            if (phase == 1) {
                load_cursor(ws, 1, 0xF8, 0xD8);
            } else if (phase == 2) {
                if (list_poll(ws, in, &ws->right_left, WIN_CLOSE(1))
                    == POLL_C) {
                    ws->window_index[0] = WIN_CLOSE(1);
                    ws->equip_hand      = (u16)(ws->right_left + 2);
                }
            }
            break;
        }
        case WIN_ROLF_PORTRAIT: {
            portrait_window(ws, phase, WIN_BASE_ROLF);
            break;
        }
        case WIN_ROLF_PORTRAIT2: {
            portrait_window(ws, phase, WIN_BASE_ROLF);
            break;
        }
        case WIN_LIBRARY_GRAPH: {
            /* Win_RolfPortrait: the library puts the graph's art where
             * Rolf's usually goes. */
            portrait_window(ws, phase, WIN_BASE_ROLF);
            break;
        }
        default: {
            if (id >= WIN_PROFILE_START && id <= WIN_PROFILE_LAST) {
                /* Win_CharProfiles, loc_11010: B or C lets go. */
                if (pressed_bc(in)) {
                    ws->window_index_saved = 0;
                    play(ws, SFX_SELECTION);
                }
                break;
            }
            /* Every other portrait window is Win_Portraits. */
            if (id > WIN_PORTRAIT_START
                && id < WIN_PORTRAIT_START + WIN_PORTRAIT_COUNT) {
                portrait_window(ws, phase, WIN_BASE_PORTRAIT);
            } else {
                ws->window_index_saved = 0;
            }
            break;
        }
    }
}

/* CheckPrepareWindows. While a window is opening nothing runs here: the
 * first frame does phase 0 and sets the processed bit, and phase 1 waits
 * until ProcessWindows has shifted the queue. */
void win_prepare(WinSys *ws, const Input *in)
{
    u16 word = ws->window_index[0];

    if (word != 0) {
        if (word & 0x8000) {
            return;
        }
        if (word & WIN_F_PROCESSED) {
            return;
        }
        ws->window_index[0]    = (u16)(word | WIN_F_PROCESSED);
        ws->window_index_saved = (u16)(word & 0xFF);
        ws->window_routine     = 0;
    }

    run_handler(ws, ws->window_index_saved, ws->window_routine, in);

    if (ws->window_routine != 2) {
        ws->window_routine++;
    }
}

/* ------------------------------------------------------------------ */
/* Cursor objects                                                     */
/* ------------------------------------------------------------------ */

static bool cursor_active(const WinSys *ws, int slot)
{
    return ws->window_index[0] == 0
        && ws->window_index_saved != 0
        && slot == ws->depth - 1;
}

static void rect_move(WinSys *ws, WinCursor *c, int dir)
{
    int idx = c->index + dir;
    if (idx < 0) {
        idx = c->max;
    } else if (idx > c->max) {
        idx = 0;
    }
    c->index  = (u8)idx;
    c->y      = (s16)(c->base_y + idx * 16);
    c->repeat = 0x1F;
    ws->cursor_index = c->index;
}

static void run_rect(WinSys *ws, WinCursor *c, const Input *in, bool active)
{
    if (!active) {
        c->hidden = false;
        return;
    }

    c->blink--;
    if (c->blink < 0) {
        c->blink  = 7;
        c->hidden = !c->hidden;
    }

    if (in->pressed.up) {
        rect_move(ws, c, -1);
    } else if (in->pressed.down) {
        rect_move(ws, c, +1);
    } else if (in->held.up) {
        c->repeat--;
        if (c->repeat < 0) {
            rect_move(ws, c, -1);
        }
    } else if (in->held.down) {
        c->repeat--;
        if (c->repeat < 0) {
            rect_move(ws, c, +1);
        }
    } else {
        c->repeat = 0x1F;
        ws->cursor_index = c->index;
    }
}

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } NameDir;

/* ObjIWBC_Main's four moves, byte arithmetic included. */
/* (build 165) A language's board: left and right to the row's next stop
 * round, up and down a row round, to the stop at the column or the
 * nearest to it, left first. The port's own (the Japanese cartridge's
 * movement is not read). */
static bool board_stop(int row, int col)
{
    return g_name_map[row * BOARD_COLS + col] != 0;
}

static void board_move(WinSys *ws, WinCursor *c, NameDir dir)
{
    int row = c->index / BOARD_COLS;
    int col = c->index % BOARD_COLS;
    switch (dir) {
        case DIR_LEFT: {
            for (int i = 0; i < BOARD_COLS; i++) {
                col = (col + BOARD_COLS - 1) % BOARD_COLS;
                if (board_stop(row, col)) {
                    break;
                }
            }
            break;
        }
        case DIR_RIGHT: {
            for (int i = 0; i < BOARD_COLS; i++) {
                col = (col + 1) % BOARD_COLS;
                if (board_stop(row, col)) {
                    break;
                }
            }
            break;
        }
        case DIR_UP: {
            row = (row + BOARD_ROWS - 1) % BOARD_ROWS;
            break;
        }
        case DIR_DOWN: {
            row = (row + 1) % BOARD_ROWS;
            break;
        }
    }
    for (int d = 0; d < BOARD_COLS && !board_stop(row, col); d++) {
        if (col - d >= 0 && board_stop(row, col - d)) {
            col -= d;
        } else if (col + d < BOARD_COLS && board_stop(row, col + d)) {
            col += d;
        }
    }
    int idx = row * BOARD_COLS + col;
    c->index = (u8)idx;
    ws->cursor_index = (u16)idx;
    c->y      = (s16)(0xB0 + row * 16);
    c->x      = (s16)(0xF8 + col * 8);
    c->frame  = (g_name_map[idx] >= NAME_ADV) ? 2 : 1;
    c->repeat = 0xF;
}

static void name_move(WinSys *ws, WinCursor *c, NameDir dir)
{
    if (g_board) {
        board_move(ws, c, dir);
        return;
    }
    int  idx  = c->index;
    int  row  = idx / NAME_COLS;
    bool scan_forward = false;

    switch (dir) {
        case DIR_UP: {
            idx -= NAME_COLS;
            if (idx < 0) {
                idx += NAME_CELLS;
            }
            break;
        }
        case DIR_DOWN: {
            idx += NAME_COLS;
            if (idx >= NAME_CELLS) {
                idx -= NAME_CELLS;
            }
            break;
        }
        case DIR_LEFT: {
            idx = (idx - 1) & 0xFF;
            if (idx / NAME_COLS != row) {
                idx = (idx + NAME_COLS) & 0xFF;
            }
            break;
        }
        case DIR_RIGHT: {
            idx += 1;
            if (idx == 0x31) {
                idx -= 0xF;          /* past Z: back to S */
            }
            if (idx / NAME_COLS != row) {
                idx -= NAME_COLS;
            }
            scan_forward = true;
            break;
        }
    }

    if (scan_forward) {
        while (idx < NAME_CELLS && g_name_map[idx] == 0) {
            idx++;
        }
        if (idx >= NAME_CELLS) {
            idx = 0x33;
        }
    } else {
        while (idx > 0 && g_name_map[idx] == 0) {
            idx--;
        }
    }

    c->index = (u8)idx;
    ws->cursor_index = (u16)idx;
    c->y      = (s16)(0xB0 + (idx / NAME_COLS) * 24);
    c->x      = (s16)(0x100 + (idx % NAME_COLS) * 8);
    c->frame  = (g_name_map[idx] >= NAME_ADV) ? 2 : 1;
    c->repeat = 0xF;
}

static void run_name(WinSys *ws, WinCursor *c, const Input *in, bool active)
{
    /* The destination tile first: it is the next object slot along. */
    if (active) {
        c->dest_blink--;
        if (c->dest_blink < 0) {
            c->dest_blink  = 7;
            c->dest_hidden = !c->dest_hidden;
        }
    }

    if (!active) {
        c->hidden = false;
        return;
    }

    c->blink--;
    if (c->blink < 0) {
        c->blink  = 7;
        c->hidden = !c->hidden;
    }

    if (in->held.c) {
        return;
    }

    static const NameDir ORDER[4] = { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
    const bool pressed[4] = { in->pressed.up, in->pressed.down,
                              in->pressed.left, in->pressed.right };
    const bool held[4]    = { in->held.up, in->held.down,
                              in->held.left, in->held.right };

    for (int i = 0; i < 4; i++) {
        if (pressed[i]) {
            name_move(ws, c, ORDER[i]);
            return;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (held[i]) {
            c->repeat--;
            if (c->repeat < 0) {
                name_move(ws, c, ORDER[i]);
            }
            return;
        }
    }
    c->repeat = 0xF;
}

/* ObjTCCSel_XPosArray: the party cursor's x by stop, from the second
 * entry when the party is one or two. */
static void triangle_place(WinSys *ws, WinTriangle *t)
{
    static const s16 XPOS[4] = { 0x90, 0xD0, 0x140, 0x180 };
    if (t->kind == TRI_CHAR_SELECT) {
        int i = t->index;
        if (ws->gd != NULL && ws->gd->party_members_num < 2) {
            i++;
        }
        t->x = XPOS[i & 3];
    } else {
        t->x = (s16)(t->base_x + t->index * 0x18);
    }
}

/* loc_1906: left and right, with the held repeat on $28; the stop is
 * written to $FFFFDE50 on every frame it is read. */
static void triangle_move(WinSys *ws, WinTriangle *t, const Input *in)
{
    int d1 = t->index;
    if (in->pressed.left) {
        d1 = (d1 == 0) ? t->max : d1 - 1;
    } else if (in->pressed.right) {
        d1 = (d1 + 1 > t->max) ? 0 : d1 + 1;
    } else if (in->held.left) {
        if (--t->repeat < 0) {
            d1 = (d1 == 0) ? t->max : d1 - 1;
        } else {
            return;
        }
    } else if (in->held.right) {
        if (--t->repeat < 0) {
            d1 = (d1 + 1 > t->max) ? 0 : d1 + 1;
        } else {
            return;
        }
    }
    t->index  = (u8)d1;
    t->repeat = 0x1F;
    ws->cursor_index = t->index;
}

static void run_triangle(WinSys *ws, WinTriangle *t, const Input *in)
{
    bool active;
    if (t->kind == TRI_CHAR_SELECT) {
        active = (t->slot == ws->depth + 1);
    } else {
        active = ws->window_index[0] == 0 && ws->window_index_saved != 0
              && t->slot == ws->depth - 1;
    }
    if (!active) {
        t->hidden = true;
        return;
    }
    if (--t->blink < 0) {
        t->blink  = 7;
        t->hidden = !t->hidden;
    }
    triangle_move(ws, t, in);
    triangle_place(ws, t);
}

void win_run_cursors(WinSys *ws, const Input *in)
{
    for (int i = 0; i < 2; i++) {
        if (ws->tri[i].kind != TRI_NONE) {
            run_triangle(ws, &ws->tri[i], in);
        }
    }

    for (int i = 0; i < ws->painted_count; i++) {
        WinCursor *c = &ws->painted[i].cursor;
        if (c->kind != CURSOR_RECT) {
            continue;
        }
        /* ObjRRCB_Main: blinks and moves only while its window is the
         * one being polled; otherwise it stays lit where it was. */
        bool active = ws->window_index[0] == 0
                   && (ws->window_index_saved & 0xFF) == ws->painted[i].id;
        run_rect(ws, c, in, active);
    }

    for (int i = 0; i < ws->depth; i++) {
        WinCursor *c = &ws->stack[i].cursor;
        bool active  = cursor_active(ws, i);

        if (c->kind == CURSOR_RECT) {
            run_rect(ws, c, in, active);
        } else if (c->kind == CURSOR_NAME) {
            run_name(ws, c, in, active);
        }
    }
}

int win_debug_text(const WinSys *ws, u8 *out, int max)
{
    int n = 0;
    while (n < max && n < WIN_TEXT_BUF) {
        u8 code = ws->text[n];
        out[n++] = code;
        if (code >= TXT_END) {
            break;
        }
    }
    return n;
}

bool win_debug_cursor(const WinSys *ws, WinCursorInfo *out)
{
    memset(out, 0, sizeof *out);

    for (int i = 0; i < 2; i++) {
        const WinTriangle *t = &ws->tri[i];
        bool active;
        if (t->kind == TRI_CHAR_SELECT) {
            active = (t->slot == ws->depth + 1);
        } else if (t->kind == TRI_COMMAND) {
            active = ws->window_index[0] == 0 && ws->window_index_saved != 0
                  && t->slot == ws->depth - 1;
        } else {
            active = false;
        }
        if (active) {
            out->stop   = t->index;
            out->stops  = t->max + 1;
            out->across = true;
            out->party  = (t->kind == TRI_CHAR_SELECT);
            return true;
        }
    }

    for (int i = 0; i < ws->painted_count; i++) {
        const WinCursor *c = &ws->painted[i].cursor;
        if (c->kind == CURSOR_RECT && ws->window_index[0] == 0
            && (ws->window_index_saved & 0xFF) == ws->painted[i].id) {
            out->stop  = c->index;
            out->stops = c->max + 1;
            return true;
        }
    }

    for (int i = 0; i < ws->depth; i++) {
        const WinCursor *c = &ws->stack[i].cursor;
        if (c->kind == CURSOR_RECT && cursor_active(ws, i)) {
            out->stop  = c->index;
            out->stops = c->max + 1;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* (build 166) A change of language                                   */
/* ------------------------------------------------------------------ */

/* A window built again in place: its map, labels and contents from the
 * game's state in the new language (build_entry, as a redraw builds it),
 * its cursor, its place over the stack and its tile base kept. Built into
 * a copy, so one the window system cannot build stays as it was; a
 * frameless text (the game over's, the ending's) has no map, its text all
 * it holds. */
static void rebuild_entry(WinSys *ws, WinEntry *e)
{
    if (e->frameless) {
        return;
    }
    static WinEntry fresh;
    u16 saved = ws->tile_base;
    ws->tile_base = e->base;
    g_rebuilding  = true;
    bool ok = build_entry(ws, &fresh, e->id);
    g_rebuilding  = false;
    ws->tile_base = saved;
    if (!ok) {
        return;
    }
    fresh.cursor = e->cursor;
    fresh.over   = e->over;
    *e = fresh;
}

/* The text window's script again in the new language: typed again from
 * its start while it is still being shown (the event code still waits on
 * it), or, finished, shown whole -- every page passed at once to its end
 * code, as the last page stood. */
static void retype_text(WinSys *ws)
{
    WinEntry *e = text_entry(ws);
    int shown = ws->depth + (ws->anim_drawn ? 1 : 0);
    if (e == NULL || ws->text_script == 0 || ws->text_target >= shown) {
        return;
    }
    bool active = ws->window_active_flag != 0;
    /* The battle box's lines as its language's box has them (his two,
     * the stock one). */
    if (e->id == WIN_BATTLE_MESSAGE) {
        ws->text_max_line = (ws->battle_box_lines == 2) ? 2 : 0;
    }
    text_clear(ws);
    ws->text_col  = 0;
    ws->text_line = 0;
    expand_script(ws, ws->text_script,
                  (ws->script_text != NULL) ? ws->script_text
                                            : ws->script_lookup, false);
    ws->newline_delay = 3;
    ws->fast          = false;
    if (active) {
        return;
    }
    for (int guard = 0; guard < 2 * WIN_TEXT_BUF; guard++) {
        u8 code = ws->text[ws->text_pos];
        if (code >= TXT_END || ws->text_pos >= WIN_TEXT_BUF - 1) {
            break;
        }
        if (code == TXT_PAUSE) {
            ws->text_pos++;
            continue;
        }
        ws->newline_delay = 0;
        run_script_step(ws);
    }
    ws->newline_delay = 3;
}

void win_language_changed(WinSys *ws)
{
    for (int i = 0; i < ws->painted_count; i++) {
        rebuild_entry(ws, &ws->painted[i]);
    }
    int shown = ws->depth + (ws->anim_drawn ? 1 : 0);
    for (int i = 0; i < shown && i < WIN_STACK; i++) {
        WinEntry *e = &ws->stack[i];
        rebuild_entry(ws, e);
        if (e->id == WIN_NAME_INPUT) {
            /* The new language's board; the letters begun on the old
             * one are its codes, and go. */
            build_board();
            memset(ws->name_entry, 0, sizeof ws->name_entry);
            ws->letter_pos   = 0;
            ws->cursor_index = 0;
            e->cursor.index  = 0;
            e->cursor.x      = g_board ? 0xF8 : 0x100;
            e->cursor.y      = 0xB0;
            e->cursor.frame  = 1;
        }
    }
    retype_text(ws);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

/* A frame cell. loc_978C (ps2.asm, the window frame writer) loads $8500 --
 * priority, line 0 -- for a window on plane A, and $500, no priority, when
 * bit 13 of the position word puts it on plane B. The four enemy name and
 * info boxes are the plane B ones: the enemies' own cells are plane A with
 * no priority, so an arm reaching up over a box is drawn over it. Up to
 * build 100b every frame cell had priority, and a box's border covered it. */
static void put(Video *v, LayerId layer, int font_first, int col, int row,
                u8 tile)
{
    Cell cell;
    memset(&cell, 0, sizeof cell);
    cell.tile = (u16)(font_first + tile);
    cell.prio = (layer == LAYER_A) ? 1 : 0;
    video_set_tile(v, layer, col, row, cell);
}

/* An interior cell: loc_980A puts the map byte in the low half of the
 * window's $FFFFF72C, so the high half carries priority, palette line
 * and which $100 tiles the byte indexes. */
static void put_based(Video *v, LayerId layer, u16 base, int col, int row,
                      u8 tile)
{
    Cell cell;
    memset(&cell, 0, sizeof cell);
    cell.tile = (u16)((base & 0x07FF) + tile);
    cell.pal  = (u8)((base >> 13) & 3);
    cell.prio = (u8)(base >> 15);
    video_set_tile(v, layer, col, row, cell);
}

/* (build 163) The text pool (window.h). */
static TileSet *g_pool;
static int      g_pool_first;
static int      g_pool_count;
static int      g_pool_used;

void win_text_pool(TileSet *vram, int first, int count)
{
    g_pool       = vram;
    g_pool_first = first;
    g_pool_count = count;
    g_pool_used  = 0;
}

void win_text_pool_reset(void)
{
    g_pool_used = 0;
}

int win_text_pool_take(int n)
{
    if (g_pool == NULL || n <= 0 || g_pool_used + n > g_pool_count
        || g_pool_first + g_pool_used + n > g_pool->count) {
        static bool told;
        if (g_pool != NULL && !told) {
            told = true;
            fprintf(stderr, "window: the text pool's %d tiles are spent; "
                            "a run is not drawn\n", g_pool_count);
        }
        return -1;
    }
    int at = g_pool_first + g_pool_used;
    g_pool_used += n;
    return at;
}

Tile *win_text_pool_tile(int index)
{
    if (g_pool == NULL || index < 0 || index >= g_pool->count) {
        return NULL;
    }
    return &g_pool->tiles[index];
}

int win_text_draw(const u8 *codes, int n, int cells, const Tile *paper)
{
    int first = win_text_pool_take(cells);
    if (first < 0) {
        return -1;
    }
    Tile *strip = &g_pool->tiles[first];
    for (int c = 0; c < cells; c++) {
        if (paper != NULL) {
            strip[c] = paper[c];
        } else {
            memset(strip[c].px, 0, sizeof strip[c].px);
        }
    }
    lang_draw(strip, cells, 0, codes, n, 1);
    return first;
}

int win_text_draw_marks(const u8 *codes, int n, int cells, const Tile *paper,
                        bool *inked)
{
    int first = win_text_pool_take(cells);
    if (first < 0) {
        return -1;
    }
    Tile *strip = &g_pool->tiles[first];
    Tile  marks[WIN_MAX_COLS];
    memset(marks, 0, sizeof marks);
    lang_draw_upper(marks, cells, 0, codes, n, 1);
    for (int c = 0; c < cells; c++) {
        strip[c] = paper[c];
        inked[c] = false;
        for (int p = 0; p < TILE_W * TILE_H; p++) {
            if (marks[c].px[p] != 0) {
                strip[c].px[p] = 1;
                inked[c] = true;
            }
        }
    }
    return first;
}

/* (build 163) A pool tile in an interior cell: the window's palette line
 * and priority, as put_based gives its font tiles. */
static void put_pool(Video *v, LayerId layer, u16 base, int col, int row,
                     int tile)
{
    Cell cell;
    memset(&cell, 0, sizeof cell);
    cell.tile = (u16)tile;
    cell.pal  = (u8)((base >> 13) & 3);
    cell.prio = (u8)(base >> 15);
    video_set_tile(v, layer, col, row, cell);
}

/* (build 165) A two-row face's upper glyphs -- the Japanese cartridge's
 * dakuten and handakuten -- over the row above a run: only the cells they
 * have ink in take a tile (the paper the cell's own, as the run's), so a
 * row above that holds something else -- a label of its own, the border --
 * shows through where the run's letters have no mark. */
static void draw_upper(const WinEntry *e, Video *v, int font_first,
                       const WinRun *r, int cells, int col0, int row0,
                       int lo, int hi)
{
    int row = r->row - 1;
    if (row < 0) {
        return;
    }
    Tile marks[WIN_MAX_COLS];
    memset(marks, 0, sizeof marks);
    lang_draw_upper(marks, cells, 0, r->code, r->len, 1);

    const TileSet *ts     = v->tileset;
    bool           border = !e->frameless && row == 0;
    int            base   = border ? font_first : (e->base & 0x07FF);
    for (int c = 0; c < cells; c++) {
        bool ink = false;
        for (int p = 0; p < TILE_W * TILE_H && !ink; p++) {
            ink = marks[c].px[p] != 0;
        }
        int fc = r->col + c + 1;
        if (!ink || fc <= lo || fc >= hi) {
            continue;
        }
        int tile = win_text_pool_take(1);
        Tile *t  = win_text_pool_tile(tile);
        if (t == NULL) {
            return;
        }
        u8  cell  = e->cell[row][r->col + c];
        int paper = base + ((cell != 0) ? cell : TILE_FILL);
        if (ts != NULL && paper < ts->count) {
            *t = ts->tiles[paper];
        } else {
            memset(t->px, 0, sizeof t->px);
        }
        for (int p = 0; p < TILE_W * TILE_H; p++) {
            if (marks[c].px[p] != 0) {
                t->px[p] = 1;
            }
        }
        if (border) {
            Cell out;
            memset(&out, 0, sizeof out);
            out.tile = (u16)tile;
            out.prio = (e->layer == LAYER_A) ? 1 : 0;
            video_set_tile(v, (LayerId)e->layer, col0 + fc, row0 + row, out);
        } else {
            put_pool(v, (LayerId)e->layer, e->base, col0 + fc, row0 + row,
                     tile);
        }
    }
}

/* (build 163) The entry's runs over its cells: each drawn into pool tiles
 * on the paper its cells show, the frame columns lo + 1 .. hi - 1 of
 * them put down. (build 164) A run on a border row -- a prompt there,
 * WHO? -- is the font's, as the border row's cells are (put). */
static void draw_runs(const WinEntry *e, Video *v, int font_first,
                      int col0, int row0, int lo, int hi)
{
    const TileSet *ts = v->tileset;
    for (int i = 0; i < e->runs; i++) {
        const WinRun *r = &e->run[i];
        if (r->len == 0 || r->row >= e->rows) {
            continue;
        }
        bool border = !e->frameless && (r->row == 0 || r->row == e->rows - 1);
        int  base   = border ? font_first : (e->base & 0x07FF);
        /* Only the cells the glyphs reach, as his engine gives only the
         * cells with ink a tile: the rest are the window's own. */
        int cells = r->cells;
        if (r->col + cells > e->w - 1) {
            cells = e->w - 1 - r->col;
        }
        int reach = (lang_measure(r->code, r->len) + TILE_W - 1) / TILE_W;
        if (cells > reach) {
            cells = reach;
        }
        if (cells <= 0) {
            continue;
        }
        /* The paper: each cell's tile -- the fill, in a frameless text's
         * cells the typing has not reached, as its spaces would write. */
        Tile paper[WIN_MAX_COLS];
        for (int c = 0; c < cells; c++) {
            u8  cell = e->cell[r->row][r->col + c];
            int t    = base + ((cell != 0) ? cell : TILE_FILL);
            if (ts != NULL && t < ts->count) {
                paper[c] = ts->tiles[t];
            } else {
                memset(paper[c].px, 0, sizeof paper[c].px);
            }
        }
        int first = win_text_draw(r->code, r->len, cells, paper);
        if (first < 0) {
            continue;
        }
        if (lang_tall()) {
            draw_upper(e, v, font_first, r, cells, col0, row0, lo, hi);
        }
        for (int c = 0; c < cells; c++) {
            int fc = r->col + c + 1;
            if (fc <= lo || fc >= hi) {
                continue;
            }
            if (border) {
                Cell cell;
                memset(&cell, 0, sizeof cell);
                cell.tile = (u16)(first + c);
                cell.prio = (e->layer == LAYER_A) ? 1 : 0;
                video_set_tile(v, (LayerId)e->layer, col0 + fc,
                               row0 + r->row, cell);
            } else {
                put_pool(v, (LayerId)e->layer, e->base, col0 + fc,
                         row0 + r->row, first + c);
            }
        }
    }
}

/* (build 142) A portrait window's picture over the interior cells this
 * draw puts down -- frame columns lo + 1 .. hi - 1 and rows 1 .. rows - 2
 * -- with its top left at the picture's `at` cell of the frame. */
static void bind_picture(const WinSys *ws, const WinEntry *e, Video *v,
                         int col0, int row0, int lo, int hi)
{
    if (ws->picture == NULL || e->frameless) {
        return;
    }
    u16 first = 0;
    u16 count = 0;
    const Picture *p = ws->picture(ws->ctx, e->id, &first, &count);
    if (p == NULL) {
        return;
    }
    VideoPicture b;
    memset(&b, 0, sizeof b);
    b.picture    = p;
    b.first_tile = first;
    b.count      = count;
    b.layer      = e->layer;
    b.col        = (s16)(col0 + p->at_col);
    b.row        = (s16)(row0 + p->at_row);
    b.col_lo     = (s16)(lo + 1 - p->at_col);
    b.col_hi     = (s16)(hi - 1 - p->at_col);
    b.row_lo     = (s16)(1 - p->at_row);
    b.row_hi     = (s16)(e->rows - 2 - p->at_row);
    video_picture_bind(v, &b);
}

/* Frame columns run 0..w: 0 is the left border, w the right, and
 * interior column j is frame column j + 1. [lo, hi] is what shows. */
static void draw_entry(const WinSys *ws, const WinEntry *e, Video *v,
                       int font_first, int cam_col, int cam_row, int lo,
                       int hi, bool right_border)
{
    int col0 = cam_col + e->col;
    int row0 = cam_row + e->row;
    LayerId layer = (LayerId)e->layer;

    bind_picture(ws, e, v, col0, row0, lo, hi);

    if (e->frameless) {
        for (int r = 1; r < e->rows - 1; r++) {
            for (int fc = 1; fc < e->w; fc++) {
                u8 t = e->cell[r][fc - 1];
                if (t != 0) {
                    put_based(v, layer, e->base, col0 + fc, row0 + r, t);
                }
            }
        }
        draw_runs(e, v, font_first, col0, row0, 0, e->w);
        return;
    }

    for (int r = 0; r < e->rows; r++) {
        bool top    = (r == 0);
        bool bottom = (r == e->rows - 1);

        u8 left  = top ? TILE_TL : (bottom ? TILE_BL : TILE_LEFT);
        u8 right = top ? TILE_TR : (bottom ? TILE_BR : TILE_RIGHT);

        put(v, layer, font_first, col0 + lo, row0 + r, left);
        for (int fc = lo + 1; fc < hi; fc++) {
            /* The border rows and columns are always the font's
             * (loc_978C loads $8500 for them itself, $500 on plane B). */
            if (top || bottom) {
                put(v, layer, font_first, col0 + fc, row0 + r,
                    e->cell[r][fc - 1]);
            } else {
                put_based(v, layer, e->base, col0 + fc, row0 + r,
                          e->cell[r][fc - 1]);
            }
        }
        if (right_border) {
            put(v, layer, font_first, col0 + hi, row0 + r, right);
        }
    }
    draw_runs(e, v, font_first, col0, row0, lo, hi);
}

static void push_tile(Video *v, int font_first, int x, int y, int tile,
                      int w)
{
    Sprite s;
    memset(&s, 0, sizeof s);
    s.x    = (s16)(x - SPRITE_BIAS);
    s.y    = (s16)(y - SPRITE_BIAS);
    s.tile = (u16)(font_first + tile);
    s.w    = (u8)w;
    s.h    = 1;
    s.prio = 1;
    video_sprite_push(v, &s);
}

static void draw_cursor(const WinSys *ws, const WinCursor *c, Video *v,
                        int font_first)
{
    if (c->kind == CURSOR_RECT) {
        if (!c->hidden) {
            /* Map_Cursor_Rectangle: one 2x1 piece at the origin. */
            push_tile(v, font_first, c->x, c->y, TILE_LAMP, 2);
        }
        return;
    }

    if (c->kind != CURSOR_NAME) {
        return;
    }

    if (!c->hidden) {
        if (c->frame == 2) {
            /* Map_Cursor_InputTileBig: five fill tiles in a row. */
            for (int i = 0; i < 5; i++) {
                push_tile(v, font_first, c->x + i * 8, c->y,
                          TILE_NAME_BLINK, 1);
            }
        } else {
            /* Map_Cursor_InputTile: the cell and the one above it. */
            push_tile(v, font_first, c->x, c->y - 8, TILE_NAME_BLINK, 1);
            push_tile(v, font_first, c->x, c->y,     TILE_NAME_BLINK, 1);
        }
    }

    if (!c->dest_hidden) {
        /* ObjQMBC_Main: $130 + 8 * position, $98. */
        push_tile(v, font_first, 0x130 + ws->letter_pos * 8, 0x98,
                  TILE_NAME_DEST, 1);
    }
}

void win_render(const WinSys *ws, Video *v, int font_first,
                int cam_col, int cam_row)
{
    /* (build 142) In the order the ROM wrote them into the nametable:
     * the painted entries drawn over d stack windows come after stack
     * window d - 1 and before stack window d. Until 142 every painted
     * entry came first, under the whole stack, and the tower's copy
     * portrait stayed hidden under the outside one it was drawn over. */
    for (int d = 0; d <= ws->depth; d++) {
        for (int i = 0; i < ws->painted_count; i++) {
            const WinEntry *e = &ws->painted[i];
            int over = (e->over < ws->depth) ? e->over : ws->depth;
            if (over != d) {
                continue;
            }
            draw_entry(ws, e, v, font_first, cam_col, cam_row, 0, e->w,
                       true);
            draw_cursor(ws, &e->cursor, v, font_first);
        }
        if (d < ws->depth) {
            const WinEntry *e = &ws->stack[d];
            draw_entry(ws, e, v, font_first, cam_col, cam_row, 0, e->w,
                       true);
            draw_cursor(ws, &e->cursor, v, font_first);
        }
    }

    if (ws->anim_drawn) {
        draw_entry(ws, &ws->stack[ws->depth], v, font_first, cam_col,
                   cam_row, ws->clip_lo, ws->clip_hi, ws->clip_right);
    }

    for (int i = 0; i < 2; i++) {
        const WinTriangle *t = &ws->tri[i];
        if (t->kind == TRI_NONE || t->hidden) {
            continue;
        }
        Sprite s;
        memset(&s, 0, sizeof s);
        s.x    = (s16)(t->x - SPRITE_BIAS);
        s.y    = (s16)(t->y - SPRITE_BIAS);
        s.tile = (u16)(font_first + TILE_TRIANGLE);
        s.w    = 2;
        s.h    = 2;
        s.prio = 1;
        video_sprite_push(v, &s);
    }
}
