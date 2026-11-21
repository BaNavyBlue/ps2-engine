#ifndef PS2_KEYS_H
#define PS2_KEYS_H

#include "ps2_types.h"

#include <stddef.h>

/* (build 160) The controls: which keys and gamepad inputs are the Mega
 * Drive pad's. Not from the ROM (the cartridge read a pad); the port's
 * own, set on the Escape menu's CONTROLS page (the keys) and the GAMEPAD
 * page's BUTTONS (the gamepads), and kept in settings.json. Herzog Zwei's
 * port's src/core/keys.[ch] (its session 54), with this game's actions,
 * defaults and reserved keys; everything else is the same.
 *
 * Keys are USB HID usage codes, which are SDL3's scancode numbers, so
 * this file needs no SDL and the headless build and the harnesses link
 * it as they are. The names here are the ones the menu draws and
 * settings.json holds: upper case, eight letters at most, only characters
 * the game's font has (win_charset: so QUOTE, MINUS, EQUALS, LBRACKET and
 * RBRACKET, where Herzog Zwei's font draws ' - = [ ]).
 *
 * Gamepad inputs (PAD_*, below) are any gamepad's: its buttons by SDL3's
 * SDL_GamepadButton numbers (positional: SOUTH is the bottom face button
 * whatever its label), its sticks pushed and triggers pulled past half
 * their travel, and the buttons of a joystick SDL has no gamepad mapping
 * for (an arcade stick, an old USB pad: JOY 1-32; its first hat is the
 * d-pad, its first two axes the left stick). Every action has two of
 * them, every one may be bound but Back (the Escape menu's), and
 * settings.json holds them by the positional names, so a binding means
 * the same on every pad. The menu draws them as the pad in hand labels
 * them (pad_input_label: CROSS on a PlayStation pad, A on an Xbox pad, B
 * on a Switch pad, the one button). */

typedef enum {
    CONTROL_UP = 0,
    CONTROL_DOWN,
    CONTROL_LEFT,
    CONTROL_RIGHT,
    CONTROL_A,
    CONTROL_B,
    CONTROL_C,
    CONTROL_START,
    CONTROL_ACTIONS
} ControlAction;

#define CONTROL_KEYS 2          /* keys an action may have */
#define CONTROL_PAD_SLOTS 2     /* gamepad inputs it may have */
#define KEY_NONE     (-1)
#define PAD_NONE     (-1)

/* Keys the platform keeps for itself or the menu always reads. Escape
 * (the Escape menu) and Backspace (a slot emptied) are never bound. */
#define KEY_RETURN    40
#define KEY_ESCAPE    41
#define KEY_BACKSPACE 42
#define KEY_RIGHT     79
#define KEY_LEFT      80
#define KEY_DOWN      81
#define KEY_UP        82

/* Gamepad inputs. 0-25 are SDL3's SDL_GamepadButton numbers; the sticks
 * and triggers follow, then a mapless joystick's buttons. Numbers between
 * are no input. */
#define PAD_SOUTH       0
#define PAD_EAST        1
#define PAD_WEST        2
#define PAD_NORTH       3
#define PAD_BACK        4       /* opens the Escape menu: never bound */
#define PAD_GUIDE       5
#define PAD_START       6
#define PAD_L1          9       /* the left shoulder */
#define PAD_R1          10
#define PAD_DPAD_UP     11
#define PAD_DPAD_DOWN   12
#define PAD_DPAD_LEFT   13
#define PAD_DPAD_RIGHT  14
#define PAD_BUTTONS     26      /* SDL_GAMEPAD_BUTTON_COUNT */
#define PAD_LS_UP       32      /* the left stick, past half its travel */
#define PAD_LS_DOWN     33
#define PAD_LS_LEFT     34
#define PAD_LS_RIGHT    35
#define PAD_RS_UP       36      /* the right stick */
#define PAD_RS_DOWN     37
#define PAD_RS_LEFT     38
#define PAD_RS_RIGHT    39
#define PAD_L2          40      /* the left trigger, past half */
#define PAD_R2          41
#define PAD_JOY1        64      /* a mapless joystick's buttons 1-32 */
#define PAD_JOYS        32
#define PAD_INPUTS      96      /* every input is below this */

/* How far a stick or trigger goes before it counts as pressed: half its
 * travel (SDL3's axes run -32768 to 32767, a trigger from 0). */
#define PAD_AXIS_PRESS 16384

/* SDL3's axes, in its order, as pad_held_axes takes them. */
enum {
    PAD_AXIS_LEFTX = 0,
    PAD_AXIS_LEFTY,
    PAD_AXIS_RIGHTX,
    PAD_AXIS_RIGHTY,
    PAD_AXIS_LEFT_TRIGGER,
    PAD_AXIS_RIGHT_TRIGGER,
    PAD_AXES
};

/* SDL3's hat bits (SDL_HAT_UP ...). */
#define PAD_HAT_UP    0x01
#define PAD_HAT_RIGHT 0x02
#define PAD_HAT_DOWN  0x04
#define PAD_HAT_LEFT  0x08

/* Whose labels the menu draws: SDL3's SDL_GamepadType, folded (the Joy-Cons
 * are the Switch's), and two of the port's: NONE, no gamepad to go by (the
 * positional names, SOUTH...), and JOYSTICK, one SDL has no mapping for.
 * The keys are for the headless platform's PS2_PAD_STYLE. */
typedef enum {
    PAD_STYLE_NONE = 0,
    PAD_STYLE_STANDARD,         /* SDL's own mapping: Xbox labels */
    PAD_STYLE_XBOX360,
    PAD_STYLE_XBOXONE,
    PAD_STYLE_PS3,
    PAD_STYLE_PS4,
    PAD_STYLE_PS5,
    PAD_STYLE_SWITCH,
    PAD_STYLE_GAMECUBE,
    PAD_STYLE_JOYSTICK,
    PAD_STYLES
} PadStyle;

/* Each pad input held: a flag per PAD_* input. */
typedef struct {
    bool held[PAD_INPUTS];
} PadHeld;

typedef struct {
    int key[CONTROL_ACTIONS][CONTROL_KEYS];        /* scancodes, KEY_NONE */
    int pad[CONTROL_ACTIONS][CONTROL_PAD_SLOTS];   /* PAD_*, PAD_NONE */
} Controls;

/* Until build 160 the CONTROLS row picked one of two key sets, ARROWS
 * (arrows, Z/X/C for A/B/C) and WASD (W/A/S/D, L/;/' for A/B/C), Enter
 * Start in both; the defaults are both at once. Gamepads: the d-pad or
 * the left stick for the directions; West/South/East for A/B/C, as the
 * Mega Drive pad's run left to right; Start. controls_default is both. */
void controls_default(Controls *c);
void controls_keys_default(Controls *c);
void controls_pad_default(Controls *c);

/* The key's name (KEY_NONE: "-"; a code with no name: "KEY nnn"). */
void key_name(int code, char *out, size_t n);
/* The code for a name, upper or lower case, or a plain number; KEY_NONE
 * if neither. */
int  key_from_name(const char *name);

/* A pad input's positional name, settings.json's (PAD_NONE: "-"; SOUTH,
 * DUP, LSUP, L2, JOY1 ...), and the input for one, upper or lower case,
 * or PAD_NONE. */
void pad_input_name(int input, char *out, size_t n);
int  pad_input_from_name(const char *name);

/* A pad input as a pad of `style` labels it, for the menu: ten characters
 * at most, letters, digits and spaces (PAD_NONE: "-"). */
void pad_input_label(int input, PadStyle style, char *out, size_t n);

/* A style's key ("none", "standard", "xbox360", "xboxone", "ps3", "ps4",
 * "ps5", "switch", "gamecube", "joystick"), and the style for one, or
 * `fallback`. */
const char *pad_style_key(PadStyle style);
PadStyle    pad_style_from_key(const char *key, PadStyle fallback);

/* Whether a key or pad input may be bound: not Escape or Backspace (the
 * platform's) or the gamepad's Back; a pad input one of the PAD_* above. */
bool key_bindable(int code);
bool pad_input_bindable(int input);

/* Binds `code` to `action`'s slot, taking it off any other slot first so a
 * key, or a pad input, does one thing. */
void controls_bind_key(Controls *c, int action, int slot, int code);
void controls_bind_pad(Controls *c, int action, int slot, int input);

/* The actions a key is, and the actions the pad inputs held are: a bit
 * per ControlAction (1 << CONTROL_A ...). */
u32 controls_key_actions(const Controls *c, int code);
u32 controls_pad_actions(const Controls *c, const PadHeld *h);

/* The Escape menu's fixed directions on a gamepad, whatever is bound: the
 * d-pad and the left stick (a mapless joystick's hat and first axes);
 * bits as above, CONTROL_UP to CONTROL_RIGHT. */
u32 pad_nav_actions(const PadHeld *h);

/* Into `h`: the sticks and triggers from SDL3's six axes (PAD_AXIS_*), a
 * mapless joystick's hat as the d-pad, its first two axes (x, y) as the
 * left stick. Each sets what is pressed and leaves the rest. */
void pad_held_axes(PadHeld *h, const s16 axis[PAD_AXES]);
void pad_held_hat(PadHeld *h, u8 hat);
void pad_held_joy_axes(PadHeld *h, s16 x, s16 y);

#endif /* PS2_KEYS_H */
