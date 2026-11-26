/* (build 123) The game's own folder and its saves' (home.h). */
#define _POSIX_C_SOURCE 200809L

#include "home.h"

#include "../core/assetfs.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#define make_one_dir(path) _mkdir(path)
#else
#include <unistd.h>
#define make_one_dir(path) mkdir(path, 0755)
#endif
#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#endif

#define SAVES_HERE  "saves"
#define MODS_HERE   "mods"
#define PROBE_NAME  ".ps2_write_test"

/* (build 193) The folder home_enter left for the game's own, in UTF-8;
 * "" when it left none. */
static char g_left[1024];

static bool readable(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}

static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Every folder of `path` made, as mkdir -p. */
static void make_dirs(const char *path)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = '\0';
            if (!(p == buf + 2 && buf[1] == ':')) {     /* not "C:" */
                make_one_dir(buf);
            }
            *p = c;
        }
    }
    make_one_dir(buf);
}

/* Whether a file can be made in `dir`: one made and removed. */
static bool can_write(const char *dir)
{
    char probe[1024];
    snprintf(probe, sizeof probe, "%s/%s", dir, PROBE_NAME);
    FILE *f = fopen(probe, "wb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    remove(probe);
    return true;
}

/* (build 138b, from home_saves) `leaf` in the user's own data folder:
 * Windows %LOCALAPPDATA%\PhantasyStarII\<leaf>; (macOS) ~/Library/
 * Application Support/PhantasyStarII/<leaf>; elsewhere $XDG_DATA_HOME, or
 * ~/.local/share, then phantasy-star-ii/<leaf>; `leaf` itself, beside the
 * game, when there is no such folder. */
static void user_folder(char *out, size_t n, const char *leaf)
{
#ifdef _WIN32
    const char *base = getenv("LOCALAPPDATA");
    if (base != NULL && base[0] != '\0') {
        snprintf(out, n, "%s/PhantasyStarII/%s", base, leaf);
    } else {
        snprintf(out, n, "%s", leaf);
    }
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        snprintf(out, n, "%s/Library/Application Support/PhantasyStarII/%s",
                 home, leaf);
    } else {
        snprintf(out, n, "%s", leaf);
    }
#else
    const char *xdg  = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        snprintf(out, n, "%s/phantasy-star-ii/%s", xdg, leaf);
    } else if (home != NULL && home[0] != '\0') {
        snprintf(out, n, "%s/.local/share/phantasy-star-ii/%s", home, leaf);
    } else {
        snprintf(out, n, "%s", leaf);
    }
#endif
}

void home_enter(void)
{
    if (readable("assets/manifest.txt")) {
        return;
    }
#ifdef _WIN32
    wchar_t exe[MAX_PATH];
    DWORD   len = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return;
    }
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash == NULL) {
        return;
    }
    *slash = L'\0';
    wchar_t here[MAX_PATH];
    bool    known = _wgetcwd(here, MAX_PATH) != NULL;
    if (_wchdir(exe) == 0) {
        fprintf(stderr, "home: the game's folder, %ls\n", exe);
        if (!known
            || WideCharToMultiByte(CP_UTF8, 0, here, -1, g_left,
                                   (int)sizeof g_left, NULL, NULL) <= 0) {
            g_left[0] = '\0';
        }
    }
#elif defined(__APPLE__)
    /* (macOS) No /proc: dyld's path to the program, resolved through any
     * links and "..". */
    char     given[PATH_MAX];
    char     exe[PATH_MAX];
    uint32_t size = sizeof given;
    if (_NSGetExecutablePath(given, &size) != 0
        || realpath(given, exe) == NULL) {
        return;
    }
    char *slash = strrchr(exe, '/');
    if (slash == NULL) {
        return;
    }
    *slash = '\0';
    char here[PATH_MAX];
    bool known = getcwd(here, sizeof here) != NULL;
    if (chdir(exe) == 0) {
        fprintf(stderr, "home: the game's folder, %s\n", exe);
        snprintf(g_left, sizeof g_left, "%s", known ? here : "");
    }
#else
    char    exe[1024];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (len <= 0) {
        return;
    }
    exe[len] = '\0';
    char *slash = strrchr(exe, '/');
    if (slash == NULL) {
        return;
    }
    *slash = '\0';
    char here[1024];
    bool known = getcwd(here, sizeof here) != NULL;
    if (chdir(exe) == 0) {
        fprintf(stderr, "home: the game's folder, %s\n", exe);
        snprintf(g_left, sizeof g_left, "%s", known ? here : "");
    }
#endif
}

void home_given_path(const char *given, char *out, size_t n)
{
#ifdef _WIN32
    bool absolute = given[0] == '/' || given[0] == '\\'
                 || (given[0] != '\0' && given[1] == ':');
#else
    bool absolute = given[0] == '/';
#endif
    if (absolute || given[0] == '\0' || g_left[0] == '\0') {
        snprintf(out, n, "%s", given);
    } else {
        snprintf(out, n, "%s/%s", g_left, given);
    }
}

void home_saves(char *out, size_t n)
{
    const char *forced = getenv("PS2_SAVES");
    if (forced != NULL && forced[0] != '\0') {
        snprintf(out, n, "%s", forced);
        make_dirs(out);
        fprintf(stderr, "saves: %s (PS2_SAVES)\n", out);
        return;
    }
    if (can_write(is_dir(SAVES_HERE) ? SAVES_HERE : ".")) {
        snprintf(out, n, "%s", SAVES_HERE);
        return;
    }
    user_folder(out, n, SAVES_HERE);
    make_dirs(out);
    fprintf(stderr, "saves: %s (the game's own folder cannot be written)\n",
            out);
}

void home_mods(char *out, size_t n)
{
    const char *forced = getenv("PS2_MODS");
    if (forced != NULL && forced[0] != '\0') {
        snprintf(out, n, "%s", forced);
        fprintf(stderr, "mods: %s (PS2_MODS)\n", out);
        return;
    }
    if (can_write(is_dir(MODS_HERE) ? MODS_HERE : ".")) {
        snprintf(out, n, "%s", MODS_HERE);
        return;
    }
    user_folder(out, n, MODS_HERE);
}

/* Every folder above the file `path`, made. */
static void make_parents(const char *path)
{
    char buf[2048];
    snprintf(buf, sizeof buf, "%s", path);
    char *slash = strrchr(buf, '/');
    char *back  = strrchr(buf, '\\');
    if (back != NULL && (slash == NULL || back > slash)) {
        slash = back;
    }
    if (slash == NULL || slash == buf) {
        return;
    }
    *slash = '\0';
    make_dirs(buf);
}

static bool write_whole(const char *path, const void *data, size_t size)
{
    make_parents(path);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    bool ok = fwrite(data, 1, size, f) == size;
    return (fclose(f) == 0) && ok;
}

static const char MODS_README[] =
    "Phantasy Star II -- the mods folder\n"
    "\n"
    "The game reads a file here in place of its own. Its own files are in\n"
    "originals/ beside this file (the game as one program: it writes them\n"
    "there on its first start, and again when the game itself changes) or,\n"
    "for a game built from its source tree, in assets/ beside the game. The\n"
    "game never reads them from originals/.\n"
    "\n"
    "To change something, copy its file to the same place here and edit the\n"
    "copy. To change the title's song:\n"
    "\n"
    "    originals/music/songs/phantasy.txt  ->  music/songs/phantasy.txt\n"
    "    (from the tree: assets/music/songs/phantasy.txt)\n"
    "\n"
    "then start the game again. A song file's opening lines say how to read\n"
    "it: notes by name (C#6), lengths in ticks, `instrument` by name. To undo\n"
    "a change, delete the copy.\n"
    "\n"
    "If the game cannot read the music here (a word it does not know, a note\n"
    "out of range, a label missing), it says which file and line on its\n"
    "console and plays its own music instead. Pictures (PNG), maps (Tiled\n"
    ".tmx) and data (JSON) are read the same way.\n";

void home_write_originals(const char *mods)
{
    if (mods == NULL || mods[0] == '\0') {
        return;
    }
    char readme[1100];
    snprintf(readme, sizeof readme, "%s/README.txt", mods);
    int count = assetfs_pack_count();
    if (count <= 0) {
        /* (build 138c) A tree build: its own files are its assets/. */
        FILE *have_readme = fopen(readme, "rb");
        if (have_readme != NULL) {
            fclose(have_readme);
        } else if (!write_whole(readme, MODS_README,
                                sizeof MODS_README - 1)) {
            fprintf(stderr, "mods: cannot write %s\n", readme);
        }
        return;
    }
    char stamp[1100];
    char want[16];
    char have[16] = "";
    snprintf(stamp, sizeof stamp, "%s/originals/pack_id.txt", mods);
    snprintf(want, sizeof want, "%08X", (unsigned)assetfs_pack_id());
    FILE *f = fopen(stamp, "rb");
    if (f != NULL) {
        size_t k = fread(have, 1, sizeof have - 1, f);
        have[k] = '\0';
        fclose(f);
    }
    if (strcmp(have, want) == 0) {
        return;
    }
    int written = 0;
    for (int i = 0; i < count; i++) {
        const char *path;
        const u8   *data;
        size_t      size;
        if (!assetfs_pack_entry(i, &path, &data, &size)) {
            break;
        }
        if (strncmp(path, "assets/", 7) != 0) {
            continue;
        }
        char dest[1600];
        snprintf(dest, sizeof dest, "%s/originals/%s", mods, path + 7);
        if (!write_whole(dest, data, size)) {
            fprintf(stderr, "mods: cannot write %s; the originals are not "
                            "all there\n", dest);
            return;
        }
        written++;
    }
    write_whole(readme, MODS_README, sizeof MODS_README - 1);
    write_whole(stamp, want, strlen(want));
    fprintf(stderr, "mods: %d of the game's files written out to "
                    "%s/originals\n", written, mods);
}
