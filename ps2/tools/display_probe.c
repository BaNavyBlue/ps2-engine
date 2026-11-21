/* (build 105) The display probe: what SDL3 says about the window, the
 * screen and the GPU, windowed and fullscreen -- the numbers the CRT page
 * decides from (docs/crt.md), taken on the real screens. SDL3 only, not
 * the game; Linux, Windows and macOS.
 *
 *     make display_probe
 *     ./display_probe > display_probe.txt       # --seconds N per state
 *
 * For each state (windowed, fullscreen, windowed again) it opens or
 * resizes nothing else: the window is 960x672 points, resizable, high
 * pixel density (as the game's), drawn grey through SDL_GPU so the window
 * system maps it and says what it is. It prints
 *   - SDL's version, the platform, the video driver;
 *   - every display: its name, desktop and current modes (size, pixel
 *     density, refresh), content scale, HDR, and its fullscreen modes;
 *   - the GPU drivers SDL has, and the device it makes preferring
 *     performance and preferring low power (the driver, the shader
 *     formats, and from SDL 3.4 the device's name -- a laptop's discrete
 *     GPU beside the CPU's own);
 *   - the window: size in points and in pixels, pixel density, display
 *     scale (a scaled desktop must not scale the frame: with high pixel
 *     density the pixels should be the panel's), fullscreen, SDL's HDR
 *     state (enabled, headroom, SDR white), and which swapchain
 *     compositions (SDR, SDR_LINEAR, HDR_EXTENDED_LINEAR, HDR10_ST2084)
 *     and present modes (VSYNC, IMMEDIATE, MAILBOX) the device offers it;
 *   - the same state as the words the headless game's PS2_DISPLAY and
 *     the route probe's `display` take, so a route can show exactly what
 *     the menu will say on this screen.
 * HDR on Linux needs SDL 3.2.6 or later (Wayland colour management), a
 * compositor that speaks it, the driver, and the monitor in HDR mode.
 * Properties newer than SDL 3.2 are behind #ifdef. */

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W 960
#define WIN_H 672

static void print_mode(const char *what, const SDL_DisplayMode *m)
{
    if (m == NULL) {
        printf("    %-8s (none: %s)\n", what, SDL_GetError());
        return;
    }
    printf("    %-8s %dx%d points, pixel density %.3f = %dx%d pixels, "
           "%.3f Hz\n", what, m->w, m->h, (double)m->pixel_density,
           (int)(m->w * m->pixel_density + 0.5f),
           (int)(m->h * m->pixel_density + 0.5f), (double)m->refresh_rate);
}

static void print_displays(void)
{
    int            count = 0;
    SDL_DisplayID *ids   = SDL_GetDisplays(&count);
    printf("displays: %d\n", count);
    for (int i = 0; ids != NULL && i < count; i++) {
        SDL_DisplayID id   = ids[i];
        const char   *name = SDL_GetDisplayName(id);
        printf("  display %u \"%s\"\n", (unsigned)id,
               (name != NULL) ? name : "?");
        print_mode("desktop", SDL_GetDesktopDisplayMode(id));
        print_mode("current", SDL_GetCurrentDisplayMode(id));
        printf("    content scale %.3f\n",
               (double)SDL_GetDisplayContentScale(id));
        SDL_PropertiesID props = SDL_GetDisplayProperties(id);
        printf("    HDR enabled (display): %s\n",
               SDL_GetBooleanProperty(props,
                                      SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN,
                                      false) ? "yes" : "no");

        int              n     = 0;
        SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(id, &n);
        printf("    fullscreen modes: %d%s\n", n,
               (n > 0) ? " (largest first)" : "");
        for (int m = 0; modes != NULL && m < n && m < 12; m++) {
            printf("      %dx%d @ %.3f = %dx%d pixels, %.3f Hz\n",
                   modes[m]->w, modes[m]->h,
                   (double)modes[m]->pixel_density,
                   (int)(modes[m]->w * modes[m]->pixel_density + 0.5f),
                   (int)(modes[m]->h * modes[m]->pixel_density + 0.5f),
                   (double)modes[m]->refresh_rate);
        }
        if (n > 12) {
            printf("      ... %d more\n", n - 12);
        }
        SDL_free(modes);
    }
    SDL_free(ids);
}

/* A device with every shader format the game will ship. */
static SDL_GPUDevice *make_device(bool low_power)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props,
                           SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN,
                           true);
    SDL_SetBooleanProperty(props,
                           SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN,
                           true);
    SDL_SetBooleanProperty(props,
                           SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN,
                           true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,
                           false);
    SDL_SetBooleanProperty(props,
                           SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN,
                           low_power);
    SDL_GPUDevice *dev = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    return dev;
}

static void print_device(const char *what, SDL_GPUDevice *dev)
{
    if (dev == NULL) {
        printf("  %s: no device (%s)\n", what, SDL_GetError());
        return;
    }
    SDL_GPUShaderFormat f = SDL_GetGPUShaderFormats(dev);
    printf("  %s: driver %s, shaders%s%s%s\n", what,
           SDL_GetGPUDeviceDriver(dev),
           (f & SDL_GPU_SHADERFORMAT_SPIRV) ? " SPIR-V" : "",
           (f & SDL_GPU_SHADERFORMAT_DXIL) ? " DXIL" : "",
           (f & SDL_GPU_SHADERFORMAT_MSL) ? " MSL" : "");
#ifdef SDL_PROP_GPU_DEVICE_NAME_STRING
    SDL_PropertiesID props = SDL_GetGPUDeviceProperties(dev);
    printf("    device \"%s\"\n",
           SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING,
                                 "?"));
#ifdef SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING
    /* Direct3D 12 gives the version but no name. */
    printf("    driver \"%s\" %s\n",
           SDL_GetStringProperty(props,
                                 SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING,
                                 "(not given)"),
           SDL_GetStringProperty(props,
                                 SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING,
                                 ""));
#endif
#else
    printf("    (the device's name needs SDL 3.4: built against %d.%d.%d)\n",
           SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
#endif
}

/* Draws the window grey, so the window system maps it and reports on
 * it; returns false if the swapchain is not there yet. */
static bool draw_grey(SDL_GPUDevice *dev, SDL_Window *win)
{
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (cmd == NULL) {
        return false;
    }
    SDL_GPUTexture *tex = NULL;
    Uint32          w = 0, h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, win, &tex, &w, &h)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return false;
    }
    if (tex != NULL) {
        SDL_GPUColorTargetInfo target;
        SDL_zero(target);
        target.texture     = tex;
        target.clear_color = (SDL_FColor){ 0.25f, 0.25f, 0.25f, 1.0f };
        target.load_op     = SDL_GPU_LOADOP_CLEAR;
        target.store_op    = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &target, 1,
                                                         NULL);
        if (pass != NULL) {
            SDL_EndGPURenderPass(pass);
        }
    }
    SDL_SubmitGPUCommandBuffer(cmd);
    return tex != NULL;
}

static const char *event_name(Uint32 type)
{
    switch (type) {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            return "pixel size changed";
        }
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
            return "display scale changed";
        }
        case SDL_EVENT_WINDOW_HDR_STATE_CHANGED: {
            return "HDR state changed";
        }
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED: {
            return "display changed";
        }
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: {
            return "entered fullscreen";
        }
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: {
            return "left fullscreen";
        }
        default: {
            return NULL;
        }
    }
}

/* Draws and pumps for `seconds`, printing the window events that bear on
 * the CRT page. False if the user closed the window. */
static bool settle(SDL_GPUDevice *dev, SDL_Window *win, double seconds)
{
    Uint64 end = SDL_GetTicksNS() + (Uint64)(seconds * 1e9);
    while (SDL_GetTicksNS() < end) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                return false;
            }
            const char *name = event_name(ev.type);
            if (name != NULL) {
                printf("    event: %s\n", name);
            }
        }
        if (dev != NULL) {
            draw_grey(dev, win);
        } else {
            SDL_Delay(16);
        }
    }
    return true;
}

/* The line count the game would use (platform_sdl3.c's frame_layout, at
 * the TV aspect 32:35). */
static int lines_k(int w, int h)
{
    double across = 320.0 * 32.0 / 35.0;
    int    k      = h / 224;
    int    k_w    = (int)((double)w / across);
    return (k_w < k) ? k_w : k;
}

static void print_window(const char *state, SDL_GPUDevice *dev,
                         SDL_Window *win)
{
    int w = 0, h = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(win, &w, &h);
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    float          density = SDL_GetWindowPixelDensity(win);
    float          scale   = SDL_GetWindowDisplayScale(win);
    bool           full    = (SDL_GetWindowFlags(win)
                              & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_DisplayID  id      = SDL_GetDisplayForWindow(win);
    const SDL_DisplayMode *desk = SDL_GetDesktopDisplayMode(id);

    printf("\nwindow, %s (on display %u)\n", state, (unsigned)id);
    printf("  size %dx%d points, %dx%d pixels, pixel density %.3f, display "
           "scale %.3f, fullscreen %s\n", w, h, pw, ph, (double)density,
           (double)scale, full ? "yes" : "no");

    SDL_PropertiesID props    = SDL_GetWindowProperties(win);
    bool             hdr      = SDL_GetBooleanProperty(
        props, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
    float            headroom = SDL_GetFloatProperty(
        props, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f);
    float            white    = SDL_GetFloatProperty(
        props, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 1.0f);
    /* SDR white in nits as the game takes it (platform_sdl3.c): scRGB's
     * 80 on Windows, BT.2408's 203 where SDL's SDR white is 1.0 and its
     * nits are not said. */
#ifdef _WIN32
    float sdr_nits = white * 80.0f;
#else
    float sdr_nits = white * 203.0f;
#endif
    /* PEAK's AUTO as the game works it out (crt_peak_auto): only with HDR
     * on and some headroom, else 1000. */
    bool  auto_hdr  = hdr && headroom > 1.0f && sdr_nits > 0.0f;
    float auto_nits = auto_hdr ? headroom * sdr_nits : 1000.0f;
    printf("  HDR enabled %s, headroom %.3f, SDR white %.3f (taken as %.0f "
           "nits), PEAK's AUTO %.0f nits%s\n",
           hdr ? "yes" : "no", (double)headroom, (double)white,
           (double)sdr_nits, (double)auto_nits,
           auto_hdr ? " (headroom x SDR white)" : " (no HDR: the fallback)");

    if (dev != NULL) {
        static const struct {
            SDL_GPUSwapchainComposition c;
            const char                 *name;
        } COMP[] = {
            { SDL_GPU_SWAPCHAINCOMPOSITION_SDR, "SDR" },
            { SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR, "SDR_LINEAR" },
            { SDL_GPU_SWAPCHAINCOMPOSITION_HDR_EXTENDED_LINEAR,
              "HDR_EXTENDED_LINEAR" },
            { SDL_GPU_SWAPCHAINCOMPOSITION_HDR10_ST2084, "HDR10_ST2084" },
        };
        static const struct {
            SDL_GPUPresentMode m;
            const char        *name;
        } MODE[] = {
            { SDL_GPU_PRESENTMODE_VSYNC, "VSYNC" },
            { SDL_GPU_PRESENTMODE_IMMEDIATE, "IMMEDIATE" },
            { SDL_GPU_PRESENTMODE_MAILBOX, "MAILBOX" },
        };
        printf("  swapchain compositions:");
        for (size_t i = 0; i < sizeof COMP / sizeof COMP[0]; i++) {
            printf(" %s %s", COMP[i].name,
                   SDL_WindowSupportsGPUSwapchainComposition(dev, win,
                                                             COMP[i].c)
                       ? "yes" : "no");
        }
        printf("\n  present modes:");
        for (size_t i = 0; i < sizeof MODE / sizeof MODE[0]; i++) {
            printf(" %s %s", MODE[i].name,
                   SDL_WindowSupportsGPUPresentMode(dev, win, MODE[i].m)
                       ? "yes" : "no");
        }
        printf("\n");
    }

    /* The game's own reading of the same numbers (platform_sdl3.c's
     * platform_display_info), and the words that show it headless. */
    int   full_w = 0, full_h = 0;
    bool  scaled = false;
    if (desk != NULL) {
        float dd = (desk->pixel_density > 0.0f) ? desk->pixel_density : 1.0f;
        full_w = (int)(desk->w * dd + 0.5f);
        full_h = (int)(desk->h * dd + 0.5f);
        scaled = !full && density > 0.0f && fabsf(density - dd) > 0.01f;
    }
    int n = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(id, &n);
    if (modes != NULL && n > 0) {
        int nw = (int)(modes[0]->w * modes[0]->pixel_density + 0.5f);
        int nh = (int)(modes[0]->h * modes[0]->pixel_density + 0.5f);
        printf("  the largest fullscreen mode is %dx%d pixels, the desktop "
               "%dx%d: %s (the game does not decide DESKTOP SCALED from "
               "this yet)\n", nw, nh, full_w, full_h,
               (nw == full_w && nh == full_h) ? "the same" : "DIFFERENT");
    }
    SDL_free(modes);
    printf("  lines a scanline (k) now %d, fullscreen %d; the window is %s "
           "by the desktop\n", lines_k(pw, ph), lines_k(full_w, full_h),
           scaled ? "SCALED" : "not scaled");
    printf("  PS2_DISPLAY=\"%dx%d%s full=%dx%d%s", pw, ph,
           full ? " fullscreen" : "", full_w, full_h,
           scaled ? " window-scaled" : "");
    if (hdr) {
        printf(" hdr=%.3f/%.3f sdr=%.0f", (double)headroom, (double)white,
               (double)sdr_nits);
    }
    printf("\"\n");
}

int main(int argc, char **argv)
{
    double seconds = 1.5;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = atof(argv[++i]);
        } else {
            fprintf(stderr, "usage: display_probe [--seconds N]\n");
            return 2;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int v = SDL_GetVersion();
    printf("SDL %d.%d.%d linked (built against %d.%d.%d), %s, revision %s\n",
           SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v),
           SDL_VERSIONNUM_MICRO(v), SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
           SDL_MICRO_VERSION, SDL_GetPlatform(), SDL_GetRevision());
    const char *video = SDL_GetCurrentVideoDriver();
    printf("video driver %s\n", (video != NULL) ? video : "?");
    if (video != NULL && strcmp(video, "wayland") == 0
        && v < SDL_VERSIONNUM(3, 2, 6)) {
        printf("  (HDR on Wayland needs SDL 3.2.6 or later)\n");
    }
    print_displays();

    printf("GPU drivers:");
    for (int i = 0; i < SDL_GetNumGPUDrivers(); i++) {
        printf(" %s", SDL_GetGPUDriver(i));
    }
    printf("\n");
    SDL_GPUDevice *low = make_device(true);
    print_device("preferring low power", low);
    if (low != NULL) {
        SDL_DestroyGPUDevice(low);
    }
    SDL_GPUDevice *dev = make_device(false);
    print_device("preferring performance (the one used below)", dev);

    SDL_Window *win = SDL_CreateWindow("display_probe", WIN_W, WIN_H,
                                       SDL_WINDOW_RESIZABLE
                                       | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (win == NULL) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (dev != NULL && !SDL_ClaimWindowForGPUDevice(dev, win)) {
        printf("claiming the window for the GPU failed: %s\n",
               SDL_GetError());
        SDL_DestroyGPUDevice(dev);
        dev = NULL;
    }

    bool go = settle(dev, win, seconds);
    if (go) {
        print_window("windowed", dev, win);
    }
    if (go && SDL_SetWindowFullscreenMode(win, NULL)
        && SDL_SetWindowFullscreen(win, true)) {
        SDL_SyncWindow(win);
        go = settle(dev, win, seconds);
        if (go) {
            print_window("fullscreen (borderless, the desktop's mode)", dev,
                         win);
        }
        SDL_SetWindowFullscreen(win, false);
        SDL_SyncWindow(win);
        go = go && settle(dev, win, seconds);
        if (go) {
            print_window("windowed again", dev, win);
        }
    } else if (go) {
        printf("\nfullscreen failed: %s\n", SDL_GetError());
    }

    if (dev != NULL) {
        SDL_ReleaseWindowFromGPUDevice(dev, win);
        SDL_DestroyGPUDevice(dev);
    }
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
