#include "script.h"

#include "assetfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool script_read(ScriptBlock *sb, const char *path)
{
    memset(sb, 0, sizeof *sb);

    const char *cursor = (const char *)assetfs_get(path, NULL);
    if (cursor == NULL) {
        fprintf(stderr, "script: cannot open %s\n", path);
        return false;
    }

    sb->entry = calloc(SCRIPT_MAX_ENTRIES, sizeof *sb->entry);
    if (sb->entry == NULL) {
        return false;
    }

    /* Entries open with a `[n]` line and run to the next one. Lines
     * starting with # are comments and blank lines between entries are
     * separators, but a blank line inside an entry is part of it. */
    char  line[512];
    char *current = NULL;
    int   index = -1;

    while (assetfs_next_line(&cursor, line, sizeof line)) {
        size_t len = strlen(line);

        if (line[0] == '#') {
            continue;
        }

        int n = 0;
        if (sscanf(line, "[%d]", &n) == 1 && n >= 1
            && n <= SCRIPT_MAX_ENTRIES) {
            index = n - 1;
            current = calloc(1, 1);
            sb->entry[index] = current;
            if (n > sb->count) {
                sb->count = n;
            }
            continue;
        }

        if (current == NULL || (len == 0 && current[0] == '\0')) {
            continue;
        }

        size_t have = strlen(current);
        char  *grown = realloc(current, have + len + 2);
        if (grown == NULL) {
            break;
        }
        if (have > 0) {
            grown[have++] = '\n';
        }
        memcpy(grown + have, line, len + 1);
        current = grown;
        sb->entry[index] = grown;
    }

    return sb->count > 0;
}

bool script_load(ScriptBlock *sb, const char *path)
{
    /* (build 157d) A mod it refuses is set aside and the game's own
     * read in its place (docs/mods.md, "When a mod is refused"). */
    if (script_read(sb, path)) {
        return true;
    }
    script_free(sb);
    return assetfs_mod_refused(path) && script_read(sb, path);
}

void script_free(ScriptBlock *sb)
{
    if (sb->entry != NULL) {
        for (int i = 0; i < SCRIPT_MAX_ENTRIES; i++) {
            free(sb->entry[i]);
        }
        free(sb->entry);
    }
    memset(sb, 0, sizeof *sb);
}

const char *script_entry(const ScriptBlock *sb, int n)
{
    if (n < 1 || n > sb->count) {
        return NULL;
    }
    return sb->entry[n - 1];
}
