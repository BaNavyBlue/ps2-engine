#ifndef PS2_WINDOW_H
#define PS2_WINDOW_H

#include "../core/video.h"
#include "../core/winmap.h"
#include "../platform/platform.h"

struct GameData;

/* The general window system: ProcessWindows (ps2.asm:13502),
 * CheckPrepareWindows (22755), RunScript (13693), LoadScript (25476) and
 * the three cursor objects (1916, 2020, 2208).
 *
 * What is kept from the original is its control flow, because the event
 * code is written against it: a queue of window commands, a stack of open
 * windows, a queue of script ids, and three conditions -- window_index,
 * window_index_saved, window_active_flag -- that every event routine
 * waits on before it runs its next state.
 *
 * What is dropped is the saved-background buffer at $FFFFDE00. The
 * original reads the nametable back before drawing a window so it can put
 * it back on close. The port rebuilds both planes every frame, so an open
 * window is simply drawn last and a closed one is simply not drawn. For
 * the same reason text is written into the window's own cell grid rather
 * than into the plane. */

#define WIN_QUEUE      8
#define WIN_STACK      16
#define WIN_MAX_COLS   32           /* widest interior: MenuCharStats, 27 */
#define WIN_MAX_ROWS   18
#define WIN_TEXT_COLS  24           /* RunScript's scroll width, $17 + 1 */
/* LoadScript's text, run-ons and all. 1024 until build 87, when Lutz's
 * $1690-$1694 (five run-on scripts, about a thousand codes -- the text
 * that overruns sound RAM on the cartridge, the music freeze) lost their
 * closing $C4 and the window never ended. */
#define WIN_TEXT_BUF   4096

/* Name length. Four in the original: the entry buffer is the long at
 * $FFFFC63C and chosen_letter_position stops at 3. */
#define WIN_NAME_LEN   4
/* (build 164) A proportional language's (lang.h): six, his long_names,
 * on the name board and in the slot list; gamedata.h's GD_NAME_MAX. */
#define WIN_NAME_MAX   6

/* Window ids are WindowArtLayoutPtrs indices, as the event code uses. */
enum {
    WIN_PLAYER_MENU        = 0x01,
    WIN_MENU_ITEM_CHAR     = 0x02,      /* the party, "WHO?" in the border */
    WIN_MENU_ITEM_LIST     = 0x03,
    WIN_MENU_ITEM_LIST2    = 0x04,
    WIN_ITEM_ACTION        = 0x05,      /* USE GIVE TOSS */
    WIN_CHOSEN_ITEM_CHAR   = 0x06,      /* the party, "ON?" */
    WIN_MENU_CHAR_STATS    = 0x07,
    WIN_STATE_ORDER        = 0x0A,
    WIN_CHAR_STATS_1       = 0x0B,      /* WinID_FirstCharStats + position */
    WIN_MENU_MESETA        = 0x0F,
    WIN_CHAR_ORDER_DEST    = 0x10,
    WIN_CHAR_LIST2         = 0x11,      /* Win_OrderCharList */
    WIN_LEVEL_TECH_LIST    = 0x12,
    WIN_LEVEL_TECH_LIST2   = 0x13,
    WIN_EQUIP_STATS        = 0x18,
    WIN_EQP_EQUIP_LIST     = 0x19,
    WIN_ITEM_LIST2         = 0x1A,      /* EQP's item pages */
    WIN_ITEM_LIST3         = 0x1B,
    WIN_RIGHT_LEFT         = 0x39,
    WIN_STRNG_HPTP         = 0x14,
    WIN_STRNG_STATS        = 0x15,
    WIN_STRNG_EQUIP        = 0x16,
    WIN_STRNG_LVEXP        = 0x17,
    WIN_SCRIPT_MESSAGE     = 0x08,
    WIN_YESNO              = 0x09,
    WIN_SCRIPT_MESSAGE_BIG = 0x1C,
    WIN_FULL_TECH_LIST     = 0x1D,
    WIN_FULL_TECH_LIST2    = 0x1E,
    WIN_YESNO2             = 0x1F,
    WIN_YESNO3             = 0x20,
    WIN_SCRIPT_MESSAGE2    = 0x21,
    WIN_BUYSELL            = 0x22,
    WIN_STORE_MESETA       = 0x23,
    WIN_NAME_INPUT         = 0x24,
    WIN_SAVE_SLOTS         = 0x25,
    WIN_LIBRARY_OPTIONS    = 0x26,
    WIN_HEAL_CURE          = 0x27,
    WIN_STORE_INVENTORY    = 0x28,
    WIN_ROLF_HOUSE_OPTIONS = 0x29,
    WIN_PROFILE_CHAR_LIST  = 0x2A,
    WIN_PROFILE_START      = 0x2B,      /* WinID_RolfProfile + character */
    WIN_PROFILE_LAST       = 0x32,
    WIN_REGROUP_CHAR_LIST  = 0x33,
    WIN_REGROUP_SELECTED   = 0x34,
    WIN_TOWER_OPTIONS      = 0x35,
    WIN_TOWER_OPTIONS2     = 0x36,
    WIN_GAME_SELECT        = 0x37,
    WIN_ROOM_OPTIONS       = 0x38,
    WIN_STRNG_CHAR_LIST    = 0x3A,
    WIN_HOUSE_LVEXP        = 0x5A,
    WIN_STORE_CHAR_LIST    = 0x5B,
    WIN_TELEPORT_PLACES    = 0x73,
    WIN_USTVESTIA_SONGS    = 0x74,      /* WinID_UstvestiaSoundtracks */

    /* The battle screen. The stat boxes, both option windows and the
     * enemy name/info boxes are opened with bit 2 of the high byte set
     * ($0400), which ProcessWindows honours by drawing the window and
     * *not* pushing it (loc_94B0 skips the count): they are pixels on
     * the plane, never on the stack, never closed by CloseAllWindows.
     * The port keeps those as "painted" entries. */
    WIN_BATTLE_CHAR_STATS_1 = 0x5C,     /* WinID_BattleFirstCharStats */
    WIN_BATTLE_OPTIONS      = 0x60,     /* FIGHT / STGY               */
    WIN_BATTLE_OPTIONS2     = 0x61,     /* ORDR / RUN                 */
    WIN_BATTLE_CHAR_NAME    = 0x62,
    WIN_BATTLE_COMMANDS     = 0x63,
    WIN_ENEMY_GROUPS        = 0x64,
    WIN_BATTLE_MESSAGE      = 0x65,
    WIN_BATTLE_TECH_LIST    = 0x66,
    WIN_BATTLE_ITEM_LIST    = 0x67,
    WIN_BATTLE_ITEM_USED    = 0x68,
    WIN_BATTLE_TECH_USED    = 0x69,
    WIN_ENEMY_NAME_1        = 0x6A,     /* plane B: the pos word has bit 13 */
    WIN_ENEMY_NAME_2        = 0x6B,
    WIN_ENEMY_INFO_1        = 0x6C,
    WIN_ENEMY_INFO_2        = 0x6D,
    WIN_BATTLE_EMPTY_1      = 0x6E,     /* WinID_BattleFirstEmptySpot */

    /* WinID_PortraitStart + portrait_index. Building_CheckRoutine opens
     * whichever one the door named; only the ones reached so far have
     * descriptors. */
    WIN_PORTRAIT_START     = 0x3B,
    WIN_PORTRAIT_COUNT     = 31,        /* portraits $00-$1E */
    WIN_ROLF_PORTRAIT      = 0x3B,
    WIN_NEI_PORTRAIT       = 0x3C,
    WIN_TOWER_PORTRAIT_COPY = 0x51,
    WIN_GOVERNOR_PORTRAIT  = 0x52,
    WIN_LIBRARY_GRAPH      = 0x55,
    WIN_ROLF_PORTRAIT2     = 0x72
};

/* $FFFFF72C: what an interior map byte is added to. The font, priority
 * set; a portrait window swaps it for the duration of its own draw. */
#define WIN_BASE_FONT      0x8500
#define WIN_BASE_ROLF      0xA400   /* line 1: RolfPortraitArt sits at $4A0 */
#define WIN_BASE_PORTRAIT  0xC400   /* line 2: LoadPortraits' art at $400   */

/* High-byte flags of a window_index word. */
#define WIN_F_PROCESSED 0x0100  /* set by CheckPrepareWindows, not callers */
#define WIN_F_INSTANT   0x0200  /* no open/close animation */
#define WIN_F_REDRAW    0x0400  /* refresh in place, do not push */
#define WIN_CLOSE(n)    ((u16)(0x8000 | (n)))

/* Text codes. Below $BB a byte is a CHARSET code. */
#define TXT_CHAR    0xBB
#define TXT_CHAR2   0xBC
#define TXT_ITEM    0xBF
#define TXT_MESETA  0xC0
#define TXT_NEWLINE 0xC1
#define TXT_CONT    0xC2
#define TXT_PAUSE   0xC3
#define TXT_END     0xC4   /* wait for a button                         */
#define TXT_END_NOW 0xC5   /* finish at once, window stays              */
#define TXT_END_T_C 0xC6   /* 60 frames or a button, then close one     */
#define TXT_END_T   0xC7   /* 60 frames                                 */

#define NAME_ADV 0xA3
#define NAME_RUB 0xA4

typedef enum {
    CURSOR_NONE = 0,
    CURSOR_RECT,     /* Obj_RedRectangleCursor  */
    CURSOR_NAME      /* Obj_InputWindowBlinkCursor + Obj_NameDestinationTile */
} CursorKind;

typedef struct {
    CursorKind kind;
    s16  x, y;           /* $A / $E, VDP sprite space (screen + 128) */
    s16  base_y;         /* $2A */
    u8   index, max;     /* $32 / $33 */
    s16  blink;          /* $26 */
    s16  repeat;         /* $28 */
    bool hidden;         /* bit 1 of 2(a0) */
    int  frame;          /* $24: 1 = one letter, 2 = ADV/RUB/END */
    s16  dest_blink;
    bool dest_hidden;
} WinCursor;

/* The two triangle cursors of the battle (Obj_TriangleCursCharSelection,
 * 2284, and Obj_TriangleCursCommSelect, 2359). They are object-RAM
 * slots rather than windows' cursors: the party one is spawned by event
 * code at slot count + 1 and runs while it still is, the command one is
 * spawned by Win_BattleCommands at count - 1 and runs while that window
 * is the one being polled. Both move left and right on the pad. */
typedef enum { TRI_NONE = 0, TRI_CHAR_SELECT, TRI_COMMAND } TriKind;

typedef struct {
    TriKind kind;
    int  slot;           /* the object slot it was spawned at          */
    s16  x, y;
    s16  base_x;         /* $2A for the command cursor                 */
    u8   index, max;     /* $32 / $33                                  */
    s16  blink;          /* $26                                        */
    s16  repeat;         /* $28                                        */
    bool hidden;
} WinTriangle;

/* (build 163) A run: text of a proportional language (src/game/lang.h),
 * glyph after glyph at each one's own advance, over the cells
 * [col, col + cells) of interior row `row`, which the window's cells give
 * the paper of. win_render draws each into tiles of the text pool
 * (win_text_pool) every frame. The messages' text is a run a line, typed
 * a glyph a frame as the stock text is typed a cell a frame. */
#define WIN_RUN_LEN   64        /* a line of 192 px is about 45 glyphs */
#define WIN_RUNS      24

typedef struct {
    u8 row, col, cells;
    u8 len;
    u8 code[WIN_RUN_LEN];
} WinRun;

typedef struct {
    int id;
    int col, row;        /* top-left of the frame, tiles from the camera */
    int w;               /* descriptor width byte: interior + 1          */
    int rows;            /* rows including both border rows              */
    u16 base;            /* $FFFFF72C when the window was drawn          */
    u8  layer;           /* LAYER_A, or LAYER_B when the position word
                            carries bit 13 ($2000 past plane A is plane B) */
    bool frameless;      /* text written straight to the plane, no box:
                            GameOverScreen's message                    */
    u8  cell[WIN_MAX_ROWS][WIN_MAX_COLS];   /* interior columns only     */
    WinCursor cursor;
    int over;            /* (build 142) a painted entry: how many stack
                            windows were open when it was drawn -- it was
                            drawn over them, and under any opened since */
    WinRun run[WIN_RUNS];   /* (build 163) a proportional language's text */
    int    runs;
} WinEntry;

#define WIN_PAINTED 12          /* four stat boxes, two options, four enemy
                                   boxes, and room to spare */

typedef struct {
    bool used;
    u8   name[WIN_NAME_MAX];    /* CHARSET codes, as $FFFFC680; (build 164)
                                   six, $C4 after a shorter one */
    int  level;                 /* $FFFFC684 */
} WinSlotInfo;

typedef struct WinSys {
    /* --- the three things event code waits on --- */
    u16  window_index[WIN_QUEUE + 1];
    u16  window_index_saved;
    u16  window_active_flag;

    u16  window_routine;
    u16  script_id[WIN_QUEUE + 1];

    /* Owned here because CloseCurrentWindow and CloseAllWindows write
     * them; the event code reads and writes them too. */
    u16  event_routine;
    u16  event_routine_sub;
    u16  event_routine_sub_2;
    /* A running level event (ProcessAButtonPress's pad-free path):
     * event_step counts its steps from 1 -- 0 is "not running" -- and
     * event_done set means the next idle visit closes every window and
     * ends it. The event's id is game.c's g_level_event. */
    u16  event_step;            /* $FFFFDE70 */
    u16  event_done;            /* $FFFFDE72 */

    /* --- results --- */
    u16  cursor_index;          /* $FFFFDE50 */
    u16  yes_no_input;          /* 0 = yes, 1 = no */
    u16  game_select;           /* $FFFFDED0 */
    u16  save_slot;             /* $FFFFDEBC */
    u8   name_entry[WIN_NAME_MAX];   /* $FFFFC63C; (build 164) six in a
                                        proportional language */
    u16  letter_pos;            /* chosen_letter_position */

    /* --- the shop windows (the level screen's menu shares them) --- */
    u16  store_max;             /* $FFFFF764: the stock's last index      */
    u16  store_index;           /* $FFFFF766: which StoreEquipItemArray   */
    u16  store_cursor;          /* $FFFFDEC2                              */
    u16  buy_sell;              /* $FFFFDEB6: 0 = BUY, 1 = SELL           */
    u16  character_index;       /* character_index, a chr[] slot          */
    u16  character_index_2;     /* character_index_2: who an item or a
                                   technique is used on ({char2})         */
    u8   item_index;            /* item_index, an item id                 */
    u8   technique_index;       /* technique_index, low six bits an id    */
    u16  item_slot;             /* $FFFFDE84: index into a chr's items[]  */
    u16  char_list_cursor;      /* $FFFFDE9A: max on draw, choice after   */
    u16  char_list_id[4];       /* $FFFFC618: party_member_id when drawn  */

    /* --- the other buildings --- */
    u16  heal_cure;             /* $FFFFDEC0: 0 = HEAL, 1 = CURE          */
    u16  tower_option;          /* $FFFFDECE                              */
    u16  ustvestia_song;        /* $FFFFCB12: the song shown, times two   */
    u16  teleport_choice;       /* $FFFFDED6: count - 1 on draw, choice   */
    u8   teleport_level[6];     /* $FFFFF768: the level each line names   */
    u16  house_option;          /* $FFFFDEC4: 0 strength, 1 regroup, 2 out */
    u16  menu_choice;           /* $FFFFDE80: ITEM STATE TECH STRNG EQP   */
    u16  strng_equip;           /* $FFFFDEA4                              */
    u16  tech_list2;            /* $FFFFDEB2                              */
    u16  library_topic;         /* $FFFFDEBE                              */
    u16  room_option;           /* $FFFFDED2: 0 keep, 1 take back         */
    u16  profile_choice;        /* $FFFFDEC6: a character index           */
    u16  regroup_choice;        /* $FFFFDECA: a line of the regroup list  */
    u16  regroup_count;         /* $FFFFC602 as Win_RegroupCharList sets it:
                                   lines - 1                              */

    /* --- the C menu --- */
    u16  item_char_line;        /* $FFFFDE82                              */
    u16  item_action;           /* $FFFFDE88: 0 USE, 1 GIVE, 2 TOSS       */
    u16  chosen_char_line;      /* $FFFFDE8A                              */
    u16  state_choice;          /* $FFFFDE92: 0 STATE, 1 ORDER            */
    u16  meseta_dismiss;        /* $FFFFDE96                              */
    u16  tech_cursor;           /* $FFFFDE9C                              */
    u16  equip_hand;            /* $FFFFDE6C: 2 right, 3 left, 6 both     */
    u16  right_left;            /* $FFFFDED4                              */
    u16  order_count;           /* $FFFFC602 during ORDER: placed - 1     */
    u16  order_dest[4];         /* $FFFFC610: the new party order         */

    /* EQP's stats box: $FFFFDE74/78/7C hold agility, attack and defense
       as (target, shown) pairs, and loc_96EC steps the shown ones toward
       the targets a point a frame, redrawing the box, on every frame
       the item page or the Right/Left window is polled ($FFFFDEA8). */
    u16  equip_target[3];
    u16  equip_shown[3];
    bool item_page_up;          /* $FFFFDEA8 */

    /* --- substitutions LoadScript resolves --- */
    const u8 *char_name;        /* $C4-terminated or WIN_NAME_LEN long;
                                   overridden by gd when that is set */
    const u8 *char2_name;
    u32  meseta_value;

    /* Party state the windows read: money, inventories, names. NULL on
     * the screens that have none (the title's own windows). */
    const struct GameData *gd;

    bool controls_locked;
    u16  tile_base;             /* $FFFFF72C */
    const Input *last_input;    /* this frame's pad, for WaitJoypad_B_C */

    /* --- the battle screen --- */
    u8   battle_box_lines;      /* (build 163) 1 or 2: the battle box a
                                   proportional language opened (his
                                   battle_box); 0 the stock box          */
    u16  battle_option;         /* $FFFFDEDA: 0 FIGHT, 1 STGY             */
    u16  battle_option2;        /* $FFFFDEDC: 0 ORDR, 1 RUN               */
    u16  battle_cmd_pick;       /* $FFFFDEE2: 0 attack 1 technique 2 item
                                   3 defense, from the commands window    */
    u16  enemy_group_pick;      /* $FFFFDEE4                              */
    u16  tech_page;             /* $FFFFDEEC: the battle tech list's page */
    u16  item_page;             /* $FFFFDEEE: the battle item list's page */
    u16  char_pick;             /* $FFFFDED8: the party cursor's stop     */
    u8   enemy_index;           /* enemy_index: the group's id, for {enemy} */
    u16  group2_start;          /* enemy_data_buffer+2                    */
    const u16 (*battle_cmd)[8]; /* char_battle_command_index: eight words
                                   a character, the first the command     */
    int  enemy_group_id[2];     /* (build 164) the two groups' enemy ids, for
                                   enemy_text in their name boxes        */
    const u8 *enemy_name[2];    /* EnemyNames rows for the two groups, ten
                                   CHARSET codes each; NULL for no group   */
    const u16 *enemy_info;      /* $FFFFCB20 / $FFFFCB22                  */
    const u8 *(*enemy_name_lookup)(void *ctx, int id);   /* EnemyNames */
    /* (build 163) The language's name of enemy `id` (lang.h), NULL where
     * the stock's stands: what {enemy} writes in a proportional one. */
    const char *(*enemy_text)(void *ctx, int id);
    WinTriangle tri[2];

    /* --- window stack --- */
    WinEntry stack[WIN_STACK];
    int  depth;                 /* current_active_objects_num */
    WinEntry painted[WIN_PAINTED];   /* drawn with $0400, never pushed */
    int  painted_count;
    int  steps;                 /* $FFFFDE40 */
    int  reveal;                /* $FFFFDE44 */
    bool closing;
    bool anim_drawn;            /* entry at [depth] is on screen */
    int  clip_lo, clip_hi;      /* frame columns currently shown */
    bool clip_right;
    /* (build 133) The main camera ($FFFFF71C/$FFFFF71E) is off the 16px
     * grid: ProcessWindows starts no window until it is back on. The
     * level screen sets it around its win_process; false elsewhere. */
    bool camera_off_grid;

    /* --- text --- */
    u8   text[WIN_TEXT_BUF];
    u16  text_script;           /* (build 166) the id the text was loaded
                                   from, for a change of language */
    int  text_pos;
    int  text_target;           /* stack index the text lands in */
    int  text_col, text_line;   /* $CD16 relative to home, $CD1A */
    int  text_max_line;         /* $CD1C */
    s16  newline_delay;         /* $CD1E */
    s16  end_timer;             /* $CD22 */
    bool fast;                  /* $CD20 */

    /* --- hooks --- */
    void *ctx;
    const char *(*script_lookup)(void *ctx, u16 id);
    /* (build 163) A script's text as script_lookup gives it, with none of
     * its side effects (the probe's script log, the story's beats): for
     * looking ahead at what a window will show. */
    const char *(*script_text)(void *ctx, u16 id);
    void (*slot_info)(void *ctx, int slot, WinSlotInfo *out);
    void (*sound)(void *ctx, u8 id);       /* move.b #id, (sound_queue) */
    void (*music)(void *ctx, u8 id);       /* UpdateSoundQueue */
    void (*music_latch)(void *ctx, u8 id); /* move.b #id, ($FFFFF640).w */
    /* loc_9BFE: the $2E word of what the leader faces on the map, or -1
     * (nothing, or no map). NULL outside the level screen. */
    int  (*facing_object)(void *ctx);
    /* (build 142) The picture window `id`'s cells stand for, asked by
     * win_render as it draws the window: the game's answer, since only it
     * knows what art and palette it loaded where (game.c's
     * window_picture). *first and *count: the tiles that art fills. NULL
     * (or no hook): the tiles as they are. */
    const Picture *(*picture)(void *ctx, int id, u16 *first, u16 *count);
} WinSys;

bool win_load_assets(void);
void win_init(WinSys *ws);

/* Appends to the queues, as `move.w #X, (window_index).w` does when the
 * queue is empty -- which it always is when event code runs. */
void win_open(WinSys *ws, u16 word);
void win_script(WinSys *ws, u16 id);
void win_script2(WinSys *ws, u16 first, u16 second);

/* `move.w #id, (script_id).w` itself: the word, so a second write in the
 * same event step replaces the first -- the ROM picks a message by
 * writing candidates in turn. win_script appends instead. */
void win_script_set(WinSys *ws, u16 id);
void win_script_set2(WinSys *ws, u16 first, u16 second);

/* True when event code may run its next state. */
bool win_idle(const WinSys *ws);

void win_close_current(WinSys *ws, u16 word);   /* CloseCurrentWindow */
void win_close_all(WinSys *ws);                 /* CloseAllWindows */

/* Forgets every painted window: the screen they were pixels on is gone. */
void win_clear_painted(WinSys *ws);

/* The battle's party cursor at an object slot (`slot` is count + 1 for
 * the event code's spawns): over `max` + 1 stops, at y. The slot it
 * takes replaces whatever cursor held it. */
void win_spawn_triangle(WinSys *ws, TriKind kind, int slot, int max, int x,
                        int y);
void win_kill_triangle(WinSys *ws, int slot);

/* One frame, in the original's order:
 *     win_run_cursors      RunObjects, the cursor objects only
 *     <event code>         IntroScr_CheckRoutine and friends
 *     win_prepare          CheckPrepareWindows
 *     win_process          ProcessWindows, from the VBlank handler */
void win_run_cursors(WinSys *ws, const Input *in);
void win_prepare(WinSys *ws, const Input *in);
void win_process(WinSys *ws);

/* For the probes: the one-line cursor the pad moves next frame, found
 * the way win_run_cursors decides which cursor is live -- the party
 * triangle, the command triangle, a painted box's lamp, the top window's
 * lamp. Its stop, how many stops (max + 1: every one of them wraps), and
 * whether left/right moves it (the triangles) rather than up/down.
 * False when none is live, or the live one is the name board's. */
typedef struct {
    int  stop;
    int  stops;
    bool across;
    bool party;          /* the party triangle (TRI_CHAR_SELECT)        */
} WinCursorInfo;
bool win_debug_cursor(const WinSys *ws, WinCursorInfo *out);

/* (build 131a) The codes of the last script load_script expanded -- what
 * the text window shows or is printing -- up to and with its end code
 * ($C4-$C7), at most max; the count copied. For route_probe's
 * `expect text`. */
int win_debug_text(const WinSys *ws, u8 *out, int max);

/* font_first is where FontsIconsArt sits in the bound tileset ($500).
 * cam_col / cam_row are the camera's tile position: window positions are
 * relative to it. (build 142) A window the `picture` hook names a picture
 * for binds it (video_picture_bind) over the interior cells it draws. */
void win_render(const WinSys *ws, Video *v, int font_first,
                int cam_col, int cam_row);

/* (build 166) The language changed (lang.h): what is on the screen redrawn
 * in the new one -- every open and painted window rebuilt in place from
 * its map and the game's state (its cursor kept), the script on the text
 * window copied again in the new language and typed again from its start
 * (or, finished, shown whole: its last page), an open name board the new
 * language's (the name begun on the old one let go). The event code's
 * flags and queues are left as they are. */
void win_language_changed(WinSys *ws);

/* GameOverScreen (12255): a script typed straight onto the plane at a
 * nametable position, `max_line` lines of the text scroll, no window
 * frame. Pushed as a stack entry so RunScript works as it does. */
void win_plain_text(WinSys *ws, u16 script, int col, int row, int max_line);

/* The ending clears plane A under a finished plain text (loc_799A): the
 * entries win_plain_text pushed go with it. */
void win_drop_plain_text(WinSys *ws);

/* CheckIfDoublePrice: an item's price for this party, where it is. */
u32 win_price(const WinSys *ws, int item);

/* VDPCharacterMaps: CHARSET code to the (upper, lower) tile pair. */
void win_glyph(u8 code, u8 *upper, u8 *lower);
u8   win_charset(char ch);

/* (build 163) The text pool: tiles of `vram` from `first`, `count` of
 * them, past every tile a screen's art takes ($800 on: the VDP's VRAM
 * ends there, the port's tile space does not), into which win_render
 * draws the runs of a proportional language. win_text_pool_reset at the
 * start of each frame's drawing, before anything takes from it;
 * win_text_pool_take gives `n` tiles' first index, or -1 when the pool is
 * spent (the run is then not drawn, said once). */
void win_text_pool(TileSet *vram, int first, int count);
void win_text_pool_reset(void);
int  win_text_pool_take(int n);
Tile *win_text_pool_tile(int index);

/* (build 163) A run's glyphs drawn into the pool over `cells` tiles whose
 * paper is `paper` (the tiles the cells show, copied first; NULL: blank),
 * in ink 1: the first pool tile's index, or -1. For text drawn outside a
 * window entry -- the opening's crawl. */
int win_text_draw(const u8 *codes, int n, int cells, const Tile *paper);

/* (build 165) A two-row face's marks (lang_draw_upper) the same way, on
 * `paper`: the first pool tile's index, or -1; `inked[c]` whether cell c
 * has a mark -- only those are put down, so what is under the others
 * shows. */
int win_text_draw_marks(const u8 *codes, int n, int cells, const Tile *paper,
                        bool *inked);

#endif /* PS2_WINDOW_H */
