/* Diagnostic: runs IntroScr_EventSelectGame end to end with a scripted
 * pad, against real save files in ./saves. Usage: intro_probe [OUTDIR] */
#include "../src/core/palette.h"
#include "../src/core/script.h"
#include "../src/core/tileset.h"
#include "../src/game/intro_select.h"

#include <stdio.h>
#include <string.h>

static ScriptBlock g_intro;
static Video       g_video;
static Framebuffer g_fb;

static const char *lookup(void *ctx, u16 id)
{
    (void)ctx;
    return ((id >> 8) == 0x13) ? script_entry(&g_intro, id & 0xFF) : NULL;
}

static void dump(const char *dir, const char *tag)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, tag);
    FILE *fh = fopen(path, "wb");
    if (fh == NULL) {
        return;
    }
    fprintf(fh, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        Rgb8 c = cram_to_rgb8(g_video.palette.entry[g_fb.px[i]]);
        unsigned char rgb[3] = { c.r, c.g, c.b };
        fwrite(rgb, 1, 3, fh);
    }
    fclose(fh);
}

/* The pad script advances only while the game is waiting on the player
 * -- a menu is live, or text has stopped at a pause -- so it does not
 * depend on how long anything takes to type or animate. */
typedef struct {
    const char *pad;        /* U D L R B C, 's' = snapshot */
    const char *label;
} Scenario;

#define SETTLE_FRAMES 20

static bool waiting_on_player(const WinSys *ws)
{
    if (ws->window_index[0] != 0) {
        return false;
    }
    if (ws->window_index_saved != 0) {
        return true;
    }
    u8 code = ws->text[ws->text_pos];
    return ws->window_active_flag != 0
        && (code == TXT_PAUSE || code == TXT_END);
}

static IntroSelectResult run(const Scenario *sc, GameData *gd,
                             bool *checked, const char *dir)
{
    WinSys      ws;
    IntroSelect is;
    IntroSelectResult result = INTRO_SELECT_RUNNING;
    int shots = 0;

    win_init(&ws);
    ws.script_lookup = lookup;
    ws.slot_info     = intro_select_slot_info;
    gamedata_new_game(gd);
    intro_select_begin(&is, &ws, checked);

    size_t steps = strlen(sc->pad);
    size_t step  = 0;
    int settled  = 0;

    for (int f = 0; f < 20000; f++) {
        Input in;
        input_clear(&in);   /* (build 160) no key, no gamepad input */

        bool snap = false;
        settled = waiting_on_player(&ws) ? settled + 1 : 0;
        if (settled >= SETTLE_FRAMES) {
            if (step >= steps) {
                break;
            }
            settled = 0;
            switch (sc->pad[step++]) {
                case 'U': {
                    in.pressed.up = true;
                    break;
                }
                case 'D': {
                    in.pressed.down = true;
                    break;
                }
                case 'L': {
                    in.pressed.left = true;
                    break;
                }
                case 'R': {
                    in.pressed.right = true;
                    break;
                }
                case 'B': {
                    in.pressed.b = true;
                    break;
                }
                case 'C': {
                    in.pressed.c = true;
                    break;
                }
                default: {
                    snap = true;
                    break;
                }
            }
        }

        win_run_cursors(&ws, &in);
        result = intro_select_tick(&is, &ws, gd);
        if (result != INTRO_SELECT_RUNNING) {
            break;
        }
        win_prepare(&ws, &in);
        win_process(&ws);

        if (snap) {
            Cell blank;
            char tag[64];
            memset(&blank, 0, sizeof blank);
            video_fill(&g_video, LAYER_A, blank);
            video_sprites_begin(&g_video);
            win_render(&ws, &g_video, 0x500, 0, 0);
            video_compose(&g_video, &g_fb);
            snprintf(tag, sizeof tag, "%s_%d", sc->label, shots++);
            dump(dir, tag);
        }
    }
    printf("%-10s -> result %d, event_routine %u\n", sc->label, result,
           ws.event_routine);
    return result;
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    TileSet  vram, font;
    GameData gd;
    bool     checked = false;

    if (!tileset_alloc(&vram, 0x800)
        || !tileset_load(&font, "assets/tiles/FontsIconsArt.png")
        || !script_load(&g_intro, "assets/script/Script_Introduction.txt")
        || !win_load_assets() || !gamedata_load_tables()) {
        return 1;
    }
    tileset_place(&vram, &font, 0x500);
    video_init(&g_video, &vram);
    palette_load_entry(&g_video.palette, 0, 0x01);    /* PALETTE_TITLE_SCREEN */

    for (int s = 0; s < GD_SAVE_SLOTS; s++) {
        gamedata_erase(s);
    }

    /* 1: no saves. NEW GAME, type "BC", END. */
    static const Scenario NEW_GAME = { "C" "RCRC" "URR" "sC" "C", "new" };
    if (run(&NEW_GAME, &gd, &checked, dir) == INTRO_SELECT_NEW_GAME) {
        /* What Data Memory would do later: label the slot and save. */
        memcpy(gd.save_name, gd.chr[0].name, GD_NAME_LEN);
        gd.save_level = gd.chr[0].level;
        gamedata_save(&gd, 1);
    }

    /* 2: CONTINUE, slot 1 is empty, then slot 2. */
    static const Scenario CONTINUE = { "DC" "C" "sC" "DsC", "continue" };
    GameData loaded;
    if (run(&CONTINUE, &loaded, &checked, dir) == INTRO_SELECT_CONTINUE) {
        printf("loaded: money %lu, map %u, x %u y %u, Rolf HP %u ATK %u "
               "DEF %u AGL %u, items %u\n",
               (unsigned long)loaded.money, loaded.level_index,
               loaded.level_x, loaded.level_y, loaded.chr[0].hp,
               loaded.chr[0].attack, loaded.chr[0].defense,
               loaded.chr[0].agility, loaded.chr[0].item_count);
    }

    /* 3: a broken file in slot 3 meets a fresh backup check. */
    FILE *fh = fopen("saves/slot3.json", "w");
    if (fh != NULL) {
        fputs("{ \"format\": 1, \"money\": }", fh);
        fclose(fh);
    }
    checked = false;

    /* ERASE slot 2: answer yes, land back on the menu, B to the title. */
    static const Scenario ERASE = {
        "sC" "CC" "sDDC" "DC" "sC" "C" "sB", "erase" };
    run(&ERASE, &gd, &checked, dir);

    SlotSummary s;
    gamedata_slot_summary(1, &s);
    printf("slot 2 exists after erase: %d\n", s.exists);
    gamedata_slot_summary(2, &s);
    printf("slot 3 exists after check: %d\n", s.exists);
    return 0;
}
