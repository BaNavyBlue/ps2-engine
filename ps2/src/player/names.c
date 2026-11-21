/* (build 171) play.py's script names (script_name, scripts_text):
 * src/game/script_ids.h's SCRIPT_ names, and for the ids it leaves out the
 * name tools/gen_script_ids.py would make from the script's text (the
 * bank's prefix and the line's first words), numbered on where a name is
 * taken. */

#include "pl.h"

#include "../core/json.h"

#include <ctype.h>
#include <stdlib.h>

#define SCRIPT_IDS 0x1A00           /* banks $00-$19 */

static char *g_name[SCRIPT_IDS];
static bool  g_loaded;

static const char *const BANK_PREFIX[0x1A] = {
    "ITEM", "TECH", "EQUIP", "MEMORY", "CLONE", "HOSPITAL", "WEAPONS",
    "ARMOR", "ITEMS", "HOME", "USTVESTIA", "INVENTOR", "TELEPORT", "TOWER",
    "ROOM", "ROOF", "LIBRARY", "GOVERNOR", "BATTLE", "INTRO", "OPENING",
    "START", "PEOPLE", "ACTION", "EVENT", "MISC"
};

static const char *const STOP[] = {
    "the", "a", "an", "to", "of", "is", "are", "in", "and", "it", "you",
    "i", "for", "that", "this", "on", "at", "be", "have", "has", "was",
    "with", "as", "your", "my", "we", "he", "she", "they", "s"
};

static bool is_stop(const char *w)
{
    char low[64];
    size_t i = 0;
    for (; w[i] != '\0' && i + 1 < sizeof low; i++) {
        low[i] = (char)tolower((unsigned char)w[i]);
    }
    low[i] = '\0';
    for (size_t k = 0; k < sizeof STOP / sizeof STOP[0]; k++) {
        if (streq(STOP[k], low)) {
            return true;
        }
    }
    return false;
}

static bool taken(const char *name)
{
    for (int i = 0; i < SCRIPT_IDS; i++) {
        if (g_name[i] != NULL && streq(g_name[i], name)) {
            return true;
        }
    }
    return false;
}

/* make_name(bank, text): the words less {codes}, \I and apostrophes, the
 * stop words left out unless that leaves under two, the first four. */
static bool make_name(int bank, const char *text, char *out, size_t n)
{
    size_t len = strlen(text);
    char *t = pl_alloc(len + 1);
    size_t k = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '{') {
            const char *close = strchr(text + i, '}');
            if (close != NULL) {
                t[k++] = ' ';
                i = (size_t)(close - text);
                continue;
            }
        }
        if (text[i] == '\\' && text[i + 1] == 'I') {
            t[k++] = ' ';
            i++;
            continue;
        }
        if (text[i] == '\'') {
            continue;
        }
        t[k++] = isalnum((unsigned char)text[i]) && (unsigned char)text[i] < 0x80
               ? text[i] : ' ';
    }
    t[k] = '\0';
    char *ws[256], *keep[256];
    int nw = 0, nk = 0;
    for (char *w = strtok(t, " "); w != NULL && nw < 256; w = strtok(NULL, " ")) {
        ws[nw++] = w;
    }
    for (int i = 0; i < nw; i++) {
        if (!is_stop(ws[i])) {
            keep[nk++] = ws[i];
        }
    }
    if (nk < 2) {
        memcpy(keep, ws, (size_t)nw * sizeof *ws);
        nk = nw;
    }
    if (nk > 4) {
        nk = 4;
    }
    if (nk == 0) {
        free(t);
        return false;
    }
    size_t used = (size_t)snprintf(out, n, "%s", BANK_PREFIX[bank]);
    for (int i = 0; i < nk && used < n; i++) {
        used += (size_t)snprintf(out + used, n - used, "_");
        for (const char *p = keep[i]; *p && used + 1 < n; p++) {
            out[used++] = (char)toupper((unsigned char)*p);
        }
        out[used] = '\0';
    }
    free(t);
    return true;
}

/* load_scripts: each bank file's [N] blocks, as id (bank << 8 | N) and
 * the block's text. */
static char **load_scripts(void)
{
    char **text = pl_alloc(SCRIPT_IDS * sizeof *text);
    char *m = pl_read_file("assets/script/script.json", NULL);
    JsonValue *manifest = (m != NULL) ? json_parse(m, NULL) : NULL;
    free(m);
    for (const JsonValue *e = (manifest != NULL) ? manifest->child : NULL;
         e != NULL; e = e->next) {
        char path[160];
        snprintf(path, sizeof path, "assets/script/%s", json_str(e, "file", ""));
        char *f = pl_read_file(path, NULL);
        if (f == NULL) {
            continue;
        }
        char *nl = strchr(f, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        const char *b = strstr(f, "bank 0x");
        int bank = -1;
        if (b != NULL) {
            bank = (int)strtol(b + 7, NULL, 16);
        }
        if (bank < 0 || bank >= 0x1A || nl == NULL) {
            free(f);
            continue;
        }
        int cur = -1;
        Lines buf = { 0 };
        char *line = nl + 1;
        for (;;) {
            char *end = strchr(line, '\n');
            if (end != NULL) {
                *end = '\0';
            }
            int num = 0;
            size_t ll = strlen(line);
            bool head = ll >= 3 && line[0] == '[' && line[ll - 1] == ']';
            for (size_t i = 1; head && i + 1 < ll; i++) {
                head = isdigit((unsigned char)line[i]) != 0;
                num = num * 10 + (line[i] - '0');
            }
            if (head) {
                if (cur >= 0 && (bank << 8 | cur) < SCRIPT_IDS) {
                    size_t total = 1;
                    for (int i = 0; i < buf.n; i++) {
                        total += strlen(buf.v[i]) + 1;
                    }
                    char *joined = pl_alloc(total);
                    for (int i = 0; i < buf.n; i++) {
                        strcat(joined, buf.v[i]);
                        strcat(joined, "\n");
                    }
                    free(text[bank << 8 | cur]);
                    text[bank << 8 | cur] = joined;
                }
                cur = num;
                lines_clear(&buf);
            } else if (cur >= 0) {
                lines_add(&buf, line);
            }
            if (end == NULL) {
                break;
            }
            line = end + 1;
        }
        if (cur >= 0 && (bank << 8 | cur) < SCRIPT_IDS) {
            size_t total = 1;
            for (int i = 0; i < buf.n; i++) {
                total += strlen(buf.v[i]) + 1;
            }
            char *joined = pl_alloc(total);
            for (int i = 0; i < buf.n; i++) {
                strcat(joined, buf.v[i]);
                strcat(joined, "\n");
            }
            free(text[bank << 8 | cur]);
            text[bank << 8 | cur] = joined;
        }
        lines_free(&buf);
        free(f);
    }
    json_free(manifest);
    return text;
}

static void names_load(void)
{
    if (g_loaded) {
        return;
    }
    g_loaded = true;
    char *h = pl_read_file("src/game/script_ids.h", NULL);
    for (const char *p = (h != NULL) ? strstr(h, "#define SCRIPT_") : NULL;
         p != NULL; p = strstr(p + 1, "#define SCRIPT_")) {
        const char *w = p + strlen("#define SCRIPT_");
        const char *e = w;
        while (isalnum((unsigned char)*e) || *e == '_') {
            e++;
        }
        const char *q = e;
        if (e == w || !isspace((unsigned char)*q)) {
            continue;
        }
        while (isspace((unsigned char)*q)) {
            q++;
        }
        if (q[0] != '0' || q[1] != 'x') {
            continue;
        }
        int v = 0;
        bool ok = true;
        for (int i = 0; i < 4; i++) {
            char c = q[2 + i];
            if (c >= '0' && c <= '9') {
                v = v * 16 + (c - '0');
            } else if (c >= 'A' && c <= 'F') {
                v = v * 16 + (c - 'A' + 10);
            } else {
                ok = false;
            }
        }
        if (!ok || v >= SCRIPT_IDS) {
            continue;
        }
        char *name = pl_alloc((size_t)(e - w) + 1);
        memcpy(name, w, (size_t)(e - w));
        free(g_name[v]);
        g_name[v] = name;
    }
    free(h);
    bool from_header[SCRIPT_IDS];
    for (int v = 0; v < SCRIPT_IDS; v++) {
        from_header[v] = g_name[v] != NULL;
    }
    char **text = load_scripts();
    for (int v = 0; v < SCRIPT_IDS; v++) {
        if (text[v] == NULL || from_header[v]) {
            continue;
        }
        char base[128], n[140];
        if (!make_name(v >> 8, text[v], base, sizeof base)) {
            snprintf(base, sizeof base, "%s_%02X", BANK_PREFIX[v >> 8], v & 0xFF);
        }
        snprintf(n, sizeof n, "%s", base);
        for (int k = 2; taken(n); k++) {
            snprintf(n, sizeof n, "%s_%d", base, k);
        }
        g_name[v] = pl_strdup(n);
    }
    for (int v = 0; v < SCRIPT_IDS; v++) {
        free(text[v]);
    }
    free(text);
}

const char *script_name(int id, char *buf, size_t n)
{
    names_load();
    if (id >= 0 && id < SCRIPT_IDS && g_name[id] != NULL) {
        return g_name[id];
    }
    snprintf(buf, n, "$%04X", (unsigned)id);
    return buf;
}

const char *scripts_text(const Ints *ids, char *out, size_t n)
{
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; ids != NULL && i < ids->n && used < n; i++) {
        char buf[8];
        used += (size_t)snprintf(out + used, n - used, "%s%s", i ? " " : "",
                                 script_name(ids->v[i], buf, sizeof buf));
    }
    return out;
}
