/* (build 189) tools/disasm_art.py's DisasmArt in C, with the parts of
 * extract_art.py and extract_inline_art.py it reads (exsheets.h). The
 * label list and names are extract_inline_art.py's, written in from it
 * by a script when this was made. */

#include "exsheets.h"
#include "exart.h"
#include "excommon.h"

#include <stdlib.h>
#include <string.h>

const char *const EX_INLINE_LABELS[] = {
    "FontsIconsArt", "SegaLogoArt", "loc_6D7BA", "loc_6F040", "loc_6FF22",
    "loc_71362", "loc_71702", "loc_2B1F0", "loc_7567A", "loc_AED00",
    "Battle_ArchDrgnArt", "Battle_ArmyEyeArt", "Battle_CatmanArt",
    "Battle_Cooley61Art", "Battle_DarkFrceArt", "Battle_DarksideArt",
    "Battle_DezoOwlArt", "Battle_EyesoreArt", "Battle_FireEyeArt",
    "Battle_FirefallArt", "Battle_LungArt", "Battle_MastodonArt",
    "Battle_MazgammaArt", "Battle_MechomanArt", "Battle_MetalmanArt",
    "Battle_MombrainArt", "Battle_MystcapeArt", "Battle_NeifirstArt",
    "Battle_OrangahArt", "Battle_PugHitArt", "Battle_RabbitArt",
    "Battle_TerakiteArt", "Battle_VanArt", "Battle_WirefaceArt",
    "Battle_WizardArt", "Battle_WolfangArt", "Battle_WresterArt",
    "AcidShotArt", "BowgunArt", "DebanAttackArt", "EijiaAttackArt",
    "FireStaffArt", "FoiArt", "GajAttackArt", "GajCastArt", "GenArt",
    "GraAttackArt", "GraCastArt", "LacoSwordArt", "LaserBarArt",
    "LaserShotArt", "LaserSwordArt", "MegidCastArt", "NeiShotAttackArt",
    "NeiShotFiredArt", "PulseCannonArt", "SakCastArt", "ShiftAttackArt",
    "ShiftCastArt", "SlasherAttackArt", "SlasherThrownArt", "SteelBarArt",
    "SwordArt", "TsuCastArt", "ZanAttackArt", "ZanCastArt", "LutzPortraitArt",
    "loc_AF82A", "loc_B6E30", "loc_B6C2C",
    NULL,
};

/* OUTPUT_NAMES: the unnamed labels' sheet names */
static const ExNamePair OUTPUT_NAMES[] = {
    { "loc_6D7BA", "DreamBattleArt" },
    { "loc_6FF22", "DreamWakingArt" },
    { "loc_6F040", "DreamHelpArt" },
    { "loc_71362", "SpaceshipPlanetArt" },
    { "loc_71702", "SpaceshipArt" },
    { "loc_2B1F0", "CharWalkArt" },
    { "loc_7567A", "battle_floor_art" },
    { "loc_AED00", "NeiDeathArt" },
    { "loc_AF82A", "EndingPicturesArt" },
    { "loc_B6E30", "EndingStarfieldUfoArt" },
    { "loc_B6C2C", "EndingCreditsLettersArt" },
    { NULL, NULL },
};

const char *ex_inline_name(Extract *x, const char *label)
{
    const char *named = ex_pair(OUTPUT_NAMES, label);
    if (named != NULL) {
        return named;
    }
    size_t n = strlen(label);
    if (ex_starts(label, "Battle_") && ex_ends(label, "Art") && n >= 10) {
        return ex_fmt(x, "battle_%s_art",
                      ex_lower(x, ex_strndup(x, label + 7, n - 10)));
    }
    return label;
}

static int cmp_bin(const void *a, const void *b)
{
    const ExArtBin *p = a;
    const ExArtBin *q = b;
    int c = strcmp(p->stem, q->stem);
    return (c != 0) ? c : strcmp(p->file, q->file);
}

ExArtBin *ex_art_bins(Extract *x, int *count)
{
    const ExMap *m = &x->map;
    ExArtBin *out = ex_alloc(x, sizeof *out * (size_t)(m->nfiles + 1));
    int n = 0;
    for (int i = 0; i < m->nfiles; i++) {
        const char *key = m->files[i].name;
        const char *slash = strrchr(key, '/');
        if (slash == NULL || (size_t)(slash - key) != 3
            || strncmp(key, "art", 3) != 0 || !ex_ends(slash + 1, ".bin")) {
            continue;
        }
        out[n].stem = ex_strndup(x, slash + 1, strlen(slash + 1) - 4);
        out[n].file = key;
        n++;
    }
    if (n > 1) {
        qsort(out, (size_t)n, sizeof *out, cmp_bin);
    }
    *count = n;
    return out;
}

int ex_guess_width(const char *stem, int tiles)
{
    if ((ex_ends(stem, "_portrait") || ex_ends(stem, "_port"))
        && tiles % 8 == 0) {
        return 8;
    }
    if (tiles == 0) {
        return 1;
    }
    return (tiles < 16) ? tiles : 16;
}

typedef struct {
    const char *name;
    bool        bin;
    const char *key;        /* the stem, or the label */
    ExSheet    *sheet;      /* made when first asked for */
} Source;

struct ExSheets {
    Extract *x;
    Source  *at;
    int      count;
};

ExSheets *ex_sheets(Extract *x)
{
    int nbins = 0;
    ExArtBin *bins = ex_art_bins(x, &nbins);
    int ninline = 0;
    while (EX_INLINE_LABELS[ninline] != NULL) {
        ninline++;
    }
    ExSheets *s = ex_alloc(x, sizeof *s);
    s->x  = x;
    s->at = ex_alloc(x, sizeof *s->at * (size_t)(nbins + ninline + 1));
    for (int i = 0; i < nbins + ninline; i++) {
        Source src;
        memset(&src, 0, sizeof src);
        if (i < nbins) {
            if (ex_listed(EX_ART_NOT_ART, bins[i].stem)) {
                continue;
            }
            src.name = ex_art_name(bins[i].stem);
            src.bin  = true;
            src.key  = bins[i].stem;
        } else {
            const char *label = EX_INLINE_LABELS[i - nbins];
            src.name = ex_inline_name(x, label);
            src.bin  = false;
            src.key  = label;
        }
        for (int k = 0; k < s->count; k++) {
            if (strcmp(s->at[k].name, src.name) == 0) {
                ex_fail(x, "%s: both %s and %s", src.name, s->at[k].key,
                        src.key);
            }
        }
        s->at[s->count++] = src;
    }
    return s;
}

const ExSheet *ex_sheet(ExSheets *s, const char *name)
{
    Extract *x = s->x;
    Source *src = NULL;
    for (int k = 0; k < s->count && src == NULL; k++) {
        if (strcmp(s->at[k].name, name) == 0) {
            src = &s->at[k];
        }
    }
    if (src == NULL) {
        ex_fail(x, "no art sheet %s", name);
    }
    if (src->sheet != NULL) {
        return src->sheet;
    }
    ExTiles tiles;
    int across = 0;
    if (src->bin) {
        size_t size = 0;
        const u8 *raw = ex_file(x, ex_fmt(x, "art/%s.bin", src->key), &size);
        tiles  = ex_art(x, raw, size);
        across = ex_guess_width(src->key, tiles.count);
    } else {
        size_t size = 0;
        const u8 *raw = ex_blob(x, src->key, &size);
        tiles  = ex_art(x, raw, size);
        across = 16;
    }
    int cap = (tiles.count > 0) ? tiles.count : 1;
    across = (across > cap) ? cap : across;
    across = (across < 1) ? 1 : across;
    ExSheet *sh = ex_alloc(x, sizeof *sh);
    sh->across = across;
    sh->rows   = (tiles.count + across - 1) / across;
    sh->count  = across * sh->rows;
    sh->px     = ex_alloc(x, (size_t)64 * (size_t)(sh->count + 1));
    for (int t = 0; t < tiles.count; t++) {
        for (int i = 0; i < 32; i++) {
            u8 b = tiles.data[(size_t)t * 32 + (size_t)i];
            sh->px[(size_t)t * 64 + (size_t)i * 2]     = b >> 4;
            sh->px[(size_t)t * 64 + (size_t)i * 2 + 1] = b & 0x0F;
        }
    }
    src->sheet = sh;
    return sh;
}
