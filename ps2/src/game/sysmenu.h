#ifndef PS2_SYSMENU_H
#define PS2_SYSMENU_H

#include "../core/crt.h"
#include "../core/palette.h"
#include "../core/tileset.h"
#include "../core/video.h"
#include "../platform/platform.h"
#include "rumble.h"

#include <stddef.h>

/* The port's own menu, on Escape. Not from the ROM: the cartridge had no
 * way to quit and no keys to remap. It is drawn the way the game draws
 * its option windows -- the same frame tiles, the same fill, the script
 * font, the lamp cursor blinking on the seven-frame timer
 * Obj_RedRectangleCursor uses -- so it does not look bolted on.
 *
 * While it is open the game is frozen: no screen logic, no fades, no
 * pad. The music keeps playing, because stopping the sequencer leaves
 * whatever is keyed on ringing (the pause jingle exists to mute first),
 * and a menu that silences the game reads as a pause rather than a
 * menu. Start still pauses on its own.
 *
 * The window is drawn in the window colours (PalettePtrs entry 2, line 0)
 * through the framebuffer's own overlay line (video.h), so it reads the
 * same on every screen and never changes the colour of anything the game
 * drew -- the game's palette is not touched. The font is placed at a tile
 * base no screen uses for art. */


typedef enum {
    SYSMENU_CONTINUE = 0,
    SYSMENU_CONTROLS,           /* (build 160) opens the CONTROLS page */
    SYSMENU_DISPLAY,
    SYSMENU_FULLSCREEN,         /* (build 100) */
    SYSMENU_CRT,                /* (build 105) opens the CRT page */
    SYSMENU_AUDIO,              /* (build 110) opens the AUDIO page */
    SYSMENU_COMPUTER,           /* (build 100b) COMPUTER PLAYS */
    SYSMENU_SPEED,              /* (build 100b) its speed (ComputerSpeed) */
    SYSMENU_GAMEPAD,            /* (build 160) opens the GAMEPAD page */
    SYSMENU_LANGUAGE,           /* (build 163) the game's text (lang.h) */
    SYSMENU_QUIT,
    SYSMENU_ITEMS
} SysMenuItem;

/* (build 120) COMPUTER PLAYS's SPEED, in the order C steps through them.
 * NORMAL a tick a frame, 2X two, FAST as many as a frame's time allows,
 * muted; AUTO and AUTO 2X are NORMAL and 2X but FAST while the driver says
 * it is grinding (docs/computer_plays.md, Speed). (build 162) NORMAL and
 * 2X in the story's beats, FAST between them; (build 169a) 32 and 64
 * ticks a frame between them, muted (main.c, AUTO_GRIND). */
typedef enum {
    COMPUTER_SPEED_NORMAL = 0,
    COMPUTER_SPEED_AUTO,
    COMPUTER_SPEED_2X,
    COMPUTER_SPEED_AUTO_2X,
    COMPUTER_SPEED_FAST,
    COMPUTER_SPEED_COUNT
} ComputerSpeed;

/* (build 105) The pages: the menu itself, CRT's (from the CRT row) and
 * HDR's (from the CRT page's HDR row). Escape or B goes back a page, and
 * closes the menu from the menu itself. */
typedef enum {
    SYSMENU_PAGE_MAIN = 0,
    SYSMENU_PAGE_CRT,
    SYSMENU_PAGE_HDR,
    SYSMENU_PAGE_AUDIO,         /* (build 110) from the AUDIO row */
    SYSMENU_PAGE_GAMEPAD,       /* (build 160) from the GAMEPAD row */
    SYSMENU_PAGE_CONTROLS,      /* (build 160) the keys, from CONTROLS */
    SYSMENU_PAGE_BUTTONS        /* (build 160) the gamepads' inputs, from
                                   the GAMEPAD page's BUTTONS */
} SysMenuPage;

/* (build 160) The GAMEPAD page's rows: each gamepad connected, by the name
 * it gives itself (NONE: none in that slot) -- C, A or Start on one buzzes
 * it, to tell which is which -- and RUMBLE (src/game/rumble.h): OFF, LOW,
 * MEDIUM (the default) or HIGH, every gamepad buzzing at the new level as
 * it changes. Left and right step RUMBLE and stop at its ends; C goes
 * round. The menu's row shows how many gamepads are connected. BUTTONS,
 * last, opens the BUTTONS page.
 *
 * (build 160) The CONTROLS page (the keys) and the BUTTONS page (every
 * gamepad's inputs, src/core/keys.h): a row an action, two slots in each,
 * then DEFAULTS, which sets that page's half of the controls back. Up and
 * down pick the row, left and right the slot; C, A or Start (or Enter)
 * on a slot listens, the slot blinking: the next key, or the next gamepad
 * input on any gamepad (a stick or trigger past half its travel), binds
 * there and leaves every other slot; Escape or a gamepad's Back keeps the
 * slot as it was, Backspace empties it, anything else is not heard. The
 * BUTTONS page draws an input as PAD 1 labels it (pad_style). */
typedef enum {
    SYSMENU_GAMEPAD_PAD1 = 0,
    SYSMENU_GAMEPAD_PAD2,
    SYSMENU_GAMEPAD_RUMBLE,
    SYSMENU_GAMEPAD_BUTTONS,    /* (build 160) opens the BUTTONS page */
    SYSMENU_GAMEPAD_ITEMS
} SysMenuGamepadItem;

#define SYSMENU_CONTROL_ROWS (CONTROL_ACTIONS + 1)  /* the actions, DEFAULTS */

#define SYSMENU_PADS     2
#define SYSMENU_PAD_NAME 19     /* characters of a name the page shows */

typedef enum {
    SYSMENU_CRT_SCANLINES = 0,
    SYSMENU_CRT_MASK,
    SYSMENU_CRT_SUBPIXELS,
    SYSMENU_CRT_HDR,
    SYSMENU_CRT_ITEMS
} SysMenuCrtItem;

typedef enum {
    SYSMENU_HDR_SWITCH = 0,
    SYSMENU_HDR_PEAK,
    SYSMENU_HDR_WHITE,
    SYSMENU_HDR_ITEMS
} SysMenuHdrItem;

/* (build 110) The AUDIO page's rows: the synth backend, and the volumes on
 * the mix's own gains (src/audio/mix.h). (build 111) EFFECTS: the sequencer's
 * hooks tell the effects apart from the music (docs/audio.md, "The AUDIO
 * page"), so MUSIC is the music alone and EFFECTS the sound effects.
 * (build 111a) OVERSAMPLE: the rate Q's PM voices run at, 1X, 2X or 4X the
 * output's -- at 1X the chip's folded-back images, its crunch on bright
 * effects; at 4X (the default) none in the audible band.
 * (build 112) Q's three switches (synth.h's SynthOption): VOICES -- EXTRA,
 * effects on voices of their own with the music playing on under them (the
 * default), or LIMITED, an effect taking the music's channel as the chip's
 * six do; TAILS -- FADE, a note struck again ends the last in 15 ms (the
 * default), or RING, the last rings out its own release; PATCHES -- ON, the
 * patch file's instruments in place of the ROM's (the default), or OFF.
 * Each shows Q ONLY under it while the chip plays. (build 116) GRAIN -- OFF
 * (the default), or ON: the chip's output grain on Q's channels, what
 * Step Up's chord instrument's tail is made of (build 112's PS2_Q_GRAIN).
 * And Q ONLY beside the value now: the page's rows are two apart. */
typedef enum {
    SYSMENU_AUDIO_SYNTH = 0,
    SYSMENU_AUDIO_MASTER,
    SYSMENU_AUDIO_MUSIC,
    SYSMENU_AUDIO_EFFECTS,
    SYSMENU_AUDIO_OVERSAMPLE,       /* (build 111a) */
    SYSMENU_AUDIO_VOICES,           /* (build 112) */
    SYSMENU_AUDIO_TAILS,            /* (build 112) */
    SYSMENU_AUDIO_PATCHES,          /* (build 112) */
    SYSMENU_AUDIO_GRAIN,            /* (build 116) */
    SYSMENU_AUDIO_ITEMS
} SysMenuAudioItem;

/* What sysmenu_audio_value draws, by row, for the route probe's `expect
 * audio` too: the main menu's AUDIO summary and the page's three rows. */
typedef enum {
    SYSMENU_AUDIO_VALUE_SUMMARY = 0,
    SYSMENU_AUDIO_VALUE_SYNTH,
    SYSMENU_AUDIO_VALUE_MASTER,
    SYSMENU_AUDIO_VALUE_MUSIC,
    SYSMENU_AUDIO_VALUE_EFFECTS,     /* (build 111) */
    SYSMENU_AUDIO_VALUE_OVERSAMPLE,  /* (build 111a) */
    SYSMENU_AUDIO_VALUE_VOICES,      /* (build 112) */
    SYSMENU_AUDIO_VALUE_TAILS,       /* (build 112) */
    SYSMENU_AUDIO_VALUE_PATCHES,     /* (build 112) */
    SYSMENU_AUDIO_VALUE_GRAIN,       /* (build 116) */
    SYSMENU_AUDIO_VALUE_COUNT
} SysMenuAudioValue;

/* (build 110) The volumes' range, whole dB: 0 the default and the top (the
 * mix at 0 dB already brings its worst passage near full scale), down to
 * SYSMENU_DB_MIN, and below it OFF (SYSMENU_DB_OFF, audio.h's AUDIO_DB_OFF). */
#define SYSMENU_DB_MAX 0
#define SYSMENU_DB_MIN (-30)
#define SYSMENU_DB_OFF (-100)

typedef struct {
    int synth;                  /* SynthId (src/audio/synth.h) */
    int master_db;              /* SYSMENU_DB_MIN..0, or SYSMENU_DB_OFF */
    int music_db;
    int effects_db;             /* (build 111) */
    int oversample;             /* (build 111a) Q's PM voices: 1, 2 or 4 x */
    int voices;                 /* (build 112) 0 EXTRA, 1 LIMITED */
    int tails;                  /* (build 112) 0 FADE, 1 RING */
    bool patches;               /* (build 112) the patch file: ON, OFF */
    bool grain;                 /* (build 116) the chip's grain on Q: ON, OFF */
} AudioSettings;

/* What sysmenu_crt_value draws, by row, for the route probe's `expect crt`
 * too: the main menu's CRT summary, the CRT page's four rows and the HDR
 * page's three. */
typedef enum {
    SYSMENU_VALUE_SUMMARY = 0,
    SYSMENU_VALUE_SCANLINES,
    SYSMENU_VALUE_MASK,
    SYSMENU_VALUE_SUBPIXELS,
    SYSMENU_VALUE_HDR,
    SYSMENU_VALUE_PEAK,
    SYSMENU_VALUE_WHITE,
    SYSMENU_VALUE_COUNT
} SysMenuValue;

typedef struct {
    bool open;
    int  cursor;
    s16  blink;
    bool lamp_hidden;
    s16  repeat;                /* held up/down auto-repeat, as $28 */
    s16  side_repeat;           /* (build 105) held left/right, CRT pages */

    Controls controls;          /* (build 160) the CONTROLS and BUTTONS
                                   pages' (until 160 a ControlScheme) */
    bool controls_changed;      /* main.c pushes the controls to the
                                   platform and writes settings.json, then
                                   clears */
    AspectMode display;         /* the pixel aspect, platform.h */
    bool display_changed;       /* the same, for the display */
    bool fullscreen;            /* (build 100) borderless, platform.h */
    bool fullscreen_changed;    /* the same, for fullscreen */

    /* (build 105) The CRT page's settings (docs/crt.md), and what the
     * display can do: main.c refreshes `crt_display` from the platform
     * every frame the menu is open, the route probe sets it with
     * `display`. `page` and the two page cursors are the submenus'. */
    CrtSettings crt;
    bool        crt_changed;
    CrtDisplay  crt_display;
    SysMenuPage page;
    int         crt_cursor;
    int         hdr_cursor;

    /* (build 110) The AUDIO page's settings; main.c hands them to the
     * mixer and writes settings.json when `audio_changed`, then clears it.
     * `synth` is what was chosen: a backend this build has not got (Q
     * without `make Q=1`) shows NOT BUILT and the chip plays. */
    AudioSettings audio;
    bool          audio_changed;
    int           audio_cursor;

    /* (build 100b) COMPUTER PLAYS (docs/computer_plays.md). Where it
     * starts -- 0 a new game, 1-4 that slot of the player's, (build 103)
     * 5 the computer's own last run -- is chosen
     * with left and right on its row; main.c fills slot_exists and
     * computer_note (NO TOOLS, or NO PYTHON3 for the Python driver: then
     * it cannot start) when the
     * menu opens. (build 100c) C on the row lights or puts out its lamp,
     * computer_on; main.c starts the computer when the menu closes with
     * the lamp lit, and hands the pad back when it closes with the lamp
     * out. */
    int  computer_from;         /* (build 103) 5: the computer's last
                                 * run, where it saved last (LAST RUN) */
    bool slot_exists[4];
    bool last_run_exists;       /* (build 103) saves/computer/last.json */
    char computer_note[16];
    bool computer_on;
    int  computer_speed;        /* SPEED: a ComputerSpeed (build 120; a
                                 * bool, NORMAL or FAST, before) */
    bool computer_speed_changed;

    /* (build 160) The GAMEPAD page: RUMBLE, and the gamepads' names, which
     * main.c fills from the platform while the menu is open ("" none:
     * sysmenu_set_pad_name). A PAD row chosen, or RUMBLE changed, leaves
     * the gamepad to buzz in `rumble_test` (SYSMENU_PADS: every one) for
     * main.c, which sets it back to -1. main.c writes settings.json when
     * `rumble_changed`, then clears it. */
    RumbleLevel rumble;
    bool        rumble_changed;

    /* (build 163) LANGUAGE: the language the game's text is in, by its
     * place in assets/lang/languages.json (lang.h). C, left and right on
     * the row step through them; the game takes the new one up at its
     * next tick (game.c), and main.c writes settings.json when
     * `language_changed`, then clears it. */
    int         language;
    bool        language_changed;
    int         rumble_test;
    int         gamepad_cursor;
    char        pad_name[SYSMENU_PADS][SYSMENU_PAD_NAME + 1];

    /* (build 160) The CONTROLS and BUTTONS pages: each page's row (an
     * action, or DEFAULTS) and slot; whether a slot is listening, and for
     * how many ticks (its blink); and whose labels the BUTTONS page draws,
     * which main.c hands over from the platform every frame
     * (sysmenu_set_pad_style). */
    int      controls_row;
    int      controls_col;
    int      buttons_row;
    int      buttons_col;
    bool     listening;
    int      listen_ticks;
    PadStyle pad_style;
    bool quit;

    Cram colours[PAL_ENTRIES];  /* PALETTE_FIELD_COMMON line 0, once */
} SysMenu;

void sysmenu_init(SysMenu *m);

/* Loads saves/settings.json into m->display, m->fullscreen,
 * m->computer_speed, (build 105) m->crt, (build 110) m->audio, (build 163)
 * m->language ("language": a key of assets/lang/languages.json; one no
 * language has keeps the first, saying so) and (build
 * 160) m->rumble ("rumble": "off", "low", "medium" or "high") and
 * m->controls ("keys": {"up": ["UP", "W"], ...} and "pad": {"up": ["DUP",
 * "LSUP"], ...}, each action's two slots by name, "-" none; a name nothing
 * has keeps that slot's default, saying so). A missing file, or a key
 * missing from it, leaves the default: the controls' (both key sets), TV,
 * windowed, normal, the CRT off, the chip at 0 dB, RUMBLE MEDIUM. Until
 * build 160 "controls" was "arrows" or "wasd", one key set: read and
 * dropped now, both being the defaults; it is not written again. */
void sysmenu_load_settings(SysMenu *m);

/* (build 100) Where settings.json is read and written: saves/settings.json
 * unless set before game_init. The route probe points it into the route's
 * own save directory, so a route never reads the player's settings (the
 * Escape menu draws them) and nothing ever writes there but main.c. */
void sysmenu_set_settings_path(const char *path);

/* (build 120) SPEED's value in settings.json -- "normal", "auto", "2x",
 * "auto_2x", "fast" -- and on the menu ("NORMAL", "AUTO", "2X", "AUTO 2X",
 * "FAST"); NULL for no ComputerSpeed. `expect setting speed` takes the
 * first. */
const char *sysmenu_speed_key(int speed);
const char *sysmenu_speed_name(int speed);
void sysmenu_save_settings(const SysMenu *m);

/* Opens or closes on Escape, and runs the menu while open. Returns true
 * when the menu consumed the frame -- the game must not tick. `sound`
 * queues an SFX id (may be NULL). */
bool sysmenu_tick(SysMenu *m, const Input *in,
                  void (*sound)(void *ctx, u8 id), void *ctx);

/* (build 105) The text a CRT row shows, and under it why the chosen
 * setting cannot be had on this display ("" when it can) -- what the
 * menu draws, and what the probe's `expect crt` compares. */
void sysmenu_crt_value(const SysMenu *m, SysMenuValue row, char *value,
                       size_t value_n, char *why, size_t why_n);

/* (build 110) The text an AUDIO row shows, and under it why the chosen
 * value cannot be had (NOT BUILT: Q in a build without it; "" when it can) --
 * what the menu draws, and what the probe's `expect audio` compares. */
void sysmenu_audio_value(const SysMenu *m, SysMenuAudioValue row, char *value,
                         size_t value_n, char *why, size_t why_n);

/* (build 160) Gamepad `pad`'s name for the GAMEPAD page (NULL: none
 * there): upper case, a character the font has not got a space, cut to
 * SYSMENU_PAD_NAME. */
void sysmenu_set_pad_name(SysMenu *m, int pad, const char *name);

/* (build 160) The gamepads the page shows. */
int  sysmenu_pads(const SysMenu *m);

/* (build 160) Whose labels the BUTTONS page draws (keys.h): PAD 1's. */
void sysmenu_set_pad_style(SysMenu *m, PadStyle style);

/* (build 160) An action's name in settings.json ("up" ... "start"; NULL
 * for no ControlAction), and what a slot shows: on the CONTROLS page
 * (`pad` false) the key's name, on the BUTTONS page the gamepad input as
 * pad_style labels it -- what the menu draws, and what the probe's
 * `expect keys` and `expect buttons` compare. */
const char *sysmenu_action_key(int action);
void sysmenu_slot_text(const SysMenu *m, bool pad, int action, int slot,
                       char *out, size_t n);

/* (build 110) The backend that plays for the chosen SYNTH: it, if this
 * build has it, else the chip. */
int  sysmenu_audio_synth_playing(const SysMenu *m);

/* Draws the window and the lamp straight into the composed frame, in the
 * overlay line, and sets fb->overlay to the menu's colours. Not into a
 * plane: on the map the planes carry the
 * camera's pixel scroll and the lamp, a sprite, would not, so the two
 * drifted apart by whatever the scroll was when the menu opened. The
 * frame is scroll-free and drawn last. `font` is FontsIconsArt. */
void sysmenu_overlay(const SysMenu *m, Framebuffer *fb, const TileSet *font);

#endif /* PS2_SYSMENU_H */
