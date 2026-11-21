#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* CHARSET from ps2.asm:25327. These are the game's own character codes;
 * the glyph tile is the code plus Font.glyph_base. Letters sit 28 tiles
 * into FontsIconsArt. Digits do NOT follow the same base -- the asm has
 * two separate charset blocks and their bases differ -- so they are left
 * unmapped until the text-drawing routine is ported. */
#define CHARSET_NONE (-1)

static const short CHARSET[128] = {
    ['A'] =  11, ['B'] =  12, ['C'] =  13, ['D'] =  14, ['E'] =  15,
    ['F'] =  16, ['G'] =  17, ['H'] =  18, ['I'] =  19, ['J'] =  20,
    ['K'] =  21, ['L'] =  22, ['M'] =  23, ['N'] =  24, ['O'] =  25,
    ['P'] =  26, ['Q'] =  27, ['R'] =  28, ['S'] =  29, ['T'] =  30,
    ['U'] =  31, ['V'] =  32, ['W'] =  33, ['X'] =  34, ['Y'] =  35,
    ['Z'] =  36,
    ['a'] =  37, ['b'] =  38, ['c'] =  39, ['d'] =  40, ['e'] =  41,
    ['f'] =  42, ['g'] =  43, ['h'] =  44, ['i'] =  45, ['j'] =  46,
    ['k'] =  47, ['l'] =  48, ['m'] =  49, ['n'] =  50, ['o'] =  51,
    ['p'] =  52, ['q'] =  53, ['r'] =  54, ['s'] =  55, ['t'] =  56,
    ['u'] =  57, ['v'] =  58, ['w'] =  59, ['x'] =  60, ['y'] =  61,
    ['z'] =  62,
    ['0'] =   1, ['1'] =   2, ['2'] =   3, ['3'] =   4, ['4'] =   5,
    ['5'] =   6, ['6'] =   7, ['7'] =   8, ['8'] =   9, ['9'] =  10,
    [' '] =   0, [','] = 0x3F, ['.'] = 0x40, [';'] = 0x41, ['"'] = 0x42,
    ['?'] = 0x43, ['!'] = 0x44, ['\''] = 0x45, ['-'] = 0x46, [':'] = 0x77,
};

static Font g_font = { 0, 28, 117, 0 };

void ui_set_font(const Font *font)
{
    g_font = *font;
}

/* A raw tile of the font/icon sheet, bypassing CHARSET. */
static Cell raw_cell(int tile, u8 pal)
{
    Cell cell;
    memset(&cell, 0, sizeof cell);

    cell.tile = (u16)(g_font.first_tile + tile);
    cell.pal  = pal & 3;
    cell.prio = 1;
    return cell;
}

/* CHARSET code for a character, or 0 for space and anything unmapped.
 * Code 0 is not a blank glyph -- glyph_base + 0 is an item icon -- so
 * space means "leave this cell as it is" and callers skip it, letting the
 * window fill underneath show through. */
static int charset_code(char ch)
{
    unsigned char c = (unsigned char)ch;
    return (c < 128) ? CHARSET[c] : 0;
}

static Cell glyph_cell(char ch, u8 pal)
{
    int code = charset_code(ch);
    int base = (code >= 1 && code <= 10) ? g_font.digit_base
                                         : g_font.glyph_base;
    return raw_cell(base + code, pal);
}

void ui_text(Video *v, LayerId layer, int col, int row, u8 pal,
             const char *text)
{
    for (int i = 0; text[i] != '\0'; i++) {
        if (charset_code(text[i]) == 0) {
            continue;
        }
        video_set_tile(v, layer, col + i, row, glyph_cell(text[i], pal));
    }
}

void ui_textf(Video *v, LayerId layer, int col, int row, u8 pal,
              const char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    ui_text(v, layer, col, row, pal, buf);
}

void ui_window(Video *v, LayerId layer, const Window *win)
{
    /* The game's own frame tiles, $B8..$BF around $26 fill. */
    for (int y = 0; y < win->h; y++) {
        bool top    = (y == 0);
        bool bottom = (y == win->h - 1);

        for (int x = 0; x < win->w; x++) {
            bool left  = (x == 0);
            bool right = (x == win->w - 1);
            int  tile  = TILE_WIN_FILL;

            if (top && left) {
                tile = TILE_WIN_TL;
            } else if (top && right) {
                tile = TILE_WIN_TR;
            } else if (bottom && left) {
                tile = TILE_WIN_BL;
            } else if (bottom && right) {
                tile = TILE_WIN_BR;
            } else if (top) {
                tile = TILE_WIN_TOP;
            } else if (bottom) {
                tile = TILE_WIN_BOTTOM;
            } else if (left) {
                tile = TILE_WIN_LEFT;
            } else if (right) {
                tile = TILE_WIN_RIGHT;
            }

            video_set_tile(v, layer, win->col + x, win->row + y,
                           raw_cell(tile, win->pal));
        }
    }
}

void ui_menu(Video *v, LayerId layer, const Window *win,
             const char *const *items, int count, int selected)
{
    ui_window(v, layer, win);

    for (int i = 0; i < count && i < win->h - 2; i++) {
        int row = win->row + 1 + i;

        if (i == selected) {
            video_set_tile(v, layer, win->col + 1, row,
                           raw_cell(TILE_CURSOR_L, win->pal));
            video_set_tile(v, layer, win->col + 2, row,
                           raw_cell(TILE_CURSOR_R, win->pal));
        }
        ui_text(v, layer, win->col + 3, row, win->pal, items[i]);
    }
}
