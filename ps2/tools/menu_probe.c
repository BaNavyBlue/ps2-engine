/* Diagnostic: the C menu, headless. Starts in the weapon store, hands
 * Rolf a few items (EQP=1: worn ones, TECH=1: three techniques,
 * NEI_ALIVE=1: Nei on her feet), and presses buttons from a script of
 * frame:button pairs -- a b c u d s(tart) -- printing the window stack
 * and the event routine whenever either changes, then the party.
 *
 *     make menu_probe
 *     ./menu_probe "200:b,260:b,320:b,900:c,960:c,1000:c,1040:c" 1400
 *
 * leaves the store (three B), opens the menu on the map at 900, and
 * picks ITEM, Rolf, his first item and USE. */
#include "../src/core/assetfs.h"
#include "../src/core/video.h"
#include "../src/core/audio.h"
#include "../src/game/game.h"
#include "../src/game/gamedata.h"
#include "../src/game/window.h"
#include "../src/platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



static void press(Input *in, char c)
{
    switch (c) {
        case 'a': { in->held.a = in->pressed.a = true; break; }
        case 'b': { in->held.b = in->pressed.b = true; break; }
        case 'c': { in->held.c = in->pressed.c = true; break; }
        case 'u': { in->held.up = in->pressed.up = true; break; }
        case 'd': { in->held.down = in->pressed.down = true; break; }
        case 's': { in->held.start = in->pressed.start = true; break; }
        default: { break; }
    }
}

static void name_of(const u8 *n, char *out)
{
    for (int i = 0; i < 4; i++) {
        u8 c = n[i];
        out[i] = (c >= 11 && c <= 36) ? (char)('A' + c - 11) : (c == 0xC4 || c == 0) ? ' ' : '?';
    }
    out[4] = 0;
}

int main(int argc, char **argv)
{
    const char *script = argc > 1 ? argv[1] : "";
    int total = argc > 2 ? atoi(argv[2]) : 1200;
    assetfs_preload("assets/manifest.txt");
    static Video video; video_init(&video, NULL);
    static Game game; if (!game_init(&game, &video)) { return 1; }
    static Audio audio; audio_init(&audio, AUDIO_RATE); audio_load_music(&audio, "assets");
    game_attach_audio(&game, &audio);
    game_debug_building(&game, 5, 13, 5, 2, 57, 18);

    GameData *gd = game_debug_data(&game);
    /* Rolf: some items and techniques, Nei in the party. */
    CharData *rolf = &gd->chr[0];
    if (getenv("EQP")) {
        rolf->item_count = 5;
        rolf->items[0] = 0xD6; rolf->items[1] = 0xAB; rolf->items[2] = 0xBF; rolf->items[3] = 0x2D; rolf->items[4] = 0x47;
    } else {
        rolf->item_count = 5;
        rolf->items[0] = 0x11; rolf->items[1] = 0x12; rolf->items[2] = 0x14; rolf->items[3] = 0x16; rolf->items[4] = 0x0E;
    }
    if (getenv("TECH")) { rolf->techs_b = 2; rolf->techs_a = 0; }

    rolf->hp = 10; rolf->tp = 20;
    gd->chr[1].hp = 0;  /* Nei dead, to test the messages */
    if (getenv("NEI_ALIVE")) { gd->chr[1].hp = 5; }
    if (getenv("TECH")) { rolf->techs_b = 3; }
    gd->chr[1].item_count = 1; gd->chr[1].items[0] = 0x15;

    WinSys *ws = game_debug_win(&game);
    int last_depth = -1, last_er = -1;
    for (int f = 0; f < total; f++) {
        Input in; input_clear(&in);   /* (build 160) nothing pressed */
        for (const char *p = script; *p; ) {
            int fr = atoi(p); const char *colon = strchr(p, ':'); if (!colon) break;
            if (fr == f) { press(&in, colon[1]); }
            p = strchr(colon, ','); if (!p) break; p++;
        }
        game_tick(&game, &in);
        if (ws->depth != last_depth || ws->event_routine != last_er) {
            printf("%5d screen %d depth %d [", f, game.screen, ws->depth);
            for (int i = 0; i < ws->depth; i++) { printf(" %02X", ws->stack[i].id); }
            printf(" ] er=%d sub=%d sub2=%d choice=%d saved=%02X q0=%04X active=%d\n", ws->event_routine, ws->event_routine_sub, ws->event_routine_sub_2, ws->menu_choice, ws->window_index_saved, ws->window_index[0], ws->window_active_flag);
            last_depth = ws->depth; last_er = ws->event_routine;
        }
    }
    char n[5];
    for (int i = 0; i < 2; i++) { name_of(gd->chr[i].name, n); printf("%s hp=%d/%d tp=%d status=%04X items:", n, gd->chr[i].hp, gd->chr[i].max_hp, gd->chr[i].tp, gd->chr[i].status); for (int k = 0; k < gd->chr[i].item_count; k++) { printf(" %02X", gd->chr[i].items[k]); } printf(" equip:"); for (int k = 0; k < 5; k++) { printf(" %02X", gd->chr[i].equip[k]); } printf("\n"); }
    printf("party:"); for (int i = 0; i < 4; i++) { printf(" %04X", gd->party_member_id[i]); } printf(" n=%d\n", gd->party_members_num);
    return 0;
}
