/* SDL3 platform.
 *
 * (build 107) Two ways to the screen. SDL_GPU first: the frame's indices
 * and its palette go up as textures and crt_gpu.c draws them -- the CRT
 * chain (docs/crt.md, phase 2, SDR) when the CRT page asks for scanlines
 * or a mask this display can have, else the flat picture laid out as
 * before. The SDR placeholder -- indices to 8-bit on the CPU, blitted
 * through SDL_Renderer -- is the fallback: when there is no SDL_GPU device,
 * when the compiled shaders are not in shaders/ (`make shaders`), or with
 * PS2_GPU=0. The line `display: ...` at startup says which. (build 108)
 * Phase 3: with HDR ON where SDL says the window has HDR, the swapchain is
 * HDR10 (Rec.2020, PQ) or scRGB (HDR_EXTENDED_LINEAR), whichever the window
 * offers (HDR10 first; PS2_HDR_SWAPCHAIN=scrgb or =pq picks), the chain in
 * nits by PEAK and WHITE; SDR as before otherwise, and it follows the
 * window's HDR state as it changes. Nothing above this file changes: the
 * interface hands over indices, not colours.
 *
 * (build 160) Gamepads, after Herzog Zwei's port (its platform_sdl3.c): up
 * to two, opened as they come and in that order, each driving the pad
 * alongside the keys, presses latched as the keys' are; and their motors,
 * the rumble (platform_rumble), by SDL_RumbleGamepad -- any pad SDL can
 * shake, a DualShock 3 among them. Then the controls made the player's
 * (src/core/keys.h): the keys and every gamepad's inputs read as they are
 * -- a gamepad's buttons, sticks and triggers, and a joystick SDL has no
 * mapping for, its buttons, first hat and first two axes -- and turned
 * into the pad's buttons by the Controls the Escape menu hands over;
 * gamecontrollerdb.txt beside the game adds mappings. */

#include "platform.h"
#include "crt_gpu.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define WINDOW_SCALE 3

/* (build 160) The rumble: what a gamepad's motors were last sent, and the
 * frames since. */
#define RUMBLE_STEP_SHIFT 11    /* a change of a 32nd is sent at once */
#define RUMBLE_RESEND     6     /* a level held is sent again so often, */
#define RUMBLE_MS         250   /* each send good for this long */

typedef struct {
    u16 low, high;
    int frames;
} RumbleSent;

/* (build 160) A gamepad's place: an SDL_Gamepad, or the SDL_Joystick of
 * one SDL has no mapping for; what has gone down on it since the last
 * pump; its axes and hat as their events left them, to tell when one
 * passes half its travel; its motors. */
typedef struct {
    SDL_Gamepad   *pad;
    SDL_Joystick  *joy;
    SDL_JoystickID id;
    PadHeld        latched;
    s16            axis[PAD_AXES];  /* a joystick's: x and y, the first two */
    u8             hat;
    RumbleSent     rumble;
} PadSlot;

/* Pixel width / height for each AspectMode (platform.h). */
static const double ASPECT_PAR[ASPECT_COUNT] = {
    1.0,                /* ASPECT_SQUARE */
    14.0 / 15.0,        /* ASPECT_4_3: (4/3) / (320/224) */
    32.0 / 35.0,        /* ASPECT_TV: 6.14 MHz / 6.71 MHz */
};

struct Platform {
    SDL_Window   *window;
    /* (build 107) The SDL_GPU chain; NULL when the placeholder draws. */
    SDL_GPUDevice *gpu;
    CrtGpu        *crt_gpu;
    char           crt_said[200];   /* the last `crt:` line, said on a change */
    /* (build 108) The swapchain's composition, and which the window
     * offers (asked again when its HDR state or display changes). */
    SDL_GPUSwapchainComposition composition;
    bool           comp_known;
    bool           comp_ok[4];
    int            hdr_said;        /* the HDR state last said: -1 none yet */
    /* The SDR placeholder. */
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    SDL_Texture  *scaled;       /* the frame at whole multiples, see below */
    int           scaled_w, scaled_h;
    bool          scaled_failed;
    AspectMode    aspect;
    bool          fullscreen;   /* as the window is, not as last asked */
    u32           pixels[SCREEN_W * SCREEN_H];
    Uint64        start_ns;
    Uint64        last_present_ns;
    bool          vsync;
    SDL_AudioStream *audio_stream;
    Audio           *audio;
    Buttons          latched;   /* presses seen since the last tick */
    /* (build 160) The Escape menu's, latched the same way: the arrows and
     * Enter, Backspace, the first key and the first gamepad input. */
    Buttons          latched_nav;
    bool             latched_back;
    int              latched_key;
    int              latched_pad_input;
    PadSlot          slot[PLATFORM_PADS];  /* (build 160) as they came */
    Controls         controls;  /* (build 160) CONTROLS's and BUTTONS's */
    CrtSettings      crt;       /* (build 105) kept for phase 2's chain */
    FbDraw           draw;      /* (build 140) PS2_DRAW: which plane is
                                   drawn; (141) the colour plane unless
                                   PS2_DRAW=palette; (141a) the GPU chain's
                                   too */
};

/* (build 160) The keys and gamepad inputs that are the pad (keys.h). Until
 * 160 a KeyMap here per ControlScheme, ARROWS or WASD; both are the
 * defaults now. Escape is the port's own menu key, outside them. */
void platform_set_controls(Platform *p, const Controls *c)
{
    p->controls = *c;
}

void platform_set_aspect(Platform *p, AspectMode mode)
{
    if (mode >= 0 && mode < ASPECT_COUNT) {
        p->aspect = mode;
    }
}

static void frame_layout(const Platform *p, int out_w, int out_h,
                         SDL_FRect *dst, int *step_w, int *step_h);
static void display_state(Platform *p, int out_w, int out_h,
                          CrtDisplay *out);

/* The output in pixels: the GPU's swapchain is the window's pixels. */
static void output_size(Platform *p, int *w, int *h)
{
    *w = 0;
    *h = 0;
    if (p->gpu != NULL) {
        SDL_GetWindowSizeInPixels(p->window, w, h);
    } else {
        SDL_GetRenderOutputSize(p->renderer, w, h);
    }
}

/* (build 112a) The mouse cursor: hidden while the window is fullscreen --
 * the game has no use for the mouse, and a pointer parked over the picture
 * sat in the middle of it -- and shown again in a window. Called wherever
 * the window's fullscreen state is learned: the menu's switch, and the
 * window system's own (a desktop shortcut, a start in fullscreen). */
static void follow_cursor(const Platform *p)
{
    if (p->fullscreen) {
        SDL_HideCursor();
    } else {
        SDL_ShowCursor();
    }
}

/* (build 100) Borderless fullscreen: SDL3's fullscreen with no mode set
 * (SDL_SetWindowFullscreenMode NULL) is the desktop's own size on the
 * display the window is on -- nothing is switched, so going in and out is
 * quick and the display keeps its native resolution, which is what the
 * subpixel simulation will want (docs/engine.md, "Fullscreen").
 * The placement needs nothing here: platform_present asks the renderer's
 * output size every frame and lays the frame out from it, and the
 * whole-multiple render target is remade whenever that layout's size
 * changes, so the frame lands right on the first frame after the window
 * has changed -- the change may land a frame or two after the call on
 * some window systems, which SDL_SyncWindow waits out where it can. */
void platform_set_fullscreen(Platform *p, bool on)
{
    if (p->fullscreen == on) {
        return;
    }
    if (on && !SDL_SetWindowFullscreenMode(p->window, NULL)) {
        SDL_Log("borderless fullscreen unavailable (%s)", SDL_GetError());
        return;
    }
    if (!SDL_SetWindowFullscreen(p->window, on)) {
        SDL_Log("fullscreen %s failed (%s)", on ? "on" : "off",
                SDL_GetError());
        return;
    }
    SDL_SyncWindow(p->window);
    p->fullscreen = on;
    follow_cursor(p);

    /* One line saying what the frame now sits in, for checking the
     * placement: the output in pixels, the frame's size and its bars. */
    int out_w = 0, out_h = 0;
    output_size(p, &out_w, &out_h);
    SDL_FRect dst = { 0.0f, 0.0f, 0.0f, 0.0f };
    int step_w = 0, step_h = 0;
    if (out_w > 0 && out_h > 0) {
        frame_layout(p, out_w, out_h, &dst, &step_w, &step_h);
    }
    const SDL_DisplayMode *mode =
        SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(p->window));
    fprintf(stderr, "display: %s, output %dx%d px (desktop %dx%d at "
                    "density %.2f), frame %.0fx%.0f at %.0f,%.0f, %dx up "
                    "and down\n",
            on ? "fullscreen" : "windowed", out_w, out_h,
            (mode != NULL) ? mode->w : 0, (mode != NULL) ? mode->h : 0,
            (mode != NULL) ? (double)mode->pixel_density : 0.0,
            (double)dst.w, (double)dst.h, (double)dst.x, (double)dst.y,
            (step_h > 0) ? step_h / SCREEN_H : 0);
}

/* SDL pulls on its own thread; the mixer is rendered here in chunks of
 * up to 512 stereo frames, left and right interleaved. `additional` is in
 * bytes; a part-frame is rounded up to a whole one. */
static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream,
                                   int additional, int total)
{
    (void)total;

    Platform *p = (Platform *)userdata;
    if (p->audio == NULL) {
        return;
    }

    float buf[512 * AUDIO_CHANNELS];
    const int frame_bytes   = AUDIO_CHANNELS * (int)sizeof(float);
    int       frames_needed = (additional + frame_bytes - 1) / frame_bytes;

    while (frames_needed > 0) {
        int chunk = (frames_needed > 512) ? 512 : frames_needed;
        audio_render(p->audio, buf, chunk);
        SDL_PutAudioStreamData(stream, buf, chunk * frame_bytes);
        frames_needed -= chunk;
    }
}

void platform_attach_audio(Platform *p, Audio *audio)
{
    p->audio = audio;

    SDL_AudioSpec spec;
    spec.format   = SDL_AUDIO_F32;
    spec.channels = AUDIO_CHANNELS;     /* stereo, left then right */
    spec.freq     = AUDIO_RATE;

    p->audio_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, p);

    if (p->audio_stream == NULL) {
        SDL_Log("audio unavailable (%s); continuing silently",
                SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(p->audio_stream);
}

/* (build 107) The SDL_GPU chain on the window: a device, the compiled
 * shaders, the window claimed with an SDR swapchain and vsync, the
 * pipelines for its format. False, with everything undone, when any of it
 * cannot be had -- the placeholder draws instead. */
static bool gpu_start(Platform *p)
{
    const char *env = SDL_getenv("PS2_GPU");
    if (env != NULL && strcmp(env, "0") == 0) {
        fprintf(stderr, "display: PS2_GPU=0, so the SDL_Renderer "
                        "placeholder\n");
        return false;
    }
    bool debug = SDL_getenv("PS2_GPU_DEBUG") != NULL;
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV
                                             | SDL_GPU_SHADERFORMAT_DXIL
                                             | SDL_GPU_SHADERFORMAT_MSL,
                                             debug, NULL);
    if (dev == NULL) {
        fprintf(stderr, "display: no SDL_GPU device (%s), so the "
                        "SDL_Renderer placeholder\n", SDL_GetError());
        return false;
    }
    CrtGpu *g = crt_gpu_create(dev, "shaders");
    if (g == NULL) {
        SDL_DestroyGPUDevice(dev);
        fprintf(stderr, "display: no CRT shaders, so the SDL_Renderer "
                        "placeholder\n");
        return false;
    }
    if (!SDL_ClaimWindowForGPUDevice(dev, p->window)) {
        fprintf(stderr, "display: SDL_GPU cannot have the window (%s), so "
                        "the SDL_Renderer placeholder\n", SDL_GetError());
        crt_gpu_destroy(g);
        SDL_DestroyGPUDevice(dev);
        return false;
    }
    /* SDR, as sRGB-encoded 8-bit: the chain encodes (phase 3 the HDR
     * swapchain), and vsync paces the game as SDL_Renderer's did. */
    SDL_SetGPUSwapchainParameters(dev, p->window,
                                  SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                  SDL_GPU_PRESENTMODE_VSYNC);
    SDL_GPUTextureFormat format =
        SDL_GetGPUSwapchainTextureFormat(dev, p->window);
    if (!crt_gpu_set_target(g, format)) {
        crt_gpu_destroy(g);
        SDL_ReleaseWindowFromGPUDevice(dev, p->window);
        SDL_DestroyGPUDevice(dev);
        fprintf(stderr, "display: no CRT pipelines, so the SDL_Renderer "
                        "placeholder\n");
        return false;
    }
    p->gpu         = dev;
    p->crt_gpu     = g;
    p->vsync       = true;
    p->composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    p->comp_known  = false;
    return true;
}

/* (build 108) Phase 3: the swapchain the CRT page asks for. */

static const char *composition_name(SDL_GPUSwapchainComposition c)
{
    switch (c) {
        case SDL_GPU_SWAPCHAINCOMPOSITION_SDR: {
            return "SDR";
        }
        case SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR: {
            return "SDR_LINEAR";
        }
        case SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR: {
            return "scRGB (HDR_EXTENDED_LINEAR)";
        }
        case SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084: {
            return "HDR10 (Rec.2020, PQ)";
        }
        default: {
            return "?";
        }
    }
}

/* Which compositions the window offers, asked once and again after SDL
 * says its HDR state or its display changed. */
static void gpu_offered(Platform *p)
{
    if (p->comp_known) {
        return;
    }
    static const SDL_GPUSwapchainComposition ALL[4] = {
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
        SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR,
        SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084,
    };
    for (int i = 0; i < 4; i++) {
        p->comp_ok[i] = SDL_WindowSupportsGPUSwapchainComposition(
            p->gpu, p->window, ALL[i]);
    }
    p->comp_known = true;
}

static bool offered(const Platform *p, SDL_GPUSwapchainComposition c)
{
    int i = (int)c;
    return i >= 0 && i < 4 && p->comp_ok[i];
}

/* HDR ON where SDL says the window has HDR (the page's NO HDR rule): HDR10
 * if the window offers it, else scRGB; PS2_HDR_SWAPCHAIN=scrgb asks for
 * scRGB first, =pq for HDR10 only. SDR otherwise. */
static SDL_GPUSwapchainComposition gpu_wanted(Platform *p,
                                              const CrtDisplay *d)
{
    if (!p->crt.hdr || crt_hdr_why(d) != CRT_OK) {
        return SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    }
    gpu_offered(p);
    const char *env   = SDL_getenv("PS2_HDR_SWAPCHAIN");
    bool        scrgb = env != NULL && strcmp(env, "scrgb") == 0;
    bool        pq    = env != NULL && strcmp(env, "pq") == 0;
    if (scrgb && offered(p, SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR)) {
        return SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR;
    }
    if (offered(p, SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084)) {
        return SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084;
    }
    if (!pq && offered(p, SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR)) {
        return SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR;
    }
    return SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
}

/* The swapchain switched to `c` and the pipelines remade for its format;
 * idle first, as nothing may be in flight on the old swapchain. False
 * (the old swapchain kept, or SDR) when SDL refuses it. */
static bool gpu_switch(Platform *p, SDL_GPUSwapchainComposition c)
{
    SDL_WaitForGPUIdle(p->gpu);
    if (!SDL_SetGPUSwapchainParameters(p->gpu, p->window, c,
                                       SDL_GPU_PRESENTMODE_VSYNC)) {
        fprintf(stderr, "display: the swapchain refused %s (%s)\n",
                composition_name(c), SDL_GetError());
        if (c >= 0 && (int)c < 4) {
            p->comp_ok[(int)c] = false;
        }
        return false;
    }
    SDL_GPUTextureFormat format =
        SDL_GetGPUSwapchainTextureFormat(p->gpu, p->window);
    if (!crt_gpu_set_target(p->crt_gpu, format)) {
        fprintf(stderr, "display: no CRT pipelines for %s\n",
                composition_name(c));
        if (c >= 0 && (int)c < 4) {
            p->comp_ok[(int)c] = false;
        }
        return false;
    }
    p->composition = c;
    fprintf(stderr, "display: swapchain %s, texture format %d\n",
            composition_name(c), (int)format);
    return true;
}

/* Each frame, before the swapchain texture is acquired: the swapchain the
 * page and the window's HDR state ask for. A composition SDL refuses is
 * not asked again until the window's HDR state changes; SDR is the floor. */
static void gpu_follow_hdr(Platform *p)
{
    CrtDisplay d;
    display_state(p, 0, 0, &d);
    int hdr = d.hdr ? 1 : 0;
    if (hdr != p->hdr_said) {
        fprintf(stderr, "display: HDR %s, headroom %.3f, SDR white %.3f "
                        "(%.0f nits), AUTO %d nits\n",
                d.hdr ? "on" : "off", (double)d.headroom,
                (double)d.sdr_white, (double)d.sdr_nits,
                crt_peak_auto(&d));
        p->hdr_said = hdr;
    }
    SDL_GPUSwapchainComposition want = gpu_wanted(p, &d);
    if (want == p->composition) {
        return;
    }
    /* A refused HDR swapchain may have left the old pipelines released
     * or the swapchain changed: SDR again, whole. */
    if (!gpu_switch(p, want) && want != SDL_GPU_SWAPCHAINCOMPOSITION_SDR) {
        gpu_switch(p, SDL_GPU_SWAPCHAINCOMPOSITION_SDR);
    }
}

/* The encode the swapchain wants, for the flat picture and the plan. */
static CrtEncode composition_encode(SDL_GPUSwapchainComposition c)
{
    if (c == SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084) {
        return CRT_ENCODE_PQ;
    }
    if (c == SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR) {
        return CRT_ENCODE_SCRGB;
    }
    return CRT_ENCODE_SRGB;
}

/* scRGB's 1.0 in nits: SDL's SDR white is that many of its linear units,
 * so 1.0 is sdr_nits / sdr_white -- 80 on Windows (scRGB), SDR white
 * (taken as 203 nits) on Wayland and macOS, where SDL's SDR white is 1.0. */
static double scrgb_unit_nits(const CrtDisplay *d)
{
    return (d->sdr_white > 0.0f) ? (double)(d->sdr_nits / d->sdr_white)
                                 : 80.0;
}

/* The SDR placeholder: SDL_Renderer and a streaming texture. */
static bool renderer_start(Platform *p)
{
    p->renderer = SDL_CreateRenderer(p->window, NULL);
    if (p->renderer == NULL) {
        SDL_Log("renderer creation failed: %s", SDL_GetError());
        return false;
    }
    p->texture = SDL_CreateTexture(p->renderer,
                                   SDL_PIXELFORMAT_XRGB8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   SCREEN_W, SCREEN_H);
    if (p->texture == NULL) {
        SDL_Log("texture creation failed: %s", SDL_GetError());
        SDL_DestroyRenderer(p->renderer);
        p->renderer = NULL;
        return false;
    }

    /* Nearest; bilinear on a 320x224 source is worse than no filtering at
     * all. platform_present places the frame itself (whole multiples up
     * and down, the pixel aspect across), so there is no logical
     * presentation, and the bars round it are black. */
    SDL_SetTextureScaleMode(p->texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(p->texture, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(p->renderer, 0, 0, 0, 255);

    /* Without pacing the loop renders as fast as the CPU allows and
     * discards almost every frame. vsync is the good version of this; the
     * sleep in platform_present covers drivers where it is unavailable. */
    p->vsync = SDL_SetRenderVSync(p->renderer, 1);
    if (!p->vsync) {
        SDL_Log("vsync unavailable (%s); pacing with sleep", SDL_GetError());
    }
    return true;
}

/* (build 160) gamecontrollerdb.txt beside the game (SDL_GetBasePath):
 * mappings for gamepads SDL does not know, one a line, as SDL's own
 * community file has them. None there says nothing. */
static void add_mappings(void)
{
    const char *base = SDL_GetBasePath();
    char        path[1024];
    snprintf(path, sizeof path, "%sgamecontrollerdb.txt",
             (base != NULL) ? base : "");
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    fclose(f);
    int n = SDL_AddGamepadMappingsFromFile(path);
    if (n < 0) {
        fprintf(stderr, "gamecontrollerdb.txt: %s\n", SDL_GetError());
    } else {
        fprintf(stderr, "gamecontrollerdb.txt: %d mappings\n", n);
    }
}

Platform *platform_create(const char *title)
{
#ifdef _WIN32
    /* (build 160a) A DualShock 3 under DsHidMini's SXS mode (Sony's
     * sixaxis.sys's protocol) through SDL's own PS3 driver: PS3 CONTROLLER,
     * its PlayStation names and its rumble. Without it SDL takes the pad
     * through Windows's generic path, whose force feedback refuses the
     * rumble. DsHidMini's XInput mode (an Xbox pad) needs nothing. */
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS3_SIXAXIS_DRIVER, "1");
#endif
#ifdef __APPLE__
    /* (build 168a) FULLSCREEN on a Mac in place, not in a Space of its own.
     * Once the GPU chain has drawn into the window, SDL's Space
     * fullscreen takes the switch and never makes it: every call says
     * yes, the window stays as it was and SDL_WINDOW_FULLSCREEN stays
     * off. Only fullscreen at the start, before the first frame, got
     * there. In place it switches every time (docs/macos_build.md). */
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return NULL;
    }
    add_mappings();

    Platform *p = SDL_calloc(1, sizeof *p);
    if (p == NULL) {
        SDL_Quit();
        return NULL;
    }
    controls_default(&p->controls);
    p->latched_key       = KEY_NONE;
    p->latched_pad_input = PAD_NONE;

    /* (build 105) High pixel density: the window's buffer is the panel's
     * own pixels on a scaled desktop (Wayland's fractional scale, macOS's
     * Retina, Windows at 150%) instead of a smaller buffer the desktop
     * scales up -- the frame is placed by the output's size in pixels, and
     * a CRT mask needs every one of them. */
    p->window = SDL_CreateWindow(title, SCREEN_W * WINDOW_SCALE,
                                 SCREEN_H * WINDOW_SCALE,
                                 SDL_WINDOW_RESIZABLE
                                 | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (p->window == NULL) {
        SDL_Log("window creation failed: %s", SDL_GetError());
        SDL_free(p);
        SDL_Quit();
        return NULL;
    }
    if (!gpu_start(p) && !renderer_start(p)) {
        SDL_DestroyWindow(p->window);
        SDL_free(p);
        SDL_Quit();
        return NULL;
    }
    p->aspect   = ASPECT_TV;
    p->hdr_said = -1;
    crt_settings_default(&p->crt);
    p->draw     = fb_draw_from_env();
    if (p->draw == FB_DRAW_PALETTE) {
        fprintf(stderr, "display: PS2_DRAW=palette: the frame through the "
                        "palette, the reference\n");
    }

    /* One line saying what is actually driving the screen. When frames
     * arrive unevenly this is the first thing to know: which video
     * backend, which renderer or GPU driver, and what rate the display
     * really runs at. */
    const SDL_DisplayMode *mode =
        SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(p->window));
    const char *video_driver = SDL_GetCurrentVideoDriver();
    char        drawing[96];
    if (p->gpu != NULL) {
        snprintf(drawing, sizeof drawing, "SDL_GPU %s, %s shaders",
                 SDL_GetGPUDeviceDriver(p->gpu),
                 crt_gpu_shader_format(p->crt_gpu));
    } else {
        const char *render_driver = SDL_GetRendererName(p->renderer);
        snprintf(drawing, sizeof drawing, "renderer %s (the placeholder)",
                 (render_driver != NULL) ? render_driver : "?");
    }
    fprintf(stderr, "display: video %s, %s, %.2f Hz, vsync %s\n",
            (video_driver != NULL) ? video_driver : "?", drawing,
            (mode != NULL) ? (double)mode->refresh_rate : 0.0,
            p->vsync ? "on" : "off (pacing with sleep)");

    p->start_ns       = SDL_GetTicksNS();
    p->last_present_ns = p->start_ns;
    return p;
}

void platform_destroy(Platform *p)
{
    if (p == NULL) {
        return;
    }
    if (p->audio_stream != NULL) {
        SDL_DestroyAudioStream(p->audio_stream);
    }
    for (int i = 0; i < PLATFORM_PADS; i++) {   /* (build 160) */
        PadSlot *sl = &p->slot[i];
        if (sl->pad != NULL) {
            SDL_RumbleGamepad(sl->pad, 0, 0, 0);
            SDL_CloseGamepad(sl->pad);
        } else if (sl->joy != NULL) {
            SDL_RumbleJoystick(sl->joy, 0, 0, 0);
            SDL_CloseJoystick(sl->joy);
        }
    }
    if (p->gpu != NULL) {
        SDL_WaitForGPUIdle(p->gpu);
        crt_gpu_destroy(p->crt_gpu);
        SDL_ReleaseWindowFromGPUDevice(p->gpu, p->window);
        SDL_DestroyGPUDevice(p->gpu);
    }
    if (p->scaled != NULL) {
        SDL_DestroyTexture(p->scaled);
    }
    if (p->texture != NULL) {
        SDL_DestroyTexture(p->texture);
    }
    if (p->renderer != NULL) {
        SDL_DestroyRenderer(p->renderer);
    }
    SDL_DestroyWindow(p->window);
    SDL_free(p);
    SDL_Quit();
}

/* (build 160) A key gone down: the actions it is, through the controls;
 * Escape the menu, Backspace back; the arrows and Enter the menu's own
 * (nav); and the first since the last pump, for the CONTROLS page. */
static void latch_key(Platform *p, SDL_Scancode sc)
{
    int code = (int)sc;
    buttons_from_actions(&p->latched, controls_key_actions(&p->controls,
                                                           code));
    if (code == KEY_ESCAPE) {
        p->latched.menu = true;
    } else if (code == KEY_BACKSPACE) {
        p->latched_back = true;
    }
    p->latched_nav.up    = p->latched_nav.up    || code == KEY_UP;
    p->latched_nav.down  = p->latched_nav.down  || code == KEY_DOWN;
    p->latched_nav.left  = p->latched_nav.left  || code == KEY_LEFT;
    p->latched_nav.right = p->latched_nav.right || code == KEY_RIGHT;
    p->latched_nav.start = p->latched_nav.start || code == KEY_RETURN;
    if (p->latched_key == KEY_NONE) {
        p->latched_key = code;
    }
}

/* ---- (build 160) the gamepads ---- */

static bool slot_used(const PadSlot *sl)
{
    return sl->pad != NULL || sl->joy != NULL;
}

/* The slot holding device `id`, or -1. */
static int slot_of(const Platform *p, SDL_JoystickID id)
{
    for (int i = 0; i < PLATFORM_PADS; i++) {
        if (slot_used(&p->slot[i]) && p->slot[i].id == id) {
            return i;
        }
    }
    return -1;
}

static const char *slot_name(const PadSlot *sl)
{
    const char *name = (sl->pad != NULL) ? SDL_GetGamepadName(sl->pad)
                     : (sl->joy != NULL) ? SDL_GetJoystickName(sl->joy)
                                         : NULL;
    return (name != NULL) ? name : "GAMEPAD";
}

/* Opens device `id` into slot `i`: as a gamepad, or as a joystick SDL has
 * no mapping for. */
static void slot_open(Platform *p, int i, SDL_JoystickID id, bool gamepad)
{
    PadSlot *sl = &p->slot[i];
    memset(sl, 0, sizeof *sl);
    if (gamepad) {
        sl->pad = SDL_OpenGamepad(id);
    } else {
        sl->joy = SDL_OpenJoystick(id);
    }
    if (!slot_used(sl)) {
        SDL_Log("gamepad %u: %s", (unsigned)id, SDL_GetError());
        return;
    }
    sl->id = id;
    fprintf(stderr, "gamepad %d: %s%s\n", i + 1, slot_name(sl),
            gamepad ? "" : " (no mapping: its buttons, hat and stick)");
}

static void slot_close(PadSlot *sl)
{
    if (sl->pad != NULL) {
        SDL_CloseGamepad(sl->pad);
    } else if (sl->joy != NULL) {
        SDL_CloseJoystick(sl->joy);
    }
    memset(sl, 0, sizeof *sl);
}

/* A gamepad, or a joystick SDL has no mapping for, has come: into the
 * first free slot (a third is left alone). A gamepad that is a joystick
 * held already -- SDL has a mapping for it now -- takes its place. */
static void pad_added(Platform *p, SDL_JoystickID id, bool gamepad)
{
    int at = slot_of(p, id);
    if (at >= 0) {
        if (gamepad && p->slot[at].joy != NULL) {
            RumbleSent r = p->slot[at].rumble;
            slot_close(&p->slot[at]);
            slot_open(p, at, id, true);
            p->slot[at].rumble = r;
        }
        return;
    }
    for (int i = 0; i < PLATFORM_PADS; i++) {
        if (!slot_used(&p->slot[i])) {
            slot_open(p, i, id, gamepad);
            return;
        }
    }
}

/* A gamepad or joystick has gone: the ones after it move up, so the slots
 * stay in the order the pads came. */
static void pad_removed(Platform *p, SDL_JoystickID id)
{
    int i = slot_of(p, id);
    if (i < 0) {
        return;
    }
    slot_close(&p->slot[i]);
    for (int j = i; j + 1 < PLATFORM_PADS; j++) {
        p->slot[j] = p->slot[j + 1];
    }
    memset(&p->slot[PLATFORM_PADS - 1], 0, sizeof p->slot[PLATFORM_PADS - 1]);
    fprintf(stderr, "gamepad %d gone\n", i + 1);
}

/* An input gone down on a slot: latched there, and the first since the
 * last pump on any, for the BUTTONS page. */
static void pad_down(Platform *p, PadSlot *sl, int input)
{
    if (input < 0 || input >= PAD_INPUTS) {
        return;
    }
    sl->latched.held[input] = true;
    if (p->latched_pad_input == PAD_NONE) {
        p->latched_pad_input = input;
    }
}

/* What an axis or hat event pressed: each input held after it and not
 * before -- a stick or trigger passing half its travel, a hat's
 * direction. */
static void pad_went_down(Platform *p, PadSlot *sl, const PadHeld *before,
                          const PadHeld *after)
{
    for (int i = 0; i < PAD_INPUTS; i++) {
        if (after->held[i] && !before->held[i]) {
            pad_down(p, sl, i);
        }
    }
}

/* A slot's inputs held now: a gamepad's buttons, sticks and triggers; a
 * joystick's buttons, first hat and first two axes. */
static void slot_held(const PadSlot *sl, PadHeld *h)
{
    memset(h, 0, sizeof *h);
    if (sl->pad != NULL) {
        for (int b = 0; b < PAD_BUTTONS && b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
            h->held[b] = SDL_GetGamepadButton(sl->pad, (SDL_GamepadButton)b);
        }
        s16 axis[PAD_AXES];
        for (int a = 0; a < PAD_AXES; a++) {
            axis[a] = SDL_GetGamepadAxis(sl->pad, (SDL_GamepadAxis)a);
        }
        pad_held_axes(h, axis);
    } else if (sl->joy != NULL) {
        int buttons = SDL_GetNumJoystickButtons(sl->joy);
        for (int b = 0; b < buttons && b < PAD_JOYS; b++) {
            h->held[PAD_JOY1 + b] = SDL_GetJoystickButton(sl->joy, b);
        }
        if (SDL_GetNumJoystickHats(sl->joy) > 0) {
            pad_held_hat(h, SDL_GetJoystickHat(sl->joy, 0));
        }
        if (SDL_GetNumJoystickAxes(sl->joy) >= 2) {
            pad_held_joy_axes(h, SDL_GetJoystickAxis(sl->joy, 0),
                              SDL_GetJoystickAxis(sl->joy, 1));
        }
    }
}

/* One gamepad or joystick event: come, gone, or an input gone down. A
 * gamepad's own inputs come as SDL's gamepad events; a joystick's only
 * from the joysticks held as such (a gamepad sends joystick events too). */
static void pad_event(Platform *p, const SDL_Event *ev)
{
    switch (ev->type) {
        case SDL_EVENT_GAMEPAD_ADDED: {
            pad_added(p, ev->gdevice.which, true);
            break;
        }
        case SDL_EVENT_JOYSTICK_ADDED: {
            if (!SDL_IsGamepad(ev->jdevice.which)) {
                pad_added(p, ev->jdevice.which, false);
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_REMOVED: {
            pad_removed(p, ev->gdevice.which);
            break;
        }
        case SDL_EVENT_JOYSTICK_REMOVED: {
            pad_removed(p, ev->jdevice.which);
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
            int i = slot_of(p, ev->gbutton.which);
            if (i >= 0 && p->slot[i].pad != NULL
                && ev->gbutton.button < PAD_BUTTONS) {
                pad_down(p, &p->slot[i], ev->gbutton.button);
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            int i = slot_of(p, ev->gaxis.which);
            if (i >= 0 && p->slot[i].pad != NULL
                && ev->gaxis.axis < PAD_AXES) {
                PadSlot *sl = &p->slot[i];
                PadHeld  before, after;
                memset(&before, 0, sizeof before);
                memset(&after, 0, sizeof after);
                pad_held_axes(&before, sl->axis);
                sl->axis[ev->gaxis.axis] = ev->gaxis.value;
                pad_held_axes(&after, sl->axis);
                pad_went_down(p, sl, &before, &after);
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN: {
            int i = slot_of(p, ev->jbutton.which);
            if (i >= 0 && p->slot[i].joy != NULL
                && ev->jbutton.button < PAD_JOYS) {
                pad_down(p, &p->slot[i], PAD_JOY1 + ev->jbutton.button);
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_HAT_MOTION: {
            int i = slot_of(p, ev->jhat.which);
            if (i >= 0 && p->slot[i].joy != NULL && ev->jhat.hat == 0) {
                PadSlot *sl = &p->slot[i];
                PadHeld  before, after;
                memset(&before, 0, sizeof before);
                memset(&after, 0, sizeof after);
                pad_held_hat(&before, sl->hat);
                sl->hat = ev->jhat.value;
                pad_held_hat(&after, sl->hat);
                pad_went_down(p, sl, &before, &after);
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
            int i = slot_of(p, ev->jaxis.which);
            if (i >= 0 && p->slot[i].joy != NULL && ev->jaxis.axis < 2) {
                PadSlot *sl = &p->slot[i];
                PadHeld  before, after;
                memset(&before, 0, sizeof before);
                memset(&after, 0, sizeof after);
                pad_held_joy_axes(&before, sl->axis[0], sl->axis[1]);
                sl->axis[ev->jaxis.axis] = ev->jaxis.value;
                pad_held_joy_axes(&after, sl->axis[0], sl->axis[1]);
                pad_went_down(p, sl, &before, &after);
            }
            break;
        }
        default: {
            break;
        }
    }
}

void platform_rumble(Platform *p, int pad, u16 low, u16 high)
{
    if (pad < 0 || pad >= PLATFORM_PADS || !slot_used(&p->slot[pad])) {
        return;
    }
    PadSlot    *sl = &p->slot[pad];
    RumbleSent *r  = &sl->rumble;
    bool on    = low != 0 || high != 0;
    bool was   = r->low != 0 || r->high != 0;
    bool moved = (low >> RUMBLE_STEP_SHIFT) != (r->low >> RUMBLE_STEP_SHIFT)
              || (high >> RUMBLE_STEP_SHIFT) != (r->high >> RUMBLE_STEP_SHIFT);
    r->frames++;
    if (on != was || moved || (on && r->frames >= RUMBLE_RESEND)) {
        if (sl->pad != NULL) {
            SDL_RumbleGamepad(sl->pad, low, high, on ? RUMBLE_MS : 0);
        } else {
            SDL_RumbleJoystick(sl->joy, low, high, on ? RUMBLE_MS : 0);
        }
        r->low    = low;
        r->high   = high;
        r->frames = 0;
    }
}

const char *platform_pad_name(Platform *p, int pad)
{
    if (pad < 0 || pad >= PLATFORM_PADS || !slot_used(&p->slot[pad])) {
        return NULL;
    }
    return slot_name(&p->slot[pad]);
}

PadStyle platform_pad_style(Platform *p, int pad)
{
    if (pad < 0 || pad >= PLATFORM_PADS || !slot_used(&p->slot[pad])) {
        return PAD_STYLE_NONE;
    }
    if (p->slot[pad].pad == NULL) {
        return PAD_STYLE_JOYSTICK;
    }
    switch (SDL_GetGamepadType(p->slot[pad].pad)) {
        case SDL_GAMEPAD_TYPE_XBOX360: {
            return PAD_STYLE_XBOX360;
        }
        case SDL_GAMEPAD_TYPE_XBOXONE: {
            return PAD_STYLE_XBOXONE;
        }
        case SDL_GAMEPAD_TYPE_PS3: {
            return PAD_STYLE_PS3;
        }
        case SDL_GAMEPAD_TYPE_PS4: {
            return PAD_STYLE_PS4;
        }
        case SDL_GAMEPAD_TYPE_PS5: {
            return PAD_STYLE_PS5;
        }
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: {
            return PAD_STYLE_SWITCH;
        }
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: {
            return PAD_STYLE_SWITCH;
        }
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: {
            return PAD_STYLE_SWITCH;
        }
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: {
            return PAD_STYLE_SWITCH;
        }
        case SDL_GAMEPAD_TYPE_GAMECUBE: {
            return PAD_STYLE_GAMECUBE;
        }
        default: {
            return PAD_STYLE_STANDARD;      /* STANDARD, UNKNOWN */
        }
    }
}

bool platform_pump(Platform *p, Input *in)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) {
            return false;
        }
        /* The window system can take the window in or out of fullscreen
         * on its own (a desktop shortcut): follow it, so the next call
         * from the menu is not skipped as already done. */
        if (ev.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) {
            p->fullscreen = true;
            follow_cursor(p);
        } else if (ev.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
            p->fullscreen = false;
            follow_cursor(p);
        }
        /* (build 108) HDR switched on or off in the desktop, its headroom
         * changed, or the window moved to another display: ask again which
         * swapchains the window offers; the next frame follows
         * (gpu_follow_hdr) and says so. */
        if (ev.type == SDL_EVENT_WINDOW_HDR_STATE_CHANGED
            || ev.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
            p->comp_known = false;
        }
        if (ev.type == SDL_EVENT_KEY_DOWN) {
            if (!ev.key.repeat) {
                /* Latched, not sampled: a tap between two frames still
                 * reaches the game. Escape used to quit here; it is now
                 * a button the game reads (the system menu). */
                latch_key(p, ev.key.scancode);
            }
        }
        /* (build 160) The gamepads: come and gone, and what goes down on
         * them latched as the keys' presses are. */
        pad_event(p, &ev);
    }

    /* (build 160) Every key held, through the controls; Escape the menu's;
     * the arrows and Enter the menu's own whatever is bound. */
    input_clear(in);
    int         numkeys = 0;
    const bool *keys    = SDL_GetKeyboardState(&numkeys);
    u32         actions = 0;
    for (int code = 0; code < numkeys; code++) {
        if (keys[code]) {
            actions |= controls_key_actions(&p->controls, code);
        }
    }
    buttons_from_actions(&in->held, actions);
    in->held.menu      = keys[SDL_SCANCODE_ESCAPE];
    in->nav_held.up    = keys[SDL_SCANCODE_UP];
    in->nav_held.down  = keys[SDL_SCANCODE_DOWN];
    in->nav_held.left  = keys[SDL_SCANCODE_LEFT];
    in->nav_held.right = keys[SDL_SCANCODE_RIGHT];
    in->nav_held.start = keys[SDL_SCANCODE_RETURN];

    /* (build 160) Every gamepad: what is held now and what went down
     * since the last pump (a tap counts), through the controls; its d-pad
     * and left stick the menu's whatever is bound; Back the menu. */
    for (int i = 0; i < PLATFORM_PADS; i++) {
        PadSlot *sl = &p->slot[i];
        if (!slot_used(sl)) {
            continue;
        }
        PadHeld h;
        slot_held(sl, &h);
        for (int k = 0; k < PAD_INPUTS; k++) {
            h.held[k] = h.held[k] || sl->latched.held[k];
        }
        buttons_from_actions(&in->held, controls_pad_actions(&p->controls,
                                                             &h));
        buttons_from_actions(&in->nav_held, pad_nav_actions(&h));
        buttons_from_actions(&p->latched,
                             controls_pad_actions(&p->controls,
                                                  &sl->latched));
        buttons_from_actions(&p->latched_nav, pad_nav_actions(&sl->latched));
        if (sl->pad != NULL) {
            in->held.menu   = in->held.menu || h.held[PAD_BACK];
            p->latched.menu = p->latched.menu || sl->latched.held[PAD_BACK];
        }
        memset(&sl->latched, 0, sizeof sl->latched);
    }

    in->pressed     = p->latched;
    in->nav_pressed = p->latched_nav;
    in->back        = p->latched_back;
    in->key         = p->latched_key;
    in->pad_input   = p->latched_pad_input;
    memset(&p->latched, 0, sizeof p->latched);
    memset(&p->latched_nav, 0, sizeof p->latched_nav);
    p->latched_back      = false;
    p->latched_key       = KEY_NONE;
    p->latched_pad_input = PAD_NONE;
    return true;
}

/* The render-target copy of the frame at w x h, made (or remade, when
 * the window changes size) on demand. Linear, so the one non-integer
 * step -- across, down to the aspect's width -- blends at most one column
 * at each pixel's edge rather than doubling some columns and not others,
 * which is what nearest does to a 32:35 pixel. NULL if the renderer
 * cannot make render targets; the caller then draws nearest. */
static SDL_Texture *scaled_target(Platform *p, int w, int h)
{
    if (p->scaled != NULL && p->scaled_w == w && p->scaled_h == h) {
        return p->scaled;
    }
    if (p->scaled != NULL) {
        SDL_DestroyTexture(p->scaled);
        p->scaled = NULL;
    }
    if (p->scaled_failed) {
        return NULL;
    }
    p->scaled = SDL_CreateTexture(p->renderer, SDL_PIXELFORMAT_XRGB8888,
                                  SDL_TEXTUREACCESS_TARGET, w, h);
    if (p->scaled == NULL) {
        SDL_Log("no render target (%s); the aspect is drawn nearest",
                SDL_GetError());
        p->scaled_failed = true;
        return NULL;
    }
    SDL_SetTextureScaleMode(p->scaled, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(p->scaled, SDL_BLENDMODE_NONE);
    p->scaled_w = w;
    p->scaled_h = h;
    return p->scaled;
}

/* Where the frame goes in a window out_w x out_h, and the whole-multiple
 * size to scale it to first. Up and down it is the largest whole multiple
 * of 224 that fits, as before, so the lines stay even; across it is that
 * many lines' worth at the pixel aspect, rounded to a whole screen pixel.
 * The first step, nearest, is to the next whole multiple of 320 at or
 * above that width; the second, linear, brings it down to it. At 1:1 the
 * two widths are the same and nothing is blended. A window too small for
 * one whole multiple gets the frame fitted to it instead. */
static void frame_layout(const Platform *p, int out_w, int out_h,
                         SDL_FRect *dst, int *step_w, int *step_h)
{
    /* (build 107) The arithmetic is crt.c's crt_sharp_layout now, which
     * the GPU's flat picture and tools/crt_gpu_check.c share. */
    CrtSharp l;
    crt_sharp_layout(out_w, out_h, ASPECT_PAR[p->aspect], &l);
    dst->w  = (float)l.w;
    dst->h  = (float)l.h;
    dst->x  = (float)l.x;
    dst->y  = (float)l.y;
    *step_w = l.step_w;
    *step_h = l.step_h;
}

/* (build 107) What the chain draws on an output of w x h: the CRT page's
 * settings, each switched off where this display cannot have it (the
 * rules the page shows, crt.c). (build 108) In nits by PEAK and WHITE on
 * an HDR swapchain, else SDR. False when neither the scanlines nor a mask
 * is left: the flat picture. */
static bool crt_plan_now(Platform *p, int w, int h, CrtPlan *plan)
{
    CrtDisplay d;
    display_state(p, w, h, &d);
    double  par  = crt_pixel_aspect((int)p->aspect);
    bool    scan = p->crt.scanlines && crt_scanlines_why(&d, par) == CRT_OK;
    CrtMask mask = (p->crt.mask != CRT_MASK_OFF
                    && crt_mask_why(&d, par, p->crt.mask) == CRT_OK)
                 ? p->crt.mask : CRT_MASK_OFF;
    if (!scan && mask == CRT_MASK_OFF) {
        return false;
    }
    memset(plan, 0, sizeof *plan);
    plan->out_w         = w;
    plan->out_h         = h;
    plan->par           = par;
    plan->set           = p->crt;
    plan->set.scanlines = scan;
    plan->set.mask      = mask;
    plan->encode        = composition_encode(p->composition);
    plan->hdr_out       = plan->encode != CRT_ENCODE_SRGB;
    plan->scrgb_nits    = scrgb_unit_nits(&d);
    plan->peak_nits     = (p->crt.peak_nits > 0) ? p->crt.peak_nits
                                                 : crt_peak_auto(&d);
    crt_plan_resolve(plan);
    return true;
}

/* (build 108) The flat picture's encode: the swapchain's, its white the
 * HDR page's WHITE. */
static void flat_encode(Platform *p, CrtEncodeParams *e)
{
    CrtDisplay d;
    display_state(p, 0, 0, &d);
    crt_encode_params(composition_encode(p->composition),
                      (p->crt.white_nits > 0) ? p->crt.white_nits
                                              : CRT_WHITE_DEFAULT,
                      scrgb_unit_nits(&d), e);
}

/* One stderr line when what is drawn changes. */
static void say_chain(Platform *p, const CrtPlan *plan, int w, int h)
{
    static const char *const MASK[CRT_MASK_COUNT] = {
        "no mask", "aperture grille", "slot mask", "shadow mask"
    };
    static const char *const SUB[CRT_SUBPIXELS_COUNT] = {
        "RGB", "BGR", "OTHER"
    };
    char line[sizeof p->crt_said];
    char mode[96];
    int  white = (p->crt.white_nits > 0) ? p->crt.white_nits
                                         : CRT_WHITE_DEFAULT;
    if (p->composition == SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084) {
        snprintf(mode, sizeof mode, "HDR10 PQ, WHITE %d", white);
    } else if (p->composition
               == SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR) {
        CrtDisplay d;
        display_state(p, 0, 0, &d);
        snprintf(mode, sizeof mode, "scRGB (1.0 = %.0f nits), WHITE %d",
                 scrgb_unit_nits(&d), white);
    } else if (p->crt.hdr) {
        CrtDisplay d;
        display_state(p, 0, 0, &d);
        snprintf(mode, sizeof mode, "SDR (HDR ON, but %s)",
                 d.hdr ? "no HDR swapchain the window offers"
                       : "the window has no HDR");
    } else {
        snprintf(mode, sizeof mode, "SDR");
    }
    if (plan == NULL) {
        snprintf(line, sizeof line, "crt: off, the flat picture, %dx%d, %s",
                 w, h, mode);
    } else if (plan->hdr_out) {
        snprintf(line, sizeof line,
                 "crt: %s, %s (%s), %dx%d, k %d, triad %d, %s, PEAK %.0f, "
                 "scan depth %.2f, mask depth %.2f",
                 plan->set.scanlines ? "scanlines" : "no scanlines",
                 MASK[plan->set.mask], SUB[plan->set.subpixels], w, h,
                 plan->k, plan->triad, mode, plan->peak_nits,
                 plan->scan_depth, plan->mask_depth);
    } else {
        snprintf(line, sizeof line,
                 "crt: %s, %s (%s), %dx%d, k %d, triad %d, %s",
                 plan->set.scanlines ? "scanlines" : "no scanlines",
                 MASK[plan->set.mask], SUB[plan->set.subpixels], w, h,
                 plan->k, plan->triad, mode);
    }
    if (strcmp(line, p->crt_said) != 0) {
        snprintf(p->crt_said, sizeof p->crt_said, "%s", line);
        fprintf(stderr, "%s\n", line);
    }
}

/* (build 107) The SDL_GPU chain. True when it waited on the display (the
 * swapchain's vsync), false when there was nothing to draw into (a
 * minimised window) and the caller must pace. */
static bool present_gpu(Platform *p, const Framebuffer *fb,
                        const Palette *pal)
{
    /* (build 141a) The chain takes the colour plane: the frame's own, or
     * with PS2_DRAW=palette the palette path's colours, pixel by pixel. */
    static Rgb8 reference[SCREEN_W * SCREEN_H];
    const Rgb8 *colour = fb->rgb;
    if (p->draw == FB_DRAW_PALETTE) {
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
            reference[i] = fb_palette_colour(fb, pal, i);
        }
        colour = reference;
    }
    gpu_follow_hdr(p);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(p->gpu);
    if (cmd == NULL) {
        return false;
    }
    SDL_GPUTexture *swap = NULL;
    Uint32          w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, p->window, &swap, &w,
                                               &h)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return false;
    }
    if (swap == NULL) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return false;
    }
    CrtPlan plan;
    bool    crt = crt_plan_now(p, (int)w, (int)h, &plan);
    if (!crt_gpu_upload(p->crt_gpu, cmd, colour, crt ? &plan : NULL)) {
        crt = false;
    }
    say_chain(p, crt ? &plan : NULL, (int)w, (int)h);
    if (crt) {
        crt_gpu_draw_crt(p->crt_gpu, cmd, swap);
    } else {
        CrtSharp        l;
        CrtEncodeParams e;
        crt_sharp_layout((int)w, (int)h, ASPECT_PAR[p->aspect], &l);
        flat_encode(p, &e);
        crt_gpu_draw_sharp(p->crt_gpu, cmd, swap, &l, &e);
    }
    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
}

/* The SDR placeholder. */
static void present_renderer(Platform *p, const Framebuffer *fb,
                             const Palette *pal)
{
    /* Resolve the palette once per frame rather than once per pixel: the
     * game's 64 entries, then the overlay's own 16 (video.h). */
    u32 lut[FB_INDICES];
    for (int i = 0; i < FB_INDICES; i++) {
        Cram cram = (i < PAL_TOTAL) ? pal->entry[i]
                                    : fb->overlay[i - FB_OVERLAY_BASE];
        Rgb8 c = cram_to_rgb8(cram);
        lut[i] = ((u32)c.r << 16) | ((u32)c.g << 8) | (u32)c.b;
    }

    /* (build 140) Or the colour plane as it is -- (build 141) the one
     * shown, unless PS2_DRAW=palette. */
    if (p->draw == FB_DRAW_COLOUR) {
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
            Rgb8 c = fb->rgb[i];
            p->pixels[i] = ((u32)c.r << 16) | ((u32)c.g << 8) | (u32)c.b;
        }
    } else {
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
            /* (build 146a) A picture's pixel has no entry: its colour is
             * the colour plane's (fb_palette_colour). */
            Rgb8 c = fb->rgb[i];
            p->pixels[i] = (fb->pic[i] != 0)
                         ? ((u32)c.r << 16) | ((u32)c.g << 8) | (u32)c.b
                         : lut[fb->px[i]];
        }
    }

    SDL_UpdateTexture(p->texture, NULL, p->pixels, SCREEN_W * (int)sizeof(u32));

    /* The whole-multiple step goes to its own target first, so the window
     * is cleared and drawn in one go after it. At 1:1 (and whenever the
     * renderer has no targets) the frame goes straight to the window,
     * nearest. */
    int out_w = 0, out_h = 0;
    SDL_GetRenderOutputSize(p->renderer, &out_w, &out_h);

    SDL_FRect    dst    = { 0.0f, 0.0f, 0.0f, 0.0f };
    SDL_Texture *source = p->texture;
    if (out_w > 0 && out_h > 0) {
        int step_w, step_h;
        frame_layout(p, out_w, out_h, &dst, &step_w, &step_h);
        if ((float)step_w != dst.w || (float)step_h != dst.h) {
            SDL_Texture *scaled = scaled_target(p, step_w, step_h);
            if (scaled != NULL) {
                SDL_SetRenderTarget(p->renderer, scaled);
                SDL_RenderTexture(p->renderer, p->texture, NULL, NULL);
                SDL_SetRenderTarget(p->renderer, NULL);
                source = scaled;
            }
        }
    }

    SDL_RenderClear(p->renderer);
    if (dst.w > 0.0f && dst.h > 0.0f) {
        SDL_RenderTexture(p->renderer, source, NULL, &dst);
    }
    SDL_RenderPresent(p->renderer);
}

void platform_present(Platform *p, const Framebuffer *fb, const Palette *pal)
{
    bool waited = p->vsync;
    if (p->gpu != NULL) {
        waited = present_gpu(p, fb, pal);
    } else {
        present_renderer(p, fb, pal);
    }

    if (!waited) {
        /* SDL_RenderPresent returned immediately, so wait out the rest of
         * the frame here rather than spinning in the game loop. */
        const Uint64 frame_ns = (Uint64)(1e9 / TICK_HZ);
        Uint64 elapsed = SDL_GetTicksNS() - p->last_present_ns;

        if (elapsed < frame_ns) {
            SDL_DelayNS(frame_ns - elapsed);
        }
    }
    p->last_present_ns = SDL_GetTicksNS();
}

double platform_now(Platform *p)
{
    return (double)(SDL_GetTicksNS() - p->start_ns) / 1e9;
}

void platform_sleep(double seconds)
{
    SDL_DelayNS((Uint64)(seconds * 1e9));
}

/* (build 105) The CRT page's settings. (build 107) The SDL_GPU chain draws
 * them from the next frame (present_gpu); the placeholder ignores them. */
void platform_set_crt(Platform *p, const CrtSettings *s)
{
    p->crt = *s;
}

/* (build 105) What the CRT page judges its values against (crt.h,
 * CrtDisplay). The output is the renderer's, in pixels; fullscreen's size
 * is the desktop mode's in pixels. The window is taken as scaled by the
 * desktop when its pixel density falls short of the desktop mode's: its
 * buffer is smaller than the panel area it covers, so the compositor
 * scales it up. Whether the desktop's own mode is the panel's is not
 * asked here: SDL has no call for the panel's native size, and the
 * largest fullscreen mode is not it everywhere (macOS lists scaled modes
 * above the native one), so a false DESKTOP SCALED would lock the masks
 * out; tools/display_probe.c prints what it would be decided from.
 * HDR is the window's own properties: enabled, the headroom over SDR
 * white, and SDR white in the linear colourspace's units. */
void platform_display_info(Platform *p, CrtDisplay *out)
{
    int w = 0, h = 0;
    output_size(p, &w, &h);
    display_state(p, w, h, out);
}

/* (build 107) platform_display_info for an output of out_w x out_h: the
 * menu asks with the window's, present_gpu with the swapchain texture's. */
static void display_state(Platform *p, int out_w, int out_h,
                          CrtDisplay *out)
{
    crt_display_default(out);
    if (out_w > 0 && out_h > 0) {
        out->out_w = out_w;
        out->out_h = out_h;
    }
    out->fullscreen = p->fullscreen;
    /* (build 141c) The placeholder draws: no CRT setting can show, and the
     * menu says why rather than taking them as on. */
    out->no_chain   = (p->crt_gpu == NULL);

    SDL_DisplayID          id   = SDL_GetDisplayForWindow(p->window);
    const SDL_DisplayMode *desk = SDL_GetDesktopDisplayMode(id);
    if (desk != NULL) {
        float density = (desk->pixel_density > 0.0f) ? desk->pixel_density
                                                     : 1.0f;
        out->full_w = (int)(desk->w * density + 0.5f);
        out->full_h = (int)(desk->h * density + 0.5f);
        float win_density = SDL_GetWindowPixelDensity(p->window);
        if (!p->fullscreen && win_density > 0.0f
            && fabsf(win_density - density) > 0.01f) {
            out->window_scaled = true;
        }
    }

    SDL_PropertiesID props = SDL_GetWindowProperties(p->window);
    out->hdr       = SDL_GetBooleanProperty(
        props, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
    out->headroom  = SDL_GetFloatProperty(
        props, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f);
    out->sdr_white = SDL_GetFloatProperty(
        props, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 1.0f);
    /* SDR white in nits. Windows's linear space is scRGB, whose 1.0 is 80
     * nits. On Wayland and macOS SDL's SDR white is 1.0 and its nits are
     * not said (the P16 under Wayland: headroom 1.624, SDR white 1.000),
     * so it is taken as BT.2408's reference white, 203 nits. */
#ifdef _WIN32
    out->sdr_nits = out->sdr_white * 80.0f;
#else
    out->sdr_nits = out->sdr_white * 203.0f;
#endif
}
