#include "crt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (build 105) docs/crt.md is the account of everything below: the model,
 * why each constant is what it is, and what tools/crt_ref.c checks. */

void crt_settings_default(CrtSettings *s)
{
    /* Off until phase 2 draws it: the defaults are the picture as it is. */
    memset(s, 0, sizeof *s);
    s->scanlines   = false;
    s->mask        = CRT_MASK_OFF;
    s->subpixels   = CRT_SUBPIXELS_RGB;
    s->hdr         = false;
    s->peak_nits   = 0;
    s->white_nits  = CRT_WHITE_DEFAULT;
}

void crt_display_default(CrtDisplay *d)
{
    memset(d, 0, sizeof *d);
    d->out_w     = SCREEN_W * 3;        /* platform_sdl3.c's WINDOW_SCALE */
    d->out_h     = SCREEN_H * 3;
    d->full_w    = 1920;
    d->full_h    = 1080;
    d->headroom  = 1.0f;
    d->sdr_white = 1.0f;
    d->sdr_nits  = 80.0f;
}

bool crt_display_parse(const char *text, CrtDisplay *d)
{
    crt_display_default(d);
    bool full_given = false;
    bool nits_given = false;
    bool first      = true;
    char word[64];
    int  used = 0;

    while (sscanf(text, " %63s%n", word, &used) == 1) {
        text += used;
        int   w = 0, h = 0;
        float a = 0.0f, b = 0.0f;
        if (first && sscanf(word, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
            d->out_w = w;
            d->out_h = h;
        } else if (sscanf(word, "full=%dx%d", &w, &h) == 2 && w > 0
                   && h > 0) {
            d->full_w  = w;
            d->full_h  = h;
            full_given = true;
        } else if (strcmp(word, "fullscreen") == 0) {
            d->fullscreen = true;
        } else if (strcmp(word, "window-scaled") == 0) {
            d->window_scaled = true;
        } else if (strcmp(word, "desktop-scaled") == 0) {
            d->desktop_scaled = true;
        } else if (strcmp(word, "hdr") == 0) {
            d->hdr = true;
        } else if (strcmp(word, "no-chain") == 0) {
            d->no_chain = true;
        } else if (sscanf(word, "hdr=%f/%f", &a, &b) == 2 && a > 0.0f
                   && b > 0.0f) {
            d->hdr       = true;
            d->headroom  = a;
            d->sdr_white = b;
        } else if (sscanf(word, "sdr=%f", &a) == 1 && a > 0.0f) {
            d->sdr_nits = a;
            nits_given  = true;
        } else {
            return false;
        }
        first = false;
    }
    if (d->fullscreen && !full_given) {
        d->full_w = d->out_w;
        d->full_h = d->out_h;
    }
    if (!nits_given) {
        d->sdr_nits = d->sdr_white * 80.0f;
    }
    return true;
}

const char *crt_why_text(CrtWhy why)
{
    switch (why) {
        case CRT_OK: {
            return "";
        }
        case CRT_NEEDS_FULLSCREEN: {
            return "NEEDS FULLSCREEN";
        }
        case CRT_NEEDS_480P: {
            return "NEEDS 480P";
        }
        case CRT_NEEDS_1080P: {
            return "NEEDS 1080P";
        }
        case CRT_NEEDS_1440P: {
            return "NEEDS 1440P";
        }
        case CRT_DESKTOP_SCALED: {
            return "DESKTOP SCALED";
        }
        case CRT_NO_HDR: {
            return "NO HDR";
        }
        case CRT_NO_SHADERS: {
            return "NO CRT SHADERS";
        }
        default: {
            return "?";
        }
    }
}

double crt_pixel_aspect(int aspect_mode)
{
    /* platform.h's AspectMode, in its order. */
    static const double PAR[3] = { 1.0, 14.0 / 15.0, 32.0 / 35.0 };
    if (aspect_mode < 0 || aspect_mode > 2) {
        return PAR[2];
    }
    return PAR[aspect_mode];
}

int crt_lines_k(int w, int h, double par)
{
    double across = SCREEN_W * par;
    int    k      = h / SCREEN_H;
    int    k_w    = (int)((double)w / across);
    return (k_w < k) ? k_w : k;
}

/* The native-pixel rule and the lines a scanline, together: `need` is the
 * k the setting wants. */
static CrtWhy needs(const CrtDisplay *d, double par, int need)
{
    bool native = d->fullscreen ? !d->desktop_scaled
                                : (!d->window_scaled && !d->desktop_scaled);
    int  k_now  = crt_lines_k(d->out_w, d->out_h, par);
    int  k_full = crt_lines_k(d->full_w, d->full_h, par);

    if (native && k_now >= need) {
        return CRT_OK;
    }
    if (d->desktop_scaled) {
        return CRT_DESKTOP_SCALED;
    }
    if (!d->fullscreen && k_full >= need) {
        return CRT_NEEDS_FULLSCREEN;
    }
    if (need >= 6) {
        return CRT_NEEDS_1440P;
    }
    if (need >= 4) {
        return CRT_NEEDS_1080P;
    }
    return CRT_NEEDS_480P;
}

/* The lines a scanline each setting wants: scanlines two (a beam and a
 * gap); the grille four (a triad's stripes need room between the lines'
 * edges, 1080p); slot and shadow masks six (their rows need several
 * display lines a scanline, or they beat against the scanlines: 1440p). */
static int mask_needs_k(CrtMask mask)
{
    switch (mask) {
        case CRT_MASK_GRILLE: {
            return 4;
        }
        case CRT_MASK_SLOT: {
            return 6;
        }
        case CRT_MASK_SHADOW: {
            return 6;
        }
        default: {
            return 0;
        }
    }
}

/* (build 141c) Scanlines, the masks and HDR are the GPU chain's; with the
 * placeholder drawing, none of them can show, whatever the display. */
CrtWhy crt_scanlines_why(const CrtDisplay *d, double par)
{
    if (d->no_chain) {
        return CRT_NO_SHADERS;
    }
    return needs(d, par, 2);
}

CrtWhy crt_mask_why(const CrtDisplay *d, double par, CrtMask mask)
{
    int need = mask_needs_k(mask);
    if (need == 0) {
        return CRT_OK;
    }
    if (d->no_chain) {
        return CRT_NO_SHADERS;
    }
    return needs(d, par, need);
}

CrtWhy crt_hdr_why(const CrtDisplay *d)
{
    if (d->no_chain) {
        return CRT_NO_SHADERS;
    }
    return d->hdr ? CRT_OK : CRT_NO_HDR;
}

int crt_peak_auto(const CrtDisplay *d)
{
    if (d->hdr && d->headroom > 1.0f && d->sdr_nits > 0.0f) {
        return (int)(d->headroom * d->sdr_nits + 0.5f);
    }
    return CRT_PEAK_FALLBACK;
}

/* ---- colour ---- */

/* Rec.709 / sRGB primaries with D65, to XYZ. */
static const double M709_TO_XYZ[3][3] = {
    { 0.4124564, 0.3575761, 0.1804375 },
    { 0.2126729, 0.7151522, 0.0721750 },
    { 0.0193339, 0.1191920, 0.9503041 },
};

/* BT.2087: Rec.709 linear to Rec.2020 linear. */
static const double M709_TO_2020[3][3] = {
    { 0.6274, 0.3293, 0.0433 },
    { 0.0691, 0.9195, 0.0114 },
    { 0.0164, 0.0880, 0.8956 },
};

static void mul3(const double m[3][3], const double in[3], double out[3])
{
    for (int r = 0; r < 3; r++) {
        out[r] = m[r][0] * in[0] + m[r][1] * in[1] + m[r][2] * in[2];
    }
}

void crt_709_to_2020(const double in[3], double out[3])
{
    mul3(M709_TO_2020, in, out);
}

void crt_709_to_xy(const double rgb[3], double *x, double *y)
{
    double xyz[3];
    mul3(M709_TO_XYZ, rgb, xyz);
    double sum = xyz[0] + xyz[1] + xyz[2];
    *x = (sum > 0.0) ? xyz[0] / sum : 0.0;
    *y = (sum > 0.0) ? xyz[1] / sum : 0.0;
}

void crt_cram_linear(Cram c, double out[3])
{
    Rgb8 v = cram_to_rgb8(c);
    out[0] = crt_value_linear(v.r);
    out[1] = crt_value_linear(v.g);
    out[2] = crt_value_linear(v.b);
}

double crt_value_linear(u8 v)
{
    return pow(v / 255.0, CRT_GAMMA);
}

void crt_source_colour(CrtSource *s, const Rgb8 *colour)
{
    float lut[256];
    for (int v = 0; v < 256; v++) {
        lut[v] = (float)crt_value_linear((u8)v);
    }
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            Rgb8 c = colour[y * SCREEN_W + x];
            s->lin[y][x][0] = lut[c.r];
            s->lin[y][x][1] = lut[c.g];
            s->lin[y][x][2] = lut[c.b];
        }
    }
}

/* ---- the encodes ---- */

double crt_srgb_encode(double v)
{
    if (v <= 0.0) {
        return 0.0;
    }
    if (v >= 1.0) {
        return 1.0;
    }
    return (v <= 0.0031308) ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

/* ST 2084's constants. */
#define PQ_M1 (2610.0 / 16384.0)
#define PQ_M2 (2523.0 / 4096.0 * 128.0)
#define PQ_C1 (3424.0 / 4096.0)
#define PQ_C2 (2413.0 / 4096.0 * 32.0)
#define PQ_C3 (2392.0 / 4096.0 * 32.0)

double crt_pq_encode(double nits)
{
    double y = nits / 10000.0;
    if (y <= 0.0) {
        return 0.0;
    }
    if (y > 1.0) {
        y = 1.0;
    }
    double p = pow(y, PQ_M1);
    return pow((PQ_C1 + PQ_C2 * p) / (1.0 + PQ_C3 * p), PQ_M2);
}

double crt_pq_decode(double e)
{
    if (e <= 0.0) {
        return 0.0;
    }
    double p   = pow(e, 1.0 / PQ_M2);
    double num = p - PQ_C1;
    if (num < 0.0) {
        num = 0.0;
    }
    return 10000.0 * pow(num / (PQ_C2 - PQ_C3 * p), 1.0 / PQ_M1);
}

/* ---- the beam ---- */

static double norm_cdf(double t)
{
    return 0.5 * erfc(-t / sqrt(2.0));
}

static double norm_pdf(double t)
{
    return exp(-0.5 * t * t) * 0.39894228040143267794;   /* 1/sqrt(2 pi) */
}

/* The antiderivative of norm_cdf: G(t) = t Phi(t) + phi(t). */
static double cdf_integral(double t)
{
    return t * norm_cdf(t) + norm_pdf(t);
}

/* The beam's height for a drive level, in display rows. */
static double beam_sigma(int k, double level)
{
    if (level < 0.0) {
        level = 0.0;
    }
    if (level > 1.0) {
        level = 1.0;
    }
    return k * (CRT_BEAM_SIGMA_MIN
                + (CRT_BEAM_SIGMA_MAX - CRT_BEAM_SIGMA_MIN)
                  * pow(level, CRT_BEAM_ATTACK));
}

/* Line j's share of display row [r0, r0 + 1) (rows from the picture's
 * top), a line's rows summing to k: a normal profile of height sigma,
 * integrated over the row, so a flat field of level L comes out L on
 * average whatever sigma is. With scan depth s it is blended with the
 * flat line (the row inside line j's pitch or not). */
static double beam_row(int k, int j, double r0, double sigma, double s)
{
    double yc   = (j + 0.5) * k;
    double beam = k * (norm_cdf((r0 + 1.0 - yc) / sigma)
                       - norm_cdf((r0 - yc) / sigma));
    double flat = (r0 >= (double)j * k && r0 < (double)(j + 1) * k) ? 1.0
                                                                   : 0.0;
    return s * beam + (1.0 - s) * flat;
}

/* A white field's brightest row over its mean, at full drive. */
static double white_beam_peak(int k)
{
    double sigma = beam_sigma(k, 1.0);
    double best  = 0.0;
    for (int r = 0; r < k; r++) {
        double sum = 0.0;
        for (int j = -4; j <= 4; j++) {
            sum += beam_row(k, j, (double)r, sigma, 1.0);
        }
        best = (sum > best) ? sum : best;
    }
    return best;
}

/* ---- the mask ---- */

/* Which channel each pixel of a triad lights (-1 none). The stripes'
 * orders put the lit subpixels where they fall evenly when the triad is
 * four pixels (R,G,B,dark on RGB: subpixels 0, 4, 8, then 12); a
 * three-pixel triad cannot (docs/crt.md), and takes the panel's order
 * reversed, which bunches the three lit subpixels two apart and leaves a
 * five-subpixel gap for the grille's dark stripe -- Sony Megatron's 4K
 * 800 TVL grille. OTHER lights whole pixels in R, G, B order. */
static int triad_channel(CrtSubpixels sub, int triad, int phase)
{
    static const int T4_RGB[4] = { 0, 1, 2, -1 };
    static const int T4_BGR[4] = { 2, 1, 0, -1 };
    static const int T3_RGB[3] = { 2, 1, 0 };
    static const int T3_BGR[3] = { 0, 1, 2 };
    static const int T3_ANY[3] = { 0, 1, 2 };

    if (triad == 4) {
        return (sub == CRT_SUBPIXELS_BGR) ? T4_BGR[phase] : T4_RGB[phase];
    }
    if (sub == CRT_SUBPIXELS_RGB) {
        return T3_RGB[phase];
    }
    if (sub == CRT_SUBPIXELS_BGR) {
        return T3_BGR[phase];
    }
    return T3_ANY[phase];
}

static int wrap(int v, int n)
{
    int r = v % n;
    return (r < 0) ? r + n : r;
}

void crt_mask_full(CrtMask mask, CrtSubpixels sub, int triad, int k,
                   int x, int yrel, double w[3])
{
    w[0] = w[1] = w[2] = 0.0;

    switch (mask) {
        case CRT_MASK_GRILLE: {
            int ch = triad_channel(sub, triad, wrap(x, triad));
            if (ch >= 0) {
                w[ch] = (double)triad;
            }
            break;
        }
        case CRT_MASK_SLOT: {
            /* Two triad columns, each cut once a line pitch by a one-row
             * bridge; the second column's bridge half a pitch below the
             * first's, both clear of the beam's centre. */
            int col    = wrap(x / triad, 2);       /* x is never negative */
            int row    = wrap(yrel, k);
            int bridge = k / 4 + ((col == 1) ? k / 2 : 0);
            if (row == bridge) {
                break;
            }
            int ch = triad_channel(sub, triad, wrap(x, triad));
            if (ch >= 0) {
                w[ch] = (double)triad * (double)k / (double)(k - 1);
            }
            break;
        }
        case CRT_MASK_SHADOW: {
            /* Delta: the lower half of each line pitch shifted half a
             * triad, so the triads sit in a hexagon. The triad is four
             * pixels here at every size (crt_plan_resolve): half of
             * three is not a pixel. */
            int half  = (wrap(yrel, k) < k / 2) ? 0 : 1;
            int phase = wrap(x + half * (triad / 2), triad);
            int ch    = triad_channel(sub, triad, phase);
            if (ch >= 0) {
                w[ch] = (double)triad;
            }
            break;
        }
        default: {
            w[0] = w[1] = w[2] = 1.0;
            break;
        }
    }
}

void crt_mask_at(const CrtPlan *p, int x, int y, double w[3])
{
    if (p->set.mask == CRT_MASK_OFF || p->mask_depth <= 0.0) {
        w[0] = w[1] = w[2] = 1.0;
        return;
    }
    double full[3];
    crt_mask_full(p->set.mask, p->set.subpixels, p->triad, p->k, x,
                  y - p->pic_y, full);
    for (int c = 0; c < 3; c++) {
        w[c] = 1.0 + p->mask_depth * (full[c] - 1.0);
    }
}

static double mask_peak_of(const CrtPlan *p)
{
    if (p->set.mask == CRT_MASK_OFF) {
        return 1.0;
    }
    double best = 0.0;
    for (int y = 0; y < p->k; y++) {
        for (int x = 0; x < 2 * p->triad; x++) {
            double w[3];
            crt_mask_full(p->set.mask, p->set.subpixels, p->triad, p->k, x,
                          y, w);
            for (int c = 0; c < 3; c++) {
                best = (w[c] > best) ? w[c] : best;
            }
        }
    }
    return best;
}

/* ---- the plan ---- */

void crt_plan_resolve(CrtPlan *p)
{
    /* (build 108) The encode goes with the mode; it moves no light. */
    if (!p->hdr_out) {
        p->encode = CRT_ENCODE_SRGB;
    } else if (p->encode == CRT_ENCODE_SRGB) {
        p->encode = CRT_ENCODE_PQ;
    }
    if (p->scrgb_nits <= 0.0) {
        p->scrgb_nits = 80.0;
    }

    int k = crt_lines_k(p->out_w, p->out_h, p->par);
    p->k     = (k < 1) ? 1 : k;
    p->pic_h = SCREEN_H * p->k;
    p->pic_w = (int)(SCREEN_W * p->par * p->k + 0.5);
    p->pic_x = (p->out_w - p->pic_w) / 2;
    p->pic_y = (p->out_h - p->pic_h) / 2;

    if (p->k < 2) {
        p->set.scanlines = false;
    }
    if (p->k < mask_needs_k(p->set.mask)) {
        p->set.mask = CRT_MASK_OFF;
    }
    /* (build 106) Four pixels everywhere, the owner's "whatever is most
     * compatible": R, G, B and dark space the lit subpixels evenly on every
     * panel, stripes and OTHER alike, whatever SUBPIXELS says -- three
     * pixels cannot on stripes (docs/crt.md) -- and it is Sony Megatron's
     * coarsest 1080p grille. Build 105 took three below 4K (k < 9). The
     * model keeps the three-pixel layout (triad_channel), so going back is
     * this line. */
    p->triad = 4;

    /* D65: the phosphors are taken as Rec.709's and the white is the
     * display's, so the guns need no gains. */
    for (int c = 0; c < 3; c++) {
        p->white_gain[c] = 1.0;
    }
    p->white_max = 1.0;
    p->beam_peak = p->set.scanlines ? white_beam_peak(p->k) : 1.0;
    p->mask_peak = mask_peak_of(p);

    if (!p->hdr_out) {
        /* SDR has no headroom: the full mask and scanlines, and the
         * picture as bright as the brightest subpixel allows -- dimmer. */
        p->scan_depth = p->set.scanlines ? 1.0 : 0.0;
        p->mask_depth = (p->set.mask != CRT_MASK_OFF) ? 1.0 : 0.0;
        p->scale      = 1.0 / (p->white_max * p->beam_peak * p->mask_peak);
        return;
    }

    /* HDR: the white field averages WHITE; its brightest subpixel is
     * WHITE x white_max x the beam's peak x the mask's, and that may not
     * pass PEAK. The scanlines are kept whole first, then the mask takes
     * what headroom is left. */
    double white  = (p->set.white_nits > 0) ? p->set.white_nits
                                            : CRT_WHITE_DEFAULT;
    double budget = p->peak_nits / (white * p->white_max);
    if (budget < 1.0) {
        white  = p->peak_nits / p->white_max;
        budget = 1.0;
    }
    p->scale = white;

    p->scan_depth = 0.0;
    if (p->set.scanlines && p->beam_peak > 1.0) {
        double s = (budget - 1.0) / (p->beam_peak - 1.0);
        p->scan_depth = (s > 1.0) ? 1.0 : s;
    }
    double left = budget / (1.0 + p->scan_depth * (p->beam_peak - 1.0));

    p->mask_depth = 0.0;
    if (p->set.mask != CRT_MASK_OFF && p->mask_peak > 1.0) {
        double m = (left - 1.0) / (p->mask_peak - 1.0);
        m = (m > 1.0) ? 1.0 : m;
        p->mask_depth = (m < 0.0) ? 0.0 : m;
    }
    /* The beam's peak moves with the depth (it is a blend), so the peak
     * the budget was spent on is reached, not passed. */
}

/* ---- rendering ---- */

/* The source columns whose beams reach a display column with footprint
 * [a, b) (in source pixels): four of the beam's sigmas either side, and
 * one more. */
static void across_span(double a, double b, int *lo, int *hi)
{
    const double sig_h = CRT_BEAM_H_SIGMA;
    *lo = (int)floor(a - 4.0 * sig_h) - 1;
    *hi = (int)floor(b + 4.0 * sig_h) + 1;
}

/* Source pixel i is a box [i, i+1) through a normal of CRT_BEAM_H_SIGMA,
 * averaged over the display column's footprint [a, b) of width du. */
static double across_weight(double a, double b, double du, int i)
{
    const double sig_h = CRT_BEAM_H_SIGMA;
    return sig_h * (cdf_integral((b - i) / sig_h)
                    - cdf_integral((a - i) / sig_h)
                    - cdf_integral((b - i - 1) / sig_h)
                    + cdf_integral((a - i - 1) / sig_h))
           / du;
}

void crt_render(const CrtPlan *p, const CrtSource *src, int x0, int y0,
                int w, int h, float *out)
{
    const double du    = (double)SCREEN_W / p->pic_w;
    const int    k     = p->k;

    for (int oy = 0; oy < h; oy++) {
        int    y    = y0 + oy;
        double r0   = (double)(y - p->pic_y);        /* row from the top */
        int    jmid = (int)floor(r0 / k);

        for (int ox = 0; ox < w; ox++) {
            int    x   = x0 + ox;
            double a   = (x - p->pic_x) * du;        /* source footprint */
            double b   = a + du;
            int    i_lo, i_hi;
            across_span(a, b, &i_lo, &i_hi);
            double acc[3] = { 0.0, 0.0, 0.0 };

            for (int i = i_lo; i <= i_hi; i++) {
                if (i < 0 || i >= SCREEN_W) {
                    continue;
                }
                double wx = across_weight(a, b, du, i);
                if (wx <= 0.0) {
                    continue;
                }
                for (int j = jmid - 3; j <= jmid + 3; j++) {
                    if (j < 0 || j >= SCREEN_H) {
                        continue;
                    }
                    for (int c = 0; c < 3; c++) {
                        double level = src->lin[j][i][c];
                        if (level <= 0.0) {
                            continue;
                        }
                        double sigma = beam_sigma(k, level);
                        acc[c] += wx * level
                                * beam_row(k, j, r0, sigma, p->scan_depth);
                    }
                }
            }

            double m[3];
            crt_mask_at(p, x, y, m);
            float *o = out + ((size_t)oy * w + ox) * 3;
            for (int c = 0; c < 3; c++) {
                o[c] = (float)(p->scale * p->white_gain[c] * acc[c] * m[c]);
            }
        }
    }
}

/* ---- (build 107) the chain as tables, for the GPU ---- */

double crt_level_linear(int level)
{
    double lin[3];
    crt_cram_linear(cram_make((u8)level, (u8)level, (u8)level), lin);
    return lin[0];
}

void crt_tables_free(CrtTables *t)
{
    free(t->beam);
    free(t->across);
    free(t->mask);
    memset(t, 0, sizeof *t);
}

bool crt_tables_build(const CrtPlan *p, CrtTables *t)
{
    memset(t, 0, sizeof *t);
    const int k = p->k;
    t->k        = k;
    t->pic_y    = p->pic_y;
    t->beam_w   = (2 * CRT_LINE_REACH + 1) * k;
    t->across_w = p->out_w;
    t->mask_w   = 2 * p->triad;
    t->mask_h   = k;
    t->beam   = calloc((size_t)CRT_BEAM_ROWS * (size_t)t->beam_w,
                       sizeof(float));
    t->across = calloc((size_t)3 * (size_t)t->across_w * 4, sizeof(float));
    t->mask   = calloc((size_t)t->mask_w * (size_t)t->mask_h * 4,
                       sizeof(float));
    if (t->beam == NULL || t->across == NULL || t->mask == NULL) {
        crt_tables_free(t);
        return false;
    }

    /* Down: a line's light in the row at offset d from its top (d = row -
     * line x k, from -3k to 4k-1): the level times beam_row, which is a
     * function of d alone (line 0 at row d is line j at row jk + d). Value
     * 0 is no light, as crt_render skips it. (build 141a) A row for each
     * 8-bit value a gun can have in the colour plane: the ladder's eight
     * are the rows the eight levels had, the same numbers. */
    for (int n = 1; n < CRT_BEAM_ROWS; n++) {
        double level = crt_value_linear((u8)n);
        double sigma = beam_sigma(k, level);
        for (int c = 0; c < t->beam_w; c++) {
            int d = c - CRT_LINE_REACH * k;
            t->beam[n * t->beam_w + c] =
                (float)(level * beam_row(k, 0, (double)d, sigma,
                                         p->scan_depth));
        }
    }

    /* Across: each display column's source columns and their weights, as
     * crt_render works them out; nothing for a column off the frame or a
     * weight that is not positive, which crt_render skips. */
    const double du = (double)SCREEN_W / p->pic_w;
    for (int x = 0; x < t->across_w; x++) {
        double a = (x - p->pic_x) * du;
        double b = a + du;
        int    lo, hi;
        across_span(a, b, &lo, &hi);
        if (hi - lo + 1 > CRT_ACROSS_TAPS) {
            crt_tables_free(t);
            return false;
        }
        for (int tap = 0; tap < CRT_ACROSS_TAPS; tap++) {
            int    i  = lo + tap;
            double wx = 0.0;
            if (i <= hi && i >= 0 && i < SCREEN_W) {
                wx = across_weight(a, b, du, i);
            }
            int row = tap / 4;
            t->across[((size_t)row * t->across_w + x) * 4 + (tap % 4)] =
                (wx > 0.0) ? (float)wx : 0.0f;
        }
        t->across[((size_t)2 * t->across_w + x) * 4] = (float)lo;
    }

    /* The mask: crt_mask_at repeats every two triads across (the slot's
     * two columns) and every line down, counted from the picture's top. */
    for (int ty = 0; ty < t->mask_h; ty++) {
        for (int tx = 0; tx < t->mask_w; tx++) {
            double m[3];
            crt_mask_at(p, tx, p->pic_y + ty, m);
            float *o = t->mask + ((size_t)ty * t->mask_w + tx) * 4;
            o[0] = (float)m[0];
            o[1] = (float)m[1];
            o[2] = (float)m[2];
            o[3] = 1.0f;
        }
    }

    for (int c = 0; c < 3; c++) {
        t->gain[c] = (float)(p->scale * p->white_gain[c]);
    }
    /* (build 108) The row within a line, from a dividend that is never
     * negative (crt.h, CrtTables.mask_phase). */
    t->mask_phase = wrap(-p->pic_y, k);
    crt_encode_params(p->encode,
                      (p->set.white_nits > 0) ? p->set.white_nits
                                              : CRT_WHITE_DEFAULT,
                      p->scrgb_nits, &t->enc);
    return true;
}

/* Floor division, for rows above the picture. */
static int floor_div(int a, int b)
{
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}

int crt_tables_row(const CrtTables *t, int y, int *dividend)
{
    int a = y + t->mask_phase;
    if (dividend != NULL) {
        *dividend = a;
    }
    return a % t->k;
}

/* The down pass at source column i, display row y: src/shaders/
 * crt_down_rgb.frag.hlsl, line for line. */
static void down_texel(const CrtTables *t, const Rgb8 *colour, int i, int y,
                       float out[3])
{
    int   r0   = y - t->pic_y;
    int   jmid = floor_div(r0, t->k);
    float acc[3] = { 0.0f, 0.0f, 0.0f };
    for (int dj = -CRT_LINE_REACH; dj <= CRT_LINE_REACH; dj++) {
        int j = jmid + dj;
        if (j >= 0 && j < SCREEN_H) {
            int col = r0 - j * t->k + CRT_LINE_REACH * t->k;
            Rgb8 v = colour[j * SCREEN_W + i];
            acc[0] += t->beam[v.r * t->beam_w + col];
            acc[1] += t->beam[v.g * t->beam_w + col];
            acc[2] += t->beam[v.b * t->beam_w + col];
        }
    }
    out[0] = acc[0];
    out[1] = acc[1];
    out[2] = acc[2];
}

void crt_tables_render(const CrtTables *t, const Rgb8 *colour, int x0,
                       int y0, int w, int h, float *out)
{
    float *down = malloc(sizeof(float) * 3 * SCREEN_W);
    for (int oy = 0; oy < h; oy++) {
        int y = y0 + oy;
        for (int i = 0; i < SCREEN_W; i++) {
            down_texel(t, colour, i, y, down + i * 3);
        }
        int my = crt_tables_row(t, y, NULL);
        for (int ox = 0; ox < w; ox++) {
            int x  = x0 + ox;
            int lo = (int)t->across[((size_t)2 * t->across_w + x) * 4];
            float acc[3] = { 0.0f, 0.0f, 0.0f };
            /* src/shaders/crt_across.frag.hlsl, line for line. */
            for (int tap = 0; tap < CRT_ACROSS_TAPS; tap++) {
                int   ic = lo + tap;
                ic = (ic < 0) ? 0 : ((ic > SCREEN_W - 1) ? SCREEN_W - 1 : ic);
                float wt = t->across[((size_t)(tap / 4) * t->across_w + x) * 4
                                     + (tap % 4)];
                for (int c = 0; c < 3; c++) {
                    acc[c] += wt * down[ic * 3 + c];
                }
            }
            const float *m = t->mask + ((size_t)my * t->mask_w
                                        + (size_t)(x % t->mask_w)) * 4;
            float *o = out + ((size_t)oy * w + ox) * 3;
            for (int c = 0; c < 3; c++) {
                o[c] = t->gain[c] * acc[c] * m[c];
            }
        }
    }
    free(down);
}

float crt_srgb_decode_f(float v)
{
    if (v <= 0.0f) {
        return 0.0f;
    }
    if (v >= 1.0f) {
        return 1.0f;
    }
    return (v <= 0.04045f) ? v / 12.92f
                           : powf((v + 0.055f) / 1.055f, 2.4f);
}

/* ---- (build 108) the HDR encodes, as the shaders do them ---- */

void crt_encode_params(CrtEncode e, double white_nits, double scrgb_nits,
                       CrtEncodeParams *out)
{
    memset(out, 0, sizeof *out);
    out->encode = e;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            out->to2020[r][c] = (float)M709_TO_2020[r][c];
        }
    }
    if (scrgb_nits <= 0.0) {
        scrgb_nits = 80.0;
    }
    switch (e) {
        case CRT_ENCODE_PQ: {
            out->scale = 1.0f / 10000.0f;
            out->white = (float)white_nits;
            break;
        }
        case CRT_ENCODE_SCRGB: {
            out->scale = (float)(1.0 / scrgb_nits);
            out->white = (float)white_nits;
            break;
        }
        default: {
            out->encode = CRT_ENCODE_SRGB;
            out->scale  = 1.0f;
            out->white  = 1.0f;
            break;
        }
    }
}

float crt_pq_encode_f(float y)
{
    const float m1 = 2610.0f / 16384.0f;
    const float m2 = 2523.0f / 4096.0f * 128.0f;
    const float c1 = 3424.0f / 4096.0f;
    const float c2 = 2413.0f / 4096.0f * 32.0f;
    const float c3 = 2392.0f / 4096.0f * 32.0f;
    if (y <= 0.0f) {
        return 0.0f;
    }
    if (y > 1.0f) {
        y = 1.0f;
    }
    float p = powf(y, m1);
    return powf((c1 + c2 * p) / (1.0f + c3 * p), m2);
}

void crt_encode_f(const CrtEncodeParams *e, const float lin[3],
                  float out[3])
{
    switch (e->encode) {
        case CRT_ENCODE_PQ: {
            for (int r = 0; r < 3; r++) {
                float c = e->to2020[r][0] * lin[0] + e->to2020[r][1] * lin[1]
                        + e->to2020[r][2] * lin[2];
                out[r] = crt_pq_encode_f(c * e->scale);
            }
            break;
        }
        case CRT_ENCODE_SCRGB: {
            for (int c = 0; c < 3; c++) {
                out[c] = lin[c] * e->scale;
            }
            break;
        }
        default: {
            for (int c = 0; c < 3; c++) {
                out[c] = crt_srgb_encode_f(lin[c]);
            }
            break;
        }
    }
}

void crt_encode_flat_f(const CrtEncodeParams *e, const float code[3],
                       float out[3])
{
    if (e->encode == CRT_ENCODE_SRGB) {
        out[0] = code[0];
        out[1] = code[1];
        out[2] = code[2];
        return;
    }
    float lin[3];
    for (int c = 0; c < 3; c++) {
        lin[c] = crt_srgb_decode_f(code[c]) * e->white;
    }
    crt_encode_f(e, lin, out);
}

void crt_encode_model(const CrtPlan *p, const double lin[3], double out[3])
{
    if (!p->hdr_out) {
        for (int c = 0; c < 3; c++) {
            out[c] = crt_srgb_encode(lin[c]);
        }
        return;
    }
    if (p->encode == CRT_ENCODE_SCRGB) {
        double unit = (p->scrgb_nits > 0.0) ? p->scrgb_nits : 80.0;
        for (int c = 0; c < 3; c++) {
            out[c] = lin[c] / unit;
        }
        return;
    }
    double r2020[3];
    crt_709_to_2020(lin, r2020);
    for (int c = 0; c < 3; c++) {
        out[c] = crt_pq_encode(r2020[c] < 0.0 ? 0.0 : r2020[c]);
    }
}

float crt_srgb_encode_f(float v)
{
    if (v <= 0.0f) {
        return 0.0f;
    }
    if (v >= 1.0f) {
        return 1.0f;
    }
    return (v <= 0.0031308f) ? 12.92f * v
                             : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
}

void crt_sharp_layout(int out_w, int out_h, double par, CrtSharp *l)
{
    double across = SCREEN_W * par;
    int    k      = out_h / SCREEN_H;
    int    k_w    = (int)((double)out_w / across);
    if (k_w < k) {
        k = k_w;
    }
    if (k >= 1) {
        l->h      = SCREEN_H * k;
        l->w      = (int)(across * k + 0.5);
        l->step_h = SCREEN_H * k;
        l->step_w = SCREEN_W * ((l->w + SCREEN_W - 1) / SCREEN_W);
    } else {
        double fit_h = (double)out_h / SCREEN_H;
        double fit_w = (double)out_w / across;
        double fit   = (fit_h < fit_w) ? fit_h : fit_w;
        l->h      = (int)(SCREEN_H * fit);
        l->w      = (int)(across * fit);
        l->step_h = SCREEN_H;
        l->step_w = SCREEN_W;
    }
    l->x = (out_w - l->w) / 2;
    l->y = (out_h - l->h) / 2;
}
