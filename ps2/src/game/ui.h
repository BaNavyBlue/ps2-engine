#ifndef PS2_UI_H
#define PS2_UI_H

#include "../core/video.h"

/* Phantasy Star II's interface is almost entirely bordered boxes with text
 * and a cursor, so that is what this layer provides. It draws through the
 * video layer like anything else -- no special-case blitting. */

typedef struct {
    int first_tile;   /* where FontsIconsArt landed in the shared tileset */
    int glyph_base;   /* added to a CHARSET value to reach the glyph tile;
                       * zero, since a CHARSET code is already the tile's
                       * index within FontsIconsArt. loc_980A builds the
                       * cell as `move.w $FFFFF72C.w, d6; move.b (a1)+, d6`
                       * with $FFFFF72C holding $8500 -- priority 1,
                       * palette 0, tile $500 -- so the code is added to
                       * the sheet's base and nothing else. */
    int digit_base;   /* CHARSET codes 1..10 are the digits, and they do
                       * NOT sit with the letters: FontsIconsArt has
                       * icons where glyph_base would put them, so
                       * '0'..'9' are tiles 118..127 instead. Digits add
                       * this base; everything else adds glyph_base. */
    u8  pal;          /* palette line used for text */
} Font;

/* Window frame tiles, at their literal indices within FontsIconsArt.
 * $B8..$BF form a complete box: corners, edges and both verticals. */
#define TILE_WIN_FILL   0x26
#define TILE_WIN_TL     0xB8
#define TILE_WIN_TOP    0xB9
#define TILE_WIN_TR     0xBA
#define TILE_WIN_LEFT   0xBB
#define TILE_WIN_RIGHT  0xBC
#define TILE_WIN_BL     0xBD
#define TILE_WIN_BOTTOM 0xBE
#define TILE_WIN_BR     0xBF

/* The paired tiles the `cursorbox` macro emits before each menu entry.
 * These are static decoration; the moving selection cursor is a sprite
 * (Obj_RedRectangleCursor, ps2.asm:487) and is not implemented yet. */
#define TILE_CURSOR_L   0xB4
#define TILE_CURSOR_R   0xB5

typedef struct {
    int col, row;     /* top-left, in tiles */
    int w, h;         /* size in tiles, borders included */
    u8  pal;
    u8  fill;         /* palette index for the interior */
} Window;

void ui_set_font(const Font *font);

void ui_window(Video *v, LayerId layer, const Window *win);
void ui_text(Video *v, LayerId layer, int col, int row, u8 pal,
             const char *text);
void ui_textf(Video *v, LayerId layer, int col, int row, u8 pal,
              const char *fmt, ...);

/* Draws a list of options inside win and marks the selected one. */
void ui_menu(Video *v, LayerId layer, const Window *win,
             const char *const *items, int count, int selected);

#endif /* PS2_UI_H */
