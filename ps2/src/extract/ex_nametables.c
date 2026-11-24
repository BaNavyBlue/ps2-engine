/* (build 189) tools/extract_nametables.py in C: the screens' nametables
 * (the words DrawArtTiles writes to the VDP: P CC V H, an 11-bit tile) as
 * Tiled maps, assets/nametables/<name>.tmx, and nametables.json. A
 * table's bytes are its label's and those of the labels that continue its
 * run; each cell is drawn from the art sheet video memory holds at its
 * tile when the game shows it (the later of two loaded over each other),
 * in a tileset a sheet, palette line and priority, flipped by Tiled's
 * flags. The sheets' sizes are the cartridge's (exsheets.c). The tables
 * below are extract_nametables.py's, written in from it by a script. */

#include "excommon.h"
#include "exsheets.h"

#include <stdlib.h>
#include <string.h>

void ex_step_nametables(Extract *x, const ExArgs *a);

#define FLIP_H 0x80000000u
#define FLIP_V 0x40000000u

/* NAMETABLES: label, width, height, the labels whose bytes continue
 * the run (NULL-ended) */
typedef struct {
    const char *label;
    int         w;
    int         h;
    const char *more[8];
} Table;

static const Table NAMETABLES[] = {
    { "SegaLogoTileInd", 12, 4, { NULL } },
    { "TitleScrBGTileInd", 40, 28, { "loc_6C5DA", NULL } },
    { "loc_6C5DA", 40, 28, { NULL } },
    { "TitleScrPlaneATileInd", 40, 28, { NULL } },
    { "loc_70A32", 28, 14, { NULL } },
    { "loc_71052", 28, 14, { NULL } },
    { "loc_75BD8", 40, 16, { NULL } },
    { "loc_AF4EA", 28, 14, { NULL } },
    { "loc_AF7FA", 6, 4, { NULL } },
    { "loc_70D42", 28, 14, { NULL } },
    { "TylerPortraitTileInd", 13, 12, { NULL } },
    { "TylerSpcshp_MonitorTileInd", 15, 60, { NULL } },
    { "loc_716BA", 6, 6, { NULL } },
    { "loc_AE8A0", 40, 14, { NULL } },
    { "LutzPortraitTileInd", 30, 14, { NULL } },
    { "loc_B7572", 40, 28, { NULL } },
    { "loc_B5A2C", 16, 24, { NULL } },
    { "loc_B5D2C", 16, 24, { NULL } },
    { "loc_B662C", 16, 24, { NULL } },
    { "loc_B692C", 16, 24, { NULL } },
    { "loc_B602C", 16, 24, { NULL } },
    { "loc_B632C", 16, 24, { NULL } },
    { "loc_B55CC", 40, 14, { "loc_B561C", "loc_B566C", "loc_B56BC", "loc_B570C", "loc_B575C", "loc_B57AC", NULL } },
};

/* main()'s output_names */
static const ExNamePair OUTPUT_NAMES[] = {
    { "loc_6C5DA", "GameOverBGTileInd" },
    { "loc_70A32", "DreamBattleTileInd" },
    { "loc_71052", "DreamWakingTileInd" },
    { "loc_75BD8", "BattleFloorTileInd" },
    { "loc_AF4EA", "NeiDeathTileInd" },
    { "loc_AF7FA", "NeiBodyTileInd" },
    { "loc_70D42", "DreamHelpTileInd" },
    { "loc_716BA", "SpaceshipPlanetTileInd" },
    { "loc_AE8A0", "MotherBrainMachineTileInd" },
    { "loc_B7572", "EndingStarfieldTileInd" },
    { "loc_B5A2C", "EndingPage1PortraitTileInd" },
    { "loc_B5D2C", "EndingPage2PortraitTileInd" },
    { "loc_B662C", "EndingPage3PortraitTileInd" },
    { "loc_B692C", "EndingPage4PortraitTileInd" },
    { "loc_B602C", "EndingPage5PortraitTileInd" },
    { "loc_B632C", "EndingPage6PortraitTileInd" },
    { "loc_B55CC", "EndingLastPictureTileInd" },
    { NULL, NULL },
};

/* SHEETS: each table's sheets in the order loaded, (sheet, first
 * tile); a NULL sheet ends a list */
typedef struct {
    const char *art;
    int         base;
} Sheet;

typedef struct {
    const char *name;
    Sheet       sheets[8];
} Sheets;

static const Sheets SHEETS[] = {
    { "SegaLogoTileInd", { { "SegaLogoArt", 0x000 }, { NULL, 0 } } },
    { "TitleScrBGTileInd", {
        { "title_background_art", 0x001 },
        { "title_copyright_art", 0x300 },
        { "FontsIconsArt", 0x500 },
        { "title_figures_art", 0x0E1 },
        { "title_logo_art", 0x200 },
        { NULL, 0 } } },
    { "TitleScrPlaneATileInd", {
        { "title_background_art", 0x001 },
        { "title_copyright_art", 0x300 },
        { "FontsIconsArt", 0x500 },
        { "title_figures_art", 0x0E1 },
        { "title_logo_art", 0x200 },
        { NULL, 0 } } },
    { "GameOverBGTileInd", {
        { "title_background_art", 0x001 },
        { NULL, 0 } } },
    { "DreamBattleTileInd", { { "DreamBattleArt", 0x001 }, { NULL, 0 } } },
    { "DreamWakingTileInd", { { "DreamWakingArt", 0x001 }, { NULL, 0 } } },
    { "DreamHelpTileInd", { { "DreamHelpArt", 0x001 }, { NULL, 0 } } },
    { "NeiDeathTileInd", { { "NeiDeathArt", 0x001 }, { NULL, 0 } } },
    { "NeiBodyTileInd", { { "NeiDeathArt", 0x001 }, { NULL, 0 } } },
    { "TylerPortraitTileInd", {
        { "tyler_spaceship_art", 0x001 },
        { NULL, 0 } } },
    { "TylerSpcshp_MonitorTileInd", {
        { "tyler_spaceship_art", 0x001 },
        { NULL, 0 } } },
    { "SpaceshipPlanetTileInd", {
        { "SpaceshipPlanetArt", 0x001 },
        { NULL, 0 } } },
    { "BattleFloorTileInd", {
        { "FontsIconsArt", 0x500 },
        { "battle_floor_art", 0x781 },
        { NULL, 0 } } },
    { "MotherBrainMachineTileInd", {
        { "battle_mombrain_art", 0x001 },
        { NULL, 0 } } },
    { "LutzPortraitTileInd", { { "LutzPortraitArt", 0x001 }, { NULL, 0 } } },
    { "EndingStarfieldTileInd", {
        { "EndingStarfieldUfoArt", 0x480 },
        { NULL, 0 } } },
    { "EndingLastPictureTileInd", {
        { "EndingPicturesArt", 0x001 },
        { NULL, 0 } } },
    { "EndingPage1PortraitTileInd", {
        { "EndingPicturesArt", 0x001 },
        { NULL, 0 } } },
    { "EndingPage2PortraitTileInd", {
        { "EndingPicturesArt", 0x001 },
        { NULL, 0 } } },
    { "EndingPage3PortraitTileInd", {
        { "EndingPicturesArt", 0x001 },
        { NULL, 0 } } },
    { "EndingPage4PortraitTileInd", {
        { "EndingPicturesArt", 0x001 },
        { NULL, 0 } } },
    { "EndingPage5PortraitTileInd", {
        { "EndingPicturesArt", 0x001 },
        { NULL, 0 } } },
    { "EndingPage6PortraitTileInd", {
        { "EndingPicturesArt", 0x001 },
        { NULL, 0 } } },
};

static const Sheets *sheets_of(Extract *x, const char *name)
{
    for (size_t i = 0; i < sizeof SHEETS / sizeof SHEETS[0]; i++) {
        if (strcmp(SHEETS[i].name, name) == 0) {
            return &SHEETS[i];
        }
    }
    ex_fail(x, "%s: no SHEETS entry", name);
}

typedef struct {
    int  art;           /* index into the table's sheets, -1 for none */
    int  line;
    int  prio;
    int  local;
    bool hf;
    bool vf;
} Cell;

typedef struct {
    int art;
    int line;
    int prio;
    int first;
} Combo;

static void write_tmx(Extract *x, const char *path, const char *file,
                      int width, int height, const u16 *words,
                      const Sheets *sheets, ExSheets *art)
{
    int nsheets = 0;
    const ExSheet *size[8];
    while (nsheets < 8 && sheets->sheets[nsheets].art != NULL) {
        size[nsheets] = ex_sheet(art, sheets->sheets[nsheets].art);
        nsheets++;
    }
    int n = width * height;
    Cell *cells = ex_alloc(x, sizeof *cells * (size_t)(n + 1));
    long long empty = -1;
    for (int k = 0; k < n; k++) {
        u16 w = words[k];
        int tile = w & 0x7FF;
        Cell c = { -1, 0, 0, 0, false, false };
        for (int s = 0; s < nsheets; s++) {
            int base = sheets->sheets[s].base;
            if (base <= tile && tile < base + size[s]->count) {
                c.art   = s;
                c.local = tile - base;
            }
        }
        if (c.art < 0) {
            if (w & ~0x6000) {
                ex_fail(x, "%s: cell %d,%d is $%04X, tile $%03X in no sheet "
                           "and not an empty cell (tile 0, a palette line, "
                           "nothing else)", file, k % width, k / width, w,
                        tile);
            }
            if (empty >= 0 && empty != w) {
                ex_fail(x, "%s: cell %d,%d is $%04X, another empty cell "
                           "$%04llX", file, k % width, k / width, w, empty);
            }
            empty = w;
            cells[k] = c;
            continue;
        }
        c.line = (w >> 13) & 3;
        c.prio = w >> 15;
        c.hf   = ((w >> 11) & 1) != 0;
        c.vf   = ((w >> 12) & 1) != 0;
        cells[k] = c;
    }

    /* a tileset a sheet, line and priority the table uses, in SHEETS'
     * order, each sheet's sorted by (line, priority) */
    Combo combos[8 * 8];
    int ncombos = 0;
    int gid = 1;
    for (int s = 0; s < nsheets; s++) {
        bool used[4][2];
        memset(used, 0, sizeof used);
        for (int k = 0; k < n; k++) {
            if (cells[k].art == s) {
                used[cells[k].line][cells[k].prio] = true;
            }
        }
        for (int line = 0; line < 4; line++) {
            for (int prio = 0; prio < 2; prio++) {
                if (!used[line][prio]) {
                    continue;
                }
                Combo c = { s, line, prio, gid };
                combos[ncombos++] = c;
                gid += size[s]->count;
            }
        }
    }

    ExStr t;
    ex_str_init(x, &t);
    ex_str_add(&t, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    ex_str_addf(&t, "<map version=\"1.10\" orientation=\"orthogonal\" "
                    "renderorder=\"right-down\" width=\"%d\" height=\"%d\" "
                    "tilewidth=\"8\" tileheight=\"8\" infinite=\"0\" "
                    "nextlayerid=\"2\" nextobjectid=\"1\">\n", width, height);
    ex_str_add(&t, " <properties>\n");
    ex_str_addf(&t, "  <property name=\"empty_palette_line\" type=\"int\" "
                    "value=\"%lld\"/>\n", (empty > 0) ? empty >> 13 : 0);
    ex_str_add(&t, " </properties>\n");
    for (int i = 0; i < ncombos; i++) {
        const Combo *c = &combos[i];
        const char *sheet = sheets->sheets[c->art].art;
        int same = 0;
        for (int k = 0; k < ncombos; k++) {
            if (combos[k].art == c->art) {
                same++;
            }
        }
        const char *name = (same > 1)
            ? ex_fmt(x, "%s_line%d%s", sheet, c->line,
                     c->prio ? "_priority" : "")
            : sheet;
        const ExSheet *sz = size[c->art];
        ex_str_addf(&t, " <tileset firstgid=\"%d\" name=\"%s\" "
                        "tilewidth=\"8\" tileheight=\"8\" tilecount=\"%d\" "
                        "columns=\"%d\">\n", c->first, name, sz->count,
                    sz->across);
        ex_str_add(&t, "  <properties>\n");
        ex_str_addf(&t, "   <property name=\"palette_line\" type=\"int\" "
                        "value=\"%d\"/>\n", c->line);
        ex_str_addf(&t, "   <property name=\"priority\" type=\"int\" "
                        "value=\"%d\"/>\n", c->prio);
        ex_str_addf(&t, "   <property name=\"vram_base\" type=\"int\" "
                        "value=\"%d\"/>\n", sheets->sheets[c->art].base);
        ex_str_add(&t, "  </properties>\n");
        ex_str_addf(&t, "  <image source=\"../tiles/%s.png\" width=\"%d\" "
                        "height=\"%d\"/>\n", sheet, sz->across * 8,
                    sz->rows * 8);
        ex_str_add(&t, " </tileset>\n");
    }
    ex_str_addf(&t, " <layer id=\"1\" name=\"nametable\" width=\"%d\" "
                    "height=\"%d\">\n", width, height);
    ex_str_add(&t, "  <data encoding=\"csv\">\n");
    for (int r = 0; r < height; r++) {
        for (int col = 0; col < width; col++) {
            const Cell *c = &cells[r * width + col];
            unsigned long long g = 0;
            if (c->art >= 0) {
                for (int i = 0; i < ncombos; i++) {
                    if (combos[i].art == c->art && combos[i].line == c->line
                        && combos[i].prio == c->prio) {
                        g = (unsigned long long)(combos[i].first + c->local);
                    }
                }
                g |= c->hf ? FLIP_H : 0;
                g |= c->vf ? FLIP_V : 0;
            }
            ex_str_addf(&t, "%s%llu", (col > 0) ? "," : "", g);
        }
        ex_str_add(&t, (r + 1 < height) ? ",\n" : "\n");
    }
    ex_str_add(&t, "</data>\n </layer>\n</map>\n");
    ex_write_text(x, path, t.buf);
}

void ex_step_nametables(Extract *x, const ExArgs *a)
{
    ExSheets *art = ex_sheets(x);
    ex_mkdirs(x, a->out);
    /* (build 135) the .nam containers are Tiled maps now */
    ex_remove_ending(x, a->out, ".nam");

    /* merged into what is there: a table the image is too short for
     * keeps its old map and entry */
    char *manifest_path = ex_path(x, a->out, "nametables.json");
    JOut *manifest = NULL;
    if (ex_read_file(x, manifest_path, NULL) != NULL) {
        manifest = jo_from(x, ex_read_json(x, manifest_path));
    } else {
        manifest = jo_obj(x);
    }
    for (size_t i = 0; i < sizeof NAMETABLES / sizeof NAMETABLES[0]; i++) {
        const Table *tb = &NAMETABLES[i];
        ExStr data;
        ex_str_init(x, &data);
        size_t len = 0;
        const u8 *b = ex_blob(x, tb->label, &len);
        ex_str_addn(&data, (const char *)b, len);
        for (int k = 0; tb->more[k] != NULL; k++) {
            b = ex_blob(x, tb->more[k], &len);
            ex_str_addn(&data, (const char *)b, len);
        }
        size_t need = (size_t)tb->w * (size_t)tb->h * 2;
        if (data.len < need) {
            ex_warn(x, "  %s: only %zu of %zu bytes", tb->label, data.len,
                    need);
            continue;
        }
        const u8 *d = (const u8 *)data.buf;
        u16 *words = ex_alloc(x, sizeof *words * (need / 2 + 1));
        for (size_t k = 0; k < need / 2; k++) {
            words[k] = (u16)(d[2 * k] << 8 | d[2 * k + 1]);
        }
        const char *name = ex_pair(OUTPUT_NAMES, tb->label);
        if (name == NULL) {
            name = tb->label;
        }
        char *file = ex_fmt(x, "%s.tmx", name);
        char *out = ex_path(x, a->out, file);
        write_tmx(x, out, file, tb->w, tb->h, words, sheets_of(x, name), art);
        JOut *entry = jo_obj(x);
        jo_seti(x, entry, "width", tb->w);
        jo_seti(x, entry, "height", tb->h);
        jo_set(x, manifest, name, entry);
        ex_say(x, "%s: %dx%d, %zu bytes -> %s", tb->label, tb->w, tb->h,
               need, out);
    }
    ex_write_text(x, manifest_path, jo_dump(x, manifest, 1, true));
}
