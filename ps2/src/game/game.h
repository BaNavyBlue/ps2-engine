#ifndef PS2_GAME_H
#define PS2_GAME_H

#include "../core/audio.h"
#include "../core/level.h"
#include "../core/nametable.h"
#include "../core/rng.h"
#include "../core/video.h"
#include "../core/winmap.h"
#include "../platform/platform.h"
#include "sysmenu.h"

#include <stddef.h>

/* Screen ids, in the order GameScreenTable dispatches them
 * (ps2.asm:151). Only the first two and a menu stand-in exist so far. */
typedef enum {
    SCREEN_SEGA = 0,
    SCREEN_TITLE,
    SCREEN_INTRO,    /* Start pressed: ScreenID_Intro, Rolf's house */
    SCREEN_LEVEL,    /* timed out: ScreenID_Level, Motavia opening */
    SCREEN_BUILDING, /* ScreenID_Building: a portrait and a conversation */
    SCREEN_BATTLE,   /* ScreenID_Battle: a random encounter */
    SCREEN_GAMEOVER, /* GameOverScreen (12255): the party destroyed */
    SCREEN_ENDING,   /* EndingScreen (10795): after the Earthmen */
    SCREEN_COUNT
} ScreenId;

/* TitleScreen is a sequence, not a single state: background, then the
 * figures dissolve in, then the logo, then the wait loop. */
typedef enum {
    TITLE_BG = 0,
    TITLE_FADE_FIGURES,
    TITLE_FADE_LOGO,
    TITLE_WAIT
} TitlePhase;

/* Every screen routine opens with PaletteFadeFrom and closes its setup
 * with PaletteFadeTo -- 21 frames each, one intensity step per frame. */
typedef enum {
    FADE_NONE = 0,
    FADE_OUT,
    FADE_IN,
    FADE_OUT_WHITE,  /* loc_634E: the flash into a battle */
    FADE_IN_WHITE    /* (build 138d) loc_62CC: the level after the approach
                        to Mother Brain (LevelScreen, event_flags $E00) */
} FadeState;

typedef struct {
    ScreenId screen;
    ScreenId pending_screen;
    bool     screen_entered;   /* false until the screen's load step runs */

    FadeState fade;
    int       fade_frames;
    Palette   fade_target;

    Video *video;
    Audio *audio;
    Rng    rng;

    Input input;

    u16  demo_timer;      /* the original's countdown, in frames */

    /* PaletteCycle's two counters: $FFFFF634 ticks every frame and gates
     * the cycle to one step in four; $FFFFF632 is the byte offset into
     * CyclingPal_SegaLogo and stops the sweep when it goes negative. */
    s16  pal_cycle_gate;
    s16  pal_cycle_offset;

    TitlePhase title_phase;
    int        fade_step;     /* 0..63, indexes FadeEffectArray */
    int        fade_gate;     /* 4 frames per step */
    /* Motavia opening: the camera crawl. MoveToOpeningScreen locks the
     * controls, so no character object is ever spawned and the whole
     * screen is the camera walking between waypoints. */
    s16  cam_x, cam_y;
    int  crawl_step;
    int  crawl_hold;
    int  crawl_close;      /* frames left of the window's closing wipe */

    /* ObjID_PushStartButton5 on the title screen: hidden until any
     * button is touched, then blinking. */
    bool push_start_armed;
    int  push_start_timer;

    u32  ticks;
    bool quit;

    /* The port's own Escape menu; see sysmenu.h. main.c reads
     * sysmenu.controls_changed to remap the keys. */
    SysMenu sysmenu;

    /* (build 186) A language asked for -- on LANGUAGE, or by settings.json
     * at the start -- whose files are not there yet (lang_missing): its
     * place in the list, for main.c to offer to make (docs/first_start.md),
     * -1 when none. The game goes on in the language it had. */
    int language_wanted;
} Game;

bool game_init(Game *g, Video *video);
void game_attach_audio(Game *g, Audio *audio);
void game_tick(Game *g, const Input *in);
void game_render(Game *g);

/* After video_compose: the system menu, drawn over the finished frame. */
void game_overlay(Game *g, Framebuffer *fb);

/* PS2_BUILDING="building,portrait,f764,f766[,cellx,celly]" (C number
 * syntax): start in that building, as the door with those four bytes
 * would. The optional cell is where on Paseo leaving puts the party. */
/* The party as the game holds it, for the probes: a tool can put items
 * in a bag or techniques on a character before it presses buttons. */
struct GameData *game_debug_data(Game *g);
struct WinSys   *game_debug_win(Game *g);

void game_debug_level(Game *g, int level, int cell_x, int cell_y);
void game_debug_battle(Game *g, int formation);

/* tools/route_probe.c's `party N LEVEL`: the first N characters in joining
 * order (Rolf, Nei, Rudo, Amy) in the party, each brought up to LEVEL a
 * row at a time the way a fight's level-ups do it (the gains rolled, on
 * the game's own RNG), then HP and TP full. For right after a start,
 * before the level is entered, so the followers match. */
void game_debug_party(Game *g, int members, int level);
/* The harness's: every live enemy's HP set to `hp` (0 kills them on
 * their next action). For routes about what follows a boss. */
void game_debug_enemy_hp(Game *g, int hp);
void game_debug_members(Game *g, const int *ids, int n);
void game_debug_member_level(Game *g, int id, int level);
bool game_debug_outfit(Game *g, int id, const int item[5]);
void game_debug_oneshot(Game *g, int scene);
void game_debug_warp(Game *g, int level, int cell_x, int cell_y);
void game_debug_building(Game *g, int building, int portrait,
                         int store_max, int store_index,
                         int cell_x, int cell_y);

/* Save slot `slot` (1-3) loaded as CONTINUE loads it, then Data Memory's
 * "We meet again" as loc_D3B6 goes on to -- without the SEGA screen, the
 * title or the select windows. False when the slot will not load. */
bool game_debug_continue(Game *g, int slot);

/* (build 100b) The console's reset button, for COMPUTER PLAYS: straight to
 * the Sega screen as a game over and the ending go (the screen not entered,
 * no fade), the random number generator back to its power-on seed, the
 * Escape menu shut. The save data and the rest are left for the Sega
 * screen, the title and NEW GAME or CONTINUE to set, as they do after a
 * game over. */
void game_debug_reset(Game *g);

/* ---- for tools/route_probe.c: what a route waits on ----
 *
 * What the game is waiting for, when it is waiting for the pad and
 * nothing else. Each is recognised from the state the ROM itself waits
 * on, never from a timer:
 *
 *   TITLE    the finished title (TITLE_WAIT), Start to go on
 *   CONTROL  the party on the map, at rest, no window, no demo walk,
 *            no scene: a held direction steps, A talks, C opens the menu
 *   TEXT     a message stopped at $C4 (end) or $C3 ({pause}): A, B or C
 *   MENU     a window's poll holding the pad (window_index_saved, its
 *            routine at 2), or the battle's party triangle
 *   EVENT    event code reading the pad itself, with no window live:
 *            the C menu's stat pages, the profile in Rolf's house. Seen
 *            as its counters (event_routine, _sub, _sub_2, $DE70/$DE72,
 *            the $DE5E timers) standing still with the windows idle for
 *            two ticks; only on the map's menu and in buildings.
 *   PAUSED   GamePausedLoop
 *
 * Anything else is GAME_WAIT_NONE, with `busy` saying what is running. */
typedef enum {
    GAME_WAIT_NONE = 0,
    GAME_WAIT_TITLE,
    GAME_WAIT_CONTROL,
    GAME_WAIT_TEXT,
    GAME_WAIT_MENU,
    GAME_WAIT_EVENT,
    GAME_WAIT_PAUSED
} GameWait;

typedef struct {
    ScreenId    screen;
    bool        entered;     /* the screen's load step has run          */
    bool        fading;      /* a screen fade: logic is held            */
    GameWait    wait;
    const char *busy;        /* what runs, when wait is GAME_WAIT_NONE  */

    /* The level screen with the party on it; level is -1 elsewhere. */
    int  level;
    int  x, y;               /* the leader, world pixels ($A / $E)      */
    int  facing;             /* FACE_UP / _DOWN / _LEFT / _RIGHT        */
    int  step_frames;        /* $28: 0 at rest                          */
    u32  steps;              /* steps begun since game_init (loc_38FC)  */
    int  level_event;        /* $DE6E while an event runs, else 0;
                                $1000 is ObjType_Events 0              */
    int  building;           /* building_index on SCREEN_BUILDING, -1   */
    const char *ending_moment;  /* on SCREEN_ENDING, ending_debug_moment:
                                   "page3_text", "credits", "ufo", ...;
                                   NULL elsewhere                      */
    bool riding;             /* on the jet scooter: x, y are its origin,
                                a cell corner, and step_frames its $28   */
    bool can_walk;           /* a held direction reaches the leader:
                                CONTROL, or mid-step with nothing else
                                in the way                              */
    u16  event_routine;
    int  window_depth;

    /* With wait == GAME_WAIT_MENU: the window whose poll holds the pad
     * (window_index_saved's low byte; 0 for the battle's party
     * triangle) and its one-line cursor, from win_debug_cursor --
     * menu_stops is 0 when there is none to read (the name board). */
    int  menu;
    int  menu_stop;
    int  menu_stops;
    bool menu_across;        /* left/right move it: the triangles       */
    bool menu_party;         /* the party triangle                      */

    /* battle_debug_commands: attack, technique, item, defend begun. */
    u32  commands[4];
    u32  enemies_joined;     /* battle_debug_enemies_joined            */
    bool hidapipe;           /* $FFFFCB0E set: the Hidapipe is on       */

    /* The party as the save block holds it, whatever the screen: each
     * member in party order by character index (0 Rolf .. 7 Shir), the
     * level, HP and TP, and the status word ($C000+0: bit 15 poison,
     * bit 14 asleep; the dead are hp 0), and the money. What a driver
     * needs to decide between the hospital and another fight. */
    int  party_count;
    struct {
        int id;
        int level;
        int hp, max_hp;
        int tp, max_tp;
        u16 status;
    } party[4];
    u32  meseta;

    /* On the battle screen: the formation (1-based) and each enemy slot
     * that is still an object -- its enemy id and HP -- so a driver can
     * tell a fight it is winning from one it is not. formation is 0
     * elsewhere. */
    int  formation;
    int  enemy_count;
    struct {
        int id;
        int hp, max_hp;
    } enemy[8];
} GameDebugState;

/* The character's name by index, "Rolf" .. "Shir" (the id, not what the
 * player typed). */
const char *game_debug_char_name(int id);

/* Every party member's bag as one line: "Rolf: Knife* CarbonSuit* Shoes*
 * Monomate; Nei: ..." -- ItemID_ names in bag order, a * on what is
 * worn. For tools/route_probe.c's `bags`. */
void game_debug_bags(const Game *g, char *out, size_t n);

/* One line: each party member's stored battle command and battle
 * techniques in list order, NAME:TP:flags. */
void game_debug_techs(const Game *g, char *out, size_t n);

/* Every live object slot on the level screen, for a driver that has to
 * find someone who wanders: "slot:object:interaction/index:cellx,celly"
 * (the cell the object's x, y fall in), with "m" after a mover mid-step,
 * space-separated. Empty off the level screen. */
void game_debug_objects(const Game *g, char *out, size_t n);

/* A scene object by ObjID_ on this level: 1 in a slot and shown, 0 in a
 * slot but hidden (bit 1 of 2(a0)), -1 in none. */
int game_debug_object(const Game *g, int object);

void game_debug_state(const Game *g, GameDebugState *out);

/* (build 161) For the rumble, once a tick, nothing the game reads: the
 * spaceship's flight, the pause, the building and the scene's shake
 * (RumbleMotion, src/game/rumble.h). */
void game_motion(const Game *g, RumbleMotion *out);

/* (build 162) For COMPUTER PLAYS's SPEED AUTO, once a tick, nothing the
 * game reads: what of the story is on screen now -- "the ending", "a boss",
 * "a scene" (a scripted level event, camera case, picture or demo walk),
 * "a story building" (the Governor, Gaira's control panel, Tyler's ship,
 * Rolf's house at the start), "the story's words" (one of the plot's
 * scripts in the text window, the first time it shows since
 * game_debug_reset) -- or NULL. With `detail` (may be
 * NULL), which one, for a log: the script's id and first words, the event,
 * the building, the formation. */
const char *game_story_beat(const Game *g, char *detail, size_t n);

/* (build 131a) The text window's last script as its codes (window.c's
 * win_debug_text): the count copied, at most max. */
int game_debug_text(const Game *g, u8 *out, int max);

/* Called with every script id that reaches a window (LoadScript's
 * lookup, run-on ids included) and every message of the attract crawl.
 * NULL to stop. */
void game_debug_on_script(Game *g, void (*fn)(void *ctx, u16 id),
                          void *ctx);

#endif /* PS2_GAME_H */
