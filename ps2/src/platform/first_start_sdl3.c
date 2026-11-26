/* (build 186) The first start, SDL3's (platform.h, docs/first_start.md).
 *
 * The game's files are made from the player's own cartridge. This is the
 * window that asks for it: an image dropped on it or chosen in the
 * system's file dialog, the files made from it with the steps shown as
 * they go, and what was said when they could not be. The game's own font
 * is one of the files to be made, so the window writes in SDL's built-in
 * 8x8 face (SDL_RenderDebugText), on an SDL_Renderer of its own; it
 * closes before the game's window opens, or -- adding a language --
 * stands over it while the game waits.
 *
 * (build 193) The files are made in C, in the game's own process:
 * src/extract/'s ex_make_* (tools/make_assets.py's twin, exmake.c), on a
 * thread of their own, so the window is drawn and its events read while a
 * step runs (the longest, the levels', takes a tenth of a second), and the
 * steps never wait for a frame to be shown. Until build 192 this window
 * ran the Python script in a Python it found (one packaged beside the
 * game, or the PATH's, with Pillow), a process beside it. Every path is
 * UTF-8, as SDL gives a dropped or chosen file's:
 * src/extract/exfs.c opens them by their wide names on Windows. */
#include "platform.h"

#include "home.h"
#include "../extract/extract.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define FS_SAID     512                     /* a line said, kept whole */
#define FS_TAIL     12                      /* the last lines said kept */
#define FS_FILES    4

/* What the files are made with: the map of the cartridge (where each
 * thing lies in it), beside the game. */
#define MAP         "tools/rom_map.json"

/* The run's status when the window was closed between its steps. */
#define RUN_STOPPED (-1)

typedef enum {
    WHAT_GAME,          /* the game's own files, from the US cartridge */
    WHAT_JAPANESE       /* the JAPANESE language */
} What;

typedef enum {
    STATE_ASK,          /* waiting for the files */
    STATE_RUN,          /* the files being made */
    STATE_FAILED,       /* they could not be: what was said, and ask again */
    STATE_DIFFERS,      /* made, but not all as proved: play on or quit */
    STATE_DONE,
    STATE_LEFT          /* the player closed it */
} State;

/* What the run has said: the last lines, and the step it is on. */
typedef struct {
    char tail[FS_TAIL][FS_SAID];
    int  tail_count;
    int  step;
    int  steps;
    char step_name[FS_LINE];
} Said;

typedef struct {
    What          what;
    const char   *lang_name;
    State         state;
    SDL_Window   *window;
    SDL_Renderer *renderer;

    /* The files to make them from: for the game one image, for the
     * JAPANESE language the cartridge and the retranslation. */
    char cartridge[FS_PATH];
    char retranslation[FS_PATH];

    /* The run, on its own thread (make_files). What it says goes into
     * `out` there, under `lock`; each frame copies it into `shown` under
     * the lock and draws from the copy, so the lock is held for a copy
     * and never while a frame is drawn. */
    SDL_Thread   *thread;
    SDL_Mutex    *lock;
    SDL_AtomicInt stop;                 /* the window closed: no more steps */
    SDL_AtomicInt ended;                /* the thread has its status */
    const char   *files[2];             /* cartridge, retranslation */
    int           nfiles;
    Said          out;
    Said          shown;
    int           exit_code;

    /* The file dialog, whose answer may come on another thread. */
    SDL_AtomicInt dialog_open;
    SDL_AtomicInt dialog_done;
    char          dialog_path[FS_PATH];

    char note[FS_LINE * 2];             /* one line under the instructions */
} FirstStart;

static bool path_is(const char *path, SDL_PathType type)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == type;
}

/* ---- the run ---- */

static void tail_push(Said *s, const char *line)
{
    if (s->tail_count == FS_TAIL) {
        memmove(s->tail[0], s->tail[1], sizeof s->tail[0] * (FS_TAIL - 1));
        s->tail_count--;
    }
    SDL_snprintf(s->tail[s->tail_count++], FS_SAID, "%s", line);
}

/* A line the run says (exmake.c's, make_assets.py's words), on the run's
 * thread: a step's `[K/N] WHAT`, or a sentence kept for the screen. Each
 * goes to the console too. */
static void said(void *user, const char *text)
{
    FirstStart *fs = user;
    char line[FS_SAID];
    SDL_snprintf(line, sizeof line, "%s", text);
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) {
        line[--n] = '\0';
    }
    fprintf(stderr, "make_assets: %s\n", line);
    SDL_LockMutex(fs->lock);
    bool step = false;
    if (line[0] == '[') {
        char *end = NULL;
        long  k   = SDL_strtol(line + 1, &end, 10);
        long  of  = 0;
        if (end != NULL && *end == '/') {
            of = SDL_strtol(end + 1, &end, 10);
        }
        if (of > 0 && end != NULL && end[0] == ']' && end[1] == ' ') {
            fs->out.step  = (int)k;
            fs->out.steps = (int)of;
            SDL_snprintf(fs->out.step_name, sizeof fs->out.step_name, "%s",
                         end + 2);
            step = true;
        }
    }
    if (!step && n > 0) {
        tail_push(&fs->out, line);
    }
    SDL_UnlockMutex(fs->lock);
}

/* The run's thread: every step, then the check, then its status --
 * EX_MAKE_*, or RUN_STOPPED when the window was closed between steps. */
static int SDLCALL make_files(void *user)
{
    FirstStart *fs = user;
    ExMakeArgs a;
    SDL_zero(a);
    a.game   = NULL;                    /* here: home_enter's folder */
    a.files  = fs->files;
    a.nfiles = fs->nfiles;
    a.say    = said;
    a.user   = fs;
    int status = EX_MAKE_FAILED;
    ExMake *m = ex_make_begin(&a, &status);
    if (m != NULL) {
        int  n       = ex_make_steps(m);
        bool stopped = false;
        for (int k = 0; k < n; k++) {
            if (SDL_GetAtomicInt(&fs->stop) != 0) {
                fprintf(stderr, "first start: stopped before step %d of "
                                "%d\n", k + 1, n);
                stopped = true;
                break;
            }
            if (!ex_make_step(m, k) && !ex_make_keep_going(m)) {
                break;
            }
        }
        status = stopped ? RUN_STOPPED : ex_make_finish(m);
        ex_make_end(m);
    }
    SDL_SetAtomicInt(&fs->ended, 1);
    return status;
}

/* (No run is going: the window's own thread alone touches `out`.) */
static bool start_make(FirstStart *fs)
{
    SDL_zero(fs->out);
    fs->note[0] = '\0';

    if (!path_is(MAP, SDL_PATHTYPE_FILE)) {
        tail_push(&fs->out, "This copy of the game has no " MAP ", the map "
                            "of the cartridge its files are made with.");
        fs->state = STATE_FAILED;
        return false;
    }
    fs->nfiles = 0;
    if (fs->cartridge[0] != '\0') {
        fs->files[fs->nfiles++] = fs->cartridge;
    }
    if (fs->retranslation[0] != '\0') {
        fs->files[fs->nfiles++] = fs->retranslation;
    }
    fprintf(stderr, "first start: making the files from %s%s%s\n",
            fs->cartridge, (fs->retranslation[0] != '\0') ? " and " : "",
            fs->retranslation);
    SDL_SetAtomicInt(&fs->stop, 0);
    SDL_SetAtomicInt(&fs->ended, 0);
    fs->thread = SDL_CreateThread(make_files, "first start", fs);
    if (fs->thread == NULL) {
        char line[FS_SAID];
        SDL_snprintf(line, sizeof line, "The game could not start making its "
                                        "files: %s", SDL_GetError());
        tail_push(&fs->out, line);
        fs->state = STATE_FAILED;
        return false;
    }
    fs->state = STATE_RUN;
    return true;
}

/* The run has ended: its status. */
static void end_make(FirstStart *fs)
{
    int code = EX_MAKE_FAILED;
    SDL_WaitThread(fs->thread, &code);
    fs->thread    = NULL;
    fs->exit_code = code;
    fprintf(stderr, "first start: the run ended, status %d\n", code);
    if (code == EX_MAKE_DONE) {
        fs->state = STATE_DONE;
    } else if (code == EX_MAKE_DIFFERS) {
        fs->state = STATE_DIFFERS;
    } else {
        fs->state = STATE_FAILED;
        /* Asked again from the start: the next files are a new pair. */
        fs->cartridge[0]     = '\0';
        fs->retranslation[0] = '\0';
    }
}

/* Closed mid-run: the steps stop where they are, after the one running.
 * The preload list is the last step, so a half-made assets/ is never
 * taken for a whole one, and the next start asks again. */
static void stop_make(FirstStart *fs)
{
    if (fs->thread == NULL) {
        return;
    }
    SDL_SetAtomicInt(&fs->stop, 1);
    int code = 0;
    SDL_WaitThread(fs->thread, &code);
    fs->thread = NULL;
    fprintf(stderr, "first start: the run stopped, status %d\n", code);
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
 * work/dialogue.json (its names are stored uncompressed). exmake.c
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
        start_make(fs);
        return;
    }
    if (looks_like_retranslation(path)) {
        SDL_snprintf(fs->retranslation, sizeof fs->retranslation, "%s", path);
    } else {
        SDL_snprintf(fs->cartridge, sizeof fs->cartridge, "%s", path);
    }
    fs->state = STATE_ASK;
    if (fs->cartridge[0] != '\0' && fs->retranslation[0] != '\0') {
        start_make(fs);
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

/* The command line's files, as UTF-8 (on Windows read again from the wide
 * command line: main() has them in the ANSI code page, which cannot hold
 * every name; ex_utf8_args), each read from the folder the game was
 * started in (home_enter may have left it). */
static int given_files(int nfiles, char **files, char out[][FS_PATH],
                       int max)
{
    char **utf8 = ex_utf8_args(nfiles, files);
    char **from = (utf8 != NULL) ? utf8 : files;
    int n = 0;
    for (; n < nfiles && n < max; n++) {
        home_given_path(from[n], out[n], FS_PATH);
    }
    free(utf8);
    return n;
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
    int from = (fs->shown.tail_count > max) ? fs->shown.tail_count - max : 0;
    for (int i = from; i < fs->shown.tail_count && row < FS_ROWS - 7; i++) {
        row = text(r, row, 2, fs->shown.tail[i]);
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
            bar(r, row, fs->shown.step, fs->shown.steps);
            row += 2;
            char line[FS_LINE * 2];
            SDL_snprintf(line, sizeof line, "step %d of %d: %s",
                         fs->shown.step, fs->shown.steps,
                         fs->shown.step_name);
            text(r, row, 0, (fs->shown.steps > 0) ? line : "starting...");
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
        bar(r, row, fs->shown.step, fs->shown.steps);
        row += 2;
        char line[FS_LINE * 2];
        SDL_snprintf(line, sizeof line, "step %d of %d: %s", fs->shown.step,
                     fs->shown.steps, fs->shown.step_name);
        text(r, row, 0, (fs->shown.steps > 0) ? line : "starting...");
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
    SDL_LockMutex(fs->lock);
    fs->shown = fs->out;
    SDL_UnlockMutex(fs->lock);
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
        if (fs->thread != NULL) {
            if (fs->state == STATE_LEFT) {
                stop_make(fs);
            } else if (SDL_GetAtomicInt(&fs->ended) != 0) {
                end_make(fs);
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
    if (fs == NULL) {
        return NULL;
    }
    fs->lock = SDL_CreateMutex();
    if (fs->lock == NULL) {
        SDL_free(fs);
        return NULL;
    }
    fs->what      = what;
    fs->lang_name = lang_name;
    fs->state     = STATE_ASK;
    return fs;
}

static void first_start_free(FirstStart *fs)
{
    stop_make(fs);
    SDL_DestroyMutex(fs->lock);
    SDL_free(fs);
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
        first_start_free(fs);
        return false;
    }
    /* Named on the command line: given together (the Japanese cartridge
     * and the retranslation may come along). */
    char given[FS_FILES][FS_PATH];
    int  n = given_files(nfiles, files, given, FS_FILES);
    if (n > 0) {
        SDL_snprintf(fs->cartridge, sizeof fs->cartridge, "%s", given[0]);
        if (n > 1) {
            SDL_snprintf(fs->retranslation, sizeof fs->retranslation, "%s",
                         given[1]);
        }
        draw(fs);
        start_make(fs);
        if (fs->state == STATE_FAILED) {
            fs->cartridge[0]     = '\0';
            fs->retranslation[0] = '\0';
        }
    }
    bool made = run(fs);
    close_window(fs);
    first_start_free(fs);
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
        first_start_free(fs);
        return false;
    }
    char given[FS_FILES][FS_PATH];
    int  n = given_files(nfiles, files, given, FS_FILES);
    for (int i = 0; i < n && fs->state != STATE_RUN; i++) {
        offer(fs, given[i]);
    }
    bool made = run(fs);
    close_window(fs);
    first_start_free(fs);
    return made;
}
