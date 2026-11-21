/* (build 160) The controls' check: src/core/keys.c on its own. Herzog
 * Zwei's port's tools/hz_controls.c, with this game's defaults, and the
 * key names held to this game's font.
 *
 *     make controls_check && ./controls_check [SEEDS]
 *
 * Every pad input's name read back as itself (upper and lower case), no
 * two alike, nothing else read as one; every label of every style ten
 * characters at most of A-Z, 0-9 and space, no two alike in a style, the
 * face buttons' as SDL3 labels them (SDL_GetGamepadButtonLabelForType,
 * written out below as SDL 3.4 gave them); every key's name eight
 * characters at most, of what the game's font draws (win_charset's), none
 * "-" (an empty slot's), read back as itself; the defaults bindable, no
 * input or key in two slots, the keys the ARROWS and WASD sets of builds
 * up to 159 and the pad build 160's first. Then SEEDS (default 200) runs
 * of random bindings against a model kept here its own way -- after each
 * bind, every slot as the model has it -- and of random held inputs and
 * keys, the actions against the model's; the axes, hats and the menu's
 * directions at and either side of their edges. Prints what broke, and
 * PASS or FAIL; exit 1 on FAIL. */

#include "../src/core/keys.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_broken;

static void broke(const char *what)
{
    if (g_broken < 20) {
        printf("  %s\n", what);
    }
    g_broken++;
}

static u32 g_rng = 1;

static u32 rnd(u32 n)
{
    g_rng = g_rng * 1103515245u + 12345u;
    return ((g_rng >> 8) & 0xFFFFFF) % n;
}

static bool is_input(int i)
{
    return (i >= 0 && i < 26) || (i >= 32 && i <= 41)
        || (i >= 64 && i < 96);
}

/* SDL 3.4's labels for SOUTH, EAST, WEST, NORTH by SDL_GamepadType
 * (STANDARD, XBOX360, XBOXONE, PS3, PS4, PS5, SWITCH_PRO, GAMECUBE). */
static const struct {
    PadStyle    style;
    const char *face[4];
} SDL_FACES[] = {
    { PAD_STYLE_STANDARD, { "A", "B", "X", "Y" } },
    { PAD_STYLE_XBOX360, { "A", "B", "X", "Y" } },
    { PAD_STYLE_XBOXONE, { "A", "B", "X", "Y" } },
    { PAD_STYLE_PS3, { "CROSS", "CIRCLE", "SQUARE", "TRIANGLE" } },
    { PAD_STYLE_PS4, { "CROSS", "CIRCLE", "SQUARE", "TRIANGLE" } },
    { PAD_STYLE_PS5, { "CROSS", "CIRCLE", "SQUARE", "TRIANGLE" } },
    { PAD_STYLE_SWITCH, { "B", "A", "Y", "X" } },
    { PAD_STYLE_GAMECUBE, { "A", "X", "B", "Y" } },
};

static void check_names(void)
{
    char msg[160];
    for (int i = -1; i < 128; i++) {
        char name[16];
        pad_input_name(i, name, sizeof name);
        if (!is_input(i)) {
            if (strcmp(name, "-") != 0) {
                snprintf(msg, sizeof msg, "input %d is no input but is "
                         "named %s", i, name);
                broke(msg);
            }
            continue;
        }
        if (pad_input_from_name(name) != i) {
            snprintf(msg, sizeof msg, "%s reads as %d, not %d", name,
                     pad_input_from_name(name), i);
            broke(msg);
        }
        char lower[16];
        for (size_t k = 0; k <= strlen(name); k++) {
            lower[k] = (char)tolower((unsigned char)name[k]);
        }
        if (pad_input_from_name(lower) != i) {
            snprintf(msg, sizeof msg, "%s reads as %d", lower,
                     pad_input_from_name(lower));
            broke(msg);
        }
        if (pad_input_bindable(i) != (i != PAD_BACK)) {
            snprintf(msg, sizeof msg, "%s bindable: %d", name,
                     pad_input_bindable(i));
            broke(msg);
        }
        for (int j = 0; j < i; j++) {
            char other[16];
            pad_input_name(j, other, sizeof other);
            if (is_input(j) && strcmp(name, other) == 0) {
                snprintf(msg, sizeof msg, "%d and %d both named %s", j, i,
                         name);
                broke(msg);
            }
        }
    }
    static const char *const NOT[] = {
        "-", "", "JOY0", "JOY33", "JOY", "SOUTHX", "LS UP", "DPAD UP",
        "CROSS", "L3", "26", "0", "MISC7", "JOY 1",
        "AVERYLONGNAMEINDEEDTHATGOESON",
    };
    for (size_t k = 0; k < sizeof NOT / sizeof NOT[0]; k++) {
        if (pad_input_from_name(NOT[k]) != PAD_NONE) {
            snprintf(msg, sizeof msg, "\"%s\" reads as %d", NOT[k],
                     pad_input_from_name(NOT[k]));
            broke(msg);
        }
    }
    for (int i = -2; i < 100; i++) {
        if (!is_input(i) && pad_input_bindable(i)) {
            snprintf(msg, sizeof msg, "no input %d is bindable", i);
            broke(msg);
        }
    }
}

static void check_labels(void)
{
    char msg[160];
    for (int s = 0; s < PAD_STYLES; s++) {
        const char *key = pad_style_key((PadStyle)s);
        if (pad_style_from_key(key, PAD_STYLES) != (PadStyle)s) {
            snprintf(msg, sizeof msg, "style key %s reads back wrong", key);
            broke(msg);
        }
        char seen[PAD_INPUTS][16];
        for (int i = 0; i < PAD_INPUTS; i++) {
            /* (build 160) Its own 16 bytes, then kept: GCC 16 reads the
             * row of `seen` as the whole array's length. */
            char label[16];
            pad_input_label(i, (PadStyle)s, label, sizeof label);
            memcpy(seen[i], label, sizeof label);
            if (!is_input(i)) {
                if (strcmp(label, "-") != 0) {
                    snprintf(msg, sizeof msg, "%s: no input %d labelled %s",
                             key, i, label);
                    broke(msg);
                }
                continue;
            }
            size_t len = strlen(label);
            bool ok = len >= 1 && len <= 10 && label[0] != ' '
                && label[len - 1] != ' ';
            for (size_t k = 0; k < len; k++) {
                char ch = label[k];
                ok = ok && ((ch >= 'A' && ch <= 'Z')
                            || (ch >= '0' && ch <= '9') || ch == ' ');
            }
            if (!ok) {
                snprintf(msg, sizeof msg, "%s: input %d labelled \"%s\"", key,
                         i, label);
                broke(msg);
            }
            for (int j = 0; j < i; j++) {
                if (is_input(j) && strcmp(seen[j], label) == 0) {
                    snprintf(msg, sizeof msg, "%s: %d and %d both %s", key,
                             j, i, label);
                    broke(msg);
                }
            }
        }
    }
    for (size_t k = 0; k < sizeof SDL_FACES / sizeof SDL_FACES[0]; k++) {
        for (int b = 0; b < 4; b++) {
            char label[16];
            pad_input_label(b, SDL_FACES[k].style, label, sizeof label);
            if (strcmp(label, SDL_FACES[k].face[b]) != 0) {
                snprintf(msg, sizeof msg, "%s: button %d is %s, SDL says %s",
                         pad_style_key(SDL_FACES[k].style), b, label,
                         SDL_FACES[k].face[b]);
                broke(msg);
            }
        }
    }
    static const struct {
        PadStyle style;
        int      input;
        const char *label;
    } SOME[] = {
        { PAD_STYLE_NONE, PAD_SOUTH, "SOUTH" },
        { PAD_STYLE_NONE, PAD_DPAD_RIGHT, "DPAD RIGHT" },
        { PAD_STYLE_NONE, PAD_LS_UP, "LS UP" },
        { PAD_STYLE_NONE, PAD_JOY1 + 31, "JOY 32" },
        { PAD_STYLE_PS3, PAD_BACK, "SELECT" },
        { PAD_STYLE_PS3, PAD_START, "START" },
        { PAD_STYLE_PS3, PAD_L2, "L2" },
        { PAD_STYLE_PS3, 7, "L3" },
        { PAD_STYLE_PS4, PAD_START, "OPTIONS" },
        { PAD_STYLE_XBOXONE, PAD_L2, "LT" },
        { PAD_STYLE_SWITCH, PAD_R2, "ZR" },
        { PAD_STYLE_JOYSTICK, PAD_DPAD_UP, "HAT UP" },
        { PAD_STYLE_JOYSTICK, PAD_JOY1, "JOY 1" },
        { PAD_STYLE_JOYSTICK, PAD_SOUTH, "SOUTH" },
    };
    for (size_t k = 0; k < sizeof SOME / sizeof SOME[0]; k++) {
        char label[16];
        pad_input_label(SOME[k].input, SOME[k].style, label, sizeof label);
        if (strcmp(label, SOME[k].label) != 0) {
            snprintf(msg, sizeof msg, "%s: input %d is %s, not %s",
                     pad_style_key(SOME[k].style), SOME[k].input, label,
                     SOME[k].label);
            broke(msg);
        }
    }
}

/* (build 160) What the game's font draws (win_charset): A-Z, a-z, 0-9,
 * , . ; " ? ! ' - : and the space (the fill). */
static bool font_has(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
        || (ch >= '0' && ch <= '9') || ch == ' '
        || (ch != '\0' && strchr(",.;\"?!'-:", ch) != NULL);
}

static void check_key_names(void)
{
    char msg[160];
    for (int code = 0; code < 512; code++) {
        char name[16];
        key_name(code, name, sizeof name);
        size_t len = strlen(name);
        bool ok = len >= 1 && len <= 8 && strcmp(name, "-") != 0;
        for (size_t k = 0; k < len; k++) {
            ok = ok && font_has(name[k]);
        }
        if (!ok) {
            snprintf(msg, sizeof msg, "key %d is named \"%s\"", code, name);
            broke(msg);
        }
        if (code > 0 && key_from_name(name) != code) {
            snprintf(msg, sizeof msg, "key %s reads as %d, not %d", name,
                     key_from_name(name), code);
            broke(msg);
        }
        if (key_bindable(code)
            != (code > 0 && code != KEY_ESCAPE && code != KEY_BACKSPACE)) {
            snprintf(msg, sizeof msg, "key %s bindable: %d", name,
                     key_bindable(code));
            broke(msg);
        }
    }
    char none[16];
    key_name(KEY_NONE, none, sizeof none);
    if (strcmp(none, "-") != 0 || key_from_name("-") != KEY_NONE) {
        broke("no key isn't \"-\"");
    }
}

static void check_defaults(void)
{
    static const int KEYS[CONTROL_ACTIONS][CONTROL_KEYS] = {
        { 82, 26 }, { 81, 22 }, { 80, 4 }, { 79, 7 }, { 29, 15 },
        { 27, 51 }, { 6, 52 }, { 40, KEY_NONE },
    };
    static const int PAD[CONTROL_ACTIONS][CONTROL_PAD_SLOTS] = {
        { 11, 32 }, { 12, 33 }, { 13, 34 }, { 14, 35 }, { 2, PAD_NONE },
        { 0, PAD_NONE }, { 1, PAD_NONE }, { 6, PAD_NONE },
    };
    Controls c, d;
    memset(&c, 0x55, sizeof c);
    controls_default(&c);
    if (memcmp(c.key, KEYS, sizeof KEYS) != 0) {
        broke("the default keys aren't ARROWS and WASD, Enter Start");
    }
    if (memcmp(c.pad, PAD, sizeof PAD) != 0) {
        broke("the default pad isn't d-pad + stick, W/S/E, Start");
    }
    memset(&d, 0x55, sizeof d);
    controls_keys_default(&d);
    controls_pad_default(&d);
    if (memcmp(&c, &d, sizeof c) != 0) {
        broke("controls_default isn't keys + pad");
    }
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        for (int s = 0; s < CONTROL_PAD_SLOTS; s++) {
            int in = c.pad[a][s];
            if (in != PAD_NONE && !pad_input_bindable(in)) {
                broke("a default pad input isn't bindable");
            }
        }
    }
}

/* The model: a flat list of slots, the one thing each binding does. */
typedef struct {
    int key[CONTROL_ACTIONS * CONTROL_KEYS];
    int pad[CONTROL_ACTIONS * CONTROL_PAD_SLOTS];
} Model;

static void model_from(Model *m, const Controls *c)
{
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        for (int s = 0; s < CONTROL_KEYS; s++) {
            m->key[a * CONTROL_KEYS + s] = c->key[a][s];
        }
        for (int s = 0; s < CONTROL_PAD_SLOTS; s++) {
            m->pad[a * CONTROL_PAD_SLOTS + s] = c->pad[a][s];
        }
    }
}

static void model_bind(int *slots, int count, int at, int value, int none)
{
    if (value != none) {
        for (int i = 0; i < count; i++) {
            if (slots[i] == value) {
                slots[i] = none;
            }
        }
    }
    slots[at] = value;
}

/* No input an eighth of the time, a button three eighths, a stick or
 * trigger two, a mapless joystick's button two. */
static int random_input(void)
{
    u32 pick = rnd(8);
    if (pick == 0) {
        return PAD_NONE;
    }
    if (pick <= 3) {
        return (int)rnd(26);
    }
    if (pick <= 5) {
        return 32 + (int)rnd(10);
    }
    return 64 + (int)rnd(32);
}

static int random_key(void)
{
    return (rnd(6) == 0) ? KEY_NONE : 4 + (int)rnd(100);
}

static void check_binding(int seeds)
{
    char msg[160];
    for (int seed = 1; seed <= seeds; seed++) {
        g_rng = (u32)seed * 2654435761u;
        Controls c;
        controls_default(&c);
        Model m;
        model_from(&m, &c);
        for (int step = 0; step < 200; step++) {
            int a = (int)rnd(CONTROL_ACTIONS);
            if (rnd(2)) {
                int s = (int)rnd(CONTROL_PAD_SLOTS), in = random_input();
                controls_bind_pad(&c, a, s, in);
                model_bind(m.pad, CONTROL_ACTIONS * CONTROL_PAD_SLOTS,
                           a * CONTROL_PAD_SLOTS + s, in, PAD_NONE);
            } else {
                int s = (int)rnd(CONTROL_KEYS), k = random_key();
                controls_bind_key(&c, a, s, k);
                model_bind(m.key, CONTROL_ACTIONS * CONTROL_KEYS,
                           a * CONTROL_KEYS + s, k, KEY_NONE);
            }
            Model now;
            model_from(&now, &c);
            if (memcmp(&now, &m, sizeof m) != 0) {
                snprintf(msg, sizeof msg, "seed %d step %d: the slots "
                         "differ from the model's", seed, step);
                broke(msg);
                break;
            }
            /* Out-of-range binds change nothing. */
            Controls before = c;
            controls_bind_pad(&c, CONTROL_ACTIONS, 0, PAD_SOUTH);
            controls_bind_pad(&c, -1, 0, PAD_SOUTH);
            controls_bind_pad(&c, 0, CONTROL_PAD_SLOTS, PAD_SOUTH);
            controls_bind_key(&c, 0, CONTROL_KEYS, 4);
            if (memcmp(&before, &c, sizeof c) != 0) {
                snprintf(msg, sizeof msg, "seed %d: a bind out of range "
                         "changed the controls", seed);
                broke(msg);
            }

            /* The actions: for each input held, the slots that hold it. */
            PadHeld h;
            memset(&h, 0, sizeof h);
            int held = (int)rnd(6);
            for (int k = 0; k < held; k++) {
                int in = random_input();
                if (in != PAD_NONE) {
                    h.held[in] = true;
                }
            }
            u32 want = 0;
            for (int in = 0; in < PAD_INPUTS; in++) {
                if (!h.held[in]) {
                    continue;
                }
                for (int i = 0; i < CONTROL_ACTIONS * CONTROL_PAD_SLOTS;
                     i++) {
                    if (m.pad[i] == in) {
                        want |= 1u << (i / CONTROL_PAD_SLOTS);
                    }
                }
            }
            if (controls_pad_actions(&c, &h) != want) {
                snprintf(msg, sizeof msg, "seed %d step %d: pad actions "
                         "%03X, the model's %03X", seed, step,
                         controls_pad_actions(&c, &h), want);
                broke(msg);
            }
            int key = random_key();
            u32 kwant = 0;
            for (int i = 0; i < CONTROL_ACTIONS * CONTROL_KEYS; i++) {
                if (key != KEY_NONE && m.key[i] == key) {
                    kwant |= 1u << (i / CONTROL_KEYS);
                }
            }
            if (controls_key_actions(&c, key) != kwant) {
                snprintf(msg, sizeof msg, "seed %d step %d: key %d actions "
                         "%03X, the model's %03X", seed, step, key,
                         controls_key_actions(&c, key), kwant);
                broke(msg);
            }
        }
    }
}

static void check_axes(void)
{
    char msg[160];
    static const s16 VALUES[] = {
        -32768, -16385, -16384, -16383, -1, 0, 1, 16383, 16384, 16385,
        32767,
    };
    for (int axis = 0; axis < PAD_AXES; axis++) {
        for (size_t k = 0; k < sizeof VALUES / sizeof VALUES[0]; k++) {
            s16 v = VALUES[k];
            s16 axes[PAD_AXES] = { 0 };
            axes[axis] = v;
            PadHeld h;
            memset(&h, 0, sizeof h);
            pad_held_axes(&h, axes);
            PadHeld want;
            memset(&want, 0, sizeof want);
            bool trigger = axis >= PAD_AXIS_LEFT_TRIGGER;
            if (trigger) {
                if (v >= 16384) {
                    want.held[axis == PAD_AXIS_LEFT_TRIGGER ? PAD_L2
                                                            : PAD_R2] = true;
                }
            } else {
                int base = (axis < PAD_AXIS_RIGHTX) ? PAD_LS_UP : PAD_RS_UP;
                bool y = (axis & 1) != 0;
                if (v <= -16384) {
                    want.held[base + (y ? 0 : 2)] = true;
                }
                if (v >= 16384) {
                    want.held[base + (y ? 1 : 3)] = true;
                }
            }
            if (memcmp(&h, &want, sizeof h) != 0) {
                snprintf(msg, sizeof msg, "axis %d at %d: the held inputs "
                         "are wrong", axis, v);
                broke(msg);
            }
            if (axis <= PAD_AXIS_LEFTY) {
                PadHeld j;
                memset(&j, 0, sizeof j);
                pad_held_joy_axes(&j, axis == 0 ? v : 0, axis == 1 ? v : 0);
                if (memcmp(&j, &want, sizeof j) != 0) {
                    snprintf(msg, sizeof msg, "joystick axis %d at %d: the "
                             "held inputs are wrong", axis, v);
                    broke(msg);
                }
            }
        }
    }
    for (int hat = 0; hat < 16; hat++) {
        PadHeld h;
        memset(&h, 0, sizeof h);
        pad_held_hat(&h, (u8)hat);
        bool ok = h.held[PAD_DPAD_UP] == ((hat & 1) != 0)
            && h.held[PAD_DPAD_RIGHT] == ((hat & 2) != 0)
            && h.held[PAD_DPAD_DOWN] == ((hat & 4) != 0)
            && h.held[PAD_DPAD_LEFT] == ((hat & 8) != 0);
        int count = 0;
        for (int i = 0; i < PAD_INPUTS; i++) {
            count += h.held[i];
        }
        if (!ok || count != __builtin_popcount((unsigned)hat)) {
            snprintf(msg, sizeof msg, "hat %X: the d-pad is wrong", hat);
            broke(msg);
        }
    }
    for (int in = 0; in < PAD_INPUTS; in++) {
        PadHeld h;
        memset(&h, 0, sizeof h);
        h.held[in] = true;
        u32 want = 0;
        if (in == PAD_DPAD_UP || in == PAD_LS_UP) {
            want = 1u << CONTROL_UP;
        } else if (in == PAD_DPAD_DOWN || in == PAD_LS_DOWN) {
            want = 1u << CONTROL_DOWN;
        } else if (in == PAD_DPAD_LEFT || in == PAD_LS_LEFT) {
            want = 1u << CONTROL_LEFT;
        } else if (in == PAD_DPAD_RIGHT || in == PAD_LS_RIGHT) {
            want = 1u << CONTROL_RIGHT;
        }
        if (pad_nav_actions(&h) != want) {
            snprintf(msg, sizeof msg, "input %d moves the menu %X", in,
                     pad_nav_actions(&h));
            broke(msg);
        }
    }
}

int main(int argc, char **argv)
{
    int seeds = (argc > 1) ? atoi(argv[1]) : 200;
    check_names();
    check_labels();
    check_key_names();
    check_defaults();
    check_binding(seeds);
    check_axes();
    if (g_broken != 0) {
        printf("controls: FAIL (%d broken)\n", g_broken);
        return 1;
    }
    printf("controls: PASS (names, labels of %d styles, key names, "
           "defaults, %d binding runs, axes)\n", PAD_STYLES, seeds);
    return 0;
}
