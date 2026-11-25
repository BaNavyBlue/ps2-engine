/* (build 190) tools/extract_tables.py in C: the small tables that drive
 * screen effects, each the bytes from its label to the next label.
 *
 * The two colour ramps (EX_RAMPS: the Sega logo's sweep and the credits'
 * fade) are pictures in palettes/ beside the out folder, ps2png.write_ramp's
 * (ex_png_ramp). The dissolve order (FadeEffectArray) is
 * dissolve_order.json, its 64 positions as [x, y]. The ending's credits
 * (EndingCredits_Script, to their end code: the zero after it is `even`'s
 * pad) are text in script/ beside the out folder, a line of the file a line
 * of the credits, checked to give the ROM's bytes back before it is
 * written. The .bin files these were until each was readable are
 * deleted. */

#include "asset_names.h"
#include "exart.h"

#include <stdio.h>
#include <string.h>

void ex_step_tables(Extract *x, const ExArgs *a);

/* (build 136) The .bin files the ramps were; then the rest of the
 * containers this wrote, each as it became readable (136a, 136d, 136e). */
static const char *const STALE[] = {
    "CyclingPal_SegaLogo.bin",
    "EndingCreditsFadeRamp.bin",
    "FadeEffectArray.bin",
    "FMInstruments.bin",
    "FMRegisterOrder.bin",
    "EndingCredits_Script.bin",
    NULL
};

/* Path(dir).parent, as pathlib reads a path: empty and "." parts left
 * out, a leading separator kept; "." when there is no folder above. */
static char *parent_dir(Extract *x, const char *dir)
{
    bool root = dir[0] == '/' || dir[0] == '\\';
    ExStr out;
    ex_str_init(x, &out);
    if (root) {
        ex_str_addc(&out, '/');
    }
    size_t last = 0;        /* where the last part's separator went */
    int parts = 0;
    const char *p = dir;
    while (*p != '\0') {
        while (*p == '/' || *p == '\\') {
            p++;
        }
        const char *start = p;
        while (*p != '\0' && *p != '/' && *p != '\\') {
            p++;
        }
        size_t n = (size_t)(p - start);
        if (n == 0 || (n == 1 && start[0] == '.')) {
            continue;
        }
        last = out.len;
        if (parts > 0) {
            ex_str_addc(&out, '/');
        }
        ex_str_addn(&out, start, n);
        parts++;
    }
    if (parts <= 1) {
        return ex_strdup(x, root ? "/" : ".");
    }
    out.buf[last] = '\0';
    return out.buf;
}

/* Path.exists() for a file. */
static bool file_there(const char *path)
{
    FILE *fh = fopen(path, "rb");
    if (fh == NULL) {
        return false;
    }
    fclose(fh);
    return true;
}

/* ---- the colour ramps ---- */

/* (build 136) Each ramp as a picture, EX_RAMPS's rows wide; a CRAM word
 * each two bytes of its label's. */
static void extract_ramps(Extract *x, const char *paldir)
{
    ex_mkdirs(x, paldir);
    for (int r = 0; EX_RAMPS[r].label != NULL; r++) {
        const ExRamp *ramp = &EX_RAMPS[r];
        size_t len = 0;
        const u8 *data = ex_blob(x, ramp->label, &len);
        if (len % 2 != 0) {
            ex_fail(x, "%s: %zu bytes, not whole words", ramp->label, len);
        }
        int n = (int)(len / 2);
        u16 *words = ex_alloc(x, sizeof *words * (len / 2 + 1));
        for (int i = 0; i < n; i++) {
            words[i] = (u16)(data[2 * i] << 8 | data[2 * i + 1]);
        }
        char *out = ex_path(x, paldir, ex_fmt(x, "%s.png", ramp->name));
        ex_png_ramp(x, out, words, n, ramp->across);
        ex_say(x, "%s: %d colours, %d row(s) of %d -> %s", ramp->label, n,
               n / ramp->across, ramp->across, out);
    }
}

/* ---- the dissolve order ---- */

#define DISSOLVE_STEPS  64
#define TILE_PIXELS     64      /* a position is y * 8 + x in the tile */

static const char DISSOLVE_ABOUT[] =
    "The dissolve order (FadeEffectArray): step i of a dissolve in reveals "
    "pixel [x, y] of every tile in its range at once (FadeArtFill, from "
    "FadeArtTiles). The dissolve out (loc_6C54) steps the first 32 and "
    "clears the two-pixel byte each falls in: x 0-1, 2-3, 4-5 or 6-7. Each "
    "position is the ROM's byte y * 8 + x, its place in the tile.";

/* (build 136a) The 64 bytes as [x, y] pixel positions, eight to a line. */
static void write_dissolve(Extract *x, const char *path, const u8 *data,
                           size_t len)
{
    bool bad = len != DISSOLVE_STEPS;
    for (size_t i = 0; !bad && i < len; i++) {
        if (data[i] >= TILE_PIXELS) {
            bad = true;
        }
    }
    if (bad) {
        ex_fail(x, "FadeEffectArray: %zu bytes, not %d positions 0-63", len,
                DISSOLVE_STEPS);
    }
    ExStr t;
    ex_str_init(x, &t);
    ex_str_add(&t, "{\n  \"source\": \"FadeEffectArray\",\n  \"about\": ");
    ex_str_add(&t, jo_quote(x, DISSOLVE_ABOUT));
    ex_str_add(&t, ",\n  \"order\": [\n    ");
    for (size_t i = 0; i < len; i++) {
        if (i > 0) {
            ex_str_add(&t, (i % 8 == 0) ? ",\n    " : ", ");
        }
        ex_str_addf(&t, "[%d, %d]", data[i] & 7, data[i] >> 3);
    }
    ex_str_add(&t, "\n  ]\n}\n");
    ex_write_text(x, path, t.buf);
    ex_say(x, "FadeEffectArray: %zu positions -> %s", len, path);
}

/* ---- the ending's credits ---- */

/* (build 136e) The credits writer's codes (loc_787E): core/credits.h's. */
#define CREDITS_SPACE            0x20
#define CREDITS_BLANK_RUN_BELOW  0x14
#define CREDITS_NEXT_LINE        0xC1
#define CREDITS_CLOSE            0xC3
#define CREDITS_SECTION          0xC4
#define CREDITS_END              0xC5
#define CREDITS_LINE_CELLS       39   /* the writer counts 40 a row; a 40th
                                         runs it out */
#define CREDITS_MAX              4096 /* credits.h: the bytes credits_load
                                         holds */

/* The codes that end a line, and their tags in the text. */
typedef struct {
    u8          code;
    const char *tag;
} LineEnd;

static const LineEnd LINE_ENDS[] = {
    { CREDITS_NEXT_LINE, "" },
    { CREDITS_CLOSE,     "{close}" },
    { CREDITS_SECTION,   "{section}" },
};

#define LINE_END_COUNT ((int)(sizeof LINE_ENDS / sizeof LINE_ENDS[0]))

static const char CREDITS_HEADER[] =
    "# The ending's credits (EndingCredits_Script), as the credits writer\n"
    "# (loc_787E) scrolls them up over the stars under \"Never Dream\".\n"
    "#\n"
    "# Each line below is a line of the credits as it lands on the screen, 40\n"
    "# cells across: its leading spaces are its indent and every other space is\n"
    "# one blank cell, so the text sits here where it shows there. A line holds\n"
    "# letters and spaces only, 39 cells at most (the writer counts 40 a row,\n"
    "# and a 40th runs its count out).\n"
    "#\n"
    "# Capitals are drawn in white (palette line 0) and lower case in the\n"
    "# titles' blue (line 1). The credits' alphabet has capitals only, so both\n"
    "# show as capitals: here the case is the colour.\n"
    "#\n"
    "# Two blank rows follow a line. A line ending {close} has one after it, and\n"
    "# a line ending {section} has 28, the gap before the next section. {empty}\n"
    "# on its own is a line with nothing on it, three rows of nothing; {end} on\n"
    "# its own stops the credits there, the screen held until A, B or C. Lines\n"
    "# starting # and empty lines are skipped: the empty lines here are only for\n"
    "# the eye.\n";

static bool is_letter(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/* The tag a code ends a line with, or NULL. */
static const char *line_end_tag(u8 code)
{
    for (int i = 0; i < LINE_END_COUNT; i++) {
        if (LINE_ENDS[i].code == code) {
            return LINE_ENDS[i].tag;
        }
    }
    return NULL;
}

/* "\n".join's next line: `text` (n bytes) then `tag`. */
static void add_line(ExStr *lines, int *count, const char *text, size_t n,
                     const char *tag)
{
    if (*count > 0) {
        ex_str_addc(lines, '\n');
    }
    if (n > 0) {
        ex_str_addn(lines, text, n);
    }
    ex_str_add(lines, tag);
    (*count)++;
}

/* (build 136e) The writer's bytes as lines of text: blank runs (a byte n
 * under $14, n + 1 cells) and $20 as spaces, letters as themselves, a
 * line's end code as its tag. Anything the text cannot say is refused. */
static char *credits_text(Extract *x, const u8 *data, size_t len)
{
    ExStr lines;
    ExStr cur;
    ex_str_init(x, &lines);
    ex_str_init(x, &cur);
    int count = 0;
    bool ended = false;
    for (size_t k = 0; k < len; k++) {
        u8 b = data[k];
        const char *tag = line_end_tag(b);
        if (ended) {
            ex_fail(x, "EndingCredits_Script: byte %zu after the end code", k);
        }
        if (b < CREDITS_BLANK_RUN_BELOW) {
            for (int i = 0; i <= b; i++) {
                ex_str_addc(&cur, ' ');
            }
        } else if (b == CREDITS_SPACE) {
            ex_str_addc(&cur, ' ');
        } else if (is_letter(b)) {
            ex_str_addc(&cur, (char)b);
        } else if (b == CREDITS_NEXT_LINE && cur.len == 0) {
            add_line(&lines, &count, "", 0, "{empty}");
        } else if (tag != NULL) {
            add_line(&lines, &count, cur.buf, cur.len, tag);
            if (strcmp(tag, "{section}") == 0) {
                add_line(&lines, &count, "", 0, "");  /* for the eye */
            }
            cur.len = 0;
        } else if (b == CREDITS_END && cur.len == 0) {
            add_line(&lines, &count, "", 0, "{end}");
            ended = true;
        } else {
            ex_fail(x, "EndingCredits_Script: byte %zu is $%02X, which the "
                       "text does not say", k, b);
        }
    }
    if (!ended) {
        ex_fail(x, "EndingCredits_Script: no end code");
    }
    return ex_fmt(x, "%s\n%s\n", CREDITS_HEADER, lines.buf);
}

/* The line [s, e) is `word`. */
static bool line_is(const char *s, const char *e, const char *word)
{
    size_t n = strlen(word);
    return (size_t)(e - s) == n && memcmp(s, word, n) == 0;
}

/* (build 136e) The text back to the writer's bytes, as credits_load reads
 * it: leading spaces as blank runs of 20 cells at most, other spaces $20,
 * letters their ASCII bytes, then the line's end code. */
static u8 *credits_bytes(Extract *x, const char *text, size_t *len)
{
    ExStr out;
    ex_str_init(x, &out);
    bool ended = false;
    int n = 0;
    const char *s = text;
    for (;;) {
        const char *e = strchr(s, '\n');
        if (e == NULL) {
            e = s + strlen(s);
        }
        n++;
        if (s == e || s[0] == '#') {
            /* skipped */
        } else if (ended) {
            ex_fail(x, "line %d is after {end}", n);
        } else if (line_is(s, e, "{end}") || line_is(s, e, "{empty}")) {
            ended = line_is(s, e, "{end}");
            ex_str_addc(&out, (char)(ended ? CREDITS_END : CREDITS_NEXT_LINE));
        } else {
            const char *brace = memchr(s, '{', (size_t)(e - s));
            const char *body_end = (brace != NULL) ? brace : e;
            int code = -1;
            for (int i = 0; i < LINE_END_COUNT; i++) {
                if (line_is(body_end, e, LINE_ENDS[i].tag)) {
                    code = LINE_ENDS[i].code;
                }
            }
            if (code < 0) {
                ex_fail(x, "line %d: %.*s out of place", n,
                        (int)(e - body_end), body_end);
            }
            int width = (int)(body_end - s);
            if (width > CREDITS_LINE_CELLS) {
                ex_fail(x, "line %d is %d cells wide", n, width);
            }
            for (const char *c = s; c < body_end; c++) {
                if (*c != ' ' && !is_letter((unsigned char)*c)) {
                    ex_fail(x, "line %d: not letters and spaces", n);
                }
            }
            int indent = 0;
            while (indent < width && s[indent] == ' ') {
                indent++;
            }
            int left = indent;
            while (left > 0) {
                int run = (left < CREDITS_BLANK_RUN_BELOW)
                              ? left : CREDITS_BLANK_RUN_BELOW;
                ex_str_addc(&out, (char)(run - 1));
                left -= run;
            }
            ex_str_addn(&out, s + indent, (size_t)(width - indent));
            ex_str_addc(&out, (char)code);
        }
        if (*e == '\0') {
            break;
        }
        s = e + 1;
    }
    if (!ended) {
        ex_fail(x, "no {end}");
    }
    if (out.len > CREDITS_MAX) {
        ex_fail(x, "%zu bytes, more than %d", out.len, CREDITS_MAX);
    }
    *len = out.len;
    return (u8 *)out.buf;
}

/* (build 185) The writer's bytes in the ROM: from the label to the end
 * code, where the writer stops. Its length is odd, so `even` puts a zero
 * after it before the next label; anything more there would be bytes the
 * text does not hold, and is refused. */
static const u8 *credits_bytes_in_rom(Extract *x, const char *label,
                                      size_t *len)
{
    size_t size = 0;
    const u8 *block = ex_blob(x, label, &size);
    const u8 *end = (size > 0) ? memchr(block, CREDITS_END, size) : NULL;
    if (end == NULL) {
        ex_fail(x, "%s: no end code before the next label", label);
    }
    size_t used = (size_t)(end - block) + 1;
    size_t rest = size - used;
    if (rest > 1 || (rest == 1 && block[used] != 0)) {
        ex_fail(x, "%s: %zu bytes after the end code that are not `even`'s "
                   "pad", label, rest);
    }
    *len = used;
    return block;
}

/* (build 136e) The credits as text, checked to give the ROM's bytes back
 * before it is written. */
static void write_credits(Extract *x, const char *path, const u8 *data,
                          size_t len)
{
    char *text = credits_text(x, data, len);
    size_t back_len = 0;
    u8 *back = credits_bytes(x, text, &back_len);
    if (back_len != len || memcmp(back, data, len) != 0) {
        ex_fail(x, "EndingCredits_Script: the text does not give the ROM's "
                   "bytes back");
    }
    ex_write_text(x, path, text);
    int lines = 0;
    for (const char *s = text; *s != '\0';) {
        const char *e = strchr(s, '\n');
        if (e == NULL) {
            e = s + strlen(s);
        }
        if (e != s && s[0] != '#') {
            lines++;
        }
        s = (*e == '\0') ? e : e + 1;
    }
    ex_say(x, "EndingCredits_Script: %zu bytes, %d lines -> %s", len, lines,
           path);
}

/* ---- the step ---- */

void ex_step_tables(Extract *x, const ExArgs *a)
{
    ex_mkdirs(x, a->out);
    char *above = parent_dir(x, a->out);
    extract_ramps(x, ex_path(x, above, "palettes"));

    size_t len = 0;
    const u8 *data = ex_blob(x, "FadeEffectArray", &len);
    write_dissolve(x, ex_path(x, a->out,
                              ex_pair(EX_TABLE_FILES, "FadeEffectArray")),
                   data, len);

    char *script_dir = ex_path(x, above, "script");
    ex_mkdirs(x, script_dir);
    const char *credits = ex_pair(EX_SCRIPT_FILES, "EndingCredits_Script");
    data = credits_bytes_in_rom(x, "EndingCredits_Script", &len);
    write_credits(x, ex_path(x, script_dir, credits), data, len);

    for (int i = 0; STALE[i] != NULL; i++) {
        char *old = ex_path(x, a->out, STALE[i]);
        if (file_there(old)) {
            ex_remove(x, old);
            ex_say(x, "deleted %s (in a readable form now)", old);
        }
    }
}
