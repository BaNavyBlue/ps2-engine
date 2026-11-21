/* (build 100b) The route language's interpreter: every command of
 * `route_probe`, run against the host in probe.h. Moved here from
 * tools/route_probe.c unchanged but for the host's tick, log, PNG and
 * stderr hooks; the language itself is documented at the top of
 * tools/route_probe.c and in docs/harness.md. */
#define _POSIX_C_SOURCE 200809L

#include "probe.h"

#include "../core/assetfs.h"
#include "../core/audio.h"
#include "../core/video.h"
#include "../game/game.h"
#include "../game/gamedata.h"
#include "../game/lang.h"
#include "../game/walk.h"
#include "../game/window.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_LIMIT  3600
#define BLOCK_GRACE    120      /* ticks a walk may stand blocked */
#define LINE_MAX_LEN   PROBE_LINE_MAX
#define ARGS_MAX       PROBE_ARGS_MAX
#define PATH_LEN       PROBE_PATH_LEN
#define OUT_LEN        PROBE_OUT_LEN

typedef ProbeCmd   Cmd;
typedef ProbeRoute Route;
/* ------------------------------------------------------------------ */
/* The script                                                         */
/* ------------------------------------------------------------------ */


static const char *const BUTTON_NAME[] = {
    "a", "b", "c", "start", "up", "down", "left", "right", "esc"
};
static const size_t BUTTON_OFFSET[] = {
    offsetof(Buttons, a), offsetof(Buttons, b), offsetof(Buttons, c),
    offsetof(Buttons, start), offsetof(Buttons, up), offsetof(Buttons, down),
    offsetof(Buttons, left), offsetof(Buttons, right), offsetof(Buttons, menu)
};
#define BUTTONS_N ((int)(sizeof BUTTON_NAME / sizeof BUTTON_NAME[0]))

/* The four directions are BUTTON_UP..BUTTON_UP + 3; Escape (build 100,
 * index 8) comes after them, and is no pad button. */
#define BUTTON_UP  4

static const char *const SCREEN_NAME[SCREEN_COUNT] = {
    "sega", "title", "intro", "level", "building", "battle", "gameover",
    "ending"
};

/* battle_debug_commands' order: the low two bits of a command word. */
static const char *const COMMAND_NAME[4] = {
    "attack", "tech", "item", "defend"
};

static const char *const WAIT_NAME[] = {
    "busy", "title", "control", "text", "menu", "event", "paused"
};

static int lookup(const char *word, const char *const *names, int n)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(word, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

/* Decimal, 0x hex, or the disassembly's $hex. */
static bool parse_num(const char *s, long *out)
{
    char *end = NULL;
    bool  neg = false;

    if (*s == '-') {
        neg = true;
        s++;
    }
    if (*s == '$') {
        *out = strtol(s + 1, &end, 16);
        if (end == s + 1) {
            return false;
        }
    } else {
        *out = strtol(s, &end, 0);
        if (end == s) {
            return false;
        }
    }
    if (*end != '\0') {
        return false;
    }
    if (neg) {
        *out = -*out;
    }
    return true;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'
                     || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
    return s;
}

/* Splits on blanks, in place. Returns the word count. */
static int split(char *s, char **word, int cap)
{
    int n = 0;

    while (*s != '\0' && n < cap) {
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == '\0') {
            break;
        }
        word[n++] = s;
        while (*s != '\0' && *s != ' ' && *s != '\t') {
            s++;
        }
        if (*s != '\0') {
            *s++ = '\0';
        }
    }
    return n;
}

static bool valid_name(const char *s)
{
    if (*s == '\0') {
        return false;
    }
    for (; *s != '\0'; s++) {
        bool ok = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')
               || (*s >= '0' && *s <= '9') || *s == '_' || *s == '-'
               || *s == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/* CharID_ by name: Rolf, Nei, Rudo, Amy, Hugh, Anna, Kain, Shir. */
static int char_id(const char *name)
{
    static const char *const CHAR_ID[8] = {
        "Rolf", "Nei", "Rudo", "Amy", "Hugh", "Anna", "Kain", "Shir"
    };
    for (int k = 0; k < 8; k++) {
        if (strcmp(name, CHAR_ID[k]) == 0) {
            return k;
        }
    }
    return -1;
}

static bool numbers(Cmd *c, char **word, int from, int n, char *err,
                    size_t errsz)
{
    c->nargs = 0;
    for (int i = from; i < n && c->nargs < ARGS_MAX; i++) {
        if (!parse_num(word[i], &c->arg[c->nargs])) {
            snprintf(err, errsz, "'%s' is not a number", word[i]);
            return false;
        }
        c->nargs++;
    }
    return true;
}

/* (build 131a) `expect text`'s string to window codes, as the script
 * files are read (window.c's load_script): a character by win_charset,
 * {XX} as that code in hex. The count written. */
static int encode_text(const char *s, u8 *out, int max)
{
    int n = 0;
    while (*s != '\0' && n < max) {
        if (*s == '{') {
            char *end = NULL;
            long  v   = strtol(s + 1, &end, 16);
            if (end != NULL && *end == '}' && end > s + 1) {
                out[n++] = (u8)v;
                s = end + 1;
                continue;
            }
        }
        /* (build 165) A language whose charset is its own whole (the
         * Japanese cartridge's): its codes, from UTF-8. */
        if (lang_charset_full()) {
            out[n++] = lang_code_of(lang_next_code_point(&s));
            continue;
        }
        out[n++] = win_charset(*s);
        s++;
    }
    return n;
}

/* The other way, for a failure's message: a glyph win_charset makes as
 * its character, anything else as {XX}. */
static void decode_text(const u8 *codes, int count, char *out, size_t size)
{
    if (lang_charset_full()) {
        /* (build 165) The language's own letters back (lang_text_of
         * stops at $C4: the codes here are the text's, ends and all). */
        size_t k = 0;
        out[0] = '\0';
        for (int i = 0; i < count && k + 8 < size; i++) {
            if (codes[i] >= TXT_CHAR) {
                k += (size_t)snprintf(out + k, size - k, "{%02X}", codes[i]);
                continue;
            }
            lang_text_of(&codes[i], 1, out + k, size - k);
            k += strlen(out + k);
        }
        return;
    }
    static const char GLYPHS[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz,.;\"?!'-: ";
    size_t n = 0;
    out[0] = '\0';
    for (int i = 0; i < count && n + 5 < size; i++) {
        char ch = 0;
        for (const char *g = GLYPHS; *g != '\0'; g++) {
            if (win_charset(*g) == codes[i]) {
                ch = *g;
                break;
            }
        }
        if (ch != 0) {
            out[n++] = ch;
            out[n] = '\0';
        } else {
            n += (size_t)snprintf(out + n, size - n, "{%02X}", codes[i]);
        }
    }
}

bool probe_parse_line(Cmd *c, char *body, char *err, size_t errsz)
{
    char  copy[LINE_MAX_LEN];
    char *word[16];

    snprintf(copy, sizeof copy, "%s", body);
    int n = split(copy, word, 16);
    if (n == 0) {
        /* (build 105) Callers pass no blank line; said, not read past. */
        snprintf(err, errsz, "an empty command");
        return false;
    }

    if (strcmp(word[0], "start") == 0) {
        c->kind = C_START;
        if (n < 2) {
            snprintf(err, errsz, "start what? new, slot, level, building, "
                                 "battle");
            return false;
        }
        if (strcmp(word[1], "new") == 0 && n == 2) {
            c->sub = START_NEW;
            return true;
        }
        if (strcmp(word[1], "slot") == 0 && (n == 3 || n == 4)) {
            c->sub = START_SLOT;
            if (!numbers(c, word, 2, 3, err, errsz)) {
                return false;
            }
            if (c->arg[0] < 1 || c->arg[0] > 3) {
                snprintf(err, errsz, "slot is 1, 2 or 3");
                return false;
            }
            if (n == 4) {
                snprintf(c->str, sizeof c->str, "%s", word[3]);
            }
            return true;
        }
        if (strcmp(word[1], "level") == 0 && n == 5) {
            c->sub = START_LEVEL;
            return numbers(c, word, 2, n, err, errsz);
        }
        if (strcmp(word[1], "building") == 0) {
            /* Spaces or PS2_BUILDING's commas. */
            char args[LINE_MAX_LEN] = "";
            for (int i = 2; i < n; i++) {
                strncat(args, word[i], sizeof args - strlen(args) - 2);
                strcat(args, " ");
            }
            for (char *p = args; *p != '\0'; p++) {
                if (*p == ',') {
                    *p = ' ';
                }
            }
            char *w2[16];
            int   m = split(args, w2, 16);
            if (m != 4 && m != 6) {
                snprintf(err, errsz, "start building B P F764 F766 [CX CY]");
                return false;
            }
            c->sub = START_BUILDING;
            return numbers(c, w2, 0, m, err, errsz);
        }
        if (strcmp(word[1], "battle") == 0 && n == 3) {
            c->sub = START_BATTLE;
            return numbers(c, word, 2, n, err, errsz);
        }
        snprintf(err, errsz, "start new | slot N [FILE] | level L CX CY | "
                             "building B P F764 F766 [CX CY] | battle F");
        return false;
    }

    if (strcmp(word[0], "synth") == 0 && n == 2) {
        /* (build 110) synth chip|q: switch the synth backend now, as the
         * AUDIO page does between ticks (audio_set_synth); a backend this
         * build has not got falls back to the chip, which is reset and
         * given the register shadow all the same. */
        c->kind   = C_SYNTH;
        c->arg[0] = synth_from_name(word[1]);
        c->nargs  = 1;
        if (c->arg[0] < 0) {
            snprintf(err, errsz, "synth chip|q");
            return false;
        }
        return true;
    }
    if (strcmp(word[0], "sound") == 0 && n == 3
        && strcmp(word[1], "play") == 0) {
        /* (build 111) sound play ID: a sound effect queued as the game
         * queues one (audio_play_sfx), for the EFFECTS volume's route. */
        c->kind  = C_SOUND_PLAY;
        c->nargs = 1;
        return numbers(c, word, 2, 3, err, errsz);
    }
    if (strcmp(word[0], "sound") == 0 && n == 3
        && strcmp(word[1], "render") == 0) {
        /* (build 110) sound render on|off: from the next tick the probe
         * renders each tick's audio through audio_render -- the backend
         * playing, the mix -- where it otherwise throws the queue away.
         * For `expect sound playing|silent`. route_probe only. */
        c->kind   = C_SOUND_RENDER;
        c->arg[0] = strcmp(word[2], "on") == 0;
        c->nargs  = 1;
        if (!c->arg[0] && strcmp(word[2], "off") != 0) {
            snprintf(err, errsz, "sound render on|off");
            return false;
        }
        return true;
    }
    if (strcmp(word[0], "display") == 0 && n >= 2) {
        /* (build 105) display WxH [fullscreen] [full=WxH] [window-scaled]
         * [desktop-scaled] [hdr | hdr=H/S]: the screen the Escape menu's
         * CRT page judges its values against (crt_display_parse). */
        c->kind = C_DISPLAY;
        c->str[0] = '\0';
        for (int i = 1; i < n; i++) {
            strncat(c->str, word[i], sizeof c->str - strlen(c->str) - 2);
            strcat(c->str, " ");
        }
        CrtDisplay d;
        if (!crt_display_parse(c->str, &d)) {
            snprintf(err, errsz, "display WxH [fullscreen] [full=WxH] "
                                 "[window-scaled] [desktop-scaled] "
                                 "[hdr|hdr=H/S]");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "press") == 0 && n == 2) {
        c->kind = C_PRESS;
        c->sub  = lookup(word[1], BUTTON_NAME, BUTTONS_N);
        if (c->sub < 0) {
            snprintf(err, errsz,
                     "press a|b|c|start|up|down|left|right|esc");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "walk") == 0 && n == 3) {
        c->kind = C_WALK;
        c->sub  = lookup(word[1], BUTTON_NAME, BUTTONS_N);
        if (c->sub < BUTTON_UP || c->sub > BUTTON_UP + 3) {
            snprintf(err, errsz, "walk up|down|left|right N");
            return false;
        }
        if (!numbers(c, word, 2, 3, err, errsz)) {
            return false;
        }
        if (c->arg[0] < 1) {
            snprintf(err, errsz, "walk at least one step");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "wait") == 0 && n >= 2) {
        if (strcmp(word[1], "idle") == 0 && n == 2) {
            c->kind = C_WAIT_IDLE;
            return true;
        }
        if (strcmp(word[1], "step") == 0 && n == 2) {
            c->kind = C_WAIT_STEP;
            return true;
        }
        if (strcmp(word[1], "pad") == 0 && n == 3) {
            /* (build 101) wait pad N: until the game waits on the pad, or
             * N ticks, whichever comes first -- never a failure. COMPUTER
             * PLAYS's pace in a fight: a menu or a page answered the
             * moment it is up, a round that repeats still looked at every
             * N ticks. */
            c->kind = C_WAIT_PAD;
            return numbers(c, word, 2, 3, err, errsz);
        }
        if (strcmp(word[1], "ticks") == 0 && n == 3) {
            c->kind = C_WAIT_TICKS;
            return numbers(c, word, 2, 3, err, errsz);
        }
        if (strcmp(word[1], "script") == 0 && n == 3) {
            /* wait script $XXXX: until that script reaches a window, on
             * expect script's cursor, which it then moves past it. */
            c->kind = C_WAIT_SCRIPT;
            return numbers(c, word, 2, 3, err, errsz);
        }
        if (strcmp(word[1], "screen") == 0 && n == 3) {
            c->kind = C_WAIT_SCREEN;
            c->sub  = lookup(word[2], SCREEN_NAME, SCREEN_COUNT);
            if (c->sub < 0) {
                snprintf(err, errsz, "no screen '%s'", word[2]);
                return false;
            }
            return true;
        }
        if (strcmp(word[1], "level") == 0 && n == 3) {
            /* wait level L: until level L is loaded and faded in -- a
             * scene's reloads (loc_7E02) change level with no pad. */
            c->kind = C_WAIT_LEVEL;
            return numbers(c, word, 2, 3, err, errsz);
        }
        if (strcmp(word[1], "ending") == 0 && n == 3) {
            /* wait ending MOMENT: until the ending screen is at that
             * moment (ending_debug_moment: page3_text, wipe, credits,
             * ufo, button, ...). */
            c->kind = C_WAIT_ENDING;
            snprintf(c->str, sizeof c->str, "%s", word[2]);
            return true;
        }
        snprintf(err, errsz,
                 "wait idle | screen NAME | level L | ending MOMENT | "
                 "ticks N | pad N | step");
        return false;
    }

    if (strcmp(word[0], "shot") == 0 && n == 2) {
        c->kind = C_SHOT;
        snprintf(c->str, sizeof c->str, "%s", word[1]);
        size_t len = strlen(c->str);
        if (len > 4 && strcmp(c->str + len - 4, ".png") == 0) {
            c->str[len - 4] = '\0';
        }
        if (!valid_name(c->str)) {
            snprintf(err, errsz, "a shot name is letters, digits, _ - .");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "expect") == 0 && n == 2
            && strcmp(word[1], "enemy-joined") == 0) {
        c->kind = C_EXPECT_JOINED;
        return true;
    }
    if (strcmp(word[0], "expect") == 0 && n == 3
        && strcmp(word[1], "stuck") == 0) {
        /* (build 129d; restored in 132a) expect stuck none|$MASK: the music
         * tracks lent to an effect that no longer holds them, a bit a track
         * (smps_stuck_tracks) -- none, or exactly MASK. */
        c->kind   = C_EXPECT_SOUND;
        c->sub    = 3;
        c->nargs  = 1;
        c->arg[0] = 0;
        if (strcmp(word[2], "none") == 0) {
            return true;
        }
        return numbers(c, word, 2, 3, err, errsz);
    }
    if (strcmp(word[0], "expect") == 0 && n == 2
            && strcmp(word[1], "sound-records") == 0) {
        /* (build 124a) expect sound-records: every register audio.c's
         * record of a track holds -- what a switch of synth or VOICES
         * replays -- is on the track's own channel, but the two the
         * sequencer puts on FM1 and FM3 whatever the track (the pan flag's
         * $B4, FM3's special-mode pitches $A8-$AE). */
        c->kind  = C_EXPECT_SOUND;
        c->sub   = 2;
        c->nargs = 0;
        return true;
    }
    if (strcmp(word[0], "expect") == 0 && n >= 3) {
        if (strcmp(word[1], "screen") == 0 && n == 3) {
            c->kind = C_EXPECT_SCREEN;
            c->sub  = lookup(word[2], SCREEN_NAME, SCREEN_COUNT);
            if (c->sub < 0) {
                snprintf(err, errsz, "no screen '%s'", word[2]);
                return false;
            }
            return true;
        }
        if (strcmp(word[1], "level") == 0 && (n == 3 || n == 5)) {
            c->kind = C_EXPECT_LEVEL;
            return numbers(c, word, 2, n, err, errsz);
        }
        if (strcmp(word[1], "script") == 0 && n == 3) {
            c->kind = C_EXPECT_SCRIPT;
            return numbers(c, word, 2, 3, err, errsz);
        }
        if (strcmp(word[1], "command") == 0 && n == 3) {
            c->kind = C_EXPECT_COMMAND;
            c->sub  = lookup(word[2], COMMAND_NAME, 4);
            if (c->sub < 0) {
                snprintf(err, errsz, "expect command attack|tech|item|defend");
                return false;
            }
            return true;
        }
        if (strcmp(word[1], "item") == 0 && (n == 3 || n == 4)) {
            /* expect item NAME [MEMBER]: in anyone's bag, or that party
             * position's. */
            c->kind = C_EXPECT_ITEM;
            snprintf(c->str, sizeof c->str, "%s", word[2]);
            c->arg[0] = -1;
            c->nargs  = 1;
            if (n == 4 && !numbers(c, word, 3, 4, err, errsz)) {
                return false;
            }
            if (c->arg[0] < -1 || c->arg[0] > 3) {
                snprintf(err, errsz, "expect item NAME [MEMBER 0-3]");
                return false;
            }
            return true;
        }
        if (strcmp(word[1], "person") == 0 && n == 4
            && (strcmp(word[3], "present") == 0
                || strcmp(word[3], "absent") == 0)) {
            /* expect person $XX present|absent: someone with that
             * dialogue number in an object slot on this level. */
            c->kind = C_EXPECT_PERSON;
            if (!numbers(c, word, 2, 3, err, errsz)) {
                return false;
            }
            c->arg[1] = strcmp(word[3], "present") == 0;
            c->nargs  = 2;
            return true;
        }
        if (strcmp(word[1], "flag") == 0 && n == 4) {
            /* expect flag $C7XX N: one byte of the $C700 block. */
            c->kind = C_EXPECT_FLAG;
            if (!numbers(c, word, 2, 4, err, errsz)) {
                return false;
            }
            if (c->arg[0] < 0xC700 || c->arg[0] > 0xC7FF
                || c->arg[1] < 0 || c->arg[1] > 0xFF) {
                snprintf(err, errsz, "expect flag $C700-$C7FF BYTE");
                return false;
            }
            return true;
        }
        if (strcmp(word[1], "join-next") == 0 && n == 3) {
            /* party_member_join_next ($C606): who knocks at Rolf's door. */
            c->kind = C_EXPECT_JOIN_NEXT;
            return numbers(c, word, 2, 3, err, errsz);
        }
        if (strcmp(word[1], "hidapipe") == 0 && n == 3
            && (strcmp(word[2], "on") == 0 || strcmp(word[2], "off") == 0)) {
            /* $FFFFCB0E, which ProcessRandomBattle reads. */
            c->kind   = C_EXPECT_HIDAPIPE;
            c->arg[0] = strcmp(word[2], "on") == 0;
            c->nargs  = 1;
            return true;
        }
        if (strcmp(word[1], "menu") == 0 && n == 3
            && strcmp(word[2], "none") == 0) {
            /* (build 97d) expect menu none: no window's poll holds the
             * pad -- a C press that the ROM ignores opened nothing. */
            c->kind   = C_EXPECT_MENU;
            c->arg[0] = -1;
            c->arg[1] = -1;
            c->nargs  = 2;
            return true;
        }
        if (strcmp(word[1], "menu") == 0 && (n == 3 || n == 4)) {
            /* expect menu $XX [STOP]: the pad is held by window $XX's
             * poll (and its cursor is on STOP), as `state` shows it. */
            c->kind = C_EXPECT_MENU;
            if (!numbers(c, word, 2, n, err, errsz)) {
                return false;
            }
            if (n == 3) {
                c->arg[1] = -1;
                c->nargs  = 2;
            }
            return true;
        }
        if (strcmp(word[1], "pixel") == 0 && n == 5) {
            /* expect pixel X Y $BGR: the frame `shot` would write has
             * that CRAM word at screen pixel X, Y. */
            c->kind = C_EXPECT_PIXEL;
            if (!numbers(c, word, 2, 5, err, errsz)) {
                return false;
            }
            if (c->arg[0] < 0 || c->arg[0] >= SCREEN_W || c->arg[1] < 0
                || c->arg[1] >= SCREEN_H) {
                snprintf(err, errsz, "expect pixel X Y: off the screen");
                return false;
            }
            return true;
        }
        if (strcmp(word[1], "picture") == 0 && n == 5) {
            /* (build 142) expect picture X Y THING|none: the frame `shot`
             * would write took screen pixel X, Y's colour from THING's
             * picture (assets/art/<kind>/THING), or from no picture -- a
             * tile through the palette, the backdrop, the overlay. */
            c->kind = C_EXPECT_PICTURE;
            if (!numbers(c, word, 2, 4, err, errsz)) {
                return false;
            }
            if (c->arg[0] < 0 || c->arg[0] >= SCREEN_W || c->arg[1] < 0
                || c->arg[1] >= SCREEN_H) {
                snprintf(err, errsz, "expect picture X Y: off the screen");
                return false;
            }
            snprintf(c->str, sizeof c->str, "%s", word[4]);
            return true;
        }
        if (strcmp(word[1], "object") == 0 && n == 4) {
            /* expect object $XX visible|hidden|absent: a scene object
             * by ObjID_ on this level. */
            static const char *const VIS[] = { "hidden", "visible" };
            c->kind = C_EXPECT_OBJECT;
            if (!numbers(c, word, 2, 3, err, errsz)) {
                return false;
            }
            c->arg[1] = (strcmp(word[3], "absent") == 0) ? -1
                      : lookup(word[3], VIS, 2);
            if (c->arg[1] < -1 || (c->arg[1] == -1
                                   && strcmp(word[3], "absent") != 0)) {
                snprintf(err, errsz, "expect object $XX visible|hidden|absent");
                return false;
            }
            c->nargs = 2;
            return true;
        }
        if (strcmp(word[1], "crt") == 0 && n >= 4) {
            /* (build 105) expect crt ROW VALUE [/ WHY]: what the CRT row
             * shows and, after a slash, why it cannot be had (none: it
             * can). Rows: summary (the menu's CRT), scanlines, mask,
             * subpixels, hdr, peak, white. */
            static const char *const ROW[SYSMENU_VALUE_COUNT] = {
                "summary", "scanlines", "mask", "subpixels", "hdr",
                "peak", "white"
            };
            c->kind   = C_EXPECT_CRT;
            c->arg[0] = lookup(word[2], ROW, SYSMENU_VALUE_COUNT);
            if (c->arg[0] < 0) {
                snprintf(err, errsz, "expect crt summary|scanlines|mask|"
                                     "subpixels|hdr|peak|white VALUE "
                                     "[/ WHY]");
                return false;
            }
            /* VALUE, a '|', WHY: words joined by single spaces. */
            c->str[0] = '\0';
            bool slash = false;
            for (int i = 3; i < n; i++) {
                if (strcmp(word[i], "/") == 0 && !slash) {
                    strcat(c->str, "|");
                    slash = true;
                    continue;
                }
                size_t len = strlen(c->str);
                if (len > 0 && c->str[len - 1] != '|') {
                    strcat(c->str, " ");
                }
                strncat(c->str, word[i], sizeof c->str - strlen(c->str) - 2);
            }
            if (!slash) {
                strcat(c->str, "|");
            }
            c->nargs = 1;
            return true;
        }
        if (strcmp(word[1], "audio") == 0 && n >= 4) {
            /* (build 110) expect audio ROW VALUE [/ WHY]: what the AUDIO
             * row shows and, after a slash, why it cannot be had. Rows:
             * summary (the menu's AUDIO), synth, master, music, (build
             * 111) effects, (111a) oversample, (112) voices, tails,
             * patches, (116) grain. */
            static const char *const ROW[SYSMENU_AUDIO_VALUE_COUNT] = {
                "summary", "synth", "master", "music", "effects",
                "oversample", "voices", "tails", "patches", "grain"
            };
            c->kind   = C_EXPECT_AUDIO;
            c->arg[0] = lookup(word[2], ROW, SYSMENU_AUDIO_VALUE_COUNT);
            if (c->arg[0] < 0) {
                snprintf(err, errsz, "expect audio summary|synth|master|"
                                     "music|effects|oversample|voices|tails|"
                                     "patches|grain VALUE [/ WHY]");
                return false;
            }
            c->str[0] = '\0';
            bool slash = false;
            for (int i = 3; i < n; i++) {
                if (strcmp(word[i], "/") == 0 && !slash) {
                    strcat(c->str, "|");
                    slash = true;
                    continue;
                }
                size_t len = strlen(c->str);
                if (len > 0 && c->str[len - 1] != '|') {
                    strcat(c->str, " ");
                }
                strncat(c->str, word[i], sizeof c->str - strlen(c->str) - 2);
            }
            if (!slash) {
                strcat(c->str, "|");
            }
            c->nargs = 1;
            return true;
        }
        if ((strcmp(word[1], "keys") == 0 || strcmp(word[1], "buttons") == 0)
            && n >= 4) {
            /* (build 160) expect keys|buttons ACTION SLOT1 / SLOT2: what
             * the CONTROLS page (keys) or the BUTTONS page (buttons, PAD
             * 1's labels) shows in ACTION's row -- up down left right a b
             * c start -- each slot as drawn, "-" for none. */
            c->kind   = C_EXPECT_CONTROLS;
            c->sub    = strcmp(word[1], "buttons") == 0;
            c->arg[0] = -1;
            for (int a = 0; sysmenu_action_key(a) != NULL; a++) {
                if (strcmp(word[2], sysmenu_action_key(a)) == 0) {
                    c->arg[0] = a;
                }
            }
            c->str[0] = '\0';
            int slashes = 0;
            for (int i = 3; i < n; i++) {
                if (strcmp(word[i], "/") == 0) {
                    strcat(c->str, "|");
                    slashes++;
                    continue;
                }
                size_t len = strlen(c->str);
                if (len > 0 && c->str[len - 1] != '|') {
                    strcat(c->str, " ");
                }
                strncat(c->str, word[i], sizeof c->str - strlen(c->str) - 2);
            }
            c->nargs = 1;
            if (c->arg[0] < 0 || slashes != 1) {
                snprintf(err, errsz, "expect keys|buttons up|down|left|right|"
                                     "a|b|c|start SLOT1 / SLOT2");
                return false;
            }
            return true;
        }
        if (strcmp(word[1], "sound-writes") == 0 && n == 4) {
            /* (build 110) expect sound-writes N $HASH: the sequencer has
             * sent N writes through its sink since start-up, hashed
             * (audio_write_hash) -- what a switch of backend must not
             * change. */
            c->kind = C_EXPECT_SOUND;
            c->sub  = 0;
            return numbers(c, word, 2, 4, err, errsz);
        }
        if (strcmp(word[1], "sound-register") == 0 && n == 5) {
            /* (build 183) expect sound-register PART $REG $VALUE: the
             * YM2612's register REG -- PART 1 for FM1-FM3 and the chip's
             * own registers, 2 for FM4-FM6 -- as the sequencer last wrote
             * it (audio_register_written), rendered or not. */
            c->kind = C_EXPECT_SOUND;
            c->sub  = 5;
            if (!numbers(c, word, 2, 5, err, errsz)) {
                return false;
            }
            if (c->arg[0] < 1 || c->arg[0] > 2 || c->arg[1] < 0
                || c->arg[1] > 0xFF || c->arg[2] < 0 || c->arg[2] > 0xFF) {
                snprintf(err, errsz, "expect sound-register 1|2 $REG $VALUE "
                                     "(a byte each)");
                return false;
            }
            return true;
        }
        if (strcmp(word[1], "sound") == 0 && n == 3) {
            /* (build 110) expect sound playing|silent: over the last
             * four ticks `sound render` rendered, the mix peaked above
             * -40 dBFS, or did not. (A switch's cut clicks through the
             * DC blocker for a few ticks: wait a dozen before asking.) */
            c->kind   = C_EXPECT_SOUND;
            c->sub    = 1;
            c->arg[0] = strcmp(word[2], "playing") == 0;
            c->nargs  = 1;
            if (!c->arg[0] && strcmp(word[2], "silent") != 0) {
                snprintf(err, errsz, "expect sound playing|silent");
                return false;
            }
            return true;
        }
        if (strcmp(word[1], "sound") == 0 && n == 5
            && strcmp(word[2], "clicks") == 0
            && strcmp(word[3], "below") == 0) {
            /* (build 143) expect sound clicks below DB: over the last four
             * ticks `sound render` rendered, the sharpest bend of either
             * side (render_sound's second difference) stayed under DB
             * dBFS. */
            c->kind = C_EXPECT_SOUND;
            c->sub  = 4;
            return numbers(c, word, 4, 5, err, errsz);
        }
        if (strcmp(word[1], "rumble") == 0 && n == 3) {
            /* (build 160) expect rumble going|still: the motors now, at
             * HIGH (the host's watcher). */
            c->kind   = C_EXPECT_RUMBLE;
            c->sub    = 1;
            c->arg[0] = strcmp(word[2], "going") == 0;
            c->nargs  = 1;
            if (!c->arg[0] && strcmp(word[2], "still") != 0) {
                snprintf(err, errsz, "expect rumble going|still");
                return false;
            }
            return true;
        }
        if (strcmp(word[1], "rumble") == 0 && n == 4) {
            /* (build 160) expect rumble KIND N: KIND (hurt, fell, lost,
             * floor) felt N times since the last `expect rumble KIND`, or
             * the start. */
            c->kind   = C_EXPECT_RUMBLE;
            c->sub    = 0;
            c->arg[0] = rumble_kind_from_name(word[2]);
            if (c->arg[0] < 0 || !parse_num(word[3], &c->arg[1])
                || c->arg[1] < 0) {
                snprintf(err, errsz, "expect rumble hurt|fell|lost|floor N");
                return false;
            }
            c->nargs = 2;
            return true;
        }
        if (strcmp(word[1], "setting") == 0 && n == 4
            && strcmp(word[2], "rumble") == 0) {
            /* (build 160) expect setting rumble off|low|medium|high: the
             * GAMEPAD page's RUMBLE. */
            c->kind   = C_EXPECT_SETTING;
            c->arg[1] = 3;
            c->arg[0] = rumble_level_from_key(word[3], (RumbleLevel)-1);
            c->nargs  = 2;
            if (c->arg[0] < 0) {
                snprintf(err, errsz, "expect setting rumble "
                                     "off|low|medium|high");
                return false;
            }
            return true;
        }
        if (strcmp(word[1], "setting") == 0 && n == 4
            && strcmp(word[2], "language") == 0) {
            /* (build 163) expect setting language KEY: the Escape menu's
             * LANGUAGE, by its key in assets/lang/languages.json. */
            c->kind   = C_EXPECT_SETTING;
            c->arg[1] = 4;
            c->nargs  = 2;
            snprintf(c->str, sizeof c->str, "%s", word[3]);
            return true;
        }
        if (strcmp(word[1], "setting") == 0 && n == 4) {
            /* (build 100) expect setting fullscreen on|off;
             * (build 100b) expect setting speed normal|fast, (build 120)
             * or any of SPEED's settings.json values: its index */
            c->kind   = C_EXPECT_SETTING;
            c->arg[1] = (strcmp(word[2], "speed") == 0) ? 1
                      : (strcmp(word[2], "computer") == 0) ? 2 : 0;
            if (c->arg[1] == 0 && strcmp(word[2], "fullscreen") != 0) {
                snprintf(err, errsz,
                         "expect setting fullscreen|speed|computer|rumble "
                         "...");
                return false;
            }
            c->arg[0] = -1;
            if (c->arg[1] == 1) {
                for (int i = 0; sysmenu_speed_key(i) != NULL; i++) {
                    if (strcmp(word[3], sysmenu_speed_key(i)) == 0) {
                        c->arg[0] = i;
                    }
                }
            } else if (strcmp(word[3], "on") == 0) {
                c->arg[0] = 1;
            } else if (strcmp(word[3], "off") == 0) {
                c->arg[0] = 0;
            }
            if (c->arg[0] < 0) {
                snprintf(err, errsz, "expect setting fullscreen|computer "
                                     "on|off, speed normal|auto|2x|auto_2x|"
                                     "fast");
                return false;
            }
            c->nargs = 2;
            return true;
        }
        if (strcmp(word[1], "escape-menu") == 0 && n == 3) {
            /* (build 100) expect escape-menu open|closed */
            c->kind = C_EXPECT_ESCAPE_MENU;
            if (strcmp(word[2], "open") == 0) {
                c->arg[0] = 1;
            } else if (strcmp(word[2], "closed") == 0) {
                c->arg[0] = 0;
            } else {
                snprintf(err, errsz, "expect escape-menu open|closed");
                return false;
            }
            c->nargs = 1;
            return true;
        }
        if (strcmp(word[1], "riding") == 0 && n == 3) {
            /* expect riding yes|no: the party is on the jet scooter
             * (jet_scooter_flag, as `state` shows it) or on foot. */
            c->kind = C_EXPECT_RIDING;
            if (strcmp(word[2], "yes") == 0) {
                c->arg[0] = 1;
            } else if (strcmp(word[2], "no") == 0) {
                c->arg[0] = 0;
            } else {
                snprintf(err, errsz, "expect riding yes|no");
                return false;
            }
            c->nargs = 1;
            return true;
        }
        if (strcmp(word[1], "music") == 0 && n == 3) {
            /* expect music $XX: the last music id ($81-$9F) that reached
             * the sequencer -- queued past UpdateSoundQueue's latch, or
             * written straight to sound_queue as loc_7E02's Power is. */
            c->kind = C_EXPECT_MUSIC;
            return numbers(c, word, 2, 3, err, errsz);
        }
        if (strcmp(word[1], "song") == 0 && n == 3) {
            /* (build 139f) expect song $XX: the music the sequencer last
             * started (PlaySoundID) -- what plays, where `expect music`
             * says what was queued, which a sound queued after it in the
             * same field can overwrite. */
            c->kind = C_EXPECT_SONG;
            return numbers(c, word, 2, 3, err, errsz);
        }
        if (strcmp(word[1], "party") == 0 && n >= 3 && n <= 6) {
            /* expect party NAME [NAME ...]: exactly these members, in
             * party order, by CharID_ name (Rolf, Nei, Rudo, Amy, Hugh,
             * Anna, Kain, Shir). */
            c->kind  = C_EXPECT_PARTY;
            c->nargs = n - 2;
            for (int i = 2; i < n; i++) {
                int id = char_id(word[i]);
                if (id < 0) {
                    snprintf(err, errsz, "expect party: unknown member %s",
                             word[i]);
                    return false;
                }
                c->arg[i - 2] = id;
            }
            return true;
        }
        if (strcmp(word[1], "hp") == 0 && (n == 4 || n == 5)) {
            /* expect hp MEMBER LO [HI]: that party position's HP. */
            c->kind = C_EXPECT_HP;
            if (!numbers(c, word, 2, n, err, errsz)) {
                return false;
            }
            if (c->arg[0] < 0 || c->arg[0] > 3) {
                snprintf(err, errsz, "expect hp MEMBER(0-3) LO [HI]");
                return false;
            }
            if (n == 4) {
                c->arg[2] = c->arg[1];
                c->nargs  = 3;
            }
            return true;
        }
        if (strcmp(word[1], "stat") == 0 && (n == 5 || n == 6)) {
            /* expect stat MEMBER STAT LO [HI]: that party position's
             * hp, tp, attack, defense or agility. The stat's name goes
             * in c->str; the numbers are MEMBER, LO, HI. */
            static const char *const STATS[] = {
                "hp", "tp", "attack", "defense", "agility", NULL };
            c->kind = C_EXPECT_STAT;
            bool known = false;
            for (int i = 0; STATS[i] != NULL; i++) {
                if (strcmp(word[3], STATS[i]) == 0) {
                    known = true;
                }
            }
            if (!known) {
                snprintf(err, errsz,
                         "expect stat MEMBER hp|tp|attack|defense|agility LO [HI]");
                return false;
            }
            snprintf(c->str, sizeof c->str, "%s", word[3]);
            long lo, hi;
            if (!parse_num(word[4], &lo) || (n == 6 && !parse_num(word[5], &hi))) {
                snprintf(err, errsz, "expect stat: bad number");
                return false;
            }
            long pos;
            if (!parse_num(word[2], &pos) || pos < 0 || pos > 3) {
                snprintf(err, errsz, "expect stat MEMBER(0-3) ...");
                return false;
            }
            c->arg[0] = pos;
            c->arg[1] = lo;
            c->arg[2] = (n == 6) ? hi : lo;
            c->nargs  = 3;
            return true;
        }
        if (strcmp(word[1], "text") == 0) {
            /* (build 131a) expect text "..." -- the rest of the line, in
             * double quotes, written as the script files write text. */
            c->kind = C_EXPECT_TEXT;
            const char *rest = strstr(body, "text") + strlen("text");
            char tmp[LINE_MAX_LEN];
            snprintf(tmp, sizeof tmp, "%s", rest);
            char  *q   = trim(tmp);
            size_t len = strlen(q);
            if (len < 2 || q[0] != '"' || q[len - 1] != '"') {
                snprintf(err, errsz, "expect text \"...\"");
                return false;
            }
            q[len - 1] = '\0';
            snprintf(c->str, sizeof c->str, "%s", q + 1);
            return true;
        }
        if (strcmp(word[1], "no-stderr") == 0) {
            /* The rest of the line, spaces and all. */
            c->kind = C_EXPECT_NO_STDERR;
            const char *rest = strstr(body, "no-stderr") + strlen("no-stderr");
            char tmp[LINE_MAX_LEN];
            snprintf(tmp, sizeof tmp, "%s", rest);
            snprintf(c->str, sizeof c->str, "%s", trim(tmp));
            return true;
        }
    }

    if (strcmp(word[0], "fuzz") == 0 && (n == 2 || n == 3)) {
        c->kind = C_FUZZ;
        if (!numbers(c, word, 1, n, err, errsz)) {
            return false;
        }
        if (c->arg[0] < 1) {
            snprintf(err, errsz, "fuzz for at least one tick");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "enemy-hp") == 0 && n == 2) {
        /* enemy-hp N (build 93): every live enemy's HP set to N, for a
         * route about what comes after a boss rather than the fight. */
        c->kind = C_ENEMY_HP;
        if (!numbers(c, word, 1, 2, err, errsz)) {
            return false;
        }
        return true;
    }
    if (strcmp(word[0], "party") == 0 && n == 3) {
        c->kind = C_PARTY;
        if (!numbers(c, word, 1, 3, err, errsz)) {
            return false;
        }
        if (c->arg[0] < 1 || c->arg[0] > 4 || c->arg[1] < 1
            || c->arg[1] > 50) {
            snprintf(err, errsz, "party is 1-4 members at level 1-50");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "members") == 0 && n >= 2 && n <= 5) {
        /* members NAME [NAME ...]: the party in this order. */
        c->kind  = C_MEMBERS;
        c->nargs = n - 1;
        for (int i = 1; i < n; i++) {
            int id = char_id(word[i]);
            if (id < 0) {
                snprintf(err, errsz, "members: unknown member %s", word[i]);
                return false;
            }
            c->arg[i - 1] = id;
        }
        return true;
    }
    if (strcmp(word[0], "member") == 0 && n == 3) {
        /* member NAME LEVEL: raised to LEVEL by the real rows. */
        c->kind   = C_MEMBER;
        c->arg[0] = char_id(word[1]);
        c->arg[1] = strtol(word[2], NULL, 0);
        if (c->arg[0] < 0 || c->arg[1] < 1 || c->arg[1] > 50) {
            snprintf(err, errsz, "member NAME LEVEL(1-50)");
            return false;
        }
        return true;
    }
    if (strcmp(word[0], "outfit") == 0 && n == 7) {
        /* outfit NAME HEAD RIGHT LEFT BODY LEGS: ItemID_ names, - for
         * none. */
        c->kind   = C_OUTFIT;
        c->arg[0] = char_id(word[1]);
        if (c->arg[0] < 0) {
            snprintf(err, errsz, "outfit: unknown member %s", word[1]);
            return false;
        }
        /* The names, resolved when it runs: the item table is loaded
         * by then. */
        snprintf(c->str, sizeof c->str, "%s %s %s %s %s", word[2], word[3],
                 word[4], word[5], word[6]);
        return true;
    }
    if (strcmp(word[0], "save-here") == 0 && n == 2) {
        /* save-here N (build 84): slot N in the route's save directory,
         * written as a Visiphone save (ItemAction_Visiphone -> Data
         * Memory with the map position kept): $C65C = 1, so CONTINUE
         * skips CheckPlanetAndCaps and lands where the party stands --
         * on Dezolis too, where a Data Memory without a Magic Cap is
         * $810. The harness's; the party needs no Visiphone. */
        c->kind  = C_SAVE_HERE;
        c->nargs = 1;
        if (!numbers(c, word, 1, 2, err, errsz)) {
            return false;
        }
        if (c->arg[0] < 1 || c->arg[0] > 4) {
            snprintf(err, errsz, "save-here SLOT(1-4)");
            return false;
        }
        return true;
    }
    if (strcmp(word[0], "language") == 0 && n == 2) {
        /* (build 163) language KEY: the Escape menu's LANGUAGE set as its
         * row would (assets/lang/languages.json's key), taken up by the
         * game at its next tick. */
        c->kind = C_LANGUAGE;
        snprintf(c->str, sizeof c->str, "%s", word[1]);
        return true;
    }
    if (strcmp(word[0], "oneshot") == 0 && n == 2) {
        /* (build 97d) oneshot $XX: the event word's high byte at the next
         * level load (a warp, a door): loc_7E02's case. */
        c->kind = C_ONESHOT;
        return numbers(c, word, 1, 2, err, errsz);
    }
    if (strcmp(word[0], "warp") == 0 && n == 4) {
        /* warp LEVEL X Y: through a door to there, the game kept. */
        c->kind = C_WARP;
        return numbers(c, word, 1, 4, err, errsz);
    }

    if (strcmp(word[0], "keep-equipment") == 0 && n == 1) {
        c->kind = C_KEEP_EQUIPMENT;
        return true;
    }

    if (strcmp(word[0], "rumble-log") == 0 && n == 3) {
        /* (build 160) rumble-log NAME LEVEL: from now on OUT/NAME has a
         * line "TICK 1 LOW HIGH" each tick the motors at LEVEL (0 OFF to
         * 3 HIGH) change -- the headless game's PS2_RUMBLE log, which
         * counts the frames it pumped where this counts ticks, so a route
         * of the headless run's presses gives the same lines
         * (tools/rumble_check.py). */
        c->kind = C_RUMBLE_LOG;
        if (!valid_name(word[1]) || !parse_num(word[2], &c->arg[0])
            || c->arg[0] < 0 || c->arg[0] >= RUMBLE_LEVELS) {
            snprintf(err, errsz, "rumble-log NAME 0-3");
            return false;
        }
        snprintf(c->str, sizeof c->str, "%s", word[1]);
        c->nargs = 1;
        return true;
    }

    if (strcmp(word[0], "key") == 0 && (n == 2 || n == 3)) {
        /* (build 160) key NAME [TICKS]: a key pressed as it is
         * (src/core/keys.c's names: Q, SPACE, ESCAPE, BACKSPC ...) and held
         * TICKS ticks (1 by default), then a tick of nothing -- what the
         * platform makes of it: Input.key as it goes down, the actions the
         * Escape menu's controls bind it to, Escape the menu, Backspace
         * back, the arrows and Enter the menu's nav (held, the menu's
         * cursor repeats). */
        c->kind   = C_KEY;
        c->arg[0] = key_from_name(word[1]);
        c->arg[1] = 1;
        c->nargs  = 2;
        if (c->arg[0] == KEY_NONE
            || (n == 3 && (!parse_num(word[2], &c->arg[1]) || c->arg[1] < 1
                           || c->arg[1] > 3600))) {
            snprintf(err, errsz, "key NAME [TICKS] (Q, SPACE, ESCAPE, "
                                 "BACKSPC ...; 1-3600)");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "pad") == 0 && (n == 2 || n == 3)) {
        /* (build 160) pad NAME [TICKS]: a gamepad input pressed as it is,
         * by its name in settings.json (SOUTH, DUP, LSUP, L2, JOY3 ...),
         * as `key`: Input.pad_input as it goes down, the actions it is
         * bound to, Back the menu, the d-pad and left stick the menu's
         * nav. */
        c->kind   = C_PAD;
        c->arg[0] = pad_input_from_name(word[1]);
        c->arg[1] = 1;
        c->nargs  = 2;
        if (c->arg[0] == PAD_NONE
            || (n == 3 && (!parse_num(word[2], &c->arg[1]) || c->arg[1] < 1
                           || c->arg[1] > 3600))) {
            snprintf(err, errsz, "pad NAME [TICKS] (SOUTH, DUP, LSUP, L2, "
                                 "JOY3 ...; 1-3600)");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "pad-style") == 0 && n == 2) {
        /* (build 160) pad-style STYLE: whose labels PAD 1 wears for the
         * BUTTONS page (keys.h: none, ps3, xboxone, switch ...), as main.c
         * hands them over from the platform. */
        c->kind   = C_PAD_STYLE;
        c->arg[0] = pad_style_from_key(word[1], PAD_STYLES);
        c->nargs  = 1;
        if (c->arg[0] == PAD_STYLES) {
            snprintf(err, errsz, "pad-style none|standard|xbox360|xboxone|"
                                 "ps3|ps4|ps5|switch|gamecube|joystick");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "defense") == 0 && n == 3) {
        /* defense MEMBER N: that party position's defense ($1E), as the
         * enemies' blows read it -- for testing loc_2AA8's rolls. */
        c->kind = C_DEFENSE;
        if (!numbers(c, word, 1, 3, err, errsz)) {
            return false;
        }
        if (c->arg[0] < 0 || c->arg[0] > 3 || c->arg[1] < 0
            || c->arg[1] > 0xFFFF) {
            snprintf(err, errsz, "defense MEMBER(0-3) N");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "flag") == 0 && n == 3) {
        /* flag $C7XX N: set one byte of the $C700 block. It takes effect
         * wherever the game next reads it -- an object's init runs at
         * the next level load, not now. */
        c->kind = C_FLAG;
        if (!numbers(c, word, 1, 3, err, errsz)) {
            return false;
        }
        if (c->arg[0] < 0xC700 || c->arg[0] > 0xC7FF
            || c->arg[1] < 0 || c->arg[1] > 0xFF) {
            snprintf(err, errsz, "flag $C700-$C7FF BYTE");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "join-next") == 0 && n == 2) {
        /* (build 145) join-next N: party_member_join_next ($C606), the
         * last recruit sent to Rolf's house. While it is ahead of
         * party_members_joined, each visit there brings the next one to
         * the door (RolfHouse, loc_B04E). */
        c->kind = C_JOIN_NEXT;
        if (!numbers(c, word, 1, 2, err, errsz)) {
            return false;
        }
        if (c->arg[0] < 1 || c->arg[0] > 8) {
            snprintf(err, errsz, "join-next 1-8");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "meseta") == 0 && n == 2) {
        c->kind  = C_MESETA;
        c->nargs = 1;
        return numbers(c, word, 1, 2, err, errsz);
    }

    if (strcmp(word[0], "give") == 0 && (n == 2 || n == 3)) {
        c->kind = C_GIVE;
        snprintf(c->str, sizeof c->str, "%s", word[1]);
        c->arg[0] = 0;
        c->nargs  = 1;
        if (n == 3 && !numbers(c, word, 2, 3, err, errsz)) {
            return false;
        }
        if (c->arg[0] < 0 || c->arg[0] > 3) {
            snprintf(err, errsz, "give: the member is a party position 0-3");
            return false;
        }
        return true;
    }

    if (strcmp(word[0], "state") == 0 && n == 1) {
        c->kind = C_STATE;
        return true;
    }

    if (strcmp(word[0], "exp") == 0 && n == 1) {
        /* exp: one log line, NAME:EXPERIENCE for all eight. */
        c->kind = C_EXP;
        return true;
    }
    if (strcmp(word[0], "roster") == 0 && n == 1) {
        c->kind = C_ROSTER;
        return true;
    }

    if (strcmp(word[0], "objects") == 0 && n == 1) {
        c->kind = C_OBJECTS;
        return true;
    }

    if (strcmp(word[0], "bags") == 0 && n == 1) {
        c->kind = C_BAGS;
        return true;
    }
    if (strcmp(word[0], "hands") == 0 && n == 1) {
        /* (build 99) what each member holds in the right and left hand:
         * `bags` marks a piece worn, not which hand wears it. */
        c->kind = C_HANDS;
        return true;
    }
    if (strcmp(word[0], "techs") == 0 && n == 1) {
        c->kind = C_TECHS;
        return true;
    }

    if (strcmp(word[0], "limit") == 0 && n == 2) {
        c->kind = C_LIMIT;
        if (!numbers(c, word, 1, 2, err, errsz)) {
            return false;
        }
        if (c->arg[0] < 1) {
            snprintf(err, errsz, "limit is at least 1");
            return false;
        }
        return true;
    }

    snprintf(err, errsz, "unknown command '%s'", body);
    return false;
}

/* The whole script is read and checked before anything runs, so a typo
 * on line 40 does not cost a run to find. */
bool probe_parse_route(Route *r, const char *path, FILE *err)
{
    FILE *fh = fopen(path, "r");
    if (fh == NULL) {
        fprintf(err, "%s: %s\n", path, strerror(errno));
        return false;
    }

    char buf[LINE_MAX_LEN];
    int  line = 0;
    bool ok   = true;

    while (fgets(buf, sizeof buf, fh) != NULL) {
        line++;
        char *hash = strchr(buf, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char *body = trim(buf);
        if (*body == '\0') {
            continue;
        }

        if (r->count == r->cap) {
            r->cap = (r->cap == 0) ? 64 : r->cap * 2;
            r->cmd = realloc(r->cmd, (size_t)r->cap * sizeof *r->cmd);
            if (r->cmd == NULL) {
                fclose(fh);
                return false;
            }
        }
        Cmd *c = &r->cmd[r->count];
        memset(c, 0, sizeof *c);
        c->line = line;
        snprintf(c->text, sizeof c->text, "%s", body);

        char msg[256];
        if (!probe_parse_line(c, body, msg, sizeof msg)) {
            fprintf(err, "%s:%d: %s\n", path, line, msg);
            ok = false;
            continue;
        }
        if ((c->kind == C_START) != (r->count == 0)) {
            fprintf(err, "%s:%d: %s\n", path, line,
                    (c->kind == C_START) ? "only one start, and first"
                                         : "the route has to begin with "
                                           "start");
            ok = false;
        }
        r->count++;
    }
    fclose(fh);

    if (ok && r->count == 0) {
        fprintf(err, "%s: empty route\n", path);
        ok = false;
    }
    return ok;
}


static bool copy_file(const char *from, const char *to)
{
    /* (build 91) `start slot N FILE` with FILE the route's own
     * saves/slotN.json -- what story.py's recover() passes after a wipe:
     * the session's last save -- opened `to` for writing, which emptied
     * `from` before a byte was read, and the CONTINUE said the file
     * "will not load". (build 113) So the whole of `from` is read before
     * `to` is opened, and the same file is written back as it was. Build
     * 91 skipped the copy when the two had one device and inode instead,
     * and Windows has no inode (st_ino is 0 for every file): any two files
     * on one drive were "the same", and a session's second CONTINUE loaded
     * its first one's save again. */
    FILE *in = fopen(from, "rb");
    if (in == NULL) {
        return false;
    }
    char  *data = NULL;
    size_t len  = 0;
    size_t cap  = 0;
    bool   ok   = true;
    for (;;) {
        if (len == cap) {
            size_t grown = (cap == 0) ? 16384 : cap * 2;
            char  *more  = realloc(data, grown);
            if (more == NULL) {
                ok = false;
                break;
            }
            data = more;
            cap  = grown;
        }
        size_t n = fread(data + len, 1, cap - len, in);
        if (n == 0) {
            break;
        }
        len += n;
    }
    ok = ok && !ferror(in);
    fclose(in);
    FILE *out = NULL;
    if (ok) {
        out = fopen(to, "wb");
    }
    if (out == NULL) {
        free(data);
        return false;
    }
    ok = (len == 0) || (fwrite(data, 1, len, out) == len);
    ok = (fclose(out) == 0) && ok;
    free(data);
    return ok;
}

/* ------------------------------------------------------------------ */
/* The run                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    u32 tick;
    u16 id;
} ScriptSeen;

static Framebuffer g_fb;          /* shots and pixel checks draw here */
static ProbeHost   g_host;
static Game       *g_gp;
static Video      *g_vp;

static u32    g_tick;
static int    g_limit = DEFAULT_LIMIT;

static ScriptSeen *g_seen;
static int         g_seen_count, g_seen_cap;
static int         g_seen_cursor;   /* expect script looks from here */
static u32         g_command_mark[4];   /* expect command counts from here */
static u32         g_joined_mark;       /* expect enemy-joined counts from here */
/* (build 160) expect rumble counts from here, a kind each; rumble-log's
 * file, its level and the motors it last wrote. */
static u32          g_rumble_mark[RUMBLE_FELT_KINDS];
static FILE        *g_rumble_log;
static RumbleLevel  g_rumble_log_level;
static RumbleMotors g_rumble_log_last;

static char  **g_errline;
static int     g_errline_count, g_errline_cap;

static char    g_fail[LINE_MAX_LEN];
static int     g_shots;

static GameDebugState g_prev;

void probe_log(const char *fmt, ...)
{
    char    buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    g_host.log(g_host.ctx, g_tick, buf);
}

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_fail, sizeof g_fail, fmt, ap);
    va_end(ap);
    probe_log("    FAIL  %s", g_fail);
}

/* Every music id that reaches the sequencer, and the $E0 fade, into the
 * log by tick: "music $90". Effects are left out -- the cursor alone
 * would bury them. */
static u8 g_music_last;

void probe_on_sound(void *ctx, u8 id)
{
    (void)ctx;
    if (id >= 0x81 && id <= 0x9F) {
        g_music_last = id;
        probe_log("    *     music $%02X", id);
    } else if (id == 0xE0) {
        probe_log("    *     music fade ($E0)");
    }
}

void probe_on_script(void *ctx, u16 id)
{
    (void)ctx;
    if (g_seen_count == g_seen_cap) {
        g_seen_cap = (g_seen_cap == 0) ? 256 : g_seen_cap * 2;
        g_seen = realloc(g_seen, (size_t)g_seen_cap * sizeof *g_seen);
    }
    g_seen[g_seen_count].tick = g_tick;
    g_seen[g_seen_count].id   = id;
    g_seen_count++;
    probe_log("    *     script $%04X", id);
}

void probe_stderr_line(const char *text)
{
    if (g_errline_count == g_errline_cap) {
        g_errline_cap = (g_errline_cap == 0) ? 64 : g_errline_cap * 2;
        g_errline = realloc(g_errline,
                            (size_t)g_errline_cap * sizeof *g_errline);
    }
    size_t n = strlen(text) + 1;
    g_errline[g_errline_count] = malloc(n);
    memcpy(g_errline[g_errline_count], text, n);
    g_errline_count++;
    probe_log("    |     %s", text);
}

/* ---- the game ---- */

/* (build 106) A level in the log by the name levels.json gives it
 * ("Paseo", "SkureB1"); its number, as $XX, only when it has none. The
 * route language still takes the number (routes are read before the
 * game's tables are). */
static const char *level_label(int level, char *buf, size_t n)
{
    const char *name = gamedata_level_name(level);
    if (name != NULL) {
        return name;
    }
    snprintf(buf, n, "$%02X", (unsigned)level & 0xFF);
    return buf;
}

static void describe(const GameDebugState *st, char *out, size_t n)
{
    char where[128] = "";
    char num[8];
    if (st->level >= 0) {
        /* On the jet scooter x, y are its origin, a cell corner: the
         * cell is the one down and right of it, and the scooter covers
         * that cell's column and row and the ones up and left. */
        snprintf(where, sizeof where, ", level %s cell %d,%d facing %s%s%s",
                 level_label(st->level, num, sizeof num),
                 st->x / 16, st->y / 16,
                 (st->facing == FACE_UP) ? "up"
                 : (st->facing == FACE_DOWN) ? "down"
                 : (st->facing == FACE_LEFT) ? "left" : "right",
                 (st->step_frames != 0) ? " mid-step" : "",
                 st->riding ? " riding" : "");
    }
    snprintf(out, n, "screen %s%s%s, %s%s%s",
             SCREEN_NAME[st->screen], st->entered ? "" : " (loading)",
             st->fading ? " (fading)" : "",
             (st->wait == GAME_WAIT_NONE) ? "busy: " : "waiting: ",
             (st->wait == GAME_WAIT_NONE)
                 ? ((st->busy != NULL) ? st->busy : "?")
                 : WAIT_NAME[st->wait],
             where);
}

/* Dark Force's curse in a member's status word (loc_2E82, loc_1C1C):
 * bit 4 once cursed, the kind in bits 0-2 while it is pending (4-6) or
 * repeats every turn (1-3), cleared to 0 once 4-6 has taken -- "marked",
 * never cursed again -- and bit 10 when kind 5 sealed the techniques. */
#define STATUS_CURSED   0x0010
#define STATUS_KIND     0x0007
#define STATUS_SEALED   0x0400
#define STATUS_DEBAN    0x0200  /* Deban: the blast and blows halved, for
                                   the rest of the fight */

static void describe_curse(u16 status, char *out, size_t n)
{
    out[0] = '\0';
    size_t used = 0;
    if (status & STATUS_CURSED) {
        if (status & STATUS_KIND) {
            used += (size_t)snprintf(out, n, " cursed %d", status & STATUS_KIND);
        } else {
            used += (size_t)snprintf(out, n, " marked");
        }
    }
    if ((status & STATUS_SEALED) && used < n) {
        used += (size_t)snprintf(out + used, n - used, " sealed");
    }
    if ((status & STATUS_DEBAN) && used < n) {
        snprintf(out + used, n - used, " deban");
    }
}

/* The party, once a game is on: ", party Rolf L3 21/38 4/9 Nei L2 ...,
 * meseta 120" -- level, hp/max, tp/max, then "dead" or "poison" when it
 * applies, Dark Force's curse ("cursed K", "marked", "sealed") and Deban. Empty before Start, when there is no party. Only `state`
 * prints it, so the wait and walk lines stay as they were. */
static void describe_party(const GameDebugState *st, char *out, size_t n)
{
    out[0] = '\0';
    size_t used = 0;
    /* Where the pad is: the building, and the window whose poll holds
     * it with its cursor, so a driver can pick a stop. */
    if (st->building >= 0) {
        used += (size_t)snprintf(out + used, n - used, ", building %d",
                                 st->building);
    }
    if (st->wait == GAME_WAIT_MENU && used < n) {
        used += (size_t)snprintf(out + used, n - used, ", menu $%02X",
                                 st->menu);
        if (st->menu_stops > 0 && used < n) {
            used += (size_t)snprintf(out + used, n - used, " stop %d/%d%s%s",
                                     st->menu_stop, st->menu_stops,
                                     st->menu_across ? " across" : "",
                                     st->menu_party ? " party" : "");
        }
    }
    if (used >= n) {
        return;
    }
    out += used;
    n   -= used;
    if (st->screen == SCREEN_SEGA || st->screen == SCREEN_TITLE
        || st->party_count <= 0) {
        return;
    }
    used = (size_t)snprintf(out, n, ", party");
    for (int m = 0; m < st->party_count && used < n; m++) {
        char curse[32];
        describe_curse(st->party[m].status, curse, sizeof curse);
        used += (size_t)snprintf(out + used, n - used,
                                 " %s L%d %d/%d %d/%d%s%s%s",
                                 game_debug_char_name(st->party[m].id),
                                 st->party[m].level,
                                 st->party[m].hp, st->party[m].max_hp,
                                 st->party[m].tp, st->party[m].max_tp,
                                 (st->party[m].hp == 0) ? " dead" : "",
                                 (st->party[m].status & 0x8000)
                                     ? " poison" : "",
                                 curse);
    }
    if (used < n) {
        used += (size_t)snprintf(out + used, n - used, ", meseta %u",
                                 st->meseta);
    }
    /* In a fight: ", formation 3 enemies 5:23/23 5:0/23" -- each live
     * enemy slot as id:hp/max. */
    if (st->formation != 0 && used < n) {
        used += (size_t)snprintf(out + used, n - used,
                                 ", formation %d enemies", st->formation);
        for (int e = 0; e < st->enemy_count && used < n; e++) {
            used += (size_t)snprintf(out + used, n - used, " %d:%d/%d",
                                     st->enemy[e].id, st->enemy[e].hp,
                                     st->enemy[e].max_hp);
        }
    }
}

static void state(GameDebugState *st)
{
    game_debug_state(g_gp, st);
}

/* Screen and level changes, as they happen, into the log. */
static void observe(void)
{
    GameDebugState st;
    state(&st);

    if (st.entered && (!g_prev.entered || st.screen != g_prev.screen)) {
        probe_log("    *     screen %s", SCREEN_NAME[st.screen]);
    }
    if (st.level >= 0 && st.entered
        && (st.level != g_prev.level || !g_prev.entered
            || st.screen != g_prev.screen)) {
        char num[8];
        probe_log("    *     level %s, leader at cell %d,%d",
                  level_label(st.level, num, sizeof num),
                  st.x / 16, st.y / 16);
    }
    if (st.building >= 0 && st.entered
        && (st.building != g_prev.building || !g_prev.entered)) {
        probe_log("    *     building %d", st.building);
    }
    if (st.level_event != 0 && st.level_event != g_prev.level_event) {
        probe_log("    *     event $%03X", st.level_event);
    }
    if (st.riding != g_prev.riding && st.level >= 0) {
        probe_log("    *     %s", st.riding ? "riding the scooter" : "on foot");
    }
    g_prev = st;
}

/* (build 110) `sound render`: each tick's audio through audio_render, on
 * this thread, before the host throws the queue away; the peak of each of
 * the last SOUND_TICKS ticks, for `expect sound playing|silent`.
 * (build 143) And each tick's sharpest bend, for `expect sound clicks
 * below`: the largest second difference x[n] - 2x[n-1] + x[n-2] of either
 * side, the last two frames of the tick before carried over. A click is a
 * step or spike, sharp at every frequency; the FM music's waveform bends
 * far less from one sample to the next. */
#define SOUND_TICKS 4

static bool   g_sound_render;
static double g_sound_owed;
static float  g_sound_peak[SOUND_TICKS];
static float  g_sound_bend[SOUND_TICKS];
static float  g_sound_last[2][AUDIO_CHANNELS];  /* [0] the last frame */
static int    g_sound_history;  /* frames in g_sound_last, up to 2 */
static int    g_sound_at;
static int    g_sound_rendered;

static void render_sound(void)
{
    static float buf[AUDIO_RATE / 10 * AUDIO_CHANNELS];
    Audio *a = g_gp->audio;

    if (a == NULL) {
        return;
    }
    /* (build 111) The AUDIO page's volumes, as main.c hands them to the
     * mixer -- the probe has no main.c, and renders only here. */
    audio_set_volume(a, g_gp->sysmenu.audio.master_db,
                     g_gp->sysmenu.audio.music_db,
                     g_gp->sysmenu.audio.effects_db);
    audio_set_oversample(a, g_gp->sysmenu.audio.oversample);
    /* (build 112) Q's switches, the same way */
    audio_set_synth_option(a, SYNTH_OPT_VOICES, g_gp->sysmenu.audio.voices);
    audio_set_synth_option(a, SYNTH_OPT_TAILS, g_gp->sysmenu.audio.tails);
    audio_set_synth_option(a, SYNTH_OPT_PATCHES,
                           g_gp->sysmenu.audio.patches ? 1 : 0);
    audio_set_synth_option(a, SYNTH_OPT_GRAIN,
                           g_gp->sysmenu.audio.grain ? 1 : 0);
    g_sound_owed += (double)AUDIO_RATE / TICK_HZ;
    int frames = (int)g_sound_owed;
    g_sound_owed -= frames;
    if (frames > AUDIO_RATE / 10) {
        frames = AUDIO_RATE / 10;
    }
    audio_render(a, buf, frames);
    float peak = 0.0f;
    for (int i = 0; i < frames * AUDIO_CHANNELS; i++) {
        float v = (buf[i] < 0.0f) ? -buf[i] : buf[i];
        peak = (v > peak) ? v : peak;
    }
    float bend = 0.0f;
    for (int f = 0; f < frames; f++) {
        for (int s = 0; s < AUDIO_CHANNELS; s++) {
            float x = buf[f * AUDIO_CHANNELS + s];
            if (g_sound_history == 2) {
                float b  = x - 2.0f * g_sound_last[0][s] + g_sound_last[1][s];
                float ab = (b < 0.0f) ? -b : b;
                bend = (ab > bend) ? ab : bend;
            }
            g_sound_last[1][s] = g_sound_last[0][s];
            g_sound_last[0][s] = x;
        }
        if (g_sound_history < 2) {
            g_sound_history++;
        }
    }
    g_sound_peak[g_sound_at] = peak;
    g_sound_bend[g_sound_at] = bend;
    g_sound_at = (g_sound_at + 1) % SOUND_TICKS;
    g_sound_rendered++;
}

/* (build 124a) `expect sound-records` (above): the audio thread's record
 * of each track, as a switch replays it. The probe renders on its own
 * thread, so the record is the one the last render drained. */
static bool expect_sound_records(const Cmd *c)
{
    const AudioShadow *s = &g_gp->audio->live;
    int  stray = 0;
    char where[256] = "";
    for (int t = 0; t < SMPS_TRACKS; t++) {
        u8 id = s->track_chan[t];
        if ((id & 0x80) || (id & 7) == 3 || (id & 7) == 7) {
            continue;
        }
        int own = ((id & 7) < 3) ? (id & 7) : (id & 7) - 1;
        for (int p = 0; p < 2; p++) {
            for (int reg = 0x30; reg < 0xB8; reg++) {
                if (!(s->track_set[t][p][reg >> 3] & (1 << (reg & 7)))) {
                    continue;
                }
                if (p == 0 && (reg == 0xB4 || (reg >= 0xA8 && reg <= 0xAE))) {
                    continue;
                }
                if ((reg & 3) == 3 || p * 3 + (reg & 3) == own) {
                    continue;
                }
                if (stray++ < 4) {
                    size_t len = strlen(where);
                    snprintf(where + len, sizeof where - len,
                             " track %d (FM%d) holds FM%d's $%02X;", t,
                             own + 1, p * 3 + (reg & 3) + 1, reg);
                }
            }
        }
    }
    if (stray == 0) {
        probe_log("    ok");
        return true;
    }
    fail("line %d (%s): %d registers off their track's channel:%s", c->line,
         c->text, stray, where);
    return false;
}

/* One tick of `in`. */
static void tick_input(const Input *in)
{
    g_host.tick(g_host.ctx, in);
    g_tick++;
    if (g_sound_render) {
        render_sound();
    }
    if (g_host.after_tick != NULL) {
        g_host.after_tick(g_host.ctx);
    }
    if (g_rumble_log != NULL && g_host.rumble != NULL) {
        RumbleMotors m = rumble_scale(rumble_motors(g_host.rumble),
                                      g_rumble_log_level);
        if (m.low != g_rumble_log_last.low
            || m.high != g_rumble_log_last.high) {
            fprintf(g_rumble_log, "%u 1 %04X %04X\n", g_tick, m.low, m.high);
            fflush(g_rumble_log);
            g_rumble_log_last = m;
        }
    }
    observe();
}

static void tick(const Buttons *held, const Buttons *pressed)
{
    Input in;
    input_clear(&in);       /* (build 160) no key, no gamepad input */
    if (held != NULL) {
        in.held = *held;
    }
    if (pressed != NULL) {
        in.pressed = *pressed;
    }
    tick_input(&in);
}

/* (build 160) `key` and `pad`: a key or gamepad input pressed as it is
 * and held `ticks` ticks, made what the platform makes of it
 * (platform_sdl3.c's latch_key, pad_event and the held state) through the
 * Escape menu's controls -- the ones main.c hands the platform -- then a
 * tick of nothing. Input.key or pad_input, Backspace and the presses only
 * in the first tick, as the platform reports a press once. */
static void raw_press(int key, int pad, int ticks)
{
    const Controls *c = &g_gp->sysmenu.controls;
    Buttons game, nav;
    memset(&game, 0, sizeof game);
    memset(&nav, 0, sizeof nav);
    Input in;
    input_clear(&in);
    if (key != KEY_NONE) {
        in.key = key;
        buttons_from_actions(&game, controls_key_actions(c, key));
        game.menu = key == KEY_ESCAPE;
        in.back   = key == KEY_BACKSPACE;
        nav.up    = key == KEY_UP;
        nav.down  = key == KEY_DOWN;
        nav.left  = key == KEY_LEFT;
        nav.right = key == KEY_RIGHT;
        nav.start = key == KEY_RETURN;
    } else {
        PadHeld h;
        memset(&h, 0, sizeof h);
        h.held[pad]  = true;
        in.pad_input = pad;
        buttons_from_actions(&game, controls_pad_actions(c, &h));
        buttons_from_actions(&nav, pad_nav_actions(&h));
        game.menu = pad == PAD_BACK;
    }
    in.held        = game;
    in.pressed     = game;
    in.nav_held    = nav;
    in.nav_pressed = nav;
    tick_input(&in);
    for (int t = 1; t < ticks; t++) {
        Input still;
        input_clear(&still);
        still.held     = game;
        still.nav_held = nav;
        tick_input(&still);
    }
    tick(NULL, NULL);
}

static void set_button(Buttons *b, int button)
{
    *((bool *)((u8 *)b + BUTTON_OFFSET[button])) = true;
}

static bool shot(const char *out, const char *name)
{
    char path[OUT_LEN + LINE_MAX_LEN + 8];
    snprintf(path, sizeof path, "%s/%s.png", out, name);

    if (g_host.write_png == NULL) {
        /* (build 100b) The window draws its own frames; the command
         * thread must not render under it. */
        probe_log("    *     shot %s (not drawn here)", name);
        return true;
    }
    game_render(g_gp);
    video_compose(g_vp, &g_fb);
    game_overlay(g_gp, &g_fb);
    if (!g_host.write_png(g_host.ctx, path, &g_fb, &g_vp->palette)) {
        fail("shot: cannot write %s", path);
        return false;
    }
    g_shots++;
    probe_log("    *     shot %s.png", name);
    return true;
}

/* ---- commands ---- */

typedef bool (*Until)(const GameDebugState *st, const Cmd *c);

static bool until_idle(const GameDebugState *st, const Cmd *c)
{
    (void)c;
    return st->wait != GAME_WAIT_NONE;
}

static bool until_screen(const GameDebugState *st, const Cmd *c)
{
    return (int)st->screen == c->sub && st->entered && !st->fading;
}

static int seen_from_cursor(u16 id)
{
    for (int i = g_seen_cursor; i < g_seen_count; i++) {
        if (g_seen[i].id == id) {
            return i;
        }
    }
    return -1;
}

static bool until_script(const GameDebugState *st, const Cmd *c)
{
    (void)st;
    return seen_from_cursor((u16)c->arg[0]) >= 0;
}

static bool until_level(const GameDebugState *st, const Cmd *c)
{
    return st->screen == SCREEN_LEVEL && st->entered && !st->fading
        && st->level == (int)c->arg[0];
}

static bool until_ending(const GameDebugState *st, const Cmd *c)
{
    return st->ending_moment != NULL
        && strcmp(st->ending_moment, c->str) == 0;
}

static bool until_rest(const GameDebugState *st, const Cmd *c)
{
    (void)c;
    return st->step_frames == 0;
}

/* Checked before every tick, so a wait that is already met costs none. */
static bool wait_until(const Cmd *c, Until done)
{
    u32 start = g_tick;
    GameDebugState st;

    for (;;) {
        state(&st);
        if (done(&st, c)) {
            char d[256];
            describe(&st, d, sizeof d);
            probe_log("    ok    after %u ticks: %s", g_tick - start, d);
            return true;
        }
        if ((int)(g_tick - start) >= g_limit) {
            char d[256];
            describe(&st, d, sizeof d);
            fail("line %d (%s): not after %d ticks -- %s", c->line, c->text,
                 g_limit, d);
            return false;
        }
        tick(NULL, NULL);
    }
}

/* (build 101) Up to `most` ticks, until the game waits on the pad (any
 * wait: text at a stop, a menu, control). Running out is not a failure:
 * the caller looks at the state and decides. */
static bool wait_pad(long most)
{
    u32 start = g_tick;
    GameDebugState st;
    char d[256];

    for (;;) {
        state(&st);
        if (st.wait != GAME_WAIT_NONE) {
            describe(&st, d, sizeof d);
            probe_log("    ok    after %u ticks: %s", g_tick - start, d);
            return true;
        }
        if ((long)(g_tick - start) >= most) {
            describe(&st, d, sizeof d);
            probe_log("    ok    still busy after %u ticks: %s",
                      g_tick - start, d);
            return true;
        }
        tick(NULL, NULL);
    }
}

static bool do_walk(const Cmd *c)
{
    GameDebugState st;
    state(&st);

    if (!st.can_walk) {
        char d[256];
        describe(&st, d, sizeof d);
        fail("line %d (%s): the leader is not under the pad -- %s", c->line,
             c->text, d);
        return false;
    }

    int  want    = (int)c->arg[0];
    u32  first   = st.steps;
    u32  start   = g_tick;
    int  blocked = 0;
    bool new_press = true;

    for (;;) {
        int taken = (int)(st.steps - first);
        if (taken >= want) {
            probe_log("    ok    %d of %d steps", taken, want);
            return true;
        }
        if (!st.can_walk) {
            char d[256];
            describe(&st, d, sizeof d);
            probe_log("    ok    the game took the pad after %d of %d steps -- "
                  "%s", taken, want, d);
            return true;
        }
        if ((int)(g_tick - start) >= g_limit) {
            char d[256];
            describe(&st, d, sizeof d);
            fail("line %d (%s): %d of %d steps after %d ticks -- %s",
                 c->line, c->text, taken, want, g_limit, d);
            return false;
        }

        Buttons held, pressed;
        memset(&held, 0, sizeof held);
        memset(&pressed, 0, sizeof pressed);
        set_button(&held, c->sub);
        if (new_press) {
            set_button(&pressed, c->sub);
            new_press = false;
        }

        bool at_rest = (st.step_frames == 0);
        u32  before  = st.steps;
        tick(&held, &pressed);
        state(&st);

        /* Standing still with the direction held and the pad his: a wall,
         * or someone in the way who may yet move off. */
        if (at_rest && st.steps == before && st.can_walk) {
            if (++blocked >= BLOCK_GRACE) {
                char d[256];
                describe(&st, d, sizeof d);
                fail("line %d (%s): blocked for %d ticks after %d of %d "
                     "steps -- %s", c->line, c->text, BLOCK_GRACE,
                     (int)(st.steps - first), want, d);
                return false;
            }
        } else {
            blocked = 0;
        }
    }
}

/* ---- fuzz ---- */

static u32 g_fz;        /* the fuzzer's own xorshift, apart from the game's */

/* `keep-equipment`: the fuzz never takes EQP in the C menu, so what the
 * party starts with stays on -- the fuzz otherwise strips far more than
 * it equips, and an empty-handed character is on Defend every fight. */
static bool g_keep_equipment;
#define FZ_PLAYER_MENU 0x01     /* WinID_PlayerMenu */
#define FZ_MENU_EQP    4        /* ITEM STATE TECH STRNG EQP */

static u32 fz_next(void)
{
    g_fz ^= g_fz << 13;
    g_fz ^= g_fz >> 17;
    g_fz ^= g_fz << 5;
    return g_fz;
}

static int fz_pick(int n)
{
    return (int)(fz_next() % (u32)n);
}

static void fz_press(int button)
{
    Buttons b;
    memset(&b, 0, sizeof b);
    set_button(&b, button);
    tick(&b, &b);
    tick(NULL, NULL);
}

/* do_walk without the failures: hold until the steps have begun, the
 * game takes the pad, or the leader has stood blocked for a second. */
static void fz_walk(int dir, int steps)
{
    GameDebugState st;
    state(&st);
    u32  first   = st.steps;
    int  blocked = 0;
    bool new_press = true;

    while ((int)(st.steps - first) < steps && st.can_walk && blocked < 60) {
        Buttons held, pressed;
        memset(&held, 0, sizeof held);
        memset(&pressed, 0, sizeof pressed);
        set_button(&held, dir);
        if (new_press) {
            set_button(&pressed, dir);
            new_press = false;
        }
        bool at_rest = (st.step_frames == 0);
        u32  before  = st.steps;
        tick(&held, &pressed);
        state(&st);
        if (at_rest && st.steps == before && st.can_walk) {
            blocked++;
        } else {
            blocked = 0;
        }
    }
}

/* A coarse picture of progress: if a few hundred inputs in a row change
 * none of it, the game may be ignoring the pad in a state the wait test
 * takes for a wait. Said once per stretch, as a warning. */
static u32 fz_progress(const GameDebugState *st)
{
    return (u32)st->screen * 2654435761u ^ (u32)(st->level + 1) * 40503u
         ^ st->steps * 2246822519u ^ (u32)g_seen_count * 3266489917u
         ^ (u32)(st->x << 16 | (st->y & 0xFFFF))
         ^ (st->commands[0] + st->commands[1] + st->commands[2]
            + st->commands[3]) * 2654435789u;
}

/* The battle's menus, played on purpose. Random moves then C, which is
 * what every other menu gets, mostly picks FIGHT: an ORDR that ends in
 * a stored command takes STGY, ORDR, the party triangle, the command
 * triangle and often a list and a target, each with a B in four against
 * it -- and the triangles move on left/right, the lamps on up/down, so
 * half the random moves do nothing. So every command stayed where
 * loc_F550 / loc_F584 left it (an unarmed Nei on Defend for good).
 *
 * Here the stop is chosen for what it does, the cursor is walked to it
 * in its own direction (every one wraps), and C takes it; sometimes B
 * backs out instead. Returns false when this is not a battle menu with
 * a cursor it can read, for the ordinary handling. */
static bool fz_battle_menu(const GameDebugState *st)
{
    if (st->screen != SCREEN_BATTLE || st->menu_stops <= 0) {
        return false;
    }

    int  r      = fz_pick(100);
    int  target = -1;                   /* -1: B */
    const char *what;

    if (st->menu_party) {
        what   = "party";
        target = (r < 90) ? fz_pick(st->menu_stops) : -1;
    } else {
        switch (st->menu) {
            case WIN_BATTLE_OPTIONS: {
                /* B does nothing here (Win_BattleOptions). */
                what   = "FIGHT/STGY";
                target = (r < 50) ? 0 : 1;
                break;
            }
            case WIN_BATTLE_OPTIONS2: {
                what   = "ORDR/RUN";
                target = (r < 85) ? 0 : (r < 95) ? 1 : -1;
                break;
            }
            case WIN_BATTLE_COMMANDS: {
                what   = "commands";
                target = (r < 95) ? fz_pick(st->menu_stops) : -1;
                break;
            }
            case WIN_BATTLE_TECH_LIST: {
                what   = "techniques";
                target = (r < 90) ? fz_pick(st->menu_stops) : -1;
                break;
            }
            case WIN_BATTLE_ITEM_LIST: {
                what   = "items";
                target = (r < 90) ? fz_pick(st->menu_stops) : -1;
                break;
            }
            case WIN_ENEMY_GROUPS: {
                what   = "groups";
                target = (r < 95) ? fz_pick(st->menu_stops) : -1;
                break;
            }
            default: {
                return false;
            }
        }
    }

    if (target < 0) {
        probe_log("    fuzz  %s: b", what);
        fz_press(1);
        return true;
    }

    /* Down or right steps the stop forward; stops presses always get
     * there. Stop early if the menu goes away under us. */
    int dir = st->menu_across ? BUTTON_UP + 3 : BUTTON_UP + 1;
    GameDebugState now = *st;
    for (int i = 0; i < st->menu_stops && now.menu_stop != target; i++) {
        fz_press(dir);
        state(&now);
        if (now.wait != GAME_WAIT_MENU || now.menu != st->menu
            || now.menu_party != st->menu_party || now.menu_stops <= 0) {
            probe_log("    fuzz  %s: the menu went away moving to stop %d",
                  what, target);
            return true;
        }
    }
    probe_log("    fuzz  %s: stop %d of %d, c", what, now.menu_stop,
          st->menu_stops);
    fz_press(2);
    return true;
}

/* (build 117) The title's and the pause's input: Start. */
static void fz_start(const GameDebugState *st)
{
    probe_log("    fuzz  start (%s)", WAIT_NAME[st->wait]);
    fz_press(3);
}

/* (build 117) A menu that is not the fight's own, or an event waiting on
 * the pad: up to three moves of the cursor, then C three times in four,
 * else B -- and B for C on the C menu's EQP under `keep-equipment`. */
static void fz_moves_press(const GameDebugState *st)
{
    int moves = fz_pick(4);
    for (int i = 0; i < moves; i++) {
        fz_press(BUTTON_UP + fz_pick(4));
    }
    int b = (fz_pick(4) < 3) ? 2 : 1;
    if (g_keep_equipment && b == 2) {
        GameDebugState now;
        state(&now);
        if (now.wait == GAME_WAIT_MENU
            && now.menu == FZ_PLAYER_MENU
            && now.menu_stop == FZ_MENU_EQP) {
            b = 1;
        }
    }
    probe_log("    fuzz  %d moves, %s (%s)", moves, BUTTON_NAME[b],
              WAIT_NAME[st->wait]);
    fz_press(b);
}

static bool do_fuzz(const Cmd *c)
{
    u32 end = g_tick + (u32)c->arg[0];
    g_fz = (c->nargs > 1 && c->arg[1] != 0) ? (u32)c->arg[1] : 0x9E3779B9u;

    u32 busy_since = g_tick;
    u32 last_prog  = 0;
    int same_prog  = 0;
    int actions    = 0;
    GameDebugState st;
    state(&st);
    u32 begun[4];
    memcpy(begun, st.commands, sizeof begun);

    while (g_tick < end) {
        state(&st);
        /* FIGHT repeats the same commands round after round until a
         * button interrupts it (loc_F1D0), so a round is a pad wait too:
         * after a while in one, press something, as a player would. */
        if (st.wait == GAME_WAIT_NONE && st.screen == SCREEN_BATTLE
            && (int)(g_tick - busy_since) >= 1200) {
            probe_log("    fuzz  c (interrupting the fight)");
            fz_press(2);
            busy_since = g_tick;
            continue;
        }
        if (st.wait == GAME_WAIT_NONE) {
            if ((int)(g_tick - busy_since) >= g_limit) {
                char d[256];
                describe(&st, d, sizeof d);
                fail("line %d (%s): busy for %d ticks -- %s", c->line,
                     c->text, g_limit, d);
                return false;
            }
            tick(NULL, NULL);
            continue;
        }

        u32 prog = fz_progress(&st);
        if (prog == last_prog) {
            if (++same_prog == 300) {
                char d[256];
                describe(&st, d, sizeof d);
                probe_log("    WARN  fuzz: 300 inputs with no progress -- %s", d);
            }
        } else {
            same_prog = 0;
            last_prog = prog;
        }

        actions++;
        switch (st.wait) {
            case GAME_WAIT_TITLE: {
                fz_start(&st);
                break;
            }
            case GAME_WAIT_PAUSED: {
                fz_start(&st);
                break;
            }
            case GAME_WAIT_CONTROL: {
                int r = fz_pick(100);
                if (r < 70) {
                    int dir   = BUTTON_UP + fz_pick(4);
                    int steps = 1 + fz_pick(6);
                    probe_log("    fuzz  walk %s %d", BUTTON_NAME[dir], steps);
                    fz_walk(dir, steps);
                } else if (r < 90) {
                    probe_log("    fuzz  a");
                    fz_press(0);
                } else if (r < 96) {
                    probe_log("    fuzz  c (menu)");
                    fz_press(2);
                } else {
                    int n = 1 + fz_pick(30);
                    probe_log("    fuzz  wait %d", n);
                    for (int i = 0; i < n; i++) {
                        tick(NULL, NULL);
                    }
                }
                break;
            }
            case GAME_WAIT_TEXT: {
                int r = fz_pick(10);
                int b = (r < 8) ? 2 : (r < 9) ? 0 : 1;
                probe_log("    fuzz  %s (text)", BUTTON_NAME[b]);
                fz_press(b);
                break;
            }
            case GAME_WAIT_MENU: {
                if (!fz_battle_menu(&st)) {
                    fz_moves_press(&st);
                }
                break;
            }
            case GAME_WAIT_EVENT: {
                fz_moves_press(&st);
                break;
            }
            default: {
                tick(NULL, NULL);
                break;
            }
        }
        busy_since = g_tick;
    }
    state(&st);
    probe_log("    ok    %d inputs; commands begun: attack %u, tech %u, "
          "item %u, defend %u", actions, st.commands[0] - begun[0],
          st.commands[1] - begun[1], st.commands[2] - begun[2],
          st.commands[3] - begun[3]);
    return true;
}

static bool do_start(const Cmd *c, const char *saves)
{
    switch ((StartKind)c->sub) {
        case START_NEW: {
            /* The probe is at power-on already. COMPUTER PLAYS presses
             * the console's reset: the game is under way. (build 170)
             * So does the probe once a tick has run: a playthrough's
             * second try at Paseo went on with the first try's game --
             * seed 5 in JAPANESE, Nei dead and 11 meseta, never out of
             * Paseo in six tries -- where the window's starts anew. Every
             * route's `start new` is its first command, at tick 0. */
            if (g_host.player_only || g_tick > 0) {
                game_debug_reset(g_gp);
            }
            return true;
        }
        case START_SLOT: {
            char from[LINE_MAX_LEN + 32], to[PATH_LEN + 32];
            if (c->str[0] != '\0') {
                snprintf(from, sizeof from, "%s", c->str);
            } else {
                snprintf(from, sizeof from, "saves/slot%ld.json", c->arg[0]);
            }
            snprintf(to, sizeof to, "%s/slot%ld.json", saves, c->arg[0]);
            if (!copy_file(from, to)) {
                fail("line %d (%s): cannot copy %s", c->line, c->text, from);
                return false;
            }
            if (g_host.player_only) {
                /* (build 100b) COMPUTER PLAYS: the save is in its own
                 * slot (its own memory card); the reset, and the title's
                 * CONTINUE is the pad's (play.continue_game). */
                game_debug_reset(g_gp);
                return true;
            }
            if (!game_debug_continue(g_gp, (int)c->arg[0])) {
                fail("line %d (%s): %s will not load", c->line, c->text,
                     from);
                return false;
            }
            return true;
        }
        case START_LEVEL: {
            game_debug_level(g_gp, (int)c->arg[0], (int)c->arg[1],
                             (int)c->arg[2]);
            return true;
        }
        case START_BUILDING: {
            long cx = (c->nargs == 6) ? c->arg[4] : -1;
            long cy = (c->nargs == 6) ? c->arg[5] : -1;
            game_debug_building(g_gp, (int)c->arg[0], (int)c->arg[1],
                                (int)c->arg[2], (int)c->arg[3], (int)cx,
                                (int)cy);
            return true;
        }
        case START_BATTLE: {
            game_debug_battle(g_gp, (int)c->arg[0]);
            return true;
        }
        default: {
            return false;
        }
    }
}

/* (build 100b) What a player can do: the pad, waiting, looking at what
 * the screen and the menus show, and the console's power and reset. The
 * rest writes the game's state from outside it. docs/computer_plays.md,
 * the audit. */
static const CmdKind NOT_A_PLAYERS[] = {
    C_FLAG, C_MEMBERS, C_MEMBER, C_OUTFIT, C_WARP, C_ONESHOT, C_SAVE_HERE,
    C_FUZZ, C_PARTY, C_ENEMY_HP, C_KEEP_EQUIPMENT, C_GIVE, C_MESETA, C_EXP,
    C_DEFENSE, C_EXPECT_PIXEL, C_DISPLAY, C_SYNTH, C_SOUND_RENDER,
    C_SOUND_PLAY, C_EXPECT_PICTURE, C_JOIN_NEXT, C_RUMBLE_LOG,
    C_KEY, C_PAD, C_PAD_STYLE,      /* (build 160) the computer plays the
                                       pad, with no keyboard or gamepad */
    C_LANGUAGE                      /* (build 163) the menu's, from outside */
};

static bool player_command(const Cmd *c)
{
    if (c->kind == C_START) {
        return c->sub == START_NEW || c->sub == START_SLOT;
    }
    for (size_t i = 0; i < sizeof NOT_A_PLAYERS / sizeof NOT_A_PLAYERS[0];
         i++) {
        if (c->kind == NOT_A_PLAYERS[i]) {
            return false;
        }
    }
    return true;
}

bool probe_run(const Cmd *c, const char *out, const char *saves)
{
    probe_log("> %-4d %s", c->line, c->text);
    if (g_host.player_only && !player_command(c)) {
        fail("line %d (%s): not a player's -- refused", c->line, c->text);
        return false;
    }

    switch (c->kind) {
        case C_START: {
            return do_start(c, saves);
        }
        case C_PRESS: {
            Buttons b;
            memset(&b, 0, sizeof b);
            set_button(&b, c->sub);
            tick(&b, &b);
            tick(NULL, NULL);
            return true;
        }
        case C_KEY: {
            raw_press((int)c->arg[0], PAD_NONE, (int)c->arg[1]);
            return true;
        }
        case C_PAD: {
            raw_press(KEY_NONE, (int)c->arg[0], (int)c->arg[1]);
            return true;
        }
        case C_PAD_STYLE: {
            sysmenu_set_pad_style(&g_gp->sysmenu, (PadStyle)c->arg[0]);
            probe_log("    pad style %s", pad_style_key((PadStyle)c->arg[0]));
            return true;
        }
        case C_EXPECT_CONTROLS: {
            char s0[16], s1[16], shown[40];
            sysmenu_slot_text(&g_gp->sysmenu, c->sub != 0, (int)c->arg[0], 0,
                              s0, sizeof s0);
            sysmenu_slot_text(&g_gp->sysmenu, c->sub != 0, (int)c->arg[0], 1,
                              s1, sizeof s1);
            snprintf(shown, sizeof shown, "%s|%s", s0, s1);
            if (strcmp(shown, c->str) == 0) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): it shows %s / %s", c->line, c->text, s0, s1);
            return false;
        }
        case C_WALK: {
            return do_walk(c);
        }
        case C_WAIT_IDLE: {
            return wait_until(c, until_idle);
        }
        case C_WAIT_SCREEN: {
            return wait_until(c, until_screen);
        }
        case C_WAIT_STEP: {
            return wait_until(c, until_rest);
        }
        case C_WAIT_SCRIPT: {
            if (!wait_until(c, until_script)) {
                return false;
            }
            int i = seen_from_cursor((u16)c->arg[0]);
            probe_log("    shown at tick %u", g_seen[i].tick);
            g_seen_cursor = i + 1;
            return true;
        }
        case C_WAIT_LEVEL: {
            return wait_until(c, until_level);
        }
        case C_WAIT_ENDING: {
            return wait_until(c, until_ending);
        }
        case C_WAIT_TICKS: {
            for (long i = 0; i < c->arg[0]; i++) {
                tick(NULL, NULL);
            }
            return true;
        }
        case C_WAIT_PAD: {
            return wait_pad(c->arg[0]);
        }
        case C_SHOT: {
            return shot(out, c->str);
        }
        case C_EXPECT_SCREEN: {
            GameDebugState st;
            state(&st);
            if ((int)st.screen != c->sub) {
                char d[256];
                describe(&st, d, sizeof d);
                fail("line %d (%s): %s", c->line, c->text, d);
                return false;
            }
            probe_log("    ok");
            return true;
        }
        case C_EXPECT_LEVEL: {
            GameDebugState st;
            state(&st);
            bool ok = st.level == (int)c->arg[0];
            if (ok && c->nargs == 3) {
                ok = st.x / 16 == (int)c->arg[1] && st.y / 16 == (int)c->arg[2];
            }
            if (!ok) {
                char d[256];
                describe(&st, d, sizeof d);
                fail("line %d (%s): %s", c->line, c->text, d);
                return false;
            }
            probe_log("    ok");
            return true;
        }
        case C_EXPECT_SCRIPT: {
            for (int i = g_seen_cursor; i < g_seen_count; i++) {
                if (g_seen[i].id == (u16)c->arg[0]) {
                    probe_log("    ok    shown at tick %u", g_seen[i].tick);
                    g_seen_cursor = i + 1;
                    return true;
                }
            }
            char list[LINE_MAX_LEN] = "";
            int  shown = 0;
            for (int i = g_seen_cursor; i < g_seen_count; i++) {
                char one[16];
                snprintf(one, sizeof one, "%s$%04X", shown ? " " : "",
                         g_seen[i].id);
                if (strlen(list) + strlen(one) + 4 >= sizeof list) {
                    strcat(list, " ...");
                    break;
                }
                strcat(list, one);
                shown++;
            }
            fail("line %d (%s): not shown%s -- shown since: %s", c->line,
                 c->text, (g_seen_cursor > 0) ? " after the last match" : "",
                 shown ? list : "nothing");
            return false;
        }
        case C_EXPECT_COMMAND: {
            GameDebugState st;
            state(&st);
            u32 n = st.commands[c->sub] - g_command_mark[c->sub];
            if (n > 0) {
                probe_log("    ok    %u begun", n);
                g_command_mark[c->sub] = st.commands[c->sub];
                return true;
            }
            fail("line %d (%s): none begun%s -- attack %u, tech %u, item %u, "
                 "defend %u in all", c->line, c->text,
                 (g_command_mark[c->sub] > 0) ? " since the last match" : "",
                 st.commands[0], st.commands[1], st.commands[2],
                 st.commands[3]);
            return false;
        }
        case C_EXPECT_JOINED: {
            GameDebugState st;
            state(&st);
            u32 n = st.enemies_joined - g_joined_mark;
            if (n > 0) {
                probe_log("    ok    %u joined", n);
                g_joined_mark = st.enemies_joined;
                return true;
            }
            fail("line %d (%s): no enemy joined%s -- %u in all", c->line,
                 c->text, (g_joined_mark > 0) ? " since the last match" : "",
                 st.enemies_joined);
            return false;
        }
        case C_EXPECT_ITEM: {
            int item = gamedata_item_id(c->str);
            if (item <= 0) {
                fail("line %d (%s): no item is called %s", c->line, c->text,
                     c->str);
                return false;
            }
            GameData *gd = game_debug_data(g_gp);
            int last = gd->party_members_num;
            if (last >= GD_PARTY_MAX) {
                last = GD_PARTY_MAX - 1;
            }
            for (int pos = 0; pos <= last; pos++) {
                if (c->arg[0] >= 0 && pos != (int)c->arg[0]) {
                    continue;
                }
                u8 *count;
                u8 *bag = gamedata_bag(gd, gd->party_member_id[pos], &count);
                for (int i = 0; i < *count && i < GD_ITEM_SLOTS; i++) {
                    if ((bag[i] & 0x7F) == item) {
                        probe_log("    ok    position %d, slot %d", pos, i);
                        return true;
                    }
                }
            }
            char b[1024];
            game_debug_bags(g_gp, b, sizeof b);
            fail("line %d (%s): not carried -- bags %s", c->line, c->text, b);
            return false;
        }
        case C_EXPECT_PERSON: {
            char b[2048], key[16];
            game_debug_objects(g_gp, b, sizeof b);
            snprintf(key, sizeof key, ":1/$%02lX:", c->arg[0] & 0xFF);
            bool there = strstr(b, key) != NULL;
            if (there == (c->arg[1] != 0)) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): objects %s", c->line, c->text, b);
            return false;
        }
        case C_EXPECT_PIXEL: {
            game_render(g_gp);
            video_compose(g_vp, &g_fb);
            game_overlay(g_gp, &g_fb);
            int at = c->arg[1] * SCREEN_W + c->arg[0];
            /* (build 146a) A picture's pixel has no palette entry: it
             * matches when it shows the colour the CRAM word does. */
            if (g_fb.pic[at] != 0) {
                Rgb8 got  = g_fb.rgb[at];
                Rgb8 want = cram_to_rgb8((Cram)c->arg[2]);
                if (got.r == want.r && got.g == want.g && got.b == want.b) {
                    probe_log("    ok");
                    return true;
                }
                fail("line %d (%s): a picture's pixel, #%02X%02X%02X",
                     c->line, c->text, got.r, got.g, got.b);
                return false;
            }
            u8 ix = g_fb.px[at];
            Cram v = (ix >= FB_OVERLAY_BASE) ? g_fb.overlay[ix - FB_OVERLAY_BASE]
                                             : g_vp->palette.entry[ix];
            if (v == (Cram)c->arg[2]) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): index %u, $%03X", c->line, c->text,
                 (unsigned)ix, (unsigned)v);
            return false;
        }
        case C_EXPECT_PICTURE: {
            game_render(g_gp);
            video_compose(g_vp, &g_fb);
            game_overlay(g_gp, &g_fb);
            int b = g_fb.pic[c->arg[1] * SCREEN_W + c->arg[0]];
            const Picture *p = (b > 0) ? g_fb.pictures[(b - 1)
                                                       % VIDEO_PICTURES_MAX]
                                       : NULL;
            const char *got = (p != NULL) ? p->thing : "none";
            if (strcmp(got, c->str) == 0) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): %s", c->line, c->text, got);
            return false;
        }
        case C_EXPECT_OBJECT: {
            static const char *const SEEN[] = { "absent", "hidden",
                                                "visible" };
            int v = game_debug_object(g_gp, (int)c->arg[0]);
            if (v == (int)c->arg[1]) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): object $%02lX is %s", c->line, c->text,
                 c->arg[0], SEEN[v + 1]);
            return false;
        }
        case C_EXPECT_RIDING: {
            GameDebugState st;
            game_debug_state(g_gp, &st);
            if ((st.riding ? 1 : 0) == c->arg[0]) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): the party is %s", c->line, c->text,
                 st.riding ? "riding the scooter" : "on foot");
            return false;
        }
        case C_EXPECT_SETTING: {
            /* (build 100c) `computer on|off`: COMPUTER PLAYS's lamp.
             * (build 120) `speed`: SPEED's value by its index. (build 160)
             * `rumble`: RUMBLE's level. */
            if (c->arg[1] == 4) {
                const char *key = lang_key(g_gp->sysmenu.language);
                if (key != NULL && strcmp(key, c->str) == 0) {
                    probe_log("    ok");
                    return true;
                }
                fail("line %d (%s): it is %s", c->line, c->text,
                     (key != NULL) ? key : "none");
                return false;
            }
            if (c->arg[1] == 3) {
                RumbleLevel level = g_gp->sysmenu.rumble;
                if ((long)level == c->arg[0]) {
                    probe_log("    ok");
                    return true;
                }
                fail("line %d (%s): it is %s", c->line, c->text,
                     rumble_level_key(level));
                return false;
            }
            if (c->arg[1] == 1) {
                int speed = g_gp->sysmenu.computer_speed;
                if (speed == c->arg[0]) {
                    probe_log("    ok");
                    return true;
                }
                fail("line %d (%s): it is %s", c->line, c->text,
                     sysmenu_speed_key(speed));
                return false;
            }
            bool on = (c->arg[1] == 2) ? g_gp->sysmenu.computer_on
                                       : g_gp->sysmenu.fullscreen;
            if ((on ? 1 : 0) == c->arg[0]) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): it is %s", c->line, c->text,
                 on ? "on" : "off");
            return false;
        }
        case C_EXPECT_RUMBLE: {
            /* (build 160) The host's watcher: a kind's count since the
             * last look at it, or the motors now. */
            if (g_host.rumble == NULL) {
                fail("line %d (%s): no rumble here", c->line, c->text);
                return false;
            }
            if (c->sub == 1) {
                RumbleMotors m = rumble_motors(g_host.rumble);
                bool going = m.low != 0 || m.high != 0;
                if ((going ? 1 : 0) == c->arg[0]) {
                    probe_log("    ok    motors %04X %04X", m.low, m.high);
                    return true;
                }
                fail("line %d (%s): the motors are %04X %04X", c->line,
                     c->text, m.low, m.high);
                return false;
            }
            int  k     = (int)c->arg[0];
            u32  count = g_host.rumble->felt_count[k] - g_rumble_mark[k];
            g_rumble_mark[k] = g_host.rumble->felt_count[k];
            if ((long)count == c->arg[1]) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): %s felt %u times", c->line, c->text,
                 rumble_kind_name(k), count);
            return false;
        }
        case C_RUMBLE_LOG: {
            if (g_host.rumble == NULL) {
                fail("line %d (%s): no rumble here", c->line, c->text);
                return false;
            }
            char path[OUT_LEN + LINE_MAX_LEN + 8];
            snprintf(path, sizeof path, "%s/%s", out, c->str);
            if (g_rumble_log != NULL) {
                fclose(g_rumble_log);
            }
            /* "wb": the same bytes on every OS, as the route's other
             * files (build 113). */
            g_rumble_log = fopen(path, "wb");
            if (g_rumble_log == NULL) {
                fail("line %d (%s): cannot write %s", c->line, c->text,
                     path);
                return false;
            }
            g_rumble_log_level = (RumbleLevel)c->arg[0];
            g_rumble_log_last.low  = 0;
            g_rumble_log_last.high = 0;
            probe_log("    *     rumble log %s at %s", c->str,
                      rumble_level_key(g_rumble_log_level));
            return true;
        }
        case C_DISPLAY: {
            crt_display_parse(c->str, &g_gp->sysmenu.crt_display);
            probe_log("    display %s", c->str);
            return true;
        }
        case C_EXPECT_CRT: {
            char value[32], why[32], shown[72];
            sysmenu_crt_value(&g_gp->sysmenu, (SysMenuValue)c->arg[0],
                              value, sizeof value, why, sizeof why);
            snprintf(shown, sizeof shown, "%s|%s", value, why);
            if (strcmp(shown, c->str) == 0) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): it shows \"%s\"%s%s", c->line, c->text,
                 value, (why[0] != '\0') ? " / " : "", why);
            return false;
        }
        case C_EXPECT_AUDIO: {
            char value[32], why[32], shown[72];
            sysmenu_audio_value(&g_gp->sysmenu, (SysMenuAudioValue)c->arg[0],
                                value, sizeof value, why, sizeof why);
            snprintf(shown, sizeof shown, "%s|%s", value, why);
            if (strcmp(shown, c->str) == 0) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): it shows \"%s\"%s%s", c->line, c->text,
                 value, (why[0] != '\0') ? " / " : "", why);
            return false;
        }
        case C_SYNTH: {
            if (g_gp->audio == NULL) {
                fail("line %d (%s): no audio", c->line, c->text);
                return false;
            }
            int on = audio_set_synth(g_gp->audio, (int)c->arg[0]);
            probe_log("    synth %s", synth_backend((SynthId)on)->name);
            return true;
        }
        case C_SOUND_PLAY: {
            if (g_gp->audio == NULL) {
                fail("line %d (%s): no audio", c->line, c->text);
                return false;
            }
            audio_play_sfx(g_gp->audio, (u8)c->arg[0]);
            return true;
        }
        case C_SOUND_RENDER: {
            g_sound_render = c->arg[0] != 0;
            g_sound_rendered = 0;
            memset(g_sound_peak, 0, sizeof g_sound_peak);
            memset(g_sound_bend, 0, sizeof g_sound_bend);
            memset(g_sound_last, 0, sizeof g_sound_last);
            g_sound_history = 0;
            return true;
        }
        case C_EXPECT_SOUND: {
            if (g_gp->audio == NULL) {
                fail("line %d (%s): no audio", c->line, c->text);
                return false;
            }
            if (c->sub == 0) {
                u32 count = 0;
                u32 hash  = audio_write_hash(g_gp->audio, &count);
                if (count == (u32)c->arg[0] && hash == (u32)c->arg[1]) {
                    probe_log("    ok");
                    return true;
                }
                fail("line %d (%s): %u writes, hash $%08X", c->line, c->text,
                     (unsigned)count, (unsigned)hash);
                return false;
            }
            if (c->sub == 2) {
                return expect_sound_records(c);
            }
            if (c->sub == 3) {
                u16 stuck = audio_stuck_tracks(g_gp->audio);
                if (stuck == (u16)c->arg[0]) {
                    probe_log("    ok");
                    return true;
                }
                fail("line %d (%s): music tracks lent with no effect $%02X,"
                     " left turned down by a pause $%02X", c->line, c->text,
                     (unsigned)(stuck & 0xFF), (unsigned)(stuck >> 8));
                return false;
            }
            if (c->sub == 5) {
                u8 held = audio_register_written(g_gp->audio,
                                                 (int)c->arg[0] - 1,
                                                 (u8)c->arg[1]);
                if (held == (u8)c->arg[2]) {
                    probe_log("    ok");
                    return true;
                }
                fail("line %d (%s): the sequencer last wrote $%02X there",
                     c->line, c->text, (unsigned)held);
                return false;
            }
            if (g_sound_rendered < SOUND_TICKS) {
                fail("line %d (%s): %d ticks rendered (sound render on, "
                     "then %d ticks)", c->line, c->text, g_sound_rendered,
                     SOUND_TICKS);
                return false;
            }
            if (c->sub == 4) {
                float bend = 0.0f;
                for (int i = 0; i < SOUND_TICKS; i++) {
                    bend = (g_sound_bend[i] > bend) ? g_sound_bend[i] : bend;
                }
                double bdb = 20.0 * log10((bend > 1e-9f) ? (double)bend
                                                         : 1e-9);
                if (bdb < (double)c->arg[0]) {
                    probe_log("    ok (sharpest bend %.1f dBFS)", bdb);
                    return true;
                }
                fail("line %d (%s): sharpest bend %.1f dBFS over the last "
                     "%d ticks", c->line, c->text, bdb, SOUND_TICKS);
                return false;
            }
            float peak = 0.0f;
            for (int i = 0; i < SOUND_TICKS; i++) {
                peak = (g_sound_peak[i] > peak) ? g_sound_peak[i] : peak;
            }
            double db = 20.0 * log10((peak > 1e-9f) ? (double)peak : 1e-9);
            bool playing = db > -40.0;
            if (playing == (c->arg[0] != 0)) {
                probe_log("    ok (peak %.1f dBFS)", db);
                return true;
            }
            fail("line %d (%s): peak %.1f dBFS over the last %d ticks",
                 c->line, c->text, db, SOUND_TICKS);
            return false;
        }
        case C_EXPECT_ESCAPE_MENU: {
            if ((g_gp->sysmenu.open ? 1 : 0) == c->arg[0]) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): the Escape menu is %s", c->line, c->text,
                 g_gp->sysmenu.open ? "open" : "closed");
            return false;
        }
        case C_EXPECT_MUSIC: {
            if (g_music_last == (u8)c->arg[0]) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): the last music was $%02X", c->line, c->text,
                 (unsigned)g_music_last);
            return false;
        }
        case C_EXPECT_SONG: {
            u8 song = (g_gp->audio != NULL)
                    ? audio_music_started(g_gp->audio) : 0;
            if (song == (u8)c->arg[0]) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): the song playing is $%02X", c->line, c->text,
                 (unsigned)song);
            return false;
        }
        case C_EXPECT_FLAG: {
            GameData *gd = game_debug_data(g_gp);
            u8 v = gd->c700[c->arg[0] - 0xC700];
            if (v == (u8)c->arg[1]) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): $%04lX is $%02X", c->line, c->text,
                 c->arg[0], (unsigned)v);
            return false;
        }
        case C_FLAG: {
            GameData *gd = game_debug_data(g_gp);
            gd->c700[c->arg[0] - 0xC700] = (u8)c->arg[1];
            probe_log("    ok    $%04lX = $%02lX", c->arg[0], c->arg[1]);
            return true;
        }
        case C_JOIN_NEXT: {
            GameData *gd = game_debug_data(g_gp);
            gd->party_member_join_next = (u16)c->arg[0];
            probe_log("    ok    party_member_join_next = %ld", c->arg[0]);
            return true;
        }
        case C_EXPECT_JOIN_NEXT: {
            GameData *gd = game_debug_data(g_gp);
            if (gd->party_member_join_next == (u16)c->arg[0]) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): party_member_join_next is %u", c->line,
                 c->text, (unsigned)gd->party_member_join_next);
            return false;
        }
        case C_EXPECT_HIDAPIPE: {
            GameDebugState st;
            state(&st);
            if (st.hidapipe == (c->arg[0] != 0)) {
                probe_log("    ok");
                return true;
            }
            fail("line %d (%s): the Hidapipe is %s", c->line, c->text,
                 st.hidapipe ? "on" : "off");
            return false;
        }
        case C_EXPECT_MENU: {
            GameDebugState st;
            state(&st);
            if (c->arg[0] < 0) {
                if (st.wait != GAME_WAIT_MENU && st.window_depth == 0) {
                    probe_log("    ok    no menu");
                    return true;
                }
                fail("line %d (%s): menu $%02X holds the pad (%d windows)",
                     c->line, c->text, st.menu, st.window_depth);
                return false;
            }
            if (st.wait == GAME_WAIT_MENU && st.menu == (int)c->arg[0]
                && (c->arg[1] < 0 || st.menu_stop == (int)c->arg[1])) {
                probe_log("    ok");
                return true;
            }
            if (st.wait != GAME_WAIT_MENU) {
                fail("line %d (%s): no menu holds the pad", c->line, c->text);
            } else {
                fail("line %d (%s): menu $%02X stop %d", c->line, c->text,
                     st.menu, st.menu_stop);
            }
            return false;
        }
        case C_EXPECT_PARTY: {
            GameData *gd = game_debug_data(g_gp);
            int count = (int)gd->party_members_num + 1;
            bool same = (count == c->nargs);
            for (int i = 0; same && i < count; i++) {
                if ((gd->party_member_id[i] & 7) != c->arg[i]) {
                    same = false;
                }
            }
            if (same) {
                probe_log("    ok    %d members", count);
                return true;
            }
            fail("line %d (%s): the party is %d members: %d %d %d %d",
                 c->line, c->text, count, gd->party_member_id[0],
                 gd->party_member_id[1], gd->party_member_id[2],
                 gd->party_member_id[3]);
            return false;
        }
        case C_EXPECT_HP: {
            GameData *gd = game_debug_data(g_gp);
            int pos = (int)c->arg[0];
            if (pos > (int)gd->party_members_num) {
                fail("line %d (%s): nobody is in party position %d", c->line,
                     c->text, pos);
                return false;
            }
            int hp = gd->chr[gd->party_member_id[pos] & 7].hp;
            if (hp >= c->arg[1] && hp <= c->arg[2]) {
                probe_log("    ok    %d", hp);
                return true;
            }
            fail("line %d (%s): hp is %d", c->line, c->text, hp);
            return false;
        }
        case C_EXPECT_STAT: {
            GameData *gd = game_debug_data(g_gp);
            int pos = (int)c->arg[0];
            if (pos > (int)gd->party_members_num) {
                fail("line %d (%s): nobody is in party position %d", c->line,
                     c->text, pos);
                return false;
            }
            const CharData *ch = &gd->chr[gd->party_member_id[pos] & 7];
            int v = ch->hp;
            if (strcmp(c->str, "tp") == 0) {
                v = ch->tp;
            } else if (strcmp(c->str, "attack") == 0) {
                v = ch->attack;
            } else if (strcmp(c->str, "defense") == 0) {
                v = ch->defense;
            } else if (strcmp(c->str, "agility") == 0) {
                v = ch->agility;
            }
            if (v >= c->arg[1] && v <= c->arg[2]) {
                probe_log("    ok    %s %d", c->str, v);
                return true;
            }
            fail("line %d (%s): %s is %d", c->line, c->text, c->str, v);
            return false;
        }
        case C_EXPECT_TEXT: {
            u8  want[PROBE_LINE_MAX];
            int nwant = encode_text(c->str, want, (int)sizeof want);
            u8  have[WIN_TEXT_BUF];
            int nhave = game_debug_text(g_gp, have, (int)sizeof have);
            for (int i = 0; nwant > 0 && i + nwant <= nhave; i++) {
                if (memcmp(have + i, want, (size_t)nwant) == 0) {
                    probe_log("    ok");
                    return true;
                }
            }
            char shown[PROBE_LINE_MAX * 2];
            decode_text(have, nhave, shown, sizeof shown);
            fail("line %d (%s): the window's text is \"%s\"", c->line,
                 c->text, shown);
            return false;
        }
        case C_EXPECT_NO_STDERR: {
            if (g_host.flush_stderr != NULL) {
                g_host.flush_stderr(g_host.ctx);
            }
            for (int i = 0; i < g_errline_count; i++) {
                if (strstr(g_errline[i], c->str) != NULL) {
                    fail("line %d (%s): \"%s\"", c->line, c->text,
                         g_errline[i]);
                    return false;
                }
            }
            probe_log("    ok");
            return true;
        }
        case C_LIMIT: {
            g_limit = (int)c->arg[0];
            return true;
        }
        case C_FUZZ: {
            return do_fuzz(c);
        }
        case C_MEMBERS: {
            int ids[4];
            for (int i = 0; i < c->nargs; i++) {
                ids[i] = (int)c->arg[i];
            }
            game_debug_members(g_gp, ids, c->nargs);
            probe_log("    ok    %d members", c->nargs);
            return true;
        }
        case C_MEMBER: {
            game_debug_member_level(g_gp, (int)c->arg[0], (int)c->arg[1]);
            probe_log("    ok");
            return true;
        }
        case C_OUTFIT: {
            int item[5];
            char names[LINE_MAX_LEN];
            snprintf(names, sizeof names, "%s", c->str);
            /* (build 105) split, not strtok_r: POSIX's, which MinGW's
             * headers do not promise under -std=c11. */
            char *word[5];
            int   words = split(names, word, 5);
            for (int i = 0; i < 5; i++) {
                const char *tok = (i < words) ? word[i] : NULL;
                if (tok == NULL || strcmp(tok, "-") == 0) {
                    item[i] = 0;
                    continue;
                }
                item[i] = gamedata_item_id(tok);
                if (item[i] <= 0) {
                    fail("line %d (%s): unknown item %s", c->line, c->text,
                         tok);
                    return false;
                }
            }
            if (!game_debug_outfit(g_gp, (int)c->arg[0], item)) {
                fail("line %d (%s): no room in the bag", c->line, c->text);
                return false;
            }
            probe_log("    ok");
            return true;
        }
        case C_SAVE_HERE: {
            GameDebugState st;
            game_debug_state(g_gp, &st);
            if (st.screen != SCREEN_LEVEL || st.level < 0) {
                probe_log("    FAIL  save-here: not on the level screen with "
                      "the party");
                return false;
            }
            GameData *gd = game_debug_data(g_gp);
            gd->level_index = (u16)st.level;
            gd->level_y     = (u16)(st.y & ~0xF);
            gd->level_x     = (u16)(st.x & ~0xF);
            /* As the Data Memory's save (loc_B5A4) by Visiphone. */
            gamedata_set_word(gd, GD_LAST_SAVE_VISIPHONE, 1);
            gamedata_set_word(gd, GD_LAST_SAVE_PLANET, gd->planet);
            gamedata_set_word(gd, GD_LAST_SAVE_MAP, gd->level_index);
            gamedata_set_word(gd, GD_LAST_SAVE_Y, gd->level_y);
            gamedata_set_word(gd, GD_LAST_SAVE_X, gd->level_x);
            gamedata_set_word(gd, GD_SLOT_IN_USE, 0xFFFF);
            gd->save_level = gd->chr[0].level;
            if (!gamedata_save(gd, (int)c->arg[0] - 1)) {
                probe_log("    FAIL  save-here: slot %ld not written", c->arg[0]);
                return false;
            }
            char num[8];
            probe_log("    ok    slot %ld: level %s cell %d,%d", c->arg[0],
                      level_label(st.level, num, sizeof num),
                      st.x >> 4, st.y >> 4);
            return true;
        }
        case C_LANGUAGE: {
            int i = lang_find(c->str);
            if (i < 0) {
                fail("line %d (%s): no language %s", c->line, c->text,
                     c->str);
                return false;
            }
            g_gp->sysmenu.language = i;
            probe_log("    ok    %s from the next tick", lang_name(i));
            return true;
        }
        case C_ONESHOT: {
            game_debug_oneshot(g_gp, (int)c->arg[0]);
            probe_log("    ok    one-shot case $%02lX at the next load", c->arg[0]);
            return true;
        }
        case C_WARP: {
            game_debug_warp(g_gp, (int)c->arg[0], (int)c->arg[1],
                            (int)c->arg[2]);
            probe_log("    ok");
            return true;
        }
        case C_PARTY: {
            game_debug_party(g_gp, (int)c->arg[0], (int)c->arg[1]);
            probe_log("    ok    %ld members at level %ld", c->arg[0], c->arg[1]);
            return true;
        }
        case C_ENEMY_HP: {
            game_debug_enemy_hp(g_gp, (int)c->arg[0]);
            probe_log("    ok    every live enemy's HP set to %ld", c->arg[0]);
            return true;
        }
        case C_KEEP_EQUIPMENT: {
            g_keep_equipment = true;
            return true;
        }
        case C_GIVE: {
            int item = gamedata_item_id(c->str);
            if (item <= 0) {
                fail("line %d (give): no item is called %s", c->line, c->str);
                return false;
            }
            GameData *gd = game_debug_data(g_gp);
            int pos = (int)c->arg[0];
            if (pos > (int)gd->party_members_num) {
                fail("line %d (give): nobody is in party position %d", c->line, pos);
                return false;
            }
            u8 *count;
            u8 *items = gamedata_bag(gd, gd->party_member_id[pos], &count);
            if (*count >= GD_ITEM_SLOTS) {
                fail("line %d (give): that bag is full", c->line);
                return false;
            }
            items[(*count)++] = (u8)item;
            probe_log("    ok    %s into position %d's bag", c->str, pos);
            return true;
        }
        case C_DEFENSE: {
            GameData *gd = game_debug_data(g_gp);
            int pos = (int)c->arg[0];
            if (pos > (int)gd->party_members_num) {
                fail("line %d (defense): nobody is in party position %d",
                     c->line, pos);
                return false;
            }
            gd->chr[gd->party_member_id[pos] & 7].defense = (u16)c->arg[1];
            probe_log("    ok    defense %ld", c->arg[1]);
            return true;
        }
        case C_MESETA: {
            GameData *gd = game_debug_data(g_gp);
            gd->money = (u32)c->arg[0];
            probe_log("    ok    meseta %u", (unsigned)gd->money);
            return true;
        }
        case C_EXP: {
            GameData *gd = game_debug_data(g_gp);
            char b[512];
            size_t used = 0;
            for (int id = 0; id < 8 && used < sizeof b; id++) {
                used += (size_t)snprintf(b + used, sizeof b - used,
                                         "%s%s:%lu", (id == 0) ? "" : " ",
                                         game_debug_char_name(id),
                                         (unsigned long)gd->chr[id]
                                             .experience);
            }
            probe_log("    ok    exp %s", b);
            return true;
        }
        case C_ROSTER: {
            /* Everyone who has joined (character ids 0 to
             * party_members_joined, the order they join in), in the
             * party or not: name, level, HP/max, TP/max, and '*' for a
             * party member. A driver choosing whom to take needs the
             * ones at home too. */
            GameData *gd = game_debug_data(g_gp);
            char b[1024];
            size_t used = 0;
            b[0] = '\0';
            for (int id = 0; id <= gd->party_members_joined && id < GD_CHARS
                 && used < sizeof b; id++) {
                const CharData *ch = &gd->chr[id];
                bool in = false;
                for (int m = 0; m <= gd->party_members_num
                     && m < GD_PARTY_MAX; m++) {
                    in = in || (gd->party_member_id[m] & 7) == id;
                }
                used += (size_t)snprintf(b + used, sizeof b - used,
                                         "%s%s:L%u:%u/%u:%u/%u%s",
                                         (id == 0) ? "" : " ",
                                         game_debug_char_name(id),
                                         (unsigned)ch->level,
                                         (unsigned)ch->hp,
                                         (unsigned)ch->max_hp,
                                         (unsigned)ch->tp,
                                         (unsigned)ch->max_tp, in ? "*" : "");
            }
            probe_log("    ok    roster %s", b);
            return true;
        }
        case C_OBJECTS: {
            char b[2048];
            game_debug_objects(g_gp, b, sizeof b);
            probe_log("    ok    objects %s", b);
            return true;
        }
        case C_TECHS: {
            char b[2048];
            game_debug_techs(g_gp, b, sizeof b);
            probe_log("    ok    techs %s", b);
            return true;
        }
        case C_BAGS: {
            char b[1024];
            game_debug_bags(g_gp, b, sizeof b);
            probe_log("    ok    bags %s", b);
            return true;
        }
        case C_HANDS: {
            /* Each member's right hand, then left (EQUIP_RIGHT, EQUIP_LEFT;
             * a two-hander is listed in both), "-" for an empty hand. */
            const GameData *gd = game_debug_data(g_gp);
            char b[512];
            size_t used = 0;
            b[0] = '\0';
            int members = gd->party_members_num + 1;
            if (members > GD_PARTY_MAX) {
                members = GD_PARTY_MAX;
            }
            for (int m = 0; m < members && used < sizeof b; m++) {
                int id = gd->party_member_id[m] % GD_CHARS;
                const CharData *ch = &gd->chr[id];
                int right = ch->equip[EQUIP_RIGHT] & 0x7F;
                int left  = ch->equip[EQUIP_LEFT] & 0x7F;
                used += (size_t)snprintf(b + used, sizeof b - used,
                                         "%s%s: %s %s", (m == 0) ? "" : "; ",
                                         game_debug_char_name(id),
                                         (right != 0) ? gamedata_item_key(right) : "-",
                                         (left != 0) ? gamedata_item_key(left) : "-");
            }
            probe_log("    ok    hands %s", b);
            return true;
        }
        case C_STATE: {
            GameDebugState st;
            char d[256], p[640];
            state(&st);
            describe(&st, d, sizeof d);
            describe_party(&st, p, sizeof p);
            probe_log("    ok    tick %u: %s%s", g_tick, d, p);
            return true;
        }
        default: {
            return false;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The host                                                           */
/* ------------------------------------------------------------------ */

void probe_attach(const ProbeHost *host)
{
    g_host = *host;
    g_gp   = host->game;
    g_vp   = host->video;

    g_tick  = 0;
    g_limit = DEFAULT_LIMIT;
    free(g_seen);
    g_seen = NULL;
    g_seen_count = g_seen_cap = g_seen_cursor = 0;
    memset(g_command_mark, 0, sizeof g_command_mark);
    g_joined_mark = 0;
    memset(g_rumble_mark, 0, sizeof g_rumble_mark);     /* (build 160) */
    if (g_rumble_log != NULL) {
        fclose(g_rumble_log);
        g_rumble_log = NULL;
    }
    for (int i = 0; i < g_errline_count; i++) {
        free(g_errline[i]);
    }
    free(g_errline);
    g_errline = NULL;
    g_errline_count = g_errline_cap = 0;
    g_fail[0] = '\0';
    g_shots = 0;
    g_music_last = 0;
    g_fz = 0;
    g_keep_equipment = false;
    memset(&g_prev, 0, sizeof g_prev);
}

void probe_begin(void)
{
    state(&g_prev);
}

int probe_run_text(char *line, int lineno, const char *out,
                   const char *saves)
{
    char *hash = strchr(line, '#');
    if (hash != NULL) {
        *hash = '\0';
    }
    char *t = trim(line);
    if (t[0] == '\0') {
        return 0;
    }
    Cmd cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.line = lineno;
    snprintf(cmd.text, sizeof cmd.text, "%s", t);
    char err[256];
    if (!probe_parse_line(&cmd, t, err, sizeof err)) {
        probe_log("    ?     %s", err);
        return 0;
    }
    return probe_run(&cmd, out, saves) ? 1 : -1;
}

u32 probe_ticks(void)
{
    return g_tick;
}

int probe_shots(void)
{
    return g_shots;
}

const char *probe_fail_text(void)
{
    return g_fail;
}
