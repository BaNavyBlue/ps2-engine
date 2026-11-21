/* (build 107) The CRT chain's read-back check: the GPU against the model.
 * docs/crt.md, "Phase 2" and "The read-back check".
 *
 *   make crt_gpu_check && ./crt_gpu_check [OUT]     (OUT: route_out/crt_gpu_check)
 *
 * Run from the tree's top, after `make shaders`: it reads shaders/ and two
 * route PNGs. It opens a window as the game does (960x672, high pixel
 * density), claims it for SDL_GPU with the game's SDR swapchain, and draws
 * through crt_gpu.c -- the game's own passes and pipelines, built for the
 * swapchain's format -- into textures of that format at each display's
 * size, and reads them back. A swapchain texture itself is made to be drawn
 * into and presented, and SDL_GPU does not promise it can be copied out of,
 * so the frames checked are drawn by the same pipelines into textures of
 * the swapchain's format; the window shows the flat picture between checks.
 *
 * What it checks, within 1 code (the encode's rounding):
 *   1. every SDR crop `crt_ref crop` writes (the title's logo, Rudo's HP
 *      box; 1080p, 1440p, 4K, 3840x2400; flat, scanlines, each mask the
 *      display can have): the GPU's pixels against the model (crt_render)
 *      and against the passes' CPU mirror (crt_tables_render), 8-bit;
 *   2. for the title, every such output whole -- bars and beam tails too --
 *      against the mirror;
 *   3. (build 108) the pq1000 crops (4K RGB grille, the P16's shadow mask)
 *      in HDR, by HDR10 into R10G10B10A2 and the P16's by scRGB into RGBA16
 *      float, the title's whole too, in 10-bit PQ codes;
 *   4. the flat picture (the CRT off), whole, at each display and a game
 *      window's two sizes, at every pixel aspect, against its CPU mirror;
 *      (build 108) and at WHITE 200 in HDR10 and scRGB.
 * Under a FAIL, a line says where: the rows and columns more than a code
 * apart and the worst pixel. The GPU's crops go to OUT as PNGs named as
 * crt_ref names its own (HDR ones 16-bit PQ), for looking at beside
 * route_out/crt_reference/; OUT/report.txt has it all. */

#include "../src/core/crt.h"
#include "../src/core/palette.h"
#include "../src/core/png.h"
#include "../src/core/pngwrite.h"
#include "../src/platform/crt_gpu.h"

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

static const struct {
    const char *name, *png;
    int         sx, sy, sw, sh;
} CROPS[] = {
    { "title_logo", "route_out/new_game/01_title.png", 140, 132, 32, 20 },
    { "fight_hp", "route_out/climatrol_nei_dies/05_party_fights.png", 12,
      172, 32, 20 },
    /* (build 141a) No PNG: every 8-bit value a channel can have, off the
     * DAC ladder too, in the crop (as crt_ref's sim check). */
    { "every_value", NULL, 0, 0, 32, 20 },
};
#define CROP_COUNT ((int)(sizeof CROPS / sizeof CROPS[0]))

static const char *const MASK_KEY[CRT_MASK_COUNT] = {
    "flat", "grille", "slot", "shadow"
};
static const char *const SUB_KEY[CRT_SUBPIXELS_COUNT] = {
    "rgb", "bgr", "other"
};

/* (build 141a) A frame as the game hands it over: its colour plane (until
 * 141a, the index plane and each index's CRAM word and levels). */
typedef struct {
    Rgb8 colour[SCREEN_W * SCREEN_H];
} Frame;

typedef struct {
    SDL_GPUDevice       *dev;
    SDL_Window          *win;
    CrtGpu              *g;
    SDL_GPUTextureFormat format;    /* the SDR swapchain's */
    SDL_GPUTextureFormat current;   /* (build 108) the pipelines' */
    const Frame         *shown;     /* what the window shows between checks */
} Ctx;

static FILE *g_report;
static int   g_passed, g_failed;

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
    fflush(stdout);
}

static void verdict(bool ok, const char *fmt, ...)
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

/* A route PNG as the game hands a frame over: (build 141a) its colour
 * plane, each pixel's PLTE colour through CRAM and back, as crt_ref reads
 * it; black past the PLTE. With no path, the every-value frame: pixel n of
 * each 32x20 block (row by row) red n, green 7n, blue 255 - n, modulo 256. */
static bool load_frame(const char *path, Frame *f)
{
    memset(f, 0, sizeof *f);
    if (path == NULL) {
        for (int y = 0; y < SCREEN_H; y++) {
            for (int x = 0; x < SCREEN_W; x++) {
                int   n = (y % 20) * 32 + (x % 32);
                Rgb8 *c = &f->colour[y * SCREEN_W + x];
                c->r = (u8)(n & 0xFF);
                c->g = (u8)((n * 7) & 0xFF);
                c->b = (u8)((255 - n) & 0xFF);
            }
        }
        return true;
    }
    /* (build 148) Read as RGBA, as crt_ref's png_colour_plane: a route
     * shot with a picture in it is RGB since 146a (the battle floor's,
     * climatrol_nei_dies's, since 148). */
    PngRgba img;
    if (!png_load_rgba(&img, path)) {
        return false;
    }
    if (img.width != SCREEN_W || img.height != SCREEN_H) {
        png_rgba_free(&img);
        return false;
    }
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        const u8 *p = img.rgba + (size_t)i * 4;
        f->colour[i] = cram_to_rgb8(cram_from_rgb8(p[0], p[1], p[2]));
    }
    png_rgba_free(&img);
    return true;
}

static const char *frame_name(const char *path)
{
    return (path != NULL) ? path : "every 8-bit value";
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

static CrtSource *source_of(const Frame *f)
{
    CrtSource *s = malloc(sizeof *s);
    crt_source_colour(s, f->colour);
    return s;
}

/* crt_ref's plan for a display: SDR, or (build 108) HDR at PEAK 1000,
 * WHITE 200 (its pq1000 crops) with the encode `e` and scRGB's 1.0 at
 * `unit` nits. */
static void plan_for(CrtPlan *p, int w, int h, bool scan, CrtMask mask,
                     CrtSubpixels sub, CrtEncode e, double unit)
{
    memset(p, 0, sizeof *p);
    p->out_w = w;
    p->out_h = h;
    p->par   = crt_pixel_aspect(2);
    crt_settings_default(&p->set);
    p->set.scanlines  = scan;
    p->set.mask       = mask;
    p->set.subpixels  = sub;
    p->set.hdr        = (e != CRT_ENCODE_SRGB);
    p->set.white_nits = CRT_WHITE_DEFAULT;
    p->hdr_out        = (e != CRT_ENCODE_SRGB);
    p->encode         = e;
    p->scrgb_nits     = unit;
    p->peak_nits      = 1000;
    crt_plan_resolve(p);
}

/* Keeps the window alive: events pumped, the flat picture presented --
 * only while the pipelines are the swapchain's format (the HDR checks
 * build them for their own targets' formats). */
static void show(Ctx *c)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
    }
    if (c->win == NULL || c->shown == NULL || c->current != c->format) {
        return;
    }
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(c->dev);
    if (cmd == NULL) {
        return;
    }
    SDL_GPUTexture *swap = NULL;
    Uint32          w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, c->win, &swap, &w, &h)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }
    if (swap != NULL
        && crt_gpu_upload(c->g, cmd, c->shown->colour, NULL)) {
        CrtSharp l;
        crt_sharp_layout((int)w, (int)h, crt_pixel_aspect(2), &l);
        crt_gpu_draw_sharp(c->g, cmd, swap, &l, NULL);
    }
    SDL_SubmitGPUCommandBuffer(cmd);
}

/* (build 108) The pipelines for a target of `format`, remade only when it
 * changes. */
static bool use_format(Ctx *c, SDL_GPUTextureFormat format)
{
    if (c->current == format) {
        return true;
    }
    c->current = format;
    return crt_gpu_set_target(c->g, format);
}

/* (build 108) A pixel's three values as the check compares them: 8-bit
 * sRGB codes (SRGB); 10-bit PQ codes (PQ, and scRGB's linear taken back to
 * nits by `unit` -- the nits of its 1.0 -- then into Rec.2020 and PQ, so
 * both HDR encodes are held to the same 10-bit scale). */
static void check_codes(CrtEncode e, double unit, const double v[3],
                        int out[3])
{
    if (e == CRT_ENCODE_SRGB) {
        for (int ch = 0; ch < 3; ch++) {
            out[ch] = (int)lround(v[ch] * 255.0);
        }
        return;
    }
    if (e == CRT_ENCODE_PQ) {
        for (int ch = 0; ch < 3; ch++) {
            out[ch] = (int)lround(v[ch] * 1023.0);
        }
        return;
    }
    double nits[3] = { v[0] * unit, v[1] * unit, v[2] * unit };
    double r2020[3];
    crt_709_to_2020(nits, r2020);
    for (int ch = 0; ch < 3; ch++) {
        out[ch] = (int)lround(crt_pq_encode(r2020[ch] < 0.0 ? 0.0
                                                          : r2020[ch])
                              * 1023.0);
    }
}

static float half_to_float(Uint16 h)
{
    int   sign = (h >> 15) & 1;
    int   ex   = (h >> 10) & 31;
    int   man  = h & 1023;
    float v;
    if (ex == 0) {
        v = ldexpf((float)man, -24);
    } else if (ex == 31) {
        v = (man == 0) ? INFINITY : NAN;
    } else {
        v = ldexpf((float)(man + 1024), ex - 25);
    }
    return sign ? -v : v;
}

/* Bytes a texel of the formats read back here. */
static int texel_bytes(SDL_GPUTextureFormat f)
{
    return (f == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) ? 8 : 4;
}

/* Draws `f` (the CRT for `plan`, else the flat picture at `l`, encoded by
 * `enc`) into a w x h texture of `format` and reads back the rectangle
 * rx, ry, rw x rh as check codes (check_codes; `e` and `unit` say how).
 * NULL on failure. */
static int *draw_and_read(Ctx *c, const Frame *f, const CrtPlan *plan,
                          const CrtSharp *l, const CrtEncodeParams *enc,
                          SDL_GPUTextureFormat format, CrtEncode e,
                          double unit, int w, int h, int rx, int ry, int rw,
                          int rh)
{
    if (!use_format(c, format)) {
        say("  (no pipelines for texture format %d)\n", (int)format);
        return NULL;
    }
    SDL_GPUTextureCreateInfo ti;
    SDL_zero(ti);
    ti.type                 = SDL_GPU_TEXTURETYPE_2D;
    ti.format               = format;
    ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ti.width                = (Uint32)w;
    ti.height               = (Uint32)h;
    ti.layer_count_or_depth = 1;
    ti.num_levels           = 1;
    ti.sample_count         = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *target = SDL_CreateGPUTexture(c->dev, &ti);

    int bytes = texel_bytes(format);
    SDL_GPUTransferBufferCreateInfo bi;
    SDL_zero(bi);
    bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    bi.size  = (Uint32)(bytes * rw * rh);
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(c->dev, &bi);
    SDL_GPUCommandBuffer  *cmd = SDL_AcquireGPUCommandBuffer(c->dev);
    if (target == NULL || tb == NULL || cmd == NULL) {
        say("  (a %dx%d target or its read-back failed: %s)\n", w, h,
            SDL_GetError());
        if (cmd != NULL) {
            SDL_CancelGPUCommandBuffer(cmd);
        }
        if (tb != NULL) {
            SDL_ReleaseGPUTransferBuffer(c->dev, tb);
        }
        if (target != NULL) {
            SDL_ReleaseGPUTexture(c->dev, target);
        }
        return NULL;
    }
    bool drew = crt_gpu_upload(c->g, cmd, f->colour, plan);
    if (drew && plan != NULL) {
        crt_gpu_draw_crt(c->g, cmd, target);
    } else if (drew) {
        crt_gpu_draw_sharp(c->g, cmd, target, l, enc);
    }
    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src;
    SDL_zero(src);
    src.texture = target;
    src.x       = (Uint32)rx;
    src.y       = (Uint32)ry;
    src.w       = (Uint32)rw;
    src.h       = (Uint32)rh;
    src.d       = 1;
    SDL_GPUTextureTransferInfo dst;
    SDL_zero(dst);
    dst.transfer_buffer = tb;
    dst.pixels_per_row  = (Uint32)rw;
    dst.rows_per_layer  = (Uint32)rh;
    SDL_DownloadFromGPUTexture(pass, &src, &dst);
    SDL_EndGPUCopyPass(pass);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence != NULL) {
        SDL_WaitForGPUFences(c->dev, true, &fence, 1);
        SDL_ReleaseGPUFence(c->dev, fence);
    }

    int *codes = NULL;
    const u8 *map = SDL_MapGPUTransferBuffer(c->dev, tb, false);
    if (drew && map != NULL) {
        codes = malloc((size_t)rw * (size_t)rh * 3 * sizeof(int));
        for (long i = 0; i < (long)rw * rh; i++) {
            const u8 *px = map + i * bytes;
            double    v[3];
            if (format == SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM) {
                Uint32 word = (Uint32)px[0] | ((Uint32)px[1] << 8)
                            | ((Uint32)px[2] << 16) | ((Uint32)px[3] << 24);
                v[0] = (word & 1023u) / 1023.0;
                v[1] = ((word >> 10) & 1023u) / 1023.0;
                v[2] = ((word >> 20) & 1023u) / 1023.0;
            } else if (format == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) {
                for (int ch = 0; ch < 3; ch++) {
                    Uint16 hv = (Uint16)(px[ch * 2] | (px[ch * 2 + 1] << 8));
                    v[ch] = half_to_float(hv);
                }
            } else {
                bool bgra = (format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
                v[0] = px[bgra ? 2 : 0] / 255.0;
                v[1] = px[1] / 255.0;
                v[2] = px[bgra ? 0 : 2] / 255.0;
            }
            check_codes(e, unit, v, codes + i * 3);
        }
    }
    if (map != NULL) {
        SDL_UnmapGPUTransferBuffer(c->dev, tb);
    }
    SDL_ReleaseGPUTransferBuffer(c->dev, tb);
    SDL_ReleaseGPUTexture(c->dev, target);
    show(c);
    return codes;
}

/* How far two pictures' codes are apart; (build 108) and where: the box
 * of the pixels more than one code apart and the worst pixel, written
 * into `where` (empty when none is). */
typedef struct {
    int  worst;
    long differ;
    int  x0, y0, x1, y1;        /* pixels more than 1 apart: the box   */
    int  wx, wy, wa, wb;        /* the worst pixel and its two codes    */
} Diff;

static void compare(const int *a, const int *b, int w, int h, Diff *d)
{
    memset(d, 0, sizeof *d);
    d->x0 = w;
    d->y0 = h;
    d->x1 = -1;
    d->y1 = -1;
    for (long i = 0; i < 3L * w * h; i++) {
        int dd = abs(a[i] - b[i]);
        int x  = (int)((i / 3) % w);
        int y  = (int)((i / 3) / w);
        if (dd > d->worst) {
            d->worst = dd;
            d->wx    = x;
            d->wy    = y;
            d->wa    = a[i];
            d->wb    = b[i];
        }
        d->differ += (dd > 0) ? 1 : 0;
        if (dd > 1) {
            d->x0 = (x < d->x0) ? x : d->x0;
            d->y0 = (y < d->y0) ? y : d->y0;
            d->x1 = (x > d->x1) ? x : d->x1;
            d->y1 = (y > d->y1) ? y : d->y1;
        }
    }
}

/* One line under a FAIL: where, in the output's pixels (`ox`, `oy` the
 * rectangle's corner), and the picture's top for reading rows by. */
static void say_where(const Diff *d, int ox, int oy, const CrtPlan *p)
{
    if (d->worst <= 1) {
        return;
    }
    say("      more than 1 apart in rows %d-%d, columns %d-%d (the "
        "picture's rows %d-%d); the worst at %d,%d: GPU %d, CPU %d\n",
        oy + d->y0, oy + d->y1, ox + d->x0, ox + d->x1,
        (p != NULL) ? p->pic_y : 0,
        (p != NULL) ? p->pic_y + p->pic_h - 1 : 0, ox + d->wx, oy + d->wy,
        d->wa, d->wb);
}

/* The model's codes for a rectangle (crt_ref's write_crop's encode). */
static int *model_codes(const CrtPlan *p, const CrtSource *src, int x0,
                        int y0, int w, int h)
{
    float *lin = malloc(sizeof(float) * 3 * (size_t)w * (size_t)h);
    crt_render(p, src, x0, y0, w, h, lin);
    int *out = malloc((size_t)w * (size_t)h * 3 * sizeof(int));
    for (long i = 0; i < (long)w * h; i++) {
        double l[3] = { lin[i * 3], lin[i * 3 + 1], lin[i * 3 + 2] };
        double v[3];
        crt_encode_model(p, l, v);
        check_codes(p->encode, p->scrgb_nits, v, out + i * 3);
    }
    free(lin);
    return out;
}

/* The passes' CPU mirror's codes for a rectangle: the tables, the sums in
 * float and the across pass's encode (crt_encode_f). */
static int *mirror_codes(const CrtPlan *p, const Frame *f, int x0, int y0,
                         int w, int h)
{
    CrtTables t;
    if (!crt_tables_build(p, &t)) {
        return NULL;
    }
    float *lin = malloc(sizeof(float) * 3 * (size_t)w * (size_t)h);
    crt_tables_render(&t, f->colour, x0, y0, w, h, lin);
    int *out = malloc((size_t)w * (size_t)h * 3 * sizeof(int));
    for (long i = 0; i < (long)w * h; i++) {
        float  e[3];
        crt_encode_f(&t.enc, lin + i * 3, e);
        double v[3] = { e[0], e[1], e[2] };
        check_codes(t.enc.encode, p->scrgb_nits, v, out + i * 3);
    }
    crt_tables_free(&t);
    free(lin);
    return out;
}

/* crt_sharp_rgb.frag.hlsl in C: the blended code, then (build 108)
 * crt_encode_flat_f for the target's encode; (build 108a) the place in
 * the step in whole numbers, as the shader now takes it. */
static int *sharp_codes(const Frame *f, const CrtSharp *l,
                        const CrtEncodeParams *enc, double unit, int w,
                        int h)
{
    int *out = malloc((size_t)w * (size_t)h * 3 * sizeof(int));
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int  *o  = out + ((size_t)y * w + x) * 3;
            float code[3] = { 0.0f, 0.0f, 0.0f };
            /* (build 108a) The shader's exact place: nu / (2 w). */
            if (x >= l->x && y >= l->y && x < l->x + l->w
                && y < l->y + l->h) {
                int   nu = (2 * (x - l->x) + 1) * l->step_w - l->w;
                int   nv = (2 * (y - l->y) + 1) * l->step_h - l->h;
                int   cu = nu / (2 * l->w);
                int   cv = nv / (2 * l->h);
                float fu = (float)(nu - cu * 2 * l->w) / (float)(2 * l->w);
                float fv = (float)(nv - cv * 2 * l->h) / (float)(2 * l->h);
                int   cs[2], rs[2];
                cs[0] = (cu < l->step_w - 1) ? cu : l->step_w - 1;
                cs[1] = (cu + 1 < l->step_w - 1) ? cu + 1 : l->step_w - 1;
                rs[0] = (cv < l->step_h - 1) ? cv : l->step_h - 1;
                rs[1] = (cv + 1 < l->step_h - 1) ? cv + 1 : l->step_h - 1;
                float tex[2][2][3];
                for (int a = 0; a < 2; a++) {
                    for (int b = 0; b < 2; b++) {
                        int  sx = (cs[b] * SCREEN_W) / l->step_w;
                        int  sy = (rs[a] * SCREEN_H) / l->step_h;
                        Rgb8 v  = f->colour[sy * SCREEN_W + sx];
                        tex[a][b][0] = v.r / 255.0f;
                        tex[a][b][1] = v.g / 255.0f;
                        tex[a][b][2] = v.b / 255.0f;
                    }
                }
                for (int ch = 0; ch < 3; ch++) {
                    float top = tex[0][0][ch] + fu * (tex[0][1][ch]
                                                      - tex[0][0][ch]);
                    float bot = tex[1][0][ch] + fu * (tex[1][1][ch]
                                                      - tex[1][0][ch]);
                    code[ch] = top + fv * (bot - top);
                }
            }
            float  e[3];
            crt_encode_flat_f(enc, code, e);
            double v[3] = { e[0], e[1], e[2] };
            if (enc->encode == CRT_ENCODE_SRGB) {
                for (int ch = 0; ch < 3; ch++) {
                    o[ch] = (int)(e[ch] * 255.0f + 0.5f);
                }
            } else {
                check_codes(enc->encode, unit, v, o);
            }
        }
    }
    return out;
}

/* A picture's codes as a PNG: 8-bit sRGB, or (HDR) 16-bit Rec.2020 PQ
 * with a cICP chunk, as crt_ref writes its pq1000 crops. */
static void write_png(const char *out, const char *name, const int *codes,
                      int w, int h, bool pq)
{
    char path[640];
    snprintf(path, sizeof path, "%s/%s.png", out, name);
    int bytes = pq ? 2 : 1;
    u8 *px    = malloc((size_t)w * (size_t)h * 3 * (size_t)bytes);
    for (long i = 0; i < 3L * w * h; i++) {
        if (pq) {
            unsigned q = (unsigned)lround(codes[i] * 65535.0 / 1023.0);
            px[i * 2]     = (u8)(q >> 8);
            px[i * 2 + 1] = (u8)q;
        } else {
            px[i] = (u8)codes[i];
        }
    }
    static const PngCicp PQ2020 = { 9, 16, 0, 1 };
    if (!png_write(path, w, h, pq ? 16 : 8, 2, px,
                   (size_t)w * 3 * (size_t)bytes, NULL, 0,
                   pq ? &PQ2020 : NULL)) {
        say("  (cannot write %s)\n", path);
    }
    free(px);
}

/* One output checked: the crop against the model and the mirror, and for
 * the title the output whole against the mirror. `format` the target's,
 * `tag` crt_ref's name for the crop. */
static void check_one(Ctx *c, const char *out, const Frame *f,
                      const CrtSource *src, int ci, const CrtPlan *p,
                      SDL_GPUTextureFormat format, const char *tag)
{
    int sx = CROPS[ci].sx, sy = CROPS[ci].sy;
    int x0 = p->pic_x + (int)floor(sx * (double)p->pic_w / SCREEN_W);
    int x1 = p->pic_x + (int)ceil((sx + CROPS[ci].sw) * (double)p->pic_w
                                  / SCREEN_W);
    int y0 = p->pic_y + sy * p->k;
    int w  = x1 - x0;
    int h  = CROPS[ci].sh * p->k;
    bool whole = (ci == 0);
    int *gpu = whole
        ? draw_and_read(c, f, p, NULL, NULL, format, p->encode,
                        p->scrgb_nits, p->out_w, p->out_h, 0, 0, p->out_w,
                        p->out_h)
        : draw_and_read(c, f, p, NULL, NULL, format, p->encode,
                        p->scrgb_nits, p->out_w, p->out_h, x0, y0, w, h);
    if (gpu == NULL) {
        verdict(false, "%s: the GPU drew nothing", tag);
        return;
    }
    int *crop = gpu;
    if (whole) {
        crop = malloc((size_t)w * (size_t)h * 3 * sizeof(int));
        for (int y = 0; y < h; y++) {
            memcpy(crop + (size_t)y * w * 3,
                   gpu + (((size_t)(y0 + y) * p->out_w) + x0) * 3,
                   (size_t)w * 3 * sizeof(int));
        }
    }
    const char *unit = (p->encode == CRT_ENCODE_SRGB) ? "8-bit sRGB"
                                                      : "10-bit PQ";
    int *model  = model_codes(p, src, x0, y0, w, h);
    int *mirror = mirror_codes(p, f, x0, y0, w, h);
    Diff dm, dr;
    compare(crop, model, w, h, &dm);
    compare(crop, mirror, w, h, &dr);
    verdict(dm.worst <= 1 && dr.worst <= 1, "%s %dx%d: model worst %d (%ld "
            "of %ld %s codes differ), mirror worst %d (%ld)", tag, w, h,
            dm.worst, dm.differ, 3L * w * h, unit, dr.worst, dr.differ);
    say_where((dm.worst > dr.worst) ? &dm : &dr, x0, y0, p);
    write_png(out, tag, crop, w, h, p->encode != CRT_ENCODE_SRGB);
    if (whole) {
        int *all = mirror_codes(p, f, 0, 0, p->out_w, p->out_h);
        Diff da;
        compare(gpu, all, p->out_w, p->out_h, &da);
        verdict(da.worst <= 1, "%s whole %dx%d against the mirror: worst "
                "%d, %ld of %ld codes differ", tag, p->out_w, p->out_h,
                da.worst, da.differ, 3L * p->out_w * p->out_h);
        say_where(&da, 0, 0, p);
        free(all);
        free(crop);
    }
    free(model);
    free(mirror);
    free(gpu);
}

static void check_crops(Ctx *c, const char *out)
{
    say("\n## 1-2. The CRT in SDR: the crops against the model and the "
        "mirror; the title whole against the mirror\n\n");
    for (int ci = 0; ci < CROP_COUNT; ci++) {
        static Frame f;
        if (!load_frame(CROPS[ci].png, &f)) {
            verdict(false, "cannot read %s%s", frame_name(CROPS[ci].png),
                    made_by(CROPS[ci].png));
            continue;
        }
        c->shown = &f;
        CrtSource *src = source_of(&f);
        for (int si = 0; si < SCREEN_COUNT; si++) {
            const Screen *s   = &SCREENS[si];
            CrtSubpixels  sub = (s->h == 2400) ? CRT_SUBPIXELS_OTHER
                                               : CRT_SUBPIXELS_RGB;
            for (int m = 0; m < CRT_MASK_COUNT; m++) {
                for (int scan = (m == 0) ? 0 : 1; scan <= 1; scan++) {
                    CrtPlan p;
                    plan_for(&p, s->w, s->h, scan != 0, (CrtMask)m, sub,
                             CRT_ENCODE_SRGB, 0.0);
                    if (p.set.mask != (CrtMask)m) {
                        continue;
                    }
                    char tag[160];
                    snprintf(tag, sizeof tag, "%s_%s_%s_%s_sdr",
                             CROPS[ci].name, s->name,
                             (m == 0 && scan) ? "scanlines" : MASK_KEY[m],
                             SUB_KEY[sub]);
                    check_one(c, out, &f, src, ci, &p, c->format, tag);
                }
            }
        }
        free(src);
        c->shown = NULL;
    }
}

/* (build 108) Phase 3: crt_ref's pq1000 crops -- the P16 (3840x2400,
 * OTHER, the shadow mask) and the PA32UCX (4K RGB, the grille), PEAK 1000,
 * WHITE 200 -- drawn by the across pass's HDR10 encode into an R10G10B10A2
 * target (HDR10's swapchain format), and the P16's also by the scRGB
 * encode into an RGBA16 float target (HDR_EXTENDED_LINEAR's), its 1.0 at
 * 203 nits as on Wayland; each against the model and the mirror in 10-bit
 * PQ codes, and the title whole against the mirror. */
static void check_hdr_crops(Ctx *c, const char *out)
{
    say("\n## 3. The CRT in HDR: crt_ref's pq1000 crops, HDR10 (10-bit) and "
        "scRGB (16-bit float), in 10-bit PQ codes\n\n");
    bool pq_ok = SDL_GPUTextureSupportsFormat(
        c->dev, SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM,
        SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    bool scrgb_ok = SDL_GPUTextureSupportsFormat(
        c->dev, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
        SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    for (int ci = 0; ci < CROP_COUNT; ci++) {
        static Frame f;
        if (!load_frame(CROPS[ci].png, &f)) {
            verdict(false, "cannot read %s%s", frame_name(CROPS[ci].png),
                    made_by(CROPS[ci].png));
            continue;
        }
        c->shown = &f;
        CrtSource *src = source_of(&f);
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
                bool ok = (e == CRT_ENCODE_PQ) ? pq_ok : scrgb_ok;
                char tag[160];
                snprintf(tag, sizeof tag, "%s_%s_%s_%s_%s1000",
                         CROPS[ci].name, s->name, MASK_KEY[m], SUB_KEY[sub],
                         (e == CRT_ENCODE_PQ) ? "pq" : "scrgb");
                if (!ok) {
                    verdict(false, "%s: this GPU cannot draw into %s", tag,
                            (e == CRT_ENCODE_PQ) ? "R10G10B10A2"
                                                 : "RGBA16 float");
                    continue;
                }
                CrtPlan p;
                plan_for(&p, s->w, s->h, true, m, sub, (CrtEncode)e, 203.0);
                check_one(c, out, &f, src, ci, &p,
                          (e == CRT_ENCODE_PQ)
                              ? SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM
                              : SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
                          tag);
            }
        }
        free(src);
        c->shown = NULL;
    }
}

static void check_sharp(Ctx *c, const char *out)
{
    static const struct {
        const char *name;
        int         w, h;
    } OUTS[] = {
        { "window_960x672", 960, 672 },
        { "p16_window_1824x1277", 1824, 1277 },
        { "1080p", 1920, 1080 },
        { "1440p", 2560, 1440 },
        { "4k", 3840, 2160 },
        { "3840x2400", 3840, 2400 },
    };
    static const char *const ASPECT[3] = { "square", "4_3", "tv" };
    say("\n## 4. The CRT off: the flat picture, whole, against its mirror\n\n");
    static Frame f;
    if (!load_frame(CROPS[0].png, &f)) {
        verdict(false, "cannot read %s%s", frame_name(CROPS[0].png),
                made_by(CROPS[0].png));
        return;
    }
    c->shown = &f;
    CrtEncodeParams sdr;
    crt_encode_params(CRT_ENCODE_SRGB, 0.0, 0.0, &sdr);
    for (size_t oi = 0; oi < sizeof OUTS / sizeof OUTS[0]; oi++) {
        for (int a = 0; a < 3; a++) {
            int      w = OUTS[oi].w, h = OUTS[oi].h;
            CrtSharp l;
            crt_sharp_layout(w, h, crt_pixel_aspect(a), &l);
            int *gpu = draw_and_read(c, &f, NULL, &l, NULL, c->format,
                                     CRT_ENCODE_SRGB, 0.0, w, h, 0, 0, w, h);
            if (gpu == NULL) {
                verdict(false, "flat %s %s: the GPU drew nothing",
                        OUTS[oi].name, ASPECT[a]);
                continue;
            }
            int *cpu = sharp_codes(&f, &l, &sdr, 0.0, w, h);
            Diff d;
            compare(gpu, cpu, w, h, &d);
            verdict(d.worst <= 1, "flat %s %s (frame %dx%d at %d,%d, step "
                    "%dx%d): worst %d, %ld of %ld codes differ",
                    OUTS[oi].name, ASPECT[a], l.w, l.h, l.x, l.y, l.step_w,
                    l.step_h, d.worst, d.differ, 3L * w * h);
            say_where(&d, 0, 0, NULL);
            if (a == 2 && oi == 2) {
                write_png(out, "flat_title_1080p_tv", gpu, w, h, false);
            }
            free(cpu);
            free(gpu);
        }
    }

    /* (build 108) HDR ON with the CRT off: the same picture at WHITE 200,
     * HDR10 (R10G10B10A2) at 1080p and the P16's 3840x2400, and scRGB
     * (RGBA16 float, 1.0 at 203 nits) at the P16's, TV aspect. */
    static const struct {
        int       oi;
        CrtEncode e;
    } HDR[] = {
        { 2, CRT_ENCODE_PQ },
        { 5, CRT_ENCODE_PQ },
        { 5, CRT_ENCODE_SCRGB },
    };
    for (size_t hi = 0; hi < sizeof HDR / sizeof HDR[0]; hi++) {
        int       w = OUTS[HDR[hi].oi].w, h = OUTS[HDR[hi].oi].h;
        CrtEncode e = HDR[hi].e;
        SDL_GPUTextureFormat format = (e == CRT_ENCODE_PQ)
            ? SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM
            : SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        const char *name = (e == CRT_ENCODE_PQ) ? "HDR10" : "scRGB";
        if (!SDL_GPUTextureSupportsFormat(c->dev, format,
                                          SDL_GPU_TEXTURETYPE_2D,
                                          SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) {
            verdict(false, "flat %s tv %s: this GPU cannot draw into its "
                    "format", OUTS[HDR[hi].oi].name, name);
            continue;
        }
        CrtSharp        l;
        CrtEncodeParams enc;
        crt_sharp_layout(w, h, crt_pixel_aspect(2), &l);
        crt_encode_params(e, CRT_WHITE_DEFAULT, 203.0, &enc);
        int *gpu = draw_and_read(c, &f, NULL, &l, &enc, format, e, 203.0, w,
                                 h, 0, 0, w, h);
        if (gpu == NULL) {
            verdict(false, "flat %s tv %s: the GPU drew nothing",
                    OUTS[HDR[hi].oi].name, name);
            continue;
        }
        int *cpu = sharp_codes(&f, &l, &enc, 203.0, w, h);
        Diff d;
        compare(gpu, cpu, w, h, &d);
        verdict(d.worst <= 1, "flat %s tv %s, WHITE %d: worst %d, %ld of %ld "
                "10-bit PQ codes differ", OUTS[HDR[hi].oi].name, name,
                CRT_WHITE_DEFAULT, d.worst, d.differ, 3L * w * h);
        say_where(&d, 0, 0, NULL);
        free(cpu);
        free(gpu);
    }
    c->shown = NULL;
}

int main(int argc, char **argv)
{
    const char *out = (argc >= 2) ? argv[1] : "route_out/crt_gpu_check";
    make_dir("route_out");
    make_dir(out);
    char path[512];
    snprintf(path, sizeof path, "%s/report.txt", out);
    /* (build 113) "wb": the same bytes on every OS (Windows's text mode
     * writes "\r\n") */
    g_report = fopen(path, "wb");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    Ctx c;
    memset(&c, 0, sizeof c);
    c.dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV
                                | SDL_GPU_SHADERFORMAT_DXIL
                                | SDL_GPU_SHADERFORMAT_MSL,
                                SDL_getenv("PS2_GPU_DEBUG") != NULL, NULL);
    if (c.dev == NULL) {
        say("no SDL_GPU device: %s\n", SDL_GetError());
        return 2;
    }
    c.g = crt_gpu_create(c.dev, "shaders");
    if (c.g == NULL) {
        say("no CRT shaders in shaders/ (run `make shaders` first)\n");
        return 2;
    }
    c.win = SDL_CreateWindow("crt_gpu_check", SCREEN_W * 3, SCREEN_H * 3,
                             SDL_WINDOW_RESIZABLE
                             | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (c.win != NULL && SDL_ClaimWindowForGPUDevice(c.dev, c.win)) {
        SDL_SetGPUSwapchainParameters(c.dev, c.win,
                                      SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                      SDL_GPU_PRESENTMODE_VSYNC);
        c.format = SDL_GetGPUSwapchainTextureFormat(c.dev, c.win);
    } else {
        say("(no window for SDL_GPU: %s; RGBA8 targets)\n", SDL_GetError());
        if (c.win != NULL) {
            SDL_DestroyWindow(c.win);
            c.win = NULL;
        }
        c.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }
    int v = SDL_GetVersion();
    say("# The CRT chain on the GPU against the model (crt_gpu_check)\n\n"
        "SDL %d.%d.%d, SDL_GPU %s, %s shaders, swapchain format %d\n",
        SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v),
        SDL_VERSIONNUM_MICRO(v), SDL_GetGPUDeviceDriver(c.dev),
        crt_gpu_shader_format(c.g), (int)c.format);
    if (c.format != SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM
        && c.format != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) {
        say("the swapchain is not 8-bit UNORM BGRA or RGBA: checking in "
            "RGBA8 targets instead\n");
        c.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }
    c.current = SDL_GPU_TEXTUREFORMAT_INVALID;
    if (!use_format(&c, c.format)) {
        say("the pipelines could not be made\n");
        return 2;
    }

    check_crops(&c, out);
    check_hdr_crops(&c, out);
    check_sharp(&c, out);
    say("\n%d checks passed, %d failed (%s)\n", g_passed, g_failed, path);

    if (g_report != NULL) {
        fclose(g_report);
    }
    crt_gpu_destroy(c.g);
    if (c.win != NULL) {
        SDL_ReleaseWindowFromGPUDevice(c.dev, c.win);
        SDL_DestroyWindow(c.win);
    }
    SDL_DestroyGPUDevice(c.dev);
    SDL_Quit();
    return (g_failed == 0) ? 0 : 1;
}
