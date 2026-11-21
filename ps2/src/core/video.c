#include "video.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void video_init(Video *v, const TileSet *ts)
{
    memset(v, 0, sizeof *v);
    v->tileset    = ts;
    v->sprite_cap = MAX_SPRITES;
    v->plane_rows = PLANE_ROWS;
    v->backdrop   = 0;
    palette_clear(&v->palette);
}

void video_set_tile(Video *v, LayerId layer, int col, int row, Cell cell)
{
    /* The plane wraps, as the hardware nametable does. Clamping instead
     * silently dropped anything a scrolled screen placed past the edge:
     * a window opened relative to the camera straddles the wrap as soon
     * as the camera is far enough along. */
    v->plane[layer][row & (v->plane_rows - 1)][col & (PLANE_COLS - 1)] =
        cell;
}

void video_fill(Video *v, LayerId layer, Cell cell)
{
    for (int row = 0; row < PLANE_ROWS_MAX; row++) {
        for (int col = 0; col < PLANE_COLS; col++) {
            v->plane[layer][row][col] = cell;
        }
    }
}

void video_scroll(Video *v, LayerId layer, int x, int y)
{
    v->scroll_x[layer] = (s16)x;
    v->scroll_y[layer] = (s16)y;
}

void video_set_plane_rows(Video *v, int rows)
{
    v->plane_rows = (rows == PLANE_ROWS_MAX) ? PLANE_ROWS_MAX : PLANE_ROWS;
}

void video_pictures_clear(Video *v)
{
    v->picture_count = 0;
}

bool video_picture_bind(Video *v, const VideoPicture *b)
{
    if (v->picture_count >= VIDEO_PICTURES_MAX || b->picture == NULL
        || b->picture->rgba == NULL || b->count == 0
        || b->layer >= LAYER_COUNT) {
        return false;
    }
    VideoPicture k = *b;
    /* The cells the picture has: a binding never reads past it. */
    int cols = (k.picture->w + TILE_W - 1) / TILE_W;
    int rows = (k.picture->h + TILE_H - 1) / TILE_H;
    if (k.col_lo < 0) {
        k.col_lo = 0;
    }
    if (k.row_lo < 0) {
        k.row_lo = 0;
    }
    if (k.col_hi > cols - 1) {
        k.col_hi = (s16)(cols - 1);
    }
    if (k.row_hi > rows - 1) {
        k.row_hi = (s16)(rows - 1);
    }
    if (k.col_lo > k.col_hi || k.row_lo > k.row_hi) {
        return false;
    }
    v->pictures[v->picture_count++] = k;
    return true;
}

void video_sprites_begin(Video *v)
{
    v->sprite_count = 0;
}

bool video_sprite_push(Video *v, const Sprite *s)
{
    if (v->sprite_count >= v->sprite_cap) {
        return false;
    }
    v->sprite[v->sprite_count++] = *s;
    return true;
}

/* Returns the palette index of one tile pixel, or 0 for transparent. */
static u8 tile_pixel(const TileSet *ts, u16 tile, int x, int y,
                     bool flip_h, bool flip_v)
{
    if (ts == NULL || tile >= (u16)ts->count) {
        return 0;
    }
    int sx = flip_h ? (TILE_W - 1 - x) : x;
    int sy = flip_v ? (TILE_H - 1 - y) : y;

    return ts->tiles[tile].px[sy * TILE_W + sx];
}

/* (build 140) Every pixel goes into both planes: its index, and its colour
 * from `lut`, the frame's palette through the DAC ladder (video.h). */
static void put_pixel(Framebuffer *fb, const Rgb8 *lut, int at, u8 index)
{
    fb->px[at]  = index;
    fb->rgb[at] = lut[index];
    fb->pic[at] = 0;
}

#define ENTRY_BIT(e) ((u64)1 << (e))

/* (build 142) The binding a plane cell's pixels take their colour from:
 * the latest whose layer, tile range and cells hold the cell at plane
 * column `col`, row `row`; -1 for none. `*pc`, `*pr` are the cell's place
 * in the picture, in cells. */
static int binding_at(const Video *v, LayerId layer, const Cell *cell,
                      int col, int row, int *pc, int *pr)
{
    for (int i = v->picture_count - 1; i >= 0; i--) {
        const VideoPicture *b = &v->pictures[i];
        if (b->layer != (u8)layer || cell->tile < b->first_tile
            || cell->tile - b->first_tile >= b->count) {
            continue;
        }
        int rc = ((col - b->col) % PLANE_COLS + PLANE_COLS) % PLANE_COLS;
        int rr = ((row - b->row) % v->plane_rows + v->plane_rows)
                 % v->plane_rows;
        if (rc < b->col_lo || rc > b->col_hi || rr < b->row_lo
            || rr > b->row_hi) {
            continue;
        }
        *pc = rc;
        *pr = rr;
        return i;
    }
    return -1;
}

/* (build 142) One pixel from a picture: the colour its picture has at the
 * place (alpha 0: nothing drawn; part alpha over what is under), or the
 * glow's where an effect has lit the pixel's entry; the index the tile's,
 * the palette path's reference (until 146a). (build 146) The pixel's
 * entry for the glow is its slot map's (art.h), in the cell's palette
 * line: a picture without one, or a pixel its map leaves 0, no effect
 * recolours. Until 146 it was
 * the tile's index, which nothing but the radar's slot 5 was ever lit at
 * (counted over every route). (build 146a) There is no tile under a
 * picture now: the index plane takes that entry -- the cell's line and
 * the slot, 0 where none -- so it says which line the pixel is in (the
 * dream's fade by line reads it) and what an effect would write there;
 * `pic` says the colour is the picture's. */
static void picture_pixel(const Video *v, Framebuffer *fb, const Rgb8 *lut,
                          int binding, int at, u8 index, int x, int y)
{
    const Picture *p = v->pictures[binding].picture;
    if (x >= p->w || y >= p->h) {
        return;
    }
    size_t    i = (size_t)y * (size_t)p->w + (size_t)x;
    const u8 *q = p->rgba + i * 4;
    if (q[3] == 0) {
        return;
    }
    u8 slot  = (p->slots != NULL) ? p->slots[i] : 0;
    u8 entry = (u8)((index & ~(PAL_ENTRIES - 1)) | slot);
    Rgb8 c;
    if (slot != 0 && (v->colours.lit & ENTRY_BIT(entry)) != 0) {
        c = lut[entry];
    } else if (q[3] == 255) {
        c.r = q[0];
        c.g = q[1];
        c.b = q[2];
    } else {
        Rgb8 under = fb->rgb[at];
        c.r = (u8)((q[0] * q[3] + under.r * (255 - q[3]) + 127) / 255);
        c.g = (u8)((q[1] * q[3] + under.g * (255 - q[3]) + 127) / 255);
        c.b = (u8)((q[2] * q[3] + under.b * (255 - q[3]) + 127) / 255);
    }
    fb->px[at]  = entry;
    fb->rgb[at] = c;
    fb->pic[at] = (u8)(binding + 1);
    fb->picture_pixels++;
}

static void draw_plane(const Video *v, Framebuffer *fb, const Rgb8 *lut,
                       LayerId layer, bool want_prio)
{
    /* Planes wrap, as the hardware nametable does. */
    const int plane_w = PLANE_COLS * TILE_W;
    const int plane_h = v->plane_rows * TILE_H;

    /* (build 142) Only a layer with a picture bound looks for one. */
    bool pictures = false;
    for (int i = 0; i < v->picture_count; i++) {
        if (v->pictures[i].layer == (u8)layer) {
            pictures = true;
        }
    }

    for (int py = 0; py < SCREEN_H; py++) {
        int wy = ((py + v->scroll_y[layer]) % plane_h + plane_h) % plane_h;
        int row = wy / TILE_H;
        int ty  = wy % TILE_H;

        for (int px = 0; px < SCREEN_W; px++) {
            int wx = ((px + v->scroll_x[layer]) % plane_w + plane_w) % plane_w;
            int col = wx / TILE_W;
            const Cell *cell = &v->plane[layer][row][col];

            if ((bool)cell->prio != want_prio) {
                continue;
            }

            u8 idx = tile_pixel(v->tileset, cell->tile, wx % TILE_W, ty,
                                cell->flip_h, cell->flip_v);
            if (pictures) {
                int pc = 0;
                int pr = 0;
                int b  = binding_at(v, layer, cell, col, row, &pc, &pr);
                if (b >= 0) {
                    /* (build 151) A pixel the reveal mask hides draws
                     * nothing: its place in the tile, before the flip. */
                    int hx = cell->flip_h ? TILE_W - 1 - wx % TILE_W
                                          : wx % TILE_W;
                    int hy = cell->flip_v ? TILE_H - 1 - ty : ty;
                    if (((v->pictures[b].hidden >> (hy * TILE_W + hx)) & 1)
                        != 0) {
                        continue;
                    }
                    picture_pixel(v, fb, lut, b, py * SCREEN_W + px,
                                  (u8)(cell->pal * PAL_ENTRIES + idx),
                                  pc * TILE_W + wx % TILE_W,
                                  pr * TILE_H + ty);
                    continue;
                }
            }
            if (idx == 0) {
                continue;
            }
            put_pixel(fb, lut, py * SCREEN_W + px,
                      (u8)(cell->pal * PAL_ENTRIES + idx));
        }
    }
}

static void draw_sprite(const Video *v, Framebuffer *fb, const Rgb8 *lut,
                        const Sprite *s)
{
    int w = s->w * TILE_W;
    int h = s->h * TILE_H;

    for (int dy = 0; dy < h; dy++) {
        int py = s->y + dy;
        if (py < 0 || py >= SCREEN_H) {
            continue;
        }

        for (int dx = 0; dx < w; dx++) {
            int px = s->x + dx;
            if (px < 0 || px >= SCREEN_W) {
                continue;
            }

            /* Source pixel within the sprite, before per-tile addressing. */
            int sx = s->flip_h ? (w - 1 - dx) : dx;
            int sy = s->flip_v ? (h - 1 - dy) : dy;

            /* Mega Drive sprites store tiles in column-major order. */
            u16 tile = (u16)(s->tile + (sx / TILE_W) * s->h + (sy / TILE_H));
            u8 idx = tile_pixel(v->tileset, tile, sx % TILE_W, sy % TILE_H,
                                false, false);
            if (idx == 0) {
                continue;
            }
            put_pixel(fb, lut, py * SCREEN_W + px,
                      (u8)(s->pal * PAL_ENTRIES + idx));
        }
    }
}

static void draw_sprites(const Video *v, Framebuffer *fb, const Rgb8 *lut,
                         bool want_prio)
{
    /* Later entries draw over earlier ones, matching link-field order. */
    for (int i = v->sprite_count - 1; i >= 0; i--) {
        if ((bool)v->sprite[i].prio != want_prio) {
            continue;
        }
        draw_sprite(v, fb, lut, &v->sprite[i]);
    }
}

/* ------------------------------------------------------------------ */
/* (build 141) The colour path: the palette operations on both paths    */
/* ------------------------------------------------------------------ */

#define NOT_LOADED   0xFFFF     /* no Cram has bit 0 set */

static void picture_entry(Video *v, int e, Cram c)
{
    v->colours.picture[e] = cram_to_rgb8(c);
    v->colours.lit &= ~ENTRY_BIT(e);
}

void video_colours_clear(Video *v, Palette *dest)
{
    palette_clear(dest);
    for (int e = 0; e < PAL_TOTAL; e++) {
        v->colours.picture[e] = cram_to_rgb8(0);
    }
    v->colours.lit = 0;
}

/* palette_load_entry into a palette of NOT_LOADED words: the entries it
 * wrote are the ones that changed. */
static bool load_marked(Palette *tmp, int line, int index)
{
    for (int e = 0; e < PAL_TOTAL; e++) {
        tmp->entry[e] = NOT_LOADED;
    }
    return palette_load_entry(tmp, line, index);
}

bool video_colours_load(Video *v, Palette *dest, int line, int index)
{
    Palette tmp;
    if (!load_marked(&tmp, line, index)) {
        return false;
    }
    for (int e = 0; e < PAL_TOTAL; e++) {
        if (tmp.entry[e] != NOT_LOADED) {
            dest->entry[e] = tmp.entry[e];
            picture_entry(v, e, tmp.entry[e]);
        }
    }
    return true;
}

void video_colours_copy(Video *v, Palette *dest, const Palette *src,
                        int line, int lines)
{
    for (int e = line * PAL_ENTRIES;
         e < (line + lines) * PAL_ENTRIES && e < PAL_TOTAL; e++) {
        dest->entry[e] = src->entry[e];
        picture_entry(v, e, src->entry[e]);
    }
}

void video_colours_set(Video *v, Palette *dest, int entry, Cram c)
{
    if (entry < 0 || entry >= PAL_TOTAL) {
        return;
    }
    dest->entry[entry] = c;
    picture_entry(v, entry, c);
}

void video_glow(Video *v, int entry, Cram c)
{
    if (entry < 0 || entry >= PAL_TOTAL) {
        return;
    }
    v->palette.entry[entry] = c;
    v->colours.glow[entry]  = cram_to_rgb8(c);
    v->colours.lit         |= ENTRY_BIT(entry);
}

bool video_glow_load(Video *v, int line, int index)
{
    Palette tmp;
    if (!load_marked(&tmp, line, index)) {
        return false;
    }
    for (int e = 0; e < PAL_TOTAL; e++) {
        if (tmp.entry[e] != NOT_LOADED) {
            video_glow(v, e, tmp.entry[e]);
        }
    }
    return true;
}

void video_backdrop(Video *v, Cram c)
{
    video_glow(v, 0, c);
}

static Rgb8 entry_colour(const VideoColours *k, int e)
{
    return ((k->lit & ENTRY_BIT(e)) != 0) ? k->glow[e] : k->picture[e];
}

bool video_backdrop_is(const Video *v, Cram c)
{
    Rgb8 a = entry_colour(&v->colours, 0);
    Rgb8 b = cram_to_rgb8(c);
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

void video_target_from_shown(Video *v, Palette *target)
{
    palette_copy(target, &v->palette);
    for (int e = 0; e < PAL_TOTAL; e++) {
        v->colours.picture[e] = entry_colour(&v->colours, e);
    }
    v->colours.lit = 0;
}

static void fade_clear(VideoColours *k)
{
    memset(&k->fade, 0, sizeof k->fade);
    memset(&k->under, 0, sizeof k->under);
    k->covered = false;
}

void video_shown_from_target(Video *v, const Palette *target)
{
    palette_copy(&v->palette, target);
    v->colours.lit = 0;
    fade_clear(&v->colours);
}

void video_fade_begin(Video *v, VideoFx kind, u8 lines)
{
    for (int e = 0; e < PAL_TOTAL; e++) {
        if ((lines & (1u << (e / PAL_ENTRIES))) == 0) {
            continue;
        }
        if (kind == VIDEO_FX_FROM_BLACK) {
            v->palette.entry[e] = 0;
        } else if (kind == VIDEO_FX_FROM_WHITE) {
            v->palette.entry[e] = 0xEEE;
        }
    }
    VideoColours *k = &v->colours;
    fade_clear(k);
    k->fade.kind  = kind;
    k->fade.step  = 0;
    k->fade.lines = lines;
}

/* What a whole-frame effect leaves the screen when it shows one colour
 * everywhere: a fade out finished, the white field, a fade in not yet
 * begun. */
static bool fade_cover(const VideoColours *k, Rgb8 *out)
{
    const VideoFade *f = &k->fade;
    if (f->lines != VIDEO_LINES_ALL) {
        return false;
    }
    bool black = (f->kind == VIDEO_FX_TO_BLACK && f->step >= VIDEO_FADE_STEPS)
              || (f->kind == VIDEO_FX_FROM_BLACK && f->step == 0);
    bool white = (f->kind == VIDEO_FX_TO_WHITE && f->step >= VIDEO_FADE_STEPS)
              || (f->kind == VIDEO_FX_FROM_WHITE && f->step == 0)
              || f->kind == VIDEO_FX_WHITE;
    if (!black && !white) {
        return false;
    }
    *out = cram_to_rgb8(black ? 0x000 : 0xEEE);
    return true;
}

/* A fade out starts from what is shown: the frame as composed, the screen
 * all one colour, or the frame under an effect not finished. */
static void fade_out_begin(VideoColours *k, VideoFx kind)
{
    Rgb8 cover;
    if (fade_cover(k, &cover)) {
        fade_clear(k);
        k->covered = true;
        k->cover   = cover;
    } else if (k->fade.kind == VIDEO_FX_NONE) {
        fade_clear(k);
    } else {
        static bool told;
        if (!told && (k->under.kind != VIDEO_FX_NONE || k->covered)) {
            told = true;
            fprintf(stderr, "video: a fade out over two unfinished effects; "
                            "the colour path keeps the later\n");
        }
        k->under   = k->fade;
        k->covered = false;
    }
    k->fade.kind  = kind;
    k->fade.step  = 0;
    k->fade.lines = VIDEO_LINES_ALL;
}

void video_fade_step(Video *v, VideoFx kind, const Palette *target)
{
    switch (kind) {
        case VIDEO_FX_TO_BLACK: {
            palette_fade_out_step(&v->palette);
            break;
        }
        case VIDEO_FX_FROM_BLACK: {
            palette_fade_in_step(&v->palette, target);
            break;
        }
        case VIDEO_FX_TO_WHITE: {
            palette_fade_to_white_step(&v->palette);
            break;
        }
        case VIDEO_FX_FROM_WHITE: {
            palette_fade_from_white_step(&v->palette, target);
            break;
        }
        default: {
            return;
        }
    }

    VideoColours *k = &v->colours;
    bool out = (kind == VIDEO_FX_TO_BLACK || kind == VIDEO_FX_TO_WHITE);
    if (out && (k->fade.kind != kind || k->fade.step >= VIDEO_FADE_STEPS)) {
        fade_out_begin(k, kind);
    } else if (!out && k->fade.kind != kind) {
        static bool told;
        if (!told) {
            told = true;
            fprintf(stderr, "video: a fade in stepped with no begin\n");
        }
        u8 lines = VIDEO_LINES_ALL;
        fade_clear(k);
        k->fade.kind  = kind;
        k->fade.lines = lines;
    }
    if (k->fade.step < VIDEO_FADE_STEPS) {
        k->fade.step++;
    }
    /* A fade in at its last step shows the pictures as they are. */
    if (!out && k->fade.step >= VIDEO_FADE_STEPS) {
        fade_clear(k);
    }
}

void video_flash_white(Video *v)
{
    for (int e = 0; e < PAL_TOTAL; e++) {
        v->palette.entry[e] = 0xEEE;
    }
    VideoColours *k = &v->colours;
    fade_clear(k);
    k->fade.kind  = VIDEO_FX_WHITE;
    k->fade.lines = VIDEO_LINES_ALL;
}

/* The fades as colour maths (docs/palette_effects.md section 2): each
 * channel's level on the DAC ladder (colour_level), stepped one level a
 * frame toward the far end, one channel after another in the ROM's order;
 * a channel takes as many frames as whole levels it has to go. `from` is
 * where the fade starts, `to` where it ends. */
static Rgb8 stepped(Rgb8 from, Rgb8 to, const int order[3], int step)
{
    float f[3] = { colour_level(from.r), colour_level(from.g),
                   colour_level(from.b) };
    float t[3] = { colour_level(to.r), colour_level(to.g),
                   colour_level(to.b) };
    float out[3];
    for (int n = 0; n < 3; n++) {
        int   ch   = order[n];
        float d    = t[ch] - f[ch];
        float dist = (d < 0.0f) ? -d : d;
        int   need = (int)ceilf(dist - 1e-4f);
        if (step >= need) {
            out[ch] = t[ch];
            step   -= need;
        } else {
            out[ch] = f[ch] + ((d < 0.0f) ? -(float)step : (float)step);
            step    = 0;
        }
    }
    Rgb8 c;
    c.r = level_colour(out[0]);
    c.g = level_colour(out[1]);
    c.b = level_colour(out[2]);
    return c;
}

static const int RED_FIRST[3]  = { 0, 1, 2 };
static const int BLUE_FIRST[3] = { 2, 1, 0 };

/* One effect on one pixel: `shown` is what the pixel shows before it (a
 * fade out starts from it), `own` its picture's colour (a fade in comes
 * up to it). */
static Rgb8 fx_apply(const VideoFade *f, Rgb8 shown, Rgb8 own)
{
    Rgb8 black = cram_to_rgb8(0x000);
    Rgb8 white = cram_to_rgb8(0xEEE);
    switch (f->kind) {
        case VIDEO_FX_TO_BLACK: {
            return stepped(shown, black, RED_FIRST, f->step);
        }
        case VIDEO_FX_FROM_BLACK: {
            return stepped(black, own, BLUE_FIRST, f->step);
        }
        case VIDEO_FX_TO_WHITE: {
            return stepped(shown, white, RED_FIRST, f->step);
        }
        case VIDEO_FX_FROM_WHITE: {
            return stepped(white, own, BLUE_FIRST, f->step);
        }
        case VIDEO_FX_WHITE: {
            return white;
        }
        default: {
            return shown;
        }
    }
}

/* The whole-frame effect on the colour plane, pixel by pixel, after the
 * pictures are composed and before the port's own overlay. Which picture
 * a pixel belongs to, for an effect on one picture (the dream's), is its
 * palette line while the art is indexed. */
static void frame_effect(const Video *v, Framebuffer *fb)
{
    const VideoColours *k = &v->colours;
    if (k->fade.kind == VIDEO_FX_NONE) {
        return;
    }
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        u8   bit  = (u8)(1u << ((fb->px[i] / PAL_ENTRIES) & 3));
        Rgb8 own  = fb->rgb[i];
        Rgb8 c    = own;
        if (k->under.kind != VIDEO_FX_NONE && (k->under.lines & bit) != 0) {
            c = fx_apply(&k->under, c, own);
        }
        if ((k->fade.lines & bit) != 0) {
            if (k->covered) {
                c = k->cover;
            }
            c = fx_apply(&k->fade, c, own);
        }
        fb->rgb[i] = c;
    }
}

void video_compose(const Video *v, Framebuffer *fb)
{
    /* (build 141) The colour path's colours: each entry's picture colour,
     * or its glow where an effect has lit it -- never the live palette. */
    Rgb8 lut[PAL_TOTAL];
    for (int i = 0; i < PAL_TOTAL; i++) {
        lut[i] = entry_colour(&v->colours, i);
    }

    memset(fb->px, v->backdrop, sizeof fb->px);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        fb->rgb[i] = lut[v->backdrop % PAL_TOTAL];
    }
    /* (build 142) Which pictures this frame's pixels came from. */
    memset(fb->pic, 0, sizeof fb->pic);
    for (int i = 0; i < VIDEO_PICTURES_MAX; i++) {
        fb->pictures[i] = (i < v->picture_count) ? v->pictures[i].picture
                                                 : NULL;
    }
    fb->picture_pixels = 0;

    draw_plane(v, fb, lut, LAYER_B, false);
    draw_plane(v, fb, lut, LAYER_A, false);
    draw_sprites(v, fb, lut, false);
    draw_plane(v, fb, lut, LAYER_B, true);
    draw_plane(v, fb, lut, LAYER_A, true);
    draw_sprites(v, fb, lut, true);

    frame_effect(v, fb);
}

FbDraw fb_draw_from_env(void)
{
    /* (build 141) The colour plane is the one shown; the palette path is
     * the reference, PS2_DRAW=palette. */
    const char *v = getenv("PS2_DRAW");
    if (v != NULL && strcmp(v, "palette") == 0) {
        return FB_DRAW_PALETTE;
    }
    return FB_DRAW_COLOUR;
}

Rgb8 fb_palette_colour(const Framebuffer *fb, const Palette *pal, int i)
{
    /* (build 146a) A picture's pixel has no tile and no palette entry
     * behind it: its colour is the colour plane's. */
    if (fb->pic[i] != 0) {
        return fb->rgb[i];
    }
    int  idx  = fb->px[i];
    Cram cram = (idx < PAL_TOTAL)
              ? pal->entry[idx]
              : fb->overlay[(idx - FB_OVERLAY_BASE) % PAL_ENTRIES];
    return cram_to_rgb8(cram);
}

int fb_colour_differs(const Framebuffer *fb, const Palette *pal, int *first)
{
    /* The 80 colours once, as fb_palette_colour gives each. */
    Rgb8 lut[FB_INDICES];
    for (int i = 0; i < FB_INDICES; i++) {
        lut[i] = cram_to_rgb8((i < PAL_TOTAL) ? pal->entry[i]
                                              : fb->overlay[i - FB_OVERLAY_BASE]);
    }

    int n = 0;
    if (first != NULL) {
        *first = -1;
    }
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        if (fb->pic[i] != 0) {
            continue;       /* (build 146a) as fb_palette_colour */
        }
        Rgb8 want = lut[fb->px[i] % FB_INDICES];
        Rgb8 got  = fb->rgb[i];
        if (want.r != got.r || want.g != got.g || want.b != got.b) {
            if (n == 0 && first != NULL) {
                *first = i;
            }
            n++;
        }
    }
    return n;
}
