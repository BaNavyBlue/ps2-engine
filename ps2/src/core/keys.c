#include "keys.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (build 160) Herzog Zwei's port's keys.c, with this game's actions,
 * defaults and reserved keys (keys.h).
 *
 * USB HID usage codes (SDL3's scancodes) and the names drawn for them.
 * Letters and digits are generated; this table has the rest. Five are
 * spelt out where Herzog Zwei's are signs this game's font has not got
 * (win_charset), MINUS among them: as "-" it read back as an empty slot. */
typedef struct {
    int         code;
    const char *name;
} KeyName;

static const KeyName KEY_NAMES[] = {
    { 40, "RETURN" },   { 41, "ESCAPE" },   { 42, "BACKSPC" },
    { 43, "TAB" },      { 44, "SPACE" },    { 45, "MINUS" },
    { 46, "EQUALS" },   { 47, "LBRACKET" }, { 48, "RBRACKET" },
    { 49, "BSLASH" },   { 50, "HASH" },     { 51, ";" },
    { 52, "QUOTE" },    { 53, "GRAVE" },    { 54, "COMMA" },
    { 55, "PERIOD" },   { 56, "SLASH" },    { 57, "CAPSLOCK" },
    { 70, "PRTSCR" },   { 71, "SCRLOCK" },  { 72, "PAUSE" },
    { 73, "INSERT" },   { 74, "HOME" },     { 75, "PAGEUP" },
    { 76, "DELETE" },   { 77, "END" },      { 78, "PAGEDOWN" },
    { 79, "RIGHT" },    { 80, "LEFT" },     { 81, "DOWN" },
    { 82, "UP" },       { 83, "NUMLOCK" },  { 84, "KPDIV" },
    { 85, "KPMUL" },    { 86, "KPMINUS" },  { 87, "KPPLUS" },
    { 88, "KPENTER" },  { 98, "KP0" },      { 99, "KPPERIOD" },
    { 100, "NONUS" },   { 101, "MENU" },
    { 224, "LCTRL" },   { 225, "LSHIFT" },  { 226, "LALT" },
    { 227, "LSUPER" },  { 228, "RCTRL" },   { 229, "RSHIFT" },
    { 230, "RALT" },    { 231, "RSUPER" },
};
#define KEY_NAME_COUNT ((int)(sizeof KEY_NAMES / sizeof KEY_NAMES[0]))

/* The pad inputs' positional names, settings.json's: SDL3's
 * SDL_GamepadButton in its order, then the sticks and triggers. */
static const char *const PAD_NAMES[PAD_BUTTONS] = {
    "SOUTH", "EAST", "WEST", "NORTH", "BACK", "GUIDE", "START", "LSTICK",
    "RSTICK", "L1", "R1", "DUP", "DDOWN", "DLEFT", "DRIGHT", "MISC1",
    "RPADDLE1", "LPADDLE1", "RPADDLE2", "LPADDLE2", "TOUCHPAD", "MISC2",
    "MISC3", "MISC4", "MISC5", "MISC6",
};
static const char *const AXIS_NAMES[PAD_R2 - PAD_LS_UP + 1] = {
    "LSUP", "LSDOWN", "LSLEFT", "LSRIGHT", "RSUP", "RSDOWN", "RSLEFT",
    "RSRIGHT", "L2", "R2",
};

/* What the menu draws for them: the positional names but spelt out, the
 * same on every pad unless LABELS has the pad's own. */
static const char *const PAD_LABELS[PAD_BUTTONS] = {
    "SOUTH", "EAST", "WEST", "NORTH", "BACK", "GUIDE", "START", "L3", "R3",
    "L1", "R1", "DPAD UP", "DPAD DOWN", "DPAD LEFT", "DPAD RIGHT", "MISC1",
    "RPADDLE1", "LPADDLE1", "RPADDLE2", "LPADDLE2", "TOUCHPAD", "MISC2",
    "MISC3", "MISC4", "MISC5", "MISC6",
};
static const char *const AXIS_LABELS[PAD_R2 - PAD_LS_UP + 1] = {
    "LS UP", "LS DOWN", "LS LEFT", "LS RIGHT", "RS UP", "RS DOWN",
    "RS LEFT", "RS RIGHT", "L2", "R2",
};

/* A pad's own labels, where they differ: the face buttons as SDL3 labels
 * them (SDL_GetGamepadButtonLabelForType), the rest as printed on the pad. */
typedef struct {
    PadStyle    style;
    int         input;
    const char *label;
} PadLabel;

static const PadLabel LABELS[] = {
    { PAD_STYLE_STANDARD, PAD_SOUTH, "A" },
    { PAD_STYLE_STANDARD, PAD_EAST, "B" },
    { PAD_STYLE_STANDARD, PAD_WEST, "X" },
    { PAD_STYLE_STANDARD, PAD_NORTH, "Y" },
    { PAD_STYLE_STANDARD, PAD_L1, "LB" },
    { PAD_STYLE_STANDARD, PAD_R1, "RB" },
    { PAD_STYLE_STANDARD, PAD_L2, "LT" },
    { PAD_STYLE_STANDARD, PAD_R2, "RT" },

    { PAD_STYLE_XBOX360, PAD_SOUTH, "A" },
    { PAD_STYLE_XBOX360, PAD_EAST, "B" },
    { PAD_STYLE_XBOX360, PAD_WEST, "X" },
    { PAD_STYLE_XBOX360, PAD_NORTH, "Y" },
    { PAD_STYLE_XBOX360, PAD_GUIDE, "XBOX" },
    { PAD_STYLE_XBOX360, 7, "LS CLICK" },
    { PAD_STYLE_XBOX360, 8, "RS CLICK" },
    { PAD_STYLE_XBOX360, PAD_L1, "LB" },
    { PAD_STYLE_XBOX360, PAD_R1, "RB" },
    { PAD_STYLE_XBOX360, PAD_L2, "LT" },
    { PAD_STYLE_XBOX360, PAD_R2, "RT" },

    { PAD_STYLE_XBOXONE, PAD_SOUTH, "A" },
    { PAD_STYLE_XBOXONE, PAD_EAST, "B" },
    { PAD_STYLE_XBOXONE, PAD_WEST, "X" },
    { PAD_STYLE_XBOXONE, PAD_NORTH, "Y" },
    { PAD_STYLE_XBOXONE, PAD_BACK, "VIEW" },
    { PAD_STYLE_XBOXONE, PAD_GUIDE, "XBOX" },
    { PAD_STYLE_XBOXONE, PAD_START, "MENU" },
    { PAD_STYLE_XBOXONE, 7, "LS CLICK" },
    { PAD_STYLE_XBOXONE, 8, "RS CLICK" },
    { PAD_STYLE_XBOXONE, PAD_L1, "LB" },
    { PAD_STYLE_XBOXONE, PAD_R1, "RB" },
    { PAD_STYLE_XBOXONE, 15, "SHARE" },
    { PAD_STYLE_XBOXONE, 16, "P1" },
    { PAD_STYLE_XBOXONE, 17, "P3" },
    { PAD_STYLE_XBOXONE, 18, "P2" },
    { PAD_STYLE_XBOXONE, 19, "P4" },
    { PAD_STYLE_XBOXONE, PAD_L2, "LT" },
    { PAD_STYLE_XBOXONE, PAD_R2, "RT" },

    { PAD_STYLE_PS3, PAD_SOUTH, "CROSS" },
    { PAD_STYLE_PS3, PAD_EAST, "CIRCLE" },
    { PAD_STYLE_PS3, PAD_WEST, "SQUARE" },
    { PAD_STYLE_PS3, PAD_NORTH, "TRIANGLE" },
    { PAD_STYLE_PS3, PAD_BACK, "SELECT" },
    { PAD_STYLE_PS3, PAD_GUIDE, "PS" },

    { PAD_STYLE_PS4, PAD_SOUTH, "CROSS" },
    { PAD_STYLE_PS4, PAD_EAST, "CIRCLE" },
    { PAD_STYLE_PS4, PAD_WEST, "SQUARE" },
    { PAD_STYLE_PS4, PAD_NORTH, "TRIANGLE" },
    { PAD_STYLE_PS4, PAD_BACK, "SHARE" },
    { PAD_STYLE_PS4, PAD_GUIDE, "PS" },
    { PAD_STYLE_PS4, PAD_START, "OPTIONS" },

    { PAD_STYLE_PS5, PAD_SOUTH, "CROSS" },
    { PAD_STYLE_PS5, PAD_EAST, "CIRCLE" },
    { PAD_STYLE_PS5, PAD_WEST, "SQUARE" },
    { PAD_STYLE_PS5, PAD_NORTH, "TRIANGLE" },
    { PAD_STYLE_PS5, PAD_BACK, "CREATE" },
    { PAD_STYLE_PS5, PAD_GUIDE, "PS" },
    { PAD_STYLE_PS5, PAD_START, "OPTIONS" },
    { PAD_STYLE_PS5, 15, "MUTE" },

    { PAD_STYLE_SWITCH, PAD_SOUTH, "B" },
    { PAD_STYLE_SWITCH, PAD_EAST, "A" },
    { PAD_STYLE_SWITCH, PAD_WEST, "Y" },
    { PAD_STYLE_SWITCH, PAD_NORTH, "X" },
    { PAD_STYLE_SWITCH, PAD_BACK, "MINUS" },
    { PAD_STYLE_SWITCH, PAD_GUIDE, "HOME" },
    { PAD_STYLE_SWITCH, PAD_START, "PLUS" },
    { PAD_STYLE_SWITCH, 7, "LS CLICK" },
    { PAD_STYLE_SWITCH, 8, "RS CLICK" },
    { PAD_STYLE_SWITCH, PAD_L1, "L" },
    { PAD_STYLE_SWITCH, PAD_R1, "R" },
    { PAD_STYLE_SWITCH, 15, "CAPTURE" },
    { PAD_STYLE_SWITCH, PAD_L2, "ZL" },
    { PAD_STYLE_SWITCH, PAD_R2, "ZR" },

    { PAD_STYLE_GAMECUBE, PAD_SOUTH, "A" },
    { PAD_STYLE_GAMECUBE, PAD_EAST, "X" },
    { PAD_STYLE_GAMECUBE, PAD_WEST, "B" },
    { PAD_STYLE_GAMECUBE, PAD_NORTH, "Y" },
    { PAD_STYLE_GAMECUBE, PAD_R1, "Z" },
    { PAD_STYLE_GAMECUBE, PAD_L2, "L" },
    { PAD_STYLE_GAMECUBE, PAD_R2, "R" },

    { PAD_STYLE_JOYSTICK, PAD_DPAD_UP, "HAT UP" },
    { PAD_STYLE_JOYSTICK, PAD_DPAD_DOWN, "HAT DOWN" },
    { PAD_STYLE_JOYSTICK, PAD_DPAD_LEFT, "HAT LEFT" },
    { PAD_STYLE_JOYSTICK, PAD_DPAD_RIGHT, "HAT RIGHT" },
    { PAD_STYLE_JOYSTICK, PAD_LS_UP, "AXIS UP" },
    { PAD_STYLE_JOYSTICK, PAD_LS_DOWN, "AXIS DOWN" },
    { PAD_STYLE_JOYSTICK, PAD_LS_LEFT, "AXIS LEFT" },
    { PAD_STYLE_JOYSTICK, PAD_LS_RIGHT, "AXIS RIGHT" },
};
#define LABEL_COUNT ((int)(sizeof LABELS / sizeof LABELS[0]))

static const char *const STYLE_KEYS[PAD_STYLES] = {
    "none", "standard", "xbox360", "xboxone", "ps3", "ps4", "ps5", "switch",
    "gamecube", "joystick",
};

/* The two key sets of builds up to 159, ARROWS and WASD (the platform's
 * KEYMAP then), both at once: the first slot ARROWS's, the second WASD's;
 * Enter Start. */
void controls_keys_default(Controls *c)
{
    static const int KEYS[CONTROL_ACTIONS][CONTROL_KEYS] = {
        { KEY_UP, 26 },             /* W */
        { KEY_DOWN, 22 },           /* S */
        { KEY_LEFT, 4 },            /* A */
        { KEY_RIGHT, 7 },           /* D */
        { 29, 15 },                 /* Z, L */
        { 27, 51 },                 /* X, ; */
        { 6, 52 },                  /* C, ' */
        { KEY_RETURN, KEY_NONE },
    };
    memcpy(c->key, KEYS, sizeof c->key);
}

/* Build 160's first gamepads (the platform's pad_button then): the d-pad
 * or the left stick the directions, West, South and East A, B and C, as
 * the Mega Drive pad's run left to right, and Start. */
void controls_pad_default(Controls *c)
{
    static const int PAD[CONTROL_ACTIONS][CONTROL_PAD_SLOTS] = {
        { PAD_DPAD_UP, PAD_LS_UP },
        { PAD_DPAD_DOWN, PAD_LS_DOWN },
        { PAD_DPAD_LEFT, PAD_LS_LEFT },
        { PAD_DPAD_RIGHT, PAD_LS_RIGHT },
        { PAD_WEST, PAD_NONE },
        { PAD_SOUTH, PAD_NONE },
        { PAD_EAST, PAD_NONE },
        { PAD_START, PAD_NONE },
    };
    memcpy(c->pad, PAD, sizeof c->pad);
}

void controls_default(Controls *c)
{
    controls_keys_default(c);
    controls_pad_default(c);
}

void key_name(int code, char *out, size_t n)
{
    if (code == KEY_NONE) {
        snprintf(out, n, "-");
        return;
    }
    if (code >= 4 && code <= 29) {
        snprintf(out, n, "%c", 'A' + (code - 4));
        return;
    }
    if (code >= 30 && code <= 38) {
        snprintf(out, n, "%c", '1' + (code - 30));
        return;
    }
    if (code == 39) {
        snprintf(out, n, "0");
        return;
    }
    if (code >= 58 && code <= 69) {
        snprintf(out, n, "F%d", code - 57);
        return;
    }
    if (code >= 89 && code <= 97) {
        snprintf(out, n, "KP%d", code - 88);
        return;
    }
    for (int i = 0; i < KEY_NAME_COUNT; i++) {
        if (KEY_NAMES[i].code == code) {
            snprintf(out, n, "%s", KEY_NAMES[i].name);
            return;
        }
    }
    snprintf(out, n, "KEY %d", code);
}

/* A plain decimal number, or -1. */
static int parse_number(const char *s)
{
    if (*s == '\0') {
        return -1;
    }
    for (const char *p = s; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return -1;
        }
    }
    long v = strtol(s, NULL, 10);
    return (v >= 0 && v < 512) ? (int)v : -1;
}

int key_from_name(const char *name)
{
    char up[16];
    size_t i = 0;
    for (; name[i] != '\0' && i + 1 < sizeof up; i++) {
        up[i] = (char)toupper((unsigned char)name[i]);
    }
    up[i] = '\0';
    if (strncmp(up, "KEY ", 4) == 0) {
        int v = parse_number(up + 4);
        return (v >= 0) ? v : KEY_NONE;
    }
    int number = parse_number(up);
    if (number >= 0 && strlen(up) > 1) {
        return number;              /* "29": a code; "2": the key 2 */
    }
    for (int code = 0; code < 256; code++) {
        char n[16];
        key_name(code, n, sizeof n);
        if (strcmp(n, up) == 0) {
            return code;
        }
    }
    return KEY_NONE;
}

/* Whether `input` is one of the PAD_* inputs. */
static bool pad_input_valid(int input)
{
    return (input >= 0 && input < PAD_BUTTONS)
        || (input >= PAD_LS_UP && input <= PAD_R2)
        || (input >= PAD_JOY1 && input < PAD_JOY1 + PAD_JOYS);
}

void pad_input_name(int input, char *out, size_t n)
{
    if (input >= 0 && input < PAD_BUTTONS) {
        snprintf(out, n, "%s", PAD_NAMES[input]);
    } else if (input >= PAD_LS_UP && input <= PAD_R2) {
        snprintf(out, n, "%s", AXIS_NAMES[input - PAD_LS_UP]);
    } else if (input >= PAD_JOY1 && input < PAD_JOY1 + PAD_JOYS) {
        snprintf(out, n, "JOY%d", input - PAD_JOY1 + 1);
    } else {
        snprintf(out, n, "-");
    }
}

int pad_input_from_name(const char *name)
{
    char up[16];
    size_t i = 0;
    for (; name[i] != '\0' && i + 1 < sizeof up; i++) {
        up[i] = (char)toupper((unsigned char)name[i]);
    }
    up[i] = '\0';
    if (name[i] != '\0') {
        return PAD_NONE;
    }
    for (int input = 0; input < PAD_INPUTS; input++) {
        if (!pad_input_valid(input)) {
            continue;
        }
        char n[16];
        pad_input_name(input, n, sizeof n);
        if (strcmp(n, up) == 0) {
            return input;
        }
    }
    return PAD_NONE;
}

void pad_input_label(int input, PadStyle style, char *out, size_t n)
{
    if (!pad_input_valid(input)) {
        snprintf(out, n, "-");
        return;
    }
    for (int i = 0; i < LABEL_COUNT; i++) {
        if (LABELS[i].style == style && LABELS[i].input == input) {
            snprintf(out, n, "%s", LABELS[i].label);
            return;
        }
    }
    if (input < PAD_BUTTONS) {
        snprintf(out, n, "%s", PAD_LABELS[input]);
    } else if (input <= PAD_R2) {
        snprintf(out, n, "%s", AXIS_LABELS[input - PAD_LS_UP]);
    } else {
        snprintf(out, n, "JOY %d", input - PAD_JOY1 + 1);
    }
}

const char *pad_style_key(PadStyle style)
{
    return (style >= 0 && style < PAD_STYLES) ? STYLE_KEYS[style] : "none";
}

PadStyle pad_style_from_key(const char *key, PadStyle fallback)
{
    for (int i = 0; key != NULL && i < PAD_STYLES; i++) {
        if (strcmp(key, STYLE_KEYS[i]) == 0) {
            return (PadStyle)i;
        }
    }
    return fallback;
}

bool key_bindable(int code)
{
    return code > 0 && code < 512 && code != KEY_ESCAPE
        && code != KEY_BACKSPACE;
}

bool pad_input_bindable(int input)
{
    return pad_input_valid(input) && input != PAD_BACK;
}

void controls_bind_key(Controls *c, int action, int slot, int code)
{
    if (action < 0 || action >= CONTROL_ACTIONS || slot < 0
        || slot >= CONTROL_KEYS) {
        return;
    }
    if (code != KEY_NONE) {
        for (int a = 0; a < CONTROL_ACTIONS; a++) {
            for (int s = 0; s < CONTROL_KEYS; s++) {
                if (c->key[a][s] == code) {
                    c->key[a][s] = KEY_NONE;
                }
            }
        }
    }
    c->key[action][slot] = code;
}

void controls_bind_pad(Controls *c, int action, int slot, int input)
{
    if (action < 0 || action >= CONTROL_ACTIONS || slot < 0
        || slot >= CONTROL_PAD_SLOTS) {
        return;
    }
    if (input != PAD_NONE) {
        for (int a = 0; a < CONTROL_ACTIONS; a++) {
            for (int s = 0; s < CONTROL_PAD_SLOTS; s++) {
                if (c->pad[a][s] == input) {
                    c->pad[a][s] = PAD_NONE;
                }
            }
        }
    }
    c->pad[action][slot] = input;
}

u32 controls_key_actions(const Controls *c, int code)
{
    u32 actions = 0;
    if (code == KEY_NONE) {
        return 0;
    }
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        for (int k = 0; k < CONTROL_KEYS; k++) {
            if (c->key[a][k] == code) {
                actions |= 1u << a;
            }
        }
    }
    return actions;
}

u32 controls_pad_actions(const Controls *c, const PadHeld *h)
{
    u32 actions = 0;
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        for (int s = 0; s < CONTROL_PAD_SLOTS; s++) {
            int input = c->pad[a][s];
            if (input >= 0 && input < PAD_INPUTS && h->held[input]) {
                actions |= 1u << a;
            }
        }
    }
    return actions;
}

u32 pad_nav_actions(const PadHeld *h)
{
    u32 actions = 0;
    if (h->held[PAD_DPAD_UP] || h->held[PAD_LS_UP]) {
        actions |= 1u << CONTROL_UP;
    }
    if (h->held[PAD_DPAD_DOWN] || h->held[PAD_LS_DOWN]) {
        actions |= 1u << CONTROL_DOWN;
    }
    if (h->held[PAD_DPAD_LEFT] || h->held[PAD_LS_LEFT]) {
        actions |= 1u << CONTROL_LEFT;
    }
    if (h->held[PAD_DPAD_RIGHT] || h->held[PAD_LS_RIGHT]) {
        actions |= 1u << CONTROL_RIGHT;
    }
    return actions;
}

/* An axis pushed past half either way: `minus` and `plus` its inputs. */
static void axis_held(PadHeld *h, s16 v, int minus, int plus)
{
    if (v <= -PAD_AXIS_PRESS) {
        h->held[minus] = true;
    }
    if (v >= PAD_AXIS_PRESS) {
        h->held[plus] = true;
    }
}

void pad_held_axes(PadHeld *h, const s16 axis[PAD_AXES])
{
    axis_held(h, axis[PAD_AXIS_LEFTX], PAD_LS_LEFT, PAD_LS_RIGHT);
    axis_held(h, axis[PAD_AXIS_LEFTY], PAD_LS_UP, PAD_LS_DOWN);
    axis_held(h, axis[PAD_AXIS_RIGHTX], PAD_RS_LEFT, PAD_RS_RIGHT);
    axis_held(h, axis[PAD_AXIS_RIGHTY], PAD_RS_UP, PAD_RS_DOWN);
    if (axis[PAD_AXIS_LEFT_TRIGGER] >= PAD_AXIS_PRESS) {
        h->held[PAD_L2] = true;
    }
    if (axis[PAD_AXIS_RIGHT_TRIGGER] >= PAD_AXIS_PRESS) {
        h->held[PAD_R2] = true;
    }
}

void pad_held_hat(PadHeld *h, u8 hat)
{
    if (hat & PAD_HAT_UP) {
        h->held[PAD_DPAD_UP] = true;
    }
    if (hat & PAD_HAT_DOWN) {
        h->held[PAD_DPAD_DOWN] = true;
    }
    if (hat & PAD_HAT_LEFT) {
        h->held[PAD_DPAD_LEFT] = true;
    }
    if (hat & PAD_HAT_RIGHT) {
        h->held[PAD_DPAD_RIGHT] = true;
    }
}

void pad_held_joy_axes(PadHeld *h, s16 x, s16 y)
{
    axis_held(h, x, PAD_LS_LEFT, PAD_LS_RIGHT);
    axis_held(h, y, PAD_LS_UP, PAD_LS_DOWN);
}
