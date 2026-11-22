#ifndef PS2_LANG_H
#define PS2_LANG_H

#include "../core/video.h"

#include <stddef.h>

/* (build 163) The languages (docs/languages.md): the Escape menu's
 * LANGUAGE, settings.json's "language".
 *
 * assets/lang/languages.json lists them in the menu's order. The first is
 * the game as the cartridge has it, the 1989 US English, whose files are
 * the ones under assets/ and which nothing here changes. Each other one is
 * a folder, assets/lang/<key>/, whose files stand in for the game's own
 * of the same path (assets/lang/<key>/script/Script_People.txt for
 * assets/script/Script_People.txt) and which adds what the cartridge has
 * no file for: names.json (the names and labels drawn), and with a
 * proportional face (lang.json's "proportional") font.png and its
 * advances. A file a language does not carry is the game's own.
 *
 * Only the game's text changes with the language: the Escape menu is
 * drawn in the stock font in every language, and nothing that plays the
 * game -- the routes, the harness, COMPUTER PLAYS -- reads a word of it
 * (the probe's `expect text` excepted, which says which language). */

#define LANG_MAX       8
#define LANG_KEY_LEN   24
#define LANG_NAME_LEN  12           /* the Escape menu's value column */

/* Read assets/lang/languages.json and choose the first language. */
void lang_init(void);

int         lang_count(void);
const char *lang_key(int i);          /* settings.json's: "us" */
const char *lang_name(int i);         /* the menu's: "ENGLISH 1989" */
int         lang_find(const char *key);   /* -1 when no language has it */
int         lang_current(void);

/* (build 186) Whether language `i`'s files are not there yet: a language
 * of a folder of its own with no lang.json in it -- the JAPANESE language
 * until the player makes it from their own Japanese cartridge
 * (docs/first_start.md). The cartridge's own never is. */
bool        lang_missing(int i);

/* (build 186) Every language's default names read again, for one whose
 * files were made after lang_init. The language chosen stays chosen. */
void        lang_rescan(void);

/* Make language `i` the one the game reads, loading its names and face.
 * Its script and window files are read where the game asks for them
 * (lang_path), so a caller that keeps what it read -- the script banks,
 * the window maps -- reads them again. A language whose folder is broken
 * says why on stderr and the stock one is chosen. */
void lang_select(int i);

/* The path the game reads for an asset path ("assets/script/x.txt"):
 * the language's copy when it carries one, else `path` itself. `buf`
 * holds the copy's path. */
const char *lang_path(const char *path, char *buf, size_t n);

/* ---- the face ---- */

/* True when the language's text is drawn in its proportional face, a
 * glyph a script byte at its own advance, rather than a cell a byte. */
bool lang_proportional(void);

/* The face's glyph for a script byte: 8x8, index 1 ink, 0 paper; and its
 * advance in pixels. Only for a proportional language. */
const Tile *lang_glyph(u8 code);
int         lang_advance(u8 code);

/* (build 165) A face of two rows a glyph (lang.json "tall"), the
 * Japanese cartridge's: each code's upper tile -- the dakuten or
 * handakuten -- drawn in the row above its letter, as the cartridge's
 * window text has it. lang_draw with `upper` draws those. */
bool        lang_tall(void);
const Tile *lang_glyph_upper(u8 code);

/* How many pixels `codes` take, glyph after glyph. */
int lang_measure(const u8 *codes, int n);

/* Draws `codes` from pixel `x` of a strip `cells` tiles wide into
 * `strip` (cells tiles, left to right, made ready by the caller): each
 * glyph's ink as `ink`, where the paper is left alone. A glyph that would
 * cross the strip's end is dropped and so is everything after it. */
void lang_draw(Tile *strip, int cells, int x, const u8 *codes, int n, u8 ink);
void lang_draw_upper(Tile *strip, int cells, int x, const u8 *codes, int n,
                     u8 ink);

/* ---- the names ---- */

/* Text of the language's to script bytes: the letters of the script
 * charset, {XX} for any byte, and the letters past ASCII its lang.json
 * gives (UTF-8). At most `max`; the count written. (build 165) A language
 * whose charset is its own whole (lang.json "charset_full", the Japanese
 * cartridge's) takes every character, ASCII too, from it. */
int lang_codes(const char *text, u8 *out, int max);

/* (build 165) One character's code (lang_codes, a code point at a time),
 * 0 -- a space -- for one the language has none for; and whether the
 * language's charset is its own whole. */
u8   lang_code_of(u32 cp);
bool lang_charset_full(void);

/* (build 165) A code point from UTF-8 at *pp, advancing past it. */
u32  lang_next_code_point(const char **pp);

/* (build 165) Codes back to the language's text (UTF-8), up to $C4 or
 * `n`: for a name kept as text across languages and in the saves. */
void lang_text_of(const u8 *codes, int n, char *out, size_t size);

/* (build 165) A layout the language's words need (lang.json "layout"):
 * "battle_box" (the dialogue window's width, two lines), "wide_techs"
 * (the technique windows a cell wider), "wide_menu" (the main menu a cell
 * wider), "long_names" (six letters a name) -- the retranslation's, none
 * the Japanese cartridge's, whose words its own windows hold. */
bool lang_layout(const char *flag);

/* The language's names (names.json), or NULL where it has none and the
 * game's own stand. Items and techniques by their keys in items.json and
 * techniques.json, enemies by their battle.json label, the rest by
 * place. */
const char *lang_item_name(const char *key);
const char *lang_tech_name(const char *key);
const char *lang_enemy_name(const char *label);
const char *lang_char_name(int index);
/* True when `name` is character `index`'s default in any language's
 * names.json (the cartridge's own are gamedata's to know). */
bool        lang_is_default_name(int index, const char *name);
const char *lang_list_name(const char *list, int index);
const char *lang_window_label(const char *art, int row);

#endif /* PS2_LANG_H */
