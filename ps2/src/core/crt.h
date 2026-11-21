#ifndef PS2_CRT_H
#define PS2_CRT_H

#include "palette.h"
#include "ps2_types.h"

/* (build 105) The CRT simulation, phase 1: the settings the Escape menu's
 * CRT submenu keeps, what the display can do with them, and a CPU
 * reference of the whole chain -- palette index to the display's code
 * values -- that phase 2's shaders must match. docs/crt.md has the model,
 * the numbers and the plan.
 *
 * Nothing here draws to a window. The reference renders into float buffers
 * for tools/crt_ref.c, which writes PNG crops at display resolution and
 * checks the mask's layout and the light by numbers. (build 107) The GPU
 * chain (src/platform/crt_gpu.c, phase 2) draws from the tables below,
 * which this file works out with the model's own functions. */

/* ---- settings ---- */

typedef enum {
    CRT_MASK_OFF = 0,
    CRT_MASK_GRILLE,            /* aperture grille: stripes, every line   */
    CRT_MASK_SLOT,              /* slot mask: stripes cut into slots      */
    CRT_MASK_SHADOW,            /* shadow mask: triads a half-triad apart */
    CRT_MASK_COUNT
} CrtMask;

/* The panel's own subpixels. They cannot be asked for (no OS reports
 * them), so this is a setting. RGB and BGR are stripes, and get masks
 * exact to the subpixel; OTHER (WOLED, QD-OLED, PenTile, Samsung's
 * Diamond Pixel) gets whole-pixel masks. */
typedef enum {
    CRT_SUBPIXELS_RGB = 0,
    CRT_SUBPIXELS_BGR,
    CRT_SUBPIXELS_OTHER,
    CRT_SUBPIXELS_COUNT
} CrtSubpixels;

typedef struct {
    bool          scanlines;
    CrtMask       mask;
    CrtSubpixels  subpixels;
    bool          hdr;
    int           peak_nits;    /* 0: AUTO (crt_peak_auto) */
    int           white_nits;   /* the game's white, HDR only */
} CrtSettings;

#define CRT_WHITE_DEFAULT 200
#define CRT_PEAK_FALLBACK 1000

void crt_settings_default(CrtSettings *s);

/* ---- what the display is ---- */

/* What the platform knows about where the frame goes. The SDL3 platform
 * fills it from the window (platform_display_info); the headless one from
 * PS2_DISPLAY; the route probe from its `display` command. */
typedef struct {
    int   out_w, out_h;         /* the output in pixels, as drawn now      */
    int   full_w, full_h;       /* the display's pixels: fullscreen's size */
    bool  fullscreen;
    bool  window_scaled;        /* windowed, and the desktop scales it    */
    bool  desktop_scaled;       /* the desktop's mode is not the panel's
                                   own, so even fullscreen is resampled   */
    bool  hdr;                  /* SDL says the window can show HDR        */
    float headroom;             /* SDL's HDR headroom, x SDR white (1: none) */
    float sdr_white;            /* SDL's SDR white in its linear space      */
    float sdr_nits;             /* (build 105) that white in nits: Windows's
                                   scRGB 1.0 is 80 nits, so sdr_white x 80;
                                   elsewhere SDL's SDR white is 1.0 and its
                                   nits are not said, taken as BT.2408's 203 */
    bool  no_chain;             /* (build 141c) the GPU chain is not running
                                   (no shaders, no SDL_GPU): the placeholder
                                   draws, flat, and no CRT setting shows */
} CrtDisplay;

/* What a window of the platform's default size on a 1080p desktop looks
 * like: 960x672, windowed, unscaled, no HDR. What the probe assumes until a
 * route says otherwise. */
void crt_display_default(CrtDisplay *d);

/* (build 105) A display described in words, for the headless platform's
 * PS2_DISPLAY and the route probe's `display` command -- every value the
 * menu can show, without the screen that shows it:
 *
 *     3840x2400 fullscreen hdr=6.25/2.5
 *     1920x1080 full=2560x1440 window-scaled
 *
 * The first word is the output in pixels. `full=WxH` is the display's own
 * (fullscreen's) size: the output when `fullscreen`, else 1920x1080 unless
 * given. `window-scaled`: the desktop scales the window. `desktop-scaled`:
 * the desktop is not at the panel's resolution. `hdr=H/S`: HDR, SDL's
 * headroom H and SDR white S (`hdr` alone: 1 and 1), that white S x 80
 * nits as on Windows unless `sdr=N` says N nits (Wayland, macOS: SDL's SDR
 * white is 1.0 there, its nits not said). (build 141c) `no-chain`: the GPU
 * chain is not running, as with no shaders. Starts from crt_display_default;
 * false (and `d` as far as it got) on a word it does not know. */
bool crt_display_parse(const char *text, CrtDisplay *d);

/* Why a setting cannot be had on this display; CRT_OK when it can. */
typedef enum {
    CRT_OK = 0,
    CRT_NEEDS_FULLSCREEN,       /* fullscreen on this display would do  */
    CRT_NEEDS_480P,             /* scanlines: 448 lines, two a line     */
    CRT_NEEDS_1080P,            /* the grille: four display lines a line */
    CRT_NEEDS_1440P,            /* slot and shadow masks: six           */
    CRT_DESKTOP_SCALED,         /* nothing of ours helps: the desktop's
                                   resolution is not the panel's         */
    CRT_NO_HDR,
    CRT_NO_SHADERS,             /* (build 141c) no GPU chain: `make shaders`
                                   writes what it needs                  */
    CRT_WHY_COUNT
} CrtWhy;

const char *crt_why_text(CrtWhy why);

/* The pixel aspect (width / height) of platform.h's AspectMode by its
 * number: 0 SQUARE 1:1, 1 4:3 14:15, 2 TV 32:35. */
double crt_pixel_aspect(int aspect_mode);

/* k: the largest whole multiple of the 224 lines that fits w x h with the
 * 320 pixels at `par` across -- platform_sdl3.c's frame_layout. */
int crt_lines_k(int w, int h, double par);

CrtWhy crt_scanlines_why(const CrtDisplay *d, double par);
CrtWhy crt_mask_why(const CrtDisplay *d, double par, CrtMask mask);
CrtWhy crt_hdr_why(const CrtDisplay *d);

/* PEAK's AUTO: SDL's headroom times its SDR white in nits (sdr_nits),
 * when SDL says the window can show HDR; else 1000. */
int crt_peak_auto(const CrtDisplay *d);

/* ---- the reference chain ---- */

/* The model's constants (docs/crt.md, "The model"). */
#define CRT_GAMMA          2.4      /* the set's EOTF on the DAC's volts */
#define CRT_BEAM_SIGMA_MIN 0.20     /* the beam's height, dark, x line pitch */
#define CRT_BEAM_SIGMA_MAX 0.32     /* ... at full drive                 */
#define CRT_BEAM_ATTACK    0.5      /* width = min + (max-min) * L^attack */
#define CRT_BEAM_H_SIGMA   0.30     /* across, in source pixels          */

/* The set's white is D65, Rec.709's and SMPTE C's (The owner, build 105: the
 * American sets', not the Japanese 9300 K), so the guns need no gains; the
 * plan keeps white_gain so another white would be three numbers. */
#define CRT_D65_X   0.3127
#define CRT_D65_Y   0.3290

/* (build 108) How the last pass writes a colour into its target -- the
 * swapchain's composition:
 *   SRGB   the SDR swapchain (8-bit): sRGB's curve on light relative to
 *          SDR white;
 *   PQ     HDR10_ST2084 (10-bit): the nits into Rec.2020 (BT.2087), then
 *          ST 2084;
 *   SCRGB  HDR_EXTENDED_LINEAR (16-bit float): the nits over the nits of
 *          1.0, Rec.709 primaries, no curve -- 80 nits on Windows (scRGB),
 *          SDR white elsewhere (SDL's SDR white is 1.0 there). */
typedef enum {
    CRT_ENCODE_SRGB = 0,
    CRT_ENCODE_PQ,
    CRT_ENCODE_SCRGB,
    CRT_ENCODE_COUNT
} CrtEncode;

/* One output to render for: the display's pixels, the settings as they
 * will be applied (unavailable ones already switched off by the caller or
 * by crt_plan_resolve), and the mode. */
typedef struct {
    int    out_w, out_h;
    double par;                 /* the pixel aspect across               */
    CrtSettings set;
    bool   hdr_out;             /* in nits, else SDR relative to 1.0      */
    double peak_nits;           /* resolved PEAK (never 0)               */
    /* (build 108) The HDR encode: PQ or SCRGB (crt_plan_resolve makes it
     * SRGB when !hdr_out, and PQ when hdr_out left it SRGB), and scRGB's
     * nits of 1.0 (0: 80). The light does not depend on either. */
    CrtEncode encode;
    double scrgb_nits;

    /* Filled by crt_plan_resolve. */
    int    k;                   /* display lines a scanline              */
    int    pic_x, pic_y;        /* the picture's top left, in pixels     */
    int    pic_w, pic_h;        /* its size: 320 at par across, 224 k    */
    int    triad;               /* the mask's triad, display pixels      */
    double white_gain[3];       /* linear RGB of the set's white, Y = 1:
                                   D65, (1, 1, 1)                        */
    double white_max;           /* the largest of the three              */
    double beam_peak;           /* a white field's brightest row / mean  */
    double mask_peak;           /* the mask's lit weight at full depth   */
    double scan_depth;          /* 0..1                                  */
    double mask_depth;          /* 0..1                                  */
    double scale;               /* level -> output: nits, or SDR 0..1     */
} CrtPlan;

/* Lays the picture out, works out the triad, the white's gains and the
 * depths from PEAK and WHITE (HDR) or full (SDR), and switches off what
 * the output cannot have (scanlines below k 2, the grille below 4, slot
 * and shadow below 6; the native-pixel rule is the caller's). */
void crt_plan_resolve(CrtPlan *p);

/* The mask's weight for each channel at display pixel (x, y): the lit
 * channel of a lit element gets the period's area over its lit area, so
 * every channel averages 1 over a period. At full depth; crt_mask_at
 * applies the plan's depth. `yrel` is y from the picture's top. */
void crt_mask_full(CrtMask mask, CrtSubpixels sub, int triad, int k,
                   int x, int yrel, double w[3]);
void crt_mask_at(const CrtPlan *p, int x, int y, double w[3]);

/* The source frame: 320 x 224 linear drive levels (0..1), three channels,
 * row major -- (build 141a) each pixel's colour, a gun's 8-bit value (the
 * DAC ladder's output while the art is the ROM's), through the set's
 * gamma: crt_colour_linear. */
typedef struct {
    float lin[SCREEN_H][SCREEN_W][3];
} CrtSource;

/* A CRAM word's three levels, through the measured ladder (palette.c) and
 * CRT_GAMMA. */
void crt_cram_linear(Cram c, double out[3]);

/* (build 141a) A gun's 8-bit value through CRT_GAMMA: the drive of a
 * colour-plane channel. crt_cram_linear is this on the ladder's values. */
double crt_value_linear(u8 v);

/* (build 141a) A frame's colour plane (Framebuffer.rgb, 320 x 224) as the
 * model's source. */
void crt_source_colour(CrtSource *s, const Rgb8 *colour);

/* Renders the display pixels [x0, x0+w) x [y0, y0+h) of the output into
 * `out` (w * h * 3 floats, row major): linear light in the display's
 * Rec.709 primaries, in nits when hdr_out, else relative to SDR white. */
void crt_render(const CrtPlan *p, const CrtSource *src, int x0, int y0,
                int w, int h, float *out);

/* ---- (build 107) the chain as tables: phase 2's GPU passes ---- */

/* The shaders (src/shaders/, docs/crt.md "Phase 2") do no modelling of
 * their own: every weight they sum is worked out here, by the functions
 * crt_render uses, and handed up as textures. Two passes, which is the
 * model's sum in the other order -- exact, because a column's weights
 * across do not depend on the line or the level:
 *
 *   down:   at each source column i and display row y, the beams of lines
 *           jmid-3 .. jmid+3 (jmid the line row y is in), each a level
 *           times its beam's share of the row: `beam` by level and by the
 *           row's offset from the line's top;
 *   across: at each display pixel, the down pass's columns lo .. lo+7
 *           times `across` (the source pixel's box through the beam's
 *           spread, over the display column's footprint), times the mask
 *           (`mask`, by x modulo two triads and the row within its line),
 *           times `gain`, then the encode.
 *
 * The arrays are laid out as the textures are. */
#define CRT_LEVELS       8      /* a gun's DAC levels (palette.c's ladder) */
#define CRT_BEAM_ROWS    256    /* (build 141a) the beam table's rows: a
                                   gun's 8-bit value, the colour plane's */
#define CRT_ACROSS_TAPS  8      /* source columns a display pixel sums     */
#define CRT_LINE_REACH   3      /* lines either side a display row sums    */

/* (build 108) The encode the across and flat passes do, as numbers from
 * here: which (CrtEncode), the matrix into Rec.2020 (PQ; BT.2087's, from
 * crt_709_to_2020), the factor from the pass's light to the encode's input
 * (PQ: 1/10000, the nits to ST 2084's 0..1; SCRGB: 1 / the nits of 1.0;
 * SRGB: 1), and the flat picture's white (HDR: its sRGB-decoded colour
 * times this many nits; SDR: 1). */
typedef struct {
    CrtEncode encode;
    float     to2020[3][3];
    float     scale;
    float     white;
} CrtEncodeParams;

/* The encode for `e`: `white_nits` the flat picture's white in HDR (the
 * HDR page's WHITE), `scrgb_nits` the nits of scRGB's 1.0 (0: 80). */
void crt_encode_params(CrtEncode e, double white_nits, double scrgb_nits,
                       CrtEncodeParams *out);

/* The across pass's encode in float, as the shaders do it: linear light
 * (SDR relative to SDR white, HDR in nits) to the target's value -- an
 * sRGB code 0..1, a PQ signal 0..1, or scRGB's linear. */
void crt_encode_f(const CrtEncodeParams *e, const float lin[3],
                  float out[3]);

/* The flat pass's colour (the ladder's 8-bit value, 0..1, blended as the
 * placeholder blended it) to the target's value: SRGB as it is; HDR its
 * sRGB decode times `white` nits, then crt_encode_f. */
void crt_encode_flat_f(const CrtEncodeParams *e, const float code[3],
                       float out[3]);

/* ST 2084 in float, as the shaders do it: Y (nits / 10000, clamped 0..1)
 * to the signal. */
float crt_pq_encode_f(float y);

/* The model's encode in double, for a pixel of crt_render's output:
 * the value crt_encode_f should come within a code of. */
void crt_encode_model(const CrtPlan *p, const double lin[3], double out[3]);

typedef struct {
    int    k;
    int    pic_y;
    /* (build 108) The across pass's row within its line is (y +
     * mask_phase) % k: mask_phase is -pic_y wrapped into 0..k-1, so the
     * dividend is never negative. Build 107 took (y - pic_y) % k and added
     * k when it came out negative; above the picture's top that is a
     * remainder of a negative number, which HLSL leaves undefined, and
     * The owner's NVIDIA GPU (Vulkan) took it as an unsigned remainder --
     * the slot and shadow masks' rows shifted in the beam's tail over
     * the picture (build 107's six FAILs). */
    int    mask_phase;
    int    beam_w;              /* 7 k: offsets -3k .. 4k-1 from a line's top */
    float *beam;                /* [CRT_BEAM_ROWS][beam_w]: R32 float, by
                                   the gun's 8-bit value (build 141a; by
                                   DAC level, 8 rows, before)              */
    int    across_w;            /* the output's width                        */
    float *across;              /* [3][across_w][4]: RGBA32 float; rows 0-1
                                   taps 0-3 and 4-7, row 2 (lo, 0, 0, 0)     */
    int    mask_w, mask_h;      /* two triads by k                           */
    float *mask;                /* [mask_h][mask_w][4]: RGBA32 float, the
                                   plan's depth applied, alpha 1             */
    float  gain[3];             /* scale x white_gain                        */
    CrtEncodeParams enc;        /* (build 108) the across pass's encode      */
} CrtTables;

/* A gun's drive at DAC level 0..7: the ladder and CRT_GAMMA, as
 * crt_cram_linear gives it. */
double crt_level_linear(int level);

/* The tables for a resolved plan. False (nothing allocated) if the plan's
 * footprint needs more than CRT_ACROSS_TAPS columns, which no k >= 1 does. */
bool crt_tables_build(const CrtPlan *p, CrtTables *t);
void crt_tables_free(CrtTables *t);

/* The two passes on the CPU, in float as the shaders do them -- a mirror to
 * check the tables and the passes against crt_render here, where no GPU
 * is. (build 141a) `colour` is the 320 x 224 colour plane, each gun's
 * 8-bit value the beam table's row (until 141a, the index plane and each
 * index's three DAC levels). Output as crt_render's: linear, w * h * 3. */
void crt_tables_render(const CrtTables *t, const Rgb8 *colour, int x0,
                       int y0, int w, int h, float *out);

/* (build 108) The across pass's row within its line for display row y
 * (y >= 0): the shader's arithmetic, (y + mask_phase) % k. `dividend`, if
 * not NULL, gets the remainder's dividend, which crt_ref sim checks is
 * never negative. */
int crt_tables_row(const CrtTables *t, int y, int *dividend);

/* The encode the across pass does in SDR, in float: sRGB's curve, clamped. */
float crt_srgb_encode_f(float linear);

/* (build 108) sRGB's curve undone, in float, 0..1 in and out. */
float crt_srgb_decode_f(float code);

/* (build 107) Where the flat picture goes when the CRT is off -- the
 * placeholder's layout (platform_sdl3.c): whole multiples of 224 up and
 * down, the pixel aspect across rounded to a pixel, centred; the frame
 * nearest to step_w x step_h (the next whole multiple of 320 across), then
 * linear down to w x h. A window too small for one multiple gets the frame
 * fitted, step 320 x 224. */
typedef struct {
    int x, y, w, h;
    int step_w, step_h;
} CrtSharp;

void crt_sharp_layout(int out_w, int out_h, double par, CrtSharp *l);

/* ---- the encodes ---- */

/* sRGB's OETF, 0..1 in and out. */
double crt_srgb_encode(double linear);

/* SMPTE ST 2084 (PQ): nits in, the signal 0..1 out. */
double crt_pq_encode(double nits);
double crt_pq_decode(double signal);

/* Rec.709 linear to Rec.2020 linear (BT.2087). */
void crt_709_to_2020(const double in[3], double out[3]);

/* Rec.709 linear RGB (D65) to CIE xy. */
void crt_709_to_xy(const double rgb[3], double *x, double *y);

#endif /* PS2_CRT_H */
