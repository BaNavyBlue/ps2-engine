/* Headless platform: no window, no GPU, no SDL. Runs a scripted input
 * sequence for a fixed number of ticks and writes the final frame as a PPM.
 *
 * This exists so the core and game layers can be built and exercised
 * anywhere, including CI, and so frames can be diffed against a reference
 * without a display attached.
 *
 * (build 160) PS2_GAMEPADS=N connects N gamepads (up to PLATFORM_PADS),
 * named "HEADLESS PAD 1" and on; their buttons are PS2_PAD's, the one
 * script, game buttons with no controls between. PS2_RUMBLE=FILE logs the
 * rumble: a line "F PAD LOW HIGH" (the frames pumped, the gamepad from 1,
 * the motors in hex) each time a connected gamepad's motors change.
 *
 * (build 160) PS2_RAW="N:NAME,N:PAD:NAME,..." presses keys and gamepad
 * inputs as they are, for the Escape menu's CONTROLS and BUTTONS pages: a
 * key by its name (src/core/keys.c: Q, SPACE, LSHIFT, BACKSPC ...) or a
 * gamepad input by its (PAD:NORTH, PAD:LSUP, PAD:L2, PAD:JOY3), each for
 * one frame at frame N as PS2_PAD counts them -- Input.key or
 * Input.pad_input. Each is also a tap of what the controls make it
 * (platform_set_controls's, as the SDL3 platform's): a key's actions, a
 * gamepad input's (when PS2_GAMEPADS connects one), and the Escape
 * menu's own -- Escape and a gamepad's Back its menu key, Backspace its
 * back, the arrows, Enter and the d-pad and left stick its nav -- so a
 * check can bind one and see it reach the game. PS2_PAD_STYLE=STYLE
 * (keys.h's keys: ps3, xboxone, switch, joystick ...) is whose labels the
 * connected gamepads wear; none by default.
 *
 * (build 160) PS2_FRAME_HZ=N: the frames come N times a second by the
 * virtual clock, not at the game's rate, so main.c's pacing runs free and
 * a frame is worth 0, 1 or 2 ticks (120: every other frame none; 30: two
 * each) -- for tools/controls_check.py, a press carried to the tick that
 * takes it. Nothing else here follows it (PS2_WAV's frames are a tick's). */

#include "platform.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAW_STEPS 512   /* (build 160) PS2_RAW's presses at most */

struct Platform {
    Audio *audio;
    FILE  *wav;
    u32    wav_frames;
    double now;
    double frame_s;       /* (build 160) PS2_FRAME_HZ's: the clock a frame */
    int    frame;
    int    frames_to_run;
    char   out_path[256];
    const  Framebuffer *last_fb;
    Palette last_pal;
    FbDraw draw;          /* (build 140) PS2_DRAW: the plane the PPM is from */
    int    pads;          /* (build 160) PS2_GAMEPADS */
    char   pad_name[PLATFORM_PADS][32];
    u16    rumble[PLATFORM_PADS][2];    /* each pad's motors as logged */
    FILE  *rumble_log;    /* PS2_RUMBLE's */
    /* (build 160) PS2_RAW's presses, PS2_PAD_STYLE's, and the controls
     * the raw presses go through. */
    int      raw_count;
    int      raw_frame[RAW_STEPS];
    int      raw_key[RAW_STEPS];        /* a scancode, or KEY_NONE */
    int      raw_pad[RAW_STEPS];        /* a gamepad input, or PAD_NONE */
    PadStyle pad_style;
    Controls controls;
};

static int env_int(const char *name, int fallback)
{
    const char *v = getenv(name);
    return (v != NULL) ? atoi(v) : fallback;
}

/* (build 160) PS2_RAW. False, with a message, if it cannot be read. */
static bool parse_raw(Platform *p)
{
    const char *text = getenv("PS2_RAW");
    if (text == NULL) {
        return true;
    }
    const char *s = text;
    while (*s != '\0') {
        char *end   = NULL;
        long  frame = strtol(s, &end, 10);
        if (end == s || *end != ':' || frame < 0
            || p->raw_count >= RAW_STEPS) {
            fprintf(stderr, "headless: cannot read PS2_RAW=%s\n", text);
            return false;
        }
        const char *b = end + 1;
        size_t      n = strcspn(b, ",");
        char        name[32];
        snprintf(name, sizeof name, "%.*s", (int)n, b);
        int at = p->raw_count;
        p->raw_frame[at] = (int)frame;
        p->raw_key[at]   = KEY_NONE;
        p->raw_pad[at]   = PAD_NONE;
        if (strncmp(name, "PAD:", 4) == 0) {
            p->raw_pad[at] = pad_input_from_name(name + 4);
        } else {
            p->raw_key[at] = key_from_name(name);
        }
        if (p->raw_key[at] == KEY_NONE && p->raw_pad[at] == PAD_NONE) {
            fprintf(stderr, "headless: PS2_RAW: no key or gamepad input "
                            "%s\n", name);
            return false;
        }
        p->raw_count++;
        s = b + n;
        if (*s == ',') {
            s++;
        }
    }
    return true;
}

Platform *platform_create(const char *title)
{
    (void)title;

    Platform *p = calloc(1, sizeof *p);
    if (p == NULL) {
        return NULL;
    }

    p->frames_to_run = env_int("PS2_FRAMES", 120);
    /* (build 160) PS2_FRAME_HZ: the rate the frames come at, by the
     * virtual clock; the game's own by default (a tick a frame). */
    p->frame_s = 1.0 / TICK_HZ;
    const char *frame_hz = getenv("PS2_FRAME_HZ");
    if (frame_hz != NULL) {
        double hz = atof(frame_hz);
        if (hz >= 1.0 && hz <= 1000.0) {
            p->frame_s = 1.0 / hz;
        } else {
            fprintf(stderr, "headless: PS2_FRAME_HZ=%s is not 1 to 1000\n",
                    frame_hz);
        }
    }
    p->draw = fb_draw_from_env();
    if (p->draw == FB_DRAW_PALETTE) {
        fprintf(stderr, "headless: the frame through the palette, the "
                        "reference (PS2_DRAW=palette)\n");
    }
    const char *out = getenv("PS2_OUT");
    snprintf(p->out_path, sizeof p->out_path, "%s",
             (out != NULL) ? out : "frame.ppm");
    p->pads = env_int("PS2_GAMEPADS", 0);
    if (p->pads < 0) {
        p->pads = 0;
    } else if (p->pads > PLATFORM_PADS) {
        p->pads = PLATFORM_PADS;
    }
    for (int i = 0; i < PLATFORM_PADS; i++) {
        snprintf(p->pad_name[i], sizeof p->pad_name[i], "HEADLESS PAD %d",
                 i + 1);
    }
    const char *rumble = getenv("PS2_RUMBLE");
    if (rumble != NULL) {
        p->rumble_log = fopen(rumble, "wb");
        if (p->rumble_log == NULL) {
            fprintf(stderr, "headless: cannot write %s\n", rumble);
        }
    }
    /* (build 160) The raw presses, the gamepads' labels, the controls. */
    controls_default(&p->controls);
    bool        ok    = parse_raw(p);
    const char *style = getenv("PS2_PAD_STYLE");
    p->pad_style = PAD_STYLE_NONE;
    if (style != NULL) {
        p->pad_style = pad_style_from_key(style, PAD_STYLES);
        if (p->pad_style == PAD_STYLES) {
            fprintf(stderr, "headless: PS2_PAD_STYLE: no style %s\n", style);
            ok = false;
        }
    }
    if (!ok) {
        if (p->rumble_log != NULL) {
            fclose(p->rumble_log);
        }
        free(p);
        return NULL;
    }
    return p;
}

static void write_ppm(const Platform *p)
{
    if (p->last_fb == NULL) {
        return;
    }

    FILE *fh = fopen(p->out_path, "wb");
    if (fh == NULL) {
        return;
    }

    fprintf(fh, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        /* (build 140) The colour plane as it is, or the index through the
         * palette handed over with the frame (fb_palette_colour). */
        Rgb8 c = (p->draw == FB_DRAW_COLOUR)
               ? p->last_fb->rgb[i]
               : fb_palette_colour(p->last_fb, &p->last_pal, i);
        fputc(c.r, fh);
        fputc(c.g, fh);
        fputc(c.b, fh);
    }
    fclose(fh);
    fprintf(stderr, "headless: wrote %s after %d frames\n",
            p->out_path, p->frame);
}

/* Minimal 16-bit stereo WAV, so a run can be listened to or measured. */
static void wav_open(Platform *p, const char *path)
{
    p->wav = fopen(path, "wb");
    if (p->wav == NULL) {
        return;
    }
    const u32 block = AUDIO_CHANNELS * 2;           /* bytes a frame */
    const u32 rate  = AUDIO_RATE * block;           /* bytes a second */

    u8 header[44];
    memset(header, 0, sizeof header);
    memcpy(header, "RIFF", 4);
    memcpy(header + 8, "WAVEfmt ", 8);
    header[16] = 16;                 /* fmt chunk size */
    header[20] = 1;                  /* PCM */
    header[22] = AUDIO_CHANNELS;     /* stereo */
    header[24] = (u8)(AUDIO_RATE & 0xFF);
    header[25] = (u8)((AUDIO_RATE >> 8) & 0xFF);
    header[26] = (u8)((AUDIO_RATE >> 16) & 0xFF);
    header[28] = (u8)(rate & 0xFF);
    header[29] = (u8)((rate >> 8) & 0xFF);
    header[30] = (u8)((rate >> 16) & 0xFF);
    header[32] = (u8)block;          /* block align */
    header[34] = 16;                 /* bits */
    memcpy(header + 36, "data", 4);
    fwrite(header, 1, sizeof header, p->wav);
}

static void wav_write_frame_audio(Platform *p)
{
    if (p->wav == NULL || p->audio == NULL) {
        return;
    }

    /* One video frame's worth of stereo frames per pumped frame. */
    int frames = AUDIO_RATE / 60;
    float buf[(AUDIO_RATE / 60 + 1) * AUDIO_CHANNELS];

    audio_render(p->audio, buf, frames);
    for (int i = 0; i < frames * AUDIO_CHANNELS; i++) {
        int v = (int)(buf[i] * 32000.0f);
        u8 le[2] = { (u8)(v & 0xFF), (u8)((v >> 8) & 0xFF) };
        fwrite(le, 1, 2, p->wav);
    }
    p->wav_frames += (u32)frames;
}

static void wav_close(Platform *p)
{
    if (p->wav == NULL) {
        return;
    }
    u32 data_bytes = p->wav_frames * AUDIO_CHANNELS * 2;
    u32 riff = 36 + data_bytes;

    fseek(p->wav, 4, SEEK_SET);
    fwrite(&riff, 4, 1, p->wav);
    fseek(p->wav, 40, SEEK_SET);
    fwrite(&data_bytes, 4, 1, p->wav);
    fclose(p->wav);
    p->wav = NULL;
}

void platform_attach_audio(Platform *p, Audio *audio)
{
    p->audio = audio;

    const char *path = getenv("PS2_WAV");
    if (path != NULL) {
        wav_open(p, path);
    }
}

void platform_destroy(Platform *p)
{
    write_ppm(p);
    wav_close(p);
    if (p->rumble_log != NULL) {
        fclose(p->rumble_log);
    }
    free(p);
}

/* (build 160) PS2_RAW's presses at this frame: the key or gamepad input
 * as it is, and a tap of what the controls and the Escape menu make it. */
static void raw_presses(Platform *p, Input *in)
{
    for (int i = 0; i < p->raw_count; i++) {
        if (p->raw_frame[i] != p->frame) {
            continue;
        }
        Buttons game, nav;
        memset(&game, 0, sizeof game);
        memset(&nav, 0, sizeof nav);
        int key = p->raw_key[i];
        int pad = p->raw_pad[i];
        if (key != KEY_NONE) {
            if (in->key == KEY_NONE) {
                in->key = key;
            }
            buttons_from_actions(&game, controls_key_actions(&p->controls,
                                                             key));
            game.menu = key == KEY_ESCAPE;
            in->back  = in->back || key == KEY_BACKSPACE;
            nav.up    = key == KEY_UP;
            nav.down  = key == KEY_DOWN;
            nav.left  = key == KEY_LEFT;
            nav.right = key == KEY_RIGHT;
            nav.start = key == KEY_RETURN;
        } else {
            if (in->pad_input == PAD_NONE) {
                in->pad_input = pad;
            }
            PadHeld h;
            memset(&h, 0, sizeof h);
            h.held[pad] = true;
            if (p->pads > 0) {
                buttons_from_actions(&game,
                                     controls_pad_actions(&p->controls, &h));
                game.menu = pad == PAD_BACK;
            }
            buttons_from_actions(&nav, pad_nav_actions(&h));
        }
        const bool *g  = (const bool *)&game;
        const bool *n  = (const bool *)&nav;
        bool       *gh = (bool *)&in->held;
        bool       *gp = (bool *)&in->pressed;
        bool       *nh = (bool *)&in->nav_held;
        bool       *np = (bool *)&in->nav_pressed;
        for (size_t k = 0; k < sizeof game / sizeof(bool); k++) {
            gh[k] = gh[k] || g[k];
            gp[k] = gp[k] || g[k];
            nh[k] = nh[k] || n[k];
            np[k] = np[k] || n[k];
        }
    }
}

bool platform_pump(Platform *p, Input *in)
{
    input_clear(in);

    /* Scripted input against the real screen sequence: SEGA runs its 300
     * frames, Start leaves the title, then the cursor walks down twice. */
    /* Default: press nothing, so the attract sequence runs in full.
     * Set PS2_START to a frame number to press Start there instead. */
    const char *start_at = getenv("PS2_START");
    if (start_at != NULL && p->frame == atoi(start_at)) {
        in->held.start    = true;
        in->pressed.start = true;
    }

    /* PS2_TOUCH holds Up for one frame, which is enough to wake the
     * title's push-start prompt without leaving the screen. */
    const char *touch_at = getenv("PS2_TOUCH");
    if (touch_at != NULL && p->frame == atoi(touch_at)) {
        in->held.up = true;
    }

    /* PS2_PAD is a list of frame:button pairs, e.g. "900:down,920:c",
     * each a one-frame press. Buttons: up down left right a b c start. */
    const char *pad = getenv("PS2_PAD");
    while (pad != NULL && *pad != '\0') {
        int  frame = atoi(pad);
        const char *colon = strchr(pad, ':');
        if (colon == NULL) {
            break;
        }
        const char *name = colon + 1;
        if (frame == p->frame) {
            static const struct {
                const char *name;
                size_t      offset;
            } BUTTONS[] = {
                { "up",    offsetof(Buttons, up) },
                { "down",  offsetof(Buttons, down) },
                { "left",  offsetof(Buttons, left) },
                { "right", offsetof(Buttons, right) },
                { "a",     offsetof(Buttons, a) },
                { "b",     offsetof(Buttons, b) },
                { "c",     offsetof(Buttons, c) },
                { "start", offsetof(Buttons, start) },
                { "esc",   offsetof(Buttons, menu) },
            };
            for (size_t i = 0; i < sizeof BUTTONS / sizeof BUTTONS[0]; i++) {
                size_t len = strlen(BUTTONS[i].name);
                if (strncmp(name, BUTTONS[i].name, len) == 0
                    && (name[len] == ',' || name[len] == '\0')) {
                    *((bool *)&in->held    + BUTTONS[i].offset) = true;
                    *((bool *)&in->pressed + BUTTONS[i].offset) = true;
                }
            }
        }
        pad = strchr(name, ',');
        if (pad != NULL) {
            pad++;
        }
    }

    raw_presses(p, in);

    wav_write_frame_audio(p);

    p->frame++;
    return p->frame <= p->frames_to_run;
}

/* (build 160) Kept for PS2_RAW's presses; PS2_PAD's are game buttons. */
void platform_set_controls(Platform *p, const Controls *c)
{
    p->controls = *c;
}

/* (build 160) The rumble: a change logged to PS2_RUMBLE, for a connected
 * gamepad. */
void platform_rumble(Platform *p, int pad, u16 low, u16 high)
{
    if (pad < 0 || pad >= p->pads) {
        return;
    }
    if (p->rumble[pad][0] == low && p->rumble[pad][1] == high) {
        return;
    }
    p->rumble[pad][0] = low;
    p->rumble[pad][1] = high;
    if (p->rumble_log != NULL) {
        fprintf(p->rumble_log, "%d %d %04X %04X\n", p->frame, pad + 1, low,
                high);
    }
}

const char *platform_pad_name(Platform *p, int pad)
{
    if (pad < 0 || pad >= p->pads) {
        return NULL;
    }
    return p->pad_name[pad];
}

/* (build 160) PS2_PAD_STYLE's, for every connected gamepad. */
PadStyle platform_pad_style(Platform *p, int pad)
{
    return (pad >= 0 && pad < p->pads) ? p->pad_style : PAD_STYLE_NONE;
}

void platform_set_aspect(Platform *p, AspectMode mode)
{
    /* The PPM is the raw 320x224 frame, one pixel a pixel. */
    (void)p;
    (void)mode;
}

void platform_set_fullscreen(Platform *p, bool on)
{
    /* No window: the PPM is the frame, whatever the setting. */
    (void)p;
    (void)on;
}

void platform_present(Platform *p, const Framebuffer *fb, const Palette *pal)
{
    p->last_fb  = fb;
    p->last_pal = *pal;
}

double platform_now(Platform *p)
{
    /* Virtual clock: one tick per pump, so runs are deterministic.
     * (build 160) PS2_FRAME_HZ's frames, still deterministic. */
    p->now += p->frame_s;
    return p->now;
}

void platform_sleep(double seconds)
{
    (void)seconds;
}

/* (build 105) PS2_DISPLAY describes the screen (crt_display_parse), so a
 * headless run can show every value the CRT page can; without it, the
 * default window on a 1080p desktop. Read each call: cheap, and a test
 * can set it before the menu opens. */
void platform_display_info(Platform *p, CrtDisplay *out)
{
    (void)p;
    static bool warned;
    const char *text = getenv("PS2_DISPLAY");
    if (text == NULL || !crt_display_parse(text, out)) {
        if (text != NULL && !warned) {
            fprintf(stderr, "PS2_DISPLAY: cannot read \"%s\"\n", text);
            warned = true;
        }
        crt_display_default(out);
    }
}

void platform_set_crt(Platform *p, const CrtSettings *s)
{
    (void)p;
    (void)s;
}
