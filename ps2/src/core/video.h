#ifndef PS2_VIDEO_H
#define PS2_VIDEO_H

#include "art.h"
#include "palette.h"
#include "ps2_types.h"

/* This is a semantic layer, not a VDP emulation. There are no VRAM
 * addresses, no control-port write pairs and no DMA queue: ported game code
 * says what it wants drawn and the compositor works out the pixels.
 *
 * What is kept from the hardware model is the indexed pipeline -- 8x8 tiles
 * of 4-bit palette indices, index 0 transparent, four palette lines. The
 * original implements every fade, flash and colour cycle as arithmetic on
 * palette entries, so keeping indices through to the final stage means
 * those routines port directly, and it gives the CRT chain the phosphor
 * levels it needs. */

#define TILE_W       8
#define TILE_H       8
#define PLANE_COLS      64
#define PLANE_ROWS      32  /* the scroll size register as VDPSetupArray
                               leaves it ($9001): 64x32, every screen but
                               one */
#define PLANE_ROWS_MAX  64  /* $9011: 64x64, which the ending screen sets
                               for its credits (EndingScreen, ps2.asm:10795)
                               and puts back at its last button */
#define MAX_SPRITES  80   /* the original's own software cap (sprite_count) */

/* An 8x8 tile of 4-bit indices, one byte per pixel for straightforward
 * addressing. Memory is free here: the whole game's art is a few MB. */
typedef struct {
    u8 px[TILE_W * TILE_H];
} Tile;

typedef struct {
    Tile *tiles;
    int   count;
} TileSet;

typedef enum {
    LAYER_B = 0,   /* background plane */
    LAYER_A = 1,   /* foreground plane */
    LAYER_COUNT
} LayerId;

typedef struct {
    u16  tile;       /* index into the bound tileset */
    u8   pal    : 2; /* palette line */
    u8   prio   : 1; /* draws above sprites that lack priority */
    u8   flip_h : 1;
    u8   flip_v : 1;
} Cell;

typedef struct {
    s16 x, y;        /* pixel position, top-left */
    u16 tile;        /* first tile; a WxH sprite consumes W*H consecutive */
    u8  w, h;        /* size in tiles */
    u8  pal    : 2;
    u8  prio   : 1;
    u8  flip_h : 1;
    u8  flip_v : 1;
} Sprite;

/* Indices FB_OVERLAY_BASE and up are the port's own overlay (the Escape
 * menu): drawn after video_compose, looked up in `overlay` rather than the
 * game's palette. The game's palette is never touched for it, so nothing
 * the game drew changes colour while the menu is up. video_compose only
 * ever writes indices below PAL_TOTAL. */
#define FB_OVERLAY_BASE PAL_TOTAL
#define FB_INDICES      (PAL_TOTAL + PAL_ENTRIES)

/* (build 140) The colour plane, `rgb`: each pixel's colour, written beside
 * its index by everything that writes `px` (video_compose, the Escape
 * menu's overlay), so a frame exists as colour as well as indices (full
 * colour's step 2, docs/palette_effects.md). While the art is indexed, a
 * pixel's colour is its index's colour in the palette the frame is
 * composed with -- the game's live palette, or `overlay` -- through the DAC
 * ladder (cram_to_rgb8): what the palette path shows, decided when the
 * frame is composed instead of at the platform. The platforms and the
 * route shots draw from either (PS2_DRAW=colour); the palette path is the
 * one shown. (build 141, said here in 145) Since 141 the colour plane is
 * made from the colour path (VideoColours, below), never read from the
 * live palette, and it is the one the platforms show (fb_draw_from_env);
 * PS2_DRAW=palette shows the palette path. The route shots stay the
 * palette path's unless PS2_DRAW=colour. */
#define VIDEO_PICTURES_MAX 8

/* (build 142) Each pixel's picture, `pic`: 0 when its colour came from a
 * tile through the colour path's palette (or the backdrop, or the
 * overlay), else the number + 1 of the binding (VideoPicture, below) whose
 * picture gave it -- `pictures` says which picture each was this frame.
 * `picture_pixels` counts them: route_probe's colour check reports it, so
 * a route shows its pictures were drawn and not left to their tiles. */
typedef struct {
    u8   px[SCREEN_W * SCREEN_H];   /* 0..PAL_TOTAL-1 the game's, then the
                                       overlay's 16 */
    Rgb8 rgb[SCREEN_W * SCREEN_H];  /* (build 140) the colour plane */
    Cram overlay[PAL_ENTRIES];      /* set by whoever draws overlay pixels */
    u8   pic[SCREEN_W * SCREEN_H];  /* (build 142) the picture, by binding */
    const Picture *pictures[VIDEO_PICTURES_MAX];
    u32  picture_pixels;
} Framebuffer;

/* (build 140) Which plane a platform or a shot draws the frame from:
 * FB_DRAW_PALETTE, the index plane through the palette (the way every
 * build to 140 drew), or FB_DRAW_COLOUR, the colour plane as it is.
 * fb_draw_from_env reads PS2_DRAW ("colour" or "palette"); (build 141)
 * unset is the colour plane, the one shown, and the platforms say on
 * stderr when it is the palette path. The route probe keeps the palette
 * path's indexed PNGs for its shots unless PS2_DRAW=colour. */
typedef enum {
    FB_DRAW_PALETTE = 0,
    FB_DRAW_COLOUR  = 1
} FbDraw;

FbDraw fb_draw_from_env(void);

/* (build 140) The colour the palette path gives pixel `i`: the index's
 * entry in `pal` (or the overlay's), through cram_to_rgb8. What the
 * colour plane must hold; fb_colour_differs counts the pixels where it
 * does not (and gives the first through `first`, -1 when none). (build
 * 146a) A pixel a picture drew (`pic`) has no entry behind it: the palette
 * path gives the colour plane's colour there, and fb_colour_differs
 * passes it by -- the pictures have no tiles to be held to any more; the
 * frames' colours hash (route_probe) held them to the tiles' last. */
Rgb8 fb_palette_colour(const Framebuffer *fb, const Palette *pal, int i);
int  fb_colour_differs(const Framebuffer *fb, const Palette *pal,
                       int *first);

/* (build 141) The whole-frame effects (docs/palette_effects.md section 2),
 * worked out on the colour plane from each pixel's colour and the step:
 * the four 21-frame fades, one channel of a colour a frame in the ROM's
 * order, and the damage floor's white field. */
typedef enum {
    VIDEO_FX_NONE = 0,
    VIDEO_FX_TO_BLACK,      /* PaletteFadeFrom: red down, then green, blue */
    VIDEO_FX_FROM_BLACK,    /* PaletteFadeTo: blue up, then green, red */
    VIDEO_FX_TO_WHITE,      /* loc_634E: red up, then green, blue */
    VIDEO_FX_FROM_WHITE,    /* loc_62CC: blue down, then green, red */
    VIDEO_FX_WHITE          /* the damage floor's field (loc_7108) */
} VideoFx;

#define VIDEO_FADE_STEPS 21     /* $14 + 1 frames, any colour all the way */
#define VIDEO_LINES_ALL  0x0F   /* the whole frame */

typedef struct {
    VideoFx kind;
    int     step;       /* frames done, 0..VIDEO_FADE_STEPS */
    u8      lines;      /* which pictures it applies to, by palette line:
                           VIDEO_LINES_ALL, or the dream's second picture
                           (line 3) alone */
} VideoFade;

/* (build 141) The colour path (full colour, step 3; docs/palette_effects.md):
 * what the colour plane is made of, kept beside the palette path's live
 * palette and never read from it. Every palette operation in the game
 * goes through the video_* calls below, which make the palette path's
 * write as before (the reference, compared every frame by
 * tools/colour_check.py) and the colour path's.
 *
 *   picture -- the pictures' own colours. While the art is indexed, a
 *              colour for each palette entry, written by every load
 *              (into the live palette or a fade target alike): the
 *              palette as loaded. With full-colour art, each pixel's own.
 *   glow    -- the colours the index-dependent effects write: the colour
 *              cycles and sweeps, Mother Brain's lights, the library's
 *              overlay, and (entry 0) the backdrop's flashes. `lit` says
 *              which entries an effect has written since their picture
 *              last loaded; a pixel drawn with a lit entry shows its glow
 *              colour (the slot plane: with glow masks, the composed mask;
 *              while the art is indexed, the index plane where lit).
 *              Entry 0 is the backdrop: the frame's own colour where
 *              nothing is drawn, its picture colour the screen's as
 *              loaded.
 *   fade    -- the whole-frame effect and its step. `under` is the effect
 *              a fade to black or white began over when that was not
 *              finished (a fade starts from what is shown); `cover` is set
 *              when it began over a screen already all one colour, which
 *              it then starts from. */
typedef struct {
    Rgb8      picture[PAL_TOTAL];
    Rgb8      glow[PAL_TOTAL];
    u64       lit;
    VideoFade fade;
    VideoFade under;
    bool      covered;
    Rgb8      cover;
} VideoColours;

/* (build 142) A picture bound to the tiles it was made from (full colour,
 * docs/asset_format.md; the portraits first). Where a plane cell on
 * `layer` draws a tile in [first_tile, first_tile + count) inside the cells
 * the binding names, the compositor takes the pixel's colour from the
 * picture at that place -- alpha 0 draws nothing -- and its index from the
 * tile as before: the palette path stays the reference, and
 * tools/colour_check.py proves the picture is the tiles through the
 * palette. A pixel whose palette entry an effect has lit (the glow) shows
 * the glow's colour, as a tile's would.
 *
 * `col`, `row` are the plane cell the picture's top-left pixel is in;
 * `col_lo`..`col_hi` and `row_lo`..`row_hi` the picture's cells (from its
 * top left, inclusive) that the drawing put there this frame -- a window
 * opening a column at a time names only the columns it has drawn, so what
 * is under the rest is never taken for the picture. Bound by whoever draws
 * the cells (window.c's draw_entry for a portrait window), for the frame:
 * game_render clears them with the planes. A later binding over an
 * earlier one wins.
 *
 * (build 151) `hidden` is the reveal mask: the pixels a dissolve has not
 * put in yet, or has taken out, which draw nothing (as alpha 0). Bit
 * y * 8 + x is a tile's pixel [x, y] as the tile holds it, before the
 * cell's flip -- the place FadeArtFill and loc_6CA4 write in every tile of
 * their range at once (dissolve_order.json) -- so a flipped cell hides
 * its mirrored pixel, as its tile would. 0 shows the whole picture. */
typedef struct {
    const Picture *picture;
    u16  first_tile;
    u16  count;
    u8   layer;
    s16  col, row;
    s16  col_lo, col_hi;
    s16  row_lo, row_hi;
    u64  hidden;
} VideoPicture;

typedef struct {
    Cell  plane[LAYER_COUNT][PLANE_ROWS_MAX][PLANE_COLS];
    int   plane_rows;       /* PLANE_ROWS or PLANE_ROWS_MAX: where both
                               planes wrap, as the VDP's scroll size
                               register decides for both at once */
    s16   scroll_x[LAYER_COUNT];
    s16   scroll_y[LAYER_COUNT];

    Sprite sprite[MAX_SPRITES];
    int    sprite_count;
    int    sprite_cap;      /* runtime-adjustable; MAX_SPRITES by default */

    const TileSet *tileset;
    Palette        palette;   /* the palette path's live palette ($FB00) */
    u8             backdrop;  /* palette index drawn where nothing else is */
    VideoColours   colours;   /* (build 141) the colour path */

    VideoPicture   pictures[VIDEO_PICTURES_MAX];   /* (build 142) */
    int            picture_count;
} Video;

void video_init(Video *v, const TileSet *ts);

void video_set_tile(Video *v, LayerId layer, int col, int row, Cell cell);
void video_fill(Video *v, LayerId layer, Cell cell);
void video_scroll(Video *v, LayerId layer, int x, int y);

/* The scroll size register's height: PLANE_ROWS or PLANE_ROWS_MAX. The
 * rows past the height are kept but neither drawn nor written. */
void video_set_plane_rows(Video *v, int rows);

/* (build 142) The pictures bound to tiles (VideoPicture): cleared every
 * frame with the planes, bound as their cells are drawn. Binding returns
 * false, the tiles then drawn as they are, past VIDEO_PICTURES_MAX or for
 * a binding that names no picture or no cells. */
void video_pictures_clear(Video *v);
bool video_picture_bind(Video *v, const VideoPicture *b);

/* Cleared every frame; game code repopulates it, exactly as BuildSprites
 * did. Returns false once the cap is reached. */
void video_sprites_begin(Video *v);
bool video_sprite_push(Video *v, const Sprite *s);

/* (build 141) The palette operations, each made on both paths. `dest` is
 * the palette path's buffer the ROM writes: &v->palette (the live
 * palette, PaletteLoad2) or a fade target (PaletteLoad1, the battle's
 * loaded palette, the dream's). The colour path has one set of picture
 * colours for both: a load into a target is made while the screen is
 * covered (black or white), and the fade in brings it up. */

/* Every entry of `dest` black; the pictures' colours black and nothing lit
 * (a screen's palette wiped before its loads). */
void video_colours_clear(Video *v, Palette *dest);

/* PalettePtrs entry `index` (PALETTE_*) to `line` of `dest`, as
 * palette_load_entry; the entries it writes are the pictures' colours
 * and unlit. */
bool video_colours_load(Video *v, Palette *dest, int line, int index);

/* `lines` palette lines of `src` from `line` on into `dest` and the
 * pictures' colours (the ending's preloaded palettes). */
void video_colours_copy(Video *v, Palette *dest, const Palette *src,
                        int line, int lines);

/* One entry of `dest`: a colour the ROM patches over a load. */
void video_colours_set(Video *v, Palette *dest, int entry, Cram c);

/* An index-dependent effect writes entry `entry`: the live palette's
 * entry and the glow colour, lit. */
void video_glow(Video *v, int entry, Cram c);

/* PalettePtrs entry `index` into the live `line` as an effect (the
 * library's overlay, loc_C4B2): every entry it writes glows. */
bool video_glow_load(Video *v, int line, int index);

/* The backdrop's colour as an effect sets it (the hit flash, the Nei
 * Sword's light, Gaira's alarm): entry 0's glow. */
void video_backdrop(Video *v, Cram c);

/* True when the backdrop shows colour `c` (the colour path's own). */
bool video_backdrop_is(const Video *v, Cram c);

/* `*target = live`: the colours as shown become the colours a fade comes
 * back to -- the glow kept as the pictures' colours, nothing lit. */
void video_target_from_shown(Video *v, Palette *target);

/* `live = *target`: the pictures as loaded shown again, no glow (the
 * damage floor's restore, the battle's fade's end), no effect. */
void video_shown_from_target(Video *v, const Palette *target);

/* A fade in begins: from black (`lines` of the live palette zeroed, as
 * PaletteFadeTo and the dream do) or from white (every live entry $EEE,
 * as loc_62CC does). */
void video_fade_begin(Video *v, VideoFx kind, u8 lines);

/* One frame of a fade: the palette path's step on the live palette (toward
 * `target` for the fades in; NULL for the fades out), the colour path's
 * step. A fade out begins at its first step, from what is shown. */
void video_fade_step(Video *v, VideoFx kind, const Palette *target);

/* The damage floor's field: every live entry $EEE, the frame white. */
void video_flash_white(Video *v);

/* Composites planes and sprites into an indexed framebuffer.
 * Order, back to front: B low, A low, sprites low, B high, A high,
 * sprites high. */
void video_compose(const Video *v, Framebuffer *fb);

#endif /* PS2_VIDEO_H */
