#ifndef PS2_PLATFORM_H
#define PS2_PLATFORM_H

#include "../core/audio.h"
#include "../core/crt.h"
#include "../core/keys.h"
#include "../core/palette.h"
#include "../core/video.h"

#include <string.h>

/* Everything OS- and GPU-specific lives behind this interface. The core and
 * game layers include no platform headers at all, which is what lets the
 * whole thing build and run headless for testing. */

typedef struct {
    bool up, down, left, right;
    bool a, b, c;
    bool start;
    bool menu;          /* the Escape key: not a pad button, the port's own */
} Buttons;

/* Which keys and gamepad inputs are the pad: (build 160) the Escape
 * menu's CONTROLS and BUTTONS pages, a Controls (src/core/keys.h) handed
 * to platform_set_controls -- two keys and two gamepad inputs an action,
 * any of them re-assigned. (Until build 160 a ControlScheme picked one of
 * two key sets, ARROWS or WASD; both are the defaults now.) Escape, and a
 * gamepad's Back (Select, Share, View), are the Escape menu's, never
 * bound; Backspace too (it empties a slot being set). */

/* How wide a pixel is drawn, as the pixel aspect ratio (width / height).
 *
 * The VDP in H40 mode (reg $8C81) clocks pixels at the master clock / 8,
 * about 6.71 MHz. A pixel square on an NTSC set would need about 6.14 MHz
 * at that width, so on a TV each of the 320 is narrower than it is tall:
 * 32:35. Drawn square, the picture is 10:7 and about 9% too wide.
 *
 *   ASPECT_SQUARE  1:1     one screen pixel per pixel, 10:7 overall
 *   ASPECT_4_3     14:15   the 320x224 frame stretched to 4:3, as most
 *                          emulators show it -- a touch wide, since a
 *                          TV's 4:3 also held the border round the 224
 *   ASPECT_TV      32:35   what the NTSC set drew */
typedef enum {
    ASPECT_SQUARE = 0,
    ASPECT_4_3    = 1,
    ASPECT_TV     = 2,
    ASPECT_COUNT
} AspectMode;

/* `held` is the current key state; `pressed` is every button that went
 * down since the last tick, latched from events.
 *
 * Polling `held` alone loses any press shorter than a frame, and when the
 * loop catches up several ticks at once every one of them sees the same
 * snapshot. The original read the joypad once per VBlank into
 * joypad_pressed, which is a latch -- so this matches it and stops
 * dropping taps.
 *
 * (build 160) Both through the controls: the keys and every gamepad's
 * inputs bound to an action are its button. The rest is the Escape
 * menu's, whatever is bound, and the game never reads it: `nav_held` and
 * `nav_pressed`, the arrows and Enter (as Start) and every gamepad's
 * d-pad and left stick (pad_nav_actions); `back`, Backspace pressed;
 * `key`, the first key pressed since the last pump (a scancode, or
 * KEY_NONE); `pad_input`, the first gamepad input (PAD_*, or PAD_NONE) --
 * a stick or trigger as it passes half its travel -- for the CONTROLS and
 * BUTTONS pages to bind. input_clear makes an Input of nothing. */
typedef struct {
    Buttons held;
    Buttons pressed;
    Buttons nav_held;
    Buttons nav_pressed;
    bool    back;
    int     key;
    int     pad_input;
} Input;

static inline void input_clear(Input *in)
{
    memset(in, 0, sizeof *in);
    in->key       = KEY_NONE;
    in->pad_input = PAD_NONE;
}

/* (build 160) The Buttons a set of action bits is (ControlAction's, keys.h)
 * OR'd into `b`. */
static inline void buttons_from_actions(Buttons *b, u32 actions)
{
    b->up    = b->up    || (actions & (1u << CONTROL_UP)) != 0;
    b->down  = b->down  || (actions & (1u << CONTROL_DOWN)) != 0;
    b->left  = b->left  || (actions & (1u << CONTROL_LEFT)) != 0;
    b->right = b->right || (actions & (1u << CONTROL_RIGHT)) != 0;
    b->a     = b->a     || (actions & (1u << CONTROL_A)) != 0;
    b->b     = b->b     || (actions & (1u << CONTROL_B)) != 0;
    b->c     = b->c     || (actions & (1u << CONTROL_C)) != 0;
    b->start = b->start || (actions & (1u << CONTROL_START)) != 0;
}

typedef struct Platform Platform;

Platform *platform_create(const char *title);
void      platform_destroy(Platform *p);

/* Services the window and fills in the current input state. Returns false
 * when the user has asked to quit. */
bool platform_pump(Platform *p, Input *in);

/* Hands off an indexed frame plus its palette. Converting indices to light
 * is the platform's job, because that is where the CRT chain and the HDR
 * output format live. */
void platform_present(Platform *p, const Framebuffer *fb, const Palette *pal);

/* Hands the mixer to the platform's audio device. Must be called once,
 * after platform_create; the platform pulls from it on its own thread. */
void platform_attach_audio(Platform *p, Audio *audio);

/* Picks which keys and gamepad inputs drive the pad: (build 160) a copy
 * is kept. The headless platform's scripted pads are game buttons and
 * need none; its raw presses (PS2_RAW) go through them. */
void platform_set_controls(Platform *p, const Controls *c);

/* (build 160) Up to two gamepads, in the order they were connected (one
 * unplugged, the next moves up): any SDL3 has a mapping for, or the
 * buttons, first hat and first two axes of a joystick it has none for. */
#define PLATFORM_PADS 2

/* (build 160) The rumble (src/game/rumble.h): gamepad `pad`'s motors from
 * now on, 0 to $FFFF each -- `low` the big, slow one, `high` the small,
 * quick one (SDL3's low and high frequency rumble); 0 and 0 stop them.
 * main.c sets every gamepad's every frame. The SDL3 platform sends a
 * change at once (to a 32nd of the range) and a level held again every 6
 * frames, each good for 250 ms, so the motors stop by themselves when the
 * frames do, and stops them when a gamepad goes and at the end; a gamepad
 * without motors ignores it. The headless platform logs each change to
 * PS2_RUMBLE. */
void platform_rumble(Platform *p, int pad, u16 low, u16 high);

/* (build 160) The name gamepad `pad` gives itself, as the system has it
 * ("PS3 Controller"); NULL when there is none there. The headless
 * platform's PS2_GAMEPADS=N connects N, "HEADLESS PAD 1" and on. */
const char *platform_pad_name(Platform *p, int pad);

/* (build 160) Whose labels gamepad `pad` wears (keys.h, PadStyle): SDL3's
 * type for it, JOYSTICK for one it has no mapping for, NONE when there is
 * none there. The headless platform's are PS2_PAD_STYLE's (a style's key,
 * "ps3"), NONE without it. */
PadStyle platform_pad_style(Platform *p, int pad);

/* Picks the pixel aspect the frame is drawn at. The headless platform
 * ignores it: its PPM is the raw 320x224 frame. */
void platform_set_aspect(Platform *p, AspectMode mode);

/* (build 100) Borderless fullscreen on the display the window is on, or
 * back to the window. No video mode is changed: the window takes the
 * display's own size, and the frame is placed in it exactly as in any
 * window -- the largest whole multiple of 224 lines that fits, the pixel
 * aspect across, bars round it. The headless platform ignores it. */
void platform_set_fullscreen(Platform *p, bool on);

/* (build 105) What the CRT page needs to know about where the frame goes
 * (crt.h, CrtDisplay): the output's pixels, fullscreen's, whether the
 * desktop scales the window, SDL's HDR state. The headless platform
 * answers from PS2_DISPLAY (crt_display_parse's words), else the default
 * window. */
void platform_display_info(Platform *p, CrtDisplay *out);

/* (build 105) The CRT page's settings. (build 107) The SDL3 platform's
 * SDL_GPU chain draws them (SDR; each switched off where the display cannot
 * have it); its SDL_Renderer fallback and the headless platform ignore
 * them. */
void platform_set_crt(Platform *p, const CrtSettings *s);

double platform_now(Platform *p);      /* monotonic seconds */
void   platform_sleep(double seconds);

#endif /* PS2_PLATFORM_H */
