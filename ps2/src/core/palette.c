#include "palette.h"

#include "json.h"
#include "png.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Measured 8-bit DAC output levels for a Mega Drive VA4 in normal
 * intensity. The ladder is deliberately non-linear; using an even ramp
 * here shifts every colour in the game. */
static const u8 DAC_LADDER[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };

u8 cram_r(Cram c)
{
    return (u8)((c >> 1) & 7);
}

u8 cram_g(Cram c)
{
    return (u8)((c >> 5) & 7);
}

u8 cram_b(Cram c)
{
    return (u8)((c >> 9) & 7);
}

Cram cram_make(u8 r, u8 g, u8 b)
{
    return (Cram)(((b & 7) << 9) | ((g & 7) << 5) | ((r & 7) << 1));
}

Rgb8 cram_to_rgb8(Cram c)
{
    Rgb8 out;
    out.r = DAC_LADDER[cram_r(c)];
    out.g = DAC_LADDER[cram_g(c)];
    out.b = DAC_LADDER[cram_b(c)];
    return out;
}

/* Nearest ladder level. The eight levels are far enough apart that a
 * colour that came out of cram_to_rgb8 comes back exactly, so a palette
 * that has been through a PNG and an image editor round-trips unless
 * someone deliberately picked a colour the hardware cannot make -- in
 * which case the nearest one it can make is the right answer. */
static u8 ladder_level(u8 v)
{
    u8 best = 0;
    int best_d = 256;
    for (u8 i = 0; i < 8; i++) {
        int d = (int)v - (int)DAC_LADDER[i];
        if (d < 0) {
            d = -d;
        }
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

Cram cram_from_rgb8(u8 r, u8 g, u8 b)
{
    return cram_make(ladder_level(r), ladder_level(g), ladder_level(b));
}

float colour_level(u8 v)
{
    for (int k = 0; k < 7; k++) {
        if (v <= DAC_LADDER[k + 1]) {
            int span = DAC_LADDER[k + 1] - DAC_LADDER[k];
            return (float)k + (float)(v - DAC_LADDER[k]) / (float)span;
        }
    }
    return 7.0f;
}

u8 level_colour(float level)
{
    if (level <= 0.0f) {
        return DAC_LADDER[0];
    }
    if (level >= 7.0f) {
        return DAC_LADDER[7];
    }
    int   k = (int)level;
    float f = level - (float)k;
    float v = (float)DAC_LADDER[k]
            + f * (float)(DAC_LADDER[k + 1] - DAC_LADDER[k]);
    return (u8)(v + 0.5f);
}

static float srgb_to_linear(u8 v)
{
    float f = (float)v / 255.0f;
    return (f <= 0.04045f) ? (f / 12.92f) : powf((f + 0.055f) / 1.055f, 2.4f);
}

RgbLinear cram_to_linear(Cram c)
{
    Rgb8 s = cram_to_rgb8(c);
    RgbLinear out;
    out.r = srgb_to_linear(s.r);
    out.g = srgb_to_linear(s.g);
    out.b = srgb_to_linear(s.b);
    return out;
}

void palette_clear(Palette *p)
{
    memset(p->entry, 0, sizeof p->entry);
}

void palette_copy(Palette *dst, const Palette *src)
{
    memcpy(dst->entry, src->entry, sizeof dst->entry);
}

/* PaletteDecreaseColor (ps2.asm:9105). */
static Cram step_down(Cram c)
{
    if (c == 0) {
        return c;
    }
    if ((c & 0x00E) != 0) {
        return (Cram)(c - 0x002);
    }
    if ((c & 0x0E0) != 0) {
        return (Cram)(c - 0x020);
    }
    if ((c & 0xE00) != 0) {
        return (Cram)(c - 0x200);
    }
    return c;
}

void palette_fade_out_step(Palette *p)
{
    for (int i = 0; i < PAL_TOTAL; i++) {
        p->entry[i] = step_down(p->entry[i]);
    }
}

/* PaletteAddColor (ps2.asm:9046): the comparisons are the words', as the
 * ROM's `cmp.w` / `bhi` make them; the red step has none. */
static Cram step_up(Cram shown, Cram target)
{
    if (shown == target) {
        return shown;
    }
    if ((u16)(shown + 0x200) <= target) {
        return (Cram)(shown + 0x200);
    }
    if ((u16)(shown + 0x020) <= target) {
        return (Cram)(shown + 0x020);
    }
    return (Cram)(shown + 0x002);
}

void palette_fade_in_step(Palette *p, const Palette *target)
{
    for (int i = 0; i < PAL_TOTAL; i++) {
        p->entry[i] = step_up(p->entry[i], target->entry[i]);
    }
}

static Cram step_from_white(Cram shown, Cram target)
{
    if (shown == target) {
        return shown;
    }
    if (shown >= 0x200 && (Cram)(shown - 0x200) >= target) {
        return (Cram)(shown - 0x200);
    }
    if (shown >= 0x20 && (Cram)(shown - 0x20) >= target) {
        return (Cram)(shown - 0x20);
    }
    return (Cram)(shown - 2);
}

void palette_fade_from_white_step(Palette *live, const Palette *target)
{
    for (int i = 0; i < PAL_TOTAL; i++) {
        live->entry[i] = step_from_white(live->entry[i], target->entry[i]);
    }
}

static Cram step_to_white(Cram c)
{
    if (c == 0xEEE) {
        return c;
    }
    if ((c & 0x00E) != 0x00E) {
        return (Cram)(c + 0x002);
    }
    if ((c & 0x0E0) != 0x0E0) {
        return (Cram)(c + 0x020);
    }
    return (Cram)(c + 0x200);
}

void palette_fade_to_white_step(Palette *live)
{
    for (int i = 0; i < PAL_TOTAL; i++) {
        live->entry[i] = step_to_white(live->entry[i]);
    }
}

bool palette_load(Palette *p, int line, const char *path)
{
    /* The palette is an indexed PNG whose PLTE is the palette: 16 wide,
     * one row per line. Nothing is stored but the colours, so editing
     * one in an image editor edits the game's palette. */
    PngImage img;
    if (!png_load_indexed(&img, path)) {
        return false;
    }

    /* One line a row, as PaletteLoad1/2 write only the words the
     * PalettePtrs entry names. The PLTE itself runs to 256 entries (what
     * editors write), and before build 84 all of it was copied: a
     * one-line palette loaded to line 2 zeroed line 3 as well. */
    int first = line * PAL_ENTRIES;
    int count = img.pal_count;
    if (img.height > 0 && count > img.height * PAL_ENTRIES) {
        count = img.height * PAL_ENTRIES;
    }
    if (first + count > PAL_TOTAL) {
        count = PAL_TOTAL - first;
    }

    for (int i = 0; i < count; i++) {
        p->entry[first + i] = cram_from_rgb8(img.pal[i][0], img.pal[i][1],
                                             img.pal[i][2]);
    }

    png_free(&img);
    return true;
}

/* PalettePtrs has 77 entries; room for a few more if a table grows. */
#define PALETTE_ENTRY_MAX  96
#define PALETTE_FILE_MAX   64

static char g_entry_file[PALETTE_ENTRY_MAX][PALETTE_FILE_MAX];
static bool g_entry_table_read;

static void read_entry_table(void)
{
    g_entry_table_read = true;
    JsonValue *table = json_parse_file("assets/palettes/paletteptrs.json");
    if (table == NULL) {
        fprintf(stderr, "palette: no assets/palettes/paletteptrs.json -- "
                        "rerun make extract-paletteptrs\n");
        return;
    }
    int n = json_length(table);
    for (int i = 0; i < n; i++) {
        const JsonValue *e = json_at(table, i);
        long index = json_int(e, "index", -1);
        if (index < 0 || index >= PALETTE_ENTRY_MAX) {
            continue;
        }
        snprintf(g_entry_file[index], PALETTE_FILE_MAX, "%s",
                 json_str(e, "file", ""));
    }
    json_free(table);
}

bool palette_load_entry(Palette *p, int line, int index)
{
    if (!g_entry_table_read) {
        read_entry_table();
    }
    if (index < 0 || index >= PALETTE_ENTRY_MAX
        || g_entry_file[index][0] == '\0') {
        fprintf(stderr, "palette: no PalettePtrs entry %d\n", index);
        return false;
    }
    char path[32 + PALETTE_FILE_MAX];
    snprintf(path, sizeof path, "assets/palettes/%s", g_entry_file[index]);
    return palette_load(p, line, path);
}
