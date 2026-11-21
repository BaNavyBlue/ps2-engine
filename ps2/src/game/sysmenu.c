#include "sysmenu.h"

#include "lang.h"
#include "window.h"
#include "../audio/synth.h"
#include "../core/json.h"
#include "rom_ids.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#else
#include <sys/stat.h>
#define make_dir(path) mkdir(path, 0755)
#endif

/* Frame and lamp tiles, the same the window system uses. */
#define TILE_FILL   0x26
#define TILE_TOP    0xB9
#define TILE_BOTTOM 0xBE
#define TILE_TL     0xB8
#define TILE_TR     0xBA
#define TILE_LEFT   0xBB
#define TILE_RIGHT  0xBC
#define TILE_BL     0xBD
#define TILE_BR     0xBF
#define TILE_LAMP_L 0xB4        /* the unlit housing, two tiles wide */
#define TILE_LAMP_R 0xB5
#define TILE_LAMP   0xB6        /* the lit lamp, a 2x1 sprite         */

#define SFX_SELECTION 0xA0

/* Frame position and size, in plane cells. Interior is COLS - 2 wide;
 * the options sit on frame rows 1, 3, 5, ... so the lamp can move 16
 * pixels an index exactly as the window cursor does, with a row of fill
 * under the last. Centred on the 40x28 screen. Labels start three cells
 * in (the lamp is two), values at VALUE_COL: room for the longest label,
 * COMPUTER PLAYS, and for the longest value, COMPUTER PLAYS's lamp and
 * NEW GAME. */
#define MENU_COLS  32
#define MENU_ROWS  (2 * SYSMENU_ITEMS + 2)
#define MENU_COL   ((SCREEN_W / 8 - MENU_COLS) / 2)
#define MENU_ROW   ((SCREEN_H / 8 - MENU_ROWS) / 2)
#define LABEL_COL  3
#define VALUE_COL  18

#define SETTINGS_PATH_DEFAULT "saves/settings.json"

static char g_settings_path[512] = SETTINGS_PATH_DEFAULT;

/* (build 160) The actions: their names on the CONTROLS and BUTTONS pages,
 * and in settings.json's "keys" and "pad". Until 160 the CONTROLS row
 * showed one of two key sets here, ARROWS ZXC or WASD L;'. */
static const char *const ACTION_NAME[CONTROL_ACTIONS] = {
    "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "C", "START"
};
static const char *const ACTION_KEY[CONTROL_ACTIONS] = {
    "up", "down", "left", "right", "a", "b", "c", "start"
};

const char *sysmenu_action_key(int action)
{
    return (action >= 0 && action < CONTROL_ACTIONS) ? ACTION_KEY[action]
                                                     : NULL;
}

/* The pixel aspect (platform.h): its name in the menu, and its value in
 * settings.json. */
static const char *const DISPLAY_NAME[ASPECT_COUNT] = {
    "SQUARE",
    "4:3",
    "TV",
};
/* (build 120) SPEED's values: settings.json's and the menu's. */
static const char *const SPEED_KEY[COMPUTER_SPEED_COUNT] = {
    "normal", "auto", "2x", "auto_2x", "fast"
};

static const char *const SPEED_NAME[COMPUTER_SPEED_COUNT] = {
    "NORMAL", "AUTO", "2X", "AUTO 2X", "FAST"
};

const char *sysmenu_speed_key(int speed)
{
    return (speed >= 0 && speed < COMPUTER_SPEED_COUNT) ? SPEED_KEY[speed]
                                                        : NULL;
}

const char *sysmenu_speed_name(int speed)
{
    return (speed >= 0 && speed < COMPUTER_SPEED_COUNT) ? SPEED_NAME[speed]
                                                        : NULL;
}

static const char *const DISPLAY_KEY[ASPECT_COUNT] = {
    "square",
    "4:3",
    "tv",
};

void sysmenu_init(SysMenu *m)
{
    memset(m, 0, sizeof *m);
    controls_default(&m->controls);         /* (build 160) */
    m->display  = ASPECT_TV;
    crt_settings_default(&m->crt);
    /* (build 110) The chip at 0 dB: build 109a's mix. */
    m->audio.synth     = SYNTH_CHIP;
    m->audio.master_db = 0;
    m->audio.music_db  = 0;
    m->audio.effects_db = 0;
    m->audio.oversample = 4;
    /* (build 112) Q's switches: 111c's ways, and the patch file on; (116)
     * no grain */
    m->audio.voices  = 0;
    m->audio.tails   = 0;
    m->audio.patches = true;
    m->audio.grain   = false;
    crt_display_default(&m->crt_display);
    m->rumble      = RUMBLE_DEFAULT;        /* (build 160) */
    m->rumble_test = -1;

    /* PalettePtrs entry 2, line 0: the window colours. Kept here and
     * handed to the frame's overlay line, never written into the game's
     * palette. */
    Palette win;
    palette_clear(&win);
    if (palette_load_entry(&win, 0, PALETTE_FIELD_COMMON)) {
        memcpy(m->colours, win.entry, sizeof m->colours);
    } else {
        fprintf(stderr, "sysmenu: no window colours (PALETTE_FIELD_COMMON)\n");
    }
}

void sysmenu_set_settings_path(const char *path)
{
    snprintf(g_settings_path, sizeof g_settings_path, "%s",
             (path != NULL) ? path : SETTINGS_PATH_DEFAULT);
}

/* (build 105) The CRT page's values: their names in the menu and their
 * values in settings.json. */
static const char *const MASK_NAME[CRT_MASK_COUNT] = {
    "OFF", "APERTURE GRILLE", "SLOT MASK", "SHADOW MASK",
};
static const char *const MASK_SHORT[CRT_MASK_COUNT] = {
    "OFF", "GRILLE", "SLOT MASK", "SHADOW MASK",
};
static const char *const MASK_KEY[CRT_MASK_COUNT] = {
    "off", "grille", "slot", "shadow",
};
static const char *const SUBPIXEL_NAME[CRT_SUBPIXELS_COUNT] = {
    "RGB", "BGR", "OTHER",
};
static const char *const SUBPIXEL_KEY[CRT_SUBPIXELS_COUNT] = {
    "rgb", "bgr", "other",
};

/* PEAK's stops, in nits; 0 is AUTO (crt_peak_auto). */
static const int PEAK_STOPS[] = {
    0, 400, 500, 600, 700, 800, 1000, 1200, 1400, 1600, 2000, 2500, 3000,
    4000, 10000,
};
#define PEAK_STOP_COUNT ((int)(sizeof PEAK_STOPS / sizeof PEAK_STOPS[0]))

/* WHITE's: 10 nits apart up to 400, 50 from there to 1000. */
#define WHITE_MIN 50
#define WHITE_MAX 1000

static int lookup_key(const char *value, const char *const *keys, int n,
                      int fallback)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(value, keys[i]) == 0) {
            return i;
        }
    }
    return fallback;
}

static void load_crt(CrtSettings *c, const JsonValue *crt)
{
    c->scanlines = json_bool(crt, "scanlines", c->scanlines);
    /* (build 167) Builds 159 to 166 had "interlaced" too: scanlines were
     * what it drew, so ON ("flicker" is passed over). */
    if (strcmp(json_str(crt, "scanlines", ""), "interlaced") == 0) {
        c->scanlines = true;
    }
    c->mask = (CrtMask)lookup_key(json_str(crt, "mask", MASK_KEY[c->mask]),
                                  MASK_KEY, CRT_MASK_COUNT, c->mask);
    c->subpixels = (CrtSubpixels)lookup_key(
        json_str(crt, "subpixels", SUBPIXEL_KEY[c->subpixels]),
        SUBPIXEL_KEY, CRT_SUBPIXELS_COUNT, c->subpixels);
    c->hdr = json_bool(crt, "hdr", c->hdr);
    /* "peak": "auto" or a number of nits. */
    long peak = json_int(crt, "peak", -1);
    if (peak > 0) {
        c->peak_nits = (int)((peak > 10000) ? 10000 : peak);
    } else if (strcmp(json_str(crt, "peak", "auto"), "auto") == 0) {
        c->peak_nits = 0;
    }
    long white = json_int(crt, "white", c->white_nits);
    if (white < WHITE_MIN) {
        white = WHITE_MIN;
    } else if (white > WHITE_MAX) {
        white = WHITE_MAX;
    }
    c->white_nits = (int)white;
}

/* (build 110) A volume from settings.json: a number of dB, clamped to the
 * page's range (below its bottom, OFF), or "off". Anything else keeps the
 * default. */
static int load_db(const JsonValue *audio, const char *key, int fallback)
{
    const JsonValue *v = json_get(audio, key);

    if (v == NULL) {
        return fallback;
    }
    if (strcmp(json_str(audio, key, ""), "off") == 0) {
        return SYSMENU_DB_OFF;
    }
    if (v->type != JSON_NUMBER) {
        return fallback;
    }
    long db = json_int(audio, key, fallback);
    if (db > SYSMENU_DB_MAX) {
        return SYSMENU_DB_MAX;
    }
    return (db < SYSMENU_DB_MIN) ? SYSMENU_DB_OFF : (int)db;
}

static void load_audio(AudioSettings *au, const JsonValue *audio)
{
    int id = synth_from_name(json_str(audio, "synth", ""));

    if (id >= 0) {
        au->synth = id;
    }
    au->master_db = load_db(audio, "master", au->master_db);
    au->music_db  = load_db(audio, "music", au->music_db);
    au->effects_db = load_db(audio, "effects", au->effects_db);
    /* (111a) 1, 2 or 4; anything else, or none (a file from before 111a),
     * the default 4 */
    int os = (int)json_int(audio, "oversample", 4);
    au->oversample = (os == 1 || os == 2) ? os : 4;
    /* (build 112) "voices": "extra" or "limited", "tails": "fade" or
     * "ring", "patches": true or false; a file from before 112 has none of
     * them and keeps the defaults */
    const char *voices = json_str(audio, "voices", "extra");
    au->voices = (strcmp(voices, "limited") == 0) ? 1 : 0;
    const char *tails = json_str(audio, "tails", "fade");
    au->tails = (strcmp(tails, "ring") == 0) ? 1 : 0;
    au->patches = json_bool(audio, "patches", true);
    /* (build 116) "grain": true or false; a file from before 116, off */
    au->grain = json_bool(audio, "grain", false);
}

/* (build 160) "keys": {"up": ["UP", "W"], ...}: a name a slot, "-" for
 * none. A name no key has keeps that slot's default. */
static void load_keys(Controls *c, const JsonValue *keys)
{
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        const JsonValue *list = json_get(keys, ACTION_KEY[a]);
        if (list == NULL || list->type != JSON_ARRAY) {
            continue;
        }
        for (int k = 0; k < CONTROL_KEYS; k++) {
            const JsonValue *name = json_at(list, k);
            if (name == NULL || name->type != JSON_STRING) {
                continue;
            }
            if (strcmp(name->string, "-") == 0) {
                c->key[a][k] = KEY_NONE;
                continue;
            }
            int code = key_from_name(name->string);
            if (code != KEY_NONE && key_bindable(code)) {
                c->key[a][k] = code;
            } else {
                fprintf(stderr, "sysmenu: no key %s; %s keeps its own\n",
                        name->string, ACTION_KEY[a]);
            }
        }
    }
}

/* (build 160) "pad": {"up": ["DUP", "LSUP"], ...}: a positional name a
 * slot (src/core/keys.c), "-" for none; a name no input has keeps the
 * slot's default. */
static void load_pad(Controls *c, const JsonValue *pad)
{
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        const JsonValue *list = json_get(pad, ACTION_KEY[a]);
        if (list == NULL || list->type != JSON_ARRAY) {
            continue;
        }
        for (int s = 0; s < CONTROL_PAD_SLOTS; s++) {
            const JsonValue *name = json_at(list, s);
            if (name == NULL || name->type != JSON_STRING) {
                continue;
            }
            if (strcmp(name->string, "-") == 0) {
                c->pad[a][s] = PAD_NONE;
                continue;
            }
            int input = pad_input_from_name(name->string);
            if (input != PAD_NONE && pad_input_bindable(input)) {
                c->pad[a][s] = input;
            } else {
                fprintf(stderr, "sysmenu: no gamepad input %s; %s keeps "
                                "its own\n", name->string, ACTION_KEY[a]);
            }
        }
    }
}

void sysmenu_load_settings(SysMenu *m)
{
    /* No file is the ordinary first run, and says nothing. */
    FILE *f = fopen(g_settings_path, "rb");
    if (f == NULL) {
        return;
    }
    fclose(f);

    /* Two keys now, so the JSON reader rather than a string search: a
     * value of one key cannot be mistaken for the other's. */
    JsonValue *root = json_parse_file(g_settings_path);
    if (root == NULL) {
        fprintf(stderr, "sysmenu: %s does not parse; using the defaults\n",
                g_settings_path);
        return;
    }

    /* (build 160) The controls, "keys" and "pad". A file from before 160
     * has "controls": "arrows" or "wasd" instead, one key set, and reads as
     * the defaults, both sets: the key is dropped, saying nothing. */
    const JsonValue *keys = json_get(root, "keys");
    if (keys != NULL) {
        load_keys(&m->controls, keys);
    }
    const JsonValue *pad = json_get(root, "pad");
    if (pad != NULL) {
        load_pad(&m->controls, pad);
    }

    const char *display = json_str(root, "display", DISPLAY_KEY[m->display]);
    for (int i = 0; i < ASPECT_COUNT; i++) {
        if (strcmp(display, DISPLAY_KEY[i]) == 0) {
            m->display = (AspectMode)i;
        }
    }

    /* (build 100) A file from before build 100 has no key: windowed. */
    m->fullscreen = json_bool(root, "fullscreen", false);

    /* (build 100b) COMPUTER PLAYS's own option, its only key; (build 120)
     * any of SPEED's five, an unknown one NORMAL. */
    const char *speed = json_str(root, "computer_speed", "normal");
    m->computer_speed = COMPUTER_SPEED_NORMAL;
    for (int i = 0; i < COMPUTER_SPEED_COUNT; i++) {
        if (strcmp(speed, SPEED_KEY[i]) == 0) {
            m->computer_speed = i;
        }
    }

    /* (build 163) LANGUAGE, by key; a file from before 163 has none, the
     * cartridge's. */
    const char *language = json_str(root, "language", NULL);
    if (language != NULL) {
        int i = lang_find(language);
        if (i >= 0) {
            m->language = i;
        } else {
            fprintf(stderr, "sysmenu: no language %s; %s is read\n",
                    language, lang_key(0));
        }
    }

    /* (build 160) RUMBLE: "off", "low", "medium" or "high"; a file from
     * before 160, or a word not known, keeps MEDIUM. */
    m->rumble = rumble_level_from_key(json_str(root, "rumble", ""),
                                      m->rumble);

    /* (build 105) The CRT page's, under one key. A file without it, or
     * without one of its keys, keeps that default. */
    const JsonValue *crt = json_get(root, "crt");
    if (crt != NULL) {
        load_crt(&m->crt, crt);
    }

    /* (build 110) The AUDIO page's, under one key; a file from before 110
     * has none, and reads as the chip at 0 dB. */
    const JsonValue *audio = json_get(root, "audio");
    if (audio != NULL) {
        load_audio(&m->audio, audio);
    }
    json_free(root);
}

void sysmenu_save_settings(const SysMenu *m)
{
    FILE *f = fopen(g_settings_path, "wb");
    if (f == NULL) {
        /* (build 105) A fresh tree has no saves/ until the first game is
         * saved: make the file's directory and try once more. */
        char dir[sizeof g_settings_path];
        snprintf(dir, sizeof dir, "%s", g_settings_path);
        char *slash = strrchr(dir, '/');
        if (slash != NULL) {
            *slash = '\0';
            make_dir(dir);
            f = fopen(g_settings_path, "wb");
        }
    }
    if (f == NULL) {
        fprintf(stderr, "sysmenu: cannot write %s\n", g_settings_path);
        return;
    }
    /* (build 160) No "controls" now: "keys" and "pad", last. */
    const char *language = lang_key(m->language);
    fprintf(f, "{\n  \"display\": \"%s\",\n"
               "  \"fullscreen\": %s,\n  \"computer_speed\": \"%s\",\n"
               "  \"rumble\": \"%s\",\n  \"language\": \"%s\",\n",
            DISPLAY_KEY[m->display], m->fullscreen ? "true" : "false",
            SPEED_KEY[m->computer_speed], rumble_level_key(m->rumble),
            (language != NULL) ? language : "us");

    /* (build 105) The CRT page's settings. */
    const CrtSettings *c = &m->crt;
    char peak[16];
    if (c->peak_nits > 0) {
        snprintf(peak, sizeof peak, "%d", c->peak_nits);
    } else {
        snprintf(peak, sizeof peak, "\"auto\"");
    }
    fprintf(f, "  \"crt\": {\n    \"scanlines\": %s,\n    \"mask\": \"%s\",\n"
               "    \"subpixels\": \"%s\",\n    \"hdr\": %s,\n"
               "    \"peak\": %s,\n    \"white\": %d\n  },\n",
            c->scanlines ? "true" : "false", MASK_KEY[c->mask],
            SUBPIXEL_KEY[c->subpixels], c->hdr ? "true" : "false", peak,
            c->white_nits);

    /* (build 110) The AUDIO page's: the synth by name (kept even where this
     * build has not got it), each volume in dB or "off". */
    const AudioSettings *au = &m->audio;
    const SynthBackend  *sb = synth_backend((SynthId)au->synth);
    char master[16];
    char music[16];
    char effects[16];
    if (au->master_db <= SYSMENU_DB_OFF) {
        snprintf(master, sizeof master, "\"off\"");
    } else {
        snprintf(master, sizeof master, "%d", au->master_db);
    }
    if (au->music_db <= SYSMENU_DB_OFF) {
        snprintf(music, sizeof music, "\"off\"");
    } else {
        snprintf(music, sizeof music, "%d", au->music_db);
    }
    if (au->effects_db <= SYSMENU_DB_OFF) {
        snprintf(effects, sizeof effects, "\"off\"");
    } else {
        snprintf(effects, sizeof effects, "%d", au->effects_db);
    }
    fprintf(f, "  \"audio\": {\n    \"synth\": \"%s\",\n    \"master\": %s,\n"
               "    \"music\": %s,\n    \"effects\": %s,\n"
               "    \"oversample\": %d,\n    \"voices\": \"%s\",\n"
               "    \"tails\": \"%s\",\n    \"patches\": %s,\n"
               "    \"grain\": %s\n  },\n",
            (sb != NULL) ? sb->name : ((au->synth == SYNTH_Q) ? "q" : "chip"),
            master, music, effects, au->oversample,
            au->voices ? "limited" : "extra", au->tails ? "ring" : "fade",
            au->patches ? "true" : "false", au->grain ? "true" : "false");

    /* (build 160) The controls: each action's two keys by name, then its
     * two gamepad inputs by their positional names, "-" for none. */
    fprintf(f, "  \"keys\": {\n");
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        char k0[16], k1[16];
        key_name(m->controls.key[a][0], k0, sizeof k0);
        key_name(m->controls.key[a][1], k1, sizeof k1);
        fprintf(f, "    \"%s\": [\"%s\", \"%s\"]%s\n", ACTION_KEY[a], k0, k1,
                (a + 1 < CONTROL_ACTIONS) ? "," : "");
    }
    fprintf(f, "  },\n  \"pad\": {\n");
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        char p0[16], p1[16];
        pad_input_name(m->controls.pad[a][0], p0, sizeof p0);
        pad_input_name(m->controls.pad[a][1], p1, sizeof p1);
        fprintf(f, "    \"%s\": [\"%s\", \"%s\"]%s\n", ACTION_KEY[a], p0, p1,
                (a + 1 < CONTROL_ACTIONS) ? "," : "");
    }
    fprintf(f, "  }\n}\n");
    fclose(f);
}

static int wrap_index(int v, int n)
{
    return ((v % n) + n) % n;
}

static void play(void (*sound)(void *, u8), void *ctx, u8 id)
{
    if (sound != NULL) {
        sound(ctx, id);
    }
}

/* The cursor of the page that is up, and its row count. */
static int *page_cursor(SysMenu *m, int *count)
{
    switch (m->page) {
        case SYSMENU_PAGE_CRT: {
            *count = SYSMENU_CRT_ITEMS;
            return &m->crt_cursor;
        }
        case SYSMENU_PAGE_HDR: {
            *count = SYSMENU_HDR_ITEMS;
            return &m->hdr_cursor;
        }
        case SYSMENU_PAGE_AUDIO: {
            *count = SYSMENU_AUDIO_ITEMS;
            return &m->audio_cursor;
        }
        case SYSMENU_PAGE_GAMEPAD: {
            *count = SYSMENU_GAMEPAD_ITEMS;
            return &m->gamepad_cursor;
        }
        case SYSMENU_PAGE_CONTROLS: {
            *count = SYSMENU_CONTROL_ROWS;
            return &m->controls_row;
        }
        case SYSMENU_PAGE_BUTTONS: {
            *count = SYSMENU_CONTROL_ROWS;
            return &m->buttons_row;
        }
        default: {
            *count = SYSMENU_ITEMS;
            return &m->cursor;
        }
    }
}

static void move_cursor(SysMenu *m, int dir)
{
    int  count;
    int *cursor = page_cursor(m, &count);
    int  idx    = *cursor + dir;
    if (idx < 0) {
        idx = count - 1;
    } else if (idx >= count) {
        idx = 0;
    }
    *cursor   = idx;
    m->repeat = 0x1F;
}

/* (build 100b) COMPUTER PLAYS's start: a new game, then each of the
 * player's slots that holds a save, (build 103) then the computer's own
 * last run when it left one, round and round. */
#define COMPUTER_FROM_LAST 5
#define COMPUTER_FROM_N    6

static bool from_exists(const SysMenu *m, int from)
{
    if (from == 0) {
        return true;
    }
    if (from == COMPUTER_FROM_LAST) {
        return m->last_run_exists;
    }
    return m->slot_exists[from - 1];
}

static void step_from(SysMenu *m, int dir)
{
    for (int i = 0; i < COMPUTER_FROM_N; i++) {
        m->computer_from = (m->computer_from + dir + COMPUTER_FROM_N)
                           % COMPUTER_FROM_N;
        if (from_exists(m, m->computer_from)) {
            return;
        }
    }
}

/* (build 163) LANGUAGE a step on, or back, round and round. */
static void step_language(SysMenu *m, int dir)
{
    int n = lang_count();
    if (n <= 1) {
        return;
    }
    m->language = ((m->language + dir) % n + n) % n;
    m->language_changed = true;
}

static void choose(SysMenu *m, void (*sound)(void *, u8), void *ctx)
{
    play(sound, ctx, SFX_SELECTION);

    switch (m->cursor) {
        case SYSMENU_CONTINUE: {
            m->open = false;
            break;
        }
        case SYSMENU_CONTROLS: {
            /* (build 160) The CONTROLS page, its cursor where it was left
             * (until 160 the row stepped between two key sets). */
            m->page = SYSMENU_PAGE_CONTROLS;
            break;
        }
        case SYSMENU_DISPLAY: {
            m->display = (AspectMode)((m->display + 1) % ASPECT_COUNT);
            m->display_changed = true;
            break;
        }
        case SYSMENU_FULLSCREEN: {
            m->fullscreen = !m->fullscreen;
            m->fullscreen_changed = true;
            break;
        }
        case SYSMENU_CRT: {
            /* (build 105) The CRT page, its cursor where it was left. */
            m->page = SYSMENU_PAGE_CRT;
            break;
        }
        case SYSMENU_AUDIO: {
            /* (build 110) The AUDIO page, the same way. */
            m->page = SYSMENU_PAGE_AUDIO;
            break;
        }
        case SYSMENU_COMPUTER: {
            /* (build 100c) The lamp, not a start: the menu's closing
             * starts or stops it (main.c). Out it always goes; lit only
             * with something to run (the value says why not). */
            if (m->computer_on || m->computer_note[0] == '\0') {
                m->computer_on = !m->computer_on;
            }
            break;
        }
        case SYSMENU_SPEED: {
            /* (build 120) On through the five, round to NORMAL. */
            m->computer_speed = (m->computer_speed + 1) % COMPUTER_SPEED_COUNT;
            m->computer_speed_changed = true;
            break;
        }
        case SYSMENU_GAMEPAD: {
            /* (build 160) The GAMEPAD page, its cursor where it was left. */
            m->page = SYSMENU_PAGE_GAMEPAD;
            break;
        }
        case SYSMENU_LANGUAGE: {
            /* (build 163) On through the languages, round to the first. */
            step_language(m, +1);
            break;
        }
        case SYSMENU_QUIT: {
            m->quit = true;
            break;
        }
        default: {
            break;
        }
    }
}

/* ---- (build 105) the CRT and HDR pages ---- */

static int step_peak(int peak, int dir, bool wrap_round)
{
    int at = 0;
    for (int i = 0; i < PEAK_STOP_COUNT; i++) {
        if (PEAK_STOPS[i] <= peak) {
            at = i;
        }
    }
    at += dir;
    if (at >= PEAK_STOP_COUNT) {
        at = wrap_round ? 0 : PEAK_STOP_COUNT - 1;
    } else if (at < 0) {
        at = 0;
    }
    return PEAK_STOPS[at];
}

static int step_white(int white, int dir, bool wrap_round)
{
    int step = (white > 400 || (white == 400 && dir > 0)) ? 50 : 10;
    int next = white + dir * step;
    if (next > WHITE_MAX) {
        next = wrap_round ? WHITE_MIN : WHITE_MAX;
    } else if (next < WHITE_MIN) {
        next = WHITE_MIN;
    }
    return next;
}

/* Left, right or C on a value row: the next value that way. C steps on
 * (wrap_round: past the last number back to the first). */
static void crt_step(SysMenu *m, int dir, bool wrap_round)
{
    CrtSettings *c = &m->crt;
    if (m->page == SYSMENU_PAGE_CRT) {
        switch (m->crt_cursor) {
            case SYSMENU_CRT_SCANLINES: {
                c->scanlines = !c->scanlines;
                break;
            }
            case SYSMENU_CRT_MASK: {
                c->mask = (CrtMask)wrap_index(c->mask + dir, CRT_MASK_COUNT);
                break;
            }
            case SYSMENU_CRT_SUBPIXELS: {
                c->subpixels = (CrtSubpixels)wrap_index(c->subpixels + dir,
                                                        CRT_SUBPIXELS_COUNT);
                break;
            }
            case SYSMENU_CRT_HDR: {
                c->hdr = !c->hdr;
                break;
            }
            default: {
                return;
            }
        }
    } else if (m->page == SYSMENU_PAGE_HDR) {
        switch (m->hdr_cursor) {
            case SYSMENU_HDR_SWITCH: {
                c->hdr = !c->hdr;
                break;
            }
            case SYSMENU_HDR_PEAK: {
                c->peak_nits = step_peak(c->peak_nits, dir, wrap_round);
                break;
            }
            case SYSMENU_HDR_WHITE: {
                c->white_nits = step_white(c->white_nits, dir, wrap_round);
                break;
            }
            default: {
                return;
            }
        }
    } else {
        return;
    }
    m->crt_changed = true;
}

/* ---- (build 110) the AUDIO page ---- */

/* A volume a step up or down: OFF, then SYSMENU_DB_MIN up to 0 dB a dB at a
 * time. Past the top, C (wrap_round) goes back round to OFF; left and right
 * stop at the ends. */
static int step_db(int db, int dir, bool wrap_round)
{
    if (db <= SYSMENU_DB_OFF) {
        return (dir > 0) ? SYSMENU_DB_MIN : SYSMENU_DB_OFF;
    }
    int next = db + dir;
    if (next > SYSMENU_DB_MAX) {
        return wrap_round ? SYSMENU_DB_OFF : SYSMENU_DB_MAX;
    }
    return (next < SYSMENU_DB_MIN) ? SYSMENU_DB_OFF : next;
}

/* (111a) OVERSAMPLE: 1, 2, 4 x. Left and right step and stop at the ends;
 * C (wrap_round) goes on round from 4 to 1. */
static int step_oversample(int os, int dir, bool wrap_round)
{
    static const int STOPS[3] = { 1, 2, 4 };
    int at = (os == 1) ? 0 : ((os == 2) ? 1 : 2);

    at += dir;
    if (at > 2) {
        at = wrap_round ? 0 : 2;
    }
    if (at < 0) {
        at = wrap_round ? 2 : 0;
    }
    return STOPS[at];
}

static void audio_step(SysMenu *m, int dir, bool wrap_round)
{
    AudioSettings *au = &m->audio;

    switch (m->audio_cursor) {
        case SYSMENU_AUDIO_SYNTH: {
            au->synth = wrap_index(au->synth + dir, SYNTH_COUNT);
            break;
        }
        case SYSMENU_AUDIO_MASTER: {
            au->master_db = step_db(au->master_db, dir, wrap_round);
            break;
        }
        case SYSMENU_AUDIO_MUSIC: {
            au->music_db = step_db(au->music_db, dir, wrap_round);
            break;
        }
        case SYSMENU_AUDIO_EFFECTS: {
            au->effects_db = step_db(au->effects_db, dir, wrap_round);
            break;
        }
        case SYSMENU_AUDIO_OVERSAMPLE: {
            au->oversample = step_oversample(au->oversample, dir, wrap_round);
            break;
        }
        case SYSMENU_AUDIO_VOICES: {
            /* (build 112) two values: any step flips it */
            au->voices = !au->voices;
            break;
        }
        case SYSMENU_AUDIO_TAILS: {
            au->tails = !au->tails;
            break;
        }
        case SYSMENU_AUDIO_PATCHES: {
            au->patches = !au->patches;
            break;
        }
        case SYSMENU_AUDIO_GRAIN: {
            /* (build 116) two values, as the three above */
            au->grain = !au->grain;
            break;
        }
        default: {
            return;
        }
    }
    m->audio_changed = true;
}

/* Left, right or C on a page's value row: the page's own step. */
/* ---- (build 160) the GAMEPAD page ---- */

/* RUMBLE a level: left and right stop at OFF and HIGH, C (wrap_round)
 * goes on round from HIGH to OFF. A change buzzes every gamepad at the
 * new level. The PAD rows have no value to step. */
static void gamepad_step(SysMenu *m, int dir, bool wrap_round)
{
    if (m->gamepad_cursor != SYSMENU_GAMEPAD_RUMBLE) {
        return;
    }
    int level = (int)m->rumble + dir;
    if (level >= RUMBLE_LEVELS) {
        level = wrap_round ? RUMBLE_OFF : RUMBLE_HIGH;
    } else if (level < 0) {
        level = RUMBLE_OFF;
    }
    if (level != (int)m->rumble) {
        m->rumble         = (RumbleLevel)level;
        m->rumble_changed = true;
        m->rumble_test    = SYSMENU_PADS;
    }
}

/* ---- (build 160) the CONTROLS and BUTTONS pages ---- */

static bool controls_page(const SysMenu *m)
{
    return m->page == SYSMENU_PAGE_CONTROLS
        || m->page == SYSMENU_PAGE_BUTTONS;
}

/* Left and right pick an action's slot: they stop at the first and the
 * second. */
static bool controls_step(SysMenu *m, int dir)
{
    bool pad = m->page == SYSMENU_PAGE_BUTTONS;
    int *col = pad ? &m->buttons_col : &m->controls_col;
    int  row = pad ? m->buttons_row : m->controls_row;
    int  at  = *col + dir;
    if (row >= CONTROL_ACTIONS || at < 0 || at >= CONTROL_KEYS) {
        return false;
    }
    *col = at;
    return true;
}

/* Choosing on an action's slot listens; on DEFAULTS that page's half of
 * the controls goes back to the defaults. */
static void controls_choose(SysMenu *m)
{
    bool pad = m->page == SYSMENU_PAGE_BUTTONS;
    int  row = pad ? m->buttons_row : m->controls_row;
    if (row < CONTROL_ACTIONS) {
        m->listening    = true;
        m->listen_ticks = 0;
        return;
    }
    if (pad) {
        controls_pad_default(&m->controls);
    } else {
        controls_keys_default(&m->controls);
    }
    m->controls_changed = true;
}

/* Listening: Escape or a gamepad's Back keeps the slot, Backspace empties
 * it, a key binds a CONTROLS slot and a gamepad input a BUTTONS slot (one
 * the platform keeps for itself is not heard); nothing else is. */
static void listen_slot(SysMenu *m, const Input *in,
                        void (*sound)(void *, u8), void *ctx)
{
    bool pad    = m->page == SYSMENU_PAGE_BUTTONS;
    int  action = pad ? m->buttons_row : m->controls_row;
    int  slot   = pad ? m->buttons_col : m->controls_col;

    m->listen_ticks++;
    if (in->pressed.menu) {
        m->listening = false;
        play(sound, ctx, SFX_SELECTION);
        return;
    }
    bool bound = false;
    if (in->back) {
        if (pad) {
            controls_bind_pad(&m->controls, action, slot, PAD_NONE);
        } else {
            controls_bind_key(&m->controls, action, slot, KEY_NONE);
        }
        bound = true;
    } else if (!pad && key_bindable(in->key)) {
        controls_bind_key(&m->controls, action, slot, in->key);
        bound = true;
    } else if (pad && pad_input_bindable(in->pad_input)) {
        controls_bind_pad(&m->controls, action, slot, in->pad_input);
        bound = true;
    }
    if (bound) {
        m->controls_changed = true;
        m->listening        = false;
        play(sound, ctx, SFX_SELECTION);
    }
}

static void page_step(SysMenu *m, int dir, bool wrap_round)
{
    if (m->page == SYSMENU_PAGE_AUDIO) {
        audio_step(m, dir, wrap_round);
    } else if (m->page == SYSMENU_PAGE_GAMEPAD) {
        gamepad_step(m, dir, wrap_round);
    } else {
        crt_step(m, dir, wrap_round);
    }
}

/* C, A or Start on a page: the main menu's rows as before; on the CRT page
 * the HDR row opens its page and every other row steps on. */
static void choose_on_page(SysMenu *m, void (*sound)(void *, u8), void *ctx)
{
    if (m->page == SYSMENU_PAGE_MAIN) {
        choose(m, sound, ctx);
        return;
    }
    play(sound, ctx, SFX_SELECTION);
    if (m->page == SYSMENU_PAGE_CRT && m->crt_cursor == SYSMENU_CRT_HDR) {
        m->page = SYSMENU_PAGE_HDR;
        return;
    }
    /* (build 160) A PAD row buzzes its gamepad, when there is one; BUTTONS
     * opens its page. */
    if (m->page == SYSMENU_PAGE_GAMEPAD
        && m->gamepad_cursor == SYSMENU_GAMEPAD_BUTTONS) {
        m->page = SYSMENU_PAGE_BUTTONS;
        return;
    }
    if (m->page == SYSMENU_PAGE_GAMEPAD
        && m->gamepad_cursor != SYSMENU_GAMEPAD_RUMBLE) {
        if (m->pad_name[m->gamepad_cursor][0] != '\0') {
            m->rumble_test = m->gamepad_cursor;
        }
        return;
    }
    /* (build 160) The CONTROLS and BUTTONS pages: listen, or DEFAULTS. */
    if (controls_page(m)) {
        controls_choose(m);
        return;
    }
    page_step(m, +1, true);
}

/* (build 160) What works the menu: the bound buttons, and its own -- the
 * arrows, Enter and every gamepad's d-pad and left stick (Input's nav),
 * whatever is bound. */
static void menu_buttons(const Input *in, Buttons *held, Buttons *pressed)
{
    *held    = in->held;
    *pressed = in->pressed;
    const bool *nh = (const bool *)&in->nav_held;
    const bool *np = (const bool *)&in->nav_pressed;
    bool       *h  = (bool *)held;
    bool       *p  = (bool *)pressed;
    for (size_t i = 0; i < sizeof *held / sizeof(bool); i++) {
        h[i] = h[i] || nh[i];
        p[i] = p[i] || np[i];
    }
}

bool sysmenu_tick(SysMenu *m, const Input *in,
                  void (*sound)(void *ctx, u8 id), void *ctx)
{
    if (!m->open) {
        if (in->pressed.menu) {
            m->open        = true;
            m->cursor      = 0;
            m->page        = SYSMENU_PAGE_MAIN;
            m->blink       = 7;
            m->lamp_hidden = false;
            m->repeat      = 0x1F;
            m->listening   = false;
            play(sound, ctx, SFX_SELECTION);
            return true;
        }
        return false;
    }

    /* (build 160) A CONTROLS or BUTTONS slot listening hears nothing else. */
    if (m->listening) {
        listen_slot(m, in, sound, ctx);
        return true;
    }

    Buttons held, pressed;
    menu_buttons(in, &held, &pressed);

    /* Escape or B goes back a page from the CRT and HDR pages, and closes
     * the menu itself (The owner, build 105). C, A or Start picks.
     * (build 160) BUTTONS goes back to the GAMEPAD page. */
    if (pressed.menu || pressed.b) {
        play(sound, ctx, SFX_SELECTION);
        if (m->page == SYSMENU_PAGE_HDR) {
            m->page = SYSMENU_PAGE_CRT;
        } else if (m->page == SYSMENU_PAGE_BUTTONS) {
            m->page = SYSMENU_PAGE_GAMEPAD;
        } else if (m->page == SYSMENU_PAGE_CRT
                   || m->page == SYSMENU_PAGE_AUDIO
                   || m->page == SYSMENU_PAGE_GAMEPAD
                   || m->page == SYSMENU_PAGE_CONTROLS) {
            m->page = SYSMENU_PAGE_MAIN;
        } else {
            m->open = false;
        }
        return true;
    }

    /* The lamp blinks on ObjRRC_Main's seven-frame timer. */
    m->blink--;
    if (m->blink < 0) {
        m->blink       = 7;
        m->lamp_hidden = !m->lamp_hidden;
    }

    if (pressed.up) {
        move_cursor(m, -1);
    } else if (pressed.down) {
        move_cursor(m, +1);
    } else if (held.up) {
        if (--m->repeat < 0) {
            move_cursor(m, -1);
        }
    } else if (held.down) {
        if (--m->repeat < 0) {
            move_cursor(m, +1);
        }
    } else {
        m->repeat = 0x1F;
    }

    if (m->page == SYSMENU_PAGE_MAIN) {
        if (m->cursor == SYSMENU_COMPUTER
            && (pressed.left || pressed.right)) {
            step_from(m, pressed.right ? +1 : -1);
            play(sound, ctx, SFX_SELECTION);
        }
        /* (build 163) LANGUAGE steps both ways too. */
        if (m->cursor == SYSMENU_LANGUAGE
            && (pressed.left || pressed.right)) {
            step_language(m, pressed.right ? +1 : -1);
            play(sound, ctx, SFX_SELECTION);
        }
    } else if (controls_page(m)) {
        /* (build 160) Left and right pick the slot, a press a step. */
        if ((pressed.left || pressed.right)
            && controls_step(m, pressed.right ? +1 : -1)) {
            play(sound, ctx, SFX_SELECTION);
        }
    } else if (pressed.left || pressed.right) {
        /* (build 105) Left and right step a CRT or HDR value, (110) or an
         * AUDIO one; held, they repeat after the cursor's delay, four
         * frames apart. */
        page_step(m, pressed.right ? +1 : -1, false);
        play(sound, ctx, SFX_SELECTION);
        m->side_repeat = 0x1F;
    } else if (held.left || held.right) {
        if (--m->side_repeat < 0) {
            page_step(m, held.right ? +1 : -1, false);
            play(sound, ctx, SFX_SELECTION);
            m->side_repeat = 3;
        }
    }

    if (pressed.c || pressed.a || pressed.start) {
        choose_on_page(m, sound, ctx);
    }
    return true;
}

/* ---- (build 105) what the CRT rows show ---- */

void sysmenu_crt_value(const SysMenu *m, SysMenuValue row, char *value,
                       size_t value_n, char *why, size_t why_n)
{
    const CrtSettings *c   = &m->crt;
    const CrtDisplay  *d   = &m->crt_display;
    double             par = crt_pixel_aspect((int)m->display);
    CrtWhy             w   = CRT_OK;

    switch (row) {
        case SYSMENU_VALUE_SUMMARY: {
            /* What will be drawn: the mask when it can be, else the
             * scanlines when they can be, else the flat picture. */
            const char *what = "OFF";
            if (c->mask != CRT_MASK_OFF
                && crt_mask_why(d, par, c->mask) == CRT_OK) {
                what = MASK_SHORT[c->mask];
            } else if (c->scanlines
                       && crt_scanlines_why(d, par) == CRT_OK) {
                what = "SCANLINES";
            }
            snprintf(value, value_n, "%s", what);
            break;
        }
        case SYSMENU_VALUE_SCANLINES: {
            snprintf(value, value_n, "%s", c->scanlines ? "ON" : "OFF");
            w = c->scanlines ? crt_scanlines_why(d, par) : CRT_OK;
            break;
        }
        case SYSMENU_VALUE_MASK: {
            snprintf(value, value_n, "%s", MASK_NAME[c->mask]);
            w = crt_mask_why(d, par, c->mask);
            break;
        }
        case SYSMENU_VALUE_SUBPIXELS: {
            snprintf(value, value_n, "%s", SUBPIXEL_NAME[c->subpixels]);
            break;
        }
        case SYSMENU_VALUE_HDR: {
            snprintf(value, value_n, "%s", c->hdr ? "ON" : "OFF");
            w = c->hdr ? crt_hdr_why(d) : CRT_OK;
            break;
        }
        case SYSMENU_VALUE_PEAK: {
            if (c->peak_nits > 0) {
                snprintf(value, value_n, "%d NITS", c->peak_nits);
            } else {
                snprintf(value, value_n, "AUTO %d NITS", crt_peak_auto(d));
            }
            break;
        }
        case SYSMENU_VALUE_WHITE: {
            snprintf(value, value_n, "%d NITS", c->white_nits);
            break;
        }
        default: {
            snprintf(value, value_n, "?");
            break;
        }
    }
    snprintf(why, why_n, "%s", crt_why_text(w));
}

/* ---- (build 110) what the AUDIO rows show ---- */

int sysmenu_audio_synth_playing(const SysMenu *m)
{
    return (synth_backend((SynthId)m->audio.synth) != NULL) ? m->audio.synth
                                                            : SYNTH_CHIP;
}

static void db_text(int db, char *value, size_t value_n)
{
    if (db <= SYSMENU_DB_OFF) {
        snprintf(value, value_n, "OFF");
    } else {
        snprintf(value, value_n, "%d DB", db);
    }
}

void sysmenu_audio_value(const SysMenu *m, SysMenuAudioValue row, char *value,
                         size_t value_n, char *why, size_t why_n)
{
    const AudioSettings *au = &m->audio;
    const SynthBackend  *sb = synth_backend((SynthId)au->synth);

    why[0] = '\0';
    switch (row) {
        case SYSMENU_AUDIO_VALUE_SUMMARY: {
            /* What plays: the chosen synth where it is built, else the
             * chip. */
            const SynthBackend *on =
                synth_backend((SynthId)sysmenu_audio_synth_playing(m));
            snprintf(value, value_n, "%s", (on != NULL) ? on->label : "?");
            break;
        }
        case SYSMENU_AUDIO_VALUE_SYNTH: {
            snprintf(value, value_n, "%s", (sb != NULL) ? sb->label
                     : ((au->synth == SYNTH_Q) ? "Q" : "?"));
            if (sb == NULL) {
                snprintf(why, why_n, "NOT BUILT");
            }
            break;
        }
        case SYSMENU_AUDIO_VALUE_MASTER: {
            db_text(au->master_db, value, value_n);
            break;
        }
        case SYSMENU_AUDIO_VALUE_MUSIC: {
            db_text(au->music_db, value, value_n);
            break;
        }
        case SYSMENU_AUDIO_VALUE_EFFECTS: {
            db_text(au->effects_db, value, value_n);
            break;
        }
        case SYSMENU_AUDIO_VALUE_OVERSAMPLE: {
            /* (111a) It is Q's: under it, why it does nothing while the
             * chip plays. */
            snprintf(value, value_n, "%dX", au->oversample);
            if (sysmenu_audio_synth_playing(m) != SYNTH_Q) {
                snprintf(why, why_n, "Q ONLY");
            }
            break;
        }
        case SYSMENU_AUDIO_VALUE_VOICES: {
            /* (build 112) Q's, as OVERSAMPLE; (build 124) both synths':
             * the chip's second chip (docs/second_chip.md) */
            snprintf(value, value_n, "%s", au->voices ? "LIMITED" : "EXTRA");
            break;
        }
        case SYSMENU_AUDIO_VALUE_TAILS: {
            snprintf(value, value_n, "%s", au->tails ? "RING" : "FADE");
            if (sysmenu_audio_synth_playing(m) != SYNTH_Q) {
                snprintf(why, why_n, "Q ONLY");
            }
            break;
        }
        case SYSMENU_AUDIO_VALUE_PATCHES: {
            snprintf(value, value_n, "%s", au->patches ? "ON" : "OFF");
            if (sysmenu_audio_synth_playing(m) != SYNTH_Q) {
                snprintf(why, why_n, "Q ONLY");
            }
            break;
        }
        case SYSMENU_AUDIO_VALUE_GRAIN: {
            /* (build 116) Q's too: the chip has its grain always */
            snprintf(value, value_n, "%s", au->grain ? "ON" : "OFF");
            if (sysmenu_audio_synth_playing(m) != SYNTH_Q) {
                snprintf(why, why_n, "Q ONLY");
            }
            break;
        }
        default: {
            snprintf(value, value_n, "?");
            break;
        }
    }
}

/* ---- (build 160) the gamepads' names ---- */

void sysmenu_set_pad_name(SysMenu *m, int pad, const char *name)
{
    if (pad < 0 || pad >= SYSMENU_PADS) {
        return;
    }
    char *out = m->pad_name[pad];
    int   n   = 0;
    for (; name != NULL && name[n] != '\0' && n < SYSMENU_PAD_NAME; n++) {
        char ch = name[n];
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 'a' + 'A');
        }
        out[n] = (win_charset(ch) != 0) ? ch : ' ';
    }
    /* No trailing spaces (a name cut at one); a name of nothing the font
     * has is still a gamepad. */
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    if (n == 0 && name != NULL) {
        snprintf(out, SYSMENU_PAD_NAME + 1, "GAMEPAD");
        return;
    }
    out[n] = '\0';
}

void sysmenu_set_pad_style(SysMenu *m, PadStyle style)
{
    m->pad_style = (style >= 0 && style < PAD_STYLES) ? style
                                                      : PAD_STYLE_NONE;
}

void sysmenu_slot_text(const SysMenu *m, bool pad, int action, int slot,
                       char *out, size_t n)
{
    if (action < 0 || action >= CONTROL_ACTIONS || slot < 0
        || slot >= CONTROL_KEYS) {
        snprintf(out, n, "?");
        return;
    }
    if (pad) {
        pad_input_label(m->controls.pad[action][slot], m->pad_style, out, n);
    } else {
        key_name(m->controls.key[action][slot], out, n);
    }
}

int sysmenu_pads(const SysMenu *m)
{
    int count = 0;
    for (int i = 0; i < SYSMENU_PADS; i++) {
        if (m->pad_name[i][0] != '\0') {
            count++;
        }
    }
    return count;
}

/* ---- drawing ---- */

/* One 8x8 tile of the font into the frame, index 0 transparent, in the
 * overlay line (video.h). (build 140) And its colour into the colour
 * plane, from the overlay's colours (sysmenu_overlay sets them first). */
static void blit_tile(Framebuffer *fb, const TileSet *font, int tile,
                      int x0, int y0)
{
    if (tile < 0 || tile >= font->count) {
        return;
    }
    const u8 *px = font->tiles[tile].px;
    for (int y = 0; y < 8; y++) {
        int py = y0 + y;
        if (py < 0 || py >= SCREEN_H) {
            continue;
        }
        for (int x = 0; x < 8; x++) {
            int p = px[y * 8 + x];
            int fx = x0 + x;
            if (p != 0 && fx >= 0 && fx < SCREEN_W) {
                fb->px[py * SCREEN_W + fx]  = (u8)(FB_OVERLAY_BASE + p);
                fb->rgb[py * SCREEN_W + fx] = cram_to_rgb8(fb->overlay[p]);
                fb->pic[py * SCREEN_W + fx] = 0;    /* (build 142) */
            }
        }
    }
}

static void put(Framebuffer *fb, const TileSet *font, int col, int row,
                u8 tile)
{
    blit_tile(fb, font, tile, col * 8, row * 8);
}

static void put_text(Framebuffer *fb, const TileSet *font, int col, int row,
                     const char *text)
{
    for (; *text != '\0'; text++, col++) {
        u8 upper, lower;
        u8 code = win_charset(*text);
        if (code == 0) {
            continue;               /* space: leave the fill */
        }
        win_glyph(code, &upper, &lower);
        put(fb, font, col, row, lower);
    }
}

/* A window frame of cols x rows cells at (col, row), filled. */
static void put_frame(Framebuffer *fb, const TileSet *font, int col0,
                      int row0, int cols, int rows)
{
    for (int r = 0; r < rows; r++) {
        bool top    = (r == 0);
        bool bottom = (r == rows - 1);
        for (int c = 0; c < cols; c++) {
            u8 tile;
            if (c == 0) {
                tile = top ? TILE_TL : (bottom ? TILE_BL : TILE_LEFT);
            } else if (c == cols - 1) {
                tile = top ? TILE_TR : (bottom ? TILE_BR : TILE_RIGHT);
            } else if (top) {
                tile = TILE_TOP;
            } else if (bottom) {
                tile = TILE_BOTTOM;
            } else {
                tile = TILE_FILL;
            }
            put(fb, font, col0 + c, row0 + r, tile);
        }
    }
}

/* The unlit housing, and the lit lamp over it when `lit`. */
static void put_lamp(Framebuffer *fb, const TileSet *font, int col, int row,
                     bool lit)
{
    put(fb, font, col, row, TILE_LAMP_L);
    put(fb, font, col + 1, row, TILE_LAMP_R);
    if (lit) {
        put(fb, font, col, row, TILE_LAMP);
        put(fb, font, col + 1, row, TILE_LAMP + 1);
    }
}

/* (build 105) The CRT and HDR pages: their own window, wider than the
 * menu's (NEEDS FULLSCREEN is sixteen), a title row, then the rows three
 * apart -- the value, why it cannot be had on the line under it, a line of
 * fill. Labels three cells in, values at PAGE_VALUE_COL. (build 116) The
 * AUDIO page's rows two apart, as the menu's own: why a value cannot be had
 * (Q ONLY, NOT BUILT) beside it at AUDIO_WHY_COL, a line of fill under the
 * row -- at three apart its eight rows took 27 of the screen's 28, and a
 * ninth would not fit. */
#define PAGE_COLS      36
#define PAGE_VALUE_COL 14
#define PAGE_PITCH     3
#define AUDIO_PITCH    2
#define AUDIO_WHY_COL  23

static void overlay_page(const SysMenu *m, Framebuffer *fb,
                         const TileSet *font)
{
    static const char *const CRT_LABEL[SYSMENU_CRT_ITEMS] = {
        "SCANLINES", "MASK", "SUBPIXELS", "HDR"
    };
    static const SysMenuValue CRT_ROW[SYSMENU_CRT_ITEMS] = {
        SYSMENU_VALUE_SCANLINES, SYSMENU_VALUE_MASK,
        SYSMENU_VALUE_SUBPIXELS, SYSMENU_VALUE_HDR
    };
    static const char *const HDR_LABEL[SYSMENU_HDR_ITEMS] = {
        "HDR", "PEAK", "WHITE"
    };
    static const SysMenuValue HDR_ROW[SYSMENU_HDR_ITEMS] = {
        SYSMENU_VALUE_HDR, SYSMENU_VALUE_PEAK, SYSMENU_VALUE_WHITE
    };

    /* (build 110) The AUDIO page: its rows by sysmenu_audio_value. */
    static const char *const AUDIO_LABEL[SYSMENU_AUDIO_ITEMS] = {
        "SYNTH", "MASTER", "MUSIC", "EFFECTS", "OVERSAMPLE", "VOICES",
        "TAILS", "PATCHES", "GRAIN"
    };
    static const SysMenuAudioValue AUDIO_ROW[SYSMENU_AUDIO_ITEMS] = {
        SYSMENU_AUDIO_VALUE_SYNTH, SYSMENU_AUDIO_VALUE_MASTER,
        SYSMENU_AUDIO_VALUE_MUSIC, SYSMENU_AUDIO_VALUE_EFFECTS,
        SYSMENU_AUDIO_VALUE_OVERSAMPLE, SYSMENU_AUDIO_VALUE_VOICES,
        SYSMENU_AUDIO_VALUE_TAILS, SYSMENU_AUDIO_VALUE_PATCHES,
        SYSMENU_AUDIO_VALUE_GRAIN
    };

    bool crt   = (m->page == SYSMENU_PAGE_CRT);
    bool audio = (m->page == SYSMENU_PAGE_AUDIO);
    int  n     = audio ? SYSMENU_AUDIO_ITEMS
               : (crt ? SYSMENU_CRT_ITEMS : SYSMENU_HDR_ITEMS);
    const char *const  *label = audio ? AUDIO_LABEL
                              : (crt ? CRT_LABEL : HDR_LABEL);
    const SysMenuValue *rowv  = crt ? CRT_ROW : HDR_ROW;
    int cursor = audio ? m->audio_cursor
               : (crt ? m->crt_cursor : m->hdr_cursor);
    int pitch  = audio ? AUDIO_PITCH : PAGE_PITCH;

    /* The frame, the title, a line of fill, the rows; at three apart the
     * last row's fill line is the frame's bottom, at two it has its own. */
    int rows = audio ? AUDIO_PITCH * n + 4 : PAGE_PITCH * n + 3;
    int col0 = (SCREEN_W / 8 - PAGE_COLS) / 2;
    int row0 = (SCREEN_H / 8 - rows) / 2;
    put_frame(fb, font, col0, row0, PAGE_COLS, rows);

    const int ix = col0 + 1;
    const int iy = row0 + 1;
    put_text(fb, font, ix + LABEL_COL, iy,
             audio ? "AUDIO" : (crt ? "CRT" : "HDR"));
    for (int i = 0; i < n; i++) {
        int  row = iy + 2 + pitch * i;
        char value[24], why[24];
        if (audio) {
            sysmenu_audio_value(m, AUDIO_ROW[i], value, sizeof value, why,
                                sizeof why);
        } else {
            sysmenu_crt_value(m, rowv[i], value, sizeof value, why,
                              sizeof why);
        }
        put_lamp(fb, font, ix, row, false);
        put_text(fb, font, ix + LABEL_COL, row, label[i]);
        put_text(fb, font, ix + PAGE_VALUE_COL, row, value);
        /* Why the chosen value cannot be had: beside it on the AUDIO page,
         * on the line under it on the CRT and HDR pages. */
        if (audio) {
            put_text(fb, font, ix + AUDIO_WHY_COL, row, why);
        } else {
            put_text(fb, font, ix + PAGE_VALUE_COL, row + 1, why);
        }
    }
    if (!m->lamp_hidden) {
        put_lamp(fb, font, ix, iy + 2 + pitch * cursor, true);
    }
}

/* (build 160) The GAMEPAD page: the CRT page's window, a title, PAD 1,
 * PAD 2, RUMBLE and BUTTONS two apart as the AUDIO page's rows, and under
 * them what choosing a PAD does, or that there is nothing to feel. */
static void overlay_gamepad(const SysMenu *m, Framebuffer *fb,
                            const TileSet *font)
{
    static const char *const LABEL[SYSMENU_GAMEPAD_ITEMS] = {
        "PAD 1", "PAD 2", "RUMBLE", "BUTTONS"
    };
    int rows = AUDIO_PITCH * SYSMENU_GAMEPAD_ITEMS + 6;
    int col0 = (SCREEN_W / 8 - PAGE_COLS) / 2;
    int row0 = (SCREEN_H / 8 - rows) / 2;
    put_frame(fb, font, col0, row0, PAGE_COLS, rows);

    const int ix = col0 + 1;
    const int iy = row0 + 1;
    put_text(fb, font, ix + LABEL_COL, iy, "GAMEPAD");
    for (int i = 0; i < SYSMENU_GAMEPAD_ITEMS; i++) {
        int row = iy + 2 + AUDIO_PITCH * i;
        const char *value = rumble_level_name(m->rumble);
        if (i == SYSMENU_GAMEPAD_BUTTONS) {
            value = "";                     /* (build 160) a page */
        } else if (i != SYSMENU_GAMEPAD_RUMBLE) {
            value = (m->pad_name[i][0] != '\0') ? m->pad_name[i] : "NONE";
        }
        put_lamp(fb, font, ix, row, false);
        put_text(fb, font, ix + LABEL_COL, row, LABEL[i]);
        put_text(fb, font, ix + PAGE_VALUE_COL, row, value);
    }
    put_text(fb, font, ix + LABEL_COL,
             iy + 2 + AUDIO_PITCH * SYSMENU_GAMEPAD_ITEMS,
             (sysmenu_pads(m) == 0) ? "NO GAMEPAD CONNECTED"
                                    : "C ON A PAD: IT BUZZES");
    if (!m->lamp_hidden) {
        put_lamp(fb, font, ix, iy + 2 + AUDIO_PITCH * m->gamepad_cursor, true);
    }
}

/* (build 160) The CONTROLS and BUTTONS pages: a window nearly the
 * screen's size, the title, the slots' headings, a row an action -- its
 * name, then each slot's lamp housing and its key or input (the lit lamp
 * at the cursor's; the slot listening blinks, 16 ticks on and 16 off) --
 * DEFAULTS, and under it while a slot listens what to press: every line
 * two apart, as the font's letters fill their cell. Ten-character labels
 * fit each slot (DPAD RIGHT, AXIS RIGHT). */
#define CTRL_COLS   38
#define CTRL_ROWS   27
#define CTRL_SLOT0  12      /* the first slot's text; its lamp 3 before */
#define CTRL_PITCH  14      /* from one slot to the next */

static void overlay_controls(const SysMenu *m, Framebuffer *fb,
                             const TileSet *font)
{
    bool pad  = m->page == SYSMENU_PAGE_BUTTONS;
    int  row  = pad ? m->buttons_row : m->controls_row;
    int  col  = pad ? m->buttons_col : m->controls_col;
    int  col0 = (SCREEN_W / 8 - CTRL_COLS) / 2;
    int  row0 = (SCREEN_H / 8 - CTRL_ROWS) / 2;
    put_frame(fb, font, col0, row0, CTRL_COLS, CTRL_ROWS);

    const int ix = col0 + 1;
    const int iy = row0 + 1;
    bool lit = m->listening || !m->lamp_hidden;
    put_text(fb, font, ix + LABEL_COL, iy,
             pad ? "GAMEPAD BUTTONS" : "CONTROLS");
    put_text(fb, font, ix + CTRL_SLOT0, iy + 2, pad ? "1ST" : "KEY 1");
    put_text(fb, font, ix + CTRL_SLOT0 + CTRL_PITCH, iy + 2,
             pad ? "2ND" : "KEY 2");
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        int y = iy + 4 + 2 * a;
        put_text(fb, font, ix + LABEL_COL, y, ACTION_NAME[a]);
        for (int s = 0; s < CONTROL_KEYS; s++) {
            int  x      = ix + CTRL_SLOT0 + CTRL_PITCH * s;
            bool cursor = a == row && s == col;
            put_lamp(fb, font, x - 3, y, cursor && lit);
            if (cursor && m->listening && (m->listen_ticks & 0x10) != 0) {
                continue;
            }
            char text[16];
            sysmenu_slot_text(m, pad, a, s, text, sizeof text);
            put_text(fb, font, x, y, text);
        }
    }
    int dy = iy + 4 + 2 * CONTROL_ACTIONS;
    put_lamp(fb, font, ix, dy, row == CONTROL_ACTIONS && lit);
    put_text(fb, font, ix + LABEL_COL, dy, "DEFAULTS");
    if (m->listening) {
        put_text(fb, font, ix + LABEL_COL, dy + 2,
                 pad ? "PRESS A PAD BUTTON" : "PRESS A KEY");
        put_text(fb, font, ix + LABEL_COL, dy + 4,
                 "ESC: KEEP   BACKSPC: NONE");
    }
}

void sysmenu_overlay(const SysMenu *m, Framebuffer *fb, const TileSet *font)
{
    if (!m->open) {
        return;
    }
    memcpy(fb->overlay, m->colours, sizeof fb->overlay);

    if (m->page == SYSMENU_PAGE_GAMEPAD) {
        overlay_gamepad(m, fb, font);
        return;
    }
    if (controls_page(m)) {
        overlay_controls(m, fb, font);
        return;
    }
    if (m->page != SYSMENU_PAGE_MAIN) {
        overlay_page(m, fb, font);
        return;
    }

    put_frame(fb, font, MENU_COL, MENU_ROW, MENU_COLS, MENU_ROWS);

    /* Interior column 0 is frame column 1. */
    const int ix = MENU_COL + 1;
    const int iy = MENU_ROW + 1;
    static const char *const LABEL[SYSMENU_ITEMS] = {
        "CONTINUE", "CONTROLS", "DISPLAY", "FULLSCREEN", "CRT", "AUDIO",
        "COMPUTER PLAYS", "SPEED", "GAMEPAD", "LANGUAGE", "QUIT"
    };
    for (int i = 0; i < SYSMENU_ITEMS; i++) {
        int row = iy + 2 * i;
        put_lamp(fb, font, ix, row, false);
        /* SPEED is COMPUTER PLAYS's: set in under it. */
        put_text(fb, font, ix + LABEL_COL + ((i == SYSMENU_SPEED) ? 2 : 0),
                 row, LABEL[i]);
    }
    /* (build 160) CONTROLS is a page now, and shows no value. */
    put_text(fb, font, ix + VALUE_COL, iy + 2 * SYSMENU_DISPLAY,
             DISPLAY_NAME[m->display]);
    put_text(fb, font, ix + VALUE_COL, iy + 2 * SYSMENU_FULLSCREEN,
             m->fullscreen ? "ON" : "OFF");

    /* (build 105) CRT: what will be drawn (the page has the rest). */
    char crt_value[24], crt_why[24];
    sysmenu_crt_value(m, SYSMENU_VALUE_SUMMARY, crt_value, sizeof crt_value,
                      crt_why, sizeof crt_why);
    put_text(fb, font, ix + VALUE_COL, iy + 2 * SYSMENU_CRT, crt_value);

    /* (build 110) AUDIO: the synth that plays (the page has the rest). */
    char audio_value[24], audio_why[24];
    sysmenu_audio_value(m, SYSMENU_AUDIO_VALUE_SUMMARY, audio_value,
                        sizeof audio_value, audio_why, sizeof audio_why);
    put_text(fb, font, ix + VALUE_COL, iy + 2 * SYSMENU_AUDIO, audio_value);

    char from[16];
    if (m->computer_note[0] != '\0') {
        snprintf(from, sizeof from, "%s", m->computer_note);
    } else if (m->computer_from == 0) {
        snprintf(from, sizeof from, "NEW GAME");
    } else if (m->computer_from == COMPUTER_FROM_LAST) {
        snprintf(from, sizeof from, "LAST RUN");
    } else {
        snprintf(from, sizeof from, "SLOT %d", m->computer_from);
    }
    /* (build 100c) Its lamp: the menu's own unlit housing, lit (the
     * cursor's lamp, not blinking) while the computer is to play. */
    int crow = iy + 2 * SYSMENU_COMPUTER;
    put_lamp(fb, font, ix + VALUE_COL, crow, m->computer_on);
    put_text(fb, font, ix + VALUE_COL + 3, crow, from);
    put_text(fb, font, ix + VALUE_COL, iy + 2 * SYSMENU_SPEED,
             SPEED_NAME[m->computer_speed]);

    /* (build 160) GAMEPAD: how many are connected (the page names them). */
    char pads[16];
    int  count = sysmenu_pads(m);
    if (count == 0) {
        snprintf(pads, sizeof pads, "NONE");
    } else {
        snprintf(pads, sizeof pads, "%d PAD%s", count, (count > 1) ? "S" : "");
    }
    put_text(fb, font, ix + VALUE_COL, iy + 2 * SYSMENU_GAMEPAD, pads);

    /* (build 163) LANGUAGE: its name, in the stock font as the whole menu
     * is in every language. */
    const char *language = lang_name(m->language);
    put_text(fb, font, ix + VALUE_COL, iy + 2 * SYSMENU_LANGUAGE,
             (language != NULL) ? language : "");

    if (!m->lamp_hidden) {
        put_lamp(fb, font, ix, iy + 2 * m->cursor, true);
    }
}
