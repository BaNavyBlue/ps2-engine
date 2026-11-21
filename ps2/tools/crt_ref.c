/* (build 105) The CRT simulation's CPU reference, run: src/core/crt.c
 * through a route's indexed PNG into crops at display resolution, and
 * the checks the model has to pass by numbers. docs/crt.md, "The
 * reference". Phase 2's shaders must match what this writes.
 *
 *   crt_ref check [OUT]
 *       Every check; a report on stdout (and OUT/checks.txt). Exits 1 if
 *       any fails. `make crt_check` runs it.
 *   crt_ref crop IN.png OUT NAME SX SY SW SH
 *       The source rectangle SX,SY SWxSH (320x224 pixels) of a route PNG,
 *       rendered for each display (1920x1080, 2560x1440, 3840x2160,
 *       3840x2400, fullscreen, TV aspect) with the settings docs/crt.md
 *       lists, as OUT/NAME_<display>_<settings>.png: 8-bit sRGB for SDR,
 *       16-bit Rec.2020 PQ with a cICP chunk for HDR (a browser on an HDR
 *       screen shows it as HDR).
 *
 * A route PNG is indexed: its PLTE is the frame's colours through the DAC
 * ladder, which cram_from_rgb8 turns back into CRAM exactly, so the chain
 * starts from the same CRAM words the game had. (build 141a) The chain
 * takes the frame's colour plane: each pixel's colour, read from the PNG
 * through its PLTE (png_colour_plane), as the game's colour plane holds
 * it. */

#include "../src/core/crt.h"
#include "../src/core/palette.h"
#include "../src/core/png.h"
#include "../src/core/pngwrite.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* (build 113) Windows (MSYS2 UCRT64): mkdir takes the path alone. */
#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#else
#define make_dir(path) mkdir(path, 0755)
#endif

typedef struct {
    const char *name;
    int         w, h;
} Screen;

static const Screen SCREENS[] = {
    { "1080p", 1920, 1080 },
    { "1440p", 2560, 1440 },
    { "4k", 3840, 2160 },
    { "3840x2400", 3840, 2400 },
};
#define SCREEN_COUNT ((int)(sizeof SCREENS / sizeof SCREENS[0]))

static const char *const MASK_KEY[CRT_MASK_COUNT] = {
    "flat", "grille", "slot", "shadow"
};
static const char *const SUB_KEY[CRT_SUBPIXELS_COUNT] = {
    "rgb", "bgr", "other"
};

static FILE *g_report;
static int   g_failed, g_passed;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (g_report != NULL) {
        va_start(ap, fmt);
        vfprintf(g_report, fmt, ap);
        va_end(ap);
    }
}

/* (build 142) A route PNG that is not there says which route makes it:
 * route_out/<route>/... is written by routes/<route>.route, which `make
 * route-<route>` runs. ` (make route-<route> makes it)`, or "" for a path
 * that is not a route's. */
static const char *made_by(const char *png)
{
    static char why[160];
    why[0] = '\0';
    if (png != NULL && strncmp(png, "route_out/", 10) == 0) {
        const char *name = png + 10;
        const char *end  = strchr(name, '/');
        if (end != NULL && end > name) {
            snprintf(why, sizeof why, " (make route-%.*s makes it, from "
                     "the tree's top)", (int)(end - name), name);
        }
    }
    return why;
}

static void check(bool ok, const char *fmt, ...)
{
    char    line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    say("%s  %s\n", ok ? "ok  " : "FAIL", line);
    if (ok) {
        g_passed++;
    } else {
        g_failed++;
    }
}

static void plan_for(CrtPlan *p, int w, int h, bool scan, CrtMask mask,
                     CrtSubpixels sub, bool hdr, int peak, int white)
{
    memset(p, 0, sizeof *p);
    p->out_w             = w;
    p->out_h             = h;
    p->par               = crt_pixel_aspect(2);
    crt_settings_default(&p->set);
    p->set.scanlines     = scan;
    p->set.mask          = mask;
    p->set.subpixels     = sub;
    p->set.hdr           = hdr;
    p->set.peak_nits     = peak;
    p->set.white_nits    = white;
    p->hdr_out           = hdr;
    p->peak_nits         = peak;
    crt_plan_resolve(p);
}

static CrtSource *source_flat(float level)
{
    CrtSource *s = malloc(sizeof *s);
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            for (int c = 0; c < 3; c++) {
                s->lin[y][x][c] = level;
            }
        }
    }
    return s;
}

/* (build 141a) A route PNG's colour plane: each pixel's PLTE colour,
 * through CRAM and back (the ladder's 8-bit values, as the game's colour
 * plane has them). NULL if it cannot be read or is not 320x224; free()
 * the result. (build 148) Read as RGBA, so a route shot with a picture
 * in it -- RGB since 146a, the battle floor's since 148 -- reads as an
 * indexed one does: each pixel's colour through CRAM and back. */
static Rgb8 *png_colour_plane(const char *path)
{
    PngRgba img;
    if (!png_load_rgba(&img, path)) {
        return NULL;
    }
    if (img.width != SCREEN_W || img.height != SCREEN_H) {
        fprintf(stderr, "%s: not %dx%d\n", path, SCREEN_W, SCREEN_H);
        png_rgba_free(&img);
        return NULL;
    }
    Rgb8 *colour = malloc(sizeof(Rgb8) * SCREEN_W * SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        const u8 *p = img.rgba + (size_t)i * 4;
        colour[i] = cram_to_rgb8(cram_from_rgb8(p[0], p[1], p[2]));
    }
    png_rgba_free(&img);
    return colour;
}

static CrtSource *source_png(const char *path)
{
    Rgb8 *colour = png_colour_plane(path);
    if (colour == NULL) {
        return NULL;
    }
    CrtSource *s = malloc(sizeof *s);
    crt_source_colour(s, colour);
    free(colour);
    return s;
}

/* ---- the checks ---- */

static void check_encodes(void)
{
    say("\n## The encodes\n\n");
    static const struct {
        double nits;
        int    code;
    } PQ[] = {
        { 0.0, 0 }, { 100.0, 520 }, { 1000.0, 769 }, { 10000.0, 1023 },
    };
    for (size_t i = 0; i < sizeof PQ / sizeof PQ[0]; i++) {
        int code = (int)lround(crt_pq_encode(PQ[i].nits) * 1023.0);
        check(code == PQ[i].code, "PQ %.0f nits -> 10-bit %d (want %d)",
              PQ[i].nits, code, PQ[i].code);
    }
    double worst = 0.0;
    for (double n = 0.01; n <= 10000.0; n *= 1.3) {
        double back = crt_pq_decode(crt_pq_encode(n));
        double err  = fabs(back - n) / n;
        worst = (err > worst) ? err : worst;
    }
    check(worst < 1e-6, "PQ decode(encode(n)) within %.1e of n, 0.01-10000 "
                        "nits", worst);
    check(fabs(crt_srgb_encode(1.0) - 1.0) < 1e-12
              && fabs(crt_srgb_encode(0.18) - 0.46135) < 1e-4
              && fabs(crt_srgb_encode(0.0031308) - 0.040449) < 1e-5,
          "sRGB: 1 -> 1, 0.18 -> %.5f, 0.0031308 -> %.6f",
          crt_srgb_encode(0.18), crt_srgb_encode(0.0031308));
    double white[3] = { 1.0, 1.0, 1.0 }, w2020[3];
    crt_709_to_2020(white, w2020);
    check(fabs(w2020[0] - 1.0) < 1e-3 && fabs(w2020[1] - 1.0) < 1e-3
              && fabs(w2020[2] - 1.0) < 1e-3,
          "709 -> 2020 keeps white: %.4f %.4f %.4f", w2020[0], w2020[1],
          w2020[2]);
}

/* Lit subpixels along one row of the mask at full depth: their physical
 * positions (3 a pixel; RGB puts R at 0, BGR puts B at 0). */
static int lit_positions(CrtMask mask, CrtSubpixels sub, int triad, int k,
                         int yrel, int pixels, int *pos)
{
    int n = 0;
    for (int x = 0; x < pixels; x++) {
        double w[3];
        crt_mask_full(mask, sub, triad, k, x, yrel, w);
        for (int c = 0; c < 3; c++) {
            if (w[c] > 0.0) {
                int at = (sub == CRT_SUBPIXELS_BGR) ? 2 - c : c;
                pos[n++] = 3 * x + at;
            }
        }
    }
    return n;
}

static void check_mask_layout(void)
{
    say("\n## The mask's layout\n\n");
    static const int KS[] = { 4, 6, 9, 10 };
    int failed_before = g_failed;

    /* Every channel averages 1 over a period, every mask, every k. */
    for (int m = CRT_MASK_GRILLE; m < CRT_MASK_COUNT; m++) {
        for (int sub = 0; sub < CRT_SUBPIXELS_COUNT; sub++) {
            for (size_t ki = 0; ki < sizeof KS / sizeof KS[0]; ki++) {
                int k = KS[ki];
                for (int triad = 3; triad <= 4; triad++) {
                    if (m == CRT_MASK_SHADOW && triad == 3) {
                        continue;
                    }
                    double sum[3] = { 0.0, 0.0, 0.0 };
                    int    area   = 0;
                    for (int y = 0; y < k; y++) {
                        for (int x = 0; x < 2 * triad; x++) {
                            double w[3];
                            crt_mask_full((CrtMask)m, (CrtSubpixels)sub,
                                          triad, k, x, y, w);
                            for (int c = 0; c < 3; c++) {
                                sum[c] += w[c];
                            }
                            area++;
                        }
                    }
                    bool ok = true;
                    for (int c = 0; c < 3; c++) {
                        ok = ok && fabs(sum[c] / area - 1.0) < 1e-12;
                    }
                    if (!ok) {
                        check(false, "%s %s triad %d k %d: channel means "
                              "%.4f %.4f %.4f", MASK_KEY[m], SUB_KEY[sub],
                              triad, k, sum[0] / area, sum[1] / area,
                              sum[2] / area);
                    }
                }
            }
        }
    }
    check(g_failed == failed_before, "every mask, subpixel order, triad and k from 4 to "
                         "10: each channel averages exactly 1 over a period");

    /* Stripes, four-pixel triad: lit subpixels every 4, R G B in turn. */
    for (int sub = CRT_SUBPIXELS_RGB; sub <= CRT_SUBPIXELS_BGR; sub++) {
        int  pos[64];
        int  n  = lit_positions(CRT_MASK_GRILLE, (CrtSubpixels)sub, 4, 10, 0,
                                16, pos);
        bool ok = (n == 12);
        for (int i = 1; i < n; i++) {
            ok = ok && (pos[i] - pos[i - 1] == 4);
        }
        check(ok, "grille, %s stripes, 4-pixel triad: %d lit subpixels in "
                  "16 pixels, every 4 (%d %d %d %d ...)", SUB_KEY[sub], n,
              pos[0], pos[1], pos[2], pos[3]);
    }
    /* Three-pixel triad: bunched 2 apart, a gap of 5 for the grille's
     * dark stripe. */
    for (int sub = CRT_SUBPIXELS_RGB; sub <= CRT_SUBPIXELS_BGR; sub++) {
        int  pos[64];
        int  n  = lit_positions(CRT_MASK_GRILLE, (CrtSubpixels)sub, 3, 6, 0,
                                12, pos);
        bool ok = (n == 12);
        for (int i = 1; i < n; i++) {
            int want = (i % 3 == 0) ? 5 : 2;
            ok = ok && (pos[i] - pos[i - 1] == want);
        }
        check(ok, "grille, %s stripes, 3-pixel triad: gaps 2, 2, 5 (%d %d "
                  "%d %d ...)", SUB_KEY[sub], pos[0], pos[1], pos[2], pos[3]);
    }
    /* OTHER: whole pixels, one colour each, R G B (dark). */
    {
        bool ok = true;
        for (int x = 0; x < 8; x++) {
            double w[3];
            crt_mask_full(CRT_MASK_GRILLE, CRT_SUBPIXELS_OTHER, 4, 10, x, 0,
                          w);
            int lit = (w[0] > 0.0) + (w[1] > 0.0) + (w[2] > 0.0);
            int ch  = x % 4;
            ok = ok && ((ch == 3) ? lit == 0 : (lit == 1 && w[ch] == 4.0));
        }
        check(ok, "grille, OTHER: whole pixels R, G, B, dark");
    }
    /* Slot: a one-row bridge a line pitch, the second column's half a
     * pitch below the first's. */
    for (size_t ki = 0; ki < sizeof KS / sizeof KS[0]; ki++) {
        int k = KS[ki];
        int dark_a = -1, dark_b = -1, darks = 0;
        for (int y = 0; y < k; y++) {
            double wa[3], wb[3];
            crt_mask_full(CRT_MASK_SLOT, CRT_SUBPIXELS_RGB, 4, k, 0, y, wa);
            crt_mask_full(CRT_MASK_SLOT, CRT_SUBPIXELS_RGB, 4, k, 4, y, wb);
            if (wa[0] == 0.0) {
                dark_a = y;
                darks++;
            }
            if (wb[0] == 0.0) {
                dark_b = y;
                darks++;
            }
        }
        check(darks == 2 && dark_a == k / 4 && dark_b == k / 4 + k / 2,
              "slot, k %d: bridges at rows %d and %d of the line pitch", k,
              dark_a, dark_b);
    }
    /* Shadow: the lower half of each pitch half a triad over. */
    {
        int top[16], bot[16];
        int nt = lit_positions(CRT_MASK_SHADOW, CRT_SUBPIXELS_RGB, 4, 10, 0,
                               4, top);
        int nb = lit_positions(CRT_MASK_SHADOW, CRT_SUBPIXELS_RGB, 4, 10, 5,
                               4, bot);
        bool ok = (nt == 3 && nb == 3);
        for (int i = 1; ok && i < 3; i++) {
            ok = (top[i] - top[i - 1] == 4) && (bot[i] - bot[i - 1] == 4);
        }
        check(ok && bot[0] == 2 && top[0] == 0,
              "shadow, k 10, RGB: upper half lit at %d %d %d, lower at %d "
              "%d %d (half a triad, 6 subpixels, over)", top[0], top[1],
              top[2], bot[0], bot[1], bot[2]);
    }
}

/* A white field's region of whole periods in the middle of the picture:
 * its mean luminance, its channel means and its brightest channel. */
static void white_field(const CrtPlan *p, const CrtSource *white,
                        double *mean_y, double mean[3], double *max)
{
    int x0 = p->pic_x + p->pic_w / 2;
    x0 -= x0 % (2 * p->triad);
    int y0 = p->pic_y + 100 * p->k;
    int w  = 12 * p->triad;
    int h  = 4 * p->k;
    float *out = malloc(sizeof(float) * 3 * (size_t)w * (size_t)h);
    crt_render(p, white, x0, y0, w, h, out);
    double sum[3] = { 0.0, 0.0, 0.0 };
    *max = 0.0;
    for (int i = 0; i < w * h; i++) {
        for (int c = 0; c < 3; c++) {
            sum[c] += out[i * 3 + c];
            *max = (out[i * 3 + c] > *max) ? out[i * 3 + c] : *max;
        }
    }
    for (int c = 0; c < 3; c++) {
        mean[c] = sum[c] / (w * h);
    }
    *mean_y = 0.2126729 * mean[0] + 0.7151522 * mean[1]
            + 0.0721750 * mean[2];
    free(out);
}

static void check_light(void)
{
    CrtSource *white = source_flat(1.0f);

    say("\n## The light (HDR: a white field averages WHITE, nothing passes "
        "PEAK)\n\n");
    say("| display | k | triad | mask | peak | white | scan depth | mask "
        "depth | mean nits | brightest |\n|---|---|---|---|---|---|---|---|"
        "---|---|\n");
    int failed_before = g_failed;
    static const int PEAKS[] = { 400, 600, 1000, 2000 };
    for (int si = 0; si < SCREEN_COUNT; si++) {
        for (int m = 0; m < CRT_MASK_COUNT; m++) {
            for (int sub = 0; sub < CRT_SUBPIXELS_COUNT; sub++) {
                for (size_t pi = 0; pi < sizeof PEAKS / sizeof PEAKS[0];
                     pi++) {
                    CrtPlan p;
                    plan_for(&p, SCREENS[si].w, SCREENS[si].h, true,
                             (CrtMask)m, (CrtSubpixels)sub, true,
                             PEAKS[pi], CRT_WHITE_DEFAULT);
                    if (p.set.mask != (CrtMask)m) {
                        continue;       /* not at this k */
                    }
                    double mean_y, mean[3], max;
                    white_field(&p, white, &mean_y, mean, &max);
                    bool ok = fabs(mean_y / CRT_WHITE_DEFAULT - 1.0) < 0.01
                           && max <= PEAKS[pi] * (1.0 + 1e-9);
                    if (!ok) {
                        check(false, "%s %s %s peak %d: mean %.2f, "
                              "brightest %.2f", SCREENS[si].name,
                              MASK_KEY[m], SUB_KEY[sub], PEAKS[pi], mean_y,
                              max);
                    }
                    if (sub == CRT_SUBPIXELS_RGB) {
                        say("| %s | %d | %d | %s | %d | %d | %.2f | %.2f | "
                            "%.2f | %.1f |\n", SCREENS[si].name, p.k,
                            p.triad, MASK_KEY[m], PEAKS[pi],
                            CRT_WHITE_DEFAULT, p.scan_depth, p.mask_depth,
                            mean_y, max);
                    }
                }
            }
        }
    }
    say("\n");
    check(g_failed == failed_before,
          "every display, mask, subpixel order and PEAK 400-2000 at WHITE "
          "200: the white field's mean within 1%% of 200 nits, no channel "
          "over PEAK");

    /* The white's chromaticity, through the whole chain. */
    {
        CrtPlan p;
        plan_for(&p, 3840, 2400, true, CRT_MASK_SHADOW, CRT_SUBPIXELS_OTHER,
                 true, 1000, 200);
        double mean_y, mean[3], max, x, y;
        white_field(&p, white, &mean_y, mean, &max);
        crt_709_to_xy(mean, &x, &y);
        check(fabs(x - CRT_D65_X) < 0.0005 && fabs(y - CRT_D65_Y) < 0.0005,
              "the white field's chromaticity x %.4f y %.4f (D65 %.4f "
              "%.4f)", x, y, CRT_D65_X, CRT_D65_Y);
    }

    say("\n## SDR (no headroom: the full mask and scanlines, the brightest "
        "subpixel at 1.0)\n\n");
    say("| display | mask | mean (of SDR white) | brightest |\n"
        "|---|---|---|---|\n");
    failed_before = g_failed;
    for (int si = 0; si < SCREEN_COUNT; si++) {
        for (int m = 0; m < CRT_MASK_COUNT; m++) {
            CrtPlan p;
            plan_for(&p, SCREENS[si].w, SCREENS[si].h, true, (CrtMask)m,
                     CRT_SUBPIXELS_RGB, false, 1000, 200);
            if (p.set.mask != (CrtMask)m) {
                continue;
            }
            double mean_y, mean[3], max;
            white_field(&p, white, &mean_y, mean, &max);
            double want = 1.0 / (p.beam_peak * p.mask_peak);
            if (fabs(mean_y / want - 1.0) > 0.01 || max > 1.0 + 1e-9) {
                check(false, "SDR %s %s: mean %.4f (want %.4f), brightest "
                      "%.4f", SCREENS[si].name, MASK_KEY[m], mean_y, want,
                      max);
            }
            say("| %s | %s | %.3f | %.3f |\n", SCREENS[si].name,
                MASK_KEY[m], mean_y, max);
        }
    }
    say("\n");
    check(g_failed == failed_before,
          "SDR: nothing over 1.0, the white field's mean 1 / (beam peak x "
          "mask peak) within 1%%");
    free(white);
}

static void check_availability(void)
{
    say("\n## What each display can have, fullscreen\n\n");
    static const struct {
        int  w, h, k;
        bool grille, slot;
    } WANT[] = {
        { 1920, 1080, 4, true, false },
        { 2560, 1440, 6, true, true },
        { 3840, 2160, 9, true, true },
        { 3840, 2400, 10, true, true },
    };
    double par = crt_pixel_aspect(2);
    for (size_t i = 0; i < sizeof WANT / sizeof WANT[0]; i++) {
        CrtDisplay d;
        char       words[64];
        snprintf(words, sizeof words, "%dx%d fullscreen", WANT[i].w,
                 WANT[i].h);
        crt_display_parse(words, &d);
        int  k  = crt_lines_k(d.out_w, d.out_h, par);
        bool gr = crt_mask_why(&d, par, CRT_MASK_GRILLE) == CRT_OK;
        bool sl = crt_mask_why(&d, par, CRT_MASK_SLOT) == CRT_OK;
        check(k == WANT[i].k && gr == WANT[i].grille && sl == WANT[i].slot,
              "%s: k %d, scanlines %s, grille %s, slot and shadow %s", words,
              k, crt_scanlines_why(&d, par) == CRT_OK ? "yes" : "no",
              gr ? "yes" : crt_why_text(crt_mask_why(&d, par,
                                                     CRT_MASK_GRILLE)),
              sl ? "yes" : crt_why_text(crt_mask_why(&d, par,
                                                     CRT_MASK_SLOT)));
    }
}

static void check_image(void)
{
    say("\n## A frame of the game\n\n");
    CrtSource *src = source_png("route_out/new_game/01_title.png");
    if (src == NULL) {
        check(false, "route_out/new_game/01_title.png does not load%s",
              made_by("route_out/new_game/01_title.png"));
        return;
    }
    static const int PEAKS[] = { 400, 1000 };
    for (size_t pi = 0; pi < 2; pi++) {
        CrtPlan p;
        plan_for(&p, 3840, 2400, true, CRT_MASK_SHADOW, CRT_SUBPIXELS_OTHER,
                 true, PEAKS[pi], 200);
        int    w   = 64 * p.pic_w / SCREEN_W;
        int    h   = 40 * p.k;
        int    x0  = p.pic_x + 124 * p.pic_w / SCREEN_W;
        int    y0  = p.pic_y + 126 * p.k;
        float *out = malloc(sizeof(float) * 3 * (size_t)w * (size_t)h);
        crt_render(&p, src, x0, y0, w, h, out);
        double max = 0.0;
        for (int i = 0; i < w * h * 3; i++) {
            max = (out[i] > max) ? out[i] : max;
        }
        check(max <= PEAKS[pi] * (1.0 + 1e-9), "the title's logo, "
              "3840x2400, shadow mask, PEAK %d: brightest %.1f nits",
              PEAKS[pi], max);
        free(out);
    }
    free(src);
}

static int run_checks(const char *out)
{
    if (out != NULL) {
        make_dir(out);
        char path[512];
        snprintf(path, sizeof path, "%s/checks.txt", out);
        /* (build 113) "wb", as sim_check.txt: the same bytes on every OS
         * (Windows's text mode writes "\r\n") */
        g_report = fopen(path, "wb");
    }
    say("# crt_ref check (build 105)\n\nThe model's constants: gamma %.1f, "
        "the beam %.2f-%.2f of a line pitch high (attack %.1f), %.2f of a "
        "pixel across.\n", CRT_GAMMA, CRT_BEAM_SIGMA_MIN, CRT_BEAM_SIGMA_MAX,
        CRT_BEAM_ATTACK, CRT_BEAM_H_SIGMA);
    check_encodes();
    check_mask_layout();
    check_availability();
    check_light();
    check_image();
    say("\n%d checks passed, %d failed\n", g_passed, g_failed);
    if (g_report != NULL) {
        fclose(g_report);
    }
    return (g_failed == 0) ? 0 : 1;
}

/* ---- the crops ---- */

static void write_crop(const CrtPlan *p, const CrtSource *src,
                       const char *out, const char *name, int sx, int sy,
                       int sw, int sh, const char *tag)
{
    int x0 = p->pic_x + (int)floor(sx * (double)p->pic_w / SCREEN_W);
    int x1 = p->pic_x + (int)ceil((sx + sw) * (double)p->pic_w / SCREEN_W);
    int y0 = p->pic_y + sy * p->k;
    int w  = x1 - x0;
    int h  = sh * p->k;
    float *lin = malloc(sizeof(float) * 3 * (size_t)w * (size_t)h);
    crt_render(p, src, x0, y0, w, h, lin);

    int bytes = p->hdr_out ? 2 : 1;
    u8 *px    = malloc((size_t)w * (size_t)h * 3 * (size_t)bytes);
    for (int i = 0; i < w * h; i++) {
        double rgb[3] = { lin[i * 3], lin[i * 3 + 1], lin[i * 3 + 2] };
        if (p->hdr_out) {
            double r2020[3];
            crt_709_to_2020(rgb, r2020);
            for (int c = 0; c < 3; c++) {
                double v = crt_pq_encode(r2020[c] < 0.0 ? 0.0 : r2020[c]);
                unsigned q = (unsigned)lround(v * 65535.0);
                px[(i * 3 + c) * 2]     = (u8)(q >> 8);
                px[(i * 3 + c) * 2 + 1] = (u8)q;
            }
        } else {
            for (int c = 0; c < 3; c++) {
                px[i * 3 + c] = (u8)lround(crt_srgb_encode(rgb[c]) * 255.0);
            }
        }
    }
    char path[640];
    snprintf(path, sizeof path, "%s/%s_%s.png", out, name, tag);
    static const PngCicp PQ2020 = { 9, 16, 0, 1 };
    if (!png_write(path, w, h, p->hdr_out ? 16 : 8, 2, px,
                   (size_t)w * 3 * (size_t)bytes, NULL, 0,
                   p->hdr_out ? &PQ2020 : NULL)) {
        fprintf(stderr, "cannot write %s\n", path);
    } else {
        printf("%s  %dx%d  k %d triad %d  scan %.2f mask %.2f\n", path, w, h,
               p->k, p->triad, p->scan_depth, p->mask_depth);
    }
    free(px);
    free(lin);
}

static int run_crops(const char *in, const char *out, const char *name,
                     int sx, int sy, int sw, int sh)
{
    CrtSource *src = source_png(in);
    if (src == NULL) {
        return 1;
    }
    make_dir(out);

    /* Per display: flat, scanlines, every mask it can have in SDR (RGB
     * stripes, and OTHER at 3840x2400: the ProArt P16's OLED), and HDR at
     * the two screens that have it (the P16, OTHER; the PA32UCX, a 4K RGB
     * LCD). */
    for (int si = 0; si < SCREEN_COUNT; si++) {
        const Screen *s = &SCREENS[si];
        bool p16 = (s->h == 2400);
        CrtSubpixels sub = p16 ? CRT_SUBPIXELS_OTHER : CRT_SUBPIXELS_RGB;
        for (int m = 0; m < CRT_MASK_COUNT; m++) {
            for (int scan = (m == 0) ? 0 : 1; scan <= 1; scan++) {
                CrtPlan p;
                plan_for(&p, s->w, s->h, scan != 0, (CrtMask)m, sub, false,
                         1000, 200);
                if (p.set.mask != (CrtMask)m) {
                    continue;
                }
                char tag[64];
                snprintf(tag, sizeof tag, "%s_%s%s_%s_sdr", s->name,
                         (m == 0 && scan) ? "scanlines" : MASK_KEY[m],
                         "", SUB_KEY[sub]);
                write_crop(&p, src, out, name, sx, sy, sw, sh, tag);
            }
        }
        if (p16 || s->h == 2160) {
            CrtPlan p;
            CrtMask m = p16 ? CRT_MASK_SHADOW : CRT_MASK_GRILLE;
            plan_for(&p, s->w, s->h, true, m, sub, true, 1000, 200);
            char tag[64];
            snprintf(tag, sizeof tag, "%s_%s_%s_pq1000", s->name,
                     MASK_KEY[m], SUB_KEY[sub]);
            write_crop(&p, src, out, name, sx, sy, sw, sh, tag);
        }
    }
    free(src);
    return 0;
}

/* ---- (build 107) the GPU passes' mirror against the model ---- */

/* A route PNG as the GPU takes it: (build 141a) the colour plane (until
 * 141a, the index plane and each index's three DAC levels). */
typedef struct {
    Rgb8 *colour;
} SimSource;

static SimSource *sim_source_png(const char *path)
{
    Rgb8 *colour = png_colour_plane(path);
    if (colour == NULL) {
        return NULL;
    }
    SimSource *s = calloc(1, sizeof *s);
    s->colour = colour;
    return s;
}

/* (build 141a) A colour plane with every 8-bit value in each channel
 * inside the 32x20 crop at the top left: pixel n of it (row by row) is
 * red n, green 7n, blue 255 - n, modulo 256 -- most of them colours the
 * DAC ladder cannot make, which full-colour art can. */
static SimSource *sim_source_values(void)
{
    SimSource *s = calloc(1, sizeof *s);
    s->colour = malloc(sizeof(Rgb8) * SCREEN_W * SCREEN_H);
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            int  n = (y % 20) * 32 + (x % 32);
            Rgb8 *c = &s->colour[y * SCREEN_W + x];
            c->r = (u8)(n & 0xFF);
            c->g = (u8)((n * 7) & 0xFF);
            c->b = (u8)((255 - n) & 0xFF);
        }
    }
    return s;
}

static void sim_source_free(SimSource *s)
{
    if (s != NULL) {
        free(s->colour);
        free(s);
    }
}

/* One crop as crt_ref's `crop` writes it (write_crop's rectangle and
 * encode), against the same rectangle through the tables and the float
 * passes: the largest difference in 8-bit codes, and how many differ. */
static void sim_crop(const CrtPlan *p, const CrtSource *src,
                     const SimSource *ss, int sx, int sy, int sw, int sh,
                     const char *what, int *worst_all, long *differ_all,
                     long *codes_all)
{
    int x0 = p->pic_x + (int)floor(sx * (double)p->pic_w / SCREEN_W);
    int x1 = p->pic_x + (int)ceil((sx + sw) * (double)p->pic_w / SCREEN_W);
    int y0 = p->pic_y + sy * p->k;
    int w  = x1 - x0;
    int h  = sh * p->k;
    float *ref = malloc(sizeof(float) * 3 * (size_t)w * (size_t)h);
    float *sim = malloc(sizeof(float) * 3 * (size_t)w * (size_t)h);
    crt_render(p, src, x0, y0, w, h, ref);
    CrtTables t;
    if (!crt_tables_build(p, &t)) {
        check(false, "%s: the tables could not be built", what);
        free(ref);
        free(sim);
        return;
    }
    crt_tables_render(&t, ss->colour, x0, y0, w, h, sim);
    crt_tables_free(&t);

    int  worst  = 0;
    long differ = 0;
    for (long i = 0; i < 3L * w * h; i++) {
        int a = (int)lround(crt_srgb_encode(ref[i]) * 255.0);
        int b = (int)(crt_srgb_encode_f(sim[i]) * 255.0f + 0.5f);
        int d = abs(a - b);
        worst = (d > worst) ? d : worst;
        differ += (d > 0) ? 1 : 0;
    }
    say("  %-44s %4dx%-4d worst %d, %ld of %ld codes differ\n", what, w, h,
        worst, differ, 3L * w * h);
    *worst_all   = (worst > *worst_all) ? worst : *worst_all;
    *differ_all += differ;
    *codes_all  += 3L * w * h;
    free(ref);
    free(sim);
}

/* The mask table against crt_mask_at at every pixel of an output. */
static bool sim_mask_table(const CrtPlan *p)
{
    CrtTables t;
    if (!crt_tables_build(p, &t)) {
        return false;
    }
    bool ok = true;
    for (int y = 0; y < p->out_h && ok; y++) {
        int my = ((y - p->pic_y) % p->k + p->k) % p->k;
        for (int x = 0; x < p->out_w && ok; x++) {
            double m[3];
            crt_mask_at(p, x, y, m);
            const float *tm = t.mask + ((size_t)my * t.mask_w
                                        + (size_t)(x % t.mask_w)) * 4;
            for (int c = 0; c < 3; c++) {
                ok = ok && (tm[c] == (float)m[c]);
            }
        }
    }
    crt_tables_free(&t);
    return ok;
}

/* (build 108) The across pass's row within a line at every display row
 * of an output: the remainder's dividend never negative (HLSL leaves a
 * remainder of a negative number undefined -- build 107's arithmetic took
 * one above the picture's top, and the owner's GPU made it an unsigned one),
 * and the row the model's (crt_mask_at's, from the picture's top). */
static bool sim_rows(const CrtPlan *p, int *first_bad)
{
    CrtTables t;
    if (!crt_tables_build(p, &t)) {
        return false;
    }
    bool ok = true;
    *first_bad = -1;
    for (int y = 0; y < p->out_h && ok; y++) {
        int dividend = 0;
        int row      = crt_tables_row(&t, y, &dividend);
        int want     = ((y - p->pic_y) % p->k + p->k) % p->k;
        if (dividend < 0 || row != want) {
            ok         = false;
            *first_bad = y;
        }
    }
    crt_tables_free(&t);
    return ok;
}

/* (build 108) An HDR crop (write_crop's rectangle, PEAK 1000, WHITE 200)
 * through the tables, the float passes and the across pass's encode
 * (crt_encode_f), against the model's PQ, in 10-bit PQ codes; scRGB's
 * linear taken back to nits by its unit first. */
static void sim_crop_hdr(const CrtPlan *p, const CrtSource *src,
                         const SimSource *ss, int sx, int sy, int sw, int sh,
                         const char *what, int *worst_all, long *differ_all,
                         long *codes_all)
{
    int x0 = p->pic_x + (int)floor(sx * (double)p->pic_w / SCREEN_W);
    int x1 = p->pic_x + (int)ceil((sx + sw) * (double)p->pic_w / SCREEN_W);
    int y0 = p->pic_y + sy * p->k;
    int w  = x1 - x0;
    int h  = sh * p->k;
    float *ref = malloc(sizeof(float) * 3 * (size_t)w * (size_t)h);
    float *sim = malloc(sizeof(float) * 3 * (size_t)w * (size_t)h);
    crt_render(p, src, x0, y0, w, h, ref);
    CrtTables t;
    if (!crt_tables_build(p, &t)) {
        check(false, "%s: the tables could not be built", what);
        free(ref);
        free(sim);
        return;
    }
    crt_tables_render(&t, ss->colour, x0, y0, w, h, sim);
    int  worst  = 0;
    long differ = 0;
    for (long i = 0; i < (long)w * h; i++) {
        double lin[3] = { ref[i * 3], ref[i * 3 + 1], ref[i * 3 + 2] };
        double r2020[3];
        crt_709_to_2020(lin, r2020);
        float e[3];
        crt_encode_f(&t.enc, sim + i * 3, e);
        double back[3] = { e[0], e[1], e[2] };
        if (t.enc.encode == CRT_ENCODE_SCRGB) {
            double nits[3] = { e[0] * p->scrgb_nits, e[1] * p->scrgb_nits,
                               e[2] * p->scrgb_nits };
            double n2020[3];
            crt_709_to_2020(nits, n2020);
            for (int c = 0; c < 3; c++) {
                back[c] = crt_pq_encode(n2020[c] < 0.0 ? 0.0 : n2020[c]);
            }
        }
        for (int c = 0; c < 3; c++) {
            int a = (int)lround(crt_pq_encode(r2020[c] < 0.0 ? 0.0
                                                             : r2020[c])
                                * 1023.0);
            int b = (int)lround(back[c] * 1023.0);
            int d = abs(a - b);
            worst = (d > worst) ? d : worst;
            differ += (d > 0) ? 1 : 0;
        }
    }
    crt_tables_free(&t);
    say("  %-44s %4dx%-4d worst %d, %ld of %ld codes differ\n", what, w, h,
        worst, differ, 3L * w * h);
    *worst_all   = (worst > *worst_all) ? worst : *worst_all;
    *differ_all += differ;
    *codes_all  += 3L * w * h;
    free(ref);
    free(sim);
}

static int run_sim(const char *out)
{
    static const struct {
        const char *name, *png;
        int sx, sy, sw, sh;
    } CROPS[] = {
        { "title_logo", "route_out/new_game/01_title.png", 140, 132, 32, 20 },
        { "fight_hp", "route_out/climatrol_nei_dies/05_party_fights.png",
          12, 172, 32, 20 },
        /* (build 141a) No PNG: every 8-bit value a channel can have, off
         * the DAC ladder too (sim_source_values), in the crop. */
        { "every_value", NULL, 0, 0, 32, 20 },
    };
    if (out != NULL) {
        make_dir(out);
        char path[512];
        snprintf(path, sizeof path, "%s/sim_check.txt", out);
        g_report = fopen(path, "wb");
    }
    say("# The GPU passes' mirror against the model (crt_ref sim)\n\n"
        "crt_tables_build's tables through crt_tables_render (the two\n"
        "shaders' sums, in float) against crt_render, every SDR crop\n"
        "`crt_ref crop` writes, in 8-bit sRGB codes; (build 108) the\n"
        "across pass's row arithmetic at every row, and the pq1000 crops\n"
        "through its HDR10 and scRGB encodes, in 10-bit PQ codes.\n\n");

    say("## The mask table against crt_mask_at, every pixel\n\n");
    for (int si = 0; si < SCREEN_COUNT; si++) {
        for (int m = 1; m < CRT_MASK_COUNT; m++) {
            for (int sub = 0; sub < CRT_SUBPIXELS_COUNT; sub++) {
                CrtPlan p;
                plan_for(&p, SCREENS[si].w, SCREENS[si].h, true, (CrtMask)m,
                         (CrtSubpixels)sub, false, 1000, 200);
                if (p.set.mask != (CrtMask)m) {
                    continue;
                }
                check(sim_mask_table(&p), "%s %s %s: the table is "
                      "crt_mask_at", SCREENS[si].name, MASK_KEY[m],
                      SUB_KEY[sub]);
            }
        }
    }

    say("\n## (build 108) The across pass's row within a line, every row\n\n");
    for (int si = 0; si < SCREEN_COUNT; si++) {
        bool ok  = true;
        int  bad = -1;
        for (int m = 1; m < CRT_MASK_COUNT && ok; m++) {
            CrtPlan p;
            plan_for(&p, SCREENS[si].w, SCREENS[si].h, true, (CrtMask)m,
                     CRT_SUBPIXELS_RGB, false, 1000, 200);
            if (p.set.mask != (CrtMask)m) {
                continue;
            }
            ok = sim_rows(&p, &bad);
        }
        check(ok, "%s: every row's remainder from a dividend that is not "
              "negative, and the model's row", SCREENS[si].name);
        if (!ok) {
            say("      the first row that is not: %d\n", bad);
        }
    }

    for (size_t ci = 0; ci < sizeof CROPS / sizeof CROPS[0]; ci++) {
        const char *png = (CROPS[ci].png != NULL) ? CROPS[ci].png
                                                  : "every 8-bit value";
        SimSource *ss  = (CROPS[ci].png != NULL)
                       ? sim_source_png(CROPS[ci].png) : sim_source_values();
        CrtSource *src = NULL;
        if (ss != NULL) {
            src = malloc(sizeof *src);
            crt_source_colour(src, ss->colour);
        }
        if (src == NULL || ss == NULL) {
            check(false, "%s: cannot read %s%s", CROPS[ci].name, png,
                  made_by(CROPS[ci].png));
            free(src);
            sim_source_free(ss);
            continue;
        }
        say("\n## %s (%s)\n\n", CROPS[ci].name, png);
        int  worst  = 0;
        long differ = 0, codes = 0;
        for (int si = 0; si < SCREEN_COUNT; si++) {
            const Screen *s = &SCREENS[si];
            CrtSubpixels sub = (s->h == 2400) ? CRT_SUBPIXELS_OTHER
                                              : CRT_SUBPIXELS_RGB;
            for (int m = 0; m < CRT_MASK_COUNT; m++) {
                for (int scan = (m == 0) ? 0 : 1; scan <= 1; scan++) {
                    CrtPlan p;
                    plan_for(&p, s->w, s->h, scan != 0, (CrtMask)m, sub,
                             false, 1000, 200);
                    if (p.set.mask != (CrtMask)m) {
                        continue;
                    }
                    char what[96];
                    snprintf(what, sizeof what, "%s_%s_%s_sdr", s->name,
                             (m == 0 && scan) ? "scanlines" : MASK_KEY[m],
                             SUB_KEY[sub]);
                    sim_crop(&p, src, ss, CROPS[ci].sx, CROPS[ci].sy,
                             CROPS[ci].sw, CROPS[ci].sh, what, &worst,
                             &differ, &codes);
                }
            }
        }
        check(worst <= 1, "%s: every code within 1 of the model (worst %d; "
              "%ld of %ld differ)", CROPS[ci].name, worst, differ, codes);

        /* (build 108) The pq1000 crops: HDR10 at both HDR screens, and
         * scRGB at the P16's (its 1.0 at 203 nits, as on Wayland). */
        say("\n  HDR, PEAK 1000, WHITE 200, in 10-bit PQ codes:\n");
        int  hworst  = 0;
        long hdiffer = 0, hcodes = 0;
        for (int si = 0; si < SCREEN_COUNT; si++) {
            const Screen *s   = &SCREENS[si];
            bool          p16 = (s->h == 2400);
            if (!p16 && s->h != 2160) {
                continue;
            }
            CrtSubpixels sub = p16 ? CRT_SUBPIXELS_OTHER : CRT_SUBPIXELS_RGB;
            CrtMask      m   = p16 ? CRT_MASK_SHADOW : CRT_MASK_GRILLE;
            for (int e = CRT_ENCODE_PQ; e <= CRT_ENCODE_SCRGB; e++) {
                if (e == CRT_ENCODE_SCRGB && !p16) {
                    continue;
                }
                CrtPlan p;
                plan_for(&p, s->w, s->h, true, m, sub, true, 1000, 200);
                p.encode     = (CrtEncode)e;
                p.scrgb_nits = 203.0;
                char what[96];
                snprintf(what, sizeof what, "%s_%s_%s_%s1000", s->name,
                         MASK_KEY[m], SUB_KEY[sub],
                         (e == CRT_ENCODE_PQ) ? "pq" : "scrgb");
                sim_crop_hdr(&p, src, ss, CROPS[ci].sx, CROPS[ci].sy,
                             CROPS[ci].sw, CROPS[ci].sh, what, &hworst,
                             &hdiffer, &hcodes);
            }
        }
        check(hworst <= 1, "%s HDR: every 10-bit PQ code within 1 of the "
              "model (worst %d; %ld of %ld differ)", CROPS[ci].name, hworst,
              hdiffer, hcodes);
        free(src);
        sim_source_free(ss);
    }
    say("\n%d checks passed, %d failed\n", g_passed, g_failed);
    if (g_report != NULL) {
        fclose(g_report);
        g_report = NULL;
    }
    return (g_failed == 0) ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "sim") == 0) {
        return run_sim((argc >= 3) ? argv[2] : NULL);
    }
    if (argc >= 2 && strcmp(argv[1], "check") == 0) {
        return run_checks((argc >= 3) ? argv[2] : NULL);
    }
    if (argc == 9 && strcmp(argv[1], "crop") == 0) {
        return run_crops(argv[2], argv[3], argv[4], atoi(argv[5]),
                         atoi(argv[6]), atoi(argv[7]), atoi(argv[8]));
    }
    fprintf(stderr, "usage: crt_ref check [OUT]\n"
                    "       crt_ref sim [OUT]\n"
                    "       crt_ref crop IN.png OUT NAME SX SY SW SH\n");
    return 2;
}
