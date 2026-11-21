#include "game.h"
#include "rom_ids.h"
#include "script_ids.h"

#include "building.h"
#include "ending.h"
#include "menu.h"
#include "battle.h"
#include "intro_select.h"
#include "intro_start.h"
#include "lang.h"
#include "npc.h"
#include "ui.h"
#include "walk.h"
#include "window.h"

#include "../core/art.h"
#include "../core/assetfs.h"
#include "../core/json.h"
#include "../core/script.h"
#include "../core/table.h"
#include "../core/tileset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- VRAM tile bases, from the load addresses in the screen routines ----
 *
 * SegaScreen writes SegaLogoArt to $40000000 -> VRAM 0, tile 0 (build
 * 149: the logo is its picture, the art not loaded).
 * TitleScreen writes TitleScrBGArt to $40200000 -> VRAM $0020, tile 1
 * (build 154: the background is its picture, the art not loaded);
 * TitleScrCopyrightArt to $60000001 -> VRAM $6000, tile $300;
 * FontsIconsArt to $60000002 -> VRAM $A000, tile $500. */
#define TILE_COPYRIGHT  0x300
#define TILE_FONT       0x500
#define FONT_FILL_COLOUR 11     /* the window fill; DrawArtTiles2 clears it */

/* FadeArtTiles destinations in the title sequence:
 * the figures go to $5C200000 -> VRAM $1C20, tile $E1, 287 tiles;
 * the logo to $40000001 -> VRAM $4000, tile $200, 256 tiles. */
#define TILE_WOMEN      0x0E1
#define COUNT_WOMEN     287
#define TILE_LOGO       0x200
#define COUNT_LOGO      256

/* Frame counts the original loads into demo_timer. */
#define SEGA_DURATION   0x12C   /* 300 frames */
#define TITLE_DURATION  0x0B4   /* 180 frames, before the fades */
#define TITLE_WAIT_TIME 0x6CC   /* 1740 frames on the finished title */
#define SEGA_CYCLE_RATE 0x028   /* starting byte offset into the ramp */

/* PaletteCycle copies 11 words into CRAM entries 2..12, every 4 frames,
 * walking the offset down by 2 bytes each time. */
#define FADE_FRAMES     21      /* $14 + 1, one step per frame */

#define PAL_CYCLE_WORDS 11
#define PAL_CYCLE_FIRST 2
#define PAL_CYCLE_GATE  3
#define FADE_STEPS      64
#define FADE_GATE       4

/* SegaScreen: 12 cells wide, 4 tall, at the position $461C0003 encodes. */
#define SEGA_LOGO_COL 14
#define SEGA_LOGO_ROW 12

static TileSet   g_vram;          /* mirrors VRAM tile space */
static TileSet   g_copyright, g_font;
static Nametable g_sega_map, g_title_a, g_title_b;
/* (build 153) GameOverScreen's plane B: loc_6C5DA, 160 bytes into the
 * title's background -- the same picture two rows up, and two rows of its
 * own below it. Until 153 the port drew the title's. */
static Nametable g_gameover_b;
/* (build 154) The title's pictures: plane A's figures and logo (one
 * picture, bound once a dissolve's tile range) and plane B's background;
 * game over's background (g_gameover_b's). The two masks are the
 * dissolves' (VideoPicture's `hidden`), each all hidden from the screen's
 * VRAM fill until FadeArtTiles begins on its range. */
static const Picture *g_title_picture, *g_title_background_picture;
static const Picture *g_gameover_background_picture;
static u64 g_figures_hidden, g_logo_hidden;
static WordTable g_cycling_pal;
static ByteTable g_fade_order;
static Battle    g_battle;
static Encounter g_encounter;
static Palette   g_battle_loaded;   /* $FB80 while the battle fades in */
static int       g_battle_formation;
static bool      g_assets_ready;

/* Level assets load on entry rather than at startup: which tileset and
 * which block set to read is a property of the level, and LoadLevelData
 * reloads both on every level change. */
static ScriptBlock g_opening_script;
static bool        g_opening_tried;     /* (build 166) since the language */

/* ScreenID_Intro: the select-game windows. */
static WinSys      g_win;
static IntroSelect g_select;
static GameData    g_data;
static bool        g_backup_checked;    /* $FFFFFF00 */

/* ScreenID_Intro, second entry: the dream. */
#define TILE_DREAM       1       /* art lands at VRAM $0020 */
#define DREAM_COL        6       /* $410C0003: row 2, column 6 */
#define DREAM_ROW        2
#define DREAM_LINE       3       /* both maps draw with palette line 3 */
#define DREAM_OUT_STEPS  32
#define DREAM_PAL_FRAMES 21      /* loc_61F4: $14 + 1 */

static IntroStart  g_start;
static Nametable   g_dream_map[2];  /* (build 150, 151) their art is their
                                       pictures' */
/* (build 151) The reveal mask of the tiles the dissolves write from tile 1
 * (TILE_DREAM), the dream's and Tyler's spaceship's: bit y * 8 + x set
 * where every tile's pixel [x, y] is blank (VideoPicture's `hidden`). A
 * dissolve in starts from blank tiles -- the intro and the building clear
 * VRAM, and a dissolve out clears all 64 places -- so from all set;
 * FadeArtFill's pixel clears its bit, loc_6CA4's byte sets its two. The
 * dream's fight and Tyler's help are drawn through it: their tables name
 * tiles 1-248 and 1-148, all inside the $FA and $96 the ROM dissolves, so
 * the mask holds for every cell of their pictures. */
static u64         g_dissolve_hidden;
#define DISSOLVE_ALL_HIDDEN (~(u64)0)

/* Tyler's spaceship (build 82). The first picture is the dream's fight
 * again (g_dream_map[0]); the second is loc_70D42 over loc_6F040; then
 * Tyler and the monitor over TylerSpaceshipArt. The two maps from art/
 * .bin files are optional: without them (make extract-nametables needs
 * ART_DIR) those two rectangles stay blank. (build 151) The fight and the
 * help are their pictures, their art not loaded. */
static TileSet     g_tyler_art;
static Nametable   g_help_map, g_tyler_portrait_map, g_tyler_monitor_map;
/* loc_8364: Lutz at tile 1 under LutzPortraitTileInd (a binclude: made
 * only with ART_DIR), plane A row 2 column 5, palette $3B in line 3.
 * (build 147) The table still puts down his cells; his picture colours
 * them, and neither his art nor his palette is loaded. */
static Nametable   g_lutz_map;
static bool        g_lutz_map_ok;
static const Picture *g_lutz_picture;  /* (build 147) assets/art/scenes */
#define LUTZ_COL 5
#define LUTZ_ROW 2
static bool        g_tyler_portrait_ok, g_tyler_monitor_ok;
static int         g_tyler_fx_step, g_tyler_fx_gate;

/* Obj_ExplosionTylerSpaceship (object 7, ps2.asm:2471) in $E800. */
static struct {
    bool live;
    int  routine;           /* $22 */
    s16  timer;             /* $26 */
    int  frame;             /* $24: Map_PalmExplosion's entry */
} g_monitor_expl;

/* Building_Roof's flight (loc_C58A, ps2.asm:18105; build 84): the planet,
 * loc_71362 at tile 1 under loc_716BA (6x6, plane B row 7 col 22), and
 * Obj_Spaceship ($44) in $EC00 with loc_71702's art at tile $400. */
#define SPACESHIP_OBJECT    0x44
#define TILE_FLIGHT_SHIP    0x400   /* $40000002 -> VRAM $8000 */
#define FLIGHT_PLANET_COL   22      /* $63AC0003 -> plane B $E3AC */
#define FLIGHT_PLANET_ROW   7
#define FLIGHT_TIMER        0x78    /* demo_timer, VBlankSub_04_08 */
static TileSet   g_ship_art;
static Nametable g_planet_map;          /* (build 148) its picture's art */
static struct {
    bool live;
    int  routine;           /* $22 */
    s16  timer;             /* $26 */
    s16  frame;             /* $24, the mapping frame */
    int  path;              /* $3C, into loc_59E4 */
    u16  x, y;              /* $A / $E, sprite space */
} g_ship;

#define TYLER_PORTRAIT_COL  6       /* $418C0003 */
#define TYLER_PORTRAIT_ROW  3
#define TYLER_MONITOR_COL   19      /* $41A60003 */
#define TYLER_MONITOR_ROW   3
#define TYLER_MONITOR_ROWS  12      /* d2 = $B; frames $168 bytes apart */
#define TYLER_MONITOR_FRAMES 5      /* loc_CDDE's 0-4 */
#define MONITOR_EXPL_OBJECT 7       /* ObjID_MonitorExplosion */
#define MONITOR_EXPL_X      0x118   /* ObjETS_Init, sprite space */
#define MONITOR_EXPL_Y      0x98
#define MONITOR_EXPL_FRAMES 6       /* deleted when $24 reaches 6 */
static bool        g_intro_dream;       /* building_index bit 0 */
static int         g_fx_step;
static int         g_fx_gate;
static Palette     g_fx_target;

/* ScreenID_Level with the controls unlocked: the party on the map. */
#define TILE_CHARS       0x300   /* $FFFFE408: first character's art */
#define CHAR_FRAME_TILES 8       /* one 16x32 frame */
#define CHAR_ART_TILES   72      /* nine frames a character */

static TileSet g_walk_art;          /* loc_2B1F0, all party members */
static Walk    g_walk;
static WalkExits g_exits;            /* loc_28546 */
static NpcSet  g_npcs;              /* $FFFFE800: the townspeople */

/* ProcessAButtonPress's little state machine: $FFFFDE70 says a talk is
 * under way, $FFFFDE72 that its script has been queued. */
static int     g_talk_state;
static int     g_talk_step;

/* $FFFFDE6E while ProcessAButtonPress runs an event that no button
 * started: only ObjType_Events $30 so far, the damage floor's fall. */
static int     g_level_event;
static s16     g_people_timer;     /* $FFFFDE5E in ObjType_People's $85 */

/* ObjType_Events 0 (loc_D9AA), which is what A finds in an empty cell:
 * $DE6E = 0, which g_level_event cannot say, since 0 there means no
 * event. The port only takes this path for the one case that needs its
 * second visit -- getting off the jet scooter; elsewhere the empty cell's
 * $1708 goes the people's way, to the same effect. */
#define LEVEL_EVENT_ZERO 0x1000

/* The level events (EVENT_*), the one-shot level events (SCENE_*, the
 * cases of loc_7E02 that event_flags' high byte names) and the demo walks
 * (DEMO_INDEX_*) are in rom_ids.h. */
#define FORMATION_MOTHER_BRAIN           0x103

/* The endgame's scripts are in script_ids.h (SCRIPT_LUTZ_REVIVES_THEM and
 * on). A script with no terminator runs on into the next one in the bank. */

/* The Earthmen's last step stands the six members other than Rolf and Nei
 * on the bridge (NoahEnding_TeleportedCharPos, ps2.asm:20894), each a
 * following-character object with the standing routine: the party's own
 * followers in their slots first, then the ones left behind. */
#define ENDING_CAST 6
typedef struct {
    int member;             /* character id, or -1 for nobody */
    s16 x, y;               /* world position, feet centre */
} EndingCastMember;
static EndingCastMember g_ending_cast[ENDING_CAST];
static bool             g_ending_cast_placed;
/* (build 182) Who the leader's slot draws from then on: the first of the
 * list at more than 0 HP, or -1. */
static int              g_ending_leader;

static u8      g_level_oneshot;
/* (build 138d) LevelScreen with event_flags $E00 -- the approach to
 * Mother Brain, loc_70D6 -- leaves through white (loc_634E) and comes in
 * from white (loc_62CC): set by the walk's level change, taken by the
 * screen's fade in. */
static bool     g_level_through_white;
/* Mother Brain's glow on the level ($FFFFF656) and its step ($F650):
 * the confrontation turns it on and off; LevelScreen clears both after
 * its first RunObjects (ps2.asm:11424). */
static bool     g_mother_brain_glow;
static u16      g_mother_brain_glow_step;

/* event_flags' high byte while the level runs (build 87). loc_3786
 * (Obj_CharactersInLevel) keeps the leader still and ProcessAButtonPress
 * ignores A while it is set; the $DE70 path still runs. Only loc_E07A's
 * $D00 -- the second word with B1's Esper -- sets it so far, and
 * loc_7E02's case $D is `rts`: it lasts until Building_EsperMansion
 * clears event_flags (loc_CF0A), which BuildingState.clear_event_flags
 * carries back here. */
static u8      g_event_hold;

/* loc_7E02 case $B clears $FFFFE400: no party object, so nobody is drawn
 * or walks, and LevelScreen spawns no followers (loc_7CF6 is skipped when
 * the slot is empty). */
static bool    g_party_gone;

/* What an event asks of the level loop, acted on as level_event returns:
 * screen_changed_flag = -1 (the level again), or ScreenID_Battle with a
 * formation. */
static bool    g_event_reload;

/* A sound the ROM waits out in place -- `move.b #0, (track_timer).w`,
 * then `cmpi.b #$80, (track_timer).w / bne.s -` inside the routine that
 * queued it (TechEffect_Musik, the Control Tower keyboard's teleport).
 * VBlank goes on playing it; the game loop stands still. While this is
 * set tick_screens runs only the sequencer and the RNG. The count is
 * the port's own guard against a sound that never ends. */
static int     g_sound_wait;
#define SOUND_WAIT_MAX (60 * 20)

static void start_sound_wait(Game *g)
{
    if (g->audio != NULL) {
        audio_track_timer_clear(g->audio);
    }
    g_sound_wait = 1;
}
static int     g_event_battle;
/* game_screen = ScreenID_Ending from the Earthmen's last step. */
static bool    g_ending_next;
/* game_screen = ScreenID_Building from an event (ObjType_Events $2F):
 * building_index and portrait_index are already in g_bstate. */
static bool    g_event_building;
/* ObjCL_Main on Gaira (ps2.asm:5186): which of loc_12B06 / loc_12B36 the
 * party's mappings are this frame -- true for the second, the ring. */
static bool    g_gaira_ring;

/* The death of Nei: loc_7E02's cases 1-7 (ps2.asm:11494-11600) and the
 * camera cases of loc_809C they set (11733-11830), across seven level
 * reloads after the Neifirst battle. Each case runs once, in enter_level,
 * and leaves what the frames after it need here.
 *
 *   picture    1: NeiDeathTileInd (28x14; 27x13 before build 126, the
 *                 draw's dbf counts) at plane A 6,2 with NeiDeathArt at tile
 *                 1 and PalettePtrs $35 on line 1; 2: NeiBodyTileInd (6x4)
 *                 at 17,9 too, with $36.
 *   no_redraw  loc_9002 skipped (bit 15 of event_flags): plane A is what
 *                 case 1 or 2 drew and plane B is empty -- LevelScreen's
 *                 ClearSpriteAndScroll zeroed both planes first.
 *   camera     event_flags' low byte, loc_809C's case: 1 counts demo_timer
 *                 down and reloads; 2 the same with both planes thrown 0-7
 *                 (A) and 0-3 (B) pixels a frame (loc_8226); 3 a 0/1-pixel
 *                 jitter with no timer (loc_80D4); 4 as 1, with $F748/$F74A/
 *                 $F74C as the level to load (loc_810E).
 *   next_*     level_index / level_y_pos / level_x_pos as the case set them
 *                 for the reload after it -- kept aside rather than written
 *                 into g_data, whose level_index the walk still reads. */
static int     g_scene_picture;
static bool    g_scene_no_redraw;
static int     g_scene_camera;
static u16     g_scene_timer;
static s16     g_scene_shake_ax, g_scene_shake_ay;    /* $F61C / $F620 high */
static s16     g_scene_shake_bx, g_scene_shake_by;    /* $F61E / $F622 */
static bool    g_scene_next_set;
static u16     g_scene_next_level, g_scene_next_x, g_scene_next_y;

#define SCENE_PICTURE_COL 6     /* $410C0003: plane A row 2, col 6 */
#define SCENE_PICTURE_ROW 2
#define BODY_COL        17      /* $44A20003: row 9, col 17 */
#define BODY_ROW        9

/* loc_7108's WaitForVBlanks: ticks the level loop is held for, and the
 * palette the white field is put back to ($FB80 in the ROM). */
static int     g_floor_stall;
/* (build 157b) What waits for the stall's last tick: loc_7108's HP, then
 * the rest of the level loop's iteration -- ObjCL_Main returns, the other
 * objects run, then ProcessAButtonPress, ProcessRandomBattle, loc_6430
 * and the rest -- with the walk's result and the windows' state from the
 * step that set it off. */
static bool       g_floor_rest;
static WalkResult g_floor_rest_result;
static bool       g_floor_rest_busy;

#define TILE_ANIMS_MAX   4
#define TILE_ANIM_FRAMES 4
#define TILE_ANIM_SEQ    8

typedef struct {
    int     level, tile, period;
    int     frame_count;
    TileSet frame[TILE_ANIM_FRAMES];
    int     seq[TILE_ANIM_SEQ];
    int     seq_len;
} TileAnim;

static TileAnim g_tile_anim[TILE_ANIMS_MAX];
static int      g_tile_anim_count;

/* loc_118F4's counters (level tile animation, below): $FFFFF63C picks the
 * frame, $FFFFF63E counts the fields between. */
static u16     g_tile_anim_step;
static s16     g_tile_anim_wait;

/* $FFFFF622 / $FFFFF61E on a plane-B backdrop (Gaira, Noah): nothing
 * points the camera at them, ClearSpriteAndScroll zeroes them on entry,
 * and LevelScreenLoop drifts them a pixel a frame (loc_7DB2). */
static s16     g_plane_b_x, g_plane_b_y;
static int     g_building_warned = -1;
static bool    g_level_walk;        /* false: the attract-mode crawl */
static const u8 *g_walk_demo;       /* DemoScriptPtrs[demo_index], or NULL */

/* ScreenID_Building. */
#define TILE_PORTRAIT       0x400   /* LoadPortraits: VRAM $8000 */
#define TILE_ROLF_PORTRAIT  0x4A0   /* BuildingScreen: VRAM $9400 */
#define TILE_PORTRAITS_END  0x500   /* (build 146a) $A000: the windows'
                                       bytes reach $FF over base $400 */
#define PORTRAITS_MAX       32
#define BUILDINGS_MAX       32

static BuildingState g_bstate;
static MenuState     g_menu;
static bool          g_paused;          /* paused_flag */
static bool          g_pause_release;   /* GamePausedLoop has returned */
static bool          g_pause_pending;   /* Start seen; $D5 goes after the
                                           tick's logic, as CheckGamePause's
                                           write does */
/* PortraitTable: each portrait's art (its label: which windows show the
 * same art) and palette. (build 146a) Neither is loaded any more -- the
 * portraits are their pictures -- but window_picture tells the tower's
 * copy window by them. */
static char    g_portrait_art_name[PORTRAITS_MAX][40];
static int     g_portrait_pal[PORTRAITS_MAX];
/* (build 142) What the portrait tiles would hold now, for the pictures
 * (window_picture): the portrait LoadPortraits last loaded (0: none, or
 * something else at tile $400 since -- the flight's ship, Tyler's
 * spaceship's second palette); and whether Rolf's art would be at $4A0,
 * which BuildingScreen puts there in every building but the library (the
 * graph is there), the roof, Tyler's spaceship (VRAM filled with 0) and
 * the Esper mansion (Lutz). (build 146a) Only the pictures stand for them:
 * no portrait art goes to VRAM and no portrait palette is loaded. */
static int     g_portrait_loaded;
static bool    g_rolf_portrait_loaded;
static int     g_building_music[BUILDINGS_MAX];
static Level    g_level;
static BlockSet g_blocks;
static TileSet  g_level_art;
static TileSet  g_level_mist_art;   /* (build 141b) loc_412F8 */
static char     g_level_stem[64];
static bool     g_level_ready;

/* For tools/route_probe.c (game_debug_state, game_debug_on_script): none
 * of it feeds back into the game. Steps are counted where loc_38FC sets
 * the step going; the event signature is what ProcessPlayerMenu and the
 * buildings' event code step on, sampled at the end of every tick. */
static void  (*g_debug_script_fn)(void *ctx, u16 id);
static void   *g_debug_script_ctx;
static u32     g_debug_steps;
static u32     g_debug_event_sig;
static int     g_debug_event_still;
static bool    g_debug_logic_ran;   /* this tick reached the screen's own
                                       logic: no fade, no Escape menu */
/* (build 162) The last script that reached a window when it is one of the
 * plot's (story_script) shown for the first time since the console's last
 * reset, else 0; and those shown, a bit a script id. For game_story_beat;
 * nothing in the game reads them. */
#define STORY_IDS 0x1A00        /* banks $00-$19 */
static u16     g_story_words;
static u8      g_story_seen[STORY_IDS / 8];

/* ---- the Motavia opening ----
 *
 * MoveToOpeningScreen (ps2.asm:10783) is where the title screen times
 * out to: level 0, camera at $5B0/$520, controls_locked = 1 and
 * event_flags = $1007.
 *
 * The two bytes of event_flags drive different things. The high byte
 * indexes the one-shot cases in loc_7E02, and $10 is the last of them:
 * it starts script $3B. The low byte indexes the per-frame camera cases
 * in loc_809C, and $07 is loc_8192, which steps the camera one pixel per
 * field toward ($FFFFC648, $FFFFC64A) and, on arrival, runs the next
 * script from $FFFFF780 -- which MoveToOpeningScreen seeded with $3C.
 *
 * Scripts $3B..$40 (EventType_DialogueIndex, ps2.asm:19934) are the
 * crawl. Each sets one axis of the target, opens a message window and
 * loads demo_timer with $10E; the camera cannot move while a script is
 * active, so the order on screen is message, hold, move. Script $40 has
 * no target and hands back to ScreenID_Sega, which is how the attract
 * loop closes.
 *
 * Note what is NOT here: because controls_locked is 1, LevelScreen skips
 * `move.w #ObjID_CharactersLevel, ($FFFFE400).w` entirely, and the
 * following-party loop is gated on that same slot. Nobody walks during
 * the opening, so none of this needs the object system. */
#define CRAWL_HOLD_FRAMES 0x10E   /* demo_timer, 270 fields */

/* WinID_ScriptMessageBig. PtrWin_ScriptMessageBig encodes its position
 * as $488E: the high field is a nametable byte offset, $880 past the
 * plane base, which is row 17 at $80 bytes per row, and the low field is
 * $0E bytes, which is column 7. WinArt_ScriptMessageBig is 24 interior
 * columns by 10 rows, and the frame is drawn around the map, so the box
 * is 26 wide.
 *
 * The original opens a window two columns per frame (loc_978C, thirteen
 * frames for this one) and fills its interior from the dynamic window
 * buffer. The crawl's box opens and closes that way below (MSG_STEP);
 * window.c's reveal does it for every other window. */
/* A CHARSET code plus this is the glyph's index within FontsIconsArt:
 * 'A' is code 11 and lands on sheet tile 39. Digits are codes 1..10 and
 * take the ten slots immediately below 'A', and the punctuation at
 * $3F..$46 follows straight after 'z'. One base covers all of them --
 * the two `charset` blocks in ps2.asm (12534 and 25327) map letters
 * identically, and the first simply omits digits rather than rebasing
 * them. Code 0 is not a blank: it means leave the cell alone, since
 * base + 0 is an item icon. */
#define GLYPH_BASE 28

/* '0' is tile 118, not glyph_base + 1: tiles 0..38 of FontsIconsArt are
 * item icons, so the digits sit past the punctuation instead, and a
 * CHARSET digit code (1..10) needs this base rather than GLYPH_BASE.
 * This is the split the two `charset` blocks in ps2.asm hint at -- the
 * letters are identical in both and only the digits move. */
#define DIGIT_BASE 117

#define MSG_COL 7
#define MSG_ROW 17
#define MSG_W   26
#define MSG_H   10

/* loc_978C redraws the whole box each frame at a width that grows by
 * two interior columns per frame -- but it also shifts the start address
 * right by (width - d1) / 2 columns (`add.b d5, d0` after
 * `andi.w #$FFFE, d5`), so the narrow box is centred and each side moves
 * outward one column per frame. Thirteen frames for 24 interior columns.
 * Closing (loc_9872) is the mirror: each frame it writes the right border
 * one column further left with the map restored beyond it, then resets
 * a1 and does the same from the left, so the box shrinks to its centre
 * with both borders walking inward. Both run inside the crawl: the open
 * is part of demo_timer's 270, and the camera cannot move until the
 * close finishes and $FFFFDE70 drops back to zero. */
#define MSG_STEP    2
#define MSG_FRAMES  ((MSG_W - 2 + MSG_STEP - 1) / MSG_STEP + 1)

static const struct {
    s16 x, y;      /* accumulated target; each script sets one axis */
    u16 message;   /* script_id; the low byte indexes Script_Opening */
} CRAWL[] = {
    { 0x1E0, 0x4A0, SCRIPT_OPENING_SECOND_PLANET_ALGO_SYSTEM },   /* $3B sets both axes */
    { 0x1E0, 0x120, SCRIPT_OPENING_OVER_1000_YEARS_PASSED },   /* $3C sets Y */
    { 0x760, 0x120, SCRIPT_OPENING_SINCE_TIME_ALGO_PROSPERED },   /* $3D sets X */
    { 0x760, 0x340, SCRIPT_OPENING_MOTHER_BRAIN_CREATED_DOMED },   /* $3E sets Y */
    { 0x3F0, 0x340, SCRIPT_OPENING_BUT_PEOPLE_ALGO_SYSTEM },   /* $3F sets X */
    { 0x3F0, 0x340, SCRIPT_OPENING_NOW_EVIL_THREATENS_SYSTEM },   /* $40 sets nothing, then back to SEGA */
};
#define CRAWL_STEPS ((int)(sizeof CRAWL / sizeof CRAWL[0]))

#define OPENING_LEVEL 0

/* MoveToOpeningScreen sets level_x_pos / level_y_pos, which are the
 * centre of the view, not its corner: LoadLevelData derives the camera
 * as pos - $A0 horizontally and pos - $80 vertically (ps2.asm:13325).
 * Taking them as the corner put the opening $A0 right and $80 down of
 * where it belongs, and left script $3B with a Y target the camera had
 * not already reached -- which is where the diagonal pan came from. With
 * the offset applied the camera starts at $510/$4A0, script $3B's target
 * Y of $4A0 is already satisfied, and the first move is due west. */
#define OPENING_X     0x5B0
#define OPENING_Y     0x520
#define CAMERA_OFFSET_X 0xA0
#define CAMERA_OFFSET_Y 0x80

/* The platform already latched the edge, so this is a read rather than a
 * comparison against the previous frame. */
#define PRESSED(g, field) ((g)->input.pressed.field)
#define HELD(g, field)    ((g)->input.held.field)
#define ANY_HELD(g) (HELD(g, up) || HELD(g, down) || HELD(g, left) \
                     || HELD(g, right) || HELD(g, a) || HELD(g, b) \
                     || HELD(g, c) || HELD(g, start))

static void begin_fade_in(Game *g);
static void begin_fade_in_white(Game *g);
static void goto_screen_white(Game *g, ScreenId next);
static void goto_screen(Game *g, ScreenId next);
static void goto_sega(Game *g);
static void begin_gameover(Game *g, ScreenId over);
static void render_level(Game *g);
static int  level_facing_object(void *ctx);
static bool level_riding(void);
static void level_floor_event(Game *g);
static void tile_anims_load(void);
static bool load_scene_pictures(void);

/* PortraitTable and BuildingMusicPtrs; tools/extract_buildings.py. */
static bool load_building_tables(void)
{
    JsonValue *portraits = json_parse_file("assets/data/portraits.json");
    JsonValue *buildings = json_parse_file("assets/data/buildings.json");
    bool ok = (portraits != NULL && buildings != NULL);

    for (const JsonValue *e = ok ? portraits->child : NULL; e != NULL;
         e = e->next) {
        long i = json_int(e, "index", -1);
        if (i > 0 && i < PORTRAITS_MAX) {
            snprintf(g_portrait_art_name[i], sizeof g_portrait_art_name[i],
                     "%s", json_str(e, "art", ""));
            g_portrait_pal[i] = (int)json_int(e, "palette", 0);
        }
    }
    for (const JsonValue *e = ok ? buildings->child : NULL; e != NULL;
         e = e->next) {
        long i = json_int(e, "index", -1);
        if (i >= 0 && i < BUILDINGS_MAX) {
            g_building_music[i] = (int)json_int(e, "music", 0);
        }
    }

    json_free(portraits);
    json_free(buildings);
    return ok;
}

/* A .tmx that make extract-nametables only writes when ART_DIR has its
 * .bin: absent is not an error here (render_tyler leaves it blank). */
static bool load_optional_nametable(Nametable *nt, const char *path)
{
    AssetReader fh;                 /* (build 125) the pack's or the disk's */
    if (!asset_open(&fh, path)) {
        memset(nt, 0, sizeof *nt);
        return false;
    }
    asset_close(&fh);
    return nametable_load(nt, path);
}

/* (build 163) The text pool (window.h's win_text_pool): tiles past the
 * VDP's 2048, which no screen's art reaches, for a proportional
 * language's runs. A frame's text takes at most a few hundred. */
#define TILE_TEXT_POOL      0x800
#define TEXT_POOL_TILES     1024

static bool load_assets(Video *video)
{
    bool ok = tileset_alloc(&g_vram, TILE_TEXT_POOL + TEXT_POOL_TILES);
    win_text_pool(&g_vram, TILE_TEXT_POOL, TEXT_POOL_TILES);

    ok = tileset_load(&g_copyright,
                      "assets/tiles/title_copyright_art.png")        && ok;
    ok = tileset_load(&g_font,      "assets/tiles/FontsIconsArt.png")  && ok;

    ok = nametable_load(&g_sega_map,
                        "assets/nametables/SegaLogoTileInd.tmx")        && ok;
    ok = nametable_load(&g_gameover_b,
                        "assets/nametables/GameOverBGTileInd.tmx")      && ok;
    ok = nametable_load(&g_title_b,
                        "assets/nametables/TitleScrBGTileInd.tmx")      && ok;
    ok = nametable_load(&g_title_a,
                        "assets/nametables/TitleScrPlaneATileInd.tmx")  && ok;

    ok = table_load_ramp(&g_cycling_pal,
                         "assets/palettes/sega_logo_sweep.png")         && ok;
    ok = table_load_dissolve(&g_fade_order,
                             "assets/tables/dissolve_order.json")   && ok;

    ok = nametable_load(&g_dream_map[0],
                        "assets/nametables/DreamBattleTileInd.tmx")      && ok;
    ok = nametable_load(&g_dream_map[1],
                        "assets/nametables/DreamWakingTileInd.tmx")      && ok;
    ok = nametable_load(&g_help_map,
                        "assets/nametables/DreamHelpTileInd.tmx")        && ok;
    ok = tileset_load(&g_ship_art, "assets/tiles/SpaceshipArt.png")      && ok;
    ok = nametable_load(&g_planet_map,
                        "assets/nametables/SpaceshipPlanetTileInd.tmx")  && ok;
    ok = tileset_load(&g_tyler_art,
                      "assets/tiles/tyler_spaceship_art.png")            && ok;
    g_tyler_portrait_ok = load_optional_nametable(&g_tyler_portrait_map,
                        "assets/nametables/TylerPortraitTileInd.tmx");
    g_tyler_monitor_ok  = load_optional_nametable(&g_tyler_monitor_map,
                        "assets/nametables/TylerSpcshp_MonitorTileInd.tmx");
    g_lutz_map_ok = load_optional_nametable(&g_lutz_map,
                        "assets/nametables/LutzPortraitTileInd.tmx");
    ok = tileset_load(&g_walk_art, "assets/tiles/CharWalkArt.png")       && ok;
    ok = walk_load_exits(&g_exits, "assets/levels/exits.json")           && ok;
    ok = load_building_tables()                                          && ok;
    /* (build 142) The portraits as pictures (assets/art/portraits). */
    ok = art_portraits_load()                                            && ok;
    /* (build 147) The cut-scene pictures (assets/art/scenes). */
    g_lutz_picture = art_scene("lutz");
    ok = (g_lutz_picture != NULL)                                        && ok;
    ok = load_scene_pictures()                                           && ok;
    /* Item names before windows and the chests: the shop stock and
     * chests.json look items up by name. */
    ok = gamedata_load_tables()                                          && ok;
    ok = npc_load_tables()                                               && ok;
    tile_anims_load();
    ok = win_load_assets()                                               && ok;
    ok = ending_load_assets()                                            && ok;

    video->tileset = &g_vram;
    return ok;
}

void game_attach_audio(Game *g, Audio *audio)
{
    g->audio = audio;
}

/* The generator's state at power-on (game_init; game_debug_reset). */
#define RNG_POWER_ON_SEED 0x2A6D365Au

bool game_init(Game *g, Video *video)
{
    memset(g, 0, sizeof *g);
    g->video  = video;
    g->screen = SCREEN_SEGA;
    rng_init(&g->rng, RNG_POWER_ON_SEED);
    sysmenu_init(&g->sysmenu);
    /* (build 163) The languages before the settings name one, and the one
     * named before anything reads a file it may carry. */
    lang_init();
    sysmenu_load_settings(&g->sysmenu);
    lang_select(g->sysmenu.language);
    g->sysmenu.language = lang_current();

    g_assets_ready = load_assets(video);
    return g_assets_ready;
}

/* ---- screen entry: the part each routine does once, before its loop ---- */

/* The sound driver's fade-out command (not a MusicID_ or SXFID_). */
#define SOUND_FADE_OUT   0xE0

static void queue_sound(Game *g, u8 id)
{
    if (g->audio != NULL) {
        audio_play_sound(g->audio, id);
    }
}

/* DemoScriptPtrs (ps2.asm:5401): the walks ported so far. 0 and 1 are
 * the walks between Rolf's house and the tower, 2 and 3 the Teim scene's
 * (started by name, not by index), 4 and 5 what Dynamite and a key or
 * card start from the menu, 7 the clone lab's. */
#define DEMO_SCRIPTS 11
static const u8 *const DEMO_SCRIPT[DEMO_SCRIPTS] = {
    DEMO_HOUSE_TO_TOWER, DEMO_TOWER_TO_HOUSE, NULL, NULL,
    DEMO_DYNAMITE_USED, DEMO_KEY_USED, DEMO_CLIMATROL_TO_CLONE_LAB,
    DEMO_CLONE_LAB_TO_TOWER, DEMO_TYLER_SPC_TO_TOWER,
    DEMO_AFTER_MOTHER_BRAIN, DEMO_TO_THE_EARTHMEN
};

#define SFX_SCOOTER_OFF   0xE1      /* the driver's stop-SFX command */

/* move.b #id, (sound_queue).w -- effects skip UpdateSoundQueue's latch. */
static void queue_sfx(Game *g, u8 id)
{
    if (g->audio != NULL) {
        audio_play_sfx(g->audio, id);
    }
}

/* jet_scooter_flag on level 0: what Obj_CharactersInLevel,
 * Obj_FollowingChars, RenderCharSprites and VBlankSub_10 test before
 * hiding the party. The flag stays set in a dam, where they walk. */
static bool level_riding(void)
{
    return g_data.level_index == 0 && g_data.jet_scooter != 0;
}

/* (build 157a) goto_sega has sent SegaScreen's $E0 as the fade before
 * the screen began; enter_sega sends it only when nothing faded first. */
static bool g_sega_fade_sent;

static void enter_sega(Game *g)
{
    /* SegaScreen's first instruction is `move.b #$E0, d0; bsr
     * UpdateSoundQueue` -- fade out whatever was playing. (build 157a)
     * Before its PaletteFadeFrom: a hand-over to it through goto_sega
     * sent it as that fade began. Here only at power-on (and the debug
     * reset, a route's `start new`), which enter with no fade, and after
     * Data Memory's reset, which is a plain goto_screen. */
    if (!g_sega_fade_sent) {
        queue_sound(g, SOUND_FADE_OUT);
    }
    g_sega_fade_sent = false;

    /* (build 149) SegaLogoArt and Pal_Sega are the logo's picture
     * (assets/art/scenes/sega_logo): neither is loaded. The art's first
     * tile, blank, is what the ROM's load leaves at tile 0, which every
     * empty cell names; VRAM cleared keeps it so. The sweep still runs
     * on line 0's entries 2-12, which the picture's slot map names. */
    tileset_clear(&g_vram);
    video_colours_clear(g->video, &g->video->palette);

    g->demo_timer       = SEGA_DURATION;
    g->pal_cycle_offset = SEGA_CYCLE_RATE;
    g->pal_cycle_gate   = PAL_CYCLE_GATE;
    g->video->backdrop  = 0;
}

static void enter_title(Game *g)
{
    /* TitleScreen opens with a DMA VRAM fill to zero (ps2.asm:10640).
     * Re-entering without it leaves the figures and logo from the last
     * visit resident, so they show fully drawn until the dissolve phase
     * blanks them again. */
    tileset_clear(&g_vram);

    tileset_place(&g_vram, &g_copyright, TILE_COPYRIGHT);
    tileset_place(&g_vram, &g_font,      TILE_FONT);

    g->push_start_armed = false;
    g->push_start_timer = 0x27;

    video_colours_clear(g->video, &g->video->palette);
    video_colours_load(g->video, &g->video->palette, 0, PALETTE_TITLE_SCREEN);

    /* TitleScreen queues the music right after loading its palette and
     * before PaletteFadeTo (ps2.asm:10681), so it starts under the
     * fade-in rather than after it. */
    queue_sound(g, MUSIC_PHANTASY);

    g->demo_timer      = TITLE_DURATION;
    g->title_phase     = TITLE_BG;
    g->fade_step       = 0;
    g->fade_gate       = FADE_GATE;
    g->video->backdrop = 0;
    g_figures_hidden   = DISSOLVE_ALL_HIDDEN;
    g_logo_hidden      = DISSOLVE_ALL_HIDDEN;
}

/* script_id is bank << 8 | entry. Banks load the first time one of
 * their scripts is asked for; script.json says which file holds which. */
#define SCRIPT_BANKS 32

static ScriptBlock g_bank[SCRIPT_BANKS];
static bool        g_bank_tried[SCRIPT_BANKS];

static const ScriptBlock *script_bank(int bank)
{
    if (bank < 0 || bank >= SCRIPT_BANKS) {
        return NULL;
    }
    if (!g_bank_tried[bank]) {
        g_bank_tried[bank] = true;

        /* (build 163) The language's script where it carries one. */
        char lang[192];
        JsonValue *manifest = json_parse_file(
            lang_path("assets/script/script.json", lang, sizeof lang));
        for (const JsonValue *e = (manifest != NULL) ? manifest->child : NULL;
             e != NULL; e = e->next) {
            if (json_int(e, "bank", -1) == bank) {
                char path[160];
                snprintf(path, sizeof path, "assets/script/%s",
                         json_str(e, "file", ""));
                script_load(&g_bank[bank], lang_path(path, lang, sizeof lang));
            }
        }
        json_free(manifest);
    }
    return &g_bank[bank];
}

/* (build 163) Every bank read again from the language's files when next
 * asked for, and the crawl's. */
static void drop_script_banks(void)
{
    for (int b = 0; b < SCRIPT_BANKS; b++) {
        script_free(&g_bank[b]);
        g_bank_tried[b] = false;
    }
    script_free(&g_opening_script);
    g_opening_tried = false;
}

/* (build 162) A script of the plot, for COMPUTER PLAYS's AUTO
 * (game_story_beat): the scenes' banks whole -- the Governor, the opening,
 * the dream at the game's start, the level events, Tyler, Lutz and
 * Pandora's box (Miscellaneous) -- and the story's lines in the mixed ones:
 * Rolf's house (Nei at the start, the knock at the door, each recruit's
 * words and naming), the inventor's gum, Central Tower's flood, the
 * library's Recorder report, Pandora's dark power, and the Prism, the dam
 * locks, the note and the Recorder among the items. Not shops, the
 * hospital, Data Memory, the clone labs, lockers, lessons, chests,
 * townspeople, the roster, Shir back from a theft or the roof's spaceship
 * (travel: a seed's game flies a dozen times). */
static bool story_script(u16 id)
{
    int n = id & 0xFF;
    switch (id >> 8) {
        case 0x00: {                    /* Script_ItemAction */
            return n == 0x07 || n == 0x1B || (n >= 0x20 && n <= 0x22)
                || n == 0x25;
        }
        case 0x09: {                    /* Script_RolfHouse */
            return (n >= 1 && n <= 7) || n == 12 || (n >= 19 && n <= 38);
        }
        case 0x0B: {                    /* Script_InventorHouse */
            return n >= 1 && n <= 7;
        }
        case 0x0D: {                    /* Script_CentralTower */
            return n >= 2 && n <= 5;
        }
        case 0x10: {                    /* Script_Library */
            return n >= 16 && n <= 20;
        }
        case 0x11: {                    /* Script_Governor */
            return true;
        }
        case 0x14: {                    /* Script_Opening */
            return true;
        }
        case 0x15: {                    /* Script_GameStart */
            return true;
        }
        case 0x17: {                    /* Script_LevelActions */
            return n == 0x1A;
        }
        case 0x18: {                    /* Script_LevelEvents */
            return true;
        }
        case 0x19: {                    /* Script_Miscellaneous */
            return true;
        }
        default: {
            return false;
        }
    }
}

/* (build 163) A script's text with no side effects (window.h's
 * script_text). */
static const char *script_text(void *ctx, u16 id)
{
    (void)ctx;
    const ScriptBlock *sb = script_bank(id >> 8);
    return (sb != NULL) ? script_entry(sb, id & 0xFF) : NULL;
}

/* LoadScript's lookup, which is every script id that reaches a window --
 * a script without a terminator runs on into the next id, and that one
 * is looked up (and reported) too. */
static const char *script_lookup(void *ctx, u16 id)
{
    (void)ctx;
    const ScriptBlock *sb = script_bank(id >> 8);
    const char *text = (sb != NULL) ? script_entry(sb, id & 0xFF) : NULL;

    if (text != NULL && g_debug_script_fn != NULL) {
        g_debug_script_fn(g_debug_script_ctx, id);
    }
    g_story_words = 0;
    if (text != NULL && id < STORY_IDS && story_script(id)
        && (g_story_seen[id / 8] & (1u << (id % 8))) == 0) {
        g_story_seen[id / 8] |= (u8)(1u << (id % 8));
        g_story_words = id;
    }
    return text;
}

static void intro_sound(void *ctx, u8 id)
{
    Game *g = ctx;
    if (g->audio != NULL) {
        audio_play_sfx(g->audio, id);
    }
}

static void intro_music(void *ctx, u8 id)
{
    queue_sound(ctx, id);
}

/* move.b #id, ($FFFFF640).w: the latch alone. */
static void latch_sound(Game *g, u8 id)
{
    if (g->audio != NULL) {
        audio_set_last_sound(g->audio, id);
    }
}

static void intro_music_latch(void *ctx, u8 id)
{
    latch_sound(ctx, id);
}

/* LoadLevelData (ps2.asm:13374-13384, loc_9222): on level types 1, 3
 * and 6 -- the Skure, cave and Dezolis dungeon tilesets -- after the
 * level's palettes and people art, line 1 entry 7 of the palette as loaded
 * becomes $ECA and loc_412F8's 23 tiles go to VRAM $3020 (the VDP command
 * $70200000), tiles $181-$197: the mist the tilesets' backdrop blocks are
 * drawn with (plane A's wrapping backdrop, line 1, high priority; blocks
 * 101-104 of tilesets 1 and 3, 64-67 of tileset 6). Until build 141b the
 * port did neither and drew those blocks empty. */
#define LEVEL_MIST_TILE   0x181
#define LEVEL_MIST_TILES  23
#define LEVEL_MIST_ENTRY  (1 * PAL_ENTRIES + 7)
#define LEVEL_MIST_COLOUR 0x0ECA

static void level_mist(Game *g)
{
    int type = g_level.tileset;
    if (type != 1 && type != 3 && type != 6) {
        return;
    }
    video_colours_set(g->video, &g->video->palette, LEVEL_MIST_ENTRY,
                      LEVEL_MIST_COLOUR);
    if (g_level_mist_art.count == 0) {
        tileset_load(&g_level_mist_art, "assets/tiles/level_mist.png");
    }
    /* The 23 tiles alone: the sheet's padding to a full row is not art. */
    TileSet mist = g_level_mist_art;
    mist.count = (mist.count < LEVEL_MIST_TILES) ? mist.count
                                                 : LEVEL_MIST_TILES;
    tileset_place(&g_vram, &mist, LEVEL_MIST_TILE);
}

static Nametable g_scene_map[2];        /* loc_AF4EA, loc_AF7FA */

/* (build 148) The pictures of the scenes nothing but the screen's fades
 * touches (assets/art/scenes; tools/extract_pictures.py's SCENES): Nei's
 * two after Neifirst and her closed eyes, the roof's flight's planet by
 * where it goes, the battle floor. */
static const Picture *g_nei_face, *g_nei_face_starry, *g_nei_eyes_closed;
static const Picture *g_planet_to_dezolis, *g_planet_to_motavia;
static const Picture *g_planet_picture;     /* this flight's */
static const Picture *g_battle_floor_picture;
/* (build 149) The colour cycles' pictures, each with its slot map: the
 * SEGA logo (its sweep) and Tyler's monitor's five frames (loc_6798's
 * colours once the explosion is over). The ending's stars are ending.c's. */
static const Picture *g_sega_picture;
static const Picture *g_tyler_monitor[TYLER_MONITOR_FRAMES];
/* (build 150) The dream's second picture, Rolf waking: only its line's
 * fade in touches it. */
static const Picture *g_dream_waking_picture;
/* (build 151) The dissolved ones, through the reveal mask
 * (g_dissolve_hidden): the dream's fight (the intro's first picture and
 * Tyler's) and Tyler's "Help!!!"; (build 152) Tyler himself, dissolved in
 * with the monitor. */
static const Picture *g_dream_battle_picture, *g_dream_help_picture;
static const Picture *g_tyler_portrait_picture;
/* (build 155) The star field behind Mother Brain, its slot map the
 * chase's, through the enemy art's reveal mask; her art stays loaded, for
 * her figure (the battle object). */
static const Picture *g_mother_brain_stars;

static bool load_scene_pictures(void)
{
    g_nei_face              = art_scene("nei_face");
    g_nei_face_starry       = art_scene("nei_face_starry");
    g_nei_eyes_closed       = art_scene("nei_eyes_closed");
    g_planet_to_dezolis     = art_scene("flight_planet_to_dezolis");
    g_planet_to_motavia     = art_scene("flight_planet_to_motavia");
    g_battle_floor_picture  = art_scene("battle_floor");
    g_sega_picture          = art_scene("sega_logo");
    g_dream_waking_picture  = art_scene("dream_waking");
    g_dream_battle_picture  = art_scene("dream_battle");
    g_dream_help_picture    = art_scene("dream_help");
    g_tyler_portrait_picture = art_scene("tyler_portrait");
    g_title_picture         = art_scene("title");
    g_title_background_picture = art_scene("title_background");
    g_gameover_background_picture = art_scene("gameover_background");
    g_mother_brain_stars    = art_scene("mother_brain_stars");
    bool ok = g_nei_face != NULL && g_nei_face_starry != NULL
           && g_nei_eyes_closed != NULL && g_planet_to_dezolis != NULL
           && g_planet_to_motavia != NULL && g_battle_floor_picture != NULL
           && g_sega_picture != NULL && g_dream_waking_picture != NULL
           && g_dream_battle_picture != NULL && g_dream_help_picture != NULL
           && g_tyler_portrait_picture != NULL && g_title_picture != NULL
           && g_title_background_picture != NULL
           && g_gameover_background_picture != NULL
           && g_mother_brain_stars != NULL;
    for (int i = 0; i < TYLER_MONITOR_FRAMES; i++) {
        g_tyler_monitor[i] = art_scene_frame("tyler_monitor", i);
        ok = (g_tyler_monitor[i] != NULL) && ok;
    }
    return ok;
}

/* loc_7E02 cases 1 and 2 (ps2.asm:11498-11536): loc_AED00 decompressed
 * to VRAM $0020 -- over the level's tiles 1 on, which the skipped loc_9002
 * never draws -- and PalettePtrs $35 or $36 onto line 1. The nametables
 * themselves go on plane A at render time. */
static void level_scene_picture(int which)
{
    if (g_scene_map[0].width == 0) {
        nametable_load(&g_scene_map[0],
                       "assets/nametables/NeiDeathTileInd.tmx");
        nametable_load(&g_scene_map[1],
                       "assets/nametables/NeiBodyTileInd.tmx");
    }
    if (g_scene_map[0].width == 0) {
        fprintf(stderr, "level: no NeiDeathTileInd.tmx -- rerun make "
                        "extract-nametables, then make manifest\n");
    }
    /* (build 148) The art and PalettePtrs $35 or $36 (both line 1) are
     * the pictures' now (g_nei_face and the rest): neither is loaded. */
    g_scene_picture = which;
}

/* level_index, level_y_pos and level_x_pos as a loc_7E02 case leaves them
 * for the reload after it. */
static void level_scene_next(u16 level, u16 x, u16 y)
{
    g_scene_next_set   = true;
    g_scene_next_level = level;
    g_scene_next_x     = x;
    g_scene_next_y     = y;
}

/* screen_changed_flag = -1 from the scene: the next LevelScreen, on
 * whatever level the last case named. */
static void level_scene_reload(void)
{
    if (g_scene_next_set) {
        g_data.level_index = g_scene_next_level;
        g_data.level_x     = g_scene_next_x;
        g_data.level_y     = g_scene_next_y;
        g_scene_next_set   = false;
    }
    g_event_reload = true;
}

/* loc_809C, every frame of LevelScreenLoop, for the cases the scene
 * sets. The shake lands on $F61C/$F620 (plane A's scroll, high word) and
 * $F61E/$F622 (plane B's), which VBlank copies to the VDP; here it is
 * added to both planes' scroll at render time. */
static void level_scene_camera(Game *g)
{
    switch (g_scene_camera) {
        case CAMERA_NONE: {
            break;
        }
        case CAMERA_WAIT_THEN_RELOAD: {
            /* loc_8240: demo_timer down, then loc_824C -- the low byte
             * cleared and the level again. */
            if (g_scene_timer != 0) {
                g_scene_timer--;
                break;
            }
            g_scene_camera = CAMERA_NONE;
            level_scene_reload();
            break;
        }
        case CAMERA_SHAKE_THEN_RELOAD: {
            /* loc_8226 twice: one roll an axis; & 7 for plane A, bits
             * 8-9 for plane B (the low word of the same long). */
            u16 rx = rng_next(&g->rng);
            g_scene_shake_ax = (s16)(rx & 7);
            g_scene_shake_bx = (s16)((rx >> 8) & 3);
            u16 ry = rng_next(&g->rng);
            g_scene_shake_ay = (s16)(ry & 7);
            g_scene_shake_by = (s16)((ry >> 8) & 3);
            if (g_scene_timer != 0) {
                g_scene_timer--;
                break;
            }
            g_scene_camera = CAMERA_NONE;
            level_scene_reload();
            break;
        }
        case CAMERA_JITTER: {
            /* loc_80D4: `andi.l #$10001, d0` on the whole register --
             * bit 16 for plane A, bit 0 for plane B, one roll an axis. */
            u32 rx = rng_next32(&g->rng);
            g_scene_shake_ax = (s16)((rx >> 16) & 1);
            g_scene_shake_bx = (s16)(rx & 1);
            u32 ry = rng_next32(&g->rng);
            g_scene_shake_ay = (s16)((ry >> 16) & 1);
            g_scene_shake_by = (s16)(ry & 1);
            break;
        }
        case CAMERA_WAIT_THEN_NEXT_LEVEL: {
            /* loc_810E: as 1, the level from $F748 -- which the case
             * that set it put in next_* already. */
            if (g_scene_timer != 0) {
                g_scene_timer--;
                break;
            }
            g_scene_camera = CAMERA_NONE;
            level_scene_reload();
            break;
        }
        case CAMERA_GAIRA_COUNTDOWN: {
            /* loc_815C (ps2.asm:11730), set by ObjType_Events $2D in the
             * Gaira prison: nothing while an event runs ($DE70), a window
             * is asked for (window_index) or open (current_active_
             * objects_num); otherwise demo_timer counts down, and at 0
             * $DE70 = 1, $DE6E = $2E and event_flags = 0 -- the
             * explosion. */
            if (g_win.event_step != 0 || !win_idle(&g_win) || g_win.depth != 0) {
                break;
            }
            if (g_scene_timer != 0) {
                g_scene_timer--;
                break;
            }
            g_win.event_step     = 1;
            g_level_event  = EVENT_GAIRA_EXPLOSION;
            g_scene_camera = CAMERA_NONE;
            break;
        }
        case CAMERA_DAMS_CHECK: {
            /* loc_81E6 (ps2.asm:11785), set by a card at its dam's panel:
             * nothing until no window is up or opening (window_index,
             * current_active_objects_num); then, if all four words
             * $C72C-$C732 are $0101 -- every dam's door and panel --
             * enemy_data_buffer = $101, the Army Eyes, and the battle
             * screen; either way event_flags = 0. */
            if (!win_idle(&g_win) || g_win.depth != 0) {
                break;
            }
            bool all = true;
            for (int i = FLAG_DAM_LOCKS_FIRST; i <= FLAG_DAM_LOCKS_LAST; i++) {
                if (g_data.c700[i] != 1) {
                    all = false;
                }
            }
            if (all) {
                g_event_battle = 0x101;
            }
            g_scene_camera = CAMERA_NONE;
            break;
        }
        default: {
            fprintf(stderr, "level: camera case %d is not ported\n",
                    g_scene_camera);
            g_scene_camera = CAMERA_NONE;
            break;
        }
    }
}

/* IntroScreen (ps2.asm:12341). It is entered twice for a new game and
 * building_index says which: 0 from the title, where loc_89EE resets RAM
 * to a new game and the select-game windows run; 1 from loc_D384 once the
 * name is chosen, where the reset is skipped and the dream runs. Either
 * way VRAM is cleared, the font goes to $500 and the title palette loads. */
static void enter_intro(Game *g)
{
    tileset_clear(&g_vram);
    tileset_place(&g_vram, &g_font, TILE_FONT);
    video_colours_clear(g->video, &g->video->palette);
    video_colours_load(g->video, &g->video->palette, 0, PALETTE_TITLE_SCREEN);
    g->video->backdrop = 0;

    if (!g_intro_dream) {
        gamedata_new_game(&g_data);
        memset(&g_bstate, 0, sizeof g_bstate);
        g_encounter.hidapipe = 0;   /* loc_8A08 zeroes $C000-$F5FF */
        /* loc_F550: the battle commands start from CharStartCommandsArray. */
        battle_tables_load();
        battle_reset_commands(&g_battle, &g_data, true);
    }

    win_init(&g_win);
    g_win.ctx           = g;
    g_win.script_lookup = script_lookup;
    g_win.script_text   = script_text;     /* (build 163) */
    g_win.slot_info     = intro_select_slot_info;
    g_win.sound         = intro_sound;
    g_win.music         = intro_music;
    g_win.music_latch   = intro_music_latch;
    g_win.char_name     = g_data.chr[0].name;   /* character_index = 0 */

    if (g_intro_dream) {
        intro_start_begin(&g_start, &g_win);
    } else {
        intro_select_begin(&g_select, &g_win, &g_backup_checked);
    }

    /* Same id both times, so UpdateSoundQueue's latch makes the second
     * a no-op and the music carries across the fade. */
    queue_sound(g, MUSIC_STEP_UP);
}

/* CheckItemExistInventory (ps2.asm:19067): every party member's sixteen
 * slots, whatever the count says, masked $7F. It leaves
 * character_index on the member it was looking at when it stopped. */
static bool party_has_item(int item)
{
    for (int i = 0; i <= g_data.party_members_num && i < GD_PARTY_MAX; i++) {
        int c = g_data.party_member_id[i] % GD_CHARS;
        g_win.character_index = (u16)c;
        for (int s = 0; s < GD_ITEM_SLOTS; s++) {
            if ((g_data.chr[c].items[s] & 0x7F) == item) {
                return true;
            }
        }
    }
    return false;
}

/* The tail of LoadLevelData, loc_9258-loc_9300 (ps2.asm:13389): block
 * maps written in RAM after they are decompressed.
 * assets/data/map_patches.json (extract_levels.py) has them in the
 * routine's order:
 *
 *   Motavia, $C735: every $8B in plane B becomes $89, and $89 goes at
 *     four places; then -- still only under $C735, loc_92CA is its rts --
 *     one loc_9302 run list for each of $C72F, $C733, $C731, $C72D.
 *   Dezolis, anyone in the party holding the Prism: block $63 at four
 *     places in plane B, the collision plane there. $63 is solid and
 *     special in all four quadrants, and the four places are the doors to
 *     Menobe, Ikuto, Guaron and Naval; without the Prism they are open
 *     ground and those interaction entries can never match.
 *   NoahF1, $C747: block $28 at plane A + $1B0.
 *
 * The planes are rebuilt from the map every frame and walk.c reads
 * collision from the same map, so one write is both. A run entry is
 * row, column, n (loc_9302): n with bit 7 set is the block written at
 * that one place, otherwise n & $3F of the list's block, across, or down
 * when bit 6 is set. */
static bool map_patch_flags(const JsonValue *p)
{
    const JsonValue *flags = json_get(p, "flags");
    for (const JsonValue *f = (flags != NULL) ? flags->child : NULL;
         f != NULL; f = f->next) {
        const char *s = (f->type == JSON_STRING) ? f->string : "";
        unsigned addr = 0;
        if (sscanf(s, "$%x", &addr) != 1 || addr < 0xC700
            || addr >= 0xC700 + GD_C700_SIZE) {
            fprintf(stderr, "level: map patch flag \"%s\" is not a $C7xx "
                            "byte\n", s);
            return false;
        }
        if (g_data.c700[addr - 0xC700] == 0) {       /* tst.b */
            return false;
        }
    }
    return true;
}

static void map_patch_put(u8 *map, int size, int at, int block)
{
    if (at >= 0 && at < size) {
        map[at] = (u8)block;
    }
}

static void level_map_patches(void)
{
    static bool warned;
    JsonValue *root = json_parse_file("assets/data/map_patches.json");
    if (root == NULL) {
        if (!warned) {
            fprintf(stderr, "level: assets/data/map_patches.json is missing "
                            "-- rerun make extract-levels; no map changes by "
                            "flag until then (the Dezolis dungeons stay "
                            "shut)\n");
            warned = true;
        }
        return;
    }

    const JsonValue *list = json_get(root, "patches");
    for (const JsonValue *p = (list != NULL) ? list->child : NULL;
         p != NULL; p = p->next) {
        if (json_int(p, "level", -1) != g_level.index) {
            continue;
        }
        if (!map_patch_flags(p)) {
            continue;
        }
        if (json_get(p, "item") != NULL
            && !party_has_item((int)json_int(p, "item", -1))) {
            continue;
        }

        LayerId layer = (json_str(p, "plane", "A")[0] == 'B') ? LAYER_B
                                                             : LAYER_A;
        u8 *map  = g_level.map[layer];
        int size = g_level.plane_cols[layer] * g_level.plane_rows[layer];
        if (map == NULL) {
            continue;
        }

        const JsonValue *rep = json_get(p, "replace");
        if (rep != NULL) {
            int from  = (int)json_int(rep, "from", -1);
            int to    = (int)json_int(rep, "to", 0);
            int bytes = (int)json_int(rep, "bytes", 0);
            for (int i = 0; i < bytes && i < size; i++) {
                if (map[i] == from) {
                    map[i] = (u8)to;
                }
            }
        }

        const JsonValue *set = json_get(p, "set");
        if (set != NULL) {
            int block = (int)json_int(set, "block", 0);
            const JsonValue *at = json_get(set, "at");
            for (const JsonValue *a = (at != NULL) ? at->child : NULL;
                 a != NULL; a = a->next) {
                map_patch_put(map, size, (int)a->number, block);
            }
        }

        const JsonValue *runs = json_get(p, "runs");
        if (runs != NULL) {
            int stride = (int)json_int(runs, "stride", 0x60);
            int block  = (int)json_int(runs, "block", 0x87);
            const JsonValue *e = json_get(runs, "entries");
            for (const JsonValue *r = (e != NULL) ? e->child : NULL;
                 r != NULL; r = r->next) {
                if (json_length(r) != 3) {
                    continue;
                }
                int at = (int)json_at(r, 0)->number * stride
                       + (int)json_at(r, 1)->number;
                int n  = (int)json_at(r, 2)->number;
                if ((n & 0x80) != 0) {
                    map_patch_put(map, size, at, n);
                    continue;
                }
                /* subq #1 then dbf: a count of 0 would run 65536 times;
                 * no list has one. */
                int count = n & 0x3F;
                for (int k = 0; k < count; k++) {
                    map_patch_put(map, size, at, block);
                    at += ((n & 0x40) != 0) ? stride : 1;
                }
            }
        }
    }
    json_free(root);
}

static bool load_level(int index)
{
    char path[128];

    level_free(&g_level);
    level_blocks_free(&g_blocks);
    tileset_free(&g_level_art);

    /* Files are named <index>_<name>; the names are in the manifest. */
    char stem[64] = "";
    char blocks_file[64] = "";
    char tiles_file[64] = "";
    JsonValue *manifest = json_parse_file("assets/levels/levels.json");
    const JsonValue *levels = json_get(manifest, "levels");
    for (const JsonValue *lv = (levels != NULL) ? levels->child : NULL;
         lv != NULL; lv = lv->next) {
        if (json_int(lv, "index", -1) == index) {
            /* LoadLevelData (ps2.asm:13229): planet_index is bit 7 of the
             * level record's first byte, set on every load. Before build
             * 104 the port never set it: the planet stayed whatever a save
             * last loaded said, so Dezolis's cap checks, its prices, the
             * Telepipe's branch there and the last save's planet all took
             * the party for on Motavia. */
            g_data.planet = (u16)json_int(lv, "planet", 0);
            snprintf(stem, sizeof stem, "%02x_%s", index,
                     json_str(lv, "name", ""));
            const JsonValue *files = json_get(lv, "files");
            snprintf(blocks_file, sizeof blocks_file, "%s",
                     json_str(files, "blocks", ""));
            snprintf(tiles_file, sizeof tiles_file, "%s",
                     json_str(files, "tiles", ""));
        }
    }
    json_free(manifest);

    if (stem[0] == '\0' || !level_load(&g_level, "assets/levels", stem)) {
        return false;
    }
    snprintf(g_level_stem, sizeof g_level_stem, "%s", stem);

    /* The tileset's blocks and tiles, by the names the level's record
     * gives them (build 97b: blocks_<tileset>.txt, tileset_<tileset>.png). */
    snprintf(path, sizeof path, "assets/levels/%s", blocks_file);
    if (!level_blocks_load(&g_blocks, path)) {
        return false;
    }

    snprintf(path, sizeof path, "assets/tiles/%s", tiles_file);
    if (!tileset_load(&g_level_art, path)) {
        return false;
    }
    level_map_patches();
    return true;
}

/* CheckItemExistInventory then RemoveItemFromInventory (ps2.asm:19060,
 * 16056): the first member in party order holding `item` loses it, the
 * slots after it moving up. character_index is left on whoever was
 * looked at last. */
static bool level_take_item(int item)
{
    for (int i = 0; i <= g_data.party_members_num && i < GD_PARTY_MAX; i++) {
        int c = g_data.party_member_id[i] & 7;
        CharData *ch = &g_data.chr[c];
        g_win.character_index = (u16)c;
        for (int s = 0; s < GD_ITEM_SLOTS; s++) {
            if ((ch->items[s] & 0x7F) != item) {
                continue;
            }
            ch->item_count--;
            memmove(&ch->items[s], &ch->items[s + 1],
                    (size_t)(GD_ITEM_SLOTS - 1 - s));
            ch->items[GD_ITEM_SLOTS - 1] = 0;
            return true;
        }
    }
    return false;
}

/* LevelScreen (ps2.asm:11338) with controls_locked clear: load the level
 * the save block names, spawn ObjID_CharactersLevel at level_x/y_pos, and
 * let LoadLevelData place the camera and queue the level's music. */
static void enter_level_walk(Game *g)
{
    char path[160];

    g_level_ready = load_level(g_data.level_index);
    if (!g_level_ready) {
        return;
    }

    /* LevelScreen (ps2.asm:11375): arriving on Motavia on the scooter --
     * out of a dam, after a fight -- parks it where the party arrives,
     * which LoadSpritesInLevel then puts it on. */
    if (g_data.level_index == 0 && g_data.jet_scooter != 0) {
        gamedata_set_word(&g_data, GD_SCOOTER_PARKED_Y, g_data.level_y);
        gamedata_set_word(&g_data, GD_SCOOTER_PARKED_X, g_data.level_x);
    }

    /* LoadSpritesInLevel (ps2.asm:799): a town up to Ryuon is marked as
     * visited by writing its own index at $C700 + index. The teleport
     * stations list what is marked. */
    if (g_data.level_index <= LEVEL_RYUON) {
        g_data.c700[FLAG_TOWNS_VISITED + g_data.level_index] = (u8)g_data.level_index;
    }
    /* Its tail: first reaching Arima, Oputa, Zema, Kueri or Piata moves
     * party_member_join_next on, which is the knock at Rolf's door
     * (Hugh's, the third, is a level action), and each town has a place
     * the jet scooter is parked when it is owned ($C716 == 2): y into
     * $C654 and x into $C656. */
    {
        static const struct { u16 level, when; u16 py, px; } TOWN[] = {
            { 4, 0xFFFF, 0x0F0, 0x450 },    /* Paseo: no bump */
            { 5, 1, 0x0F0, 0x530 },         /* Arima */
            { 6, 2, 0x1F0, 0x250 },         /* Oputa */
            { 7, 4, 0x6B0, 0x4B0 },         /* Zema */
            { 8, 5, 0x5F0, 0x0F0 },         /* Kueri */
            { 9, 6, 0x6B0, 0x8D0 },         /* Piata */
        };
        for (size_t i = 0; i < sizeof TOWN / sizeof TOWN[0]; i++) {
            if (TOWN[i].level != g_data.level_index) {
                continue;
            }
            if (g_data.party_member_join_next == TOWN[i].when) {
                g_data.party_member_join_next++;
            }
            if (g_data.c700[FLAG_JET_SCOOTER_OWNED] == 2) {
                gamedata_set_word(&g_data, GD_SCOOTER_PARKED_Y, TOWN[i].py);
                gamedata_set_word(&g_data, GD_SCOOTER_PARKED_X, TOWN[i].px);
            }
        }
    }

    snprintf(path, sizeof path, "assets/levels/%s.collision.txt",
             g_level_stem);
    g_level_ready = walk_load_collision(&g_walk, path);
    snprintf(path, sizeof path, "assets/levels/%s.doors.json",
             g_level_stem);
    g_level_ready = walk_load_doors(&g_walk, path) && g_level_ready;
    g_walk.exits  = &g_exits;
    if (g_data.level_index == 0 && !g_walk.have_scooter_table
        && !walk_load_scooter_collision(&g_walk,
                "assets/levels/jet_scooter.collision.txt")) {
        fprintf(stderr, "level: no jet scooter table -- rerun make "
                        "extract-collision, then make manifest\n");
    }

    tileset_clear(&g_vram);
    tileset_place(&g_vram, &g_level_art, 0);
    tileset_place(&g_vram, &g_font, TILE_FONT);

    video_colours_clear(g->video, &g->video->palette);
    video_colours_load(g->video, &g->video->palette, 0, PALETTE_FIELD_COMMON);
    video_colours_load(g->video, &g->video->palette, 2, g_level.palette);
    g->video->backdrop = 0;

    /* LevelScreen sets $24 = 3, facing down. level_type is $FFFFF710,
     * the low nibble of the level record's first byte, which is also
     * the tileset number. */
    /* LoadLevelData decompresses the level's people art; then
     * LoadSpritesInLevel fills $FFFFE800 from LevelSpriteData. */
    npc_spawn(&g_npcs, g_data.level_index, g_level.tileset, g_data.c700,
              &g_vram);
    level_mist(g);
    g_walk.npcs  = &g_npcs;
    g_npcs.jet_scooter = &g_data.jet_scooter;

    /* LoadSpritesInLevel's tail (ps2.asm:826): on Motavia, with the
     * scooter owned ($C716) and event_flags' high byte clear, slot 0 is
     * ObjID_JetScooter at the parking place, $24 = $2A = 6 (left). The
     * rest of the slot is what SpriteData_MotaWorldMap's first entry left
     * there, which is the scooter too. */
    if (g_data.level_index == 0 && g_data.c700[FLAG_JET_SCOOTER_OWNED] != 0
        && g_level_oneshot == 0) {
        Npc *sc = npc_put(&g_npcs, 0, NPC_OBJ_JET_SCOOTER,
                          (s16)gamedata_word(&g_data, GD_SCOOTER_PARKED_X),
                          (s16)gamedata_word(&g_data, GD_SCOOTER_PARKED_Y), FACE_LEFT);
        if (sc != NULL) {
            sc->facing      = FACE_LEFT;
            sc->interaction = NPC_INTERACT_EVENT;
            sc->index       = EVENT_JET_SCOOTER_PARKED;
        }
    }
    g_talk_state = 0;
    g_talk_step  = 0;
    g_level_event = 0;
    g_floor_stall = 0;
    g_floor_rest  = false;
    g_tile_anim_step = 0;           /* LevelScreen: $F63C = $F63E = 0 */
    g_mother_brain_glow      = false;   /* ... and $F650-$F658 */
    g_mother_brain_glow_step = 0;
    g_tile_anim_wait = 0;
    g_plane_b_x   = 0;
    g_plane_b_y   = 0;

    win_init(&g_win);
    g_win.ctx           = g;
    g_win.script_lookup = script_lookup;
    g_win.script_text   = script_text;     /* (build 163) */
    g_win.sound         = intro_sound;
    g_win.music         = intro_music;
    g_win.music_latch   = intro_music_latch;
    g_win.char_name     = g_data.chr[0].name;
    g_win.gd            = &g_data;
    g_win.facing_object = level_facing_object;
    g_npcs.event_step         = &g_win.event_step;
    g_npcs.sound        = intro_sound;
    g_npcs.sound_ctx    = g;
    menu_init(&g_menu);
    g_paused        = false;
    g_pause_release = false;
    g_pause_pending = false;

    g_walk_demo = NULL;
    if (g_bstate.demo_flag && g_bstate.demo_index >= 0
        && g_bstate.demo_index < DEMO_SCRIPTS) {
        g_walk_demo = DEMO_SCRIPT[g_bstate.demo_index];
        if (g_walk_demo == NULL) {
            fprintf(stderr, "level: demo walk %d is not ported\n",
                    g_bstate.demo_index);
        }
    }

    /* LevelScreen's order is LoadLevelData, LoadSpritesInLevel, loc_7E02
     * (ps2.asm:11394-11396), and the level's music goes through
     * UpdateSoundQueue in the first (13296): against the latch as it
     * stood before this load, ahead of anything the one-shot cases below
     * write to it. loc_7E02's cases 3-7 latch the *next* level's music
     * ($82, $87, $82, $87, $84), so each reload's request matches and
     * Power plays on; queued after the case, as through build 79, every
     * reload from the third restarted Advanced or Restoration.
     *
     * (build 139f) And the driver takes it before anything later in the
     * load can queue a sound. LevelScreen's loads run with the vertical
     * blank on ($2500 from its start; loc_9002 and loc_7E02's pictures
     * mask it only while they write the planes), and VBlankExit runs
     * SoundDriverInput every field even while vblank_routine is 0: after
     * UpdateSoundQueue come LoadLevelData's own art (DecompressArt,
     * 13322) and LoadSpritesInLevel's, fields of it, so the music is
     * started before RunObjects runs the jet scooter's init and its engine,
     * $D8, goes into the same one-byte sound_queue (ObjJetSctr_Init,
     * 6797). The port loads in one tick: the engine overwrote the music,
     * and after a fight on the scooter the victory music played on (out of
     * a dam, the dam's). A load that leaves the music alone (the latch
     * already its) takes nothing early. */
    if (g_level.music != 0 && g->audio != NULL
        && audio_play_sound(g->audio, (u8)g_level.music)) {
        audio_take_queue(g->audio);
    }

    /* LoadSpritesInLevel's tail (ps2.asm:903): in the tube, Teim in
     * anyone's bag starts the Teim and Darum scene -- she comes out of the
     * bag, $DE70 = 1, $DE72 = 0, $DE6E = $302, demo walk 2 and "A
     * Prologue". */
    if (g_data.level_index == LEVEL_DARUM_TUBE
        && level_take_item(gamedata_item_id("Teim"))) {
        g_win.event_step     = 1;
        g_win.event_done     = 0;
        g_level_event  = EVENT_KIND_TEIM_DARUM | TEIM_DARUM_SCENE;
        g_walk_demo    = DEMO_TEIM_SCENE;
        queue_sound(g, MUSIC_DREAM);    /* "A Prologue": after LoadLevelData's, so it wins */
    }

    /* loc_7E02, after LoadSpritesInLevel: the one-shot case event_flags'
     * high byte names. Case $B (loc_7FF2) is ObjType_Events $35's reload:
     * $FFFFE400 cleared, $DE70 = 1, $DE6E = $36, event_flags = $8000 --
     * whose bit 15 loc_7D08 clears again at once, and whose being set
     * there skips loc_9002, the full redraw of both planes (see the
     * README: the port redraws every frame regardless). */
    g_party_gone = false;
    g_event_reload = false;
    g_event_battle = 0;
    g_event_building = false;
    g_gaira_ring   = false;         /* ObjCL_Init: loc_12B06 */
    g_scene_picture     = 0;
    g_scene_no_redraw = false;
    g_scene_camera      = CAMERA_NONE;
    g_scene_timer       = 0;
    g_scene_shake_ax = g_scene_shake_ay = 0;
    g_scene_shake_bx = g_scene_shake_by = 0;
    g_scene_next_set    = false;
    u8  oneshot_next  = SCENE_NONE;
    int arrive_dy     = 0;
    int arrive_facing = FACE_DOWN;
    switch (g_level_oneshot) {
        case SCENE_NONE: {
            break;
        }
        case SCENE_NEI_DYING: {
            /* After the Neifirst battle: the picture, no party object,
             * $DE6E = $26, event_flags = $8200, Power in the queue and
             * $87 in the latch. */
            level_scene_picture(1);
            g_party_gone  = true;
            g_win.event_step    = 1;
            g_win.event_done    = 0;
            g_level_event = EVENT_NEI_DEATH;
            g_scene_no_redraw = true;
            oneshot_next  = SCENE_NEI_DEAD;
            queue_sfx(g, MUSIC_POWER);
            latch_sound(g, MUSIC_ADVANCED);
            break;
        }
        case SCENE_NEI_DEAD: {
            /* The picture and Nei's body, $DE6E = $27, event_flags =
             * $8300; Power again only if Nei is dead -- after the party's
             * own battle with Neifirst, which replaced it. */
            level_scene_picture(2);
            g_party_gone  = true;
            g_win.event_step    = 1;
            g_win.event_done    = 0;
            g_level_event = EVENT_NEI_GONE;
            g_scene_no_redraw = true;
            oneshot_next  = SCENE_CLIMATROL_SHAKES;
            if (g_data.chr[1].hp == 0) {
                queue_sfx(g, MUSIC_POWER);
            }
            latch_sound(g, MUSIC_ADVANCED);
            break;
        }
        case SCENE_CLIMATROL_SHAKES: {
            /* loc_7EE4: $DE6E = $28, event_flags = $403, Motavia at
             * $490,$4A0 for the reload after, $82 in the latch. */
            g_win.event_step    = 1;
            g_win.event_done    = 0;
            g_level_event = EVENT_CLIMATROL_SHAKES;
            g_scene_camera = CAMERA_JITTER;
            oneshot_next  = SCENE_MOTAVIA_BEFORE;
            level_scene_next(LEVEL_MOTAVIA_OUTSIDE, 0x490, 0x4A0);
            latch_sound(g, MUSIC_RESTORATION);
            break;
        }
        case SCENE_MOTAVIA_BEFORE: {
            /* loc_7F14: no party object, 300 frames, event_flags = $501,
             * F7 at $300,$1A0 next, $87. */
            g_party_gone   = true;
            g_scene_timer  = 0x12C;
            g_scene_camera = CAMERA_WAIT_THEN_RELOAD;
            oneshot_next   = SCENE_CLIMATROL_EXPLODES;
            level_scene_next(LEVEL_CLIMATROL_F7, 0x300, 0x1A0);
            latch_sound(g, MUSIC_ADVANCED);
            break;
        }
        case SCENE_CLIMATROL_EXPLODES: {
            /* loc_7F44: $DE6E = $29, 300 frames of the big shake
             * (event_flags = $602), Motavia next, $C735 = 1, $82. */
            g_win.event_step     = 1;
            g_win.event_done     = 0;
            g_level_event  = EVENT_CLIMATROL_EXPLOSIONS;
            g_scene_timer  = 0x12C;
            g_scene_camera = CAMERA_SHAKE_THEN_RELOAD;
            oneshot_next   = SCENE_MOTAVIA_AFTER;
            level_scene_next(LEVEL_MOTAVIA_OUTSIDE, 0x490, 0x4A0);
            g_data.c700[FLAG_CLIMATROL_DESTROYED] = 1;
            latch_sound(g, MUSIC_RESTORATION);
            break;
        }
        case SCENE_MOTAVIA_AFTER: {
            /* loc_7F80: no party object, 300 frames, event_flags = $704,
             * F7 at $300,$1A0 through $F748, $87. */
            g_party_gone   = true;
            g_scene_timer  = 0x12C;
            g_scene_camera = CAMERA_WAIT_THEN_NEXT_LEVEL;
            oneshot_next   = SCENE_RETURN_TO_PASEO;
            level_scene_next(LEVEL_CLIMATROL_F7, 0x300, 0x1A0);
            latch_sound(g, MUSIC_ADVANCED);
            break;
        }
        case SCENE_RETURN_TO_PASEO: {
            /* loc_7FB0: $DE6E = $2A, event_flags = 0, $84 -- Pleasure,
             * for the Paseo the event ends in. */
            g_win.event_step    = 1;
            g_win.event_done    = 0;
            g_level_event = EVENT_RETURN_TO_PASEO;
            latch_sound(g, MUSIC_PLEASURE);
            break;
        }
        case SCENE_GAIRA_PRISON: {
            /* loc_7FDA: the Army Eyes' capture (loc_2E24 left event_flags
             * = $A00) -- $DE70 = 1, $DE6E = $2D, event_flags = 0: the
             * Gaira prison, ObjType_Events $2D (loc_DF32). */
            g_win.event_step    = 1;
            g_win.event_done    = 0;
            g_level_event = EVENT_GAIRA_PRISON;
            break;
        }
        case SCENE_SHIR_GONE: {
            /* loc_8010 (ps2.asm:11624), after ProcessStealItem: $DE70 = 1
             * and $DE6E = $31 -- ObjType_Events $31 on the first idle
             * frame -- and event_flags left for loc_E042 to clear. */
            g_win.event_step    = 1;
            g_win.event_done    = 0;
            g_level_event = EVENT_SHIR_GONE;
            break;
        }
        case SCENE_MOTHER_BRAIN_ARRIVAL: {
            /* loc_8028 (ps2.asm:11632), after the approach: the leader
             * standing facing up ($E424 = 0) one cell further down ($E40E
             * + $10) -- the followers take their places from him -- her
             * image and the two energy arcs, and the confrontation with no
             * button. event_flags stays $E00 until the confrontation's
             * first step clears it, and nothing can reload before then. */
            arrive_dy     = 0x10;
            arrive_facing = FACE_UP;
            npc_spawn_mother_brain_image(&g_npcs);
            g_win.event_step = 1;
            g_win.event_done = 0;
            g_level_event = EVENT_MOTHER_BRAIN_CONFRONTATION;
            break;
        }
        case SCENE_MOTHER_BRAIN_DEFEATED: {
            /* loc_8070 (ps2.asm:11651), on the level after her battle:
             * the aftermath with no button. */
            g_win.event_step = 1;
            g_win.event_done = 0;
            g_level_event = EVENT_MOTHER_BRAIN_AFTERMATH;
            break;
        }
        case SCENE_PANDORAS_BOX_OPENED: {
            g_party_gone  = true;
            g_win.event_step    = 1;
            g_win.event_done    = 0;
            g_level_event = EVENT_PANDORAS_BOX_OPENED;
            break;
        }
        case SCENE_ESPER_B1_HOLD: {
            /* loc_8022 (ps2.asm:11631) is `rts`: the word stays $D00, so
             * the hold outlasts the load -- the leader still, A and the C
             * menu shut -- until Building_EsperMansion clears it. The port
             * keeps that byte as g_event_hold, which no load touches. */
            g_event_hold = SCENE_ESPER_B1_HOLD;
            break;
        }
        case SCENE_OPENING: {
            /* loc_8082 (ps2.asm:11656): $DE70 = 1, $DE6E = $3B, the word
             * left as it is. Only MoveToOpeningScreen's $1007 brings it,
             * and the port runs the opening from its own path (the crawl,
             * enter_level's opening branch), not through this load. */
            fprintf(stderr, "level: one-shot case $10 (the opening) on a "
                            "walking level: the opening is the crawl's\n");
            break;
        }
        default: {
            if (g_level_oneshot > SCENE_OPENING) {
                /* loc_8094 (ps2.asm:11662): any case past the last,
                 * `move.w #0, (event_flags).w` -- the one-shot and the
                 * camera byte both cleared, as they already are here. */
                break;
            }
            /* Cases 8 and 9 (loc_7FCE, loc_7FD4) are `rts` and nothing
             * sets them. */
            fprintf(stderr, "level: one-shot event $%02X is not ported\n",
                    g_level_oneshot);
            break;
        }
    }
    g_level_oneshot = oneshot_next;
    g_npcs.party_gone = g_party_gone;
    g_ending_cast_placed = false;

    /* LevelScreen spawns one ObjID_FollowingCharacters per party member
     * after the first (loc_7CF6) -- none when $E400 is empty. */
    g_walk.riding = level_riding();
    walk_begin(&g_walk, &g_level, g_level.tileset,
               g_data.level_x, g_data.level_y + arrive_dy, arrive_facing,
               g_walk_demo, g_party_gone ? 0 : g_data.party_members_num);

    /* LevelScreen (ps2.asm:11418): RunObjects, RenderCharSprites and
     * BuildSprites once before the fade in, so the first frame shown
     * already has the chests and the people in it. */
    npc_setup(&g_npcs, g_walk.width, g_walk.height, g_walk.cam_x,
              g_walk.cam_y);

    if (getenv("PS2_LEVELINFO") != NULL) {
        int anim = -1;
        for (int i = 0; i < g_tile_anim_count; i++) {
            if (g_tile_anim[i].level == g_level.index) {
                anim = i;
            }
        }
        if (anim >= 0) {
            fprintf(stderr, "level $%02X: tile animation, %d frames to "
                            "tile $%03X every %d fields\n", g_level.index,
                    g_tile_anim[anim].frame_count, g_tile_anim[anim].tile,
                    g_tile_anim[anim].period);
        } else {
            fprintf(stderr, "level $%02X: no tile animation (%d loaded)\n",
                    g_level.index, g_tile_anim_count);
        }
    }

}

static void enter_level(Game *g)
{

    /* LevelScreen (11338): $FFFFCB0C, the encounter counter, is zeroed
     * on every entry -- level change, leaving a building, after a
     * battle -- and fight_active_flag with it. */
    g_encounter.counter = 0;

    if (g_level_walk) {
        enter_level_walk(g);
        return;
    }

    g_level_ready = load_level(OPENING_LEVEL);
    if (!g_level_ready) {
        return;
    }

    if (g_opening_script.count == 0) {
        char lang[192];
        script_load(&g_opening_script,
                    lang_path("assets/script/Script_Opening.txt", lang,
                              sizeof lang));
    }


    /* LoadLevelData sends the Nemesis tile art to VDP address 0, so the
     * block definitions index the tileset from tile 0. */
    tileset_clear(&g_vram);
    tileset_place(&g_vram, &g_level_art, 0);

    /* FontsIconsArt at $500, the same place every other screen puts it,
     * so the window frame and glyph codes index it directly. It has to
     * follow the clear, not precede it. */
    tileset_place(&g_vram, &g_font, TILE_FONT);

    /* Two PalettePtrs entries: index 2 is lines 0 and 1 and is the same
     * for every level; the level's own index is lines 2 and 3. */
    video_colours_clear(g->video, &g->video->palette);
    video_colours_load(g->video, &g->video->palette, 0, PALETTE_FIELD_COMMON);
    video_colours_load(g->video, &g->video->palette, 2, g_level.palette);
    level_mist(g);

    /* No music is queued. MoveToOpeningScreen writes $82 straight into
     * the last-sound latch at $FFFFF640 without going through the sound
     * queue, so when LoadLevelData asks for Restoration it matches the
     * latch and is dropped -- the title theme plays on across the
     * transition. Queueing it here would start music the original never
     * starts. */

    g->cam_x = (s16)(OPENING_X - CAMERA_OFFSET_X);
    g->cam_y = (s16)(OPENING_Y - CAMERA_OFFSET_Y);
    g->crawl_step = 0;
    g->crawl_hold = CRAWL_HOLD_FRAMES;
    g->crawl_close = 0;
    if (g_debug_script_fn != NULL) {
        g_debug_script_fn(g_debug_script_ctx, CRAWL[0].message);
    }
    g->video->backdrop = 0;
}

/* One pixel per field toward the target, per axis, which is what
 * loc_8192 computes into $FFFFF724 / $FFFFF726 before loc_8BBA applies
 * it. Returns true once both axes have arrived. */
static bool crawl_step_camera(Game *g, s16 target_x, s16 target_y)
{
    s16 dx = (s16)(target_x - g->cam_x);
    s16 dy = (s16)(target_y - g->cam_y);

    if (dx != 0) {
        g->cam_x = (s16)(g->cam_x + ((dx > 0) ? 1 : -1));
    }
    if (dy != 0) {
        g->cam_y = (s16)(g->cam_y + ((dy > 0) ? 1 : -1));
    }
    return (dx == 0) && (dy == 0);
}

/* loc_E584's test for the Dezolians and the musk cats: Rolf's HP
 * (character_data_buffer + 2) and the byte at $C020, his head, compared
 * as it is -- no mask. */
static int rolf_cap(void)
{
    if (g_data.chr[0].hp == 0) {
        return NPC_CAP_NONE;
    }
    int head = g_data.chr[0].equip[EQUIP_HEAD];
    if (head == gamedata_item_id("MagicCap")) {
        return NPC_CAP_MAGIC;
    }
    if (head == gamedata_item_id("MogicCap")) {
        return NPC_CAP_MOGIC;
    }
    return NPC_CAP_NONE;
}

/* loc_9BFE for the item menu: what Level_ChkTargetInteract finds in the
 * cell the leader faces, as its $2E word, or -1. */
static int level_facing_object(void *ctx)
{
    (void)ctx;
    const Npc *n = npc_in_the_way(&g_npcs, NULL, g_walk.x, g_walk.y,
                                  g_walk.facing / 3);
    npc_note_leader_check(&g_npcs, g_walk.facing / 3, true, n);
    return (n != NULL) ? (n->interaction << 8 | n->index) : -1;
}

/* loc_E84A (ps2.asm:21438), where a chest with an item in it and the
 * events that hand one over all end up: `item` to the first member in
 * party order who is standing and has a free slot. $1702 ("... has found
 * ...") then $1703, and `*flag` + 1 -- or, with nobody, $1702 then $1704
 * and the flag left alone. character_index is left on whoever took it,
 * or on the last one looked at.
 *
 * The ROM also plays SXFID_ItemReceived on a success and adds 1 to
 * $DE72 either way; those are left to the callers, since the chests'
 * two-visit path does not use $DE72. Returns whether it was taken. */
static bool level_give_item(u8 item, u8 *flag)
{
    int last = g_data.party_members_num;
    if (last >= GD_PARTY_MAX) {
        last = GD_PARTY_MAX - 1;
    }

    g_win.item_index = item;
    for (int i = 0; i <= last; i++) {
        int c = g_data.party_member_id[i] & 7;
        g_win.character_index = (u16)c;
        if (g_data.chr[c].hp == 0) {
            continue;
        }
        u8 *count;
        u8 *bag = gamedata_bag(&g_data, c, &count);
        if (*count < GD_ITEM_SLOTS) {
            (*flag)++;
            bag[(*count)++] = item;
            win_script_set2(&g_win, SCRIPT_ACTION_HAS_FOUND, SCRIPT_ACTION_GOTTEN_HOLD);
            return true;
        }
    }
    win_script_set2(&g_win, SCRIPT_ACTION_HAS_FOUND, SCRIPT_ACTION_BUT_CANT_HOLD_ANY);
    return false;
}

/* ObjType_TreasureChests (ps2.asm:21386), the first visit. The window
 * is already open; the second visit is level_talk's CloseAllWindows.
 *
 * treasure_chest_flags ($C780 + index): 0 shut, bit 7 locked, anything
 * else opened. A locked chest says so -- unless it holds nothing, in
 * which case it opens onto garbage anyway. Meseta goes to the first
 * member standing. An item goes through loc_E84A (level_give_item):
 * with nobody to take it, the chest stays shut to try again.
 *
 * Returns SXFID_ItemReceived when something was taken, else 0. It is a
 * move.b to the same sound_queue byte ProcessAButAction wrote
 * SXFID_Selection to that frame, so it replaces the click.
 *
 * AddToMoneyOwned caps the total at 99,999,999 only when the amount
 * added is that large; a chest's is a word, so it never is, and the
 * cap is left out. */
static u8 level_open_chest(int index)
{
    if (index == 0) {
        win_script_set(&g_win, SCRIPT_ACTION_THERE_NOTHING_INSIDE);     /* "There is nothing inside." */
        return 0;
    }
    u8  *flag = &g_data.c700[(FLAG_CHESTS + index) & 0xFF];
    u16  word = npc_chest_content(index);
    int  last = g_data.party_members_num;
    if (last >= GD_PARTY_MAX) {
        last = GD_PARTY_MAX - 1;
    }

    if (*flag != 0) {
        if ((*flag & 0x80) == 0) {
            win_script_set(&g_win, SCRIPT_ACTION_THERE_NOTHING_INSIDE);
            return 0;
        }
        if (word != 0) {
            win_script_set(&g_win, SCRIPT_ACTION_ITS_LOCKED_WONT_OPEN); /* "It's locked ..." */
            return 0;
        }
        *flag = 1;
        win_script_set(&g_win, SCRIPT_ACTION_ITS_FULL_GARBAGE);     /* "It's full of garbage." */
        return 0;
    }

    if (word == 0) {
        *flag = 1;
        win_script_set(&g_win, SCRIPT_ACTION_ITS_FULL_GARBAGE);
        return 0;
    }

    if ((word & 0x8000) == 0) {
        g_win.meseta_value = word;
        g_data.money += word;
        *flag = 1;
        win_script_set(&g_win, SCRIPT_ACTION_GOTTEN_HOLD_MESETA);
        for (int i = 0; i <= last; i++) {
            int c = g_data.party_member_id[i] & 7;
            g_win.character_index = (u16)c;
            if (g_data.chr[c].hp != 0) {
                break;
            }
        }
        return SFX_ITEM_RECEIVED;
    }

    return level_give_item((u8)word, flag) ? SFX_ITEM_RECEIVED : 0;
}

/* ObjType_TeimDarum's scene (loc_E9BA), one step per idle visit; d2 is
 * $DE70. Steps 1, 5, 8 and 10 wait on the demo walk or on Teim and Darum
 * themselves, which move $DE70 on (npc.c). */
static void level_teim_scene(u16 d2)
{
    Npc *teim  = npc_slot(&g_npcs, 0);      /* $FFFFE800 */
    Npc *darum = npc_slot(&g_npcs, 1);      /* $FFFFE840 */

    switch (d2) {
        case 2: {
            g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
            win_script_set(&g_win, SCRIPT_EVENT_WHATS_TEIM_DOING);     /* "What's Teim doing?" */
            g_win.event_step++;
            break;
        }
        case 3: {
            /* "I'm going to go meet my father ..." and Teim out of the
             * bag, a cell left of the leader, $24 = 1. */
            win_script_set(&g_win, SCRIPT_EVENT_IM_GOING_GO_MEET);
            npc_put(&g_npcs, 0, NPC_OBJ_TEIM, g_walk.x - 16, g_walk.y, 1);
            g_win.event_step++;
            break;
        }
        case 4: {
            if (teim != NULL) {
                teim->routine = 1;
            }
            g_win.event_step++;
            break;
        }
        case 6: {
            win_script_set(&g_win, SCRIPT_EVENT_HEY_GIRL_GIVE_ME);
            if (darum != NULL) {
                darum->routine = 1;
            }
            g_win.event_step++;
            break;
        }
        case 7: {
            /* Once Darum has stopped walking ($FFFFE854). */
            if (darum == NULL || darum->vx == 0) {
                if (darum != NULL) {
                    darum->routine = 2;
                }
                g_win.event_step++;
            }
            break;
        }
        case 9: {
            win_script_set(&g_win, SCRIPT_EVENT_UHHHH_FATHER_KILLING_TEIM);
            g_win.event_step++;
            break;
        }
        case 10: {
            if (teim != NULL && teim->routine == 5) {
                teim->routine = 6;
            }
            break;
        }
        case 11: {
            /* The window shut and demo walk 3 on past where he stood. */
            if (g_walk.demo == NULL) {
                g_win.window_index[0] = WIN_CLOSE(1);
                walk_start_demo(&g_walk, DEMO_AFTER_TEIM_DEATH);
            }
            break;
        }
        case 12: {
            g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
            win_script_set(&g_win, SCRIPT_EVENT_JUST_ONE_MANY_TRAGEDIES);
            g_win.event_step++;
            break;
        }
        default: {
            if (d2 <= 12) {
                break;              /* 0, 1, 5, 8: waiting */
            }
            /* loc_EAB0: done for good. */
            g_data.c700[FLAG_DARUM_SCENE_DONE] = 1;
            g_walk.demo = NULL;
            g_win.event_done++;
            break;
        }
    }
}

/* ObjType_TeimDarum (ps2.asm:21587), by $DE6F:
 *   1  A on Darum while Teim is not with the party -- "I've seen that
 *      face! ..." in the big box
 *   2  the scene LoadSpritesInLevel starts
 *   3  message 6 naming Rolf
 * With $DE72 set, the next idle visit is CloseAllWindows. */
static void level_teim_darum(u16 d2)
{
    static int trace = -1;
    if (trace < 0) {
        trace = (getenv("PS2_ATRACE") != NULL) ? 1 : 0;
    }
    if (trace) {
        fprintf(stderr, "TeimDarum: event $%03X, d2 %u, $DE72 %u%s\n",
                g_level_event, d2, g_win.event_done,
                (g_win.event_done != 0) ? " -> CloseAllWindows" : "");
    }
    if (g_win.event_done != 0) {
        win_close_all(&g_win);
        g_level_event = 0;
        return;
    }
    switch (g_level_event & 0xFF) {
        case 1: {
            g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
            win_script_set(&g_win, SCRIPT_EVENT_IVE_SEEN_FACE_TRIED);
            g_win.event_done++;
            break;
        }
        case 2: {
            level_teim_scene(d2);
            break;
        }
        case 3: {
            g_win.window_index[0] = WIN_SCRIPT_MESSAGE;
            g_win.character_index = 0;
            win_script_set(&g_win, SCRIPT_ITEM_IS_DEAD);
            g_win.event_done++;
            break;
        }
        default: {
            /* Index 0 is ObjType_Events' plain line (loc_D9AA). */
            fprintf(stderr, "level: TeimDarum index $%02X is not ported\n",
                    g_level_event & 0xFF);
            g_win.event_done++;
            break;
        }
    }
}

/* loc_E84A as the events use it: SXFID_ItemReceived on a success, and
 * $DE72 + 1 either way, so the box closes on the next visit unless the
 * caller clears it again. */
static bool level_event_item(Game *g, u8 item, u8 *flag)
{
    bool taken = level_give_item(item, flag);
    if (taken) {
        queue_sfx(g, SFX_ITEM_RECEIVED);
    }
    g_win.event_done++;
    return taken;
}

/* CheckItemExistInventory (ps2.asm:19067): party order, each bag's 16
 * slots with bit 7 (worn) masked off. The holder goes to
 * character_index and the slot to $DE84; -1 when nobody has it. */
static int level_find_item(u8 item, int *slot)
{
    int last = g_data.party_members_num;
    if (last >= GD_PARTY_MAX) {
        last = GD_PARTY_MAX - 1;
    }
    for (int i = 0; i <= last; i++) {
        int c = g_data.party_member_id[i] & 7;
        g_win.character_index = (u16)c;
        u8 *count;
        u8 *bag = gamedata_bag(&g_data, c, &count);
        for (int s = 0; s < GD_ITEM_SLOTS; s++) {
            if ((bag[s] & 0x7F) == item) {
                *slot = s;
                return c;
            }
        }
    }
    return -1;
}

/* RemoveItemFromInventory2 (ps2.asm:16065): the count down one and the
 * slots above `slot` moved down, the last cleared. */
static void level_remove_item(int c, int slot)
{
    u8 *count;
    u8 *bag = gamedata_bag(&g_data, c, &count);
    (*count)--;
    for (int s = slot; s < GD_ITEM_SLOTS - 1; s++) {
        bag[s] = bag[s + 1];
    }
    bag[GD_ITEM_SLOTS - 1] = 0;
}

/* ObjType_Events $17 (loc_DC36, ps2.asm:20180): Teim in Nido. The big
 * box; with the flag set, or nobody carrying the Letter, "I'm Teim,
 * daughter of Darum ..." ($1807) and the box closes on the next button.
 * With the Letter, "{char} hands over {item} ..." ($1808), and the next
 * idle visit (loc_DC64) sets the flag -- which is what makes Obj_Teim
 * take herself off the level -- takes the Letter from its holder, puts
 * Teim into that same bag (AddItemToInventory2 with character_index),
 * plays SXFID_ItemReceived and closes the box. */
static void level_teim(Game *g, u16 d2, u8 *flag)
{
    int slot = 0;
    if (d2 == 0) {
        g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
        if (*flag != 0 || level_find_item(ITEM_LETTER, &slot) < 0) {
            win_script_set(&g_win, SCRIPT_EVENT_IM_TEIM_DAUGHTER_DARUM);
            g_win.event_done++;
            return;
        }
        g_win.item_index = ITEM_LETTER;
        win_script_set(&g_win, SCRIPT_EVENT_HANDS_OVER_TEIMS_HANDS);
        return;
    }
    (*flag)++;
    int c = level_find_item(ITEM_LETTER, &slot);
    if (c >= 0) {
        level_remove_item(c, slot);
    } else {
        c = g_win.character_index;
    }
    u8 *count;
    u8 *bag = gamedata_bag(&g_data, c, &count);
    if (*count < GD_ITEM_SLOTS) {
        bag[(*count)++] = ITEM_TEIM;
    }
    g_win.item_index = ITEM_TEIM;
    queue_sfx(g, SFX_ITEM_RECEIVED);
    g_win.event_done++;
}

/* ObjType_Events $11 (loc_DBB2, ps2.asm:20127): the body on Shure's top
 * floor with the Small Key and the Letter in its pocket. event_flags $11
 * ($C721) counts what has been taken: 0 nothing, 1 the key, 2 both.
 *
 *   d2 0  "Those are bodies of the scoundrels ..." ($170B). With the flag
 *         at 2 that is all, and the box closes; otherwise "Oh, my! There
 *         is something inside the pocket." ($170C) follows it.
 *   d2 1  $DE70 + 1. With the flag at 1, straight on to the Letter.
 *         Otherwise the Small Key: taken, $DE72 is cleared again so the
 *         box stays up for the Letter on the next visit; nobody with
 *         room, and the box closes with the flag still 0.
 *   d2 2  the Letter; the box closes either way, the flag at 2 if it
 *         was taken and still at 1 if not. */
static void level_dead_body(Game *g, u16 d2, u8 *flag)
{
    if (d2 == 0) {
        if (*flag == 2) {
            win_script_set(&g_win, SCRIPT_ACTION_THOSE_BODIES_SCOUNDRELS_WONDER);
            g_win.event_done++;
        } else {
            win_script_set2(&g_win, SCRIPT_ACTION_THOSE_BODIES_SCOUNDRELS_WONDER, SCRIPT_ACTION_OH_THERE_SOME_THING);
        }
        return;
    }
    if (d2 == 1) {
        g_win.event_step++;
        if (*flag != 1) {
            if (level_event_item(g, ITEM_SMALL_KEY, flag)) {
                g_win.event_done = 0;
            }
            return;
        }
    }
    level_event_item(g, ITEM_LETTER, flag);     /* loc_DBF2 */
}

/* ObjType_Events $07 (loc_DAD4, ps2.asm:20054): the Control Tower's
 * keyboard. d2 0: "Here's a key board. Oh, here's some music, too. Shall
 * I touch it?" ($1711). d2 1: WinID_YesNo2 and $DE70 + 1. d2 2: YES is
 * a jump to where an Escapipe goes -- the way into the last dungeon,
 * GD_ENTRANCE_* ($C64C level, $C64E y, $C650 x) --
 * screen_changed_flag = -1 and SXFID_Teleport, waited out in place;
 * NO nothing. $DE72 + 1 either way. The ROM leaves the scooter flag
 * alone here; the port clears it, which is the same thing in the tower. */
static void level_keyboard(Game *g, u16 d2)
{
    if (d2 == 0) {
        win_script_set(&g_win, SCRIPT_ACTION_HERES_KEY_BOARD_OH);
        return;
    }
    if (d2 == 1) {
        win_open(&g_win, WIN_YESNO2);
        g_win.event_step++;
        return;
    }
    if (g_win.yes_no_input == 0) {
        g_data.level_index = gamedata_word(&g_data, GD_ENTRANCE_MAP);
        g_data.level_y     = gamedata_word(&g_data, GD_ENTRANCE_Y);
        g_data.level_x     = gamedata_word(&g_data, GD_ENTRANCE_X);
        g_data.jet_scooter = 0;
        queue_sfx(g, SFX_TELEPORT);
        start_sound_wait(g);
        g_event_reload = true;
    }
    g_win.event_done++;
}

/* ObjType_Events $10 (loc_DB90, ps2.asm:20113): the Maruera tree on
 * Uzo. d2 0: with the flag clear "This is it! This is the Maruera-tree."
 * ($1710) and nothing else, so the box stays up; set, $1708 and the box
 * closes. d2 1 (loc_DBAC): loc_E84A with the Maruera Leaf. */
static void level_maruera_tree(Game *g, u16 d2, u8 *flag)
{
    if (d2 == 0) {
        if (*flag == 0) {
            win_script_set(&g_win, SCRIPT_ACTION_MARUERA_TREE);
        } else {
            win_script_set(&g_win, SCRIPT_ACTION_THERE_SEEMS_NOTHING_UNUSUAL);
            g_win.event_done++;
        }
        return;
    }
    level_event_item(g, ITEM_MRURA_LEAF, flag);
}

/* ObjType_Events $12 (loc_DBF8, ps2.asm:20156): the Recorder in the
 * Biosystems Lab's basement. No d2 test: on the first visit, with the
 * flag clear, loc_E84A with the Recorder; taken, and with
 * party_member_join_next at 3, it goes to 4 -- the next knock at Rolf's
 * door. The flag set, $1708 and the box closes. */
static void level_recorder(Game *g, u8 *flag)
{
    if (*flag != 0) {
        win_script_set(&g_win, SCRIPT_ACTION_THERE_SEEMS_NOTHING_UNUSUAL);
        g_win.event_done++;
        return;
    }
    if (level_event_item(g, ITEM_RECORDER, flag)
            && g_data.party_member_join_next == 3) {
        g_data.party_member_join_next++;
    }
}

/* ObjType_Events $13-$16 (loc_DC1E, ps2.asm:20171): Control Tower F1's
 * four card boxes. No d2 test either: the flag clear, loc_E84A with item
 * event - $E -- the Green, Blue, Yellow and Red cards ($5-$8); set,
 * $1708. */
static void level_card(Game *g, int event, u8 *flag)
{
    if (*flag != 0) {
        win_script_set(&g_win, SCRIPT_ACTION_THERE_SEEMS_NOTHING_UNUSUAL);
        g_win.event_done++;
        return;
    }
    level_event_item(g, (u8)(event - EVENT_TOWER_GREEN_CARD + ITEM_GREEN_CARD), flag);
}

/* EventType_DialogueIndex's door entries: A on a door or a dam's panel
 * is one line, and the box closes on the next button. loc_DC84 for the
 * dynamite doors ($18-$1A): $170D, "Maybe dynamite can open this door."
 * loc_DC90 for the tube's locked door and Control Tower F1's ($1B, $24):
 * $170E. loc_DC9C for the four dams' doors and panels ($1C-$23): $171B.
 * 0 for any other event. */
static u16 level_door_line(int event)
{
    if (event >= EVENT_DYNAMITE_DOOR_1 && event <= EVENT_DYNAMITE_DOOR_3) {
        return SCRIPT_ACTION_MAYBE_DYNAMITE_CAN_OPEN;
    }
    if (event == EVENT_STRONG_SHUTTER || event == EVENT_STRONG_SHUTTER_2) {
        return SCRIPT_ACTION_SEEMS_LIKE_VERY_STRONG;
    }
    if (event >= EVENT_DAM_LOCK_1 && event <= EVENT_DAM_LOCK_8) {
        return SCRIPT_ACTION_THERES_PLACE_PUT_CARD;
    }
    return 0;
}

/* The ObjType_Events numbers A can start that are ported, by the index
 * byte of the target's $2E. Any other event object keeps the plain line
 * and the note on stderr in level_talk. */
static bool level_event_ported(int index)
{
    return (index >= EVENT_ARIMA_BLOWN_DOOR && index <= EVENT_FAKE_MARUERA_TREE)
        || index == EVENT_JET_SCOOTER_PARKED
        || index == EVENT_KEYBOARD
        || (index >= EVENT_SKURE_NEWSPAPER_1 && index <= EVENT_TEIM)
        || (index >= EVENT_DYNAMITE_DOOR_1 && index <= EVENT_STRONG_SHUTTER_2)
        || index == EVENT_NEIFIRST
        || index == EVENT_GAIRA_CONTROL_PANEL
        || index == EVENT_ESPER_MANSION_LUTZ
        || index == EVENT_PANDORAS_BOX
        || index == EVENT_MOUNT_UZO;
}

/* loc_DDA2 / DetectLeadingCharacter (ps2.asm:20303): character_index for
 * {char} -- Rolf while he stands, else the first member with HP, else
 * whoever was looked at last. */
static void level_name_leader(void)
{
    if (g_data.chr[0].hp != 0) {
        g_win.character_index = 0;
        return;
    }
    for (int i = 0; i <= g_data.party_members_num && i < GD_PARTY_MAX; i++) {
        g_win.character_index = g_data.party_member_id[i];
        if (g_data.chr[g_data.party_member_id[i] & 7].hp != 0) {
            return;
        }
    }
}

/* ObjType_Events $25, loc_DCA8 (ps2.asm:20217): A on Neifirst on
 * Climatrol F7, one step per idle visit with d2 = $DE70:
 *   0  the big box, $180A then $180C ("What's this? She looks just like
 *      Nei! ... you don't mean ...?")
 *   1  Nei dead ($C042 = 0): $180E, "It matters not, because Nei is
 *      dead.", and done. Else $180F, $1810 (Nei and Neifirst), $DE70 + 1
 *   2  WinID_YesNo2, $DE70 + 1
 *   3  NO: $1822 ("Nei stood with her head down ...") and done. YES:
 *      $1823, $DE70 + 1
 *   4  $DE72 + 1, formation $100, ScreenID_Battle, event_flags = $100;
 *      the party saved to $C618/$C61C/$C624 and replaced by Nei alone. */
static void level_neifirst(Game *g, u16 d2)
{
    (void)g;
    switch (d2) {
        case 0: {
            g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
            win_script_set2(&g_win, SCRIPT_EVENT_WHATS_LOOKS_JUST_LIKE, SCRIPT_EVENT_CANT_DONT_MEAN);
            break;
        }
        case 1: {
            if (g_data.chr[1].hp == 0) {
                win_script_set(&g_win, SCRIPT_EVENT_MATTERS_NOT_BECAUSE_NEI);
                g_win.event_done++;
                break;
            }
            win_script_set2(&g_win, SCRIPT_EVENT_NO_DONT_HATE_HUMANS, SCRIPT_EVENT_FOOL_SAY_SUCH_THINGS);
            g_win.event_step++;
            break;
        }
        case 2: {
            win_open(&g_win, WIN_YESNO2);
            g_win.event_step++;
            break;
        }
        case 3: {
            if (g_win.yes_no_input != 0) {
                win_script_set(&g_win, SCRIPT_EVENT_NEI_STOOD_HER_HEAD);
                g_win.event_done++;
                break;
            }
            win_script_set(&g_win, SCRIPT_EVENT_DONT_WANT_MAKE_ANY);
            g_win.event_step++;
            break;
        }
        default: {
            g_win.event_done++;
            for (int i = 0; i < GD_PARTY_MAX; i++) {
                gamedata_set_word(&g_data, GD_PARTY_HELD + 2 * i, g_data.party_member_id[i]);
            }
            gamedata_set_word(&g_data, GD_PARTY_HELD_NUM, g_data.party_members_num);
            g_data.party_member_id[0] = 1;          /* move.l #$10000 */
            g_data.party_member_id[1] = 0;
            g_data.party_member_id[2] = 0;          /* move.l #0, $C60C */
            g_data.party_member_id[3] = 0;
            g_data.party_members_num  = 0;
            g_event_battle  = 0x100;
            g_level_oneshot = SCENE_NEI_DYING;
            break;
        }
    }
}

/* ObjType_Events $26, loc_DD52 (20266), started by loc_7E02 case 1 with
 * $DE70 at 1:
 *   1  the big box: $1826 ("... there's no hope for me ...") with Nei
 *      alone in the party -- she won -- or $183D ("Nei, hold on!", on into
 *      $183E) with the party back, which loc_F1F6 did when she went down;
 *      $DE70 + 1 and loc_DDA2
 *   2  Nei alone: the party back from $C618 and the level again. The
 *      party: ScreenID_Battle -- the whole party fights her. */
static void level_nei_death(Game *g, u16 d2)
{
    (void)g;
    if (d2 == 0) {
        return;
    }
    if (d2 == 1) {
        g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
        win_script_set(&g_win, (g_data.party_members_num == 0) ? SCRIPT_EVENT_THERES_NO_HOPE_ME
                                                              : SCRIPT_EVENT_NEI_HOLD_THERES_NO);
        g_win.event_step++;
        level_name_leader();
        return;
    }
    if (g_data.party_members_num != 0) {
        g_event_battle = 0x100;
        return;
    }
    for (int i = 0; i < GD_PARTY_MAX; i++) {
        g_data.party_member_id[i] = gamedata_word(&g_data, GD_PARTY_HELD + 2 * i);
    }
    g_data.party_members_num = gamedata_word(&g_data, GD_PARTY_HELD_NUM);
    level_scene_reload();
}

/* ObjType_Events $27, loc_DDD0 (20309), from loc_7E02 case 2:
 *   1  $C737 = 1 (a0 is event_flags + $27), the big box, $1827 ("{char}
 *      calls Nei's name once again ..."), $DE70 + 1, loc_DDA2
 *   2  $C610 = 0; Nei's entry taken out of party_member_id, the ones
 *      after it moved up and the last zeroed; party_members_num - 1; the
 *      level again. */
static void level_nei_gone(Game *g, u16 d2)
{
    (void)g;
    if (d2 == 0) {
        return;
    }
    if (d2 == 1) {
        g_data.c700[FLAG_NEI_GONE] = 1;
        g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
        win_script_set(&g_win, SCRIPT_EVENT_CALLS_NEIS_NAME_ONCE);
        g_win.event_step++;
        level_name_leader();
        return;
    }
    memset(&g_data.c600_raw[GD_ORDER_NEW], 0, 4);
    for (int i = 0; i < GD_PARTY_MAX; i++) {
        if (g_data.party_member_id[i] != 1) {       /* CharID_Nei */
            continue;
        }
        for (int j = i; j < GD_PARTY_MAX - 1; j++) {
            g_data.party_member_id[j] = g_data.party_member_id[j + 1];
        }
        g_data.party_member_id[GD_PARTY_MAX - 1] = 0;
        break;
    }
    g_data.party_members_num--;
    level_scene_reload();
}

/* ObjType_Events $2A, loc_DEC6 (20416), from loc_7E02 case 7:
 *   1  the explosions again (timers 0-63), the big box, $1828 ("What has
 *      happened? {char} and the others quickly decide to return to
 *      Paseo.") -- loc_DE52 itself moves $DE70 on
 *   2  $DE72 + 1; Paseo at x $20, y $170; jet_scooter_flag = 0; the clone
 *      lab with portrait $B at the end of demo walk 6; the level again. */
static void level_return_to_paseo(Game *g, u16 d2)
{
    if (d2 == 0) {
        return;
    }
    if (d2 == 1) {
        npc_spawn_explosions(&g_npcs, &g->rng, false);
        g_win.end_timer = 0x12C;
        g_win.event_step++;
        g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
        win_script_set(&g_win, SCRIPT_EVENT_WHAT_HAPPENED_OTHERS_QUICKLY);
        return;
    }
    g_win.event_done++;
    g_scene_next_set    = false;
    g_data.level_index  = LEVEL_PASEO;
    g_data.level_y      = 0x170;
    g_data.level_x      = 0x20;
    g_data.jet_scooter  = 0;
    g_bstate.building   = BUILDING_CLONE_LAB;
    g_bstate.portrait   = 0x0B;
    g_bstate.demo_flag  = true;
    g_bstate.demo_index = DEMO_INDEX_CLIMATROL_TO_CLONE_LAB;
    g_event_reload      = true;
}

/* ObjType_Events $2C and $2D, loc_DF32 (ps2.asm:20448); $2D is the one
 * loc_7E02 case $A starts on arriving at Gaira with $DE70 at 1:
 *   0  nothing
 *   1  the big box, $1839; $DE72 + 1 (the next idle visit closes it);
 *      demo_timer = $708 and event_flags = 6, camera case 6 (loc_815C);
 *      loc_6D5A four times, every character's $40 bytes copied to $DA00;
 *      loc_ADB0, every character's equipment bonuses off; for each of the
 *      eight: TP 0, only PrsnClths worn, the bag a PlsmRing and the
 *      PrsnClths; SetCharEquipment (gamedata_prison). */
static void level_prison(Game *g, u16 d2)
{
    (void)g;
    if (d2 == 0) {
        return;
    }
    g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
    win_script_set(&g_win, SCRIPT_EVENT_SATELLITE_GAILA_THERE_NO);
    g_win.event_done++;
    g_scene_timer  = 0x708;
    g_scene_camera = CAMERA_GAIRA_COUNTDOWN;
    gamedata_prison(&g_data);
}

/* ObjType_Events $2E, loc_DFA8 (20487), from camera case 6:
 *   1  SXFID_Explosion, demo_timer = $78, $DE70 + 1
 *   2  demo_timer down to 0 (one visit a frame, the last one only
 *      finding it at 0); then $C73E = 1 (a0 is event_flags + $2E), the
 *      big box, $1812 ("ROLF hears a large explosion ... fix the Gaila's
 *      orbit!"), $DE72 + 1. $C73E is what drifts Gaira's backdrop and
 *      sounds its alarm (level_palette_cycle). */
static void level_prison_blast(Game *g, u16 d2, u8 *flag)
{
    if (d2 == 0) {
        return;
    }
    if (d2 == 1) {
        queue_sfx(g, SFX_EXPLOSION);
        g_scene_timer = 0x78;
        g_win.event_step++;
        return;
    }
    if (g_scene_timer != 0) {
        g_scene_timer--;
        return;
    }
    *flag = 1;
    g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
    win_script_set(&g_win, SCRIPT_EVENT_HEARS_LARGE_EXPLOSION_FEELS);
    g_win.event_done++;
}

/* ObjType_Events $2F, loc_DFE6 (20513): A on Gaira's one object, the
 * invisible block ($37) at $360,$1A0. No test of d2 or of $C73E: $C73F = 1,
 * building_index = BuildingID_GairaControlPanel, portrait_index = $1B,
 * game_screen = ScreenID_Building. */
static void level_gaira_panel(u8 *flag)
{
    *flag = 1;
    g_bstate.building = BUILDING_GAIRA_PANEL;
    g_bstate.portrait = 0x1B;               /* the Radar */
    g_event_building  = true;
}

/* ObjType_Events (ps2.asm:19862), for an event object the A button
 * found: $DE6F is the event number and a0 is event_flags + that number
 * ($C710 + n). The first visit is level_talk's with d2 = 0, then one a
 * frame the windows are idle with d2 = $DE70. With $DE72 set, the next
 * visit is CloseAllWindows. */
/* The Mother Brain confrontation (ObjType_Events $37, loc_E122,
 * ps2.asm:20617), started by the arrival scene with no button. One step
 * per idle visit:
 *
 *   1  nothing until her image stands formed (its routine 3, $EC22); then
 *      her glow on ($F656), the big box and $1813 ("So this is the Mother
 *      Brain controlling Algo. ... Will they have to fight ...?"), and
 *      event_flags cleared (the port's pending scene is already 0)
 *   2  WinID_YesNo2
 *   3  YES: $1814, which runs on into $1815 ("... If that is your aim,
 *      disable me! If not, return now!"), step 4. NO: $1816 into $1817
 *      ("Just as I thought, you cannot harm me ... On your way"), step 7
 *   4  WinID_YesNo2
 *   5  NO: $1818 ("I will show no mercy. Die!"), step 6. YES: $1817 on
 *      its own -- the second half of her refusal, from "destruction." --
 *      step 7
 *   6  the event's own flag, mother_brain_fought ($C747), set; her battle
 *      ($103, ScreenID_Battle) and event_flags $F00: the aftermath scene
 *      on the level after it (loc_E1B6)
 *   7  done: the windows close on the next visit, her image goes (2(a0)
 *      of $EC00 = 1: deleted after its next routine) and the glow is off
 *      -- the approach will start it all again (loc_E1D2) */
static void level_mother_brain_confrontation(Game *g, u16 step, u8 *flag)
{
    (void)g;
    if (step == 0) {
        return;
    }
    if (step == 1) {
        Npc *image = npc_slot(&g_npcs, NPC_MOTHER_BRAIN_IMAGE_SLOT);
        if (image == NULL || image->routine != NPC_MOTHER_BRAIN_IMAGE_FORMED) {
            return;
        }
        g_mother_brain_glow = true;
        g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
        win_script_set(&g_win, SCRIPT_EVENT_SO_MOTHER_BRAIN_CONTROLLING);
        g_win.event_step++;
        return;
    }
    if (step == 2 || step == 4) {
        win_open(&g_win, WIN_YESNO2);
        g_win.event_step++;
        return;
    }
    if (step == 3) {
        if (g_win.yes_no_input != 0) {
            win_script_set(&g_win, SCRIPT_EVENT_MOTHER_BRAIN_LAUGHED_LOUDLY);
            g_win.event_step = (u16)(g_win.event_step + 4);
            return;
        }
        win_script_set(&g_win, SCRIPT_EVENT_MOTHER_BRAIN_BEGINS_GRIN);
        g_win.event_step++;
        return;
    }
    if (step == 5) {
        if (g_win.yes_no_input != 0) {
            win_script_set(&g_win, SCRIPT_EVENT_WILL_SHOW_NO_MERCY);
            g_win.event_step++;
            return;
        }
        win_script_set(&g_win, SCRIPT_EVENT_DESTRUCTION_WAY_THEN_BECAUSE);
        g_win.event_step = (u16)(g_win.event_step + 2);
        return;
    }
    if (step == 6) {
        *flag = 1;                      /* FLAG_MOTHER_BRAIN_FOUGHT */
        g_event_battle  = FORMATION_MOTHER_BRAIN;
        g_level_oneshot = SCENE_MOTHER_BRAIN_DEFEATED;
        return;
    }
    g_win.event_done++;
    Npc *image = npc_slot(&g_npcs, NPC_MOTHER_BRAIN_IMAGE_SLOT);
    if (image != NULL) {
        image->doomed = true;
    }
    g_mother_brain_glow = false;
}

/* The aftermath (level event $38, loc_E1E4, ps2.asm:20682), started with
 * no button on the level after Mother Brain's battle. One step an idle
 * visit -- and the visits go on during the scripted walk, which is what
 * step 3's demo-flag test is for:
 *
 *   1  the big box opens; every party member's HP is set to their
 *      maximum, the ones found at 0 counted. If any, Lutz's revival
 *      message; if none, script_id is not written, and since every
 *      loaded script has been shifted out of it by then, the box opens
 *      EMPTY and step 2 fills it -- ROM behaviour, kept
 *   2  "stood in amazement" (which runs on into "... a sense of hope")
 *      and then "Let's get back to Mota", queued
 *   3  the box closed and the walk after Mother Brain started from its
 *      first byte: three steps back down the corridor to its stop byte,
 *      which counts this event on (loc_37EC). No step increment here:
 *      while the walk runs the visits come back to this step and find
 *      the demo flag set
 *   4  "{char} wait! ... There is still somebody in the ship!"
 *   5  CloseAllWindows (event step and done back to 0), the same walk
 *      resumed one byte past its stop -- eleven steps north -- and the
 *      event becomes Lutz's warning, at step 0 */
static void level_mother_brain_aftermath(Game *g, u16 step)
{
    (void)g;
    switch (step) {
        case 0: {
            break;
        }
        case 1: {
            g_win.character_index = 0;
            g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
            int dead = 0;
            for (int i = 0; i <= g_data.party_members_num && i < GD_PARTY_MAX;
                 i++) {
                CharData *ch = &g_data.chr[g_data.party_member_id[i] & 7];
                if (ch->hp == 0) {
                    dead++;
                }
                ch->hp = ch->max_hp;
            }
            if (dead != 0) {
                win_script_set(&g_win, SCRIPT_LUTZ_REVIVES_THEM);
            }
            g_win.event_step++;
            break;
        }
        case 2: {
            win_script_set2(&g_win, SCRIPT_STOOD_IN_AMAZEMENT,
                            SCRIPT_BACK_TO_MOTA);
            g_win.event_step++;
            break;
        }
        case 3: {
            if (g_walk.demo == NULL && !g_bstate.demo_flag) {
                win_open(&g_win, WIN_CLOSE(1));
                walk_start_demo(&g_walk, DEMO_AFTER_MOTHER_BRAIN);
            }
            break;
        }
        case 4: {
            g_win.character_index = 0;
            g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
            win_script_set(&g_win, SCRIPT_LUTZ_WAIT);
            g_win.event_step++;
            break;
        }
        default: {
            win_close_all(&g_win);
            walk_resume_demo(&g_walk, DEMO_AFTER_MOTHER_BRAIN);
            g_level_event = EVENT_LUTZ_WARNING;
            break;
        }
    }
}

/* Lutz's warning (level event $39, loc_E29E, ps2.asm:20744). Step 0 is
 * the walk north; its stop byte makes it step 1: the music "Exclaim", the
 * big box and "Who are these people?". The visit after that closes every
 * window, starts the walk to the Earthmen from its first byte and makes
 * the event the Earthmen, at step 0 again. */
static void level_lutz_warning(Game *g, u16 step)
{
    if (step == 0) {
        return;
    }
    if (step == 1) {
        queue_sound(g, MUSIC_EXCLAIM);
        g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
        win_script_set(&g_win, SCRIPT_WHO_ARE_THESE_PEOPLE);
        g_win.event_step++;
        return;
    }
    win_close_all(&g_win);
    walk_start_demo(&g_walk, DEMO_TO_THE_EARTHMEN);
    g_level_event = EVENT_EARTHMEN;
}

/* loc_11868 (ps2.asm:26697), once for each slot of the line from the
 * leader's: the next id on the list, passing over anyone at 0 HP -- the
 * living close up behind a dead member, who is not drawn -- and once the
 * list runs out, the slots left are hidden (2(a0) = 2). Fills member[]
 * and returns how many slots draw someone. (build 182) */
static int line_pack(const int *ids, int n, int slots, int *member)
{
    int drawn = 0;
    for (int i = 0; i < n && drawn < slots; i++) {
        int id = ids[i] & 7;
        if (g_data.chr[id].hp != 0) {
            member[drawn++] = id;
        }
    }
    return drawn;
}

/* The six who stand on the bridge for the Earthmen's last step
 * (loc_E398, ps2.asm:20802): the ids 2-7 not among the party's first
 * four slots are appended after the party, then six following-character
 * objects are written over object slots 1-6 -- the party's own followers
 * among them -- standing (their third routine is a plain rts), facing up,
 * at the six positions. loc_1183A then packs the list into slots 0-6, the
 * leader's first, as the walking line does (line_pack). From then on
 * RenderCharSprites packs only the list's first four ids into slots 0-3,
 * every field, so each dead one among them hides one of those slots,
 * from the last.
 * Past the list's end, with someone dead, the ROM's last slot would read
 * the word after it in party_member_id's twelve; nobody here. The
 * aftermath (level_mother_brain_aftermath) has just brought the party to
 * full HP, so only those left at home can be dead here. */
static void ending_place_cast(void)
{
    int ids[GD_CHARS];
    int n = 0;
    for (int i = 0; i <= g_data.party_members_num && i < GD_PARTY_MAX; i++) {
        ids[n++] = g_data.party_member_id[i] & 7;
    }
    for (int id = 2; id < GD_CHARS; id++) {
        bool in_party = false;
        for (int i = 0; i < GD_PARTY_MAX; i++) {
            if ((g_data.party_member_id[i] & 7) == id) {
                in_party = true;
            }
        }
        if (!in_party && n < GD_CHARS) {
            ids[n++] = id;
        }
    }
    static const s16 POS[ENDING_CAST][2] = {
        /* x, y: NoahEnding_TeleportedCharPos is y, x pairs */
        { 0x218, 0x0A8 }, { 0x218, 0x0B8 }, { 0x218, 0x0C8 },
        { 0x208, 0x098 }, { 0x208, 0x0A8 }, { 0x208, 0x0B8 },
    };
    int line[1 + ENDING_CAST];
    int drawn = line_pack(ids, n, 1 + ENDING_CAST, line);
    int first4[4];
    int kept = line_pack(ids, (n < 4) ? n : 4, 4, first4);
    g_ending_leader = (drawn > 0) ? line[0] : -1;
    for (int slot = 1; slot <= ENDING_CAST; slot++) {
        EndingCastMember *m = &g_ending_cast[slot - 1];
        bool shown = slot < drawn && (slot >= 4 || slot < kept);
        m->member = shown ? line[slot] : -1;
        m->x      = POS[slot - 1][0];
        m->y      = POS[slot - 1][1];
    }
    g_ending_cast_placed = true;
    /* party_members_num is 7 for the sprite build and then forced to 3,
     * whatever the party was (loc_E432). */
    g_data.party_members_num = 3;
}

/* The Earthmen (level event $3A, loc_E2E2, ps2.asm:20765). Step 0 is the
 * walk to them; its stop byte makes it step 1:
 *
 *   1  "Welcome to the spaceship Noah ... Are you here because you think
 *      we are enemies?"
 *   2  YES/NO
 *   3  YES "Of course we hate you ...", NO "Thanks to the Mother Brain
 *      which we created, Algo prospered."; then, queued, "So you are the
 *      creators ..." and "We are not people of Algo ... Do you want to
 *      know why?"
 *   4  YES/NO
 *   5  YES: "We were a weaker people then ..." (running on into "... You
 *      will die!"), straight to step 8. NO: "Since you are going to die
 *      anyway, you might as well know.", step 6
 *   6  YES/NO
 *   7  YES: "In place of the home we have lost ..."; NO: "If you want to
 *      know, we will tell you." and then the same story as YES at step 5
 *   8  "Thanks to Lutz's power, they were sent on their way ... attacked
 *      by several hundred earthmen!", and the six on the bridge
 *   9  done; the ending screen */
static void level_earthmen(Game *g, u16 step)
{
    (void)g;
    switch (step) {
        case 0: {
            break;
        }
        case 1: {
            g_win.character_index = 0;
            g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
            win_script_set(&g_win, SCRIPT_WELCOME_TO_NOAH);
            g_win.event_step++;
            break;
        }
        case 2: {
            win_open(&g_win, WIN_YESNO2);
            g_win.event_step++;
            break;
        }
        case 3: {
            g_win.script_id[0] = (g_win.yes_no_input != 0)
                               ? SCRIPT_ALGO_PROSPERED : SCRIPT_WE_HATE_YOU;
            g_win.script_id[1] = SCRIPT_HARDLY_EXPECTED;
            g_win.script_id[2] = SCRIPT_WE_ARE_FROM_EARTH;
            g_win.event_step++;
            break;
        }
        case 4: {
            win_open(&g_win, WIN_YESNO2);
            g_win.event_step++;
            break;
        }
        case 5: {
            if (g_win.yes_no_input != 0) {
                win_script_set(&g_win, SCRIPT_DIE_ANYWAY);
                g_win.event_step++;
                break;
            }
            win_script_set(&g_win, SCRIPT_WE_WERE_WEAKER);
            g_win.event_step = (u16)(g_win.event_step + 3);
            break;
        }
        case 6: {
            win_open(&g_win, WIN_YESNO2);
            g_win.event_step++;
            break;
        }
        case 7: {
            if (g_win.yes_no_input != 0) {
                win_script_set2(&g_win, SCRIPT_WE_WILL_TELL_YOU,
                                SCRIPT_WE_WERE_WEAKER);
            } else {
                win_script_set(&g_win, SCRIPT_SETTLE_FOR_THIS_STAR);
            }
            g_win.event_step++;
            break;
        }
        case 8: {
            win_script_set(&g_win, SCRIPT_SENT_ON_THEIR_WAY);
            g_win.event_step++;
            ending_place_cast();
            break;
        }
        default: {
            g_win.event_done++;
            g_ending_next = true;
            break;
        }
    }
}

static void level_objtype_events(Game *g, u16 d2)
{
    if (g_win.event_done != 0) {
        win_close_all(&g_win);
        g_level_event = 0;
        return;
    }
    int event = g_level_event & 0xFF;
    u8 *flag  = &g_data.c700[(FLAG_EVENT_SLOTS + event) & 0xFF];
    u16 door_line = level_door_line(event);
    if (door_line != 0) {
        win_script_set(&g_win, door_line);
        g_win.event_done++;
        return;
    }
    switch (event) {
        case EVENT_NOTHING: {
            /* loc_D9AA (ps2.asm:19941), A on nothing. Riding on Motavia:
             * the leader's copy moves by JetScooter_CharOffPosOffsets
             * for the way the scooter faces, and the walking table
             * (loc_29B64) is asked about the scooter plus that offset.
             * Open: $171D, "Ok. Let's get off and walk from here.", and
             * $E1, which stops the engine. Solid: $171E, "We can't get
             * off here!", and the box closes. The second visit, only
             * reached from the first, is loc_DA26: the scooter parked at
             * its own cell ($C654/$C656), the flag cleared, the party
             * back to its init, the scooter to its own ($E822 = 0). */
            if (d2 != 0) {
                s16 park_x = 0, park_y = 0;
                walk_scooter_dismount(&g_walk, &park_x, &park_y);
                gamedata_set_word(&g_data, GD_SCOOTER_PARKED_Y, (u16)park_y);
                gamedata_set_word(&g_data, GD_SCOOTER_PARKED_X, (u16)park_x);
                g_data.jet_scooter = 0;
                Npc *sc = npc_jet_scooter(&g_npcs);
                if (sc != NULL) {
                    sc->routine = 0;
                }
                g_win.event_done++;
                break;
            }
            if (!level_riding()) {
                win_script_set(&g_win, SCRIPT_ACTION_THERE_SEEMS_NOTHING_UNUSUAL);     /* loc_D9BA */
                g_win.event_done++;
                break;
            }
            if (walk_scooter_can_dismount(&g_walk)) {
                win_script_set(&g_win, SCRIPT_ACTION_OK_LETS_GET_OFF);
                queue_sfx(g, SFX_SCOOTER_OFF);
            } else {
                win_script_set(&g_win, SCRIPT_ACTION_CANT_GET_OFF_HERE);     /* loc_DA10 */
                g_win.event_done++;
            }
            break;
        }
        case EVENT_JET_SCOOTER_PARKED: {
            /* loc_DAA4 (ps2.asm:20038), A on the parked scooter, a0 =
             * $C716. The first time ($C716 = 1) it goes to 2 and the note
             * the Mota people left comes first: $1686, then $171C, "Let's
             * get on the jet scooter!". On the next idle visit the
             * scooter takes the leader's facing, jet_scooter_flag = 1,
             * and the box closes. The scooter's init sees the flag on
             * the next frame; the party hides on the same one. */
            if (d2 == 0) {
                if (*flag == 1) {
                    *flag = 2;
                    win_script_set2(&g_win, SCRIPT_PEOPLE_HMM_WONDER_WHERE_THOSE, SCRIPT_ACTION_LETS_GET_JET_SCOOTER);
                } else {
                    win_script_set(&g_win, SCRIPT_ACTION_LETS_GET_JET_SCOOTER);
                }
                break;
            }
            Npc *sc = npc_jet_scooter(&g_npcs);
            if (sc != NULL) {
                sc->facing = g_walk.facing;         /* $E82A = $E42A */
            }
            g_data.jet_scooter = 1;
            g_win.event_done++;
            break;
        }
        case EVENT_ARIMA_BLOWN_DOOR: {
            /* loc_DA74 (ps2.asm:20022): Arima's blown doors, "Those
             * scoundrels seem to have blown open the door with dynamite
             * and stolen the things." */
            win_script_set(&g_win, SCRIPT_ACTION_THOSE_SCOUNDRELS_SEEM_BLOWN);
            g_win.event_done++;
            break;
        }
        case EVENT_CONTROL_TOWER: {
            /* loc_DA80 (ps2.asm:20026): the towns' control towers, "This
             * is the control tower which links the town and Mother Brain
             * in one network." */
            win_script_set(&g_win, SCRIPT_ACTION_CONTROL_TOWER_WHICH_LINKS);
            g_win.event_done++;
            break;
        }
        case EVENT_FAKE_MARUERA_TREE: {
            /* loc_DA98: "It looks like a Maruera-tree, but it isn't." */
            win_script_set(&g_win, SCRIPT_ACTION_LOOKS_LIKE_MARUERA_TREE);
            g_win.event_done++;
            break;
        }
        case EVENT_SKURE_NEWSPAPER_1: {
            /* loc_DB22 on (ps2.asm:20085): Skure's notices, "I found an
             * old newspaper!" ($1719) and then one of them. */
            win_script_set2(&g_win, SCRIPT_ACTION_FOUND_OLD_NEWS_PAPER, SCRIPT_ACTION_WHEN_YOURE_LONELY_BEST);
            g_win.event_done++;
            break;
        }
        case EVENT_SKURE_NEWSPAPER_2: {
            win_script_set2(&g_win, SCRIPT_ACTION_FOUND_OLD_NEWS_PAPER, SCRIPT_ACTION_WHY_DONT_TALK_PET);
            g_win.event_done++;
            break;
        }
        case EVENT_SKURE_NEWSPAPER_3: {
            win_script_set2(&g_win, SCRIPT_ACTION_FOUND_OLD_NEWS_PAPER, SCRIPT_ACTION_RECENTLY_THERES_BEEN_MANY);
            g_win.event_done++;
            break;
        }
        case EVENT_SKURE_NEWSPAPER_4: {
            win_script_set2(&g_win, SCRIPT_ACTION_FOUND_OLD_NEWS_PAPER, SCRIPT_ACTION_GAS_LEAK_BEEN_REPORTED);
            g_win.event_done++;
            break;
        }
        case EVENT_SKURE_NEWSPAPER_5: {
            win_script_set2(&g_win, SCRIPT_ACTION_FOUND_OLD_NEWS_PAPER, SCRIPT_ACTION_CAUSE_GAS_LEAK_TURNED);
            g_win.event_done++;
            break;
        }
        case EVENT_SKURE_NEWSPAPER_6: {
            win_script_set2(&g_win, SCRIPT_ACTION_FOUND_OLD_NEWS_PAPER, SCRIPT_ACTION_GAS_LEAKED_OUT_COULD);
            g_win.event_done++;
            break;
        }
        case EVENT_SKURE_NEWSPAPER_7: {
            win_script_set2(&g_win, SCRIPT_ACTION_FOUND_OLD_NEWS_PAPER, SCRIPT_ACTION_LAST_RESCUE_SHIP_LEAVES);
            g_win.event_done++;
            break;
        }
        case EVENT_WATER_COLOUR: {
            /* loc_DB84: "The color of water around here certainly seems
             * different ..." */
            win_script_set(&g_win, SCRIPT_ACTION_COLOR_WATER_AROUND_HERE);
            g_win.event_done++;
            break;
        }
        case EVENT_DEAD_BODY_BARE: {
            /* loc_DA8C (ps2.asm:20030): the two bodies with nothing on
             * them. */
            win_script_set(&g_win, SCRIPT_ACTION_THOSE_BODIES_SCOUNDRELS_WONDER);
            g_win.event_done++;
            break;
        }
        case EVENT_KEYBOARD: {
            level_keyboard(g, d2);
            break;
        }
        case EVENT_MARUERA_TREE: {
            level_maruera_tree(g, d2, flag);
            break;
        }
        case EVENT_DEAD_BODY_POCKET: {
            level_dead_body(g, d2, flag);
            break;
        }
        case EVENT_RECORDER: {
            level_recorder(g, flag);
            break;
        }
        case EVENT_TOWER_GREEN_CARD: {
            level_card(g, event, flag);
            break;
        }
        case EVENT_TOWER_BLUE_CARD: {
            level_card(g, event, flag);
            break;
        }
        case EVENT_TOWER_YELLOW_CARD: {
            level_card(g, event, flag);
            break;
        }
        case EVENT_TOWER_RED_CARD: {
            level_card(g, event, flag);
            break;
        }
        case EVENT_TEIM: {
            level_teim(g, d2, flag);
            break;
        }
        case EVENT_MOTHER_BRAIN_CONFRONTATION: {
            level_mother_brain_confrontation(g, d2, flag);
            break;
        }
        case EVENT_MOTHER_BRAIN_AFTERMATH: {
            level_mother_brain_aftermath(g, d2);
            break;
        }
        case EVENT_LUTZ_WARNING: {
            level_lutz_warning(g, d2);
            break;
        }
        case EVENT_EARTHMEN: {
            level_earthmen(g, d2);
            break;
        }
        case EVENT_NEIFIRST: {
            level_neifirst(g, d2);
            break;
        }
        case EVENT_NEI_DEATH: {
            level_nei_death(g, d2);
            break;
        }
        case EVENT_NEI_GONE: {
            level_nei_gone(g, d2);
            break;
        }
        case EVENT_CLIMATROL_SHAKES: {
            /* loc_DE1E: nothing on a visit with d2 at 0; "The weather
             * control system is shaking terrifically ..." ($1825) in the
             * big box; then the level again. */
            if (d2 == 0) {
                break;
            }
            if (d2 == 1) {
                g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
                win_script_set(&g_win, SCRIPT_EVENT_WEATHER_CONTROL_SYSTEM_SHAKING);
                g_win.event_step++;
                break;
            }
            level_scene_reload();
            break;
        }
        case EVENT_CLIMATROL_EXPLOSIONS: {
            /* loc_DE42: the sixteen explosions with timers of 64-127
             * (loc_DE52 adds $40 for this event), $CD22 = $12C, $DE70 + 1;
             * the visit after is $DE72 + 1, and the one after that
             * CloseAllWindows with nothing open. */
            if (d2 == 0) {
                break;
            }
            if (d2 == 1) {
                npc_spawn_explosions(&g_npcs, &g->rng, true);
                g_win.end_timer = 0x12C;
                g_win.event_step++;
                break;
            }
            g_win.event_done++;
            break;
        }
        case EVENT_RETURN_TO_PASEO: {
            level_return_to_paseo(g, d2);
            break;
        }
        case EVENT_GAIRA_PRISON_2: {
            level_prison(g, d2);
            break;
        }
        case EVENT_GAIRA_PRISON: {
            level_prison(g, d2);
            break;
        }
        case EVENT_GAIRA_EXPLOSION: {
            level_prison_blast(g, d2, flag);
            break;
        }
        case EVENT_GAIRA_CONTROL_PANEL: {
            level_gaira_panel(flag);
            break;
        }
        case EVENT_SHIR_GONE: {
            /* loc_E042 (ps2.asm:20545): WinID_ScriptMessage, $1722 \"What?
             * Shir is gone?\", $DE72 + 1, event_flags = 0. The next idle
             * visit is CloseAllWindows. */
            win_open(&g_win, WIN_SCRIPT_MESSAGE);
            win_script(&g_win, SCRIPT_ACTION_WHAT_SHIR_GONE);
            g_win.event_done++;
            break;
        }
        case EVENT_ESPER_MANSION_LUTZ: {
            /* loc_E0AA (ps2.asm:20579), F1's $33 once $C743 is 3: no test
             * of d2 -- BuildingID_EsperMansion, portrait $39,
             * ScreenID_Building, then loc_E0D8: level_y/x_pos from the
             * leader's cell and screen_changed_flag, so the level comes
             * back where he stood. */
            g_bstate.building = BUILDING_ESPER_MANSION;
            g_bstate.portrait = 0x39;
            g_data.level_x    = (u16)(g_walk.x & ~0xF);
            g_data.level_y    = (u16)(g_walk.y & ~0xF);
            g_event_building  = true;
            break;
        }
        case EVENT_PANDORAS_BOX: {
            /* loc_E0BE (ps2.asm:20584), Noah's box: A on it is "What is
             * this? I feel the tremendous strength of dark power!" and
             * $DE70 + 1. The next visit marks it ($C745 = 1, which is
             * what keeps LoadSpritesInLevel from placing it again), sets
             * event_flags to $B00, puts level_x/y_pos on the leader's cell
             * and raises screen_changed_flag: LevelScreen runs again,
             * fade and all, and loc_7E02 case $B takes it from there. The
             * message window is not closed first. */
            if (d2 == 0) {
                win_script_set(&g_win, SCRIPT_ACTION_WHAT_FEEL_TREMENDOUS_STRENGTH);
                g_win.event_step++;
                break;
            }
            *flag = 1;
            g_level_oneshot = SCENE_PANDORAS_BOX_OPENED;
            g_data.level_x  = (u16)(g_walk.x & ~0xF);
            g_data.level_y  = (u16)(g_walk.y & ~0xF);
            g_event_reload  = true;
            break;
        }
        case EVENT_PANDORAS_BOX_OPENED: {
            /* loc_E0F8, which only loc_7E02 case $B starts, with $DE70 at
             * 1: the big box and "Ha,ha,ha,ha,ha! This is Pandora's box!
             * ..." ($1910, a timed message), then $DE70 + 1; on the next
             * idle visit enemy_data_buffer = $102 and ScreenID_Battle:
             * Dark Force. A visit with d2 at 0 does nothing. */
            if (d2 == 0) {
                break;
            }
            if (d2 == 1) {
                g_win.window_index[0] = WIN_SCRIPT_MESSAGE_BIG;
                win_script_set(&g_win, SCRIPT_MISC_HA_HA_HA_HA);
                g_win.event_step++;
                break;
            }
            g_event_battle = 0x102;
            break;
        }
        case EVENT_MOUNT_UZO: {
            /* loc_DF24 (ps2.asm:20444): the rocky mountain on Motavia, an
             * event block at 2768,1544 (cell 173,96). In the small box the
             * A button opened, $171F; $DE72 + 1, so the next button closes
             * the box; and loc_DDA2 names {char} -- Rolf while he stands,
             * else the first member with HP. One visit, no flag. The
             * script's first page ends in $C4, which ends a script
             * (LoadScript, loc_11268), so the ROM shows only "There is a
             * high rocky mountain in front of"; "{char} and the others!
             * Is this Uzo mountain?" is never loaded (the disassembly:
             * "this should be $C3"). The port's text does the same. */
            win_script_set(&g_win, SCRIPT_ACTION_THERE_HIGH_ROCKY_MOUNTAIN);
            g_win.event_done++;
            level_name_leader();
            break;
        }
        default: {
            fprintf(stderr, "level: event $%02X is not ported\n", event);
            g_win.event_done++;
            break;
        }
    }
}

/* ObjType_People (ps2.asm:20500) for the two Roron people whose
 * dialogue number does more than talk. $DE6E is $1xx here: the high byte
 * is the people interaction, the low the dialogue number.
 *
 * $85, loc_E5FE, the cake (RoronF4), one step per idle visit with d2 =
 * $DE70:
 *   0  \"Hey! I found some tasty cake! Won't you try some?\" ($1681)
 *   1  WinID_YesNo2, $DE70 + 1
 *   2  NO: $1683, \"... respectfully declined the offer.\", and done.
 *      YES: $1682, \"... ate the cake. It tasted funny! ...\", $DE70 + 1
 *   3  WinID_MenuCharStats, $DE5E = $1E, $DE70 + 1
 *   4  $DE5E down to -1, then ten HP off every member (not below 0: a
 *      member under 10 dies of it), the stats redrawn in place
 *      ((6<<8)|MenuCharStats), $DE70 + 1
 *   5  WaitJoypad_B_C_Pressed, then done ($DE72 = 1)
 * character_index is zeroed on every visit, so {char} is ROLF's name
 * whoever leads. Done is loc_E53A: the first sixteen slots unfrozen and
 * CloseAllWindows. */
static void people_cake(Game *g, u16 d2)
{
    switch (d2) {
        case 0: {
            win_script_set(&g_win, SCRIPT_PEOPLE_HEY_FOUND_SOME_TASTY);
            break;
        }
        case 1: {
            win_open(&g_win, WIN_YESNO2);
            g_win.event_step++;
            break;
        }
        case 2: {
            if (g_win.yes_no_input != 0) {
                win_script_set(&g_win, SCRIPT_PEOPLE_OTHERS_RESPECTFULLY_DECLINED_OFFER);
                g_win.event_done = 1;
                break;
            }
            win_script_set(&g_win, SCRIPT_PEOPLE_OTHERS_ATE_CAKE_TASTED);
            g_win.event_step++;
            break;
        }
        case 3: {
            win_open(&g_win, WIN_MENU_CHAR_STATS);
            g_people_timer = 0x1E;
            g_win.event_step++;
            break;
        }
        case 4: {
            g_people_timer--;
            if (g_people_timer >= 0) {
                break;
            }
            for (int i = 0; i <= g_data.party_members_num && i < GD_PARTY_MAX;
                 i++) {
                CharData *ch = &g_data.chr[g_data.party_member_id[i] & 7];
                ch->hp = (ch->hp >= 10) ? (u16)(ch->hp - 10) : 0;
            }
            win_open(&g_win, WIN_F_INSTANT | WIN_F_REDRAW
                             | WIN_MENU_CHAR_STATS);
            g_win.event_step++;
            break;
        }
        default: {
            if (g->input.pressed.b || g->input.pressed.c) {
                g_win.event_done = 1;
            }
            break;
        }
    }
}

/* loc_E07A (ps2.asm:20561), dialogue $8F: the two Espers on B1 by the
 * sleeper. d2 0: $1697 \"Welcome. Our master has been waiting for you.\",
 * or $1698 once $C743 is set; $DE70 + 1 and no $DE72, so the next idle
 * visit comes back here with d2 = 2: event_flags = $D00 if $C743 was
 * already set, $C743 + 1, CloseAllWindows -- without loc_E53A, so the
 * Esper stays turned and frozen. The second word therefore makes $C743 2,
 * which starts B1's objects ($23-$27), and holds the party still. */
static void people_esper(Game *g, u16 d2)
{
    (void)g;
    u8 *talks = &g_data.c700[FLAG_ESPER_B1_TALKS];
    if (d2 == 0) {
        win_script_set(&g_win, (*talks != 0) ? SCRIPT_PEOPLE_PERSONALLY_WISH_MASTER_SLEEP : SCRIPT_PEOPLE_WELCOME_OUR_MASTER_BEEN);
        g_win.event_step++;
        return;
    }
    if (*talks != 0) {
        g_event_hold = SCENE_ESPER_B1_HOLD;
    }
    (*talks)++;
    win_close_all(&g_win);
    g_level_event = 0;
}

/* ObjType_People (ps2.asm:20946) for the dialogue numbers that run on:
 * $DE72 set is loc_E53A -- the first sixteen slots unfrozen and
 * CloseAllWindows -- and otherwise character_index = 0 and the number's
 * own routine. */
static void level_people_scene(Game *g, u16 d2)
{
    if (g_win.event_done != 0) {
        npc_unfreeze_all(&g_npcs);
        win_close_all(&g_win);
        g_level_event = 0;
        return;
    }
    g_win.character_index = 0;
    if ((g_level_event & 0xFF) == 0x8F) {
        people_esper(g, d2);
        return;
    }
    people_cake(g, d2);
}

/* ProcessAButtonPress's $FFFFDE70 path: whatever event $DE6E names, on
 * every frame the windows are idle. $3xx is ObjType_TeimDarum; $030 the
 * damage floor's fall, which no button started; any other type-0 number
 * an event object the A button found. */
static void level_event(Game *g)
{
    if ((g_level_event >> 8) == (EVENT_KIND_PEOPLE >> 8)) {
        if (!win_idle(&g_win) || g_win.event_routine != 0) {
            return;
        }
        level_people_scene(g, g_win.event_step);
        return;
    }
    if ((g_level_event >> 8) == (EVENT_KIND_TEIM_DARUM >> 8)) {
        if (!win_idle(&g_win) || g_win.event_routine != 0) {
            return;
        }
        level_teim_darum(g_win.event_step);
        return;
    }
    if (g_level_event == EVENT_MEMBER_DEAD) {
        level_floor_event(g);
        return;
    }
    if (!win_idle(&g_win) || g_win.event_routine != 0) {
        return;
    }
    level_objtype_events(g, g_win.event_step);
}

/* PS2_ATRACE=1: every A press on the level screen, on stderr -- whether
 * ProcessAButtonPress looked at it, what the hit test saw, and which way
 * it went. */
static bool a_trace(const Game *g)
{
    static int on = -1;
    if (on < 0) {
        on = (getenv("PS2_ATRACE") != NULL) ? 1 : 0;
    }
    return on != 0 && g->input.pressed.a;
}

/* ProcessAButtonPress (ps2.asm:19781). A, and only A, talks to whoever
 * the box ahead of the leader overlaps: the cell he faces when he stands
 * on one, and wherever he has got to when he is mid-step himself. The
 * person turns to face him and freezes (3(a1) = $FF) until the
 * conversation is over; a person mid-step cannot be talked to at all. An
 * empty cell gets $1708. */
static void level_talk(Game *g)
{
    bool trace = a_trace(g);
    if (trace) {
        fprintf(stderr, "A: tick %u, level $%02X, leader %d,%d facing %d\n",
                g->ticks, g_level.index, g_walk.x, g_walk.y, g_walk.facing);
    }

    if (!win_idle(&g_win) || g_win.event_routine != 0) {
        if (trace) {
            fprintf(stderr, "  ignored: windows busy (idle %d, "
                            "event_routine %u)\n", win_idle(&g_win),
                    g_win.event_routine);
        }
        return;
    }

    if (g_talk_state != 0) {
        /* ObjType_People, second visit: the script has played out. */
        if (g_talk_step != 0) {
            npc_unfreeze_all(&g_npcs);
            win_close_all(&g_win);
            g_talk_state = 0;
            g_talk_step  = 0;
        }
        return;
    }

    if (g_event_hold != 0) {
        /* ProcessAButtonPress: `tst.b (event_flags).w` -- no A. */
        return;
    }

    /* ProcessAButtonPress: `tst.w (demo_flag).w` -- no A while a demo
     * walk runs. Nothing there looks at the party's step: A answers
     * mid-step, on foot as on the scooter (build 133; until then the
     * port held an A on foot back to the end of the step). */
    if (g_walk.demo != NULL || !g->input.pressed.a) {
        if (trace) {
            fprintf(stderr, "  ignored: a demo walk is running\n");
        }
        return;
    }

    static const u8 FACE_BACK[4] = { FACE_DOWN, FACE_UP, FACE_RIGHT,
                                     FACE_LEFT };
    /* Level_ChkTargetInteract (ps2.asm:10503): loc_7278's box for the
     * leader's $2A / 3 about his $20/$1E, against each person's own box
     * about theirs. Those are screen positions, which RunObject's tail
     * (loc_3E4) writes after the frame's velocity, all of them this
     * frame against one camera -- loc_8BBA moves it inside ObjCL_Main,
     * before the leader's tail runs -- so the overlap is the one
     * npc_in_the_way finds from level positions: g_walk.x/y with this
     * frame's pixel in, mid-step or not. $2A stays the step's direction
     * until the step ends; the pad is not read in between. */
    int  dir    = g_walk.facing / 3;
    int  index  = 0;
    Npc *target = npc_in_the_way(&g_npcs, NULL, g_walk.x, g_walk.y, dir);
    /* ProcessAButAction's d6: the leader's $2A / 3, then the scan's. */
    npc_note_leader_check(&g_npcs, dir, true, target);

    if (trace) {
        npc_trace(&g_npcs, g_walk.x, g_walk.y, dir);
        if (target == NULL) {
            fprintf(stderr, "  -> nothing there: people line 0\n");
        } else {
            fprintf(stderr, "  -> slot %d, $2E %d/$%02X\n",
                    (int)(target - g_npcs.npc), target->interaction,
                    target->index & 0xFF);
        }
    }

    if (target != NULL) {
        if (target->vx != 0 || target->vy != 0) {
            if (trace) {
                fprintf(stderr, "  -> it is moving: the sound only\n");
            }
            queue_sfx(g, SFX_SELECTION);
            return;
        }
        if (target->interaction == NPC_INTERACT_PEOPLE) {
            target->frame  = FACE_BACK[dir];
            target->facing = FACE_BACK[dir];
            target->frozen = true;
            index = target->index;
        }
    }

    if (target != NULL && target->interaction == NPC_INTERACT_TEIM) {
        /* loc_D84A: WinID_ScriptMessage, $DE70 = 1, $DE72 = 0, $DE6E =
         * the target's $2E, then ObjType_TeimDarum with d2 = 0. It runs
         * on through level_event, not the people's two visits. */
        queue_sfx(g, SFX_SELECTION);
        win_open(&g_win, WIN_SCRIPT_MESSAGE);
        g_win.event_step    = 1;
        g_win.event_done    = 0;
        g_level_event = EVENT_KIND_TEIM_DARUM | target->index;
        level_teim_darum(0);
        return;
    }

    if (target == NULL && level_riding()) {
        /* loc_D84A with d4 = 0: ObjType_Events 0, the scooter's way off
         * (see level_objtype_events). */
        queue_sfx(g, SFX_SELECTION);
        win_open(&g_win, WIN_SCRIPT_MESSAGE);
        g_win.event_step    = 1;
        g_win.event_done    = 0;
        g_level_event = LEVEL_EVENT_ZERO;
        level_objtype_events(g, 0);
        return;
    }

    if (target != NULL && target->interaction == NPC_INTERACT_EVENT
        && level_event_ported(target->index)) {
        /* loc_D84A the same way, into ObjType_Events: $DE6E is the
         * target's $2E, whose high byte is 0 here. */
        queue_sfx(g, SFX_SELECTION);
        win_open(&g_win, WIN_SCRIPT_MESSAGE);
        g_win.event_step    = 1;
        g_win.event_done    = 0;
        g_level_event = target->index;
        level_objtype_events(g, 0);
        return;
    }

    if (target != NULL && target->interaction == NPC_INTERACT_PEOPLE
        && (index == 0x85 || index == 0x8F)) {
        /* loc_D84A as for an event: $DE70 = 1, and ObjType_People's
         * first visit with d2 = 0 (level_people_scene): Roron's cake
         * ($85) and B1's Espers ($8F). */
        queue_sfx(g, SFX_SELECTION);
        win_open(&g_win, WIN_SCRIPT_MESSAGE);
        g_win.event_step    = 1;
        g_win.event_done    = 0;
        g_level_event = EVENT_KIND_PEOPLE | index;
        level_people_scene(g, 0);
        return;
    }

    win_open(&g_win, WIN_SCRIPT_MESSAGE);
    g_talk_state = 1;
    g_talk_step  = 1;

    /* AButton_ObjTypeIndex, by the high byte of the target's $2E. */
    if (target != NULL && target->interaction == NPC_INTERACT_CHEST) {
        u8 sfx = level_open_chest(target->index);
        queue_sfx(g, (sfx != 0) ? sfx : SFX_SELECTION);
        return;
    }
    queue_sfx(g, SFX_SELECTION);
    if (target != NULL && target->interaction != NPC_INTERACT_PEOPLE
        && target->index != 0) {
        fprintf(stderr, "level: A on object type %d index $%02X is not "
                        "ported\n", target->interaction, target->index);
    }
    u16 script = npc_dialogue_script(index, g_data.c700[FLAG_NEI_GONE] != 0,
                                     rolf_cap());
    bool people = target != NULL
               && target->interaction == NPC_INTERACT_PEOPLE;
    if (people && index == 0x88) {
        /* loc_E05A (ps2.asm:20553), F1's two Espers at the door: $1688
         * \"You are not {char}, are you? Please leave.\" -- or, with Rolf
         * standing, $C742 = 1 and $1687 \"You must be {char}. We've been
         * expecting you.\" -- and $DE72 = 1. Both step aside once $C742 is
         * set (Obj_MovingEsper); the one spoken to when the talk ends. */
        g_win.character_index = 0;
        script = SCRIPT_PEOPLE_NOT_PLEASE_LEAVE;
        if (g_data.chr[0].hp != 0) {
            g_data.c700[FLAG_DOOR_ESPERS_LET_ROLF_IN] = 1;
            script = SCRIPT_PEOPLE_MUST_WEVE_BEEN_EXPECTING;
        }
    } else if (people && index >= 0x88 && index != 0x8F
               && g_data.c700[FLAG_ESPER_B1_TALKS] == 3) {
        /* loc_E5D0: dialogue $88 and up while $C743 is 3 is $16A1, \"The
         * future of Algo depends on you...\" -- F1's six Espers once
         * Lutz is awake. */
        script = SCRIPT_PEOPLE_FUTURE_ALGO_DEPENDS_IM;
    }
    win_script(&g_win, script);
    if (target != NULL && target->interaction == NPC_INTERACT_PEOPLE
        && index == 0x87) {
        /* loc_E690: $1685 (the table's own entry too), $C716 = 1 -- the
         * scooter given -- and parked at x $590, y $730 on Motavia
         * ($C656, $C654), where LoadSpritesInLevel's tail puts it. */
        g_data.c700[FLAG_JET_SCOOTER_OWNED] = 1;
        gamedata_set_word(&g_data, GD_SCOOTER_PARKED_Y, 0x730);
        gamedata_set_word(&g_data, GD_SCOOTER_PARKED_X, 0x590);
    }
}

/* loc_7108, on a damage floor after a step: every entry of the displayed
 * palette to $EEE for one field, SXFID_DangerousFloor, the palette put
 * back from $FB80, seven more fields, and a point of HP off everyone
 * still standing. The whole level loop waits inside ObjCL_Main for those
 * eight fields, so here they are ticks that run nothing (the RNG still
 * advances: it is called from VBlankExit). (build 157b) The white field
 * is the flash's own tick, nothing of the loop after it: the HP
 * (level_floor_hp) and the rest of the iteration wait for the stall's
 * last tick (tick_level_walk). Until 157b they ran in the flash's tick,
 * and loc_6430's cycle wrote its entries over the white on the palette
 * path. */
static void level_dangerous_floor(Game *g)
{
    video_flash_white(g->video);
    queue_sfx(g, SFX_DANGEROUS_FLOOR);   /* loc_7108 */
    g_floor_stall = 8;
}

/* loc_7162, after loc_7108's eight fields: a point of HP off everyone
 * still standing. If someone drops to 0 -- the last one in party order --
 * ObjType_Events $30 follows ($DE70 = 1, $DE6E = $30), which the same
 * iteration's ProcessAButtonPress runs. */
static void level_floor_hp(void)
{
    for (int i = 0; i <= g_data.party_members_num && i < GD_PARTY_MAX; i++) {
        int c = g_data.party_member_id[i] & 7;
        if (g_data.chr[c].hp == 0) {
            continue;
        }
        g_data.chr[c].hp--;
        if (g_data.chr[c].hp == 0) {
            g_win.character_index = (u16)c;
            g_win.event_step    = 1;
            g_level_event = EVENT_MEMBER_DEAD;
        }
    }
}

/* ObjType_Events $30 (loc_DFFE), reached through ProcessAButtonPress's
 * $FFFFDE70 path whenever the windows are idle: $1721 first, then --
 * once it has played out -- the Sega screen if nobody is left standing,
 * and CloseAllWindows either way on the visit after. */
static void level_floor_event(Game *g)
{
    if (!win_idle(&g_win) || g_win.event_routine != 0) {
        return;
    }
    if (g_win.event_done != 0) {
        win_close_all(&g_win);
        g_level_event = 0;
        return;
    }
    if (g_win.event_step == 1) {
        win_open(&g_win, WIN_SCRIPT_MESSAGE);
        win_script(&g_win, SCRIPT_ACTION_IS_DEAD);
        g_win.event_step = 2;
        return;
    }
    g_win.event_done++;
    for (int i = 0; i <= g_data.party_members_num && i < GD_PARTY_MAX; i++) {
        if (g_data.chr[g_data.party_member_id[i] & 7].hp != 0) {
            return;
        }
    }
    /* loc_E01A: move.b #ScreenID_Sega, (game_screen).w -- and the level
     * loop's loc_7DEC, with the controls unlocked, branches to
     * GameOverScreen ("branch if all characters are dead"); SegaScreen
     * follows it. (build 157c) Until 157c the port went straight to the
     * SEGA screen, with no game over; it also put g_level_walk out here,
     * so the fade drew the level through the attract crawl's camera. The
     * walk stays on under the game over's erase: the title's crawl and
     * every start set it again. */
    memset(&g_bstate, 0, sizeof g_bstate);
    g_level_event = 0;
    begin_gameover(g, SCREEN_LEVEL);
}

/* CheckGamePause (ps2.asm:8556), on the map and in buildings.
 *
 * One tick here is one VBlank. The ROM's loop is CheckGamePause, then
 * WaitForVBlank, then the frame's logic, and CheckGamePause looks at the
 * pad as read at the *previous* VBlank. So:
 *
 *  - The tick Start is pressed on runs normally; the pause starts on the
 *    next one, which is GamePausedLoop's first WaitForVBlank.
 *  - Each paused tick is a VBlank inside GamePausedLoop. B held or C
 *    pressed there returns -- but the main loop then waits one more
 *    VBlank before any logic, and that read recomputes joypad_pressed.
 *    The frame let through is the *next* tick, with that tick's own
 *    input: the C that released it is not in it, so the menu does not
 *    open, and B held lets a frame through every other VBlank, which is
 *    the half-speed slow motion.
 *  - Start in GamePausedLoop unpauses the same way: sound, paused_mode
 *    cleared, and the next tick runs.
 *
 * Returns whether this tick runs the screen's logic. */
static bool pause_gate(Game *g)
{
    if (g_pause_release) {
        g_pause_release = false;
        return true;
    }
    if (g_paused) {
        if (g->input.held.b || g->input.pressed.c) {
            g_pause_release = true;
        } else if (g->input.pressed.start) {
            queue_sfx(g, SFX_UNPAUSE);
            if (g->audio != NULL) {
                audio_music_resume(g->audio);
            }
            g_paused        = false;
            g_pause_release = true;
        }
        return false;
    }
    if (g->input.pressed.start) {
        /* This tick's logic still runs. LevelScreenLoop (ps2.asm:11443)
         * is CheckGamePause, WaitForVBlank, RunObjects: the $D5 goes to
         * queue slot 0 at the top of the next loop, after anything this
         * tick's logic queued, so it is queued at the end of the tick. */
        g_pause_pending = true;
    }
    return true;
}

/* loc_118F4 (VBlankSub_10, the level screen's VBlank): tile animations.
 * Every 32 fields a frame of raw art is copied over a run of VRAM tiles
 * -- the Motavian sea (level $00, two frames) and the Underwater Passage
 * (level $13, three frames played 0 1 2 1). assets/data/level_anims.json,
 * from extract_levels.py. $F63C and $F63E are zeroed on every LevelScreen
 * entry, so the first frame goes up on the first field. It runs in the
 * damage floor's eight-field wait (that is VBlank $10 too) but not while
 * paused (GamePausedLoop is VBlank $20). */

/* Optional: without the file the water simply stands still. */
static void tile_anims_load(void)
{
    g_tile_anim_count = 0;
    JsonValue *root = json_parse_file("assets/data/level_anims.json");
    if (root == NULL) {
        fprintf(stderr, "level: assets/data/level_anims.json is missing -- "
                        "rerun tools/extract_levels.py; the sea will not "
                        "move until then\n");
        return;
    }
    const JsonValue *list = json_get(root, "anims");
    for (const JsonValue *a = (list != NULL) ? list->child : NULL;
         a != NULL && g_tile_anim_count < TILE_ANIMS_MAX; a = a->next) {
        TileAnim *ta = &g_tile_anim[g_tile_anim_count];
        memset(ta, 0, sizeof *ta);
        ta->level  = (int)json_int(a, "level", -1);
        ta->tile   = (int)json_int(a, "tile", 0);
        ta->period = (int)json_int(a, "period", 0x20);

        bool ok = true;
        const JsonValue *frames = json_get(a, "frames");
        for (const JsonValue *f = (frames != NULL) ? frames->child : NULL;
             f != NULL && ta->frame_count < TILE_ANIM_FRAMES; f = f->next) {
            char path[160];
            snprintf(path, sizeof path, "assets/tiles/%s",
                     json_str(f, "file", ""));
            if (!tileset_load(&ta->frame[ta->frame_count++], path)) {
                fprintf(stderr, "level: tile animation frame %s will not "
                                "load\n", path);
                ok = false;
            }
        }
        const JsonValue *seq = json_get(a, "sequence");
        for (const JsonValue *s = (seq != NULL) ? seq->child : NULL;
             s != NULL && ta->seq_len < TILE_ANIM_SEQ; s = s->next) {
            ta->seq[ta->seq_len++] = (int)s->number;
        }
        if (ok && ta->frame_count > 0 && ta->seq_len > 0) {
            g_tile_anim_count++;
        }
    }
    json_free(root);
}

static void level_tile_anim(void)
{
    for (int i = 0; i < g_tile_anim_count; i++) {
        TileAnim *ta = &g_tile_anim[i];
        if (ta->level != g_level.index) {
            continue;
        }
        g_tile_anim_wait--;
        if (g_tile_anim_wait >= 0) {
            return;
        }
        g_tile_anim_wait = (s16)(ta->period - 1);
        int k = ta->seq[g_tile_anim_step % ta->seq_len];
        g_tile_anim_step++;
        if (k >= 0 && k < ta->frame_count) {
            tileset_place(&g_vram, &ta->frame[k], ta->tile);
        }
        return;
    }
}

/* loc_6430 (LevelScreenLoop, every frame): the level's palette
 * animation -- dungeon and tube lights, town lamps, Uzo, Gaira's
 * alarm, Noah's glow -- written into the displayed palette ($FB00).
 * $F632 counts steps and $F634 waits between them; nothing resets
 * them on entering a level. */
static u16 g_cycle_step;    /* $FFFFF632 */
static s16 g_cycle_wait;    /* $FFFFF634 */

static const u16 CYCLE_GLOW[4][3] = {           /* loc_6650 */
    { 0x0EEE, 0x0A88, 0x0422 }, { 0x0CAA, 0x0ECC, 0x0866 },
    { 0x0866, 0x0A88, 0x0CAA }, { 0x0CAA, 0x0644, 0x0866 },
};

/* loc_64E8: entries $33, $38 and $3A. */
static void cycle_glow(Video *v, u16 step)
{
    const u16 *gl = CYCLE_GLOW[step & 3];
    video_glow(v, 0x33, gl[0]);
    video_glow(v, 0x38, gl[1]);
    video_glow(v, 0x3A, gl[2]);
}

static void level_palette_cycle(Game *g)
{
    static const u16 PULSE[16][2] = {           /* loc_65D8 */
        { 0x044E, 0x0006 }, { 0x044E, 0x0006 }, { 0x022C, 0x004A },
        { 0x0008, 0x006C }, { 0x0004, 0x02AE }, { 0x0004, 0x02AE },
        { 0x0008, 0x006C }, { 0x022C, 0x004A }, { 0x022C, 0x066C },
        { 0x022C, 0x066C }, { 0x044E, 0x088E }, { 0x066E, 0x0AAE },
        { 0x088E, 0x0CCE }, { 0x088E, 0x0CCE }, { 0x066E, 0x0AAE },
        { 0x044E, 0x088E },
    };
    static const u16 RAMP[16] = {               /* loc_6618 */
        0x0640, 0x0640, 0x0640, 0x0640, 0x0A80, 0x0EC0, 0x0EE4, 0x0EE8,
        0x0EE8, 0x0EE8, 0x0EE8, 0x0EE8, 0x0EE4, 0x0EC0, 0x0A80, 0x0640,
    };
    static const u16 UZO[4][3] = {              /* loc_6638 */
        { 0x0ECA, 0x0EC2, 0x0CA0 }, { 0x0EC2, 0x0CA0, 0x0EC2 },
        { 0x0CA0, 0x0EC2, 0x0ECA }, { 0x0EC2, 0x0ECA, 0x0EC2 },
    };
    static const u8  ALARM_STEP[16] = {         /* loc_6668: byte offsets */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x12, 0x24,
        0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x24, 0x12,
    };
    static const u16 ALARM[4][9] = {            /* loc_6678 */
        { 0x0886, 0x0664, 0x0442, 0x0220, 0x0AAA, 0x0888, 0x0666, 0x0444,
          0x0222 },
        { 0x088A, 0x0668, 0x0446, 0x0224, 0x0AAC, 0x088A, 0x0668, 0x0446,
          0x0224 },
        { 0x066A, 0x0448, 0x0226, 0x0224, 0x0AAE, 0x066A, 0x0448, 0x0226,
          0x0224 },
        { 0x066E, 0x022E, 0x0008, 0x0004, 0x0AAE, 0x066E, 0x022E, 0x0008,
          0x0004 },
    };

    Video *v = g->video;
    int level = g_level.index;

    switch (g_level.tileset) {
        case 5: {
            if (--g_cycle_wait >= 0) {
                return;
            }
            g_cycle_wait = 7;
            u16 step = g_cycle_step++;
            video_glow(v, 0x2E, PULSE[step & 7][0]);
            video_glow(v, 0x2F, PULSE[step & 7][1]);
            if (level == 0x32) {                /* BiosystemsLab B1 */
                video_glow(v, 0x2C, PULSE[8 + (step & 7)][0]);
                video_glow(v, 0x2D, PULSE[8 + (step & 7)][1]);
            }
            video_glow(v, 0x3C, RAMP[step & 0xF]);
            video_glow(v, 0x3D, (step & 1) ? 0x0A80 : 0x0CA0);
            video_glow(v, 0x3E, (step & 1) ? 0x0CA0 : 0x0A80);
            if (level == 0x41) {                /* Gaira */
                if (g_data.c700[FLAG_GAIRA_OFF_ORBIT] != 0) {
                    int at = ALARM_STEP[step & 0xF];
                    if (at == 1) {
                        at = 0;
                        queue_sfx(g, SFX_ALARM);
                    }
                    for (int i = 0; i < 9; i++) {
                        video_glow(v, 0x22 + i, ALARM[at / 0x12][i]);
                    }
                }
                cycle_glow(v, step);
            }
            break;
        }
        case 2: {
            if (level >= 0x0A) {                /* Aukba and after */
                return;
            }
            if (--g_cycle_wait >= 0) {
                return;
            }
            g_cycle_wait = 7;
            u16 step = g_cycle_step++;
            video_glow(v, 0x3C, (step & 1) ? 0x0EC0 : 0x0EA0);
            video_glow(v, 0x3D, (step & 1) ? 0x0E60 : 0x0E80);
            break;
        }
        case 3: {
            if (level != 0x12) {                /* Uzo only */
                return;
            }
            if (--g_cycle_wait >= 0) {
                return;
            }
            g_cycle_wait = 0xF;
            u16 step = g_cycle_step++;
            for (int i = 0; i < 3; i++) {
                video_glow(v, 0x2B + i, UZO[step & 3][i]);
            }
            break;
        }
        case 6: {
            if (level < 0x63) {                 /* Noah on */
                return;
            }
            if (--g_cycle_wait >= 0) {
                return;
            }
            g_cycle_wait = 7;
            u16 step = g_cycle_step++;
            cycle_glow(v, step);
            /* Entry $2D: $442, or while Mother Brain's glow is on (the
             * confrontation sets it) the next of loc_66C0's sixteen --
             * a slow red pulse -- with its own step, $F650. */
            video_glow(v, 0x2D, 0x0442);
            if (g_mother_brain_glow) {
                static const u16 GLOW[16] = {   /* loc_66C0 */
                    0x0442, 0x0442, 0x0442, 0x0442, 0x0442, 0x0640,
                    0x0840, 0x0A40, 0x0C40, 0x0C40, 0x0C40, 0x0C40,
                    0x0C40, 0x0A40, 0x0840, 0x0640,
                };
                video_glow(v, 0x2D,
                           GLOW[(g_mother_brain_glow_step >> 1) & 0xF]);
                g_mother_brain_glow_step = (u16)(g_mother_brain_glow_step + 2);
            }
            break;
        }
        default: {
            break;
        }
    }
}

static void level_walk_rest(Game *g, WalkResult result, bool busy);

static void tick_level_walk(Game *g)
{
    if (!g_level_ready) {
        return;
    }
    if (g_floor_stall > 0) {
        level_tile_anim();
        /* Inside loc_7108: the white field is over, the rest wait. */
        if (g_floor_stall == 8) {
            /* (build 138d) loc_7140: the fade target -- the level as
             * loaded -- copied back, so the cycling entries show their
             * loaded colours until the next cycle step (until 138d the
             * palette as it was shown before the white field). */
            video_shown_from_target(g->video, &g->fade_target);
        }
        g_floor_stall--;
        if (g_floor_stall == 0 && g_floor_rest) {
            /* (build 157b) The eighth field: loc_7108's HP, then the rest
             * of the iteration the white field cut into. */
            g_floor_rest = false;
            level_floor_hp();
            level_walk_rest(g, g_floor_rest_result, g_floor_rest_busy);
        }
        return;
    }
    if (!pause_gate(g)) {
        return;
    }
    level_tile_anim();

    /* ObjCL_Main stands still while a window is open or on the stack. */
    bool busy = !win_idle(&g_win) || g_win.depth != 0 || g_talk_state != 0
             || g_event_hold != 0;

    win_run_cursors(&g_win, &g->input);
    g_walk.piata_tower_unlocked = (g_data.c700[FLAG_PIATA_TOWER_UNLOCKED] != 0);
    g_walk.mother_brain_fought = (g_data.c700[FLAG_MOTHER_BRAIN_FOUGHT] != 0);
    g_walk.rolf_hp   = g_data.chr[0].hp;
    g_walk.riding    = level_riding();
    bool at_rest = (walk_step_frames(&g_walk) == 0);
    WalkResult result = WALK_RUNNING;
    if (!g_party_gone) {             /* RunObjects: nothing in $E400 */
        result = walk_tick(&g_walk, &g->input, busy);
    }
    if (at_rest && walk_step_frames(&g_walk) != 0) {
        g_debug_steps++;            /* loc_38FC / loc_4A6E: a step begun */
    }
    if (g_walk.bump_a) {
        /* CharSprites_ChkMoveLeft's `move.w #$40, (joypad_held).w`: the
         * pad word for the rest of the frame is nothing held and A
         * pressed, so the menu sees no C and ProcessAButtonPress, below,
         * talks to Darum. The Escape key is the port's, not the pad's,
         * and is left alone. */
        Buttons was_held    = g->input.held;
        Buttons was_pressed = g->input.pressed;
        memset(&g->input.held, 0, sizeof g->input.held);
        memset(&g->input.pressed, 0, sizeof g->input.pressed);
        g->input.held.menu    = was_held.menu;
        g->input.pressed.menu = was_pressed.menu;
        g->input.pressed.a    = true;
    }
    if (g_walk.demo_stopped) {
        g_win.event_step++;               /* loc_37EC */
    }
    if (g_walk.demo_door) {
        /* loc_37FE: the door SFX, and routine 2 into $FFFFE822 -- the
         * level's first object, which on every level with a demo 4 or 5
         * is the door the item was used on. */
        queue_sfx(g, SFX_DOOR_OPEN);
        Npc *door = npc_slot(&g_npcs, 0);
        if (door != NULL) {
            door->routine = 2;
        }
    }

    /* ObjCL_Main (ps2.asm:5186): on Gaira the leader's mappings swap every
     * frame between loc_12B06 and loc_12B36, and each follower takes the
     * one ahead of it (ObjFlwngChar_Main's move.l -$3C(a0), 4(a0)), so the
     * whole party shows the ring every other frame. */
    g_gaira_ring = (g_data.level_index == LEVEL_GAIRA) ? !g_gaira_ring : false;

    if (g_walk.hurt_now) {
        /* loc_7108 from ObjCL_Main: the white field before anything else
         * of the iteration (build 157b); the rest after the stall. */
        level_dangerous_floor(g);
        g_floor_rest        = true;
        g_floor_rest_result = result;
        g_floor_rest_busy   = busy;
        return;
    }
    level_walk_rest(g, result, busy);
}

/* LevelScreenLoop after ObjCL_Main: the other objects, BuildSprites,
 * ProcessPlayerMenu, ProcessAButtonPress, ProcessRandomBattle, loc_6430
 * and the rest of the iteration, with the leader's step's result and
 * whether the windows held him. (build 157b) Its own function so a
 * damage floor's stall can run it at its end, as loc_7108 returns. */
static void level_walk_rest(Game *g, WalkResult result, bool busy)
{
    level_palette_cycle(g);

    /* LevelScreenLoop, after ProcessRandomBattle: a plane-B backdrop
     * drifts on its own -- down and left while $C73E is set before Noah,
     * up always from Noah on. Every frame, stepping or not. */
    if (g_level.flags & LEVEL_FLAG_B_BACKDROP) {
        if (g_level.index >= LEVEL_NOAH_GROUND_F) {
            g_plane_b_y--;
        } else if (g_data.c700[FLAG_GAIRA_OFF_ORBIT] != 0) {
            g_plane_b_y++;
            g_plane_b_x++;
        }
    }

    /* loc_809C, for the scene's camera cases; a reload it asks for
     * happens once the frame's objects have run (loc_7DCE). */
    level_scene_camera(g);

    s16 party_x[1 + WALK_FOLLOWERS], party_y[1 + WALK_FOLLOWERS];
    /* Riding, the party objects are hidden: bit 7 of 2(a0) stays clear,
     * so nobody can bump into them (loc_7224). */
    int party = (g_party_gone || g_walk.riding) ? 0
              : walk_party_positions(&g_walk, party_x, party_y,
                                     1 + WALK_FOLLOWERS);
    /* LevelScreenLoop: ProcessPlayerMenu runs after RunObjects and
     * before ProcessAButtonPress. */
    g_menu.demo_flag = g_bstate.demo_flag || g_walk.demo != NULL;
    /* The event word's high byte as the ROM holds it while the level
     * runs: after loc_7E02 (and LevelScreen's `bclr #7`) it is the case
     * the next load will take -- g_level_oneshot -- or $D, the hold. */
    g_menu.event_word_high = g_level_oneshot != SCENE_NONE
                          || g_event_hold != 0;
    g_menu.slot0_x   = g_npcs.npc[0].x;
    g_menu.slot0_y   = g_npcs.npc[0].y;
    menu_tick(&g_menu, &g_win, &g_data, &g->input);

    if (g_menu.sound_wait) {
        g_menu.sound_wait = false;
        start_sound_wait(g);
    }
    if (g_menu.hidapipe) {
        g_menu.hidapipe      = false;
        g_encounter.hidapipe = 0xFFFF;
    }
    if (g_menu.start_demo >= 0) {
        /* ItemAction_Dynamite / ItemAction_KeyTube / the cards: demo_flag
         * = 1 and demo_index set while the menu is still up. ObjCL_Main
         * reads nothing from it until window_index is 0 again, which is
         * what `busy` above does for walk_tick. */
        if (g_menu.start_demo < DEMO_SCRIPTS
            && DEMO_SCRIPT[g_menu.start_demo] != NULL) {
            walk_start_demo(&g_walk, DEMO_SCRIPT[g_menu.start_demo]);
        } else {
            fprintf(stderr, "level: demo walk %d is not ported\n",
                    g_menu.start_demo);
        }
        g_menu.start_demo = -1;
    }

    if (g_menu.event_flags >= 0) {
        /* `move.w #N, (event_flags).w` from an item: the high byte is the
         * next load's loc_7E02 case, the low loc_809C's camera case. */
        if ((g_menu.event_flags >> 8) != 0) {
            g_level_oneshot = (u8)(g_menu.event_flags >> 8);
        }
        g_scene_camera    = g_menu.event_flags & 0xFF;
        g_menu.event_flags = -1;
    }

    if (g_menu.level_change) {
        /* A pipe, Ryuka, Hinas or the Nei Sword: level_index and the
         * arrival cell are set, screen_changed_flag = -1, and the level
         * screen runs again from the top. */
        g_menu.level_change = false;
        goto_screen(g, SCREEN_LEVEL);
        return;
    }
    if (g_menu.to_data_memory) {
        /* Visiphone_ItemSelected: Data Memory as a building, with
         * $F764 = 0, $F766 = 1 and the map position kept. */
        g_menu.to_data_memory = false;
        memset(&g_bstate, 0, sizeof g_bstate);
        g_bstate.building    = BUILDING_DATA_MEMORY;
        g_bstate.portrait    = 9;
        g_bstate.store_max   = 0;
        g_bstate.store_index = 1;
        g_data.level_x = (u16)(g_walk.x & ~0xF);
        g_data.level_y = (u16)(g_walk.y & ~0xF);
        goto_screen(g, SCREEN_BUILDING);
        return;
    }

    /* ProcessRandomBattle: once the step is over and no window is up,
     * a roll; a hit is the flash into ScreenID_Battle. */
    if (battle_encounter_check(&g_encounter, &g->rng, &g_data, &g_level,
                               &g_walk, busy || g_win.event_step != 0)) {
        g_battle_formation = g_encounter.formation;
        g_data.level_x = (u16)(g_walk.x & ~0xF);
        g_data.level_y = (u16)(g_walk.y & ~0xF);
        if (getenv("PS2_ENCLOG") != NULL) {
            fprintf(stderr, "encounter: tick %u level %d cell %d,%d "
                            "formation %d\n", g->ticks, g_level.index,
                    g_data.level_x / 16, g_data.level_y / 16,
                    g_battle_formation);
        }
        goto_screen_white(g, SCREEN_BATTLE);
        return;
    }

    npc_tick(&g_npcs, &g->rng, walk_cell_open, &g_walk,
             g_walk.width, g_walk.height, g_walk.cam_x, g_walk.cam_y,
             party_x, party_y, party);
    if (g_npcs.esper_building) {
        /* loc_4DBE: B1's object $27 at the end of its scene --
         * BuildingID_EsperMansion, portrait $39, ScreenID_Building. No
         * level position: the building's loc_CFD8 sets F1's. */
        g_npcs.esper_building = false;
        g_bstate.building     = BUILDING_ESPER_MANSION;
        g_bstate.portrait     = 0x39;
        g_event_building      = true;
    }

    if (a_trace(g) && (result != WALK_RUNNING || g_level_event != 0)) {
        fprintf(stderr, "A: tick %u, not a talk: walk result %d, level "
                        "event $%03X, $DE70 %u, $DE72 %u\n", g->ticks,
                (int)result, g_level_event, g_win.event_step, g_win.event_done);
    }
    if (result == WALK_RUNNING) {
        if (g_level_event != 0) {
            level_event(g);
        } else {
            level_talk(g);
        }
    }
    if (g_event_reload) {
        /* screen_changed_flag from an event (loc_E0D8): LevelScreen
         * again, level_x/y_pos already set. */
        g_event_reload = false;
        goto_screen(g, SCREEN_LEVEL);
        return;
    }
    if (g_event_building) {
        /* loc_DFE6's move.b #ScreenID_Building: LevelScreenLoop ends at
         * loc_7DF6 and BuildingScreen runs, with the message window the
         * A button asked for never opened. */
        g_event_building = false;
        g_level_event    = 0;       /* BuildingScreen clears $DE00-$DFFF */
        goto_screen(g, SCREEN_BUILDING);
        return;
    }
    if (g_event_battle != 0) {
        /* ScreenID_Battle from an event: a boss. BattleScreen flashes to
         * white itself, as it does for an encounter. */
        g_battle_formation = g_event_battle;
        g_event_battle     = 0;
        /* BattleScreen (12065): level_x/y_pos from the leader's own
         * position, cell-aligned, so the level comes back where he
         * stood -- not where he came in. */
        if (!g_party_gone) {
            g_data.level_x = (u16)(g_walk.x & ~0xF);
            g_data.level_y = (u16)(g_walk.y & ~0xF);
        }
        goto_screen_white(g, SCREEN_BATTLE);
        return;
    }
    if (g_ending_next) {
        /* loc_E458: the ending screen, straight from the level loop. Its
         * first act, before the fade to white, is "The Place of Death". */
        g_ending_next = false;
        g_level_event = 0;
        queue_sound(g, MUSIC_THE_PLACE_OF_DEATH);
        goto_screen_white(g, SCREEN_ENDING);
        return;
    }
    win_prepare(&g_win, &g->input);
    /* ProcessWindows' camera test (window.c): the level screen is the
     * only one whose main camera the port keeps, in g_walk. */
    g_win.camera_off_grid = ((g_walk.cam_x | g_walk.cam_y) & 0xF) != 0;
    win_process(&g_win);
    g_win.camera_off_grid = false;

    if (result == WALK_LEVEL_CHANGE) {
        /* loc_6FA6: level_index and the arrival cell, SXFID_LevelChanged,
         * screen_changed_flag = -1 -- LevelScreen runs again, fade and
         * all. */
        if (g_walk.set_return) {
            /* loc_6F1C also clears $FFFFCB0E: the Hidapipe lasts until
             * the next way into a dungeon. */
            g_encounter.hidapipe = 0;
            gamedata_set_word(&g_data, GD_ENTRANCE_MAP, (u16)g_walk.return_level);
            gamedata_set_word(&g_data, GD_ENTRANCE_Y, (u16)g_walk.return_y);
            gamedata_set_word(&g_data, GD_ENTRANCE_X, (u16)g_walk.return_x);
            g_walk.set_return = false;
        }
        g_data.level_index = (u16)g_walk.go_level;
        g_data.level_x     = (u16)g_walk.end_x;
        g_data.level_y     = (u16)g_walk.end_y;
        if (g_walk.go_sfx != 0) {
            queue_sfx(g, g_walk.go_sfx);
        }
        if (g_walk.go_scene != 0) {
            g_level_oneshot = g_walk.go_scene;  /* event_flags, high byte */
        }
        if (g_walk.go_scene == SCENE_MOTHER_BRAIN_ARRIVAL) {
            /* (build 138d) LevelScreen's event_flags $E00 test: out
             * through white, in from it. */
            g_level_through_white = true;
            goto_screen_white(g, SCREEN_LEVEL);
        } else {
            goto_screen(g, SCREEN_LEVEL);
        }
        return;
    }

    if (result == WALK_EVENT) {
        /* loc_6EA6: the door starts an event instead; the level loop
         * runs it through ProcessAButtonPress's $DE70 path, as it does
         * the Teim scene. */
        g_win.event_step    = 1;
        g_win.event_done    = 0;
        g_level_event = g_walk.go_event;
        return;
    }

    if (result == WALK_BUILDING) {
        /* loc_6EE6: the door names the building, who is inside and, for
         * a shop, its stock. A door to a building that is not ported
         * stays shut, said once per door rather than once per frame
         * pushed against it. */
        if (!building_ported(g_walk.go_building)) {
            if (g_walk.go_building != g_building_warned) {
                fprintf(stderr, "level: door to building %d (portrait "
                                "$%02X) is not ported\n",
                        g_walk.go_building, g_walk.go_portrait);
                g_building_warned = g_walk.go_building;
            }
            return;
        }
        g_bstate.building    = g_walk.go_building;
        g_bstate.portrait    = g_walk.go_portrait;
        g_bstate.store_max   = g_walk.go_store_max;
        g_bstate.store_index = g_walk.go_store_index;
        g_data.level_x    = (u16)g_walk.end_x;
        g_data.level_y    = (u16)g_walk.end_y;
        goto_screen(g, SCREEN_BUILDING);
        return;
    }
    g_building_warned = -1;

    if (result == WALK_DEMO_ENDED) {
        /* ObjCL_Main, demo byte $FF: game_screen = ScreenID_Building with
         * level_x/y_pos snapped to the grid. building_index and
         * portrait_index were set by whoever started the walk. */
        g_data.level_x = (u16)g_walk.end_x;
        g_data.level_y = (u16)g_walk.end_y;
        goto_screen(g, SCREEN_BUILDING);
    }
}

/* LoadPortraits (ps2.asm:11969): portrait_index 1..$1E picks art for VRAM
 * $8000 and a palette, loaded into both the live palette and the fade
 * target (PaletteLoad1, then PaletteLoad2). Anything else -- 0, or $37 for
 * Rolf at home -- loads nothing. (build 146a) Here it loads nothing at
 * all: the portrait is its picture (assets/art/portraits), drawn in the
 * window's cells wherever window_picture binds it, with its own colours.
 * Until 146a the art went to tile $400 and the palette to line 2 as the
 * ROM's do, and the picture stood for them. */
static void load_portrait(int portrait)
{
    if (portrait <= 0 || portrait >= 0x1F
        || g_portrait_art_name[portrait][0] == '\0') {
        return;
    }
    g_portrait_loaded = portrait;
}

/* (build 142) WinSys's picture hook: the picture a portrait window's
 * cells stand for, where the art and palette its picture was made from
 * are the ones loaded (g_portrait_loaded, g_rolf_portrait_loaded), and the
 * tiles that art fills. Window n's picture is portrait n's art through
 * window n's map in portrait n's palette (tools/extract_pictures.py); it
 * stands for the tiles when the portrait loaded is n -- or another
 * number with the same art and palette: the Central Tower's copy window
 * (22) redrawn over the outside portrait's art (21). Rolf's two windows
 * (his own and the one the Governor and his house open) draw his art at
 * $4A0 -- only where BuildingScreen put it there: in the library the same
 * tiles hold the graph, shown through its own window, which (build 146)
 * draws the graph's picture there. NULL: the tiles are drawn as they
 * are. (build 146a) There are no portrait tiles: a window with no picture
 * draws nothing inside its frame. A binding's range is all the tiles the
 * windows' bytes can name over that art's place -- LoadPortraits' from
 * $400 to Rolf's $4A0, Rolf's and the graph's from $4A0 to $500 -- which
 * every portrait window's interior keeps inside (its map's bytes, counted
 * in build 146a), so the font's tiles of a window drawn over it are never
 * taken for the picture. */
static const Picture *window_picture(void *ctx, int id, u16 *first,
                                     u16 *count)
{
    const Game *g = ctx;
    if (g == NULL || g->screen != SCREEN_BUILDING) {
        return NULL;
    }
    if (id == WIN_LIBRARY_GRAPH) {
        if (g_bstate.building != BUILDING_LIBRARY) {
            return NULL;
        }
        *first = TILE_ROLF_PORTRAIT;
        *count = TILE_PORTRAITS_END - TILE_ROLF_PORTRAIT;
        return art_library_graph();
    }
    if (id == WIN_ROLF_PORTRAIT || id == WIN_ROLF_PORTRAIT2) {
        if (!g_rolf_portrait_loaded) {
            return NULL;
        }
        *first = TILE_ROLF_PORTRAIT;
        *count = TILE_PORTRAITS_END - TILE_ROLF_PORTRAIT;
        return art_portrait(0);
    }
    int n = id - WIN_PORTRAIT_START;
    int m = g_portrait_loaded;
    if (n <= 0 || n >= WIN_PORTRAIT_COUNT || n >= PORTRAITS_MAX || m <= 0
        || strcmp(g_portrait_art_name[n], g_portrait_art_name[m]) != 0
        || g_portrait_pal[n] != g_portrait_pal[m]) {
        return NULL;
    }
    *first = TILE_PORTRAIT;
    *count = TILE_ROLF_PORTRAIT - TILE_PORTRAIT;
    return art_portrait(n);
}

/* Building_Roof's flight, run by the building screen (tick_flight). */
typedef enum { FLIGHT_NONE, FLIGHT_FADE_OUT, FLIGHT_FADE_IN, FLIGHT_LOOP,
               FLIGHT_DONE }
    FlightPhase;
static FlightPhase g_flight_phase;
static int         g_flight_step;
static u16         g_flight_timer;     /* demo_timer */

/* BuildingScreen (ps2.asm:11838). ClearSpriteAndScroll wipes both planes,
 * so the room is black; the common palette goes to lines 0 and 1, Rolf's
 * portrait art to $9400 with his palette on line 1, and LoadPortraits
 * does the rest. The four special cases -- library, roof, spaceship,
 * Esper mansion -- follow. */
static void enter_building(Game *g)
{

    tileset_clear(&g_vram);
    tileset_place(&g_vram, &g_font, TILE_FONT);
    /* BuildingScreen's library case (loc_8304): LibrGraphPortArt and its
     * palette where Rolf's portrait would go. */
    /* Its roof case (loc_8318) branches past RolfPortraitArt and
     * pal_id_rolf_port both, straight to LoadPortraits: line 1 stays the
     * common palette's. */
    bool library = (g_bstate.building == BUILDING_LIBRARY);
    bool roof    = (g_bstate.building == BUILDING_TOWER_ROOF);
    bool esper   = (g_bstate.building == BUILDING_ESPER_MANSION);
    bool tyler   = (g_bstate.building == BUILDING_TYLER_SHIP);
    /* (build 142) Rolf's art and palette, as BuildingScreen loads them:
     * not in the library, on the roof, on Tyler's spaceship or in the
     * Esper mansion. LoadPortraits' art comes with load_portrait.
     * (build 146a) Rolf's art and palette, and the graph's in the library,
     * are their pictures now: neither goes to VRAM $9400 or line 1. */
    g_portrait_loaded      = 0;
    g_rolf_portrait_loaded = !library && !roof && !esper && !tyler;
    /* loc_8364, the Esper mansion: LutzPortraitArt at VRAM $0020 and
     * PaletteLoad1 $3B for line 3, where LutzPortraitTileInd draws (build
     * 127: on line 1 before, his picture stayed black). (build 147) Lutz
     * is his picture (assets/art/scenes/lutz): neither is loaded. */

    video_colours_clear(g->video, &g->video->palette);
    video_colours_load(g->video, &g->video->palette, 0, PALETTE_FIELD_COMMON);
    if (g_bstate.building == BUILDING_TYLER_SHIP) {
        /* loc_8326: all of VRAM filled with 0, event_routine + 1 (no
         * portrait window), pal_id_tit_scr, and LoadPortraits skipped.
         * The pictures the building then draws are building.c's (build
         * 82: the dream's fight, Tyler's help, the monitor). */
        video_colours_load(g->video, &g->video->palette, 0,
                           PALETTE_TITLE_SCREEN);
        static bool told;
        if (!told && (!g_tyler_portrait_ok || !g_tyler_monitor_ok)) {
            told = true;
            fprintf(stderr, "building: TylerPortraitTileInd.tmx or "
                            "TylerSpcshp_MonitorTileInd.tmx is missing -- "
                            "make extract-nametables (ART_DIR), then make "
                            "manifest; Tyler and the monitor stay blank\n");
        }
    } else {
        load_portrait(g_bstate.portrait);
    }
    g->video->backdrop = 0;

    /* BuildingScreen clears object RAM ($FFFFE800 among it). */
    g_bstate.picture         = TYLER_VIEW_NONE;
    g_bstate.monitor_frame   = 0;
    g_bstate.fx              = BUILDING_FX_NONE;
    g_bstate.palette_2a      = false;
    g_bstate.graph_overlay   = false;
    g_bstate.spawn_explosion = false;
    memset(&g_monitor_expl, 0, sizeof g_monitor_expl);
    memset(&g_ship, 0, sizeof g_ship);
    g_flight_phase = FLIGHT_NONE;

    win_init(&g_win);
    g_win.ctx           = g;
    g_win.script_lookup = script_lookup;
    g_win.script_text   = script_text;     /* (build 163) */
    g_win.sound         = intro_sound;
    g_win.music         = intro_music;
    g_win.music_latch   = intro_music_latch;
    g_win.char_name     = g_data.chr[0].name;   /* character_index = 0 */
    g_win.gd            = &g_data;
    g_win.slot_info     = intro_select_slot_info;   /* Data Memory's slots */
    g_win.picture       = window_picture;           /* (build 142) */
    g_bstate.rng        = &g->rng;
    building_begin(&g_win, &g_bstate);
    /* loc_8318 on DezolisSkure: addq.w #5, (event_routine) -- from 1 to
     * 6, loc_C578, the flight to Paseo on the first idle pass. */
    if (roof && g_data.level_index == LEVEL_DEZOLIS_SKURE) {
        g_win.event_routine += 5;
    }

    if (g_bstate.building >= 0 && g_bstate.building < BUILDINGS_MAX
        && g_building_music[g_bstate.building] != 0) {
        queue_sound(g, (u8)g_building_music[g_bstate.building]);
    }
}

/* Ustvestia's lesson (loc_C7AC), inside one call of the building's
 * routine: PaletteFadeFrom, SXFID_Musik ($B1), $78 frames with
 * vblank_routine $14, PaletteFadeTo. Nothing else runs meanwhile. */
static void fade_out_step(Game *g);
static void fade_in_step(Game *g);

typedef enum { LESSON_NONE, LESSON_FADE_OUT, LESSON_WAIT, LESSON_FADE_IN,
               LESSON_PANEL_FADE }
    LessonPhase;
static LessonPhase g_lesson_phase;
static int         g_lesson_step;

static bool tick_lesson(Game *g)
{
    switch (g_lesson_phase) {
        case LESSON_FADE_OUT: {
            fade_out_step(g);
            if (++g_lesson_step >= FADE_FRAMES) {
                queue_sound(g, SFX_MUSIK);
                g_lesson_phase = LESSON_WAIT;
                g_lesson_step  = 0;
            }
            return true;
        }
        case LESSON_WAIT: {
            if (++g_lesson_step >= 0x78) {
                video_fade_begin(g->video, VIDEO_FX_FROM_BLACK,
                                 VIDEO_LINES_ALL);
                g_lesson_phase = LESSON_FADE_IN;
                g_lesson_step  = 0;
            }
            return true;
        }
        case LESSON_FADE_IN: {
            fade_in_step(g);
            if (++g_lesson_step >= FADE_FRAMES) {
                g_lesson_phase = LESSON_NONE;
            }
            return true;
        }
        case LESSON_PANEL_FADE: {
            /* Gaira's control panel, loc_CC10: PaletteFadeFrom inside the
             * routine, then $E0 into the sound queue (the music fades). */
            fade_out_step(g);
            if (++g_lesson_step >= FADE_FRAMES) {
                queue_sound(g, SOUND_FADE_OUT);
                g_lesson_phase = LESSON_NONE;
            }
            return true;
        }
        default: {
            return false;
        }
    }
}

/* loc_66F6 (ps2.asm:9527), every frame of BuildingScreenLoop after
 * CheckPrepareWindows, with $F632/$F634 carried over from the level.
 * Gaira's control panel, while event_routine < 4, every eighth frame:
 * SXFID_Alarm when the step's low nibble is 0, the backdrop (palette_table,
 * line 0 colour 0) $00E for the first eight steps of sixteen and black for
 * the rest, and line 2 colour 5 through loc_6790. Tyler's spaceship
 * (building $11) from event_routine $A, every fourth: line 3 colours
 * 12-15 through loc_6798 -- the monitor's, which (build 149) its frames'
 * slot maps name (the red's and the explosion's). */
static void building_palette_cycle(Game *g)
{
    static const u16 PANEL[4] = { 0x04EE, 0x04AE, 0x046E, 0x042E };
    static const u16 MONITOR[4][4] = {
        { 0x06EE, 0x06EE, 0x02AE, 0x000E },
        { 0x0AEE, 0x08EE, 0x008E, 0x000A },
        { 0x06EE, 0x0AEE, 0x02AE, 0x0006 },
        { 0x02AE, 0x08EE, 0x04CE, 0x000A },
    };
    Video *v = g->video;

    if (g_bstate.building == BUILDING_GAIRA_PANEL) {
        if (g_win.event_routine >= 4) {
            return;
        }
        if (--g_cycle_wait >= 0) {
            return;
        }
        g_cycle_wait = 7;
        u16 step = g_cycle_step++;
        if ((step & 0xF) == 0) {
            queue_sfx(g, SFX_ALARM);
        }
        video_backdrop(v, ((step & 0xF) < 8) ? 0x000E : 0x0000);
        video_glow(v, 0x25, PANEL[step & 3]);
        return;
    }
    if (g_bstate.building == BUILDING_TYLER_SHIP) {
        if (g_win.event_routine < 0xA) {
            return;
        }
        if (--g_cycle_wait >= 0) {
            return;
        }
        g_cycle_wait = 3;
        u16 step = g_cycle_step++;
        for (int i = 0; i < 4; i++) {
            video_glow(v, 0x3C + i, MONITOR[step & 3][i]);
        }
    }
}

static void dream_erase_step(int order_index);

/* One frame of the dissolve Tyler's spaceship asked for: loc_6BBE's
 * FadeArtFill (four frames, then one pixel of every tile, 64 times; the
 * ROM ORs into tiles loc_6C54 left blank, so a copy is the same) or
 * loc_6C54's loc_6CA4 (a byte of every tile each frame, 32 times).
 * (build 151) The fight and the help are pictures: their dissolves are the
 * reveal mask's alone. Tyler's art (the monitor view, only ever dissolved
 * in) is still tiles. */
static bool picture_fx_step(void)
{
    if (g_bstate.fx == BUILDING_FX_IN) {
        if (--g_tyler_fx_gate > 0) {
            return false;
        }
        g_tyler_fx_gate = FADE_GATE;
        if (g_tyler_fx_step < g_fade_order.count) {
            int pixel = g_fade_order.value[g_tyler_fx_step];
            if (g_bstate.fx_art == TYLER_VIEW_MONITOR) {
                tileset_reveal_pixel(&g_vram, &g_tyler_art, TILE_DREAM,
                                     g_bstate.fx_tiles, pixel);
            }
            g_dissolve_hidden &= ~((u64)1 << pixel);
        }
        g_tyler_fx_step++;
        return g_tyler_fx_step >= FADE_STEPS;
    }
    dream_erase_step(g_tyler_fx_step);
    g_tyler_fx_step++;
    return g_tyler_fx_step >= DREAM_OUT_STEPS;
}

/* Obj_ExplosionTylerSpaceship (ps2.asm:2471), from RunObjects. ObjETS_Init
 * ($118,$98, 2(a0) = $30, tile word $6000, Map_PalmExplosion, $26 = 3)
 * falls into ObjETS_Main the same call: $26 down, and below zero $26 = 3
 * and $24 + 1; at 6 2(a0) = 1, and RunObject clears the slot in that same
 * call (loc_3DC), so BuildSprites never draws a seventh frame. */
static void monitor_explosion_tick(void)
{
    if (!g_monitor_expl.live) {
        return;
    }
    if (g_monitor_expl.routine == 0) {
        g_monitor_expl.timer   = 3;
        g_monitor_expl.frame   = 0;
        g_monitor_expl.routine = 1;
    }
    if (--g_monitor_expl.timer >= 0) {
        return;
    }
    g_monitor_expl.timer = 3;
    if (++g_monitor_expl.frame == MONITOR_EXPL_FRAMES) {
        memset(&g_monitor_expl, 0, sizeof g_monitor_expl);
    }
}

/* loc_59E4: Obj_Spaceship's path, x and y a step, each + $80. */
static const u8 SHIP_PATH[11][2] = {
    { 0xB8, 0x98 }, { 0x60, 0x58 }, { 0x58, 0x20 }, { 0x70, 0x20 },
    { 0x88, 0x28 }, { 0x98, 0x30 }, { 0xA0, 0x38 }, { 0xA8, 0x40 },
    { 0xA8, 0x40 }, { 0xA8, 0x40 }, { 0xB0, 0x48 }
};

/* Obj_Spaceship (ps2.asm:8138), from RunObjects. loc_597A, the init --
 * 2(a0) = $10, tile word $4400, loc_71DEA, $24 = -1, $3C = loc_59E4 --
 * falls into loc_59A4 the same call: $26 (0 in the slot BuildingScreen
 * cleared) down, and below zero $26 = 4, $24 + 1, x and y the next pair
 * + $80. When $24 reaches $A, 2(a0) = 1 and RunObject clears the slot in
 * that call: mappings 0-9 five frames each, the eleventh never drawn. */
static void spaceship_tick(void)
{
    if (!g_ship.live) {
        return;
    }
    if (g_ship.routine == 0) {
        g_ship.frame   = -1;
        g_ship.path    = 0;
        g_ship.routine = 1;
    }
    if (--g_ship.timer >= 0) {
        return;
    }
    g_ship.timer = 4;
    g_ship.frame++;
    g_ship.x = (u16)(SHIP_PATH[g_ship.path][0] + 0x80);
    g_ship.y = (u16)(SHIP_PATH[g_ship.path][1] + 0x80);
    g_ship.path++;
    if (g_ship.frame == 0xA) {
        memset(&g_ship, 0, sizeof g_ship);
    }
}

/* loc_C58A, once the building has asked for the flight: display off,
 * ClearSpriteAndScroll (both planes and the sprites), loc_71362 at tile 1
 * and loc_71702 at tile $400, loc_716BA 6x6 at plane B row 7 col 22,
 * ObjID_Spaceship into $EC00, PaletteLoad1 $39 (going to DezolisSkure;
 * $3A anywhere else -- level_index is the destination by now) and $3C,
 * both to the target, demo_timer $78, SXFID_SpaceshipDeparted; then
 * PaletteFadeTo, which zeroes all 64 live entries first. */
static void flight_begin(Game *g)
{
    tileset_place(&g_vram, &g_ship_art, TILE_FLIGHT_SHIP);
    g_portrait_loaded = 0;      /* (build 142) the ship is at $400 now */
    memset(&g_ship, 0, sizeof g_ship);
    g_ship.live = true;
    /* (build 148) The planet is its picture, by where the ship goes:
     * loc_71362 at tile 1 and PaletteLoad1 $39 or $3A for line 3 are not
     * loaded. */
    g_planet_picture = (g_data.level_index == LEVEL_DEZOLIS_SKURE)
                     ? g_planet_to_dezolis : g_planet_to_motavia;
    video_colours_load(g->video, &g->fade_target, 2,
                       PALETTE_FLIGHT_SPACESHIP);
    g_flight_timer = FLIGHT_TIMER;
    queue_sound(g, SFX_SPACESHIP_DEPARTED);
    video_fade_begin(g->video, VIDEO_FX_FROM_BLACK, VIDEO_LINES_ALL);
    g_flight_phase = FLIGHT_FADE_IN;
    g_flight_step  = 0;
}

/* The flight inside the building's call. loc_C572's PaletteFadeFrom and
 * PaletteFadeTo run on vblank_routine $24 -- no windows, no objects, no
 * demo_timer, no pause -- and SpaceShipLoop is CheckGamePause, vblank
 * routine 8 (demo_timer - 1), RunObjects, BuildSprites until demo_timer is
 * 0: 120 frames. Then ScreenID_Level and SXFID_SpaceshipLanded, and
 * LevelScreen's own PaletteFadeFrom over the flight's last picture.
 * Returns true while it has the screen. */
static bool tick_flight(Game *g, bool paused_ok)
{
    switch (g_flight_phase) {
        case FLIGHT_FADE_OUT: {
            fade_out_step(g);
            if (++g_flight_step >= FADE_FRAMES) {
                flight_begin(g);
            }
            return true;
        }
        case FLIGHT_FADE_IN: {
            fade_in_step(g);
            if (++g_flight_step >= FADE_FRAMES) {
                g_flight_phase = FLIGHT_LOOP;
            }
            return true;
        }
        case FLIGHT_LOOP: {
            if (!paused_ok) {
                return false;
            }
            if (g_flight_timer != 0) {
                g_flight_timer--;
            }
            spaceship_tick();
            if (g_flight_timer == 0) {
                g_flight_phase = FLIGHT_DONE;
                queue_sound(g, SFX_SPACESHIP_LANDED);
                g_level_walk = true;
                goto_screen(g, SCREEN_LEVEL);
            }
            return true;
        }
        case FLIGHT_DONE: {
            return true;
        }
        default: {
            return false;
        }
    }
}

/* BuildingScreenLoop: RunObjects, Building_CheckRoutine,
 * CheckPrepareWindows, and ProcessWindows from the VBlank handler. */
static void tick_building(Game *g)
{
    /* The flight's two fades are inside the building's call, before any
     * CheckGamePause; SpaceShipLoop has its own. */
    if (g_flight_phase == FLIGHT_FADE_OUT || g_flight_phase == FLIGHT_FADE_IN
        || g_flight_phase == FLIGHT_DONE) {
        tick_flight(g, false);
        return;
    }
    /* BuildingScreenLoop starts with CheckGamePause too. */
    if (!pause_gate(g)) {
        return;
    }
    if (tick_flight(g, true)) {
        return;
    }
    if (tick_lesson(g)) {
        return;
    }
    if (g_bstate.fx != BUILDING_FX_NONE) {
        /* Inside the building's call in the ROM: vblank_routine $24 is
         * VBlankSub_1C_24 only (joypad, palette and sprite DMA), so no
         * ProcessWindows, no objects and no loc_66F6 until it returns.
         * Then the rest of that loop pass: CheckPrepareWindows, loc_66F6. */
        if (!picture_fx_step()) {
            return;
        }
        g_bstate.fx = BUILDING_FX_NONE;
        win_prepare(&g_win, &g->input);
        win_process(&g_win);
        building_palette_cycle(g);
        return;
    }

    monitor_explosion_tick();                   /* RunObjects */
    win_run_cursors(&g_win, &g->input);

    BuildingResult result = building_tick(&g_bstate, &g_win, &g_data,
                                          &g->input);
    if (g_bstate.clear_event_flags) {
        /* Building_EsperMansion's `move.w #0, (event_flags).w`. */
        g_bstate.clear_event_flags = false;
        g_event_hold = 0;
    }

    /* REORGANIZE's loc_F550: the building asks, the battle state is
     * here. */
    if (g_bstate.reset_commands) {
        g_bstate.reset_commands = false;
        battle_reset_commands(&g_battle, &g_data, true);
    }
    /* Tyler's spaceship: loc_CD50's PaletteLoad1 and PaletteLoad2 $2A
     * (Pal_CentTow2Port, 32 words to lines 2 and 3), and loc_CE20's
     * ObjID_MonitorExplosion into $E800, first run next pass. */
    if (g_bstate.graph_overlay) {
        /* (build 138d) loc_C4B2: PaletteLoad2 $32, straight into the
         * live line 1 under the graph -- its hidden lines appear. */
        g_bstate.graph_overlay = false;
        video_glow_load(g->video, 1, PALETTE_LIBRARY_GRAPH_OVERLAY);
    }
    if (g_bstate.palette_2a) {
        g_bstate.palette_2a = false;
        g_portrait_loaded   = 0;        /* (build 142) line 2 is not its */
        video_colours_load(g->video, &g->video->palette, 2,
                           PALETTE_PORTRAIT_CENTRAL_TOWER_OUTSIDE_2);
        video_colours_load(g->video, &g->fade_target, 2,
                           PALETTE_PORTRAIT_CENTRAL_TOWER_OUTSIDE_2);
    }
    if (g_bstate.spawn_explosion) {
        g_bstate.spawn_explosion = false;
        memset(&g_monitor_expl, 0, sizeof g_monitor_expl);
        g_monitor_expl.live = true;
    }

    switch (result) {
        case BUILDING_LOAD_PORTRAIT: {
            load_portrait(g_bstate.portrait);
            break;
        }
        case BUILDING_TO_LEVEL: {
            if ((g_bstate.event_flags >> 8) != 0) {
                /* ProcessStealItem's event_flags = $C00: the next level
                 * load's loc_7E02 case $C (Shir gone). */
                g_level_oneshot = (u8)(g_bstate.event_flags >> 8);
            }
            g_bstate.event_flags = 0;
            g_level_walk = true;
            goto_screen(g, SCREEN_LEVEL);
            return;
        }
        case BUILDING_CHANGED: {
            /* screen_changed_flag: BuildingScreen runs again from the top
             * for the building and portrait now named. One the port does
             * not have yet leaves you on the street instead. */
            if (building_ported(g_bstate.building)) {
                goto_screen(g, SCREEN_BUILDING);
                return;
            }
            fprintf(stderr, "building: %d is not ported; back to the "
                            "street\n", g_bstate.building);
            g_level_walk = true;
            goto_screen(g, SCREEN_LEVEL);
            return;
        }
        case BUILDING_FLIGHT: {
            /* Building_Roof, loc_C572 or loc_C578: the palette as it
             * stands is what the fades work from. */
            video_target_from_shown(g->video, &g->fade_target);
            if (g_bstate.flight_fade) {
                g_flight_phase = FLIGHT_FADE_OUT;
                g_flight_step  = 0;
            } else {
                flight_begin(g);
            }
            return;
        }
        case BUILDING_PICTURE: {
            g_tyler_fx_step = 0;
            g_tyler_fx_gate = FADE_GATE;
            if (g_bstate.fx == BUILDING_FX_IN) {
                g_dissolve_hidden = DISSOLVE_ALL_HIDDEN;
            }
            return;
        }
        case BUILDING_FADE_OUT: {
            g_lesson_phase = LESSON_PANEL_FADE;
            g_lesson_step  = 0;
            return;
        }
        case BUILDING_LESSON: {
            /* The palette as it stands is what PaletteFadeTo comes back
             * to. */
            video_target_from_shown(g->video, &g->fade_target);
            g_lesson_phase = LESSON_FADE_OUT;
            g_lesson_step  = 0;
            return;
        }
        case BUILDING_RESET: {
            /* Data Memory, "No" to more adventure: jmp EntryPoint. The
             * console starts over at the SEGA screen. */
            memset(&g_bstate, 0, sizeof g_bstate);
            g_level_walk = false;
            goto_screen(g, SCREEN_SEGA);
            return;
        }
        default: {
            break;
        }
    }

    win_prepare(&g_win, &g->input);
    win_process(&g_win);
    building_palette_cycle(g);
}

/* Plane A of Tyler's spaceship and the explosion's sprites. DrawArtTiles
 * wrote the maps once; the port redraws them every frame, so a view
 * stands for what the ROM left there: the second picture's map after its
 * dissolve out is all blank tiles, and loc_CD50's two cleared rows are
 * the rows the monitor view does not draw. */
static void render_tyler(Game *g)
{
    switch (g_bstate.picture) {
        case TYLER_VIEW_BATTLE: {
            nametable_draw(g->video, LAYER_A, &g_dream_map[0],
                           DREAM_COL, DREAM_ROW);
            /* (build 151) From its picture, through the dissolves. */
            nametable_picture_hidden(g->video, LAYER_A, &g_dream_map[0],
                                     g_dream_battle_picture, DREAM_COL,
                                     DREAM_ROW, g_dissolve_hidden);
            break;
        }
        case TYLER_VIEW_HELP: {
            nametable_draw(g->video, LAYER_A, &g_help_map,
                           DREAM_COL, DREAM_ROW);
            nametable_picture_hidden(g->video, LAYER_A, &g_help_map,
                                     g_dream_help_picture, DREAM_COL,
                                     DREAM_ROW, g_dissolve_hidden);
            break;
        }
        case TYLER_VIEW_MONITOR: {
            if (g_tyler_portrait_ok) {
                nametable_draw(g->video, LAYER_A, &g_tyler_portrait_map,
                               TYLER_PORTRAIT_COL, TYLER_PORTRAIT_ROW);
                /* (build 152) Tyler from his picture, through the
                 * dissolve that brings him in. */
                nametable_picture_hidden(g->video, LAYER_A,
                                         &g_tyler_portrait_map,
                                         g_tyler_portrait_picture,
                                         TYLER_PORTRAIT_COL,
                                         TYLER_PORTRAIT_ROW,
                                         g_dissolve_hidden);
            }
            if (g_tyler_monitor_ok) {
                nametable_draw_rows(g->video, LAYER_A, &g_tyler_monitor_map,
                                    TYLER_MONITOR_COL, TYLER_MONITOR_ROW,
                                    g_bstate.monitor_frame
                                        * TYLER_MONITOR_ROWS,
                                    TYLER_MONITOR_ROWS);
            }
            /* (build 149) The frame's picture; (build 152) through the
             * dissolve too, which brings it in with Tyler. Their art and
             * palette $2A stay: the monitor's explosion is a sprite from
             * the same tiles, in $2A's line 3. */
            int frame = g_bstate.monitor_frame;
            if (g_tyler_monitor_ok && frame >= 0
                && frame < TYLER_MONITOR_FRAMES) {
                nametable_picture_rows(g->video, LAYER_A,
                                       &g_tyler_monitor_map,
                                       g_tyler_monitor[frame],
                                       TYLER_MONITOR_COL, TYLER_MONITOR_ROW,
                                       TYLER_MONITOR_ROWS, g_dissolve_hidden);
            }
            break;
        }
        default: {
            break;
        }
    }

    const NpcSceneType *st = npc_scene_type(MONITOR_EXPL_OBJECT);
    if (!g_monitor_expl.live || st == NULL
        || g_monitor_expl.frame >= st->frames) {
        return;
    }
    int f = g_monitor_expl.frame;
    for (int i = 0; i < st->pieces[f]; i++) {
        const NpcPiece *p = &st->frame[f][i];
        Sprite sp;
        memset(&sp, 0, sizeof sp);
        sp.x      = (s16)(MONITOR_EXPL_X - 0x80 + p->x);
        sp.y      = (s16)(MONITOR_EXPL_Y - 0x80 + p->y);
        sp.tile   = (u16)(st->tile + p->tile);
        sp.w      = p->w;
        sp.h      = p->h;
        sp.pal    = (u8)st->line;
        sp.flip_h = p->flip_h;
        sp.flip_v = p->flip_v;
        video_sprite_push(g->video, &sp);
    }
}

/* The flight's picture: plane A empty, the planet in plane B, the ship's
 * sprites (loc_71DEA, tile word $4400: line 2, low priority). */
static void render_flight(Game *g)
{
    nametable_draw(g->video, LAYER_B, &g_planet_map,
                   FLIGHT_PLANET_COL, FLIGHT_PLANET_ROW);
    /* (build 148) The planet from its picture: flight_begin's. */
    nametable_picture(g->video, LAYER_B, &g_planet_map, g_planet_picture,
                      FLIGHT_PLANET_COL, FLIGHT_PLANET_ROW);
    const NpcSceneType *st = npc_scene_type(SPACESHIP_OBJECT);
    if (!g_ship.live || st == NULL || g_ship.frame < 0
        || g_ship.frame >= st->frames) {
        return;
    }
    int f = g_ship.frame;
    for (int i = 0; i < st->pieces[f]; i++) {
        const NpcPiece *p = &st->frame[f][i];
        Sprite sp;
        memset(&sp, 0, sizeof sp);
        sp.x      = (s16)(g_ship.x - 0x80 + p->x);
        sp.y      = (s16)(g_ship.y - 0x80 + p->y);
        sp.tile   = (u16)(st->tile + p->tile);
        sp.w      = p->w;
        sp.h      = p->h;
        sp.pal    = (u8)st->line;
        sp.flip_h = p->flip_h;
        sp.flip_v = p->flip_v;
        video_sprite_push(g->video, &sp);
    }
}

static void render_building(Game *g)
{
    if (g_flight_phase == FLIGHT_FADE_IN || g_flight_phase == FLIGHT_LOOP
        || g_flight_phase == FLIGHT_DONE) {
        render_flight(g);
        return;
    }
    if (g_bstate.building == BUILDING_TYLER_SHIP) {
        render_tyler(g);
    }
    if (g_bstate.building == BUILDING_ESPER_MANSION && g_lutz_map_ok) {
        nametable_draw(g->video, LAYER_A, &g_lutz_map, LUTZ_COL, LUTZ_ROW);
        /* (build 147) Lutz drawn from his picture over the cells. */
        nametable_picture(g->video, LAYER_A, &g_lutz_map, g_lutz_picture,
                          LUTZ_COL, LUTZ_ROW);
    }
    win_render(&g_win, g->video, TILE_FONT, 0, 0);
}


/* ---- the battle screen ---- */

static const u8 *battle_enemy_name_hook(void *ctx, int id);
static const char *battle_enemy_text_hook(void *ctx, int id);
static void render_battle(Game *g);

static Nametable g_battle_floor;
static Nametable g_mother_brain_picture;   /* loc_AE8A0 */

/* BattleScreen (12047), the part after the flash to white: VRAM $0000-
 * $3FFF cleared, the font at $A000, the floor art at $F020 and its
 * nametable at plane B row 12, PalettePtrs $2F, loc_796, the stat boxes
 * drawn with the display still off. */
static void enter_battle(Game *g)
{
    tileset_clear(&g_vram);
    tileset_place(&g_vram, &g_font, TILE_FONT);

    /* (build 148) The floor is its picture (g_battle_floor_picture): its
     * art is not put at $F020 (tile $781); its table still lays the
     * cells. */
    if (g_battle_floor.width == 0) {
        nametable_load(&g_battle_floor,
                       "assets/nametables/BattleFloorTileInd.tmx");
    }
    battle_set_fade_order(g_fade_order.value);

    g->video->backdrop = 0;

    win_init(&g_win);
    win_clear_painted(&g_win);
    g_win.ctx           = g;
    g_win.script_lookup = script_lookup;
    g_win.script_text   = script_text;     /* (build 163) */
    g_win.sound         = intro_sound;
    g_win.music         = intro_music;
    g_win.music_latch   = intro_music_latch;
    g_win.char_name     = g_data.chr[0].name;
    g_win.gd            = &g_data;
    g_win.enemy_name_lookup = battle_enemy_name_hook;
    g_win.enemy_text        = battle_enemy_text_hook;     /* (build 163) */

    battle_enter(&g_battle, g_battle_formation, &g_data, &g_win, &g->rng,
                 &g_vram, g->video, &g_battle_loaded);
    g_battle.cycle_step = g_cycle_step;     /* $F632/$F634 carry on */
    g_battle.cycle_wait = g_cycle_wait;

    queue_sound(g, g_battle.music);

    /* loc_86A2: the queue drained before the display comes back on --
     * every window instant, so a handful of passes. */
    for (int i = 0; i < 64 && !win_idle(&g_win); i++) {
        win_prepare(&g_win, &g->input);
        win_process(&g_win);
    }

    /* The display comes on white and loc_62CC fades it down to the
     * loaded palette over the next 21 frames. */
    video_fade_begin(g->video, VIDEO_FX_FROM_WHITE, VIDEO_LINES_ALL);
}

/* BattleScreenLoop (12200): CheckGamePause, the VBlank's window pass,
 * RunObjects, BattleScreen_CheckRoutine, CheckPrepareWindows. */
static void tick_battle(Game *g)
{
    if (battle_in_loop(&g_battle) || g_battle.phase == BP_DISSOLVE) {
        if (!pause_gate(g)) {
            return;
        }
    }

    win_run_cursors(&g_win, &g->input);
    if (getenv("PS2_BTRACE") != NULL) {
        int ci = -1;
        for (int i = 0; i < g_win.painted_count; i++) {
            if (g_win.painted[i].cursor.kind != CURSOR_NONE) {
                ci = g_win.painted[i].id * 100 + g_win.painted[i].cursor.index;
            }
        }
        fprintf(stderr, "bt %u ev %d main %d opt %d opt2 %d word %04X saved %02X depth %d painted %d cur %d pressed d%d c%d b%d\n",
                g->ticks, g_win.event_routine, g_battle.main_routine,
                g_win.battle_option, g_win.battle_option2,
                g_win.window_index[0], g_win.window_index_saved, g_win.depth,
                g_win.painted_count, ci, g->input.pressed.down,
                g->input.pressed.c, g->input.pressed.b);
    }

    BattleResult r = battle_tick(&g_battle, &g_data, &g_win, &g->rng,
                                 &g_vram, g->video,
                                 &g_battle_loaded, &g->input, intro_sound, g);
    if (battle_in_loop(&g_battle)) {
        win_prepare(&g_win, &g->input);
        win_process(&g_win);
    }

    if (r == BATTLE_TO_LEVEL || r == BATTLE_GAME_OVER) {
        g_cycle_step = g_battle.cycle_step;
        g_cycle_wait = g_battle.cycle_wait;
    }
    if (r == BATTLE_TO_LEVEL) {
        /* ScreenID_Level: LevelScreen runs again from the top, with
         * PaletteFadeFrom first. loc_2E24 moved the party to Gaira;
         * the level index is already in the save block. */
        g_level_walk = true;
        if (g_battle.to_level_change) {
            /* loc_2E24's `move.w #$A00, (event_flags).w`. */
            g_level_oneshot = SCENE_GAIRA_PRISON;
        }
        goto_screen(g, SCREEN_LEVEL);
    } else if (r == BATTLE_GAME_OVER) {
        /* ScreenID_Sega: BattleScreenLoop sees it and runs
         * GameOverScreen before MainGameLoop gets the SEGA screen. */
        begin_gameover(g, SCREEN_BATTLE);
    }
}

/* The EnemyNames row for {enemy}, from battle.c's tables. */
static const u8 *battle_enemy_name_hook(void *ctx, int id)
{
    (void)ctx;
    return battle_enemy_name(id);
}

/* (build 163) The language's name for {enemy}, by battle.json's label. */
static const char *battle_enemy_text_hook(void *ctx, int id)
{
    (void)ctx;
    return lang_enemy_name(battle_enemy_label(id));
}

/* ---- GameOverScreen (12255) ---- */

typedef enum { GO_DISSOLVE, GO_FADE_OUT, GO_FADE_IN, GO_WAIT } GameOverPhase;
static GameOverPhase g_go_phase;
static int           g_go_step;
/* (build 157c) The screen GameOverScreen starts over -- the battle's or
 * the level's loop branches to it -- drawn under the erase and the fade
 * as that loop left it. */
static ScreenId      g_go_over = SCREEN_BATTLE;

/* No fade: the loop that found ScreenID_Sega branches to GameOverScreen,
 * which begins over what that loop last showed. */
static void begin_gameover(Game *g, ScreenId over)
{
    g_go_over         = over;
    g->screen         = SCREEN_GAMEOVER;
    g->screen_entered = false;
    g->fade           = FADE_NONE;
}

/* The game over music, then tiles 0-$5BF erased over 32 frames
 * (loc_6C54), PaletteFadeFrom, the title background's art under its own
 * table (loc_6C5DA: g_gameover_b) and the message typed onto plane A at
 * $4514 -- row 10, column 10 -- four lines high, with $708 frames or B/C
 * to end it. */
static void enter_gameover(Game *g)
{
    /* The one-shot scene is event_flags ($C710), inside the flag block a
     * CONTINUE or a new game loads afresh; the port keeps it apart, so a
     * scene a lost battle left pending -- the aftermath the Mother Brain
     * confrontation queues -- is dropped here instead. */
    g_level_oneshot     = 0;
    g_mother_brain_glow = false;

    queue_sound(g, MUSIC_OVER);
    g_go_phase = GO_DISSOLVE;
    g_go_step  = 0;
    /* (build 157c) The window system is set up for the message with the
     * screen, after the fade (gameover_setup): until then the windows on
     * screen stay, plane A cells whose tiles the erase takes with the
     * rest. Until 157c they went at the erase's first frame. */
}

/* GameOverScreen's setup, the display off after PaletteFadeFrom:
 * ClearSpriteAndScroll empties both planes, so the windows go; the
 * message's window state from here. */
static void gameover_setup_windows(Game *g)
{
    win_init(&g_win);
    win_clear_painted(&g_win);
    g_win.ctx           = g;
    g_win.script_lookup = script_lookup;
    g_win.script_text   = script_text;     /* (build 163) */
    g_win.sound         = intro_sound;
    g_win.gd            = &g_data;
    g_win.character_index = 0;
}

static void gameover_erase_step(int order_index)
{
    if (order_index >= g_fade_order.count) {
        return;
    }
    int pixel = g_fade_order.value[order_index] & ~1;
    /* (build 155) The enemy art's tiles are among them: its reveal mask
     * loses the byte too (Mother Brain's star field is drawn through it). */
    g_battle.art_hidden |= (u64)3 << pixel;
    for (int i = 0; i < 0x5C0 && i < g_vram.count; i++) {
        g_vram.tiles[i].px[pixel]     = 0;
        g_vram.tiles[i].px[pixel + 1] = 0;
    }
}

static void tick_gameover(Game *g)
{
    switch (g_go_phase) {
        case GO_DISSOLVE: {
            gameover_erase_step(g_go_step++);
            if (g_go_step >= 32) {
                g_go_phase = GO_FADE_OUT;
                g_go_step  = 0;
            }
            break;
        }
        case GO_FADE_OUT: {
            fade_out_step(g);
            if (++g_go_step >= FADE_FRAMES) {
                /* The screen rebuilt with the display off: the font by
                 * DrawArtTiles2, colour 11 (the window fill) cleared, so
                 * the message sits on the picture with no box. */
                tileset_clear(&g_vram);
                tileset_place_clearing(&g_vram, &g_font, TILE_FONT,
                                       FONT_FILL_COLOUR);
                gameover_setup_windows(g);
                /* (build 154) The background is its picture: its art
                 * is not loaded. */
                video_fade_begin(g->video, VIDEO_FX_FROM_BLACK,
                                 VIDEO_LINES_ALL);
                video_colours_load(g->video, &g->fade_target, 0,
                                   PALETTE_TITLE_SCREEN);
                win_plain_text(&g_win, SCRIPT_BATTLE_OTHERS_FAILED_RESTORE_PEACE, 10, 10, 4);
                g->demo_timer = 0x708;
                g_go_phase = GO_FADE_IN;
                g_go_step  = 0;
            }
            break;
        }
        case GO_FADE_IN: {
            win_process(&g_win);
            video_fade_step(g->video, VIDEO_FX_FROM_BLACK, &g->fade_target);
            if (++g_go_step >= FADE_FRAMES) {
                g_go_phase = GO_WAIT;
            }
            break;
        }
        default: {
            /* GameOverScreenLoop */
            win_prepare(&g_win, &g->input);
            win_process(&g_win);
            if (--g->demo_timer == 0 || g->input.pressed.b
                || g->input.pressed.c) {
                /* GameOverScreen returns; MainGameLoop finds
                 * ScreenID_Sega, and SegaScreen fades this out first
                 * (build 157a: it was cut). */
                goto_sega(g);
            }
            break;
        }
    }
}

static void render_gameover(Game *g)
{
    /* (build 157a) The tick the battle hands over draws before
     * enter_gameover: the battle, as GameOverScreen starts over it. Until
     * 157a that frame took g_go_phase as the last game over left it --
     * after one, the message's background over the battle's VRAM. */
    if (!g->screen_entered || g_go_phase == GO_DISSOLVE
        || g_go_phase == GO_FADE_OUT) {
        /* (build 157c) Or the level, after a wipe-out on the map. */
        if (g_go_over == SCREEN_LEVEL) {
            render_level(g);
        } else {
            render_battle(g);
        }
        return;
    }
    nametable_draw(g->video, LAYER_B, &g_gameover_b, 0, 0);
    /* (build 154) From its picture. */
    nametable_picture(g->video, LAYER_B, &g_gameover_b,
                      g_gameover_background_picture, 0, 0);
    win_render(&g_win, g->video, TILE_FONT, 0, 0);
}

/* The ending screen (EndingScreen, ps2.asm:10795): ending.c. game.c
 * queued "The Place of Death" before the fade to white that led here, as
 * the routine does before its loc_634E; the screen's own setup follows,
 * with the window system as the text needs it: no box, controls locked
 * (a button cannot hurry the pages' text). */
static void ending_sound(void *ctx, int id)
{
    queue_sound((Game *)ctx, (u8)id);
}

static void enter_ending(Game *g)
{
    g_level_oneshot     = 0;
    g_mother_brain_glow = false;
    win_init(&g_win);
    win_clear_painted(&g_win);
    g_win.ctx             = g;
    g_win.script_lookup   = script_lookup;
    g_win.script_text     = script_text;   /* (build 163) */
    g_win.sound           = intro_sound;
    g_win.gd              = &g_data;
    g_win.character_index = 0;
    g_win.controls_locked = true;

    EndingHost host;
    memset(&host, 0, sizeof host);
    host.video       = g->video;
    host.vram        = &g_vram;
    host.font        = &g_font;
    host.win         = &g_win;
    host.fade_target = &g->fade_target;
    host.queue_sound = ending_sound;
    host.ctx         = g;
    ending_enter(&host);
}

static void tick_ending(Game *g)
{
    if (ending_tick(&g->input)) {
        /* loc_785C's ScreenID_Sega: SegaScreen fades the credits out
         * first (build 157a: they were cut), the planes 64x32 by then. */
        goto_sega(g);
    }
}

static void render_ending(Game *g)
{
    (void)g;
    ending_render();
}

static void render_battle(Game *g)
{
    /* DrawArtTiles at $66000003: plane B from row 12. */
    nametable_draw(g->video, LAYER_B, &g_battle_floor, 0, 12);
    /* (build 148) The floor from its picture. */
    nametable_picture(g->video, LAYER_B, &g_battle_floor,
                      g_battle_floor_picture, 0, 12);
    if (g_battle.formation == FORMATION_MOTHER_BRAIN) {
        /* BattleScreen (ps2.asm:12092): the star field behind Mother
         * Brain, 40x14 across the top of plane B from row 0, drawn after
         * the floor so it covers the floor's first two rows. Its tiles are
         * 21 of her own art's ($8E-$A1, revealed by the dissolve with the
         * rest), its colours line 3 -- the chase runs along entries 8-15,
         * the stars twinkling. (build 127) Her figure is the battle
         * object; this was called her picture before. */
        if (g_mother_brain_picture.width == 0) {
            nametable_load(&g_mother_brain_picture,
                           "assets/nametables/MotherBrainMachineTileInd.tmx");
        }
        nametable_draw(g->video, LAYER_B, &g_mother_brain_picture, 0, 0);
        /* (build 155) From its picture, through the dissolve that brings
         * her art in and game over's that takes it out. */
        nametable_picture_hidden(g->video, LAYER_B, &g_mother_brain_picture,
                                 g_mother_brain_stars, 0, 0,
                                 g_battle.art_hidden);
    }
    battle_render(&g_battle, g->video, &g_vram);
    win_render(&g_win, g->video, TILE_FONT, 0, 0);
}

static void tick_level(Game *g)
{
    if (g_level_walk) {
        tick_level_walk(g);
        return;
    }

    if (!g_level_ready || g->crawl_step >= CRAWL_STEPS) {
        return;
    }

    /* Start returns to the title at any point (LevelScreenLoop tests
     * ButtonStart_Mask while the controls are locked). */
    if (PRESSED(g, start)) {
        goto_screen(g, SCREEN_TITLE);
        return;
    }

    if (g->crawl_hold > 0) {
        g->crawl_hold--;
        if (g->crawl_hold == 0) {
            if (g->crawl_step == CRAWL_STEPS - 1) {
                /* Script $40 has no target: the last message is the end
                 * of the attract loop. */
                goto_sega(g);
            } else {
                g->crawl_close = MSG_FRAMES;
            }
        }
        return;
    }

    if (g->crawl_close > 0) {
        g->crawl_close--;
        return;
    }

    if (crawl_step_camera(g, CRAWL[g->crawl_step].x,
                          CRAWL[g->crawl_step].y)) {
        g->crawl_step++;
        g->crawl_hold = CRAWL_HOLD_FRAMES;
        /* The crawl draws its own box from Script_Opening rather than
         * through LoadScript, so the probes hear about it here. */
        if (g_debug_script_fn != NULL) {
            g_debug_script_fn(g_debug_script_ctx,
                              CRAWL[g->crawl_step].message);
        }
    }
}

/* One script in the message box, drawing only box columns in
 * [first, last), with a border at each end of that span. Open and close
 * both pass a span centred on the box, so the borders walk outward and
 * then back in. */
/* (build 163) A crawl line in a proportional language: the line a run on
 * the box's fill (win_text_draw), interior columns [first + 1, last - 1)
 * of it put down. Its {XX} bytes are glyphs -- his ellipsis is three
 * {47} -- and its other {...} spans are skipped, as the stock line's. */
static void draw_message_run(Game *g, const char *line, size_t len,
                             int base_col, int row, int first, int last)
{
    char text[WIN_RUN_LEN * 4];
    size_t out = 0;
    for (size_t i = 0; i < len && out < sizeof text - 1; i++) {
        bool byte = line[i] == '{' && i + 3 < len && line[i + 3] == '}';
        if (line[i] == '{' && !byte) {
            while (i < len && line[i] != '}') {
                i++;
            }
            continue;
        }
        text[out++] = line[i];
    }
    text[out] = '\0';

    u8  codes[WIN_RUN_LEN];
    int n     = lang_codes(text, codes, WIN_RUN_LEN);
    int cells = MSG_W - 2;
    Tile paper[MSG_W];
    for (int c = 0; c < cells; c++) {
        paper[c] = g_vram.tiles[TILE_FONT + TILE_WIN_FILL];
    }
    int tile = win_text_draw(codes, n, cells, paper);
    if (tile < 0) {
        return;
    }
    for (int c = first + 1; c < last - 1; c++) {
        Cell cell;
        memset(&cell, 0, sizeof cell);
        cell.tile = (u16)(tile + c - 1);
        cell.prio = 1;
        video_set_tile(g->video, LAYER_A, base_col + c, row, cell);
    }
    /* (build 165) A two-row face's marks in the row above (the crawl's
     * lines two rows apart then), where they have ink. */
    bool inked[MSG_W];
    int  marks = lang_tall() ? win_text_draw_marks(codes, n, cells, paper,
                                                   inked) : -1;
    for (int c = first + 1; marks >= 0 && c < last - 1; c++) {
        if (!inked[c - 1]) {
            continue;
        }
        Cell cell;
        memset(&cell, 0, sizeof cell);
        cell.tile = (u16)(marks + c - 1);
        cell.prio = 1;
        video_set_tile(g->video, LAYER_A, base_col + c, row - 1, cell);
    }
}

static void draw_message(Game *g, int entry, int first, int last)
{
    /* (build 166) A change of language drops the crawl's script with the
     * banks: read again, the new language's -- once. */
    if (g_opening_script.count == 0 && !g_opening_tried) {
        g_opening_tried = true;
        char lang[192];
        script_load(&g_opening_script,
                    lang_path("assets/script/Script_Opening.txt", lang,
                              sizeof lang));
    }
    const char *text = script_entry(&g_opening_script, entry);
    if (text == NULL || first >= last) {
        return;
    }

    Font font = { TILE_FONT, GLYPH_BASE, DIGIT_BASE, 0 };
    ui_set_font(&font);

    /* Window positions are relative to the camera, not to the plane:
     * ProcessWindows builds the nametable address from the camera's own
     * tile position (`(F718 & $F8) << 4` plus `(F71A & $1F8) >> 2`) and
     * adds the window's offset to that. */
    int base_col = g->cam_x / TILE_W + MSG_COL;
    int base_row = g->cam_y / TILE_H + MSG_ROW;

    for (int y = 0; y < MSG_H; y++) {
        bool top    = (y == 0);
        bool bottom = (y == MSG_H - 1);

        for (int x = first; x < last; x++) {
            bool left  = (x == first);
            bool right = (x == last - 1);
            int  tile  = TILE_WIN_FILL;

            if (top && left) {
                tile = TILE_WIN_TL;
            } else if (top && right) {
                tile = TILE_WIN_TR;
            } else if (bottom && left) {
                tile = TILE_WIN_BL;
            } else if (bottom && right) {
                tile = TILE_WIN_BR;
            } else if (top) {
                tile = TILE_WIN_TOP;
            } else if (bottom) {
                tile = TILE_WIN_BOTTOM;
            } else if (left) {
                tile = TILE_WIN_LEFT;
            } else if (right) {
                tile = TILE_WIN_RIGHT;
            }

            Cell cell;
            memset(&cell, 0, sizeof cell);
            cell.tile = (u16)(TILE_FONT + tile);
            cell.prio = 1;
            video_set_tile(g->video, LAYER_A, base_col + x, base_row + y,
                           cell);
        }
    }

    /* The text, clipped to interior columns [first + 1, last - 1). (build
     * 165) A two-row face's lines two rows apart, the marks above each. */
    int step = lang_tall() ? 2 : 1;
    int row  = base_row + step;
    const char *line = text;
    while (line != NULL && row < base_row + MSG_H - 1) {
        const char *eol = strchr(line, '\n');
        size_t len = (eol != NULL) ? (size_t)(eol - line) : strlen(line);

        /* `{...}` spans are control codes the extractor kept -- the
         * terminator, and the name and value substitutions LoadScript
         * resolves from party state. None are glyphs; all are skipped. */
        char buf[64];
        size_t out = 0;
        for (size_t i = 0; i < len && out < sizeof buf - 1; i++) {
            if (line[i] == '{') {
                while (i < len && line[i] != '}') {
                    i++;
                }
                continue;
            }
            buf[out++] = line[i];
        }
        buf[out] = '\0';

        if (lang_proportional()) {
            draw_message_run(g, line, len, base_col, row, first, last);
            row += step;
            line = (eol != NULL) ? eol + 1 : NULL;
            continue;
        }

        for (int c = first + 1; c < last - 1 && (size_t)(c - 1) < out;
             c++) {
            char one[2] = { buf[c - 1], '\0' };
            ui_text(g->video, LAYER_A, base_col + c, row, 0, one);
        }

        row++;
        line = (eol != NULL) ? eol + 1 : NULL;
    }
}

/* RenderCharSprites (ps2.asm:26657) copies the current frame's eight
 * tiles to VRAM every field rather than keeping a character's whole set
 * there; BuildSprites then draws Map 12B58, one 2x4 piece hung from the
 * feet: y - $20, x - 8. */
static void render_walker(Game *g, int member, int slot, int frame,
                          int screen_x, int screen_y)
{
    /* loc_118C8: where each character's 72 tiles sit in the block, and
     * the palette line they draw with. Amy and Shir, Hugh and Kain share
     * art and differ by line. */
    static const struct {
        u8 art;
        u8 line;
    } CHAR_ART[GD_CHARS] = {
        { 0, 0 },   /* Rolf  $03FF3000 */
        { 1, 1 },   /* Nei   $23FF3900 */
        { 4, 0 },   /* Rudo  $03FF5400 */
        { 2, 0 },   /* Amy   $03FF4200 */
        { 5, 1 },   /* Hugh  $23FF5D00 */
        { 3, 0 },   /* Anna  $03FF4B00 */
        { 5, 0 },   /* Kain  $03FF5D00 */
        { 2, 1 },   /* Shir  $23FF4200 */
    };
    member %= GD_CHARS;

    int src = CHAR_ART[member].art * CHAR_ART_TILES + walk_frame_tiles(frame);
    int dst = TILE_CHARS + slot * CHAR_FRAME_TILES;

    /* The level loop's VBlank ($10, loc_5B6E) sends the party's frames
     * from $FFD600 to tile $300. (build 157c) GameOverScreen's ($24) sends
     * only the palette and the sprite table: under its erase, after a
     * wipe-out on the map, the party's tiles go with the rest. */
    if (g->screen == SCREEN_LEVEL) {
        for (int i = 0; i < CHAR_FRAME_TILES; i++) {
            if (src + i < g_walk_art.count && dst + i < g_vram.count) {
                g_vram.tiles[dst + i] = g_walk_art.tiles[src + i];
            }
        }
    }

    Sprite s;
    memset(&s, 0, sizeof s);
    s.x      = (s16)(screen_x - 0x80 - 8);
    s.y      = (s16)(screen_y - 0x80 - 0x20);
    s.tile   = (u16)dst;
    s.w      = 2;
    s.h      = 4;
    s.pal    = CHAR_ART[member].line;
    s.flip_h = walk_frame_flipped(frame);

    /* loc_12B36's frames (loc_12B70, loc_12B80 flipped), on Gaira every
     * other frame: a 16x8 piece at y-16 from tile +$22 ahead of the body,
     * the body, a 16x8 piece at y-24 from tile +$20 behind it. Art list 13
     * puts loc_5D12C -- the ring, front and back -- at $320, $328, $330
     * and $338, $20 on from each party slot's art; the pieces take the
     * slot's palette line with its tile word. */
    bool ring = g_gaira_ring && g_data.level_index == LEVEL_GAIRA;
    Sprite r = s;
    r.w = 2;
    r.h = 1;
    if (ring) {
        r.y    = (s16)(screen_y - 0x80 - 0x10);
        r.tile = (u16)(dst + 0x22);
        video_sprite_push(g->video, &r);
    }
    video_sprite_push(g->video, &s);
    if (ring) {
        r.y    = (s16)(screen_y - 0x80 - 0x18);
        r.tile = (u16)(dst + 0x20);
        video_sprite_push(g->video, &r);
    }
}

static void render_level_walk(Game *g)
{
    /* loc_8BEE: plane B scrolls with the main camera ($F71C/$F71E) unless
     * it is a backdrop; plane A with its own ($F718/$F71A), which is the
     * main one except in the dungeons, where it moves at twice the speed
     * over the overhead layer. */
    bool b_drifts = (g_level.flags & LEVEL_FLAG_B_BACKDROP) != 0;
    int bx = b_drifts ? (g_plane_b_x & 0x1FF) : g_walk.cam_x;
    int by = b_drifts ? (g_plane_b_y & 0xFF)  : g_walk.cam_y;

    if (g_scene_no_redraw) {
        /* loc_9002 skipped, and nothing of the battle left either:
         * LevelScreen's ClearSpriteAndScroll (ps2.asm:8490) is a VRAM
         * fill of $4800 bytes from $B800 -- the sprite table, H scroll,
         * plane A ($C000) and plane B ($E000), VDPSetupArray's bases --
         * before loc_7E02 draws the picture on plane A. So both planes
         * are empty but for the picture, over the backdrop (line 0,
         * colour 0: black). Build 78 drew the battle's floor here. */
        Cell blank;
        memset(&blank, 0, sizeof blank);
        video_fill(g->video, LAYER_B, blank);
        video_fill(g->video, LAYER_A, blank);
    } else {
        level_draw_plane(g->video, LAYER_B, &g_level, &g_blocks, bx, by);
        level_draw_plane(g->video, LAYER_A, &g_level, &g_blocks,
                         g_walk.plane_a_x, g_walk.plane_a_y);
    }
    if (g_scene_picture != 0) {
        nametable_draw(g->video, LAYER_A, &g_scene_map[0], SCENE_PICTURE_COL,
                       SCENE_PICTURE_ROW);
        /* (build 148) From their pictures: the face in case 1's palette
         * or case 2's, and case 2's closed eyes over it (bound after, so
         * taken first where they overlap). */
        nametable_picture(g->video, LAYER_A, &g_scene_map[0],
                          (g_scene_picture == 1) ? g_nei_face
                                                 : g_nei_face_starry,
                          SCENE_PICTURE_COL, SCENE_PICTURE_ROW);
        if (g_scene_picture == 2) {
            nametable_draw(g->video, LAYER_A, &g_scene_map[1], BODY_COL,
                           BODY_ROW);
            nametable_picture(g->video, LAYER_A, &g_scene_map[1],
                              g_nei_eyes_closed, BODY_COL, BODY_ROW);
        }
    }
    video_scroll(g->video, LAYER_B, bx + g_scene_shake_bx,
                 by + g_scene_shake_by);
    video_scroll(g->video, LAYER_A, g_walk.plane_a_x + g_scene_shake_ax,
                 g_walk.plane_a_y + g_scene_shake_ay);

    /* RunObject files each object under its screen row and BuildSprites
     * walks the rows bottom-up, so whoever stands lower on screen is
     * drawn in front. The first sprite pushed wins here, so: lowest
     * first, and on the same row the party ahead of the townspeople and
     * the leader ahead of his followers, which is object-slot order. */
    enum { DRAW_MAX = 1 + WALK_FOLLOWERS + NPC_MAX };
    int order[DRAW_MAX];
    int ys[DRAW_MAX];
    /* RenderCharSprites draws nobody while the party rides (d3 = 0).
     * (build 182) Otherwise slot i -- the leader's, then each
     * follower's -- draws line[i], the party with anyone at 0 HP passed
     * over, and the slots past the living are hidden. */
    int line[1 + WALK_FOLLOWERS];
    int party = 0;
    if (g_ending_cast_placed) {
        /* The six standing on the bridge have the followers' slots. */
        line[0] = g_ending_leader;
        party   = (g_ending_leader >= 0) ? 1 : 0;
    } else if (!g_party_gone && !g_walk.riding) {
        int ids[GD_PARTY_MAX];
        int n = 0;
        for (int i = 0; i <= g_data.party_members_num && i < GD_PARTY_MAX;
             i++) {
            ids[n++] = g_data.party_member_id[i];
        }
        party = line_pack(ids, n, 1 + g_walk.follower_count, line);
    }
    int count = party + g_npcs.count;

    for (int i = 0; i < count; i++) {
        order[i] = i;
        if (i == 0) {
            ys[i] = g_walk.screen_y;
        } else if (i < party) {
            ys[i] = g_walk.follower[i - 1].screen_y;
        } else {
            const Npc *n = &g_npcs.npc[i - party];
            /* 2(a0) bit 5: ahead of every row (the burst). */
            ys[i] = ((n->scene != NULL && n->scene->front) || n->front)
                  ? 0x7FFF : n->screen_y;
        }
    }
    for (int i = 1; i < count; i++) {
        for (int j = i; j > 0 && ys[order[j]] > ys[order[j - 1]]; j--) {
            int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    }

    for (int n = 0; n < count; n++) {
        int i = order[n];

        if (i >= party) {
            npc_push_sprite(&g_npcs.npc[i - party], g->video);
        } else if (i == 0) {
            render_walker(g, line[0], 0, g_walk.frame, g_walk.screen_x,
                          g_walk.screen_y);
        } else {
            const WalkFollower *f = &g_walk.follower[i - 1];
            render_walker(g, line[i], i, f->frame, f->screen_x,
                          f->screen_y);
        }
    }
    if (g_ending_cast_placed) {
        for (int i = 0; i < ENDING_CAST; i++) {
            const EndingCastMember *m = &g_ending_cast[i];
            if (m->member < 0) {
                continue;
            }
            /* World to sprite space as update_screen_pos does it. */
            render_walker(g, m->member, 1 + i, FACE_UP,
                          m->x - g_walk.cam_x + 0x80,
                          m->y - g_walk.cam_y + 0x80);
        }
    }

    /* Windows sit on plane A at a fixed place on screen, which on a
     * scrolled plane means relative to the camera's tile -- plane A's
     * camera, $F718/$F71A (ProcessWindows, 13522). A window only opens
     * with the main camera on a 16px boundary (camera_off_grid, build
     * 133), and plane A's has then moved twice as far from 0,0, so it
     * is on one too. */
    win_render(&g_win, g->video, TILE_FONT, g_walk.plane_a_x / 8,
               g_walk.plane_a_y / 8);
}

static void render_level(Game *g)
{
    if (!g_level_ready) {
        return;
    }
    if (g_level_walk) {
        render_level_walk(g);
        return;
    }

    /* Both planes take the same camera here. They are separate in the
     * original ($FFFFF718/$FFFFF71A for plane A, $FFFFF71C/$FFFFF71E for
     * plane B) and can move at different rates when $FFFFF73C is set,
     * but the Motavia entry's extra-art byte is $0A with bit 7 clear, so
     * there is no parallax on this level. */
    level_draw_plane(g->video, LAYER_B, &g_level, &g_blocks,
                     g->cam_x, g->cam_y);
    level_draw_plane(g->video, LAYER_A, &g_level, &g_blocks,
                     g->cam_x, g->cam_y);

    video_scroll(g->video, LAYER_B, g->cam_x, g->cam_y);
    video_scroll(g->video, LAYER_A, g->cam_x, g->cam_y);

    if (g->crawl_step < CRAWL_STEPS) {
        int entry = CRAWL[g->crawl_step].message & 0xFF;

        int interior = -1;
        if (g->crawl_hold > 0) {
            /* Opening: two interior columns per frame from zero. */
            interior = (CRAWL_HOLD_FRAMES - g->crawl_hold) * MSG_STEP;
            if (interior > MSG_W - 2) {
                interior = MSG_W - 2;
            }
        } else if (g->crawl_close > 0) {
            /* Closing: two fewer per frame, down to the bare borders. */
            interior = (MSG_W - 2) - (MSG_FRAMES - g->crawl_close) * MSG_STEP;
            if (interior < 0) {
                interior = 0;
            }
        }

        if (interior >= 0) {
            /* Centred: the visible span's interior sits in the middle of
             * the full box, and the borders sit just outside it. */
            int first = (MSG_W - 2 - interior) / 2;
            draw_message(g, entry, first, first + interior + 2);
        }
    }
}

static void enter_screen(Game *g)
{
    /* Every screen routine opens with ClearSpriteAndScroll
     * (ps2.asm:5D90), which zeroes all four scroll registers and empties
     * the sprite table. Without it the crawl's last camera position was
     * still in effect on the SEGA and title screens: both planes stayed
     * offset, content near column 0 wrapped around to the right edge
     * while content further right did not, and sprites -- which do not
     * scroll at all -- stayed put, which is why the menu, the portrait
     * and the indicator lamps each appeared to move by a different
     * amount. */
    for (int layer = 0; layer < LAYER_COUNT; layer++) {
        video_scroll(g->video, (LayerId)layer, 0, 0);
    }
    video_sprites_begin(g->video);
    /* 64x32, the scroll size every screen but the ending's keeps (it
     * sets 64x64 in its own setup and puts this back at its end). */
    video_set_plane_rows(g->video, PLANE_ROWS);

    switch (g->screen) {
        case SCREEN_SEGA:
            enter_sega(g);
            break;
        case SCREEN_TITLE:
            enter_title(g);
            break;
        case SCREEN_INTRO:
            enter_intro(g);
            break;
        case SCREEN_LEVEL:
            enter_level(g);
            break;
        case SCREEN_BUILDING:
            enter_building(g);
            break;
        case SCREEN_BATTLE:
            enter_battle(g);
            break;
        case SCREEN_GAMEOVER:
            enter_gameover(g);
            break;
        case SCREEN_ENDING:
            enter_ending(g);
            break;
        default:
            break;
    }
    g->screen_entered = true;

    /* The screen has loaded its palette; fade it up rather than cut. The
     * battle and the ending come in from white instead and run their own
     * fades. */
    if (g->screen == SCREEN_LEVEL && g_level_through_white) {
        g_level_through_white = false;
        begin_fade_in_white(g);
    } else if (g->screen != SCREEN_BATTLE && g->screen != SCREEN_GAMEOVER
               && g->screen != SCREEN_ENDING) {
        begin_fade_in(g);
    }
}

/* Leaving a screen darkens the palette over 21 frames before the next
 * screen loads, which is the wipe the original shows between screens. */
static void goto_screen(Game *g, ScreenId next)
{
    g->pending_screen = next;
    g->fade           = FADE_OUT;
    g->fade_frames    = FADE_FRAMES;
}

/* (build 157a) The game's way back to the SEGA screen: SegaScreen
 * (ps2.asm:10591) sends $E0, the music's fade out, then PaletteFadeFrom
 * darkens whatever was on screen, so the music and the picture go down
 * together. Until 157a the $E0 went in enter_sega, after the fade, 21
 * frames late; and the ending's last button and game over's end set the
 * screen with no fade at all, so the logo's setup cut in at once. */
static void goto_sega(Game *g)
{
    queue_sound(g, SOUND_FADE_OUT);
    g_sega_fade_sent = true;
    goto_screen(g, SCREEN_SEGA);
}

/* loc_634E: 21 frames into white, one channel of every entry a frame --
 * red until it reads $E, then green, then blue (loc_6382). */
static void goto_screen_white(Game *g, ScreenId next)
{
    g->pending_screen = next;
    g->fade           = FADE_OUT_WHITE;
    g->fade_frames    = FADE_FRAMES;
}

static void fade_to_white_step(Game *g)
{
    video_fade_step(g->video, VIDEO_FX_TO_WHITE, NULL);
}

/* One step of PaletteFadeOut (PaletteDecreaseColor): one channel of each
 * entry down a level, red first (build 138d; every channel at once
 * before). */
static void fade_out_step(Game *g)
{
    video_fade_step(g->video, VIDEO_FX_TO_BLACK, NULL);
}

/* PaletteFadeTo zeroes the palette then walks it up to the target, one
 * channel of each entry a frame, blue first (PaletteAddColor; build 138d). */
static void fade_in_step(Game *g)
{
    video_fade_step(g->video, VIDEO_FX_FROM_BLACK, &g->fade_target);
}

/* (build 138d) loc_62CC: the loaded palette kept as the target, every
 * entry white, then 21 frames down to it (palette_fade_from_white_step). */
static void begin_fade_in_white(Game *g)
{
    video_target_from_shown(g->video, &g->fade_target);
    video_fade_begin(g->video, VIDEO_FX_FROM_WHITE, VIDEO_LINES_ALL);
    g->fade        = FADE_IN_WHITE;
    g->fade_frames = FADE_FRAMES;
}

static void begin_fade_in(Game *g)
{
    video_target_from_shown(g->video, &g->fade_target);
    video_fade_begin(g->video, VIDEO_FX_FROM_BLACK, VIDEO_LINES_ALL);
    g->fade        = FADE_IN;
    g->fade_frames = FADE_FRAMES;
}

/* Returns true while a fade is running, so screen logic stays paused. */
static bool fade_update(Game *g)
{
    if (g->fade == FADE_NONE) {
        return false;
    }

    if (g->fade == FADE_OUT || g->fade == FADE_OUT_WHITE) {
        if (g->fade == FADE_OUT_WHITE) {
            fade_to_white_step(g);
        } else {
            fade_out_step(g);
        }
        if (--g->fade_frames <= 0) {
            g->screen         = g->pending_screen;
            g->screen_entered = false;
            g->fade           = FADE_NONE;
        }
        return true;
    }

    if (g->fade == FADE_IN_WHITE) {
        video_fade_step(g->video, VIDEO_FX_FROM_WHITE, &g->fade_target);
    } else {
        fade_in_step(g);
    }
    if (--g->fade_frames <= 0) {
        g->fade = FADE_NONE;
    }
    return true;
}

/* ---- per-frame logic, the body of each routine's wait loop ---- */

/* PaletteCycle, ps2.asm:9226. A sliding window over CyclingPal_SegaLogo
 * (assets/palettes/sega_logo_sweep.png, the ramp as a picture)
 * rather than a rotation: the offset walks down by one word every four
 * frames, and the sweep stops once it passes zero. */
static void palette_cycle_sega(Game *g)
{
    if (--g->pal_cycle_gate >= 0) {
        return;
    }
    g->pal_cycle_gate = PAL_CYCLE_GATE;

    if (g->pal_cycle_offset < 0) {
        return;
    }

    int word = g->pal_cycle_offset / 2;
    for (int i = 0; i < PAL_CYCLE_WORDS; i++) {
        int from = word + i;
        if (from >= g_cycling_pal.count) {
            break;
        }
        video_glow(g->video, PAL_CYCLE_FIRST + i, g_cycling_pal.value[from]);
    }

    g->pal_cycle_offset -= 2;
}

static void tick_sega(Game *g)
{
    palette_cycle_sega(g);

    if (g->demo_timer > 0) {
        g->demo_timer--;
    }
    if (g->demo_timer == 0 || PRESSED(g, start)) {
        goto_screen(g, SCREEN_TITLE);
    }
}

/* One step of the ordered dissolve FadeArtTiles performs: reveal the
 * pixel position FadeEffectArray names, across the whole tile range.
 * (build 154) The figures and the logo are the title's picture: the step
 * is the range's reveal mask's, `hidden`, no tile. */
static bool fade_step(Game *g, u64 *hidden)
{
    if (--g->fade_gate > 0) {
        return false;
    }
    g->fade_gate = FADE_GATE;

    if (g->fade_step < g_fade_order.count) {
        *hidden &= ~((u64)1 << g_fade_order.value[g->fade_step]);
    }
    g->fade_step++;

    return g->fade_step >= FADE_STEPS;
}

static void begin_fade(Game *g, TitlePhase phase, u64 *hidden)
{
    /* The destination starts blank and the source bleeds into it: the
     * range's mask all hidden (build 154: no tiles are written). */
    *hidden        = DISSOLVE_ALL_HIDDEN;
    g->title_phase = phase;
    g->fade_step   = 0;
    g->fade_gate   = FADE_GATE;
}

/* Obj_PushStartButton (ps2.asm:8074). Its Init routine sits with the
 * render flag at $12 -- hidden -- until any button on the pad is held,
 * and only then moves to Main. So the prompt is not on the title screen
 * at all until the player touches something, which is the original's
 * own quirk; the disassembly even carries a comment on how to remove
 * it. Main then blinks it: visible while the timer is above 16, hidden
 * from 16 down to 0, reset to 40 when it goes negative. 24 fields on, 17
 * off. */
#define PUSH_START_PERIOD 0x28
#define PUSH_START_OFF    0x10

static void tick_push_start(Game *g)
{
    if (!g->push_start_armed) {
        g->push_start_timer = 0x27;
        if (ANY_HELD(g)) {
            g->push_start_armed = true;
        }
        return;
    }

    g->push_start_timer--;
    if (g->push_start_timer < 0) {
        g->push_start_timer = PUSH_START_PERIOD;
    }
}

static bool push_start_visible(const Game *g)
{
    return g->push_start_armed && g->push_start_timer > PUSH_START_OFF;
}

static void tick_title(Game *g)
{
    /* Start leaves the title at any point in the sequence. */
    if (PRESSED(g, start)) {
        g_intro_dream = false;      /* building_index = 0 */
        goto_screen(g, SCREEN_INTRO);
        return;
    }

    /* demo_timer is decremented by the VBlank handler (VBlankSub_04_08),
     * not by the screen routine, so it keeps counting through the two
     * dissolves. $6CC is loaded before them and TitleScreenLoop only
     * runs out what is left -- about 1228 frames, not 1740. Counting it
     * down only in the wait phase added the dissolves' 512 frames on top
     * and put the Motavia opening eight seconds late. */
    if (g->demo_timer > 0) {
        g->demo_timer--;
    }

    tick_push_start(g);

    switch (g->title_phase) {
        case TITLE_BG:
            if (g->demo_timer == 0) {
                g->demo_timer = TITLE_WAIT_TIME;
                begin_fade(g, TITLE_FADE_FIGURES, &g_figures_hidden);
            }
            break;

        case TITLE_FADE_FIGURES:
            if (fade_step(g, &g_figures_hidden)) {
                begin_fade(g, TITLE_FADE_LOGO, &g_logo_hidden);
            }
            break;

        case TITLE_FADE_LOGO:
            if (fade_step(g, &g_logo_hidden)) {
                g->title_phase = TITLE_WAIT;
            }
            break;

        case TITLE_WAIT:
            if (g->demo_timer == 0) {
                g_level_walk = false;       /* controls_locked = 1 */
                goto_screen(g, SCREEN_LEVEL);
            }
            break;

        default:
            break;
    }
}

/* loc_6CA4, one step of the dissolve out. Where FadeArtFill ORs one
 * pixel in, this ANDs a byte away: FadeEffectArray's entry picks the word
 * (entry >> 2) and bit 1 picks which half of it, so two pixels go at once
 * and bit 0 is ignored. (build 151) What it dissolves out -- the dream's
 * fight, Tyler's help -- are pictures: the byte goes from the reveal mask,
 * no tile. */
static void dream_erase_step(int order_index)
{
    if (order_index >= g_fade_order.count) {
        return;
    }
    int pixel = g_fade_order.value[order_index] & ~1;
    g_dissolve_hidden |= (u64)3 << pixel;
}

/* Line 3 of the live palette zeroed (loc_D572, loc_D59C): the dream's
 * picture on it black, the start of its own fade in. */
static void dream_cut_line(Game *g)
{
    video_fade_begin(g->video, VIDEO_FX_FROM_BLACK,
                     (u8)(1u << DREAM_LINE));
}

static void dream_effect_begin(Game *g, DreamEffect fx)
{
    g_fx_step = 0;
    g_fx_gate = FADE_GATE;

    if (fx == DREAM_FX_DISSOLVE_IN) {
        g_dissolve_hidden = DISSOLVE_ALL_HIDDEN;
    }
    if (fx == DREAM_FX_PALETTE_IN) {
        /* loc_D59C: the map and the art go up in one go under a blacked
         * out line 3, palette $30 loads into the target, and loc_61F4
         * walks the line up to it. (build 150) The waking is its picture
         * (render_intro): loc_6FF22 at tile 1 and $30 are not loaded, and
         * the line's fade in brings the picture up. */
        video_target_from_shown(g->video, &g_fx_target);
        dream_cut_line(g);
    }
}

/* One frame of whichever effect is running; true when it has finished.
 * The original spins in WaitForVBlank for these, so nothing else on the
 * screen advances while one runs. */
static bool dream_effect_step(Game *g, DreamEffect fx)
{
    switch (fx) {
        case DREAM_FX_DISSOLVE_IN: {
            /* loc_6BBE: four frames, then a pixel, sixty-four times. */
            if (--g_fx_gate > 0) {
                return false;
            }
            g_fx_gate = FADE_GATE;
            /* (build 151) The fight is its picture: the pixel goes
             * into the reveal mask, not the tiles. */
            if (g_fx_step < g_fade_order.count) {
                g_dissolve_hidden &=
                    ~((u64)1 << g_fade_order.value[g_fx_step]);
            }
            g_fx_step++;
            return g_fx_step >= FADE_STEPS;
        }
        case DREAM_FX_DISSOLVE_OUT: {
            /* loc_6C54: a step every frame, thirty-two of them, then
             * loc_D572 zeroes line 3 of the live palette. */
            dream_erase_step(g_fx_step);
            g_fx_step++;
            if (g_fx_step < DREAM_OUT_STEPS) {
                return false;
            }
            dream_cut_line(g);
            return true;
        }
        case DREAM_FX_PALETTE_IN: {
            video_fade_step(g->video, VIDEO_FX_FROM_BLACK, &g_fx_target);
            g_fx_step++;
            return g_fx_step >= DREAM_PAL_FRAMES;
        }
        default: {
            return true;
        }
    }
}

static void tick_intro_dream(Game *g)
{
    switch (intro_start_tick(&g_start, &g_win)) {
        case INTRO_START_EFFECT: {
            DreamEffect fx = intro_start_effect(&g_start);
            if (g_fx_step < 0) {
                dream_effect_begin(g, fx);
            }
            if (!dream_effect_step(g, fx)) {
                return;
            }
            /* The state queues its script and returns into the loop, so
             * CheckPrepareWindows loads it this same frame. Returning
             * early here would leave one frame where nothing is pending
             * and the next state would fire at once. */
            g_fx_step = -1;
            intro_start_effect_done(&g_start, &g_win);
            break;
        }
        case INTRO_START_TO_LEVEL: {
            /* loc_D5EA: screen_changed_flag = -1 sends IntroScreen's
             * exit to ScreenID_Level, with demo_flag set and the building
             * to enter at the end of the walk already chosen. */
            g_intro_dream       = false;
            g_level_walk        = true;
            g_bstate.building   = BUILDING_GOVERNOR;
            g_bstate.portrait   = PORTRAIT_GOVERNOR;
            g_bstate.demo_flag  = true;
            g_bstate.demo_index = DEMO_INDEX_HOUSE_TO_TOWER;
            goto_screen(g, SCREEN_LEVEL);
            return;
        }
        default: {
            break;
        }
    }

    win_prepare(&g_win, &g->input);
    win_process(&g_win);
}

/* loc_D3B6: g_data holds the loaded save. Data Memory, with $F764 = 1
 * ("We meet again") and $F766 from $C65C; the Dezolian clerk when the
 * save was made on Dezolis at a Motavian-style station. */
static void enter_continue(Game *g)
{
    memset(&g_bstate, 0, sizeof g_bstate);
    /* Data Memory's welcome with $F764 set (ps2.asm:16596): loc_F550,
     * the battle commands back to CharStartCommandsArray -- worked out
     * against the loaded party, not the new-game one the select screen
     * set up. */
    battle_reset_commands(&g_battle, &g_data, true);
    g_bstate.building    = BUILDING_DATA_MEMORY;
    g_bstate.portrait    = 9;
    g_bstate.store_index = gamedata_word(&g_data, GD_LAST_SAVE_VISIPHONE);
    if (g_bstate.store_index == 0
        && gamedata_word(&g_data, GD_LAST_SAVE_PLANET) != 0) {
        g_bstate.portrait = 0x10;
    }
    g_bstate.store_max = 1;
    g_level_walk       = true;
    goto_screen(g, SCREEN_BUILDING);
}

/* IntroScreenLoop: RunObjects, IntroScr_CheckRoutine,
 * CheckPrepareWindows, then ProcessWindows from the VBlank handler. */
static void tick_intro(Game *g)
{
    win_run_cursors(&g_win, &g->input);

    if (g_intro_dream) {
        tick_intro_dream(g);
        return;
    }

    switch (intro_select_tick(&g_select, &g_win, &g_data)) {
        case INTRO_SELECT_TO_TITLE: {
            goto_screen(g, SCREEN_TITLE);
            return;
        }
        case INTRO_SELECT_NEW_GAME: {
            /* loc_D384: building_index = 1, screen_changed_flag = 1. The
             * main loop calls IntroScreen again, fade and all. */
            g_intro_dream = true;
            g_fx_step     = -1;
            goto_screen(g, SCREEN_INTRO);
            return;
        }
        case INTRO_SELECT_CONTINUE: {
            enter_continue(g);
            return;
        }
        default: {
            break;
        }
    }

    win_prepare(&g_win, &g->input);
    win_process(&g_win);
}

/* PS2_BUILDING: straight into a building from a new game with Rolf and
 * Nei, as a door would send you. For checking one against the emulator
 * without walking there. Leaving puts you on Paseo at the 16px cell
 * (cell_x, cell_y), or its start position when those are negative. */
GameData *game_debug_data(Game *g)
{
    (void)g;
    return &g_data;
}

WinSys *game_debug_win(Game *g)
{
    (void)g;
    return &g_win;
}

void game_debug_building(Game *g, int building, int portrait,
                         int store_max, int store_index,
                         int cell_x, int cell_y)
{
    gamedata_new_game(&g_data);
    if (cell_x >= 0 && cell_y >= 0) {
        g_data.level_x = (u16)(cell_x * 16);
        g_data.level_y = (u16)(cell_y * 16);
    }
    g_data.party_members_num    = 1;
    g_data.party_members_joined = 1;
    g_data.party_member_id[0]   = 0;
    g_data.party_member_id[1]   = 1;

    memset(&g_bstate, 0, sizeof g_bstate);
    g_bstate.building    = building;
    g_bstate.portrait    = portrait;
    g_bstate.store_max   = store_max;
    g_bstate.store_index = store_index;

    g_level_walk      = true;
    g->screen         = SCREEN_BUILDING;
    g->screen_entered = false;
    g->fade           = FADE_NONE;
}

/* PS2_LEVEL=level,cx,cy: a new game standing on that cell of that level,
 * Rolf and Nei, no flash. For checking encounters. */
void game_debug_level(Game *g, int level, int cell_x, int cell_y)
{
    gamedata_new_game(&g_data);
    g_data.level_index          = (u16)level;
    g_data.level_x              = (u16)(cell_x * 16);
    g_data.level_y              = (u16)(cell_y * 16);
    g_data.party_members_num    = 1;
    g_data.party_members_joined = 1;
    g_data.party_member_id[0]   = 0;
    g_data.party_member_id[1]   = 1;
    /* The level-4 doors need the Governor flag or the town is the
     * pre-Governor one; either is fine for a walk. */
    g_level_walk      = true;
    g->screen         = SCREEN_LEVEL;
    g->screen_entered = false;
    g->fade           = FADE_NONE;
}

void game_debug_enemy_hp(Game *g, int hp)
{
    (void)g;
    for (int e = 0; e < 8; e++) {
        if (g_battle.obj[BOBJ_ENEMIES + 2 * e].id != 0) {
            g_battle.stat[e].hp = (u16)hp;
        }
    }
}

void game_debug_party(Game *g, int members, int level)
{
    if (members < 1) {
        members = 1;
    }
    if (members > GD_PARTY_MAX) {
        members = GD_PARTY_MAX;
    }
    g_data.party_members_num    = (u16)(members - 1);
    g_data.party_members_joined = (u16)(members - 1);
    for (int i = 0; i < members; i++) {
        g_data.party_member_id[i] = (u16)i;
    }
    for (int c = 0; c < members; c++) {
        CharData *ch = &g_data.chr[c];
        gamedata_equip_bonuses_remove(&g_data, c);
        while (ch->level < level && ch->level < 0x32) {
            LevelRow row;
            if (!gamedata_level_row(c, ch->level, &row)) {
                break;
            }
            u32 need = gamedata_level_exp(c, ch->level) & 0xFFFFFF;
            if (ch->experience < need) {
                ch->experience = need;
            }
            bool hp_up, tp_up, techs_up;
            battle_apply_level_row(&g_data, c, &row, &g->rng, &hp_up, &tp_up,
                                   &techs_up);
        }
        gamedata_equip_bonuses_add(&g_data, c);
        ch->hp = ch->max_hp;
        ch->tp = ch->max_tp;
    }
}

/* The party in this order: ids[0..n-1] into party_member_id, the rest
 * zeroed, as RolfHouseOption_Reorganize leaves it. Everyone named counts
 * as joined. For trials: the house itself puts Nei in slot 1 while $C737
 * is clear, and the caller is trusted to do the same. */
void game_debug_members(Game *g, const int *ids, int n)
{
    (void)g;
    if (n < 1) {
        return;
    }
    if (n > GD_PARTY_MAX) {
        n = GD_PARTY_MAX;
    }
    for (int i = 0; i < GD_PARTY_MAX; i++) {
        g_data.party_member_id[i] = (i < n) ? (u16)(ids[i] & 7) : 0;
    }
    g_data.party_members_num = (u16)(n - 1);
    for (int i = 0; i < n; i++) {
        if (g_data.party_members_joined < (u16)(ids[i] & 7)) {
            g_data.party_members_joined = (u16)(ids[i] & 7);
        }
    }
}

/* One character up to `level` by the real rows (battle_apply_level_row,
 * the victory's Character_ProcessLevelUp), with its equipment's bonuses
 * taken off and put back around it as the menus do. Never lowers. */
void game_debug_member_level(Game *g, int id, int level)
{
    int c = id & 7;
    CharData *ch = &g_data.chr[c];
    gamedata_equip_bonuses_remove(&g_data, c);
    while (ch->level < level && ch->level < 0x32) {
        LevelRow row;
        if (!gamedata_level_row(c, ch->level, &row)) {
            break;
        }
        u32 need = gamedata_level_exp(c, ch->level) & 0xFFFFFF;
        if (ch->experience < need) {
            ch->experience = need;
        }
        bool hp_up, tp_up, techs_up;
        battle_apply_level_row(&g_data, c, &row, &g->rng, &hp_up, &tp_up,
                               &techs_up);
    }
    gamedata_equip_bonuses_add(&g_data, c);
    ch->hp = ch->max_hp;
    ch->tp = ch->max_tp;
}

/* Equipment by item id per slot (0 = none): the old equipped entries
 * out of the bag, the new ones in with bit 7 set, the bonuses taken off
 * and put back (loc_ADBE / loc_AD4C). False when the bag has no room. */
bool game_debug_outfit(Game *g, int id, const int item[EQUIP_COUNT])
{
    (void)g;
    int c = id & 7;
    CharData *ch = &g_data.chr[c];
    gamedata_equip_bonuses_remove(&g_data, c);
    int keep = 0;
    for (int i = 0; i < ch->item_count && i < GD_ITEM_SLOTS; i++) {
        if ((ch->items[i] & 0x80) == 0) {
            ch->items[keep++] = ch->items[i];
        }
    }
    bool ok = true;
    for (int s = 0; s < EQUIP_COUNT; s++) {
        ch->equip[s] = (u8)(item[s] & 0x7F);
        if (item[s] == 0) {
            continue;
        }
        /* A two-handed weapon is listed in both hands, one bag entry. */
        if (s == EQUIP_LEFT && item[s] == item[EQUIP_RIGHT]
            && (gamedata_item_action_flags(item[s]) & 7) == 3) {
            continue;
        }
        if (keep < GD_ITEM_SLOTS) {
            ch->items[keep++] = (u8)(0x80 | (item[s] & 0x7F));
        } else {
            ok = false;
        }
    }
    ch->item_count = (u8)keep;
    gamedata_equip_bonuses_add(&g_data, c);
    return ok;
}

/* To a level and cell with the game as it is -- a door, not a new game:
 * off the scooter, the level's music as it comes. */
/* (build 97d) The event word's high byte for the next level load:
 * loc_7E02's case, as a `move.w #$XX00, (event_flags).w` just before a
 * reload would leave it. */
void game_debug_oneshot(Game *g, int scene)
{
    (void)g;
    g_level_oneshot = (u8)scene;
}

void game_debug_warp(Game *g, int level, int cell_x, int cell_y)
{
    g_data.level_index = (u16)level;
    g_data.level_x     = (u16)(cell_x * 16);
    g_data.level_y     = (u16)(cell_y * 16);
    g_data.jet_scooter = 0;
    g_level_walk       = true;
    g->screen          = SCREEN_LEVEL;
    g->screen_entered  = false;
    g->fade            = FADE_NONE;
}

/* PS2_BATTLE=formation: straight into that formation from a new game. */
void game_debug_battle(Game *g, int formation)
{
    game_debug_level(g, 0, 40, 40);
    g_battle_formation = formation;
    g->screen          = SCREEN_BATTLE;
}

void game_debug_reset(Game *g)
{
    rng_init(&g->rng, RNG_POWER_ON_SEED);
    g->sysmenu.open   = false;
    g->screen         = SCREEN_SEGA;
    g->screen_entered = false;
    g->fade           = FADE_NONE;
    g_sound_wait      = 0;
    g_sega_fade_sent  = false;      /* power-on: enter_sega sends $E0 */
    g_story_words     = 0;          /* (build 162) game_story_beat's */
    memset(g_story_seen, 0, sizeof g_story_seen);
}

bool game_debug_continue(Game *g, int slot)
{
    gamedata_new_game(&g_data);
    if (!gamedata_load(&g_data, slot - 1)) {
        return false;
    }
    battle_tables_load();
    enter_continue(g);
    g->screen         = SCREEN_BUILDING;
    g->screen_entered = false;
    g->fade           = FADE_NONE;
    return true;
}

void game_debug_on_script(Game *g, void (*fn)(void *ctx, u16 id), void *ctx)
{
    (void)g;
    g_debug_script_fn  = fn;
    g_debug_script_ctx = ctx;
}

/* The counters event code steps on. They stand still, with the windows
 * idle, only while it waits on something outside itself. */
static u32 debug_event_sig(void)
{
    u32 h = 2166136261u;
    const u32 part[] = {
        g_win.event_routine, g_win.event_routine_sub,
        g_win.event_routine_sub_2, g_win.event_step, g_win.event_done,
        (u32)g_win.depth, (u32)g_win.painted_count,
        (u16)g_menu.timer, (u16)g_bstate.timer, (u16)g_people_timer,
        /* loc_CC3E's demo_timer and the monitor's frame wait ($F766):
         * Gaira's control panel and Tyler's spaceship count them with
         * no window asking for the pad. */
        g_bstate.demo_timer, (u16)g_bstate.store_index,
        (u16)g_bstate.store_max,
        /* A building's own fade (the panel's loc_CC10, the lesson) runs
         * with no window up and the counters still. */
        (u32)g_lesson_phase, (u32)g_lesson_step,
        /* Tyler's dissolves and the monitor's explosion (build 82). */
        (u32)g_bstate.fx, (u32)g_tyler_fx_step, (u32)g_tyler_fx_gate,
        (u32)g_monitor_expl.live, (u16)g_monitor_expl.timer,
        (u32)g_monitor_expl.frame
    };
    for (size_t i = 0; i < sizeof part / sizeof part[0]; i++) {
        h = (h ^ part[i]) * 16777619u;
    }
    return h;
}

static bool debug_windows_quiet(void)
{
    return win_idle(&g_win) && g_win.steps == 0 && g_win.script_id[0] == 0;
}

static void debug_track(void)
{
    /* Stillness only counts on ticks the event code had the chance to
     * run: through a fade nothing moves because nothing runs. The pause
     * holds it off too (pause_gate), and is a wait of its own. */
    u32 sig = debug_event_sig();
    if (g_debug_logic_ran && !g_paused && !g_pause_release
        && debug_windows_quiet() && sig == g_debug_event_sig) {
        g_debug_event_still++;
    } else {
        g_debug_event_still = 0;
    }
    g_debug_event_sig = sig;
}

/* The window system's own waits: a poll holding the pad, a message at
 * its stop, or the battle's party triangle. NONE with *busy set when
 * the windows are doing something; NONE with *busy NULL when they are
 * quiet and it is up to the screen. */
static GameWait debug_window_wait(const char **busy)
{
    *busy = NULL;
    if (g_win.steps != 0) {
        *busy = "a window opening or closing";
        return GAME_WAIT_NONE;
    }
    if (g_win.window_index[0] != 0) {
        *busy = "windows queued";
        return GAME_WAIT_NONE;
    }
    if (g_win.window_index_saved != 0) {
        if (g_win.window_routine >= 2) {
            return GAME_WAIT_MENU;
        }
        *busy = "a window setting up";
        return GAME_WAIT_NONE;
    }
    if (g_win.window_active_flag != 0) {
        u8 code = g_win.text[g_win.text_pos];
        if (code == TXT_END || code == TXT_PAUSE) {
            return GAME_WAIT_TEXT;
        }
        *busy = (code >= TXT_END_NOW) ? "a timed message" : "text printing";
        return GAME_WAIT_NONE;
    }
    if (g_win.script_id[0] != 0) {
        *busy = "a script queued";
        return GAME_WAIT_NONE;
    }
    for (int i = 0; i < 2; i++) {
        if (g_win.tri[i].kind == TRI_CHAR_SELECT
            && g_win.tri[i].slot == g_win.depth + 1) {
            return GAME_WAIT_MENU;
        }
    }
    return GAME_WAIT_NONE;
}

static GameWait debug_event_wait(const char **busy, const char *what)
{
    if (g_debug_event_still >= 2) {
        return GAME_WAIT_EVENT;
    }
    *busy = what;
    return GAME_WAIT_NONE;
}

static GameWait debug_wait(const Game *g, const char **busy)
{
    GameWait w;

    if (g->sysmenu.open) {
        *busy = "the Escape menu";
        return GAME_WAIT_NONE;
    }
    if (!g->screen_entered || g->fade != FADE_NONE) {
        *busy = "a screen fade";
        return GAME_WAIT_NONE;
    }

    switch (g->screen) {
        case SCREEN_SEGA: {
            *busy = "the SEGA screen";
            return GAME_WAIT_NONE;
        }
        case SCREEN_TITLE: {
            if (g->title_phase == TITLE_WAIT) {
                return GAME_WAIT_TITLE;
            }
            *busy = "the title dissolving in";
            return GAME_WAIT_NONE;
        }
        case SCREEN_INTRO: {
            if (g_intro_dream && g_fx_step >= 0) {
                *busy = "a dream effect";
                return GAME_WAIT_NONE;
            }
            w = debug_window_wait(busy);
            if (w == GAME_WAIT_NONE && *busy == NULL) {
                *busy = "intro event code";
            }
            return w;
        }
        case SCREEN_LEVEL: {
            if (!g_level_walk) {
                *busy = "the attract crawl";
                return GAME_WAIT_NONE;
            }
            if (g_paused) {
                return GAME_WAIT_PAUSED;
            }
            if (g_floor_stall > 0) {
                *busy = "a damage floor";
                return GAME_WAIT_NONE;
            }
            w = debug_window_wait(busy);
            if (w != GAME_WAIT_NONE || *busy != NULL) {
                return w;
            }
            if ((g_level_event >> 8) == (EVENT_KIND_PEOPLE >> 8) && g_win.event_step >= 5) {
                /* the cake's last step reads the pad itself */
                return debug_event_wait(busy, "a level event");
            }
            if (g_level_event != 0 || g_win.event_step != 0) {
                *busy = "a level event";
                return GAME_WAIT_NONE;
            }
            /* Camera case 6 (loc_815C), the Gaira prison's countdown,
             * leaves the leader under the pad: ObjCL_Main tests only
             * event_flags' high byte and the windows. */
            if ((g_scene_camera != CAMERA_NONE
                 && g_scene_camera != CAMERA_GAIRA_COUNTDOWN)
                || g_party_gone) {
                *busy = "a scene";
                return GAME_WAIT_NONE;
            }
            if (g_talk_state != 0) {
                *busy = "a talk closing";
                return GAME_WAIT_NONE;
            }
            if (g_win.event_routine != 0 || g_win.depth != 0) {
                return debug_event_wait(busy, "the C menu");
            }
            if (g_walk.demo != NULL || g_bstate.demo_flag) {
                *busy = "a demo walk";
                return GAME_WAIT_NONE;
            }
            if (walk_step_frames(&g_walk) != 0) {
                *busy = "mid-step";
                return GAME_WAIT_NONE;
            }
            return GAME_WAIT_CONTROL;
        }
        case SCREEN_BUILDING: {
            if (g_flight_phase != FLIGHT_NONE) {
                *busy = "the spaceship flight";
                return GAME_WAIT_NONE;
            }
            if (g_paused) {
                return GAME_WAIT_PAUSED;
            }
            w = debug_window_wait(busy);
            if (w != GAME_WAIT_NONE || *busy != NULL) {
                return w;
            }
            return debug_event_wait(busy, "building event code");
        }
        case SCREEN_BATTLE: {
            if (!battle_in_loop(&g_battle)) {
                *busy = "the battle coming in";
                return GAME_WAIT_NONE;
            }
            if (g_paused) {
                return GAME_WAIT_PAUSED;
            }
            if (g_battle.cc06 != 0) {
                *busy = "actions animating";
                return GAME_WAIT_NONE;
            }
            w = debug_window_wait(busy);
            if (w == GAME_WAIT_NONE && *busy == NULL) {
                *busy = (g_battle.main_routine == 1) ? "the fight"
                                                     : "battle event code";
            }
            return w;
        }
        case SCREEN_GAMEOVER: {
            w = debug_window_wait(busy);
            if (w == GAME_WAIT_NONE && *busy == NULL) {
                *busy = "the game over screen";
            }
            return w;
        }
        case SCREEN_ENDING: {
            if (ending_waits_on_pad()) {
                return GAME_WAIT_CONTROL;
            }
            *busy = "the ending screen";
            return GAME_WAIT_NONE;
        }
        default: {
            *busy = "an unknown screen";
            return GAME_WAIT_NONE;
        }
    }
}

/* Nothing between the pad and the leader but, perhaps, the step he is
 * already taking: ObjCL_Main's own early-outs, the pause, the damage
 * floor's stall, and the scenes that take the pad away. */
static bool debug_leader_free(const Game *g)
{
    return g->screen == SCREEN_LEVEL && g_level_walk && g_level_ready
        && g->screen_entered && g->fade == FADE_NONE && !g->sysmenu.open
        && !g_paused && !g_pause_release && g_floor_stall == 0
        && debug_windows_quiet() && g_win.depth == 0
        && g_win.event_routine == 0 && g_win.event_step == 0
        && g_level_event == 0 && g_talk_state == 0
        && (g_scene_camera == CAMERA_NONE
            || g_scene_camera == CAMERA_GAIRA_COUNTDOWN)
        && !g_party_gone
        && g_walk.demo == NULL && !g_bstate.demo_flag;
}

void game_debug_state(const Game *g, GameDebugState *out)
{
    memset(out, 0, sizeof *out);
    out->screen        = g->screen;
    out->entered       = g->screen_entered;
    out->fading        = g->fade != FADE_NONE;
    out->wait          = debug_wait(g, &out->busy);
    out->steps         = g_debug_steps;
    out->event_routine = g_win.event_routine;
    out->window_depth  = g_win.depth;
    out->level         = -1;
    out->building      = (g->screen == SCREEN_BUILDING) ? g_bstate.building
                                                        : -1;
    out->ending_moment = (g->screen == SCREEN_ENDING && g->screen_entered)
                             ? ending_debug_moment() : NULL;
    battle_debug_commands(out->commands);
    out->enemies_joined = battle_debug_enemies_joined();
    out->hidapipe       = g_encounter.hidapipe != 0;

    WinCursorInfo cur;
    if (out->wait == GAME_WAIT_MENU) {
        out->menu = g_win.window_index_saved & 0xFF;
    }
    if (out->wait == GAME_WAIT_MENU && win_debug_cursor(&g_win, &cur)) {
        out->menu        = cur.party ? 0 : (g_win.window_index_saved & 0xFF);
        out->menu_stop   = cur.stop;
        out->menu_stops  = cur.stops;
        out->menu_across = cur.across;
        out->menu_party  = cur.party;
    }

    out->party_count = g_data.party_members_num + 1;
    if (out->party_count > 4) {
        out->party_count = 4;
    }
    for (int m = 0; m < out->party_count; m++) {
        int id = g_data.party_member_id[m] % GD_CHARS;
        const CharData *ch = &g_data.chr[id];
        out->party[m].id     = id;
        out->party[m].level  = ch->level;
        out->party[m].hp     = ch->hp;
        out->party[m].max_hp = ch->max_hp;
        out->party[m].tp     = ch->tp;
        out->party[m].max_tp = ch->max_tp;
        out->party[m].status = ch->status;
    }
    out->meseta = g_data.money;

    if (g->screen == SCREEN_BATTLE && battle_in_loop(&g_battle)) {
        out->formation = g_battle.formation;
        for (int e = 0; e < 8; e++) {
            if (g_battle.obj[BOBJ_ENEMIES + 2 * e].id == 0) {
                continue;
            }
            /* An enemy object's `who` is the slot its group starts at:
             * 0 for the first group, group2_start for the second. */
            const BObj *o = &g_battle.obj[BOBJ_ENEMIES + 2 * e];
            int n = out->enemy_count++;
            out->enemy[n].id     = (g_battle.group2_start != 0
                                    && o->who == g_battle.group2_start)
                                     ? g_battle.group_id[1]
                                     : g_battle.group_id[0];
            out->enemy[n].hp     = g_battle.stat[e].hp;
            out->enemy[n].max_hp = g_battle.stat[e].max_hp;
        }
    }

    if (g->screen == SCREEN_LEVEL && g_level_walk && g_level_ready) {
        out->level       = g_level.index;
        out->x           = g_walk.x;
        out->y           = g_walk.y;
        out->facing      = g_walk.facing;
        out->step_frames = walk_step_frames(&g_walk);
        out->riding      = g_walk.riding;
        out->level_event = g_level_event;
        out->can_walk    = debug_leader_free(g);
    }
}

void game_motion(const Game *g, RumbleMotion *out)
{
    memset(out, 0, sizeof *out);
    switch (g_flight_phase) {
        case FLIGHT_FADE_OUT: {
            out->flight = RUMBLE_FLIGHT_FADE_OUT;
            break;
        }
        case FLIGHT_FADE_IN: {
            out->flight = RUMBLE_FLIGHT_FADE_IN;
            break;
        }
        case FLIGHT_LOOP: {
            out->flight = RUMBLE_FLIGHT_LOOP;
            break;
        }
        case FLIGHT_DONE: {
            out->flight = RUMBLE_FLIGHT_DONE;
            break;
        }
        default: {
            out->flight = RUMBLE_FLIGHT_NONE;
            break;
        }
    }
    out->paused   = g_paused;
    out->building = (g->screen == SCREEN_BUILDING) ? g_bstate.building : -1;
    if (g->screen == SCREEN_LEVEL && g_scene_camera == CAMERA_JITTER) {
        out->shake = RUMBLE_SHAKE_JITTER;
    } else if (g->screen == SCREEN_LEVEL
               && g_scene_camera == CAMERA_SHAKE_THEN_RELOAD) {
        out->shake = RUMBLE_SHAKE_HARD;
    } else {
        out->shake = RUMBLE_SHAKE_NONE;
    }
}

/* (build 162) The level events (ObjType_Events) that are the story's
 * scenes: Darum, the pocket's Letter, the Recorder, Teim, the dam locks,
 * Neifirst and Nei's death, Climatrol, Gaira, the Esper mansion, Pandora's
 * box, Mother Brain, Lutz, the Earthmen and the opening. Not the hints, the
 * signs, the newspapers, the jet scooter, the keyboard, the cards' boxes,
 * a member's death on the damage floor or Shir's thefts. */
static const u8 STORY_EVENTS[] = {
    EVENT_DARUM_SEEN, EVENT_DEAD_BODY_POCKET, EVENT_RECORDER, EVENT_TEIM,
    EVENT_DAM_LOCK_1, EVENT_DAM_LOCK_2, EVENT_DAM_LOCK_3, EVENT_DAM_LOCK_4,
    EVENT_DAM_LOCK_5, EVENT_DAM_LOCK_6, EVENT_DAM_LOCK_7, EVENT_DAM_LOCK_8,
    EVENT_NEIFIRST, EVENT_NEI_DEATH, EVENT_NEI_GONE, EVENT_CLIMATROL_SHAKES,
    EVENT_CLIMATROL_EXPLOSIONS, EVENT_RETURN_TO_PASEO, EVENT_GAIRA_PRISON_2,
    EVENT_GAIRA_PRISON, EVENT_GAIRA_EXPLOSION, EVENT_GAIRA_CONTROL_PANEL,
    EVENT_DOOR_ESPERS, EVENT_ESPER_B1, EVENT_ESPER_MANSION_LUTZ,
    EVENT_PANDORAS_BOX, EVENT_PANDORAS_BOX_OPENED,
    EVENT_MOTHER_BRAIN_CONFRONTATION, EVENT_MOTHER_BRAIN_AFTERMATH,
    EVENT_LUTZ_WARNING, EVENT_EARTHMEN, EVENT_OPENING_PAGE_1,
    EVENT_OPENING_PAGE_2, EVENT_OPENING_PAGE_3, EVENT_OPENING_PAGE_4,
    EVENT_OPENING_PAGE_5, EVENT_OPENING_PAGE_6
};

static bool story_event(int ev)
{
    if ((ev & 0xF00) == EVENT_KIND_TEIM_DARUM) {
        return true;
    }
    if (ev == 0 || (ev & ~0xFF) != EVENT_KIND_EVENTS) {
        return false;
    }
    for (size_t i = 0; i < sizeof STORY_EVENTS; i++) {
        if (STORY_EVENTS[i] == ev) {
            return true;
        }
    }
    return false;
}

/* game_story_beat's detail: the script's id and its first words, the
 * braces' codes left out and line breaks as spaces. */
static void story_words_detail(u16 id, char *out, size_t n)
{
    const ScriptBlock *sb   = script_bank(id >> 8);
    const char        *text = (sb != NULL) ? script_entry(sb, id & 0xFF)
                                           : NULL;
    int at = snprintf(out, n, "$%04X", id);
    if (text == NULL || at < 0 || (size_t)at + 2 >= n) {
        return;
    }
    out[at++] = ' ';
    int kept = 0;
    for (const char *p = text; *p != '\0' && (size_t)at + 1 < n && kept < 32;
         p++) {
        if (*p == '{') {
            const char *close = strchr(p, '}');
            if (close == NULL) {
                break;
            }
            p = close;
            continue;
        }
        out[at++] = (*p == '\n') ? ' ' : *p;
        kept++;
    }
    out[at] = '\0';
}

const char *game_story_beat(const Game *g, char *detail, size_t n)
{
    const char *why = NULL;
    if (detail != NULL && n > 0) {
        detail[0] = '\0';
    }
    switch (g->screen) {
        case SCREEN_ENDING: {
            why = "the ending";
            break;
        }
        case SCREEN_BATTLE: {
            /* The fights with The Place of Death's music (battle_enter):
             * Neifirst, the Army Eyes, Dark Force, Mother Brain. */
            if (g_battle_formation >= 0x100) {
                why = "a boss";
                if (detail != NULL) {
                    snprintf(detail, n, "formation $%X", g_battle_formation);
                }
            }
            break;
        }
        case SCREEN_LEVEL: {
            if (story_event(g_level_event) || g_walk.demo != NULL
                || g_party_gone || g_event_hold != 0
                || g_scene_picture != 0
                || (g_scene_camera != CAMERA_NONE
                    && g_scene_camera != CAMERA_BACK_TO_WAY_OUT)) {
                why = "a scene";
                if (detail != NULL) {
                    snprintf(detail, n, "level %d, event $%X, camera %d, "
                             "picture %d, demo walk %s, party %s",
                             g_level.index, (unsigned)g_level_event,
                             g_scene_camera, g_scene_picture,
                             (g_walk.demo != NULL) ? "on" : "off",
                             (g_party_gone || g_event_hold != 0)
                                 ? "held" : "free");
                }
            }
            break;
        }
        case SCREEN_BUILDING: {
            /* The visits that are scenes from door to door. Not the
             * roof's spaceship (travel) or the Esper mansion (Lutz sends
             * the party to Noah there on every trip); their first words
             * are the story's (story_script). */
            int b = g_bstate.building;
            if (b == BUILDING_ROLF_HOUSE_START || b == BUILDING_GOVERNOR
                || b == BUILDING_GAIRA_PANEL || b == BUILDING_TYLER_SHIP) {
                why = "a story building";
                if (detail != NULL) {
                    snprintf(detail, n, "building %d", b);
                }
            }
            break;
        }
        default: {
            break;
        }
    }
    /* The words while their window types and waits; on the intro screen
     * (the dream) also between its pages, until other words come. */
    if (why == NULL && g_story_words != 0
        && (g_win.window_active_flag != 0 || g->screen == SCREEN_INTRO)) {
        why = "the story's words";
        if (detail != NULL) {
            story_words_detail(g_story_words, detail, n);
        }
    }
    return why;
}

const char *game_debug_char_name(int id)
{
    static const char *const NAME[GD_CHARS] = {
        "Rolf", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"
    };
    return NAME[id % GD_CHARS];
}

/* `techs`: every party member in party order -- name, the command
 * stored for them (b->cmd[c][0], Battle_CommandActionIndex: attack,
 * tech, item, defend), then their battle techniques in list order
 * ($C810 + 32c, techs_a & $1F of them) as NAME:TP:flags, flags being
 * TechniqueData+6 (& 3 0: no target; bit 7 enemies; else one of the
 * party). The commands persist from fight to fight: loc_F4F8 only puts
 * a used technique back to attack, and CharStartCommandsArray starts
 * Amy, Hugh and Shir on defend. */
void game_debug_techs(const Game *g, char *out, size_t n)
{
    static const char *const CMD[4] = { "attack", "tech", "item", "defend" };
    static const char *const WHO[GD_CHARS] = {
        "Rolf", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"
    };
    (void)g;
    size_t used = 0;
    out[0] = '\0';
    for (int m = 0; m <= g_data.party_members_num && m < GD_PARTY_MAX
                    && used < n; m++) {
        int c = g_data.party_member_id[m] % GD_CHARS;
        const CharData *ch = &g_data.chr[c];
        u8 known[32];
        gamedata_known_techs(&g_data, c, known);
        used += (size_t)snprintf(out + used, n - used, "%s%s: %s",
                                 (m == 0) ? "" : "; ", WHO[c],
                                 CMD[g_battle.cmd[c][0] & 3]);
        int count = ch->techs_a & 0x1F;
        for (int i = 0; i < count && i < 16 && used < n; i++) {
            int id = known[16 + i] & 0x3F;
            used += (size_t)snprintf(out + used, n - used, " %s:%u:$%02X",
                                     gamedata_tech_name(id),
                                     (unsigned)gamedata_tech_tp(id),
                                     (unsigned)gamedata_tech_flags(id));
        }
    }
}

int game_debug_text(const Game *g, u8 *out, int max)
{
    (void)g;
    return win_debug_text(&g_win, out, max);
}

void game_debug_bags(const Game *g, char *out, size_t n)
{
    (void)g;
    size_t used = 0;
    out[0] = '\0';
    int members = g_data.party_members_num + 1;
    if (members > GD_PARTY_MAX) {
        members = GD_PARTY_MAX;
    }
    for (int m = 0; m < members && used < n; m++) {
        int id = g_data.party_member_id[m] % GD_CHARS;
        const CharData *ch = &g_data.chr[id];
        used += (size_t)snprintf(out + used, n - used, "%s%s:",
                                 (m == 0) ? "" : "; ",
                                 game_debug_char_name(id));
        for (int i = 0; i < ch->item_count && i < GD_ITEM_SLOTS
             && used < n; i++) {
            used += (size_t)snprintf(out + used, n - used, " %s%s",
                                     gamedata_item_key(ch->items[i] & 0x7F),
                                     (ch->items[i] & 0x80) ? "*" : "");
        }
    }
}

void game_debug_objects(const Game *g, char *out, size_t n)
{
    size_t used = 0;
    out[0] = '\0';
    if (g->screen != SCREEN_LEVEL) {
        return;
    }
    for (int i = 0; i < g_npcs.count && used < n; i++) {
        const Npc *o = &g_npcs.npc[i];
        if (o->free) {
            continue;
        }
        int obj = (o->type != NULL) ? o->type->object
                : (o->chest != NULL) ? o->chest->object
                : (o->scene != NULL) ? o->scene->object : 0;
        used += (size_t)snprintf(out + used, n - used,
                                 "%s%d:$%02X:%d/$%02X:%d,%d%s%s#%d",
                                 (used == 0) ? "" : " ", i, obj,
                                 o->interaction, o->index & 0xFF,
                                 o->x / 16, o->y / 16,
                                 (o->vx != 0 || o->vy != 0) ? "m" : "",
                                 o->hidden ? "h" : "", o->frame);
    }
}

int game_debug_object(const Game *g, int object)
{
    int found = -1;
    if (g->screen != SCREEN_LEVEL) {
        return -1;
    }
    for (int i = 0; i < g_npcs.count; i++) {
        const Npc *o = &g_npcs.npc[i];
        if (o->free || o->scene == NULL || o->scene->object != object) {
            continue;
        }
        if (!o->hidden) {
            return 1;
        }
        found = 0;
    }
    return found;
}

static void debug_track(void);

static void tick_screens(Game *g, const Input *in);

void game_tick(Game *g, const Input *in)
{
    tick_screens(g, in);
    debug_track();
}

/* (build 112b) The screens the Escape menu does not stop: the ones that
 * play by themselves in time with their music -- the SEGA logo, the title
 * and its dissolves, the attract mode's crawl across Motavia (the level
 * screen before any walk: g_level_walk false), and the ending. Everything
 * the player drives stays frozen under the menu, as before. */
static bool runs_under_menu(const Game *g)
{
    switch (g->screen) {
        case SCREEN_SEGA: {
            return true;
        }
        case SCREEN_TITLE: {
            return true;
        }
        case SCREEN_LEVEL: {
            return !g_level_walk;
        }
        case SCREEN_ENDING: {
            return true;
        }
        default: {
            return false;
        }
    }
}

/* (build 163) LANGUAGE changed on the Escape menu: the game reads the new
 * one's files from here on -- its names and face (lang_select), every
 * script bank again when next asked for, the party's default names its
 * own. A message on the screen stays as it was typed; the next is in the
 * new language. */
static void take_language(Game *g)
{
    if (g->sysmenu.language == lang_current()) {
        return;
    }
    /* (build 165) The names as text in the old language's letters, made
     * codes in the new one's: the Japanese cartridge's codes are not the
     * script charset's. */
    char names[GD_CHARS][GD_NAME_TEXT];
    char label[GD_NAME_TEXT];
    gamedata_names_text(&g_data, names, label);
    lang_select(g->sysmenu.language);
    g->sysmenu.language = lang_current();
    gamedata_names_set_text(&g_data, names, label);
    drop_script_banks();
    gamedata_language_names(&g_data);
    /* (build 166) What is on the screen, in the new language too. */
    win_language_changed(&g_win);
}

static void tick_screens(Game *g, const Input *in)
{
    g->input = *in;
    g_debug_logic_ran = false;

    /* Advanced unconditionally, once per tick, as VBlankExit does. */
    (void)rng_next(&g->rng);

    /* SoundDriverInput runs from the VBlank handler every field,
     * including during the blocking fades, so it goes before anything
     * that can return early. */
    if (g->audio != NULL) {
        audio_music_tick(g->audio);
    }

    /* The system menu freezes everything below it: no screen logic, no
     * fade, no pad. Only the music runs on. (build 112b) Except where
     * nobody plays -- the SEGA logo, the title, the attract crawl, the
     * ending (runs_under_menu): the music runs on under the menu, so they
     * run on with it, the pad withheld, and stay in time with it. */
    take_language(g);               /* (build 163) */
    if (sysmenu_tick(&g->sysmenu, &g->input, intro_sound, g)) {
        if (g->sysmenu.quit) {
            g->quit = true;
        }
        if (g->quit || !runs_under_menu(g)) {
            g->ticks++;
            return;
        }
        memset(&g->input, 0, sizeof g->input);
    }

    if (g_sound_wait > 0) {
        if (g->audio == NULL || audio_track_ended(g->audio)) {
            g_sound_wait = 0;
        } else if (++g_sound_wait > SOUND_WAIT_MAX) {
            fprintf(stderr, "game: a sound waited on for %d ticks never "
                            "ended\n", SOUND_WAIT_MAX);
            g_sound_wait = 0;
        } else {
            g->ticks++;
            return;
        }
    }

    if (!g->screen_entered && g->fade == FADE_NONE) {
        enter_screen(g);
    }

    /* Screen logic is paused while a fade runs, matching the original's
     * blocking PaletteFadeFrom / PaletteFadeTo calls. Input is still
     * latched by the platform throughout, so nothing is dropped. */
    if (fade_update(g)) {
        g->ticks++;
        return;
    }

    g_debug_logic_ran = true;
    switch (g->screen) {
        case SCREEN_SEGA:
            tick_sega(g);
            break;
        case SCREEN_TITLE:
            tick_title(g);
            break;
        case SCREEN_INTRO:
            tick_intro(g);
            break;
        case SCREEN_LEVEL:
            tick_level(g);
            break;
        case SCREEN_BUILDING:
            tick_building(g);
            break;
        case SCREEN_BATTLE:
            tick_battle(g);
            break;
        case SCREEN_GAMEOVER:
            tick_gameover(g);
            break;
        case SCREEN_ENDING:
            tick_ending(g);
            break;
        default:
            break;
    }

    if (g_pause_pending) {
        g_pause_pending = false;
        queue_sfx(g, SFX_PAUSE);
        g_paused = true;
    }
    g->ticks++;
}

/* ---- drawing ---- */

static void render_intro(Game *g)
{
    /* DrawArtTiles writes the dream pictures to plane A, where the
     * windows also live; the big message box sits below them. */
    if (g_intro_dream && g_start.picture != 0) {
        nametable_draw(g->video, LAYER_A, &g_dream_map[g_start.picture - 1],
                       DREAM_COL, DREAM_ROW);
    }
    /* (build 151) The fight from its picture, as much of it as the
     * dissolve has put in. */
    if (g_intro_dream && g_start.picture == 1) {
        nametable_picture_hidden(g->video, LAYER_A, &g_dream_map[0],
                                 g_dream_battle_picture, DREAM_COL,
                                 DREAM_ROW, g_dissolve_hidden);
    }
    /* (build 150) The waking drawn from its picture over the cells. */
    if (g_intro_dream && g_start.picture == 2) {
        nametable_picture(g->video, LAYER_A, &g_dream_map[1],
                          g_dream_waking_picture, DREAM_COL, DREAM_ROW);
    }
    win_render(&g_win, g->video, TILE_FONT, 0, 0);
}

/* ObjID_CopyrightText (Obj_CopyrightText, ps2.asm:8116): the "(c) SEGA
 * 1989" line in the bottom-right corner of the title screen. It is not
 * in either title nametable -- it is a sprite object, spawned into
 * copyright_text right after DrawArtTiles and left at its Init values:
 *
 *     $A(a0) = $160, $E(a0) = $150     position, VDP sprite space
 *     8(a0)  = $2300                   palette 1, tile $300
 *     4(a0)  = loc_6D75A               mapping, frame 0
 *
 * Frame 0 is three pieces on one row: a 1x1 at +0, a 4x1 at +16 and a
 * 4x1 at +56, tiles $300, $301 and $305 -- the symbol, the word and the
 * year. VDP sprite coordinates carry a 128-pixel offset, so on screen
 * that is (224, 208). Being a sprite it is untouched by FadeArtTiles and
 * is present from the first frame, coming up with the palette. */
static void draw_copyright(Game *g)
{
    static const struct { s8 x; u8 w; u8 tile; } PIECE[3] = {
        {  0, 1, 0x00 },
        { 16, 4, 0x01 },
        { 56, 4, 0x05 },
    };
    const int base_x = 0x160 - 128;
    const int base_y = 0x150 - 128;

    for (int i = 0; i < 3; i++) {
        Sprite s;
        memset(&s, 0, sizeof s);
        s.x    = (s16)(base_x + PIECE[i].x);
        s.y    = (s16)base_y;
        s.tile = (u16)(TILE_COPYRIGHT + PIECE[i].tile);
        s.w    = PIECE[i].w;
        s.h    = 1;
        s.pal  = 1;
        s.prio = 0;
        video_sprite_push(g->video, &s);
    }
}

/* Frame 1 of loc_6D75A: fifteen single tiles spelling the prompt, all on
 * one row, from the same copyright sheet -- tiles $09..$12 with 'S', 'T'
 * and 'U' reused. Object position $FD/$140 in sprite space is (125, 192)
 * on screen; the offsets run from -32 to +96 with gaps at 0 and 48 for
 * the two spaces. */
static void draw_push_start(Game *g)
{
    static const struct { s8 x; u8 tile; } PIECE[15] = {
        { -32, 0x09 }, { -24, 0x0A }, { -16, 0x0B }, {  -8, 0x0C },
        {   8, 0x0B }, {  16, 0x0D }, {  24, 0x0E }, {  32, 0x0F },
        {  40, 0x0D }, {  56, 0x10 }, {  64, 0x0A }, {  72, 0x0D },
        {  80, 0x0D }, {  88, 0x11 }, {  96, 0x12 },
    };
    const int base_x = 0x0FD - 128;
    const int base_y = 0x140 - 128;

    for (int i = 0; i < 15; i++) {
        Sprite s;
        memset(&s, 0, sizeof s);
        s.x    = (s16)(base_x + PIECE[i].x);
        s.y    = (s16)base_y;
        s.tile = (u16)(TILE_COPYRIGHT + PIECE[i].tile);
        s.w    = 1;
        s.h    = 1;
        s.pal  = 1;
        s.prio = 0;
        video_sprite_push(g->video, &s);
    }
}

void game_render(Game *g)
{
    Cell blank;
    memset(&blank, 0, sizeof blank);

    video_fill(g->video, LAYER_A, blank);
    video_fill(g->video, LAYER_B, blank);
    video_pictures_clear(g->video);     /* (build 142) with the planes */
    video_sprites_begin(g->video);
    win_text_pool_reset();              /* (build 163) the frame's text */

    switch (g->screen) {
        case SCREEN_SEGA:
            nametable_draw(g->video, LAYER_A, &g_sega_map,
                           SEGA_LOGO_COL, SEGA_LOGO_ROW);
            /* (build 149) Its picture, from the frame after enter_sega:
             * the frame drawn between the switch and the setup shows what
             * VRAM held, as it did. (157a) Every hand-over fades first
             * now, so that frame is black. */
            if (g->screen_entered) {
                nametable_picture(g->video, LAYER_A, &g_sega_map,
                                  g_sega_picture, SEGA_LOGO_COL,
                                  SEGA_LOGO_ROW);
            }
            break;
        case SCREEN_TITLE:
            nametable_draw(g->video, LAYER_B, &g_title_b, 0, 0);
            nametable_draw(g->video, LAYER_A, &g_title_a, 0, 0);
            /* (build 154) Both planes from their pictures: plane A's
             * once a dissolve's range, through its mask. */
            nametable_picture(g->video, LAYER_B, &g_title_b,
                              g_title_background_picture, 0, 0);
            nametable_picture_tiles(g->video, LAYER_A, &g_title_a,
                                    g_title_picture, 0, 0, TILE_WOMEN,
                                    COUNT_WOMEN, g_figures_hidden);
            nametable_picture_tiles(g->video, LAYER_A, &g_title_a,
                                    g_title_picture, 0, 0, TILE_LOGO,
                                    COUNT_LOGO, g_logo_hidden);
            /* Both objects are spawned at TitleScreenLoop (ps2.asm:10765),
             * which is entered only after the two dissolves finish. They
             * are not on screen for the first 692 frames of the title. */
            if (g->title_phase == TITLE_WAIT) {
                draw_copyright(g);
                if (push_start_visible(g)) {
                    draw_push_start(g);
                }
            }
            break;
        case SCREEN_INTRO:
            render_intro(g);
            break;
        case SCREEN_LEVEL:
            render_level(g);
            break;
        case SCREEN_BUILDING:
            render_building(g);
            break;
        case SCREEN_BATTLE:
            render_battle(g);
            break;
        case SCREEN_GAMEOVER:
            render_gameover(g);
            break;
        case SCREEN_ENDING:
            render_ending(g);
            break;
        default:
            break;
    }

}

void game_overlay(Game *g, Framebuffer *fb)
{
    sysmenu_overlay(&g->sysmenu, fb, &g_font);
}
