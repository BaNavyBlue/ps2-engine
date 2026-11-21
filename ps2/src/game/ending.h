#ifndef PS2_ENDING_H
#define PS2_ENDING_H

#include "../core/palette.h"
#include "../core/video.h"
#include "../platform/platform.h"

struct WinSys;

/* The ending screen (EndingScreen, ps2.asm:10795), from the fade out of
 * white to the last button: the six epilogue pages, the strip wipe that
 * opens the last picture, the text over the stars, the credits under
 * "Never Dream" with their palette fade, and the ending UFO.
 *
 * It borrows what every screen shares from game.c. */
typedef struct {
    Video         *video;
    TileSet       *vram;           /* VRAM tile space                    */
    const TileSet *font;           /* FontsIconsArt as loaded            */
    struct WinSys *win;            /* set up by game.c, controls locked  */
    Palette       *fade_target;    /* $FFFFFB80: what the fades aim for  */
    void         (*queue_sound)(void *ctx, int id);
    void          *ctx;
} EndingHost;

/* At startup, with the other assets: the pictures, the starfield, the
 * credits and their tables, the pages' palettes. */
bool ending_load_assets(void);

/* The screen's setup, with the display off: its art, the starfield, the
 * 64-row planes, the title palette as the fade's target (line 3 black)
 * and every entry white. */
void ending_enter(const EndingHost *host);

/* One frame. True once A, B or C at the end asks for the Sega screen;
 * the planes are 32 rows again by then. */
bool ending_tick(const Input *in);

void ending_render(void);

/* For tools/route_probe.c: the moment the ending is at -- "setup",
 * "fade_in", "pageN_draw", "pageN_slide", "pageN_text", "pageN_cleared"
 * (N 1-6), "last_page_hold", "wipe", "last_picture", "to_white", "white",
 * "fade_in_again", "final_days_slide", "final_days_text",
 * "final_days_cleared", "credits", "ufo" (while the UFO is drawn),
 * "button" -- and whether it waits on the pad. */
const char *ending_debug_moment(void);
bool        ending_waits_on_pad(void);

#endif /* PS2_ENDING_H */
