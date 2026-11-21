#include "ending.h"
#include "rom_ids.h"

#include "../core/credits.h"
#include "../core/nametable.h"
#include "../core/table.h"
#include "../core/tileset.h"
#include "npc.h"
#include "window.h"

#include <stdio.h>
#include <string.h>

/* The ending screen, EndingScreen (ps2.asm:10795) and the routines it
 * calls: the page routine (loc_7932) and its picture blitter (loc_7914),
 * the plane clear and the holds (loc_799A, loc_79D2, loc_79EA), the
 * credits writer (loc_787E), the credits' palette fade (loc_6AE0) and
 * the ending UFO (Obj_EndingUFO, object 8, ps2.asm:2505).
 *
 * One call of ending_tick is one frame: the code between two of the
 * routine's WaitForVBlank calls. What the ROM's VBlank does at the end of
 * a frame -- the window processor when the routine asked for it -- is
 * done at the end of the tick, so the frame drawn after the tick shows it
 * as the next field did. */

/* ------------------------------------------------------------------ */
/* Where things go                                                    */
/* ------------------------------------------------------------------ */

/* The VRAM tile each of the screen's loads lands on. */
#define STARFIELD_UFO_TILE      0x480   /* loc_B6E30 at $9000 */
#define FONT_TILE               0x500   /* FontsIconsArt at $A000 */
#define CREDITS_LETTERS_TILE    0x500   /* loc_B6C2C at $A000: the credits'
                                           alphabet over the font's first
                                           26 tiles, icons no text uses */
#define FONT_CLEARED_COLOUR     11      /* DrawArtTiles2: the window fill */

#define MUSIC_NEVER_DREAM       0x97    /* MusicID_NeverDream: the credits */
#define SOUND_FADE_OUT          0xE0    /* the music faded before the white */

#define PORTRAIT_ROW            2       /* loc_79F2: $4108 and $4128 */
#define PORTRAIT_LEFT           4
#define PORTRAIT_RIGHT          20
#define TEXT_MAX_LINE           0xC     /* $CD1C: twelve lines */

#define SCROLL_MASK             0x1FF   /* 512 px: the 64x64 planes */
#define SLIDE_START             0x120   /* a page's scroll before its slide */
#define SLIDE_STEP              0x20    /* 32 px a frame */

#define WHITE_FADE_FRAMES       21      /* loc_62CC and loc_634E: $14 + 1 */
#define TEXT_LOOPS              0x168   /* demo_timer: 360 loops of two frames */
#define CLEARED_FRAMES          0x3C    /* loc_799A's hold: 60 */
#define LAST_PAGE_EXTRA_FRAMES  0x78    /* loc_79EA: 120 more after page six */
#define LAST_PICTURE_FRAMES     0x168   /* 360 */

#define LOWERCASE_CREDITS_COLOUR 0xA44  /* $FFFFFBA2: line 1, entry 1 */

/* The strip wipe: loc_79F2's entries 6-12 are seven windows into the
 * last picture, 40 wide, from its middle two rows (6-7, drawn at plane
 * rows 13-14) out to all fourteen (drawn from row 7), one a frame. */
#define WIPE_BANDS              7
#define WIPE_FIRST_PICTURE_ROW  6
#define WIPE_FIRST_PLANE_ROW    13

/* The credits: one step every three frames (loc_7824). Each step scrolls
 * plane A up a pixel; every eighth writes a row of 40 cells a quarter of
 * the plane below the top of the view (loc_787E), 32 px under its bottom
 * edge. */
#define CREDITS_FRAMES_PER_STEP 3
#define CREDITS_ROW_AHEAD       0x100
#define CREDITS_COLS            40
/* The bytes' codes (CREDITS_SPACE, CREDITS_NEXT_LINE, ...) are credits.h's
 * (build 136e), which its text loader writes. */
#define CREDITS_LETTER_BASE     0x4BF   /* + 'A' ($41) is tile $500 */
#define CREDITS_ROWS_PER_LINE   3
#define CREDITS_SECTION_ROWS    0x1C

/* The credits' palette fade (loc_6AE0): 3600 steps in, then every eighth
 * step one row of four words of the ramp lands in line 3, entries 9-12,
 * from its last row (black) to its first; the last row written starts
 * the ending UFO. */
#define FADE_DELAY              0xE10   /* $F634 */
#define FADE_ROW_GAP            7
#define FADE_FIRST_OFFSET       0x40    /* $F632: a byte offset, 8 a row */
#define FADE_ROW_BYTES          8
#define FADE_LINE               3
#define FADE_FIRST_ENTRY        9
#define FADE_ENTRIES            4

/* Obj_EndingUFO: in screen coordinates (bit 4 of its flags), hidden (bit
 * 1) for its first 29 runs, then its five frames two runs each; its
 * sixth sets bit 0 and RunObject deletes it. */
#define ENDING_UFO_OBJECT       8
#define UFO_X                   0x156
#define UFO_Y                   0x100
#define UFO_FIRST_WAIT          0x1C
#define UFO_FRAME_WAIT          1
#define UFO_FRAMES              5
#define SPRITE_ORIGIN           0x80    /* sprite 0,0 is 128 up and left */

#define SCRIPT_EPILOGUE_1       0x182A  /* "We shall make you see ..." */
#define SCRIPT_EPILOGUE_2       0x182B  /* "I can see the confusion ..." */
#define SCRIPT_EPILOGUE_3       0x182C  /* "How dare you ruin Algo!" */
#define SCRIPT_EPILOGUE_4       0x182D  /* "I refuse to be a slave of fate!" */
#define SCRIPT_EPILOGUE_5       0x182E  /* "You have shown me the ugliness ..." */
#define SCRIPT_EPILOGUE_6       0x182F  /* "These are my parting words ..." */
#define SCRIPT_FINAL_DAYS       0x1830  /* "I wonder what the people will see
                                           in the final days?" */

/* ------------------------------------------------------------------ */
/* The pages                                                          */
/* ------------------------------------------------------------------ */

/* Six epilogue pages and, last, the text over the stars: each a call of
 * the page routine with a script at a text position, a palette into
 * line 2 and a slide -- the scroll preset to SLIDE_START on one or both
 * axes and stepped back to zero. The text position is the word the ROM
 * gives $CD16/$CD18 ($45AC is row 11, column 22). */
typedef struct {
    const char *portrait;       /* its nametable, or NULL */
    int         portrait_col;
    u16         script;
    int         text_col, text_row;
    int         palette;        /* PalettePtrs index */
    int         palette_lines;
    bool        preset_x, preset_y;
    s16         step_x, step_y;
} EndingPage;

#define EPILOGUE_PAGES   6
#define FINAL_DAYS_PAGE  6
#define PAGES            7

static const EndingPage PAGE[PAGES] = {
    /* 1: from the right */
    { "EndingPage1PortraitTileInd", PORTRAIT_LEFT, SCRIPT_EPILOGUE_1,
      22, 11, PALETTE_ENDING_PAGE_1, 1, true, false, SLIDE_STEP, 0 },
    /* 2: up from the bottom */
    { "EndingPage2PortraitTileInd", PORTRAIT_RIGHT, SCRIPT_EPILOGUE_2,
      2, 11, PALETTE_ENDING_PAGE_2, 1, false, true, 0, SLIDE_STEP },
    /* 3: from the bottom right */
    { "EndingPage3PortraitTileInd", PORTRAIT_LEFT, SCRIPT_EPILOGUE_3,
      22, 11, PALETTE_ENDING_PAGE_3, 1, true, true, SLIDE_STEP, SLIDE_STEP },
    /* 4: from the right */
    { "EndingPage4PortraitTileInd", PORTRAIT_LEFT, SCRIPT_EPILOGUE_4,
      22, 11, PALETTE_ENDING_PAGE_4, 1, true, false, SLIDE_STEP, 0 },
    /* 5: down from the top */
    { "EndingPage5PortraitTileInd", PORTRAIT_RIGHT, SCRIPT_EPILOGUE_5,
      2, 11, PALETTE_ENDING_PAGE_5, 1, false, true, 0, -SLIDE_STEP },
    /* 6: from the bottom right */
    { "EndingPage6PortraitTileInd", PORTRAIT_LEFT, SCRIPT_EPILOGUE_6,
      22, 11, PALETTE_ENDING_PAGE_6, 1, true, true, SLIDE_STEP, SLIDE_STEP },
    /* The text over the stars: no picture, no slide ($4418: row 8,
     * column 12), the last picture's two lines. */
    { NULL, 0, SCRIPT_FINAL_DAYS,
      12, 8, 0x4B, 2, false, false, 0, 0 },
};

#define TITLE_PALETTE   PALETTE_TITLE_SCREEN    /* pal_id_tit_scr */
#define LAST_PALETTE    PALETTE_ENDING_LAST_PICTURE

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    AT_SETUP,           /* the frame the display comes back on, white */
    AT_FADE_IN,         /* loc_62CC */
    AT_PAGE_DRAW,       /* loc_7914 and the page routine's setup */
    AT_PAGE_SLIDE,      /* loc_794A */
    AT_PAGE_TEXT,       /* loc_797C */
    AT_PAGE_CLEARED,    /* loc_799A, then loc_79D2 */
    AT_LAST_PAGE_HOLD,  /* loc_79EA */
    AT_WIPE,            /* loc_77B2 */
    AT_LAST_PICTURE,    /* loc_79D2 */
    AT_TO_WHITE,        /* loc_634E */
    AT_WHITE,           /* loc_799A, then loc_79D2 */
    AT_FADE_IN_AGAIN,   /* loc_62CC */
    AT_CREDITS,         /* loc_7824 */
    AT_BUTTON           /* loc_785C */
} EndingAt;

typedef struct {
    bool live;          /* in its slot */
    bool started;       /* ObjEU_Init has run */
    bool hidden;        /* bit 1 of 2(a0) */
    s16  timer;         /* $26 */
    s16  frame;         /* $24 */
} EndingUfo;

static EndingHost g_host;
static EndingAt   g_at;
static int        g_count;
static int        g_page;
static s16        g_scroll_x, g_scroll_y;   /* $F620, $F61C: plane A */
static s16        g_step_x, g_step_y;       /* the slide's d2 */
static int        g_text_loops;             /* demo_timer in the text */
static bool       g_text_second_frame;
static int        g_wipe_band;
static Cell       g_plane_a[PLANE_ROWS_MAX][PLANE_COLS];

static int        g_credits_pos;            /* a3 */
static s16        g_credits_blank_rows;     /* $F770 */
static int        g_credits_frame;
static s16        g_fade_delay;             /* $F634 */
static s16        g_fade_offset;            /* $F632 */
static EndingUfo  g_ufo;
static int        g_ufo_drawn_frame;        /* BuildSprites' last list */

/* Assets, loaded at startup. */
static TileSet    g_starfield_ufo_art, g_credits_letters;
static Nametable  g_starfield, g_last_picture;
static Nametable  g_portrait[EPILOGUE_PAGES];

/* (build 148) The pages' pictures and the last one (assets/art/scenes),
 * and which is in plane A now -- its table and where that table's top
 * left is -- for ending_render's binding (nametable_picture). */
static const char *const PAGE_PICTURE[EPILOGUE_PAGES] = {
    "ending_rudo", "ending_amy", "ending_kain", "ending_shir",
    "ending_hugh", "ending_anna"
};
static const Picture   *g_page_picture[EPILOGUE_PAGES];
static const Picture   *g_rolf_picture;
static const Nametable *g_plane_table;
static const Picture   *g_plane_picture;
static int              g_plane_col, g_plane_row;
/* (build 149) The star field's picture (its slot map the credits' fade's
 * four entries) and whether line 3 holds its colours yet: cleared at the
 * setup, so the stars are black on the black backdrop until the
 * PaletteLoad1 $4B before the final days' page. */
static const Picture   *g_stars_picture;
static bool             g_stars_loaded;
static CreditsScript g_credits;
static WordTable  g_fade_ramp;
static Palette    g_title_palette, g_last_palette;
static Palette    g_page_palette[PAGES];

/* ------------------------------------------------------------------ */
/* Loading                                                            */
/* ------------------------------------------------------------------ */

static bool load_palette(Palette *p, int index)
{
    memset(p, 0, sizeof *p);
    /* Every one of the ending's palettes lands on line 2 or, the title's,
     * on all four; PaletteLoad1/2 name the line, the PNG the count. */
    return palette_load_entry(p, (index == TITLE_PALETTE) ? 0 : 2, index);
}

bool ending_load_assets(void)
{
    bool ok = true;
    char path[96];

    ok = tileset_load(&g_starfield_ufo_art,
                      "assets/tiles/EndingStarfieldUfoArt.png")          && ok;
    ok = tileset_load(&g_credits_letters,
                      "assets/tiles/EndingCreditsLettersArt.png")        && ok;
    ok = nametable_load(&g_starfield,
                        "assets/nametables/EndingStarfieldTileInd.tmx")  && ok;
    ok = nametable_load(&g_last_picture,
                        "assets/nametables/EndingLastPictureTileInd.tmx") && ok;
    for (int i = 0; i < EPILOGUE_PAGES; i++) {
        snprintf(path, sizeof path, "assets/nametables/%s.tmx",
                 PAGE[i].portrait);
        ok = nametable_load(&g_portrait[i], path) && ok;
    }
    ok = credits_load(&g_credits,
                      "assets/script/ending_credits.txt")                && ok;
    ok = table_load_ramp(&g_fade_ramp,
                         "assets/palettes/ending_credits_fade.png")      && ok;

    ok = load_palette(&g_title_palette, TITLE_PALETTE)                   && ok;
    ok = load_palette(&g_last_palette, LAST_PALETTE)                     && ok;
    for (int i = 0; i < PAGES; i++) {
        ok = load_palette(&g_page_palette[i], PAGE[i].palette)           && ok;
    }
    for (int i = 0; i < EPILOGUE_PAGES; i++) {
        g_page_picture[i] = art_scene(PAGE_PICTURE[i]);
        ok = (g_page_picture[i] != NULL)                                 && ok;
    }
    g_rolf_picture = art_scene("ending_rolf");
    ok = (g_rolf_picture != NULL)                                        && ok;
    g_stars_picture = art_scene("ending_stars");
    ok = (g_stars_picture != NULL)                                       && ok;
    if (!ok) {
        fprintf(stderr, "ending: an asset is missing (make extract-inline_art "
                        "extract-nametables extract-tables extract-npcs)\n");
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* Pieces                                                             */
/* ------------------------------------------------------------------ */

/* DrawArtTiles into plane A: rows first .. first + rows - 1 of a table,
 * the first at (col, row). */
static void draw_rows(const Nametable *nt, int col, int row, int first,
                      int rows)
{
    for (int y = 0; y < rows; y++) {
        int from = first + y;
        if (from < 0 || from >= nt->height) {
            continue;
        }
        for (int x = 0; x < nt->width; x++) {
            int r = (row + y) & (PLANE_ROWS_MAX - 1);
            int c = (col + x) & (PLANE_COLS - 1);
            g_plane_a[r][c] = nametable_cell(nt->entry[from * nt->width + x]);
        }
    }
}

/* loc_799A: plane A filled with zeroes by DMA, and with it whatever
 * text the window system had typed there. */
static void clear_plane_a(void)
{
    memset(g_plane_a, 0, sizeof g_plane_a);
    win_drop_plain_text(g_host.win);
    g_plane_picture = NULL;
}

/* (build 148) `p` is in plane A now, its table `nt`'s top left at
 * (col, row). */
static void plane_picture(const Nametable *nt, const Picture *p, int col,
                          int row)
{
    g_plane_table   = nt;
    g_plane_picture = p;
    g_plane_col     = col;
    g_plane_row     = row;
}

/* The scroll before a page's slide, set with its own frame's wait. */
static void page_preset(void)
{
    const EndingPage *p = &PAGE[g_page];
    if (p->preset_x) {
        g_scroll_x = SLIDE_START;
    }
    if (p->preset_y) {
        g_scroll_y = SLIDE_START;
    }
}

/* The page routine's setup: the palette straight into the live line
 * (PaletteLoad2) and the slide's step. The text position waits for the
 * first window pass. */
static void page_setup(void)
{
    const EndingPage *p = &PAGE[g_page];
    /* (build 148) A page with a picture is drawn in its picture's own
     * colours: its palette is not loaded. The text over the stars (no
     * picture) still loads its two lines. */
    if (p->portrait == NULL) {
        video_colours_copy(g_host.video, &g_host.video->palette,
                           &g_page_palette[g_page], 2, p->palette_lines);
    }
    g_step_x = p->step_x;
    g_step_y = p->step_y;
}

/* loc_794A: one frame of the slide. An axis that comes to zero stops;
 * true when both have. */
static bool slide_step(void)
{
    g_scroll_y = (s16)((g_scroll_y + g_step_y) & SCROLL_MASK);
    if (g_scroll_y == 0) {
        g_step_y = 0;
    }
    g_scroll_x = (s16)((g_scroll_x + g_step_x) & SCROLL_MASK);
    if (g_scroll_x == 0) {
        g_step_x = 0;
    }
    return g_step_x == 0 && g_step_y == 0;
}

/* loc_797C: two frames a loop -- the window processor at the first's
 * VBlank, then CheckPrepareWindows after the second -- for TEXT_LOOPS
 * loops. The first window pass loads the script (Win_Null's
 * CheckLoadScript); the text types one code a loop, a line break taking
 * four. The controls are locked, so no button hurries it. True when the
 * last loop is done and plane A cleared. */
static bool page_text_frame(const Input *in)
{
    WinSys *ws = g_host.win;
    if (!g_text_second_frame) {
        g_text_second_frame = true;
        return false;
    }
    g_text_second_frame = false;

    if (g_text_loops == TEXT_LOOPS) {
        const EndingPage *p = &PAGE[g_page];
        win_plain_text(ws, p->script, p->text_col, p->text_row,
                       TEXT_MAX_LINE);
    } else {
        win_prepare(ws, in);
    }
    if (--g_text_loops != 0) {
        win_process(ws);
        return false;
    }
    clear_plane_a();
    return true;
}

/* loc_7914 with entries 6-12: band k is the picture's rows 6-k to 7+k at
 * plane rows 13-k on. */
static void wipe_band(int band)
{
    draw_rows(&g_last_picture, 0, WIPE_FIRST_PLANE_ROW - band,
              WIPE_FIRST_PICTURE_ROW - band, 2 + 2 * band);
}

/* ------------------------------------------------------------------ */
/* The credits                                                        */
/* ------------------------------------------------------------------ */

static void credits_start(void)
{
    g_host.queue_sound(g_host.ctx, MUSIC_NEVER_DREAM);
    g_fade_offset        = FADE_FIRST_OFFSET;
    g_fade_delay         = FADE_DELAY;
    g_credits_pos        = 0;
    g_credits_blank_rows = 0;
    g_credits_frame      = 0;
}

static u8 credits_byte(void)
{
    if (g_credits_pos >= g_credits.count) {
        return CREDITS_END;
    }
    return g_credits.byte[g_credits_pos];
}

/* loc_787E: the row 256 px below the top of the view. A text line, then
 * two blank rows (one after CREDITS_CLOSE_LINE, 28 after a section's
 * end). A text line is blank runs, spaces and letters; a letter is one of
 * 26 tiles, lowercase the same tile in palette line 1. No line reaches 40
 * cells, where the ROM's count would run out (credits_load refuses one,
 * build 136e). */
static void credits_write_row(void)
{
    int  row = ((g_scroll_y + CREDITS_ROW_AHEAD) >> 3) & (PLANE_ROWS_MAX - 1);
    Cell *out = g_plane_a[row];
    int  col = 0;

    memset(out, 0, CREDITS_COLS * sizeof *out);
    if (--g_credits_blank_rows >= 0) {
        return;
    }
    g_credits_blank_rows += CREDITS_ROWS_PER_LINE;

    while (col < CREDITS_COLS) {
        u8 code = credits_byte();
        g_credits_pos++;
        if (code == CREDITS_SPACE) {
            col++;
            continue;
        }
        if (code == CREDITS_NEXT_LINE) {
            return;
        }
        if (code == CREDITS_CLOSE_LINE) {
            g_credits_blank_rows--;
            return;
        }
        if (code == CREDITS_END_SECTION || code == CREDITS_END) {
            g_credits_blank_rows = CREDITS_SECTION_ROWS;
            return;
        }
        if (code < CREDITS_BLANK_RUN_BELOW) {
            col += code + 1;
            continue;
        }
        Cell cell;
        memset(&cell, 0, sizeof cell);
        cell.pal  = (code & CREDITS_LOWERCASE) ? 1 : 0;
        cell.tile = (u16)((code & ~CREDITS_LOWERCASE) + CREDITS_LETTER_BASE);
        out[col++] = cell;
    }
}

/* Obj_EndingUFO, from RunObjects. ObjEU_Init falls into ObjEU_Main on the
 * same call. */
static void ufo_run(void)
{
    if (!g_ufo.live) {
        return;
    }
    if (!g_ufo.started) {
        g_ufo.started = true;
        g_ufo.hidden  = true;               /* 2(a0) = $32 */
        g_ufo.frame   = -1;                 /* $24 = $FFFF */
        g_ufo.timer   = UFO_FIRST_WAIT;
    }
    if (--g_ufo.timer >= 0) {
        return;
    }
    g_ufo.timer  = UFO_FRAME_WAIT;
    g_ufo.hidden = false;                   /* 2(a0) = $30 */
    g_ufo.frame++;
    if (g_ufo.frame == UFO_FRAMES) {
        /* 2(a0) = 1: RunObject clears the slot this call. */
        memset(&g_ufo, 0, sizeof g_ufo);
    }
}

/* loc_6AE0, after RunObjects and BuildSprites. */
static void credits_palette_fade(void)
{
    if (--g_fade_delay >= 0) {
        return;
    }
    g_fade_delay = FADE_ROW_GAP;
    s16 offset = g_fade_offset;
    if (offset < 0) {
        return;
    }
    if (offset == 0) {
        memset(&g_ufo, 0, sizeof g_ufo);
        g_ufo.live = true;
    }
    g_fade_offset = (s16)(g_fade_offset - FADE_ROW_BYTES);

    for (int i = 0; i < FADE_ENTRIES; i++) {
        int from  = offset / 2 + i;
        int entry = FADE_LINE * PAL_ENTRIES + FADE_FIRST_ENTRY + i;
        video_glow(g_host.video, entry,
                   (from < g_fade_ramp.count) ? g_fade_ramp.value[from] : 0);
    }
}

/* loc_7824, the step's own frame: RunObjects, BuildSprites, the palette
 * fade, the scroll and perhaps a row. True once the next byte is the end. */
static bool credits_step(void)
{
    ufo_run();
    g_ufo_drawn_frame = (g_ufo.live && !g_ufo.hidden) ? g_ufo.frame : -1;
    credits_palette_fade();
    g_scroll_y = (s16)((g_scroll_y + 1) & SCROLL_MASK);
    if ((g_scroll_y & 7) == 0) {
        credits_write_row();
    }
    return credits_byte() == CREDITS_END;
}

/* ------------------------------------------------------------------ */
/* The screen                                                         */
/* ------------------------------------------------------------------ */

void ending_enter(const EndingHost *host)
{
    g_host = *host;
    Video *v = host->video;

    tileset_clear(host->vram);
    tileset_place_clearing(host->vram, host->font, FONT_TILE,
                           FONT_CLEARED_COLOUR);
    tileset_place(host->vram, &g_starfield_ufo_art, STARFIELD_UFO_TILE);
    tileset_place(host->vram, &g_credits_letters, CREDITS_LETTERS_TILE);

    /* $9011: both planes 64x64. ClearSpriteAndScroll zeroed them and the
     * scroll. */
    video_set_plane_rows(v, PLANE_ROWS_MAX);
    memset(g_plane_a, 0, sizeof g_plane_a);
    g_plane_picture = NULL;
    g_stars_loaded  = false;
    g_scroll_x = 0;
    g_scroll_y = 0;

    /* PaletteLoad1 pal_id_tit_scr into the target, its line 3 cleared;
     * loc_62CC's fill of every live entry with white. */
    video_colours_copy(v, host->fade_target, &g_title_palette, 0, PAL_LINES);
    for (int i = 0; i < PAL_ENTRIES; i++) {
        video_colours_set(v, host->fade_target, 3 * PAL_ENTRIES + i, 0);
    }
    video_fade_begin(v, VIDEO_FX_FROM_WHITE, VIDEO_LINES_ALL);
    v->backdrop = 0;

    memset(&g_ufo, 0, sizeof g_ufo);
    g_ufo_drawn_frame = -1;
    g_at    = AT_SETUP;
    g_count = 0;
    g_page  = 0;
}

bool ending_tick(const Input *in)
{
    Video   *v = g_host.video;
    Palette *target = g_host.fade_target;

    switch (g_at) {
        case AT_SETUP: {
            g_at    = AT_FADE_IN;
            g_count = 0;
            break;
        }
        case AT_FADE_IN: {
            video_fade_step(v, VIDEO_FX_FROM_WHITE, target);
            if (++g_count == WHITE_FADE_FRAMES) {
                g_page = 0;
                page_preset();
                g_at = AT_PAGE_DRAW;
            }
            break;
        }
        case AT_PAGE_DRAW: {
            /* loc_7914 then the page routine: the picture straight into
             * plane A, then its palette. */
            draw_rows(&g_portrait[g_page], PAGE[g_page].portrait_col,
                      PORTRAIT_ROW, 0, g_portrait[g_page].height);
            plane_picture(&g_portrait[g_page], g_page_picture[g_page],
                          PAGE[g_page].portrait_col, PORTRAIT_ROW);
            page_setup();
            g_at = AT_PAGE_SLIDE;
            break;
        }
        case AT_PAGE_SLIDE: {
            if (slide_step()) {
                g_at                = AT_PAGE_TEXT;
                g_text_loops        = TEXT_LOOPS;
                g_text_second_frame = false;
            }
            break;
        }
        case AT_PAGE_TEXT: {
            if (page_text_frame(in)) {
                g_at    = AT_PAGE_CLEARED;
                g_count = CLEARED_FRAMES;
            }
            break;
        }
        case AT_PAGE_CLEARED: {
            if (--g_count != 0) {
                break;
            }
            if (g_page < EPILOGUE_PAGES - 1) {
                g_page++;
                page_preset();
                g_at = AT_PAGE_DRAW;
            } else if (g_page == EPILOGUE_PAGES - 1) {
                g_at    = AT_LAST_PAGE_HOLD;
                g_count = LAST_PAGE_EXTRA_FRAMES;
            } else {
                credits_start();
                g_at = AT_CREDITS;
            }
            break;
        }
        case AT_LAST_PAGE_HOLD: {
            if (--g_count == 0) {
                /* (build 148) PalettePtrs $4A, the last picture's: its
                 * picture's colours now, not loaded. */
                /* The bands put the table's row t at plane row 7 + t. */
                plane_picture(&g_last_picture, g_rolf_picture, 0,
                              WIPE_FIRST_PLANE_ROW - WIPE_FIRST_PICTURE_ROW);
                g_wipe_band = 0;
                g_at        = AT_WIPE;
            }
            break;
        }
        case AT_WIPE: {
            wipe_band(g_wipe_band);
            if (++g_wipe_band == WIPE_BANDS) {
                g_at    = AT_LAST_PICTURE;
                g_count = LAST_PICTURE_FRAMES;
            }
            break;
        }
        case AT_LAST_PICTURE: {
            if (--g_count == 0) {
                g_host.queue_sound(g_host.ctx, SOUND_FADE_OUT);
                g_at    = AT_TO_WHITE;
                g_count = 0;
            }
            break;
        }
        case AT_TO_WHITE: {
            video_fade_step(v, VIDEO_FX_TO_WHITE, NULL);
            if (++g_count == WHITE_FADE_FRAMES) {
                /* PaletteLoad1 $4B into the target, the lowercase credits'
                 * colour, and plane A cleared of the picture. */
                video_colours_copy(v, target, &g_last_palette, 2, 2);
                video_colours_set(v, target, 1 * PAL_ENTRIES + 1,
                                  LOWERCASE_CREDITS_COLOUR);
                g_stars_loaded = true;
                clear_plane_a();
                g_at    = AT_WHITE;
                g_count = CLEARED_FRAMES;
            }
            break;
        }
        case AT_WHITE: {
            if (--g_count == 0) {
                video_fade_begin(v, VIDEO_FX_FROM_WHITE, VIDEO_LINES_ALL);
                g_at    = AT_FADE_IN_AGAIN;
                g_count = 0;
            }
            break;
        }
        case AT_FADE_IN_AGAIN: {
            video_fade_step(v, VIDEO_FX_FROM_WHITE, target);
            if (++g_count == WHITE_FADE_FRAMES) {
                /* The page routine again, with no picture and no slide. */
                g_page = FINAL_DAYS_PAGE;
                page_setup();
                g_at = AT_PAGE_SLIDE;
            }
            break;
        }
        case AT_CREDITS: {
            if (++g_credits_frame < CREDITS_FRAMES_PER_STEP) {
                break;
            }
            g_credits_frame = 0;
            if (credits_step()) {
                g_at = AT_BUTTON;
            }
            break;
        }
        case AT_BUTTON: {
            if (in->pressed.a || in->pressed.b || in->pressed.c) {
                /* $9001: the planes 64x32 again, then the Sega screen. */
                video_set_plane_rows(v, PLANE_ROWS);
                return true;
            }
            break;
        }
        default: {
            break;
        }
    }
    return false;
}

static void ufo_render(Video *v)
{
    const NpcSceneType *st = npc_scene_type(ENDING_UFO_OBJECT);
    int f = g_ufo_drawn_frame;
    if (f < 0 || st == NULL || f >= st->frames) {
        return;
    }
    for (int i = 0; i < st->pieces[f]; i++) {
        const NpcPiece *p = &st->frame[f][i];
        Sprite sp;
        memset(&sp, 0, sizeof sp);
        sp.x      = (s16)(UFO_X - SPRITE_ORIGIN + p->x);
        sp.y      = (s16)(UFO_Y - SPRITE_ORIGIN + p->y);
        sp.tile   = (u16)(st->tile + p->tile);
        sp.w      = p->w;
        sp.h      = p->h;
        sp.pal    = (u8)st->line;
        sp.flip_h = p->flip_h;
        sp.flip_v = p->flip_v;
        video_sprite_push(v, &sp);
    }
}

void ending_render(void)
{
    Video *v = g_host.video;
    if (v == NULL) {
        return;
    }
    /* (build 157a) The rows the VDP reads: all 64 while the ending's
     * $9011 holds, the first 32 once the last button's $9001 has put the
     * planes back to 64x32 -- a row's cells are where they were (64 a
     * row either way), only the wrap is shorter -- through SegaScreen's
     * fade. Writing all 64 into a 32-row plane wrapped rows 32-63 over
     * 0-31. */
    for (int r = 0; r < v->plane_rows && r < PLANE_ROWS_MAX; r++) {
        for (int c = 0; c < PLANE_COLS; c++) {
            video_set_tile(v, LAYER_A, c, r, g_plane_a[r][c]);
        }
    }
    if (g_plane_picture != NULL) {
        nametable_picture(v, LAYER_A, g_plane_table, g_plane_picture,
                          g_plane_col, g_plane_row);
    }
    /* (build 149) The star field from its picture once line 3 holds its
     * colours; until then it was black on the black backdrop, so it is
     * not drawn. Its art stays loaded for the UFO, a sprite from the same
     * sheet. */
    if (g_stars_loaded) {
        nametable_draw(v, LAYER_B, &g_starfield, 0, 0);
        nametable_picture(v, LAYER_B, &g_starfield, g_stars_picture, 0, 0);
    }
    video_scroll(v, LAYER_A, g_scroll_x, g_scroll_y);
    video_scroll(v, LAYER_B, 0, 0);
    win_render(g_host.win, v, FONT_TILE, 0, 0);
    ufo_render(v);
}

/* ------------------------------------------------------------------ */
/* For the probe                                                      */
/* ------------------------------------------------------------------ */

static const char *page_moment(const char *what)
{
    static char name[32];
    if (g_page == FINAL_DAYS_PAGE) {
        snprintf(name, sizeof name, "final_days_%s", what);
    } else {
        snprintf(name, sizeof name, "page%d_%s", g_page + 1, what);
    }
    return name;
}

const char *ending_debug_moment(void)
{
    switch (g_at) {
        case AT_SETUP: {
            return "setup";
        }
        case AT_FADE_IN: {
            return "fade_in";
        }
        case AT_PAGE_DRAW: {
            return page_moment("draw");
        }
        case AT_PAGE_SLIDE: {
            return page_moment("slide");
        }
        case AT_PAGE_TEXT: {
            return page_moment("text");
        }
        case AT_PAGE_CLEARED: {
            return page_moment("cleared");
        }
        case AT_LAST_PAGE_HOLD: {
            return "last_page_hold";
        }
        case AT_WIPE: {
            return "wipe";
        }
        case AT_LAST_PICTURE: {
            return "last_picture";
        }
        case AT_TO_WHITE: {
            return "to_white";
        }
        case AT_WHITE: {
            return "white";
        }
        case AT_FADE_IN_AGAIN: {
            return "fade_in_again";
        }
        case AT_CREDITS: {
            return (g_ufo_drawn_frame >= 0) ? "ufo" : "credits";
        }
        case AT_BUTTON: {
            return "button";
        }
        default: {
            return "unknown";
        }
    }
}

bool ending_waits_on_pad(void)
{
    return g_at == AT_BUTTON;
}
