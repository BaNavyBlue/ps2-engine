/* (build 187) tools/extract_collision.py and extract_interactions.py in C:
 * each level's collision table as text (a line a block: its solid and
 * special quadrants), and its interaction list (doors, entrances) and the
 * town-edge exits as JSON. Both read the levels.json extract_levels wrote
 * for the tables' labels and the levels' names. */

#include "excommon.h"

#include <string.h>

void ex_step_collision(Extract *x, const ExArgs *a);
void ex_step_interactions(Extract *x, const ExArgs *a);

static const JsonValue *levels_of(Extract *x, const ExArgs *a)
{
    const JsonValue *manifest = ex_read_json(x, ex_path(x, a->out,
                                                        "levels.json"));
    const JsonValue *levels = json_get(manifest, "levels");
    if (levels == NULL || levels->type != JSON_ARRAY) {
        ex_fail(x, "levels.json has no \"levels\"");
    }
    return levels;
}

static long long int_of(Extract *x, const JsonValue *lv, const char *key)
{
    const JsonValue *v = json_get(lv, key);
    if (v == NULL || v->type != JSON_NUMBER) {
        ex_fail(x, "levels.json: a level with no \"%s\"", key);
    }
    return (long long)v->number;
}

static const char *str_of(Extract *x, const JsonValue *lv, const char *key)
{
    const JsonValue *v = json_get(lv, key);
    if (v == NULL || v->type != JSON_STRING) {
        ex_fail(x, "levels.json: a level with no \"%s\"", key);
    }
    return v->string;
}

/* f"{value}" of a level's field: an integer, or a string as it is. */
static char *shown(Extract *x, const JsonValue *lv, const char *key)
{
    const JsonValue *v = json_get(lv, key);
    if (v != NULL && v->type == JSON_STRING) {
        return ex_strdup(x, v->string);
    }
    return ex_fmt(x, "%lld", int_of(x, lv, key));
}

static char *stem_of(Extract *x, const JsonValue *lv)
{
    return ex_fmt(x, "%02llx_%s", int_of(x, lv, "index"),
                  str_of(x, lv, "name"));
}

/* ---- collision ---- */

static void nibble(ExStr *s, unsigned bits)
{
    for (int q = 0; q < 4; q++) {
        ex_str_addc(s, (bits & (1u << q)) ? '#' : '.');
    }
}

static void write_table(Extract *x, const char *path, const char *title,
                        const u8 *table, size_t n)
{
    ExStr s;
    ex_str_init(x, &s);
    ex_str_add(&s, title);
    ex_str_add(&s, "\n# block  solid  special        quadrants read TL TR "
                   "BL BR");
    for (size_t block = 0; block < n; block++) {
        ex_str_addf(&s, "\n%02zx       ", block);
        nibble(&s, table[block]);
        ex_str_add(&s, "   ");
        nibble(&s, (unsigned)table[block] >> 4);
    }
    ex_str_addc(&s, '\n');
    ex_write_text(x, path, s.buf);
}

void ex_step_collision(Extract *x, const ExArgs *a)
{
    const JsonValue *levels = levels_of(x, a);
    int count = 0;
    for (const JsonValue *lv = levels->child; lv != NULL; lv = lv->next) {
        const JsonValue *label = json_get(lv, "collision");
        /* `if not label or not rom.has(label)` */
        if (label == NULL || label->type != JSON_STRING
            || label->string[0] == '\0' || !ex_has(x, label->string)) {
            continue;
        }
        size_t n = 0;
        const u8 *table = ex_blob(x, label->string, &n);
        const char *name = str_of(x, lv, "name");
        write_table(x, ex_path(x, a->out, ex_fmt(x, "%s.collision.txt",
                                                 stem_of(x, lv))),
                    ex_fmt(x, "# %s: %s, one line per block of blocks%s",
                           name, label->string, shown(x, lv, "tileset")),
                    table, n);
        count++;
    }
    /* The one table no level names: the jet scooter's. */
    size_t n = 0;
    const u8 *table = ex_blob(x, "loc_29E16", &n);
    write_table(x, ex_path(x, a->out, "jet_scooter.collision.txt"),
                "# loc_29E16: the jet scooter's, on MotaviaOutside "
                "(blocks_motavia_outside), one line per block", table, n);
    count++;
    ex_say(x, "%d collision tables -> %s", count, a->out);
}

/* ---- interactions ---- */

#define FIRST_EXIT_LEVEL 4
#define EXIT_TABLE       "loc_28546"

static const char *level_name(Extract *x, const JsonValue *levels,
                              long long index)
{
    /* the Python's {index: name}: a later level of the same index wins */
    const char *found = "?";
    for (const JsonValue *lv = levels->child; lv != NULL; lv = lv->next) {
        if (int_of(x, lv, "index") == index) {
            found = str_of(x, lv, "name");
        }
    }
    return found;
}

void ex_step_interactions(Extract *x, const ExArgs *a)
{
    const JsonValue *levels = levels_of(x, a);
    int lists = 0;
    for (const JsonValue *lv = levels->child; lv != NULL; lv = lv->next) {
        const char *source = str_of(x, lv, "interactions");
        size_t n = 0;
        const u8 *raw = ex_blob(x, source, &n);
        JOut *entries = jo_arr(x);
        for (size_t at = 0; at + 6 <= n; at += 6) {
            const u8 *rec = raw + at;
            if (rec[0] == 0xFF) {
                break;
            }
            JOut *e = jo_obj(x);
            jo_seti(x, e, "y", rec[0]);
            jo_seti(x, e, "x", rec[1]);
            if (rec[2] == 0) {
                jo_seti(x, e, "to_level", rec[3]);
                jo_sets(x, e, "to_name", level_name(x, levels, rec[3]));
                jo_seti(x, e, "to_y", rec[4]);
                jo_seti(x, e, "to_x", rec[5]);
            } else {
                jo_seti(x, e, "building", rec[2]);
                jo_seti(x, e, "portrait", rec[3]);
                jo_seti(x, e, "f764", rec[4]);
                jo_seti(x, e, "f766", rec[5]);
            }
            jo_add(entries, e);
        }
        JOut *doc = jo_obj(x);
        jo_sets(x, doc, "units", "16px cells");
        jo_sets(x, doc, "source", source);
        jo_set(x, doc, "entries", entries);
        ex_write_text(x, ex_path(x, a->out, ex_fmt(x, "%s.doors.json",
                                                   stem_of(x, lv))),
                      jo_dump(x, doc, 1, false));
        lists++;
    }

    /* Three bytes an exit, the first a level; the table's odd length
     * leaves `even`'s pad byte, which makes no whole entry. */
    size_t n = 0;
    const u8 *raw = ex_blob(x, EXIT_TABLE, &n);
    JOut *exits = jo_arr(x);
    for (size_t i = 0; i < n / 3; i++) {
        long long from = FIRST_EXIT_LEVEL + (long long)i;
        JOut *e = jo_obj(x);
        jo_seti(x, e, "from_level", from);
        jo_sets(x, e, "from_name", level_name(x, levels, from));
        jo_seti(x, e, "to_level", raw[3 * i]);
        jo_sets(x, e, "to_name", level_name(x, levels, raw[3 * i]));
        jo_seti(x, e, "to_y", raw[3 * i + 1]);
        jo_seti(x, e, "to_x", raw[3 * i + 2]);
        jo_add(exits, e);
    }
    JOut *doc = jo_obj(x);
    jo_sets(x, doc, "units", "16px cells");
    jo_sets(x, doc, "source", EXIT_TABLE);
    jo_set(x, doc, "entries", exits);
    ex_write_text(x, ex_path(x, a->out, "exits.json"),
                  jo_dump(x, doc, 1, false));
    ex_say(x, "%d door lists, %d exits -> %s", lists, jo_len(exits), a->out);
}
