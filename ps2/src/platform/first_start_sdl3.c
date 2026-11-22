/* (build 186) The first start, SDL3's (platform.h, docs/first_start.md).
 *
 * The game's files are made from the player's own cartridge by
 * tools/make_assets.py. This is the window that asks for it: an image
 * dropped on it or chosen in the system's file dialog, the script run on
 * it with its steps shown as they go, and what it said when it could not.
 * The game's own font is one of the files to be made, so the window writes
 * in SDL's built-in 8x8 face (SDL_RenderDebugText), on an SDL_Renderer of
 * its own; it closes before the game's window opens, or -- adding a
 * language -- stands over it while the game waits.
 *
 * The script runs in a Python found once a run: PS2_PYTHON; a Python
 * packaged beside the game (python/ in the folder above the game's or in
 * its own, pythonw.exe first on Windows, which opens no console); then
 * python3, python and (Windows) py -3 on the PATH. Each is asked to import
 * Pillow first, which the extractors need. */
#include "platform.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* The window: 80 columns and 45 rows of the 8x8 face, drawn twice the
 * size (whole multiples when it is made bigger). */
#define FS_W        640
#define FS_H        360
#define FS_SCALE    2
#define FS_CELL     8
#define FS_ROW_H    11                      /* a line, with its leading */
#define FS_ROWS     (FS_H / FS_ROW_H)
#define FS_COLS     (FS_W / FS_CELL)
#define FS_MARGIN   4                       /* columns */
#define FS_TEXT     (FS_COLS - 2 * FS_MARGIN)

#define FS_PATH     1024
#define FS_LINE     160
#define FS_TAIL     12                      /* the script's last lines kept */
#define FS_FILES    4

#define SCRIPT      "tools/make_assets.py"

/* How long a Python gets to answer whether it has Pillow. */
#define PROBE_SECONDS 15.0

typedef enum {
    WHAT_GAME,          /* the game's own files, from the US cartridge */
    WHAT_JAPANESE       /* the JAPANESE language */
} What;

typedef enum {
    STATE_ASK,          /* waiting for the files */
    STATE_RUN,          /* the script running */
    STATE_FAILED,       /* it could not: what it said, and ask again */
    STATE_DIFFERS,      /* made, but not all as proved: play on or quit */
    STATE_DONE,
    STATE_LEFT          /* the player closed it */
} State;

typedef struct {
    What          what;
    const char   *lang_name;
    State         state;
    SDL_Window   *window;
    SDL_Renderer *renderer;

    /* The files to give the script: for the game one image, for the
     * JAPANESE language the cartridge and the retranslation. */
    char cartridge[FS_PATH];
    char retranslation[FS_PATH];

    /* The script's run. */
    SDL_Process *proc;
    char         partial[FS_LINE];
    size_t       partial_len;
    char         tail[FS_TAIL][FS_LINE];
    int          tail_count;
    int          step;
    int          steps;
    char         step_name[FS_LINE];
    int          exit_code;

    /* The file dialog, whose answer may come on another thread. */
    SDL_AtomicInt dialog_open;
    SDL_AtomicInt dialog_done;
    char          dialog_path[FS_PATH];

    char note[FS_LINE * 2];             /* one line under the instructions */
} FirstStart;

/* ---- the Python ---- */

static char g_python[3][FS_PATH];       /* argv's first one or two */
static int  g_python_args = -1;         /* -1 not looked for, 0 none found */

static bool path_is(const char *path, SDL_PathType type)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == type;
}

/* Whether ARGV runs a Python with Pillow: it is asked to import it. */
static bool probe_python(const char *const *argv)
{
    const char *args[8];
    int n = 0;
    for (; argv[n] != NULL && n < 4; n++) {
        args[n] = argv[n];
    }
    args[n++] = "-c";
    args[n++] = "import PIL";
    args[n]   = NULL;

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                           (void *)args);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                          SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                          SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                          SDL_PROCESS_STDIO_NULL);
    SDL_Process *p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (p == NULL) {
        return false;
    }
    int    code  = -1;
    Uint64 start = SDL_GetTicks();
    bool   ended = false;
    while (!(ended = SDL_WaitProcess(p, false, &code))
           && (double)(SDL_GetTicks() - start) / 1000.0 < PROBE_SECONDS) {
        SDL_PumpEvents();
        SDL_Delay(20);
    }
    if (!ended) {
        SDL_KillProcess(p, true);
        SDL_WaitProcess(p, true, &code);
        code = -1;
    }
    SDL_DestroyProcess(p);
    return code == 0;
}

#ifdef _WIN32
/* (Windows) The windowless twin of a console Python named by its path --
 * pythonw.exe beside python.exe or python3.exe -- into `out`, where there
 * is one: a console Python started by a game with no console of its own
 * opens a console window over it. */
static const char *windowless(const char *path, char *out, size_t n)
{
    const char *base = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    if (SDL_strcasecmp(base, "python.exe") != 0
        && SDL_strcasecmp(base, "python3.exe") != 0) {
        return NULL;
    }
    static const char name[] = "pythonw.exe";
    size_t dir = (size_t)(base - path);
    if (dir + sizeof name > n) {
        return NULL;
    }
    memcpy(out, path, dir);
    memcpy(out + dir, name, sizeof name);
    return path_is(out, SDL_PATHTYPE_FILE) ? out : NULL;
}
#endif

static void find_python(void)
{
    if (g_python_args >= 0) {
        return;
    }
    g_python_args = 0;
    const char *forced = SDL_getenv("PS2_PYTHON");
#ifdef _WIN32
    char        twin_buf[FS_PATH];
    const char *twin = (forced != NULL) ? windowless(forced, twin_buf,
                                                     sizeof twin_buf)
                                        : NULL;
#endif
    const char *candidates[][3] = {
#ifdef _WIN32
        { twin, NULL, NULL },
#endif
        { forced, NULL, NULL },
#ifdef _WIN32
        /* pythonw and pyw first: no console window (windowless above) */
        { "..\\python\\pythonw.exe", NULL, NULL },
        { "..\\python\\python.exe", NULL, NULL },
        { "python\\pythonw.exe", NULL, NULL },
        { "python\\python.exe", NULL, NULL },
        { "pythonw", NULL, NULL },
        { "pyw", "-3", NULL },
        { "python3", NULL, NULL },
        { "python", NULL, NULL },
        { "py", "-3", NULL },
#else
        { "../python/bin/python3", NULL, NULL },
        { "python/bin/python3", NULL, NULL },
        { "python3", NULL, NULL },
        { "python", NULL, NULL },
#endif
    };
    for (size_t i = 0; i < SDL_arraysize(candidates); i++) {
        const char *first = candidates[i][0];
        if (first == NULL || first[0] == '\0') {
            continue;
        }
        /* A path is tried only where it is; a bare name on the PATH. */
        bool is_path = strchr(first, '/') != NULL
                    || strchr(first, '\\') != NULL;
        if (is_path && !path_is(first, SDL_PATHTYPE_FILE)) {
            continue;
        }
        if (probe_python(candidates[i])) {
            int n = 0;
            for (; n < 2 && candidates[i][n] != NULL; n++) {
                SDL_snprintf(g_python[n], FS_PATH, "%s", candidates[i][n]);
            }
            g_python_args = n;
            fprintf(stderr, "first start: Python %s%s%s\n", g_python[0],
                    (n > 1) ? " " : "", (n > 1) ? g_python[1] : "");
            return;
        }
    }
    fprintf(stderr, "first start: no Python with Pillow found\n");
}

/* ---- the script's run ---- */

static void tail_push(FirstStart *fs, const char *line)
{
    if (fs->tail_count == FS_TAIL) {
        memmove(fs->tail[0], fs->tail[1], sizeof fs->tail[0] * (FS_TAIL - 1));
        fs->tail_count--;
    }
    SDL_snprintf(fs->tail[fs->tail_count++], FS_LINE, "%s", line);
}

/* One whole line of the script's: a step's `[K/N] WHAT`, or a sentence
 * kept for the screen. Each goes to the console too. */
static void take_line(FirstStart *fs, char *line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) {
        line[--n] = '\0';
    }
    fprintf(stderr, "make_assets: %s\n", line);
    if (line[0] == '[') {
        char *end = NULL;
        long  k   = SDL_strtol(line + 1, &end, 10);
        long  of  = 0;
        if (end != NULL && *end == '/') {
            of = SDL_strtol(end + 1, &end, 10);
        }
        if (of > 0 && end != NULL && end[0] == ']' && end[1] == ' ') {
            fs->step  = (int)k;
            fs->steps = (int)of;
            SDL_snprintf(fs->step_name, sizeof fs->step_name, "%s", end + 2);
            return;
        }
    }
    if (n > 0) {
        tail_push(fs, line);
    }
}

static void read_output(FirstStart *fs)
{
    SDL_IOStream *out = SDL_GetProcessOutput(fs->proc);
    if (out == NULL) {
        return;
    }
    char buf[512];
    size_t got;
    while ((got = SDL_ReadIO(out, buf, sizeof buf)) > 0) {
        for (size_t i = 0; i < got; i++) {
            if (buf[i] == '\n') {
                fs->partial[fs->partial_len] = '\0';
                take_line(fs, fs->partial);
                fs->partial_len = 0;
            } else if (fs->partial_len + 1 < sizeof fs->partial) {
                fs->partial[fs->partial_len++] = buf[i];
            }
        }
    }
}

static bool start_script(FirstStart *fs)
{
    fs->tail_count = 0;
    fs->step       = 0;
    fs->steps      = 0;
    fs->step_name[0] = '\0';
    fs->note[0]      = '\0';
    fs->partial_len  = 0;

    if (!path_is(SCRIPT, SDL_PATHTYPE_FILE)) {
        tail_push(fs, "This copy of the game has no " SCRIPT ", which makes "
                      "its files.");
        fs->state = STATE_FAILED;
        return false;
    }
    find_python();
    if (g_python_args <= 0) {
        tail_push(fs, "The game's files are made by a Python 3 script, and "
                      "no Python 3 with Pillow was found.");
#ifdef _WIN32
        tail_push(fs, "Install Python 3 from python.org, then in a command "
                      "prompt: py -m pip install pillow");
#elif defined(__APPLE__)
        tail_push(fs, "In a terminal: brew install python pillow");
#else
        tail_push(fs, "Debian/Ubuntu: sudo apt install python3 python3-pil");
        tail_push(fs, "Fedora: sudo dnf install python3 python3-pillow");
        tail_push(fs, "Arch: sudo pacman -S python python-pillow");
#endif
        tail_push(fs, "Or name one: PS2_PYTHON=/path/to/python3");
        fs->state = STATE_FAILED;
        return false;
    }

    const char *args[12];
    int n = 0;
    for (int i = 0; i < g_python_args; i++) {
        args[n++] = g_python[i];
    }
    args[n++] = "-X";
    args[n++] = "utf8";
    args[n++] = "-u";
    args[n++] = SCRIPT;
    if (fs->cartridge[0] != '\0') {
        args[n++] = fs->cartridge;
    }
    if (fs->retranslation[0] != '\0') {
        args[n++] = fs->retranslation;
    }
    args[n] = NULL;

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                           (void *)args);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                          SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                          SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props,
                           SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,
                           true);
    fs->proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (fs->proc == NULL) {
        char line[FS_LINE];
        SDL_snprintf(line, sizeof line, "%s could not be started: %s",
                     g_python[0], SDL_GetError());
        tail_push(fs, line);
        fs->state = STATE_FAILED;
        return false;
    }
    fprintf(stderr, "first start: %s %s%s%s\n", SCRIPT, fs->cartridge,
            (fs->retranslation[0] != '\0') ? " " : "", fs->retranslation);
    fs->state = STATE_RUN;
    return true;
}

/* The run's end, when it has ended: its last output, its status. */
static void watch_script(FirstStart *fs)
{
    read_output(fs);
    int code = -1;
    if (!SDL_WaitProcess(fs->proc, false, &code)) {
        return;
    }
    read_output(fs);
    if (fs->partial_len > 0) {
        fs->partial[fs->partial_len] = '\0';
        take_line(fs, fs->partial);
        fs->partial_len = 0;
    }
    SDL_DestroyProcess(fs->proc);
    fs->proc      = NULL;
    fs->exit_code = code;
    fprintf(stderr, "first start: %s ended, status %d\n", SCRIPT, code);
    if (code == 0) {
        fs->state = STATE_DONE;
    } else if (code == 3) {
        fs->state = STATE_DIFFERS;
    } else {
        fs->state = STATE_FAILED;
        /* Asked again from the start: the next files are a new pair. */
        fs->cartridge[0]     = '\0';
        fs->retranslation[0] = '\0';
    }
}

static void stop_script(FirstStart *fs)
{
    if (fs->proc == NULL) {
        return;
    }
    SDL_KillProcess(fs->proc, true);
    int code = 0;
    SDL_WaitProcess(fs->proc, true, &code);
    SDL_DestroyProcess(fs->proc);
    fs->proc = NULL;
    fprintf(stderr, "first start: %s stopped\n", SCRIPT);
}

/* ---- the files given ---- */

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *back  = strrchr(path, '\\');
    if (back != NULL && (slash == NULL || back > slash)) {
        slash = back;
    }
    return (slash != NULL) ? slash + 1 : path;
}

/* Whether a file is the retranslation rather than a cartridge, for the
 * screen to say which it has: a folder, or a zip naming
 * work/dialogue.json (its names are stored uncompressed). The script
 * tells them apart again, properly. */
static bool looks_like_retranslation(const char *path)
{
    if (path_is(path, SDL_PATHTYPE_DIRECTORY)) {
        return true;
    }
    static const char want[] = "work/dialogue.json";
    size_t size = 0;
    char  *data = SDL_LoadFile(path, &size);
    if (data == NULL) {
        return false;
    }
    bool found = false;
    if (size > 4 && memcmp(data, "PK\x03\x04", 4) == 0) {
        size_t w = sizeof want - 1;
        for (size_t i = 0; i + w <= size && !found; i++) {
            found = data[i] == 'w' && memcmp(data + i, want, w) == 0;
        }
    }
    SDL_free(data);
    return found;
}

/* A file dropped or chosen: the game's image runs at once; a language's
 * two things wait for each other. */
static void offer(FirstStart *fs, const char *path)
{
    if (fs->state == STATE_RUN || path == NULL || path[0] == '\0') {
        return;
    }
    if (fs->what == WHAT_GAME) {
        SDL_snprintf(fs->cartridge, sizeof fs->cartridge, "%s", path);
        start_script(fs);
        return;
    }
    if (looks_like_retranslation(path)) {
        SDL_snprintf(fs->retranslation, sizeof fs->retranslation, "%s", path);
    } else {
        SDL_snprintf(fs->cartridge, sizeof fs->cartridge, "%s", path);
    }
    fs->state = STATE_ASK;
    if (fs->cartridge[0] != '\0' && fs->retranslation[0] != '\0') {
        start_script(fs);
    }
}

static void SDLCALL dialog_answer(void *userdata, const char *const *files,
                                  int filter)
{
    (void)filter;
    FirstStart *fs = userdata;
    fs->dialog_path[0] = '\0';
    if (files != NULL && files[0] != NULL) {
        SDL_snprintf(fs->dialog_path, sizeof fs->dialog_path, "%s", files[0]);
    } else if (files == NULL) {
        fprintf(stderr, "first start: the file dialog failed: %s\n",
                SDL_GetError());
    }
    SDL_SetAtomicInt(&fs->dialog_done, 1);
}

static const SDL_DialogFileFilter CARTRIDGE_FILTERS[] = {
    { "Cartridge images (.bin .md .gen .smd .zip)", "bin;md;gen;smd;zip" },
    { "All files", "*" },
};
static const SDL_DialogFileFilter RETRANSLATION_FILTERS[] = {
    { "The retranslation's zip", "zip" },
    { "All files", "*" },
};

static void choose(FirstStart *fs)
{
    if (SDL_GetAtomicInt(&fs->dialog_open) != 0 || fs->state == STATE_RUN) {
        return;
    }
    bool second = fs->what == WHAT_JAPANESE && fs->cartridge[0] != '\0'
               && fs->retranslation[0] == '\0';
    SDL_SetAtomicInt(&fs->dialog_open, 1);
    SDL_SetAtomicInt(&fs->dialog_done, 0);
    if (second) {
        SDL_ShowOpenFileDialog(dialog_answer, fs, fs->window,
                               RETRANSLATION_FILTERS,
                               (int)SDL_arraysize(RETRANSLATION_FILTERS),
                               NULL, false);
    } else {
        SDL_ShowOpenFileDialog(dialog_answer, fs, fs->window,
                               CARTRIDGE_FILTERS,
                               (int)SDL_arraysize(CARTRIDGE_FILTERS), NULL,
                               false);
    }
}

/* ---- the command line, as UTF-8 ---- */

#ifdef _WIN32
/* Windows hands main() its arguments in the ANSI code page, which cannot
 * hold every file name; they are read again from the wide command line
 * by CommandLineToArgvW's rules, the program's own name skipped. */
static int wide_arguments(char out[][FS_PATH], int max)
{
    const wchar_t *p = GetCommandLineW();
    if (p == NULL) {
        return 0;
    }
    if (*p == L'"') {                       /* the program: up to a quote */
        p++;
        while (*p != L'\0' && *p != L'"') {
            p++;
        }
        if (*p == L'"') {
            p++;
        }
    } else {
        while (*p != L'\0' && *p != L' ' && *p != L'\t') {
            p++;
        }
    }
    int count = 0;
    static wchar_t arg[FS_PATH];
    for (;;) {
        while (*p == L' ' || *p == L'\t') {
            p++;
        }
        if (*p == L'\0' || count == max) {
            break;
        }
        size_t n      = 0;
        bool   quoted = false;
        while (*p != L'\0' && (quoted || (*p != L' ' && *p != L'\t'))) {
            size_t slashes = 0;
            while (*p == L'\\') {
                slashes++;
                p++;
            }
            if (*p == L'"') {
                for (size_t s = 0; s < slashes / 2 && n + 1 < FS_PATH; s++) {
                    arg[n++] = L'\\';
                }
                if (slashes % 2 == 1) {
                    if (n + 1 < FS_PATH) {
                        arg[n++] = L'"';
                    }
                } else if (quoted && p[1] == L'"') {
                    if (n + 1 < FS_PATH) {
                        arg[n++] = L'"';
                    }
                    p++;
                } else {
                    quoted = !quoted;
                }
                p++;
            } else {
                for (size_t s = 0; s < slashes && n + 1 < FS_PATH; s++) {
                    arg[n++] = L'\\';
                }
                if (*p != L'\0' && (quoted || (*p != L' ' && *p != L'\t'))) {
                    if (n + 1 < FS_PATH) {
                        arg[n++] = *p;
                    }
                    p++;
                }
            }
        }
        arg[n] = L'\0';
        if (WideCharToMultiByte(CP_UTF8, 0, arg, -1, out[count], FS_PATH,
                                NULL, NULL) > 0) {
            count++;
        }
    }
    return count;
}
#endif

static int given_files(int nfiles, char **files, char out[][FS_PATH],
                       int max)
{
#ifdef _WIN32
    (void)nfiles;
    (void)files;
    return wide_arguments(out, max);
#else
    int n = 0;
    for (; n < nfiles && n < max; n++) {
        SDL_snprintf(out[n], FS_PATH, "%s", files[n]);
    }
    return n;
#endif
}

/* ---- the screen ---- */

static void colour(SDL_Renderer *r, Uint8 red, Uint8 green, Uint8 blue)
{
    SDL_SetRenderDrawColor(r, red, green, blue, 255);
}

/* TEXT from column FS_MARGIN of row ROW, wrapped at spaces to FS_TEXT
 * columns, (indent) INDENT more for every line but the first; the next
 * free row. Only ASCII is in the face: other letters show as it draws
 * them. */
static int text(SDL_Renderer *r, int row, int indent, const char *s)
{
    char line[FS_COLS + 1];
    int  first = 1;
    while (*s != '\0') {
        int width = FS_TEXT - (first ? 0 : indent);
        int len   = (int)strlen(s);
        int cut   = len;
        const char *nl = strchr(s, '\n');
        if (nl != NULL && nl - s <= width) {
            cut = (int)(nl - s);
        } else if (len > width) {
            cut = width;
            while (cut > 0 && s[cut] != ' ') {
                cut--;
            }
            if (cut == 0) {
                cut = width;
            }
        }
        memcpy(line, s, (size_t)cut);
        line[cut] = '\0';
        SDL_RenderDebugText(r,
                            (float)((FS_MARGIN + (first ? 0 : indent))
                                    * FS_CELL),
                            (float)(row * FS_ROW_H), line);
        row++;
        s += cut;
        while (*s == ' ' || *s == '\n') {
            s++;
        }
        first = 0;
    }
    return row;
}

static void bar(SDL_Renderer *r, int row, int step, int steps)
{
    SDL_FRect box  = { (float)(FS_MARGIN * FS_CELL), (float)(row * FS_ROW_H),
                       (float)(FS_TEXT * FS_CELL), (float)FS_CELL };
    SDL_FRect fill = box;
    fill.w = (steps > 0) ? box.w * (float)step / (float)steps : 0.0f;
    colour(r, 0x30, 0x60, 0xC0);
    SDL_RenderFillRect(r, &fill);
    colour(r, 0xA0, 0xB0, 0xD0);
    SDL_RenderRect(r, &box);
}

static int tail_text(SDL_Renderer *r, const FirstStart *fs, int row, int max)
{
    int from = (fs->tail_count > max) ? fs->tail_count - max : 0;
    for (int i = from; i < fs->tail_count && row < FS_ROWS - 7; i++) {
        row = text(r, row, 2, fs->tail[i]);
    }
    return row;
}

static void draw_game(SDL_Renderer *r, const FirstStart *fs)
{
    int row = 3;
    colour(r, 0xFF, 0xE0, 0x80);
    row = text(r, row, 0, "PHANTASY STAR II") + 2;
    colour(r, 0xE8, 0xE8, 0xE8);
    switch (fs->state) {
        case STATE_RUN: {
            row = text(r, row, 0, "Making the game's files from");
            row = text(r, row, 2, base_name(fs->cartridge)) + 2;
            bar(r, row, fs->step, fs->steps);
            row += 2;
            char line[FS_LINE * 2];
            SDL_snprintf(line, sizeof line, "step %d of %d: %s", fs->step,
                         fs->steps, fs->step_name);
            text(r, row, 0, (fs->steps > 0) ? line : "starting...");
            break;
        }
        case STATE_DIFFERS: {
            row = text(r, row, 0, "The game's files are made, but not all "
                                  "are as the game was proved with:") + 1;
            colour(r, 0xB0, 0xB0, 0xB0);
            row = tail_text(r, fs, row, 8) + 1;
            colour(r, 0xE8, 0xE8, 0xE8);
            text(r, row, 0, "It may still play. Enter plays; Escape quits.");
            break;
        }
        default: {
            row = text(r, row, 0,
                       "The game's files are not here yet. They are made "
                       "once, from your own cartridge image of Phantasy "
                       "Star II for the USA/Europe, revision 02 -- the one "
                       "often named \"Phantasy Star II (UE) (REV02)\": a "
                       ".bin, .md, .gen or .smd file, or a .zip holding "
                       "one. Nothing is downloaded or sent anywhere.") + 2;
            if (fs->state == STATE_FAILED) {
                colour(r, 0xFF, 0x90, 0x80);
                row = text(r, row, 0, "That did not work:");
                colour(r, 0xB0, 0xB0, 0xB0);
                row = tail_text(r, fs, row, 9) + 1;
                colour(r, 0xE8, 0xE8, 0xE8);
            }
            colour(r, 0xFF, 0xFF, 0xFF);
            row = text(r, row, 2, "Drop the image onto this window,");
            row = text(r, row, 2, "or press Enter (or click) to choose it.")
                + 2;
            colour(r, 0x90, 0x90, 0x90);
            text(r, FS_ROWS - 4, 0, "Escape quits. The JAPANESE language is added "
                           "afterwards, in the game: Escape, then LANGUAGE.");
            break;
        }
    }
}

static void draw_japanese(SDL_Renderer *r, const FirstStart *fs)
{
    int row = 3;
    char head[FS_LINE];
    SDL_snprintf(head, sizeof head, "PHANTASY STAR II -- THE %s LANGUAGE",
                 fs->lang_name);
    colour(r, 0xFF, 0xE0, 0x80);
    row = text(r, row, 0, head) + 2;
    colour(r, 0xE8, 0xE8, 0xE8);
    if (fs->state == STATE_RUN) {
        row = text(r, row, 0, "Making its files...") + 2;
        bar(r, row, fs->step, fs->steps);
        row += 2;
        char line[FS_LINE * 2];
        SDL_snprintf(line, sizeof line, "step %d of %d: %s", fs->step,
                     fs->steps, fs->step_name);
        text(r, row, 0, (fs->steps > 0) ? line : "starting...");
        return;
    }
    if (fs->state == STATE_DIFFERS) {
        row = text(r, row, 0, "Its files are made, but not all are as the "
                              "game was proved with:") + 1;
        colour(r, 0xB0, 0xB0, 0xB0);
        row = tail_text(r, fs, row, 8) + 1;
        colour(r, 0xE8, 0xE8, 0xE8);
        text(r, row, 0, "It may still read well. Enter goes on; Escape "
                        "leaves it.");
        return;
    }
    row = text(r, row, 0, "Its files are made from two things of your own:")
        + 1;
    row = text(r, row, 3, " 1. the Japanese cartridge's image: Phantasy Star "
                          "II -- Kaerazaru Toki no Owari ni (1989), a .bin, "
                          ".md or .gen file, or a .zip holding one;");
    row = text(r, row, 3, " 2. Excalibur_Z's retranslation, which carries "
                          "the cartridge's Japanese text matched to the US "
                          "script: github.com/ExcalZ/ps2-retranslation, "
                          "then Code, Download ZIP.") + 1;
    if (fs->state == STATE_FAILED) {
        colour(r, 0xFF, 0x90, 0x80);
        row = text(r, row, 0, "That did not work:");
        colour(r, 0xB0, 0xB0, 0xB0);
        row = tail_text(r, fs, row, 7) + 1;
        colour(r, 0xE8, 0xE8, 0xE8);
    }
    char line[FS_LINE * 2];
    SDL_snprintf(line, sizeof line, "cartridge:      %s",
                 (fs->cartridge[0] != '\0') ? base_name(fs->cartridge)
                                            : "(not yet)");
    row = text(r, row, 2, line);
    SDL_snprintf(line, sizeof line, "retranslation:  %s",
                 (fs->retranslation[0] != '\0')
                     ? base_name(fs->retranslation) : "(not yet)");
    row = text(r, row, 2, line) + 1;
    colour(r, 0xFF, 0xFF, 0xFF);
    row = text(r, row, 2, "Drop them onto this window, together or one at "
                          "a time,");
    text(r, row, 2, "or press Enter (or click) to choose them.");
    colour(r, 0x90, 0x90, 0x90);
    text(r, FS_ROWS - 4, 0, "Escape leaves it: the game goes on as it was.");
}

static void draw(FirstStart *fs)
{
    SDL_Renderer *r = fs->renderer;
    colour(r, 0x08, 0x10, 0x30);
    SDL_RenderClear(r);
    if (fs->what == WHAT_GAME) {
        draw_game(r, fs);
    } else {
        draw_japanese(r, fs);
    }
    if (fs->note[0] != '\0') {
        colour(r, 0xFF, 0x90, 0x80);
        text(r, FS_ROWS - 2, 0, fs->note);
    }
    SDL_RenderPresent(r);
}

/* ---- the window ---- */

static bool open_window(FirstStart *fs, const char *title)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        fprintf(stderr, "first start: SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_CreateWindowAndRenderer(title, FS_W * FS_SCALE, FS_H * FS_SCALE,
                                     SDL_WINDOW_RESIZABLE, &fs->window,
                                     &fs->renderer)) {
        fprintf(stderr, "first start: no window: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_SetRenderLogicalPresentation(fs->renderer, FS_W, FS_H,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderVSync(fs->renderer, 1);
    SDL_RaiseWindow(fs->window);
    return true;
}

static void close_window(FirstStart *fs)
{
    SDL_DestroyRenderer(fs->renderer);
    SDL_DestroyWindow(fs->window);
    fs->renderer = NULL;
    fs->window   = NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

/* The window's loop until the files are made or the player leaves. The
 * events of any other window -- the game's, while a language is made --
 * are let go, but for quitting, which is handed back to the game. */
static bool run(FirstStart *fs)
{
    bool quit_seen = false;
    SDL_WindowID mine = SDL_GetWindowID(fs->window);
    while (fs->state != STATE_DONE && fs->state != STATE_LEFT) {
        SDL_Event ev;
        bool      have = SDL_WaitEventTimeout(&ev, 30);
        while (have) {
            switch (ev.type) {
                case SDL_EVENT_QUIT: {
                    quit_seen = true;
                    fs->state = STATE_LEFT;
                    break;
                }
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                    if (ev.window.windowID == mine) {
                        fs->state = STATE_LEFT;
                    }
                    break;
                }
                case SDL_EVENT_DROP_FILE: {
                    if (ev.drop.windowID == mine) {
                        offer(fs, ev.drop.data);
                    }
                    break;
                }
                case SDL_EVENT_KEY_DOWN: {
                    if (ev.key.windowID != mine || ev.key.repeat) {
                        break;
                    }
                    if (ev.key.key == SDLK_ESCAPE
                        && fs->state != STATE_RUN) {
                        fs->state = STATE_LEFT;
                    } else if (ev.key.key == SDLK_RETURN
                               || ev.key.key == SDLK_KP_ENTER
                               || ev.key.key == SDLK_SPACE) {
                        if (fs->state == STATE_DIFFERS) {
                            fs->state = STATE_DONE;
                        } else {
                            choose(fs);
                        }
                    }
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    if (ev.button.windowID == mine
                        && fs->state != STATE_DIFFERS) {
                        choose(fs);
                    }
                    break;
                }
                default: {
                    break;
                }
            }
            have = SDL_PollEvent(&ev);
        }
        if (SDL_GetAtomicInt(&fs->dialog_done) != 0) {
            SDL_SetAtomicInt(&fs->dialog_done, 0);
            SDL_SetAtomicInt(&fs->dialog_open, 0);
            if (fs->state != STATE_LEFT) {
                offer(fs, fs->dialog_path);
            }
        }
        if (fs->proc != NULL) {
            if (fs->state == STATE_LEFT) {
                stop_script(fs);
            } else {
                watch_script(fs);
            }
        }
        if (fs->state != STATE_DONE && fs->state != STATE_LEFT) {
            draw(fs);
        }
    }
    /* A dialog still open answers into nothing: wait for it to go. */
    while (SDL_GetAtomicInt(&fs->dialog_open) != 0
           && SDL_GetAtomicInt(&fs->dialog_done) == 0) {
        SDL_PumpEvents();
        SDL_Delay(20);
    }
    if (quit_seen) {
        SDL_Event q;
        SDL_zero(q);
        q.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&q);
    }
    return fs->state == STATE_DONE;
}

static FirstStart *first_start_new(What what, const char *lang_name)
{
    FirstStart *fs = SDL_calloc(1, sizeof *fs);
    if (fs != NULL) {
        fs->what      = what;
        fs->lang_name = lang_name;
        fs->state     = STATE_ASK;
    }
    return fs;
}

bool platform_make_assets(int nfiles, char **files)
{
    fprintf(stderr, "first start: no game files here (assets/manifest.txt); "
                    "asking for the cartridge\n");
    FirstStart *fs = first_start_new(WHAT_GAME, NULL);
    if (fs == NULL) {
        return false;
    }
    if (!open_window(fs, "Phantasy Star II")) {
        SDL_free(fs);
        return false;
    }
    /* Named on the command line: given to the script together (the
     * Japanese cartridge and the retranslation may come along). */
    char given[FS_FILES][FS_PATH];
    int  n = given_files(nfiles, files, given, FS_FILES);
    if (n > 0) {
        SDL_snprintf(fs->cartridge, sizeof fs->cartridge, "%s", given[0]);
        if (n > 1) {
            SDL_snprintf(fs->retranslation, sizeof fs->retranslation, "%s",
                         given[1]);
        }
        draw(fs);
        start_script(fs);
        if (fs->state == STATE_FAILED) {
            fs->cartridge[0]     = '\0';
            fs->retranslation[0] = '\0';
        }
    }
    bool made = run(fs);
    close_window(fs);
    SDL_free(fs);
    return made;
}

bool platform_add_language(const char *key, const char *name, int nfiles,
                           char **files)
{
    if (key == NULL || strcmp(key, "ja") != 0) {
        fprintf(stderr, "lang: %s's files are not here, and the game knows "
                        "no way to make them\n", (name != NULL) ? name : "?");
        return false;
    }
    fprintf(stderr, "first start: the %s language's files are not here; "
                    "asking for them\n", name);
    FirstStart *fs = first_start_new(WHAT_JAPANESE, name);
    if (fs == NULL) {
        return false;
    }
    char title[96];
    SDL_snprintf(title, sizeof title, "Phantasy Star II -- the %s language",
                 name);
    if (!open_window(fs, title)) {
        SDL_free(fs);
        return false;
    }
    char given[FS_FILES][FS_PATH];
    int  n = given_files(nfiles, files, given, FS_FILES);
    for (int i = 0; i < n && fs->state != STATE_RUN; i++) {
        offer(fs, given[i]);
    }
    bool made = run(fs);
    close_window(fs);
    SDL_free(fs);
    return made;
}
