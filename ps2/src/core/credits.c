#include "credits.h"

#include "assetfs.h"

#include <stdio.h>
#include <string.h>

/* (build 136e) The credits as text to the credits writer's bytes;
 * credits.h has the format. */

#define CREDITS_TEXT_LINE_MAX   256     /* a line past it is far too wide */
#define CREDITS_RUN_CELLS_MAX   CREDITS_BLANK_RUN_BELOW  /* byte $13: 20 */

typedef struct {
    CreditsScript *c;
    const char    *path;
    int            line;
    bool           ok;
} Reader;

static void put(Reader *r, int byte)
{
    if (!r->ok) {
        return;
    }
    if (r->c->count >= CREDITS_MAX) {
        fprintf(stderr, "credits: %s: line %d: more than %d bytes\n",
                r->path, r->line, CREDITS_MAX);
        r->ok = false;
        return;
    }
    r->c->byte[r->c->count++] = (u8)byte;
}

/* The code a tag ends a line with; -1 for any other text. */
static int line_end_code(const char *tag)
{
    if (strcmp(tag, "{close}") == 0) {
        return CREDITS_CLOSE_LINE;
    }
    if (strcmp(tag, "{section}") == 0) {
        return CREDITS_END_SECTION;
    }
    return -1;
}

static bool is_letter(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

/* One line of the credits: its indent as blank runs, then its letters
 * and spaces, then the code its tag (or none) ends it with. */
static void read_line(Reader *r, const char *line)
{
    const char *tag    = strchr(line, '{');
    int         length = (tag != NULL) ? (int)(tag - line) : (int)strlen(line);
    int         ending = CREDITS_NEXT_LINE;

    if (tag != NULL) {
        ending = line_end_code(tag);
        if (ending < 0) {
            fprintf(stderr, "credits: %s: line %d: \"%s\" is not {close} or "
                            "{section} at a line's end ({empty} and {end} "
                            "stand alone)\n", r->path, r->line, tag);
            r->ok = false;
            return;
        }
    }
    if (length > CREDITS_LINE_CELLS) {
        fprintf(stderr, "credits: %s: line %d is %d cells wide, more than "
                        "%d (the credits writer counts 40 a row; a 40th "
                        "runs its count out)\n", r->path, r->line, length,
                CREDITS_LINE_CELLS);
        r->ok = false;
        return;
    }
    int indent = 0;
    while (indent < length && line[indent] == ' ') {
        indent++;
    }
    for (int i = indent; i < length; i++) {
        unsigned char ch = (unsigned char)line[i];
        if (ch != ' ' && !is_letter((char)ch)) {
            if (ch >= 0x21 && ch <= 0x7E) {
                fprintf(stderr, "credits: %s: line %d, column %d: '%c' is "
                                "not a letter or a space (the credits' "
                                "alphabet is A-Z)\n", r->path, r->line,
                        i + 1, ch);
            } else {
                fprintf(stderr, "credits: %s: line %d, column %d: byte $%02X "
                                "is not a letter or a space (the credits' "
                                "alphabet is A-Z)\n", r->path, r->line,
                        i + 1, ch);
            }
            r->ok = false;
            return;
        }
    }

    for (int left = indent; left > 0; ) {
        int run = (left > CREDITS_RUN_CELLS_MAX) ? CREDITS_RUN_CELLS_MAX : left;
        put(r, run - 1);
        left -= run;
    }
    for (int i = indent; i < length; i++) {
        /* A space is CREDITS_SPACE itself; a letter its ASCII byte, lower
         * case with CREDITS_LOWERCASE set. */
        put(r, (unsigned char)line[i]);
    }
    put(r, ending);
}

static bool credits_read(CreditsScript *c, const char *path)
{
    memset(c, 0, sizeof *c);

    const char *cursor = (const char *)assetfs_get(path, NULL);
    if (cursor == NULL) {
        fprintf(stderr, "credits: cannot open %s\n", path);
        return false;
    }

    Reader r = { c, path, 0, true };
    bool   ended = false;
    char   line[CREDITS_TEXT_LINE_MAX];

    while (r.ok && assetfs_next_line(&cursor, line, sizeof line)) {
        r.line++;
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        if (ended) {
            fprintf(stderr, "credits: %s: line %d is after {end}, where the "
                            "credits writer never reads\n", path, r.line);
            r.ok = false;
        } else if (strcmp(line, "{end}") == 0) {
            put(&r, CREDITS_END);
            ended = true;
        } else if (strcmp(line, "{empty}") == 0) {
            put(&r, CREDITS_NEXT_LINE);
        } else {
            read_line(&r, line);
        }
    }
    if (r.ok && !ended) {
        fprintf(stderr, "credits: %s has no {end}: the credits would not "
                        "stop\n", path);
        r.ok = false;
    }
    if (!r.ok) {
        c->count = 0;
    }
    return r.ok;
}

bool credits_load(CreditsScript *c, const char *path)
{
    /* (build 157d) A mod it refuses is set aside and the game's own
     * read in its place (docs/mods.md, "When a mod is refused"). */
    return credits_read(c, path)
        || (assetfs_mod_refused(path) && credits_read(c, path));
}
