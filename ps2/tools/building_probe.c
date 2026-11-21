/* Diagnostic: every ported building with a scripted pad, from a fresh
 * Rolf and Nei. Usage: building_probe [OUTDIR]
 *
 * Each scenario prints the scripts it showed, the state of the party's
 * money and bags after, and writes a PPM wherever the pad has an 's'.
 * The pad advances only when the game is waiting on the player. */
#include "../src/core/palette.h"
#include "../src/core/script.h"
#include "../src/core/tileset.h"
#include "../src/core/json.h"
#include "../src/game/building.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BANKS 32
static ScriptBlock g_bank[BANKS];
static Video       g_video;
static Framebuffer g_fb;
static u16         g_last_script;
static char        g_shown[512];

static const char *lookup(void *ctx, u16 id)
{
    (void)ctx;
    int bank = id >> 8;
    if (bank >= BANKS) {
        return NULL;
    }
    if (id != g_last_script) {
        char tag[8];
        snprintf(tag, sizeof tag, " %03X", id);
        strncat(g_shown, tag, sizeof g_shown - strlen(g_shown) - 1);
        g_last_script = id;
    }
    return script_entry(&g_bank[bank], id & 0xFF);
}

static void load_banks(void)
{
    JsonValue *m = json_parse_file("assets/script/script.json");
    for (const JsonValue *e = (m != NULL) ? m->child : NULL; e != NULL;
         e = e->next) {
        int bank = (int)json_int(e, "bank", -1);
        if (bank >= 0 && bank < BANKS) {
            char path[160];
            snprintf(path, sizeof path, "assets/script/%s",
                     json_str(e, "file", ""));
            script_load(&g_bank[bank], path);
        }
    }
    json_free(m);
}

static void slot_info(void *ctx, int slot, WinSlotInfo *out)
{
    (void)ctx;
    SlotSummary s;
    gamedata_slot_summary(slot, &s);
    out->used  = s.exists;
    memcpy(out->name, s.name, sizeof out->name);
    out->level = s.level;
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
        /* (build 146a) fb_palette_colour: a portrait's pixels are its
         * picture's, with no palette entry behind them. */
        Rgb8 c = fb_palette_colour(&g_fb, &g_video.palette, i);
        unsigned char rgb[3] = { c.r, c.g, c.b };
        fwrite(rgb, 1, 3, fh);
    }
    fclose(fh);
}

/* 1: a menu is live or text has stopped for a button. 2: nothing is
 * live, nothing typing, but the building has not let go -- it may be
 * reading the pad itself (Rolf's house under a profile), or counting
 * down (the hospital's 30-frame pauses), so that case waits longer. */
static int waiting_on_player(const WinSys *ws)
{
    if (ws->window_index[0] != 0) {
        return 0;
    }
    if (ws->window_index_saved != 0) {
        return 1;
    }
    u8 code = ws->text[ws->text_pos];
    if (ws->window_active_flag != 0) {
        return (code == TXT_PAUSE || code == TXT_END) ? 1 : 0;
    }
    return (ws->depth > 0) ? 2 : 0;
}

typedef struct {
    int         building, portrait, store_max, store_index;
    const char *pad;
    const char *label;
    bool        demo;
} Scenario;

#define SETTLE_FRAMES 20
#define IDLE_FRAMES   90

static void bags(const GameData *gd)
{
    printf("  money %lu", (unsigned long)gd->money);
    for (int c = 0; c < 2; c++) {
        const CharData *d = &gd->chr[c];
        printf(" | %s(%u):", c == 0 ? "Rolf" : "Nei", d->item_count);
        for (int i = 0; i < d->item_count && i < GD_ITEM_SLOTS; i++) {
            printf(" %s%s", gamedata_item_name(d->items[i]),
                   (d->items[i] & 0x80) ? "*" : "");
        }
        printf(" hp %u/%u tp %u/%u st %04X", d->hp, d->max_hp, d->tp,
               d->max_tp, d->status);
    }
    printf("\n");
}

static void run(const Scenario *sc, GameData *gd, Rng *rng, const char *dir)
{
    WinSys        ws;
    BuildingState bs;
    int shots = 0;

    memset(&bs, 0, sizeof bs);
    bs.building    = sc->building;
    bs.portrait    = sc->portrait;
    bs.store_max   = sc->store_max;
    bs.store_index = sc->store_index;
    bs.rng         = rng;
    bs.demo_flag   = sc->demo;

    win_init(&ws);
    ws.script_lookup = lookup;
    ws.gd            = gd;
    ws.slot_info     = slot_info;
    building_begin(&ws, &bs);
    g_shown[0]    = '\0';
    g_last_script = 0;

    size_t steps = strlen(sc->pad);
    size_t step  = 0;
    int settled  = 0;
    BuildingResult result = BUILDING_RUNNING;

    for (int f = 0; f < 30000; f++) {
        Input in;
        input_clear(&in);   /* (build 160) no key, no gamepad input */

        bool snap = false;
        int wait = waiting_on_player(&ws);
        settled = (wait != 0) ? settled + 1 : 0;
        if ((wait == 1 && settled >= SETTLE_FRAMES)
            || (wait == 2 && settled >= IDLE_FRAMES)) {
            if (step >= steps) {
                break;
            }
            settled = 0;
            if (getenv("TRACE")) {
                fprintf(stderr, "  f%d press %c  er=%u sub=%u saved=%02X depth=%d top=%02X text=%02X act=%u\n",
                        f, sc->pad[step], ws.event_routine, ws.event_routine_sub,
                        ws.window_index_saved, ws.depth,
                        ws.depth ? ws.stack[ws.depth-1].id : 0,
                        ws.text[ws.text_pos], ws.window_active_flag);
            }
            switch (sc->pad[step++]) {
                case 'L': {
                    in.pressed.left = true;
                    break;
                }
                case 'R': {
                    in.pressed.right = true;
                    break;
                }
                case 'U': {
                    in.pressed.up = true;
                    break;
                }
                case 'D': {
                    in.pressed.down = true;
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

        if (getenv("FRAMES") && strcmp(sc->label, getenv("FRAMES")) == 0 && f < 60) {
            fprintf(stderr, "f%02d er=%u w0=%04X saved=%02X depth=%d act=%u sid=%04X pos=%d code=%02X\n",
                    f, ws.event_routine, ws.window_index[0], ws.window_index_saved,
                    ws.depth, ws.window_active_flag, ws.script_id[0], ws.text_pos,
                    ws.text[ws.text_pos]);
        }
        win_run_cursors(&ws, &in);
        result = building_tick(&bs, &ws, gd, &in);
        if (result != BUILDING_RUNNING && result != BUILDING_LOAD_PORTRAIT) {
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
    static const char *const WHY[] = { "STUCK", "portrait", "street",
                                       "changed", "reset" };
    printf("%-12s -> %s after %zu/%zu presses, scripts:%s\n", sc->label,
           WHY[result], step, steps, g_shown);
    if (result == BUILDING_CHANGED || bs.demo_index != 0) {
        printf("  -> building %d portrait $%02X demo %d\n", bs.building,
               bs.portrait, bs.demo_index);
    }
    bags(gd);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    TileSet  vram, font;
    GameData gd;
    Rng      rng;

    if (!tileset_alloc(&vram, 0x800)
        || !tileset_load(&font, "assets/tiles/FontsIconsArt.png")
        || !gamedata_load_tables() || !win_load_assets()) {
        return 1;
    }
    load_banks();
    tileset_place(&vram, &font, 0x500);
    video_init(&g_video, &vram);
    palette_load_entry(&g_video.palette, 0, 0x02);    /* PALETTE_FIELD_COMMON */
    rng_init(&rng, 0x2A6D365Au);

    gamedata_new_game(&gd);
    gd.party_members_num    = 1;
    gd.party_members_joined = 1;
    gd.party_member_id[0]   = 0;
    gd.party_member_id[1]   = 1;
    gd.level_index          = 4;
    gd.level_x              = 0x390;
    gd.level_y              = 0x120;
    printf("start\n");
    bags(&gd);

    /* Hospital (Paseo door: 4, $0A, 8, 0). Nobody hurt: $509 $50A, no. */
    static const Scenario HEAL_NONE = {
        4, 0x0A, 8, 0, "C" "C" "sDC" "C", "heal-none", false };
    run(&HEAL_NONE, &gd, &rng, dir);

    /* Both hurt: 2 x 8 = 16 meseta, yes, then no more. */
    gd.chr[0].hp = 3;
    gd.chr[1].tp = 1;
    static const Scenario HEAL = {
        4, 0x0A, 8, 0, "C" "sC" "s" "DC" "C", "heal", false };
    run(&HEAL, &gd, &rng, dir);

    /* Cure: Nei poisoned, Rolf not. Rolf first ($502), yes to more,
     * then Nei, 10 meseta. */
    gd.chr[1].status |= 0x8000;
    static const Scenario CURE = {
        4, 0x0A, 8, 0,
        "DC" "C"        /* CURE, Rolf -> $502 $50A          */
        "C" "sC"        /* YES: back to HEAL/CURE (on CURE) */
        "C" "DC"        /* Nei -> $503                      */
        "sC" "s"        /* YES, paid, cured                 */
        "DC" "C",       /* NO -> $506                       */
        "cure", false };
    run(&CURE, &gd, &rng, dir);

    /* Clone lab (3, $0B): Nei dead at level 1 -> 20 meseta. */
    gd.chr[1].hp = 0;
    static const Scenario CLONE = {
        3, 0x0B, 0, 0,
        "C"             /* $401 pause                        */
        "DC"            /* Nei -> $403                       */
        "C" "sC"        /* pause, YES -> $406 $405           */
        "DC" "C",       /* NO -> $404                        */
        "clone", false };
    run(&CLONE, &gd, &rng, dir);

    /* The same after Neifirst (demo_flag): Rolf dead, revived free, and
     * off to the tower on demo 7. */
    gd.chr[0].hp = 0;
    static const Scenario CLONE_DEMO = {
        3, 0x0B, 0, 0, "CCCCCCCCCCCC", "clone-demo", true };
    run(&CLONE_DEMO, &gd, &rng, dir);

    /* Data Memory (2, 9): report, yes, slot 3, name "AB", then stop
     * playing -> reset. */
    gamedata_erase(2);
    static const Scenario SAVE = {
        2, 0x09, 0, 0,
        "C" "C" "C"     /* welcome, Rolf's and Nei's reports */
        "C" "sC"        /* pause in $303, YES -> $305        */
        "DDC"           /* slot 3 -> $307 (empty)            */
        "C" "RC"        /* A, B on the name grid             */
        "URR" "sC"      /* up to ADV, right to RUB, END      */
        "C"             /* $308 waits                        */
        "sDC" "C",      /* $309, NO -> $30A, then reset      */
        "save", false };
    run(&SAVE, &gd, &rng, dir);
    SlotSummary ss;
    gamedata_slot_summary(2, &ss);
    printf("  slot 3: exists %d level %d name %02X %02X %02X %02X\n",
           ss.exists, ss.level, ss.name[0], ss.name[1], ss.name[2],
           ss.name[3]);

    /* CONTINUE's arrival: "We meet again", the report, "Be careful". */
    static const Scenario WELCOME_BACK = {
        2, 0x09, 1, 0, "CCCC", "continue", false };
    run(&WELCOME_BACK, &gd, &rng, dir);

    /* Teleport station (15, $1E): only Paseo known, so no service. */
    memset(gd.c700, 0, 13);
    gd.c700[4] = 4;
    static const Scenario TELE_NONE = {
        15, 0x1E, 0, 0, "CCCCCC", "tele-none", false };
    run(&TELE_NONE, &gd, &rng, dir);

    /* Arima and Oputa known: go to Oputa. */
    gd.c700[5] = 5;
    gd.c700[6] = 6;
    gd.money = 500;
    static const Scenario TELE = {
        15, 0x1E, 0, 0, "C" "s" "DC" "sC" "C", "teleport", false };
    run(&TELE, &gd, &rng, dir);
    printf("  now level %u at y $%X x $%X\n", gd.level_index, gd.level_y,
           gd.level_x);
    gd.level_index = 4;

    /* Central Tower desk (8, $15): LIBRARY. Then OUTSIDE. */
    static const Scenario TOWER = {
        8, 0x15, 0, 0, "sDC", "tower", false };
    run(&TOWER, &gd, &rng, dir);
    static const Scenario TOWER_OUT = {
        8, 0x15, 0, 0, "DDC" "C", "tower-out", false };
    run(&TOWER_OUT, &gd, &rng, dir);

    /* Rolf's house (1, $37). Nothing new: "home at last", Rolf and Nei,
     * go out again? NO -> the options. SEE STRENGTH on Nei, back, then
     * REORGANIZE ("nobody else"), then OUTSIDE. */
    static const Scenario HOUSE = {
        1, 0x37, 0, 0,
        "C" "C"         /* $908 pause and end               */
        "sDC"           /* NO -> $90A (pause) $90B          */
        "C"             /* $90A's pause; then the options   */
        "sC"            /* SEE STRENGTH                     */
        "DC" "s" "C"    /* Nei; profile up; C closes four   */
        "DC" "C"        /* REORGANIZE -> $929 waits         */
        "DC" "C",       /* the lamp stays on REORGANIZE, so
                           OUTSIDE is one down -> $909      */
        "house", false };
    run(&HOUSE, &gd, &rng, dir);

    /* A knock: Rudo joins (join_next 2), and is renamed "AB". */
    gd.party_member_join_next = 2;
    static const Scenario ARRIVAL = {
        1, 0x37, 0, 0,
        "C"             /* $90C knock                        */
        "CCC"           /* Rudo's two scripts: three pauses  */
        "sC"            /* rename? YES -> $916               */
        "C" "RC" "URR" "sC"  /* A, B, END                    */
        "C"             /* $91A "I will call you AB!"        */
        "DDC" "C",      /* $90E; OUTSIDE -> $909             */
        "arrival", false };
    run(&ARRIVAL, &gd, &rng, dir);
    printf("  joined %u join_next %u, Rudo is %02X %02X %02X %02X\n",
           gd.party_members_joined, gd.party_member_join_next,
           gd.chr[2].name[0], gd.chr[2].name[1], gd.chr[2].name[2],
           gd.chr[2].name[3]);

    /* REORGANIZE with Rudo about: take him, and the party is full at
     * joined == 2 ($92F). */
    static const Scenario REGROUP = {
        1, 0x37, 0, 0,
        "C" "C" "sDC" "C"       /* greeting, NO, $90A pause      */
        "DC"                    /* REORGANIZE -> $90F $910       */
        "sC" "sC",              /* Rudo -> $92F waits, done      */
        "regroup", false };
    run(&REGROUP, &gd, &rng, dir);
    printf("  party %u: %u %u %u %u\n", gd.party_members_num,
           gd.party_member_id[0], gd.party_member_id[1],
           gd.party_member_id[2], gd.party_member_id[3]);

    /* Central Tower room (9, $14): the keeper takes Nei's ribbon, which
     * comes off first, then gives it back to Rolf. */
    static const Scenario KEEP = {
        9, 0x14, 0, 0,
        "C" "C"         /* $E01 pause; KEEP BAGGAGE -> $E02  */
        "DC"            /* Nei -> $E03                       */
        "sC"            /* the ribbon -> $E04 (pause) $E07   */
        "C" "C"         /* $E04's pause and end              */
        "sDC" "C",      /* NO -> $E08                        */
        "keep", false };
    run(&KEEP, &gd, &rng, dir);
    u8 *kc;
    const u8 *kept = gamedata_bag(&gd, GD_KEEPER, &kc);
    printf("  keeper holds %u: %s\n", *kc, gamedata_item_name(kept[0]));

    static const Scenario TAKE = {
        9, 0x14, 0, 0,
        "C" "DC"        /* $E01; BAGGAGE PLEASE -> $E09      */
        "C" "sC"        /* $E09 pause; the ribbon -> $E0D    */
        "C"             /* Rolf -> $E0B (pause)              */
        "C" "C"         /* $E0B's pause and end; $E07        */
        "sDC" "C",      /* NO -> $E08                        */
        "take", false };
    run(&TAKE, &gd, &rng, dir);
    printf("  keeper holds %u\n", *kc);

    /* Library (10, 8): HISTORY, then no more. */
    static const Scenario LIBRARY = {
        10, 0x08, 0, 0,
        "C" "C" "C" "C" /* $1001, $1016 pause+end, $1015 pause */
        "sC"            /* HISTORY -> $1004                  */
        "C" "C" "C"     /* two pauses and the end; $1003     */
        "sDC" "C",      /* NO -> $1002                       */
        "library", false };
    run(&LIBRARY, &gd, &rng, dir);

    /* The shops, as ps2-port_31 had them: pads wait for a live menu or
     * text stopped at {pause} / {end:C4}; {end:C5} does not wait. */
    gd.money = 2000;
    gd.planet = 0;

    /* Weapon store (5, $0D, 5, 2): the Knife for Nei -- the one person
     * it excludes -- no, then for Rolf, no more. */
    static const Scenario WEAPON = {
        5, 0x0D, 5, 2, "sC" "C" "sDC" "sDC" "UC" "sDC" "C" "C", "weapon",
        false };
    run(&WEAPON, &gd, &rng, dir);

    /* Armor store (6, $0E, 5, 1): Headgear for Rolf, more, the Carbon
     * Emel he cannot wear, insist, done. */
    static const Scenario ARMOR = {
        6, 0x0E, 5, 1, "C" "C" "C" "sC" "DDDDDC" "C" "C" "sC" "sDC" "C" "C",
        "armor", false };
    run(&ARMOR, &gd, &rng, dir);

    /* Item store (7, $0C, 4, 0): BUY two Monomates for Rolf. */
    static const Scenario BUY = {
        7, 0x0C, 4, 0, "sC" "C" "C" "sC" "C" "C" "sDC" "C", "buy", false };
    run(&BUY, &gd, &rng, dir);

    /* SELL Nei's worn ribbon; it comes off first. */
    static const Scenario SELL = {
        7, 0x0C, 4, 0, "DC" "DC" "sC" "sC" "DC" "C", "sell", false };
    run(&SELL, &gd, &rng, dir);

    /* B on the stock list: "Later, ace!" and out. */
    static const Scenario LEAVE = { 5, 0x0D, 5, 2, "sB" "sC", "leave",
                                    false };
    run(&LEAVE, &gd, &rng, dir);
    return 0;
}
